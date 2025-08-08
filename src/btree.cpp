// =============================================================================
// btree.cpp
#include "btree.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <iomanip>
#include <queue>
#include <vector>



// Helper functions to access arrays within the node's data area
static uint32_t *get_keys(BTreeNode *node) {
  return reinterpret_cast<uint32_t *>(node->data);
}

static uint32_t *get_children(BTreeNode *node) {
  // Children array comes after keys array in internal nodes
  return reinterpret_cast<uint32_t *>(node->data +
                                      node->max_keys * sizeof(uint32_t));
}

static uint8_t *get_record_data(BTreeNode *node) {
  // Record data comes after keys array in leaf nodes
  return node->data + node->max_keys * sizeof(uint32_t);
}

static uint8_t *get_record_at(BTreeNode *node, uint32_t index) {
  if (!node->is_leaf || index >= node->num_keys)
    return nullptr;
  return get_record_data(node) + (index * node->record_size);
}

// Forward declaration
static bool bp_find_in_tree(BTreeNode *node, uint32_t key);

BPlusTreeCapacity bp_calculate_capacity(const std::vector<ColumnInfo> &schema) {
  constexpr uint32_t USABLE_SPACE = PAGE_SIZE - NODE_HEADER_SIZE; // 4064 bytes

  // Calculate total record size
  uint32_t record_size = 0;
  for (const auto &col : schema) {
    record_size += col.type;
  }

  // For leaf nodes: keys + records
  uint32_t leaf_entry_size = sizeof(uint32_t) + record_size;
  uint32_t leaf_max_keys = std::floor(USABLE_SPACE / leaf_entry_size);

  // For internal nodes: keys + children
  // FIXED: Account for n+1 children in the calculation
  // Total space needed: n * sizeof(key) + (n+1) * sizeof(child)
  // = n * 4 + (n+1) * 4 = n * 8 + 4
  uint32_t internal_max_keys =
      (USABLE_SPACE - sizeof(uint32_t)) / (2 * sizeof(uint32_t));

  // Actually verify the calculation:
  // For n keys: keys_size = n * 4, children_size = (n+1) * 4
  // Total = n * 4 + (n+1) * 4 = n * 8 + 4
  // So: n * 8 + 4 <= USABLE_SPACE
  // n <= (USABLE_SPACE - 4) / 8

  // Double-check our math:
  uint32_t keys_space = internal_max_keys * sizeof(uint32_t);
  uint32_t children_space = (internal_max_keys + 1) * sizeof(uint32_t);
  uint32_t total_space = keys_space + children_space;

  // If total_space > USABLE_SPACE, reduce internal_max_keys
  while (total_space > USABLE_SPACE && internal_max_keys > 3) {
    internal_max_keys--;
    keys_space = internal_max_keys * sizeof(uint32_t);
    children_space = (internal_max_keys + 1) * sizeof(uint32_t);
    total_space = keys_space + children_space;
  }

  // Ensure we have at least 3 keys for proper splitting
  leaf_max_keys = std::max(3U, leaf_max_keys);
  internal_max_keys = std::max(3U, internal_max_keys);

  uint32_t leaf_min_keys = leaf_max_keys / 2;
  uint32_t internal_min_keys = internal_max_keys / 2;

  return {std::min(leaf_max_keys, internal_max_keys),
          std::max(1U, std::min(leaf_min_keys, internal_min_keys))};
}

BPlusTree bp_create(const std::vector<ColumnInfo> &schema) {
  BPlusTree tree;

  auto capacity = bp_calculate_capacity(schema);

  // Calculate record size
  uint32_t record_size = 0;
  for (const auto &col : schema) {
    record_size += col.type;
  }

  // Recalculate specific capacities for internal and leaf nodes
  const uint32_t USABLE_SPACE = PAGE_SIZE - 32;

  // Leaf nodes store keys + records
  tree.leaf_max_keys = USABLE_SPACE / (sizeof(uint32_t) + record_size);
  tree.leaf_max_keys = std::max(2U, tree.leaf_max_keys);
  tree.leaf_min_keys = tree.leaf_max_keys / 2;
  tree.leaf_split_index = tree.leaf_max_keys / 2;

  // Internal nodes store keys + child pointers
  tree.internal_max_keys =
      (USABLE_SPACE - sizeof(uint32_t)) / (2 * sizeof(uint32_t));
  tree.internal_max_keys = std::max(3U, tree.internal_max_keys);
  tree.internal_min_keys = tree.internal_max_keys / 2;
  tree.internal_split_index = tree.internal_max_keys / 2;

  tree.record_size = record_size;
  tree.root_page_index = 0;

  return tree;
}

// Need some special init or creat
void bp_init(BPlusTree &tree) {
  if (tree.root_page_index == 0) {
    BTreeNode *root = bp_create_node(tree, true);
    if (root) {
      tree.root_page_index = root->index;
    }
  }
}

void bp_reset(BPlusTree &tree) {
  tree.internal_max_keys = 100;
  tree.leaf_max_keys = 50;
  tree.internal_min_keys = 50;
  tree.leaf_min_keys = 25;
  tree.internal_split_index = 50;
  tree.leaf_split_index = 25;
  tree.root_page_index = 0;
  tree.record_size = 0;
}

BTreeNode *bp_create_node(BPlusTree &tree, bool is_leaf) {
  uint32_t page_index = pager_new();
  BTreeNode *node = static_cast<BTreeNode *>(pager_get(page_index));

  if (!node) {
    return nullptr;
  }

  // Mark dirty BEFORE modifying
  pager_mark_dirty(page_index);

  // Initialize the node
  node->index = page_index;
  node->parent = 0;
  node->next = 0;
  node->previous = 0;
  node->num_keys = 0;
  node->is_leaf = is_leaf ? 1 : 0;
  node->max_keys = is_leaf ? tree.leaf_max_keys : tree.internal_max_keys;
  node->record_size = is_leaf ? tree.record_size : 0;

  // Zero out the data area
  memset(node->data, 0, sizeof(node->data));

  return node;
}

void bp_destroy_node(BTreeNode *node) {
  if (!node)
    return;

  // Update linked list pointers before destroying
  if (node->is_leaf) {
    if (node->previous != 0) {
      BTreeNode *prev_node = bp_get_prev(node);
      if (prev_node) {
        bp_set_next(prev_node, node->next);
      }
    }

    if (node->next != 0) {
      BTreeNode *next_node = bp_get_next(node);
      if (next_node) {
        bp_set_prev(next_node, node->previous);
      }
    }
  }

  pager_delete(node->index);
}

void bp_mark_dirty(BTreeNode *node) {
  if (node) {
    pager_mark_dirty(node->index);
  }
}

BTreeNode *bp_get_parent(BTreeNode *node) {
  if (!node || node->parent == 0)
    return nullptr;
  return static_cast<BTreeNode *>(pager_get(node->parent));
}

BTreeNode *bp_get_child(BTreeNode *node, uint32_t index) {
  if (!node || node->is_leaf)
    return nullptr;

  uint32_t *children = get_children(node);
  if (index >= node->num_keys + 1 || children[index] == 0) {
    return nullptr;
  }

  return static_cast<BTreeNode *>(pager_get(children[index]));
}

BTreeNode *bp_get_next(BTreeNode *node) {
  if (!node || node->next == 0)
    return nullptr;
  return static_cast<BTreeNode *>(pager_get(node->next));
}

BTreeNode *bp_get_prev(BTreeNode *node) {
  if (!node || node->previous == 0)
    return nullptr;
  return static_cast<BTreeNode *>(pager_get(node->previous));
}

void bp_set_parent(BTreeNode *node, uint32_t parent_index) {
  if (!node)
    return;

  // Mark dirty BEFORE modifying
  bp_mark_dirty(node);

  node->parent = parent_index;

  if (parent_index != 0) {
    pager_mark_dirty(parent_index);
  }
}

void bp_set_child(BTreeNode *node, uint32_t child_index, uint32_t node_index) {
  if (!node || node->is_leaf)
    return;

  // Mark dirty BEFORE modifying
  bp_mark_dirty(node);

  uint32_t *children = get_children(node);
  children[child_index] = node_index;

  if (node_index != 0) {
    BTreeNode *child_node = static_cast<BTreeNode *>(pager_get(node_index));
    if (child_node) {
      bp_set_parent(child_node, node->index);
    }
  }
}

void bp_set_next(BTreeNode *node, uint32_t index) {
  if (!node)
    return;

  // Mark dirty BEFORE modifying
  bp_mark_dirty(node);

  node->next = index;

  if (index != 0) {
    pager_mark_dirty(index);
  }
}

void bp_set_prev(BTreeNode *node, uint32_t index) {
  if (!node)
    return;

  // Mark dirty BEFORE modifying
  bp_mark_dirty(node);

  node->previous = index;

  if (index != 0) {
    pager_mark_dirty(index);
  }
}

uint32_t bp_get_max_keys(const BPlusTree &tree, const BTreeNode *node) {
  return node->is_leaf ? tree.leaf_max_keys : tree.internal_max_keys;
}

uint32_t bp_get_min_keys(const BPlusTree &tree, const BTreeNode *node) {
  return node->is_leaf ? tree.leaf_min_keys : tree.internal_min_keys;
}

uint32_t bp_get_split_index(const BPlusTree &tree, const BTreeNode *node) {
  return node->is_leaf ? tree.leaf_split_index : tree.internal_split_index;
}

BTreeNode *bp_get_root(const BPlusTree &tree) {
  return static_cast<BTreeNode *>(pager_get(tree.root_page_index));
}

void bp_insert_element(BPlusTree &tree, uint32_t key, const uint8_t *data) {
  BTreeNode *root = bp_get_root(tree);

  if (root->num_keys == 0) {
    // Mark dirty BEFORE modifying
    bp_mark_dirty(root);

    uint32_t *keys = get_keys(root);
    uint8_t *record_data = get_record_data(root);

    keys[0] = key;
    memcpy(record_data, data, tree.record_size);
    root->num_keys = 1;
  } else {
    bp_insert(tree, root, key, data);
  }

  pager_sync();
}

void bp_insert(BPlusTree &tree, BTreeNode *node, uint32_t key,
               const uint8_t *data) {
  if (node->is_leaf) {
    uint32_t *keys = get_keys(node);
    uint8_t *record_data = get_record_data(node);

    // Check for update
    for (uint32_t i = 0; i < node->num_keys; i++) {
      if (keys[i] == key) {
        // Mark dirty BEFORE modifying
        bp_mark_dirty(node);
        memcpy(record_data + i * tree.record_size, data, tree.record_size);
        return;
      }
    }

    // Mark dirty BEFORE modifying (for insertion)
    bp_mark_dirty(node);

    // Insert into leaf
    node->num_keys++;
    uint32_t insert_index = node->num_keys - 1;

    while (insert_index > 0 && keys[insert_index - 1] > key) {
      keys[insert_index] = keys[insert_index - 1];

      memcpy(record_data + insert_index * tree.record_size,
             record_data + (insert_index - 1) * tree.record_size,
             tree.record_size);
      insert_index--;
    }

    keys[insert_index] = key;
    memcpy(record_data + insert_index * tree.record_size, data,
           tree.record_size);

    bp_insert_repair(tree, node);
  } else {
    // Find correct child to insert into
    uint32_t *keys = get_keys(node);
    uint32_t find_index = 0;

    while (find_index < node->num_keys && keys[find_index] < key) {
      find_index++;
    }

    BTreeNode *child_node = bp_get_child(node, find_index);
    if (child_node) {
      bp_insert(tree, child_node, key, data);
    }
  }
}

void bp_insert_repair(BPlusTree &tree, BTreeNode *node) {
  if (node->num_keys <= bp_get_max_keys(tree, node) - 1) {
    return;
  } else if (node->parent == 0) {
    BTreeNode *new_root = bp_split(tree, node);
    tree.root_page_index = new_root->index;
  } else {
    BTreeNode *new_node = bp_split(tree, node);
    bp_insert_repair(tree, new_node);
  }
}

BTreeNode *bp_split(BPlusTree &tree, BTreeNode *node) {
  BTreeNode *right_node = bp_create_node(tree, node->is_leaf);

  uint32_t split_index = bp_get_split_index(tree, node);

  uint32_t *node_keys = get_keys(node);
  uint32_t rising_key = node_keys[split_index];

  uint32_t parent_index = 0;

  if (node->parent != 0) {
    BTreeNode *current_parent = bp_get_parent(node);

    uint32_t *parent_keys = get_keys(current_parent);
    uint32_t *parent_children = get_children(current_parent);

    // Find parent index
    for (parent_index = 0; parent_index < current_parent->num_keys + 1 &&
                           parent_children[parent_index] != node->index;
         parent_index++)
      ;

    if (parent_index == current_parent->num_keys + 1) {
      throw std::runtime_error("Couldn't find which child we were!");
    }

    // Mark dirty BEFORE modifying parent
    bp_mark_dirty(current_parent);

    // Shift parent keys and children
    for (uint32_t i = current_parent->num_keys; i > parent_index; i--) {
      parent_children[i + 1] = parent_children[i];
      parent_keys[i] = parent_keys[i - 1];
    }

    current_parent->num_keys++;
    parent_keys[parent_index] = rising_key;
    bp_set_child(current_parent, parent_index + 1, right_node->index);
  }

  uint32_t right_split = node->is_leaf ? split_index : split_index + 1;

  if (node->is_leaf) {
    // Set up doubly-linked list
    bp_set_prev(right_node, node->index);
    bp_set_next(right_node, node->next);

    if (node->next != 0) {
      BTreeNode *next_node = bp_get_next(node);
      if (next_node) {
        bp_set_prev(next_node, right_node->index);
      }
    }

    bp_set_next(node, right_node->index);
  }

  // Mark dirty BEFORE modifying right_node and node
  bp_mark_dirty(right_node);
  bp_mark_dirty(node);

  right_node->num_keys = node->num_keys - right_split;

  uint32_t *right_keys = get_keys(right_node);
  uint32_t *node_children = get_children(node);
  uint32_t *right_children = get_children(right_node);

  // Move children to right node (internal nodes only)
  if (!node->is_leaf) {
    for (uint32_t i = right_split; i < node->num_keys + 1; i++) {
      bp_set_child(right_node, i - right_split, node_children[i]);
      node_children[i] = 0;
    }
  }

  // Move keys and data to right node
  for (uint32_t i = right_split; i < node->num_keys; i++) {
    right_keys[i - right_split] = node_keys[i];
  }

  // Copy record data for leaf nodes
  if (node->is_leaf) {
    uint8_t *node_records = get_record_data(node);
    uint8_t *right_records = get_record_data(right_node);

    memcpy(right_records, node_records + right_split * tree.record_size,
           right_node->num_keys * tree.record_size);
  }

  node->num_keys = split_index;

  if (node->parent != 0) {
    return bp_get_parent(node);
  } else {
    // Create new root
    BTreeNode *new_root = bp_create_node(tree, false);
    uint32_t *new_root_keys = get_keys(new_root);

    new_root_keys[0] = rising_key;
    new_root->num_keys = 1;
    bp_set_child(new_root, 0, node->index);
    bp_set_child(new_root, 1, right_node->index);
    return new_root;
  }
}

bool bp_find_element(const BPlusTree &tree, uint32_t key) {
  BTreeNode *root = bp_get_root(tree);
  return bp_find_in_tree(root, key);
}

static bool bp_find_in_tree(BTreeNode *node, uint32_t key) {
  if (!node)
    return false;

  uint32_t *keys = get_keys(node);
  uint32_t i;

  for (i = 0; i < node->num_keys && keys[i] < key; i++)
    ;

  if (i == node->num_keys) {
    if (!node->is_leaf) {
      return bp_find_in_tree(bp_get_child(node, node->num_keys), key);
    } else {
      return false;
    }
  } else if (keys[i] > key) {
    if (!node->is_leaf) {
      return bp_find_in_tree(bp_get_child(node, i), key);
    } else {
      return false;
    }
  } else {
    if (keys[i] == key && node->is_leaf) {
      return true;
    } else {
      return bp_find_in_tree(bp_get_child(node, i + 1), key);
    }
  }
}

const uint8_t *bp_get(const BPlusTree &tree, uint32_t key) {
  BTreeNode *root = bp_get_root(tree);
  BTreeNode *leaf_node = bp_find_leaf_node(root, key);
  if (!leaf_node)
    return nullptr;

  uint32_t *keys = get_keys(leaf_node);

  for (uint32_t i = 0; i < leaf_node->num_keys; i++) {
    if (keys[i] == key) {
      return get_record_at(leaf_node, i);
    }
  }

  return nullptr;
}

BTreeNode *bp_find_leaf_node(BTreeNode *node, uint32_t key) {
  if (node->is_leaf) {
    return node;
  }

  uint32_t *keys = get_keys(node);
  uint32_t i;

  for (i = 0; i < node->num_keys && keys[i] < key; i++)
    ;

  if (i == node->num_keys) {
    return bp_find_leaf_node(bp_get_child(node, node->num_keys), key);
  } else if (keys[i] > key) {
    return bp_find_leaf_node(bp_get_child(node, i), key);
  } else {
    return bp_find_leaf_node(bp_get_child(node, i + 1), key);
  }
}

BTreeNode *bp_left_most(const BPlusTree &tree) {
  BTreeNode *temp = bp_get_root(tree);

  while (temp && !temp->is_leaf) {
    temp = bp_get_child(temp, 0);
  }

  return temp;
}

std::vector<std::pair<uint32_t, const uint8_t *>>
bp_print_leaves(const BPlusTree &tree) {
  BTreeNode *start = bp_left_most(tree);
  std::vector<std::pair<uint32_t, const uint8_t *>> result;
  BTreeNode *temp = start;

  while (temp) {
    uint32_t *keys = get_keys(temp);

    for (uint32_t i = 0; i < temp->num_keys; i++) {
      result.push_back({keys[i], get_record_at(temp, i)});
    }
    temp = bp_get_next(temp);
  }

  return result;
}

void bp_delete_element(BPlusTree &tree, uint32_t key) {
  BTreeNode *root = bp_get_root(tree);
  if (!root)
    return;

  bp_do_delete(tree, root, key);

  if (root->num_keys == 0 && !root->is_leaf) {
    BTreeNode *old_root = root;
    BTreeNode *new_root = bp_get_child(root, 0);
    if (new_root) {
      tree.root_page_index = new_root->index;
      bp_set_parent(new_root, 0);
    }
    bp_destroy_node(old_root);
  }

  pager_sync();
}

void bp_do_delete(BPlusTree &tree, BTreeNode *node, uint32_t key) {
  if (!node)
    return;

  uint32_t *keys = get_keys(node);
  uint32_t i;

  for (i = 0; i < node->num_keys && keys[i] < key; i++)
    ;

  if (i == node->num_keys) {
    if (!node->is_leaf) {
      bp_do_delete(tree, bp_get_child(node, node->num_keys), key);
    }
  } else if (!node->is_leaf && keys[i] == key) {
    bp_do_delete(tree, bp_get_child(node, i + 1), key);
  } else if (!node->is_leaf) {
    bp_do_delete(tree, bp_get_child(node, i), key);
  } else if (node->is_leaf && keys[i] == key) {
    // Mark dirty BEFORE modifying
    bp_mark_dirty(node);

    // Delete from leaf
    uint8_t *record_data = get_record_data(node);

    // Shift keys and data left
    for (uint32_t j = i; j < node->num_keys - 1; j++) {
      keys[j] = keys[j + 1];
      memcpy(record_data + j * tree.record_size,
             record_data + (j + 1) * tree.record_size, tree.record_size);
    }
    node->num_keys--;

    // Update parent keys if we removed the smallest element
    if (i == 0 && node->parent != 0) {
      bp_update_parent_keys(tree, node, key);
    }

    bp_repair_after_delete(tree, node);
  }
}

void bp_update_parent_keys(BPlusTree &tree, BTreeNode *node,
                           uint32_t deleted_key) {
  uint32_t next_smallest = 0;
  BTreeNode *parent_node = bp_get_parent(node);
  if (!parent_node)
    return;

  uint32_t *parent_children = get_children(parent_node);
  uint32_t parent_index;

  for (parent_index = 0; parent_children[parent_index] != node->index;
       parent_index++)
    ;

  if (node->num_keys == 0) {
    if (parent_index == parent_node->num_keys) {
      next_smallest = 0;
    } else {
      BTreeNode *next_sibling = bp_get_child(parent_node, parent_index + 1);
      if (next_sibling) {
        uint32_t *sibling_keys = get_keys(next_sibling);
        next_smallest = sibling_keys[0];
      }
    }
  } else {
    uint32_t *node_keys = get_keys(node);
    next_smallest = node_keys[0];
  }

  BTreeNode *current_parent = parent_node;
  while (current_parent) {
    uint32_t *current_keys = get_keys(current_parent);
    if (parent_index > 0 && current_keys[parent_index - 1] == deleted_key) {
      // Mark dirty BEFORE modifying
      bp_mark_dirty(current_parent);
      current_keys[parent_index - 1] = next_smallest;
    }

    BTreeNode *grandparent = bp_get_parent(current_parent);
    if (grandparent) {
      uint32_t *grandparent_children = get_children(grandparent);
      for (parent_index = 0;
           grandparent_children[parent_index] != current_parent->index;
           parent_index++)
        ;
    }
    current_parent = grandparent;
  }
}

void bp_repair_after_delete(BPlusTree &tree, BTreeNode *node) {
  if (node->num_keys < bp_get_min_keys(tree, node)) {
    if (node->parent == 0) {
      if (node->num_keys == 0 && !node->is_leaf) {
        BTreeNode *new_root = bp_get_child(node, 0);
        if (new_root) {
          tree.root_page_index = new_root->index;
          bp_set_parent(new_root, 0);
        }
        bp_destroy_node(node);
      }
    } else {
      BTreeNode *parent_node = bp_get_parent(node);
      uint32_t *parent_children = get_children(parent_node);
      uint32_t parent_index;

      for (parent_index = 0; parent_children[parent_index] != node->index;
           parent_index++)
        ;

      BTreeNode *left_sibling =
          (parent_index > 0) ? bp_get_child(parent_node, parent_index - 1)
                             : nullptr;
      BTreeNode *right_sibling =
          (parent_index < parent_node->num_keys)
              ? bp_get_child(parent_node, parent_index + 1)
              : nullptr;

      if (left_sibling &&
          left_sibling->num_keys > bp_get_min_keys(tree, left_sibling)) {
        bp_steal_from_left(tree, node, parent_index);
      } else if (right_sibling && right_sibling->num_keys >
                                      bp_get_min_keys(tree, right_sibling)) {
        bp_steal_from_right(tree, node, parent_index);
      } else if (parent_index == 0 && right_sibling) {
        BTreeNode *next_node = bp_merge_right(tree, node);
        bp_repair_after_delete(tree, bp_get_parent(next_node));
      } else if (left_sibling) {
        BTreeNode *next_node = bp_merge_right(tree, left_sibling);
        bp_repair_after_delete(tree, bp_get_parent(next_node));
      }
    }
  }
}

BTreeNode *bp_merge_right(BPlusTree &tree, BTreeNode *node) {
  BTreeNode *parent_node = bp_get_parent(node);
  uint32_t *parent_children = get_children(parent_node);
  uint32_t *parent_keys = get_keys(parent_node);
  uint32_t parent_index = 0;

  for (parent_index = 0; parent_children[parent_index] != node->index;
       parent_index++)
    ;

  BTreeNode *right_sib = bp_get_child(parent_node, parent_index + 1);
  uint32_t *node_keys = get_keys(node);
  uint32_t *right_keys = get_keys(right_sib);
  uint32_t *node_children = get_children(node);
  uint32_t *right_children = get_children(right_sib);

  // Mark dirty BEFORE modifying
  bp_mark_dirty(node);

  if (!node->is_leaf) {
    node_keys[node->num_keys] = parent_keys[parent_index];
  }

  for (uint32_t i = 0; i < right_sib->num_keys; i++) {
    uint32_t insert_index = node->num_keys + 1 + i;
    if (node->is_leaf) {
      insert_index -= 1;
    }
    node_keys[insert_index] = right_keys[i];
  }

  if (!node->is_leaf) {
    for (uint32_t i = 0; i <= right_sib->num_keys; i++) {
      bp_set_child(node, node->num_keys + 1 + i, right_children[i]);
    }
    node->num_keys = node->num_keys + right_sib->num_keys + 1;
  } else {
    // Copy record data for leaf nodes
    uint8_t *node_records = get_record_data(node);
    uint8_t *right_records = get_record_data(right_sib);

    memcpy(node_records + node->num_keys * tree.record_size, right_records,
           right_sib->num_keys * tree.record_size);

    node->num_keys = node->num_keys + right_sib->num_keys;

    // Update doubly-linked list
    bp_set_next(node, right_sib->next);

    if (right_sib->next != 0) {
      BTreeNode *next_node = bp_get_next(right_sib);
      if (next_node) {
        bp_set_prev(next_node, node->index);
      }
    }
  }

  // Mark dirty BEFORE modifying parent
  bp_mark_dirty(parent_node);

  // Remove right sibling from parent
  for (uint32_t i = parent_index + 1; i < parent_node->num_keys; i++) {
    parent_children[i] = parent_children[i + 1];
    parent_keys[i - 1] = parent_keys[i];
  }
  parent_node->num_keys--;

  bp_destroy_node(right_sib);
  return node;
}

BTreeNode *bp_steal_from_right(BPlusTree &tree, BTreeNode *node,
                               uint32_t parent_index) {
  BTreeNode *parent_node = bp_get_parent(node);
  BTreeNode *right_sib = bp_get_child(parent_node, parent_index + 1);

  uint32_t *node_keys = get_keys(node);
  uint32_t *right_keys = get_keys(right_sib);
  uint32_t *parent_keys = get_keys(parent_node);
  uint32_t *node_children = get_children(node);
  uint32_t *right_children = get_children(right_sib);

  // Mark dirty BEFORE modifying
  bp_mark_dirty(node);
  bp_mark_dirty(right_sib);
  bp_mark_dirty(parent_node);

  node->num_keys++;

  if (node->is_leaf) {
    node_keys[node->num_keys - 1] = right_keys[0];

    // Copy record data
    uint8_t *node_records = get_record_data(node);
    uint8_t *right_records = get_record_data(right_sib);

    memcpy(node_records + (node->num_keys - 1) * tree.record_size,
           right_records, tree.record_size);

    parent_keys[parent_index] = right_keys[1];

    // Shift right sibling data left
    for (uint32_t i = 1; i < right_sib->num_keys; i++) {
      right_keys[i - 1] = right_keys[i];
      memcpy(right_records + (i - 1) * tree.record_size,
             right_records + i * tree.record_size, tree.record_size);
    }
  } else {
    node_keys[node->num_keys - 1] = parent_keys[parent_index];
    parent_keys[parent_index] = right_keys[0];

    bp_set_child(node, node->num_keys, right_children[0]);

    for (uint32_t i = 1; i < right_sib->num_keys + 1; i++) {
      right_children[i - 1] = right_children[i];
    }

    for (uint32_t i = 1; i < right_sib->num_keys; i++) {
      right_keys[i - 1] = right_keys[i];
    }
  }

  right_sib->num_keys--;

  return node;
}

BTreeNode *bp_steal_from_left(BPlusTree &tree, BTreeNode *node,
                              uint32_t parent_index) {
  BTreeNode *parent_node = bp_get_parent(node);
  BTreeNode *left_sib = bp_get_child(parent_node, parent_index - 1);

  uint32_t *node_keys = get_keys(node);
  uint32_t *left_keys = get_keys(left_sib);
  uint32_t *parent_keys = get_keys(parent_node);
  uint32_t *node_children = get_children(node);
  uint32_t *left_children = get_children(left_sib);

  // Mark dirty BEFORE modifying
  bp_mark_dirty(node);
  bp_mark_dirty(left_sib);
  bp_mark_dirty(parent_node);

  node->num_keys++;

  // Shift all keys in node to the right
  for (uint32_t i = node->num_keys - 1; i > 0; i--) {
    node_keys[i] = node_keys[i - 1];
  }

  if (node->is_leaf) {
    // Shift record data right
    uint8_t *node_records = get_record_data(node);
    uint8_t *left_records = get_record_data(left_sib);

    for (uint32_t i = node->num_keys - 1; i > 0; i--) {
      memcpy(node_records + i * tree.record_size,
             node_records + (i - 1) * tree.record_size, tree.record_size);
    }

    node_keys[0] = left_keys[left_sib->num_keys - 1];
    memcpy(node_records,
           left_records + (left_sib->num_keys - 1) * tree.record_size,
           tree.record_size);

    parent_keys[parent_index - 1] = left_keys[left_sib->num_keys - 1];
  } else {
    node_keys[0] = parent_keys[parent_index - 1];
    parent_keys[parent_index - 1] = left_keys[left_sib->num_keys - 1];

    for (uint32_t i = node->num_keys; i > 0; i--) {
      node_children[i] = node_children[i - 1];
    }
    bp_set_child(node, 0, left_children[left_sib->num_keys]);
    left_children[left_sib->num_keys] = 0;
  }

  left_sib->num_keys--;

  return node;
}



/*----------DEBUG----------- */





void bp_debug_print_tree(const BPlusTree& tree) {
    if (tree.root_page_index == 0) {
        std::cout << "Tree is empty (no root)\n";
        return;
    }

    BTreeNode* root = bp_get_root(tree);
    if (!root) {
        std::cout << "Failed to get root node\n";
        return;
    }

    std::cout << "=== B+ TREE DEBUG VISUALIZATION ===\n";
    std::cout << "Tree Configuration:\n";
    std::cout << "  Internal max_keys: " << tree.internal_max_keys
              << ", min_keys: " << tree.internal_min_keys << "\n";
    std::cout << "  Leaf max_keys: " << tree.leaf_max_keys
              << ", min_keys: " << tree.leaf_min_keys << "\n";
    std::cout << "  Record size: " << tree.record_size << " bytes\n";
    std::cout << "  Root page: " << tree.root_page_index << "\n\n";

    // Level-order traversal to print tree structure
    struct NodeInfo {
        BTreeNode* node;
        int level;
        int position_in_level;
    };

    std::queue<NodeInfo> node_queue;
    node_queue.push({root, 0, 0});

    int current_level = -1;
    int position_counter = 0;

    while (!node_queue.empty()) {
        NodeInfo info = node_queue.front();
        node_queue.pop();

        BTreeNode* node = info.node;
        if (!node) continue;

        // Print level header when we encounter a new level
        if (info.level != current_level) {
            if (current_level >= 0) {
                std::cout << "\n";
            }
            current_level = info.level;
            position_counter = 0;
            std::cout << "LEVEL " << current_level << ":\n";
            std::cout << std::string(80, '-') << "\n";
        }

        // Print node information
        std::cout << "Node[" << position_counter << "] (Page " << node->index << "): ";

        if (node->is_leaf) {
            std::cout << "LEAF";
        } else {
            std::cout << "INTERNAL";
        }

        std::cout << " | Parent: ";
        if (node->parent == 0) {
            std::cout << "ROOT";
        } else {
            std::cout << node->parent;
        }

        std::cout << " | Keys(" << node->num_keys << "/" << node->max_keys << "): ";

        // Print keys
        uint32_t* keys = get_keys(node);
        std::cout << "[";
        for (uint32_t i = 0; i < node->num_keys; i++) {
            if (i > 0) std::cout << ", ";
            std::cout << keys[i];
        }
        std::cout << "]";

        if (!node->is_leaf) {
            // Print children for internal nodes
            uint32_t* children = get_children(node);
            std::cout << " | Children: [";
            for (uint32_t i = 0; i <= node->num_keys; i++) {
                if (i > 0) std::cout << ", ";
                std::cout << children[i];
            }
            std::cout << "]";
        } else {
            // Print sibling links for leaf nodes
            std::cout << " | Prev: " << (node->previous == 0 ? "NULL" : std::to_string(node->previous));
            std::cout << " | Next: " << (node->next == 0 ? "NULL" : std::to_string(node->next));
        }

        std::cout << "\n";

        // Add children to queue for next level (internal nodes only)
        if (!node->is_leaf) {
            uint32_t* children = get_children(node);
            int child_position = 0;
            for (uint32_t i = 0; i <= node->num_keys; i++) {
                if (children[i] != 0) {
                    BTreeNode* child = static_cast<BTreeNode*>(pager_get(children[i]));
                    node_queue.push({child, info.level + 1, child_position++});
                }
            }
        }

        position_counter++;
    }

    std::cout << "\n" << std::string(80, '=') << "\n";

    // Print leaf chain traversal
    std::cout << "LEAF CHAIN TRAVERSAL:\n";
    std::cout << std::string(80, '-') << "\n";

    BTreeNode* leftmost = bp_left_most(tree);
    if (leftmost) {
        BTreeNode* current_leaf = leftmost;
        int leaf_count = 0;

        while (current_leaf) {
            uint32_t* keys = get_keys(current_leaf);

            std::cout << "Leaf[" << leaf_count << "] (Page " << current_leaf->index << "): ";
            std::cout << "Keys[" << current_leaf->num_keys << "]: ";

            for (uint32_t i = 0; i < current_leaf->num_keys; i++) {
                if (i > 0) std::cout << ", ";
                std::cout << keys[i];
            }

            // Show first few bytes of each record for debugging
            if (current_leaf->num_keys > 0) {
                uint8_t* records = get_record_data(current_leaf);
                std::cout << " | Sample record bytes: ";
                for (uint32_t i = 0; i < std::min(3U, current_leaf->num_keys); i++) {
                    std::cout << "[";
                    for (uint32_t j = 0; j < std::min(8U, tree.record_size); j++) {
                        if (j > 0) std::cout << " ";
                        std::cout << std::hex << std::setw(2) << std::setfill('0')
                                  << static_cast<int>(records[i * tree.record_size + j]);
                    }
                    std::cout << std::dec << "]";
                    if (tree.record_size > 8) std::cout << "...";
                    if (i < std::min(3U, current_leaf->num_keys) - 1) std::cout << " ";
                }
            }

            std::cout << "\n";

            if (current_leaf->next == 0) {
                break;
            }
            current_leaf = bp_get_next(current_leaf);
            leaf_count++;

            // Safety check to prevent infinite loops
            if (leaf_count > 100) {
                std::cout << "... (truncated after 100 leaves)\n";
                break;
            }
        }
    } else {
        std::cout << "No leftmost leaf found\n";
    }

    std::cout << std::string(80, '=') << "\n";
    std::cout << "END TREE VISUALIZATION\n";
}



// Recursive helper for structure-only printing
static void bp_debug_print_node_recursive(BTreeNode* node, int depth, const std::string& prefix) {
    if (!node) return;

    uint32_t* keys = get_keys(node);

    std::cout << prefix << (node->is_leaf ? "LEAF" : "INTERNAL")
              << "(pg:" << node->index << ") [";

    for (uint32_t i = 0; i < node->num_keys; i++) {
        if (i > 0) std::cout << ",";
        std::cout << keys[i];
    }
    std::cout << "]\n";

    if (!node->is_leaf) {
        uint32_t* children = get_children(node);
        for (uint32_t i = 0; i <= node->num_keys; i++) {
            if (children[i] != 0) {
                BTreeNode* child = static_cast<BTreeNode*>(pager_get(children[i]));
                bp_debug_print_node_recursive(child, depth + 1, prefix + "  ");
            }
        }
    }
}

// Additional helper function to print just the structure without data
void bp_debug_print_structure(const BPlusTree& tree) {
    if (tree.root_page_index == 0) {
        std::cout << "Empty tree\n";
        return;
    }

    BTreeNode* root = bp_get_root(tree);
    if (!root) {
        std::cout << "Invalid root\n";
        return;
    }

    std::cout << "Tree Structure (keys only):\n";
    bp_debug_print_node_recursive(root, 0, "");
}


void hash_node_recursive(const BPlusTree& tree, BTreeNode* node, uint64_t* hash, uint64_t prime, int depth) {
    if (!node) return;

    // Hash node structure
    *hash ^= node->index;
    *hash *= prime;
    *hash ^= node->parent;
    *hash *= prime;
    *hash ^= node->next;
    *hash *= prime;
    *hash ^= node->previous;
    *hash *= prime;
    *hash ^= node->num_keys;
    *hash *= prime;
    *hash ^= (node->is_leaf ? 1 : 0) | (depth << 1);
    *hash *= prime;

    // Hash keys in order
    uint32_t* keys = get_keys(node);
    for (uint32_t i = 0; i < node->num_keys; i++) {
        *hash ^= keys[i];
        *hash *= prime;
    }

    if (node->is_leaf) {
        // Hash record data
        uint8_t* record_data = get_record_data(node);
        for (uint32_t i = 0; i < node->num_keys; i++) {
            // Hash first 8 bytes of each record for speed
            const uint8_t* record = record_data + i * tree.record_size;
            uint32_t bytes_to_hash = std::min(8U, tree.record_size);
            for (uint32_t j = 0; j < bytes_to_hash; j++) {
                *hash ^= record[j];
                *hash *= prime;
            }
        }
    } else {
        // Recurse to children
        uint32_t* children = get_children(node);
        for (uint32_t i = 0; i <= node->num_keys; i++) {
            if (children[i] != 0) {
                BTreeNode* child = bp_get_child(node, i);
                if (child) {
                    hash_node_recursive(tree, child, hash, prime, depth + 1);
                }
            }
        }
    }
}
uint64_t debug_hash_tree(const BPlusTree& tree) {
    uint64_t hash = 0xcbf29ce484222325ULL; // FNV-1a basis
    const uint64_t prime = 0x100000001b3ULL;

    // Hash tree metadata
    hash ^= tree.root_page_index;
    hash *= prime;
    hash ^= tree.internal_max_keys;
    hash *= prime;
    hash ^= tree.leaf_max_keys;
    hash *= prime;
    hash ^= tree.record_size;
    hash *= prime;

    // Hash all nodes starting from root
    if (tree.root_page_index != 0) {
        BTreeNode* root = bp_get_root(tree);
        if (root) {
            hash_node_recursive(tree, root, &hash, prime, 0);
        }
    }

    return hash;
}
