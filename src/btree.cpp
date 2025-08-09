#include "btree.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <sys/types.h>
#include <vector>

// Helper functions to access arrays within the node's data area
static uint32_t *get_keys(BPTreeNode *node) {
  return reinterpret_cast<uint32_t *>(node->data);
}






static uint32_t *get_children(BPlusTree &tree, BPTreeNode *node) {
  return reinterpret_cast<uint32_t *>(node->data + tree.internal_max_keys *
                                                       tree.node_key_size);
}

static uint8_t *get_record_data(BPlusTree &tree, BPTreeNode *node) {
  return node->data + tree.leaf_max_keys * tree.node_key_size;
}

static uint8_t *get_record_at(BPlusTree &tree, BPTreeNode *node,
                              uint32_t index) {
  if (!node->is_leaf || index >= node->num_keys)
    return nullptr;
  return get_record_data(tree, node) + (index * node->record_size);
}

// Forward declaration
static bool bp_find_in_tree(BPlusTree &tree, BPTreeNode *node, uint32_t key);

// Combined btree.cpp functions - calculate_capacity and create merged

/*

internal:
A tree with n keys will have n+1 children


 */

BPlusTree bp_create(DataType key, const std::vector<ColumnInfo> &schema,
                    TreeType tree_type) {
  BPlusTree tree;
  tree.node_key_size = key;
  tree.tree_type = tree_type;

  // Calculate record size
  uint32_t record_size = 0;
  for (const auto &col : schema) {
    record_size += col.type;
  }
  tree.record_size = record_size;

  constexpr uint32_t USABLE_SPACE = PAGE_SIZE - NODE_HEADER_SIZE;
  const uint32_t minimum_entry_count = 3U;

  // Sanity check
  if ((record_size * minimum_entry_count) > USABLE_SPACE) {
    tree.tree_type = INVALID;
    return tree;
  }

  if (tree_type == BTREE) {
    // === REGULAR B-TREE: Both node types store records ===
    // Layout for both: [keys][records][children (wasted space in leaves)]
    // Same capacity calculation applies to both node types
    uint32_t entry_size = tree.node_key_size + record_size;
    uint32_t max_keys =
        std::max(minimum_entry_count, USABLE_SPACE / entry_size);
    uint32_t min_keys = max_keys / 2;
    uint32_t split_index = max_keys / 2;

    // Both node types have same capacity
    tree.leaf_max_keys = max_keys;
    tree.leaf_min_keys = min_keys;
    tree.leaf_split_index = split_index;
    tree.internal_max_keys = max_keys;
    tree.internal_min_keys = min_keys;
    tree.internal_split_index = split_index;
  } else if (tree_type == BPLUS) {
    // === B+TREE: Different capacities for leaf vs internal ===

    // Leaf nodes: [keys][records]
    uint32_t leaf_entry_size = tree.node_key_size + record_size;
    tree.leaf_max_keys =
        std::max(minimum_entry_count, USABLE_SPACE / leaf_entry_size);
    tree.leaf_min_keys = tree.leaf_max_keys / 2;
    tree.leaf_split_index = tree.leaf_max_keys / 2;

    // Internal nodes: [keys][children] (no records)
    // For n keys: n*key_size + (n+1)*key_size <= USABLE_SPACE
    // so  n * 2 (key + child pointer), and usuable space - that extra key in
    // the n+1.
    tree.internal_max_keys =
        (USABLE_SPACE - tree.node_key_size) / (2 * tree.node_key_size);
    tree.internal_max_keys =
        std::max(minimum_entry_count, tree.internal_max_keys);
    tree.internal_min_keys = tree.internal_max_keys / 2;
    tree.internal_split_index = tree.internal_max_keys / 2;
  } else {
    tree.tree_type = INVALID;
    return tree;
  }


  tree.root_page_index = 0;

  return tree;
}

// Remove the separate bp_calculate_capacity function since it's now integrated

void bp_init(BPlusTree &tree) {
  if (tree.root_page_index == 0) {
    BPTreeNode *root = bp_create_node(tree, true);
    if (root) {
      tree.root_page_index = root->index;
    }
  }
}

BPTreeNode *bp_create_node(BPlusTree &tree, bool is_leaf) {
  uint32_t page_index = pager_new();
  BPTreeNode *node = static_cast<BPTreeNode *>(pager_get(page_index));

  if (!node) {
    return nullptr;
  }

  pager_mark_dirty(page_index);

  node->index = page_index;
  node->parent = 0;
  node->next = 0;
  node->previous = 0;
  node->num_keys = 0;
  node->is_leaf = is_leaf ? 1 : 0;
  node->record_size = is_leaf ? tree.record_size : 0;

  memset(node->data, 0, sizeof(node->data));

  return node;
}

void bp_destroy_node(BPTreeNode *node) {
  if (!node)
    return;

  if (node->is_leaf) {
    if (node->previous != 0) {
      BPTreeNode *prev_node = bp_get_prev(node);
      if (prev_node) {
        bp_set_next(prev_node, node->next);
      }
    }

    if (node->next != 0) {
      BPTreeNode *next_node = bp_get_next(node);
      if (next_node) {
        bp_set_prev(next_node, node->previous);
      }
    }
  }

  pager_delete(node->index);
}

void bp_mark_dirty(BPTreeNode *node) {
  if (node) {
    pager_mark_dirty(node->index);
  }
}

BPTreeNode *bp_get_parent(BPTreeNode *node) {
  if (!node || node->parent == 0)
    return nullptr;
  return static_cast<BPTreeNode *>(pager_get(node->parent));
}

BPTreeNode *bp_get_child(BPlusTree &tree, BPTreeNode *node, uint32_t index) {
  if (!node || node->is_leaf)
    return nullptr;

  uint32_t *children = get_children(tree, node);
  if (index >= node->num_keys + 1 || children[index] == 0) {
    return nullptr;
  }

  return static_cast<BPTreeNode *>(pager_get(children[index]));
}

BPTreeNode *bp_get_next(BPTreeNode *node) {
  if (!node || node->next == 0)
    return nullptr;
  return static_cast<BPTreeNode *>(pager_get(node->next));
}

BPTreeNode *bp_get_prev(BPTreeNode *node) {
  if (!node || node->previous == 0)
    return nullptr;
  return static_cast<BPTreeNode *>(pager_get(node->previous));
}

void bp_set_parent(BPTreeNode *node, uint32_t parent_index) {
  if (!node)
    return;

  // Debug check for self-reference
  if (node->index == parent_index) {
    std::cerr << "ERROR: Attempting to make node " << node->index
              << " its own child!" << std::endl;
    exit(1);
  }

  bp_mark_dirty(node);
  node->parent = parent_index;

  if (parent_index != 0) {
    pager_mark_dirty(parent_index);
  }
}

void bp_set_child(BPlusTree &tree, BPTreeNode *node, uint32_t child_index,
                  uint32_t node_index) {
  if (!node || node->is_leaf)
    return;

  // Debug check for self-reference
  if (node->index == child_index) {
    std::cerr << "ERROR: Attempting to make node " << node->index
              << " its own child!" << std::endl;
    exit(1);
  }

  bp_mark_dirty(node);
  uint32_t *children = get_children(tree, node);
  children[child_index] = node_index;

  if (node_index != 0) {
    BPTreeNode *child_node = static_cast<BPTreeNode *>(pager_get(node_index));
    if (child_node) {
      bp_set_parent(child_node, node->index);
    }
  }
}

void bp_set_next(BPTreeNode *node, uint32_t index) {
  if (!node)
    return;

  bp_mark_dirty(node);
  node->next = index;

  if (index != 0) {
    pager_mark_dirty(index);
  }
}

void bp_set_prev(BPTreeNode *node, uint32_t index) {
  if (!node)
    return;

  bp_mark_dirty(node);
  node->previous = index;

  if (index != 0) {
    pager_mark_dirty(index);
  }
}

uint32_t bp_get_max_keys(BPlusTree &tree, BPTreeNode *node) {
  return node->is_leaf ? tree.leaf_max_keys : tree.internal_max_keys;
}

uint32_t bp_get_min_keys(BPlusTree &tree, BPTreeNode *node) {
  return node->is_leaf ? tree.leaf_min_keys : tree.internal_min_keys;
}

uint32_t bp_get_split_index(BPlusTree &tree, BPTreeNode *node) {
  return node->is_leaf ? tree.leaf_split_index : tree.internal_split_index;
}

BPTreeNode *bp_get_root(BPlusTree &tree) {
  return static_cast<BPTreeNode *>(pager_get(tree.root_page_index));
}

void bp_insert_element(BPlusTree &tree, uint32_t key, const uint8_t *data) {
  BPTreeNode *root = bp_get_root(tree);

  if (root->num_keys == 0) {
    bp_mark_dirty(root);
    uint32_t *keys = get_keys(root);
    uint8_t *record_data = get_record_data(tree, root);

    keys[0] = key;
    memcpy(record_data, data, tree.record_size);
    root->num_keys = 1;
  } else {
    bp_insert(tree, root, key, data);
  }

  pager_sync();
}

void bp_insert_repair(BPlusTree &tree, BPTreeNode *node) {
  if (node->num_keys < bp_get_max_keys(tree, node)) {
    return;
  } else if (node->parent == 0) {
    BPTreeNode *new_root = bp_split(tree, node);
    tree.root_page_index = new_root->index;
  } else {
    BPTreeNode *new_node = bp_split(tree, node);
    bp_insert_repair(tree, new_node);
  }
}

void bp_insert(BPlusTree &tree, BPTreeNode *node, uint32_t key,
               const uint8_t *data) {
  if (node->is_leaf) {
    uint32_t *keys = get_keys(node);
    uint8_t *record_data = get_record_data(tree, node);

    for (uint32_t i = 0; i < node->num_keys; i++) {
      if (keys[i] == key) {
        bp_mark_dirty(node);
        memcpy(record_data + i * tree.record_size, data, tree.record_size);
        return;
      }
    }

    if (node->num_keys >= tree.leaf_max_keys ) {
      bp_insert_repair(tree, node);
      bp_insert(tree, bp_find_leaf_node(tree, bp_get_root(tree), key), key,
                data);
      return;
    }

    bp_mark_dirty(node);
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
  } else {
    uint32_t *keys = get_keys(node);
    uint32_t find_index = 0;

    while (find_index < node->num_keys && keys[find_index] < key) {
      find_index++;
    }

    BPTreeNode *child_node = bp_get_child(tree, node, find_index);
    if (child_node) {
      bp_insert(tree, child_node, key, data);
    }
  }
}

BPTreeNode *bp_split(BPlusTree &tree, BPTreeNode *node) {
  BPTreeNode *right_node = bp_create_node(tree, node->is_leaf);
  uint32_t split_index = bp_get_split_index(tree, node);
  uint32_t *node_keys = get_keys(node);
  uint32_t rising_key = node_keys[split_index];
  uint32_t parent_index = 0;

  if (node->parent != 0) {
    BPTreeNode *current_parent = bp_get_parent(node);
    uint32_t *parent_keys = get_keys(current_parent);
    uint32_t *parent_children = get_children(tree, current_parent);

    for (parent_index = 0; parent_index < current_parent->num_keys + 1 &&
                           parent_children[parent_index] != node->index;
         parent_index++)
      ;

    if (parent_index == current_parent->num_keys + 1) {
      throw std::runtime_error("Couldn't find which child we were!");
    }

    bp_mark_dirty(current_parent);
    for (uint32_t i = current_parent->num_keys; i > parent_index; i--) {
      parent_children[i + 1] = parent_children[i];
      parent_keys[i] = parent_keys[i - 1];
    }

    current_parent->num_keys++;
    parent_keys[parent_index] = rising_key;
    bp_set_child(tree, current_parent, parent_index + 1, right_node->index);
  }

  uint32_t right_split = node->is_leaf ? split_index : split_index + 1;

  if (node->is_leaf) {
    bp_set_prev(right_node, node->index);
    bp_set_next(right_node, node->next);

    if (node->next != 0) {
      BPTreeNode *next_node = bp_get_next(node);
      if (next_node) {
        bp_set_prev(next_node, right_node->index);
      }
    }

    bp_set_next(node, right_node->index);
  }

  bp_mark_dirty(right_node);
  bp_mark_dirty(node);

  right_node->num_keys = node->num_keys - right_split;

  uint32_t *right_keys = get_keys(right_node);
  uint32_t *node_children = get_children(tree, node);
  uint32_t *right_children = get_children(tree, right_node);

  if (!node->is_leaf) {
    for (uint32_t i = right_split; i < node->num_keys + 1; i++) {
      bp_set_child(tree, right_node, i - right_split, node_children[i]);
      node_children[i] = 0;
    }
  }

  for (uint32_t i = right_split; i < node->num_keys; i++) {
    right_keys[i - right_split] = node_keys[i];
  }

  if (node->is_leaf) {
    uint8_t *node_records = get_record_data(tree, node);
    uint8_t *right_records = get_record_data(tree, right_node);
    memcpy(right_records, node_records + right_split * tree.record_size,
           right_node->num_keys * tree.record_size);
  }

  node->num_keys = split_index;

  if (node->parent != 0) {
    return bp_get_parent(node);
  } else {
    BPTreeNode *new_root = bp_create_node(tree, false);
    uint32_t *new_root_keys = get_keys(new_root);

    new_root_keys[0] = rising_key;
    new_root->num_keys = 1;
    bp_set_child(tree, new_root, 0, node->index);
    bp_set_child(tree, new_root, 1, right_node->index);
    return new_root;
  }
}

bool bp_find_element(BPlusTree &tree, uint32_t key) {
  BPTreeNode *root = bp_get_root(tree);
  return bp_find_in_tree(tree, root, key);
}

static bool bp_find_in_tree(BPlusTree &tree, BPTreeNode *node, uint32_t key) {
  if (!node)
    return false;

  uint32_t *keys = get_keys(node);
  uint32_t i;

  for (i = 0; i < node->num_keys && keys[i] < key; i++)
    ;

  if (i == node->num_keys) {
    if (!node->is_leaf) {
      return bp_find_in_tree(tree, bp_get_child(tree, node, node->num_keys),
                             key);
    } else {
      return false;
    }
  } else if (keys[i] > key) {
    if (!node->is_leaf) {
      return bp_find_in_tree(tree, bp_get_child(tree, node, i), key);
    } else {
      return false;
    }
  } else {
    if (keys[i] == key && node->is_leaf) {
      return true;
    } else {
      return bp_find_in_tree(tree, bp_get_child(tree, node, i + 1), key);
    }
  }
}

const uint8_t *bp_get(BPlusTree &tree, uint32_t key) {
  BPTreeNode *root = bp_get_root(tree);
  BPTreeNode *leaf_node = bp_find_leaf_node(tree, root, key);
  if (!leaf_node)
    return nullptr;

  uint32_t *keys = get_keys(leaf_node);

  for (uint32_t i = 0; i < leaf_node->num_keys; i++) {
    if (keys[i] == key) {
      return get_record_at(tree, leaf_node, i);
    }
  }

  return nullptr;
}

BPTreeNode *bp_find_leaf_node(BPlusTree &tree, BPTreeNode *node, uint32_t key) {
  if (node->is_leaf) {
    return const_cast<BPTreeNode *>(node);
  }

  uint32_t *keys = get_keys(node);
  uint32_t i;

  for (i = 0; i < node->num_keys && keys[i] < key; i++)
    ;

  if (i == node->num_keys) {
    return bp_find_leaf_node(tree, bp_get_child(tree, node, node->num_keys),
                             key);
  } else if (keys[i] > key) {
    return bp_find_leaf_node(tree, bp_get_child(tree, node, i), key);
  } else {
    return bp_find_leaf_node(tree, bp_get_child(tree, node, i + 1), key);
  }
}

BPTreeNode *bp_left_most(BPlusTree &tree) {
  BPTreeNode *temp = bp_get_root(tree);

  while (temp && !temp->is_leaf) {
    temp = bp_get_child(tree, temp, 0);
  }

  return temp;
}

std::vector<std::pair<uint32_t, const uint8_t *>>
bp_print_leaves(BPlusTree &tree) {
  std::vector<std::pair<uint32_t, const uint8_t *>> result;
  BPTreeNode *temp = bp_left_most(tree);

  while (temp) {
    uint32_t *keys = get_keys(temp);

    for (uint32_t i = 0; i < temp->num_keys; i++) {
      result.push_back({keys[i], get_record_at(tree, temp, i)});
    }
    temp = bp_get_next(temp);
  }

  return result;
}

void bp_delete_element(BPlusTree &tree, uint32_t key) {
  BPTreeNode *root = bp_get_root(tree);
  if (!root)
    return;

  bp_do_delete(tree, root, key);

  if (root->num_keys == 0 && !root->is_leaf) {
    BPTreeNode *old_root = root;
    BPTreeNode *new_root = bp_get_child(tree, root, 0);
    if (new_root) {
      tree.root_page_index = new_root->index;
      bp_set_parent(new_root, 0);
    }
    bp_destroy_node(old_root);
  }

  pager_sync();
}

void bp_do_delete(BPlusTree &tree, BPTreeNode *node, uint32_t key) {
  if (!node)
    return;

  uint32_t *keys = get_keys(node);
  uint32_t i;

  for (i = 0; i < node->num_keys && keys[i] < key; i++)
    ;

  if (i == node->num_keys) {
    if (!node->is_leaf) {
      bp_do_delete(tree, bp_get_child(tree, node, node->num_keys), key);
    }
  } else if (!node->is_leaf && keys[i] == key) {
    bp_do_delete(tree, bp_get_child(tree, node, i + 1), key);
  } else if (!node->is_leaf) {
    bp_do_delete(tree, bp_get_child(tree, node, i), key);
  } else if (node->is_leaf && keys[i] == key) {
    bp_mark_dirty(node);

    uint8_t *record_data = get_record_data(tree, node);

    for (uint32_t j = i; j < node->num_keys - 1; j++) {
      keys[j] = keys[j + 1];
      memcpy(record_data + j * tree.record_size,
             record_data + (j + 1) * tree.record_size, tree.record_size);
    }
    node->num_keys--;

    if (i == 0 && node->parent != 0) {
      bp_update_parent_keys(tree, node, key);
    }

    bp_repair_after_delete(tree, node);
  }
}

void bp_update_parent_keys(BPlusTree &tree, BPTreeNode *node,
                           uint32_t deleted_key) {
  uint32_t next_smallest = 0;
  BPTreeNode *parent_node = bp_get_parent(node);
  if (!parent_node)
    return;

  uint32_t *parent_children = get_children(tree, parent_node);
  uint32_t parent_index;

  for (parent_index = 0; parent_children[parent_index] != node->index;
       parent_index++)
    ;

  if (node->num_keys == 0) {
    if (parent_index == parent_node->num_keys) {
      next_smallest = 0;
    } else {
      BPTreeNode *next_sibling =
          bp_get_child(tree, parent_node, parent_index + 1);
      if (next_sibling) {
        uint32_t *sibling_keys = get_keys(next_sibling);
        next_smallest = sibling_keys[0];
      }
    }
  } else {
    uint32_t *node_keys = get_keys(node);
    next_smallest = node_keys[0];
  }

  BPTreeNode *current_parent = parent_node;
  while (current_parent) {
    uint32_t *current_keys = get_keys(current_parent);
    if (parent_index > 0 && current_keys[parent_index - 1] == deleted_key) {
      bp_mark_dirty(current_parent);
      current_keys[parent_index - 1] = next_smallest;
    }

    BPTreeNode *grandparent = bp_get_parent(current_parent);
    if (grandparent) {
      uint32_t *grandparent_children = get_children(tree, grandparent);
      for (parent_index = 0;
           grandparent_children[parent_index] != current_parent->index;
           parent_index++)
        ;
    }
    current_parent = grandparent;
  }
}

void bp_repair_after_delete(BPlusTree &tree, BPTreeNode *node) {
  if (node->num_keys < bp_get_min_keys(tree, node)) {
    if (node->parent == 0) {
      if (node->num_keys == 0 && !node->is_leaf) {
        BPTreeNode *new_root = bp_get_child(tree, node, 0);
        if (new_root) {
          tree.root_page_index = new_root->index;
          bp_set_parent(new_root, 0);
        }
        bp_destroy_node(node);
      }
    } else {
      BPTreeNode *parent_node = bp_get_parent(node);
      uint32_t *parent_children = get_children(tree, parent_node);
      uint32_t parent_index;

      for (parent_index = 0; parent_children[parent_index] != node->index;
           parent_index++)
        ;

      BPTreeNode *left_sibling =
          (parent_index > 0) ? bp_get_child(tree, parent_node, parent_index - 1)
                             : nullptr;
      BPTreeNode *right_sibling =
          (parent_index < parent_node->num_keys)
              ? bp_get_child(tree, parent_node, parent_index + 1)
              : nullptr;

      if (left_sibling &&
          left_sibling->num_keys > bp_get_min_keys(tree, left_sibling)) {
        bp_steal_from_left(tree, node, parent_index);
      } else if (right_sibling && right_sibling->num_keys >
                                      bp_get_min_keys(tree, right_sibling)) {
        bp_steal_from_right(tree, node, parent_index);
      } else if (parent_index == 0 && right_sibling) {
        BPTreeNode *next_node = bp_merge_right(tree, node);
        bp_repair_after_delete(tree, bp_get_parent(next_node));
      } else if (left_sibling) {
        BPTreeNode *next_node = bp_merge_right(tree, left_sibling);
        bp_repair_after_delete(tree, bp_get_parent(next_node));
      }
    }
  }
}

BPTreeNode *bp_merge_right(BPlusTree &tree, BPTreeNode *node) {
  BPTreeNode *parent_node = bp_get_parent(node);
  uint32_t *parent_children = get_children(tree, parent_node);
  uint32_t *parent_keys = get_keys(parent_node);
  uint32_t parent_index = 0;

  for (parent_index = 0; parent_children[parent_index] != node->index;
       parent_index++)
    ;

  BPTreeNode *right_sib = bp_get_child(tree, parent_node, parent_index + 1);
  uint32_t *node_keys = get_keys(node);
  uint32_t *right_keys = get_keys(right_sib);
  uint32_t *node_children = get_children(tree, node);
  uint32_t *right_children = get_children(tree, right_sib);

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
      bp_set_child(tree, node, node->num_keys + 1 + i, right_children[i]);
    }
    node->num_keys = node->num_keys + right_sib->num_keys + 1;
  } else {
    uint8_t *node_records = get_record_data(tree, node);
    uint8_t *right_records = get_record_data(tree, right_sib);

    memcpy(node_records + node->num_keys * tree.record_size, right_records,
           right_sib->num_keys * tree.record_size);

    node->num_keys = node->num_keys + right_sib->num_keys;

    bp_set_next(node, right_sib->next);

    if (right_sib->next != 0) {
      BPTreeNode *next_node = bp_get_next(right_sib);
      if (next_node) {
        bp_set_prev(next_node, node->index);
      }
    }
  }

  bp_mark_dirty(parent_node);

  for (uint32_t i = parent_index + 1; i < parent_node->num_keys; i++) {
    parent_children[i] = parent_children[i + 1];
    parent_keys[i - 1] = parent_keys[i];
  }
  parent_node->num_keys--;

  bp_destroy_node(right_sib);
  return node;
}

BPTreeNode *bp_steal_from_right(BPlusTree &tree, BPTreeNode *node,
                                uint32_t parent_index) {
  BPTreeNode *parent_node = bp_get_parent(node);
  BPTreeNode *right_sib = bp_get_child(tree, parent_node, parent_index + 1);

  uint32_t *node_keys = get_keys(node);
  uint32_t *right_keys = get_keys(right_sib);
  uint32_t *parent_keys = get_keys(parent_node);
  uint32_t *node_children = get_children(tree, node);
  uint32_t *right_children = get_children(tree, right_sib);

  bp_mark_dirty(node);
  bp_mark_dirty(right_sib);
  bp_mark_dirty(parent_node);

  node->num_keys++;

  if (node->is_leaf) {
    node_keys[node->num_keys - 1] = right_keys[0];

    uint8_t *node_records = get_record_data(tree, node);
    uint8_t *right_records = get_record_data(tree, right_sib);

    memcpy(node_records + (node->num_keys - 1) * tree.record_size,
           right_records, tree.record_size);

    parent_keys[parent_index] = right_keys[1];

    for (uint32_t i = 1; i < right_sib->num_keys; i++) {
      right_keys[i - 1] = right_keys[i];
      memcpy(right_records + (i - 1) * tree.record_size,
             right_records + i * tree.record_size, tree.record_size);
    }
  } else {
    node_keys[node->num_keys - 1] = parent_keys[parent_index];
    parent_keys[parent_index] = right_keys[0];

    bp_set_child(tree, node, node->num_keys, right_children[0]);

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

BPTreeNode *bp_steal_from_left(BPlusTree &tree, BPTreeNode *node,
                               uint32_t parent_index) {
  BPTreeNode *parent_node = bp_get_parent(node);
  BPTreeNode *left_sib = bp_get_child(tree, parent_node, parent_index - 1);

  uint32_t *node_keys = get_keys(node);
  uint32_t *left_keys = get_keys(left_sib);
  uint32_t *parent_keys = get_keys(parent_node);
  uint32_t *node_children = get_children(tree, node);
  uint32_t *left_children = get_children(tree, left_sib);

  bp_mark_dirty(node);
  bp_mark_dirty(left_sib);
  bp_mark_dirty(parent_node);

  node->num_keys++;

  for (uint32_t i = node->num_keys - 1; i > 0; i--) {
    node_keys[i] = node_keys[i - 1];
  }

  if (node->is_leaf) {
    uint8_t *node_records = get_record_data(tree, node);
    uint8_t *left_records = get_record_data(tree, left_sib);

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
    bp_set_child(tree, node, 0, left_children[left_sib->num_keys]);
    left_children[left_sib->num_keys] = 0;
  }

  left_sib->num_keys--;

  return node;
}

void bp_debug_print_tree(BPlusTree &tree) {
  if (tree.root_page_index == 0) {
    std::cout << "Tree is empty (no root)\n";
    return;
  }

  BPTreeNode *root = bp_get_root(tree);
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

  struct NodeInfo {
    BPTreeNode *node;
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

    BPTreeNode *node = info.node;
    if (!node)
      continue;

    if (info.level != current_level) {
      if (current_level >= 0) {
        std::cout << "\n";
      }
      current_level = info.level;
      position_counter = 0;
      std::cout << "LEVEL " << current_level << ":\n";
      std::cout << std::string(80, '-') << "\n";
    }

    std::cout << "Node[" << position_counter << "] (Page " << node->index
              << "): ";

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

    std::cout << " | Keys(" << node->num_keys << "/"
              << (node->is_leaf ? tree.leaf_max_keys : tree.internal_max_keys)
              << "): ";

    uint32_t *keys = get_keys(node);
    std::cout << "[";
    for (uint32_t i = 0; i < node->num_keys; i++) {
      if (i > 0)
        std::cout << ", ";
      std::cout << keys[i];
    }
    std::cout << "]";

    if (!node->is_leaf) {
      uint32_t *children = get_children(tree, node);
      std::cout << " | Children: [";
      for (uint32_t i = 0; i <= node->num_keys; i++) {
        if (i > 0)
          std::cout << ", ";
        std::cout << children[i];
      }
      std::cout << "]";
    } else {
      std::cout << " | Prev: "
                << (node->previous == 0 ? "NULL"
                                        : std::to_string(node->previous));
      std::cout << " | Next: "
                << (node->next == 0 ? "NULL" : std::to_string(node->next));
    }

    std::cout << "\n";

    if (!node->is_leaf) {
      uint32_t *children = get_children(tree, node);
      int child_position = 0;
      for (uint32_t i = 0; i <= node->num_keys; i++) {
        if (children[i] != 0) {
          BPTreeNode *child = static_cast<BPTreeNode *>(pager_get(children[i]));
          node_queue.push({child, info.level + 1, child_position++});
        }
      }
    }

    position_counter++;
  }

  std::cout << "\n" << std::string(80, '=') << "\n";

  std::cout << "LEAF CHAIN TRAVERSAL:\n";
  std::cout << std::string(80, '-') << "\n";

  BPTreeNode *leftmost = bp_left_most(tree);
  if (leftmost) {
    BPTreeNode *current_leaf = leftmost;
    int leaf_count = 0;

    while (current_leaf) {
      uint32_t *keys = get_keys(current_leaf);

      std::cout << "Leaf[" << leaf_count << "] (Page " << current_leaf->index
                << "): ";
      std::cout << "Keys[" << current_leaf->num_keys << "]: ";

      for (uint32_t i = 0; i < current_leaf->num_keys; i++) {
        if (i > 0)
          std::cout << ", ";
        std::cout << keys[i];
      }

      if (current_leaf->num_keys > 0) {
        uint8_t *records = get_record_data(tree, current_leaf);
        std::cout << " | Sample record bytes: ";
        for (uint32_t i = 0; i < std::min(3U, current_leaf->num_keys); i++) {
          std::cout << "[";
          for (uint32_t j = 0; j < std::min(8U, tree.record_size); j++) {
            if (j > 0)
              std::cout << " ";
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<int>(records[i * tree.record_size + j]);
          }
          std::cout << std::dec << "]";
          if (tree.record_size > 8)
            std::cout << "...";
          if (i < std::min(3U, current_leaf->num_keys) - 1)
            std::cout << " ";
        }
      }

      std::cout << "\n";

      if (current_leaf->next == 0) {
        break;
      }
      current_leaf = bp_get_next(current_leaf);
      leaf_count++;

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

static void bp_debug_print_node_recursive(BPlusTree &tree, BPTreeNode *node,
                                          int depth,
                                          const std::string &prefix) {
  if (!node)
    return;

  uint32_t *keys = get_keys(node);

  std::cout << prefix << (node->is_leaf ? "LEAF" : "INTERNAL")
            << "(pg:" << node->index << ") [";

  for (uint32_t i = 0; i < node->num_keys; i++) {
    if (i > 0)
      std::cout << ",";
    std::cout << keys[i];
  }
  std::cout << "]\n";

  if (!node->is_leaf) {
    uint32_t *children = get_children(tree, node);
    for (uint32_t i = 0; i <= node->num_keys; i++) {
      if (children[i] != 0) {
        BPTreeNode *child = static_cast<BPTreeNode *>(pager_get(children[i]));
        bp_debug_print_node_recursive(tree, child, depth + 1, prefix + "  ");
      }
    }
  }
}

void bp_debug_print_structure(BPlusTree &tree) {
  if (tree.root_page_index == 0) {
    std::cout << "Empty tree\n";
    return;
  }

  BPTreeNode *root = bp_get_root(tree);
  if (!root) {
    std::cout << "Invalid root\n";
    return;
  }

  std::cout << "Tree Structure (keys only):\n";
  bp_debug_print_node_recursive(tree, root, 0, "");
}

void hash_node_recursive(BPlusTree &tree, BPTreeNode *node, uint64_t *hash,
                         uint64_t prime, int depth) {
  if (!node)
    return;

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

  uint32_t *keys = get_keys(node);
  for (uint32_t i = 0; i < node->num_keys; i++) {
    *hash ^= keys[i];
    *hash *= prime;
  }

  if (node->is_leaf) {
    uint8_t *record_data = get_record_data(tree, node);
    for (uint32_t i = 0; i < node->num_keys; i++) {
      const uint8_t *record = record_data + i * tree.record_size;
      uint32_t bytes_to_hash = std::min(8U, tree.record_size);
      for (uint32_t j = 0; j < bytes_to_hash; j++) {
        *hash ^= record[j];
        *hash *= prime;
      }
    }
  } else {
    uint32_t *children = get_children(tree, node);
    for (uint32_t i = 0; i <= node->num_keys; i++) {
      if (children[i] != 0) {
        BPTreeNode *child = bp_get_child(tree, node, i);
        if (child) {
          hash_node_recursive(tree, child, hash, prime, depth + 1);
        }
      }
    }
  }
}

uint64_t debug_hash_tree(BPlusTree &tree) {
  uint64_t hash = 0xcbf29ce484222325ULL;
  const uint64_t prime = 0x100000001b3ULL;

  hash ^= tree.root_page_index;
  hash *= prime;
  hash ^= tree.internal_max_keys;
  hash *= prime;
  hash ^= tree.leaf_max_keys;
  hash *= prime;
  hash ^= tree.record_size;
  hash *= prime;

  if (tree.root_page_index != 0) {
    BPTreeNode *root = bp_get_root(tree);
    if (root) {
      hash_node_recursive(tree, root, &hash, prime, 0);
    }
  }

  return hash;
}

std::vector<LeafDataEntry> bp_extract_leaf_data(BPlusTree &tree) {
  std::vector<LeafDataEntry> result;
  auto leaf_pairs = bp_print_leaves(tree);
  BPTreeNode *current_node = bp_left_most(tree);
  uint32_t current_node_page = current_node ? current_node->index : 0;
  size_t pair_index = 0;

  while (current_node && pair_index < leaf_pairs.size()) {
    uint32_t *keys = get_keys(current_node);

    for (uint32_t i = 0;
         i < current_node->num_keys && pair_index < leaf_pairs.size(); i++) {
      if (keys[i] == leaf_pairs[pair_index].first) {
        LeafDataEntry entry;
        entry.key = leaf_pairs[pair_index].first;
        entry.node_page = current_node_page;
        entry.data.resize(tree.record_size);
        memcpy(entry.data.data(), leaf_pairs[pair_index].second,
               tree.record_size);
        result.push_back(entry);
        pair_index++;
      }
    }

    if (current_node->next == 0) {
      break;
    }
    current_node = bp_get_next(current_node);
    if (current_node) {
      current_node_page = current_node->index;
    }
  }

  return result;
}

void bp_debug_capacity_calculation(BPlusTree &tree,
                                   const std::vector<ColumnInfo> &schema) {
  std::cout << "=== B-TREE CAPACITY DEBUG ===" << std::endl;
  std::cout << "PAGE_SIZE: " << PAGE_SIZE << " bytes" << std::endl;
  std::cout << "NODE_HEADER_SIZE: " << NODE_HEADER_SIZE << " bytes"
            << std::endl;
  std::cout << "USABLE_SPACE: " << (PAGE_SIZE - NODE_HEADER_SIZE) << " bytes"
            << std::endl;
  std::cout << "tree.node_key_size: " << tree.node_key_size << " bytes"
            << std::endl;

  uint32_t record_size = 0;
  std::cout << "\nSchema breakdown:" << std::endl;
  for (size_t i = 0; i < schema.size(); i++) {
    std::cout << "  Column " << i << ": " << schema[i].type << " bytes"
              << std::endl;
    record_size += schema[i].type;
  }
  std::cout << "Total record size: " << record_size << " bytes" << std::endl;

  uint32_t usable_space = PAGE_SIZE - NODE_HEADER_SIZE;
  uint32_t leaf_entry_size = tree.node_key_size + record_size;
  uint32_t leaf_max_keys = usable_space / leaf_entry_size;

  std::cout << "\nLEAF NODE CAPACITY:" << std::endl;
  std::cout << "  Entry size (key + record): " << leaf_entry_size << " bytes"
            << std::endl;
  std::cout << "  Calculated max_keys: " << leaf_max_keys << std::endl;
  std::cout << "  Space used by max_keys: " << (leaf_max_keys * leaf_entry_size)
            << " bytes" << std::endl;
  std::cout << "  Remaining space: "
            << (usable_space - (leaf_max_keys * leaf_entry_size)) << " bytes"
            << std::endl;

  bool can_fit_one_more =
      (usable_space - (leaf_max_keys * leaf_entry_size)) >= leaf_entry_size;
  std::cout << "  Can fit one more entry? " << (can_fit_one_more ? "YES" : "NO")
            << std::endl;

  if (can_fit_one_more) {
    std::cout << "  WARNING: Capacity calculation may be off by 1!"
              << std::endl;
  }

  uint32_t internal_entry_size = 2 * tree.node_key_size;
  uint32_t internal_space_needed_for_n_keys = 0;
  uint32_t internal_max_keys = 0;

  for (uint32_t n = 1; n <= 2000; n++) {
    internal_space_needed_for_n_keys =
        n * tree.node_key_size + (n + 1) * tree.node_key_size;
    if (internal_space_needed_for_n_keys > usable_space) {
      internal_max_keys = n - 1;
      break;
    }
  }

  std::cout << "\nINTERNAL NODE CAPACITY:" << std::endl;
  std::cout << "  For n=" << internal_max_keys << " keys:" << std::endl;
  std::cout << "    Keys space: " << (internal_max_keys * tree.node_key_size)
            << " bytes" << std::endl;
  std::cout << "    Children space: "
            << ((internal_max_keys + 1) * tree.node_key_size) << " bytes"
            << std::endl;
  std::cout << "    Total space: "
            << (internal_max_keys * tree.node_key_size +
                (internal_max_keys + 1) * tree.node_key_size)
            << " bytes" << std::endl;
  std::cout << "    Remaining: "
            << (usable_space - (internal_max_keys * tree.node_key_size +
                                (internal_max_keys + 1) * tree.node_key_size))
            << " bytes" << std::endl;

  uint32_t test_space = (internal_max_keys + 1) * tree.node_key_size +
                        (internal_max_keys + 2) * tree.node_key_size;
  std::cout << "  For n=" << (internal_max_keys + 1)
            << " keys would need: " << test_space << " bytes";
  if (test_space <= usable_space) {
    std::cout << " (FITS - calculation error!)";
  }
  std::cout << std::endl;

  std::cout << "\nMEMORY LAYOUT ANALYSIS:" << std::endl;
  std::cout << "Node structure in memory:" << std::endl;
  std::cout << "  Offset 0-31: Node header ("
            << sizeof(BPTreeNode) - (PAGE_SIZE - NODE_HEADER_SIZE) << " bytes)"
            << std::endl;
  std::cout << "  Offset 32-" << (32 + leaf_max_keys * tree.node_key_size - 1)
            << ": Keys array (" << (leaf_max_keys * tree.node_key_size)
            << " bytes)" << std::endl;
  std::cout << "  Offset " << (32 + leaf_max_keys * tree.node_key_size) << "-"
            << (32 + leaf_max_keys * tree.node_key_size +
                leaf_max_keys * record_size - 1)
            << ": Records array (" << (leaf_max_keys * record_size) << " bytes)"
            << std::endl;
  std::cout << "  End at offset: "
            << (32 + leaf_max_keys * tree.node_key_size +
                leaf_max_keys * record_size)
            << std::endl;
  std::cout << "  Page ends at: " << PAGE_SIZE << std::endl;

  uint32_t total_used =
      32 + leaf_max_keys * tree.node_key_size + leaf_max_keys * record_size;
  if (total_used > PAGE_SIZE) {
    std::cout << "  OVERFLOW! Used " << total_used
              << " bytes, but page is only " << PAGE_SIZE << " bytes!"
              << std::endl;
    std::cout << "  Overflow by: " << (total_used - PAGE_SIZE) << " bytes"
              << std::endl;
  }

  std::cout << "============================" << std::endl;
}

void bp_debug_node_layout(BPlusTree &tree, BPTreeNode *node,
                          std::vector<ColumnInfo> &schema) {
  if (!node) {
    std::cout << "Node is null!" << std::endl;
    return;
  }

  std::cout << "=== NODE LAYOUT DEBUG ===" << std::endl;
  std::cout << "Node info:" << std::endl;
  std::cout << "  Index: " << node->index << std::endl;
  std::cout << "  Is leaf: " << (node->is_leaf ? "YES" : "NO") << std::endl;
  std::cout << "  Num keys: " << node->num_keys << std::endl;
  std::cout << "  Record size: " << node->record_size << std::endl;

  if (node->is_leaf) {
    uint32_t keys_end = tree.leaf_max_keys * tree.node_key_size;
    uint32_t records_start = keys_end;
    uint32_t records_end =
        records_start + tree.leaf_max_keys * node->record_size;

    std::cout << "Leaf layout:" << std::endl;
    std::cout << "  Keys: data[0] to data[" << (keys_end - 1) << "]"
              << std::endl;
    std::cout << "  Records: data[" << records_start << "] to data["
              << (records_end - 1) << "]" << std::endl;
    std::cout << "  Total data used: " << records_end << " bytes" << std::endl;
    std::cout << "  Available data space: " << (PAGE_SIZE - NODE_HEADER_SIZE)
              << " bytes" << std::endl;

    if (records_end > (PAGE_SIZE - NODE_HEADER_SIZE)) {
      std::cout << "  ERROR: Data extends beyond available space!" << std::endl;
      std::cout << "  Overflow: "
                << (records_end - (PAGE_SIZE - NODE_HEADER_SIZE)) << " bytes"
                << std::endl;
    }
  } else {
    uint32_t keys_end = tree.internal_max_keys * tree.node_key_size;
    uint32_t children_start = keys_end;
    uint32_t children_end =
        children_start + (tree.internal_max_keys + 1) * tree.node_key_size;

    std::cout << "Internal layout:" << std::endl;
    std::cout << "  Keys: data[0] to data[" << (keys_end - 1) << "]"
              << std::endl;
    std::cout << "  Children: data[" << children_start << "] to data["
              << (children_end - 1) << "]" << std::endl;
    std::cout << "  Total data used: " << children_end << " bytes" << std::endl;
    std::cout << "  Available data space: " << (PAGE_SIZE - NODE_HEADER_SIZE)
              << " bytes" << std::endl;

    if (children_end > (PAGE_SIZE - NODE_HEADER_SIZE)) {
      std::cout << "  ERROR: Data extends beyond available space!" << std::endl;
      std::cout << "  Overflow: "
                << (children_end - (PAGE_SIZE - NODE_HEADER_SIZE)) << " bytes"
                << std::endl;
    }
  }

  std::cout << "==========================" << std::endl;
}

/* DBGINV */

#include <cassert>

static void bp_verify_node_invariants(BPlusTree &tree, BPTreeNode *node) {
  if (!node)
    return;

  // Check key count bounds
  uint32_t min_keys = bp_get_min_keys(tree, node);
  uint32_t max_keys = bp_get_max_keys(tree, node);

  // Root can have fewer than min_keys
  if (node->parent != 0 && node->num_keys < min_keys) {
    std::cerr << "INVARIANT VIOLATION: Node " << node->index << " has "
              << node->num_keys << " keys, minimum is " << min_keys
              << std::endl;
    exit(1);
  }

  if (node->num_keys > max_keys) {
    std::cerr << "INVARIANT VIOLATION: Node " << node->index << " has "
              << node->num_keys << " keys, maximum is " << max_keys
              << std::endl;
    exit(1);
  }

  uint32_t *keys = get_keys(node);

  // Check key ordering within node
  for (uint32_t i = 1; i < node->num_keys; i++) {
    if (keys[i - 1] >= keys[i]) {
      std::cerr << "INVARIANT VIOLATION: Keys not sorted in node "
                << node->index << " at positions " << i - 1 << " and " << i
                << " (values " << keys[i - 1] << ", " << keys[i] << ")"
                << std::endl;
      exit(1);
    }
  }

  // Check child pointer count for internal nodes
  if (!node->is_leaf) {
    uint32_t *children = get_children(tree, node);

    // Should have exactly num_keys + 1 children
    for (uint32_t i = 0; i <= node->num_keys; i++) {
      if (children[i] == 0) {
        std::cerr << "INVARIANT VIOLATION: Internal node " << node->index
                  << " missing child pointer at index " << i << std::endl;
        exit(1);
      }
    }

    // Check that children point back to this node as parent
    for (uint32_t i = 0; i <= node->num_keys; i++) {
      BPTreeNode *child = bp_get_child(tree, node, i);
      if (!child) {
        std::cerr << "INVARIANT VIOLATION: Cannot access child " << i
                  << " of node " << node->index << std::endl;
        exit(1);
      }

      // Check for self-reference (node cannot be its own child)
      if (child->index == node->index) {
        std::cerr << "INVARIANT VIOLATION: Node " << node->index
                  << " is its own child at position " << i << std::endl;
        exit(1);
      }

      if (child->parent != node->index) {
        std::cerr << "INVARIANT VIOLATION: Child " << child->index
                  << " has parent " << child->parent << " but should be "
                  << node->index << std::endl;
        exit(1);
      }
    }
  }
}

static void bp_verify_key_separation(BPlusTree &tree, BPTreeNode *node) {
  if (!node || node->is_leaf)
    return;

  uint32_t *keys = get_keys(node);

  for (uint32_t i = 0; i <= node->num_keys; i++) {
    BPTreeNode *child = bp_get_child(tree, node, i);
    if (!child)
      continue;

    uint32_t *child_keys = get_keys(child);

    // For child[i], check constraints based on separators
    // child[0] has keys < keys[0]
    // child[1] has keys >= keys[0] and < keys[1]
    // child[2] has keys >= keys[1] and < keys[2]
    // etc.

    // Check upper bound: child[i] should have keys < keys[i] (except for
    // rightmost child)
    if (i < node->num_keys) {
      uint32_t upper_separator = keys[i];
      for (uint32_t j = 0; j < child->num_keys; j++) {
        if (child_keys[j] >= upper_separator) {
          std::cerr << "INVARIANT VIOLATION: Key " << child_keys[j]
                    << " in child " << child->index << " violates upper bound "
                    << upper_separator << " from parent " << node->index
                    << std::endl;
          exit(1);
        }
      }
    }

    // Check lower bound: child[i] should have keys >= keys[i-1] (except for
    // leftmost child)
    if (i > 0) {
      uint32_t lower_separator = keys[i - 1];
      for (uint32_t j = 0; j < child->num_keys; j++) {
        if (child_keys[j] < lower_separator) {
          std::cerr << "INVARIANT VIOLATION: Key " << child_keys[j]
                    << " in child " << child->index << " violates lower bound "
                    << lower_separator << " from parent " << node->index
                    << std::endl;
          exit(1);
        }
      }
    }

    // Recursively check children
    bp_verify_key_separation(tree, child);
  }
}

static void bp_verify_leaf_links(BPlusTree &tree) {
  BPTreeNode *current = bp_left_most(tree);
  BPTreeNode *prev = nullptr;

  while (current) {
    if (!current->is_leaf) {
      std::cerr << "INVARIANT VIOLATION: Non-leaf node " << current->index
                << " found in leaf traversal" << std::endl;
      exit(1);
    }

    // Check backward link
    if (prev && current->previous != (prev ? prev->index : 0)) {
      std::cerr << "INVARIANT VIOLATION: Leaf " << current->index
                << " has previous=" << current->previous << " but should be "
                << (prev ? prev->index : 0) << std::endl;
      exit(1);
    }

    // Check forward link consistency
    if (prev && prev->next != current->index) {
      std::cerr << "INVARIANT VIOLATION: Leaf " << prev->index
                << " has next=" << prev->next << " but should be "
                << current->index << std::endl;
      exit(1);
    }

    prev = current;
    current = bp_get_next(current);
  }
}

static void bp_verify_tree_height(BPlusTree &tree, BPTreeNode *node,
                                  int expected_height, int current_height = 0) {
  if (!node)
    return;

  if (node->is_leaf) {
    if (current_height != expected_height) {
      std::cerr << "INVARIANT VIOLATION: Leaf " << node->index << " at height "
                << current_height << " but expected height " << expected_height
                << std::endl;
      exit(1);
    }
  } else {
    for (uint32_t i = 0; i <= node->num_keys; i++) {
      BPTreeNode *child = bp_get_child(tree, node, i);
      if (child) {
        bp_verify_tree_height(tree, child, expected_height, current_height + 1);
      }
    }
  }
}

static int bp_calculate_tree_height(BPlusTree &tree) {
  BPTreeNode *node = bp_get_root(tree);
  int height = 0;

  while (node && !node->is_leaf) {
    node = bp_get_child(tree, node, 0);
    height++;
  }

  return height;
}

void bp_verify_all_invariants(BPlusTree &tree) {
  BPTreeNode *root = bp_get_root(tree);
  if (!root)
    return;

  // Calculate expected height
  int expected_height = bp_calculate_tree_height(tree);

  // Verify all nodes
  std::queue<BPTreeNode *> to_visit;
  to_visit.push(root);

  while (!to_visit.empty()) {
    BPTreeNode *node = to_visit.front();
    to_visit.pop();

    bp_verify_node_invariants(tree, node);

    if (!node->is_leaf) {
      for (uint32_t i = 0; i <= node->num_keys; i++) {
        BPTreeNode *child = bp_get_child(tree, node, i);
        if (child) {
          to_visit.push(child);
        }
      }
    }
  }

  // Verify key separation
  bp_verify_key_separation(tree, root);

  // Verify leaf links
  bp_verify_leaf_links(tree);

  // Verify uniform height
  bp_verify_tree_height(tree, root, expected_height);
}


void validate_bplus_leaf_node(BPlusTree &tree, BPTreeNode *node) {
    if (!node) {
        std::cout << "// Node pointer is null" << std::endl;
        exit(0);
    }

    // Must be marked as leaf
    if (!node->is_leaf) {
        std::cout << "// Node is not marked as leaf (is_leaf = 0)" << std::endl;
        exit(0);
    }

    // Key count must be within bounds
    uint32_t min_keys = (node->parent == 0) ? 1 : tree.leaf_min_keys;  // Root can have as few as 1 key
    if (node->num_keys < min_keys) {
        std::cout << "// Leaf node has too few keys: " << node->num_keys
                  << " < " << min_keys << std::endl;
        exit(0);
    }

    if (node->num_keys > tree.leaf_max_keys) {
        std::cout << "// Leaf node has too many keys: " << node->num_keys
                  << " > " << tree.leaf_max_keys << std::endl;
        exit(0);
    }

    // Keys must be sorted in ascending order
    uint32_t *keys = get_keys(node);
    for (uint32_t i = 1; i < node->num_keys; i++) {
        if (keys[i] <= keys[i-1]) {
            std::cout << "// Leaf keys not in ascending order: keys[" << i-1
                      << "] = " << keys[i-1] << " >= keys[" << i << "] = " << keys[i] << std::endl;
            exit(0);
        }
    }

    // Record size must match tree's record size for leaf nodes
    if (node->record_size != tree.record_size) {
        std::cout << "// Leaf node record size mismatch: node=" << node->record_size
                  << " != tree=" << tree.record_size << std::endl;
        exit(0);
    }

    // If this is not the root, validate parent relationship
    if (node->parent != 0) {
        BPTreeNode *parent = static_cast<BPTreeNode *>(pager_get(node->parent));
        if (!parent) {
            std::cout << "// Cannot access parent node at page " << node->parent << std::endl;
            exit(0);
        }

        if (parent->is_leaf) {
            std::cout << "// Parent node is marked as leaf but has children" << std::endl;
            exit(0);
        }

        // Verify that parent actually points to this node
        uint32_t *parent_children = get_children(tree, parent);
        bool found_in_parent = false;
        for (uint32_t i = 0; i <= parent->num_keys; i++) {
            if (parent_children[i] == node->index) {
                found_in_parent = true;
                break;
            }
        }
        if (!found_in_parent) {
            std::cout << "// Node not found in parent's children array" << std::endl;
            exit(0);
        }
    }

    // Validate sibling links if they exist
    if (node->next != 0) {
        BPTreeNode *next_node = static_cast<BPTreeNode *>(pager_get(node->next));
        if (next_node && next_node->previous != node->index) {
            std::cout << "// Next sibling's previous pointer does not point back to this node" << std::endl;
            exit(0);
        }
    }

    if (node->previous != 0) {
        BPTreeNode *prev_node = static_cast<BPTreeNode *>(pager_get(node->previous));
        if (prev_node && prev_node->next != node->index) {
            std::cout << "// Previous sibling's next pointer does not point to this node" << std::endl;
            exit(0);
        }
    }
}

void validate_bplus_internal_node(BPlusTree &tree, BPTreeNode *node) {
    if (!node) {
        std::cout << "// Node pointer is null" << std::endl;
        exit(0);
    }

    // Must not be marked as leaf
    if (node->is_leaf) {
        std::cout << "// Node is marked as leaf but should be internal" << std::endl;
        exit(0);
    }

    // Key count must be within bounds
    uint32_t min_keys = (node->parent == 0) ? 1 : tree.internal_min_keys;  // Root can have as few as 1 key
    if (node->num_keys < min_keys) {
        std::cout << "// Internal node has too few keys: " << node->num_keys
                  << " < " << min_keys << std::endl;
        exit(0);
    }

    if (node->num_keys > tree.internal_max_keys) {
        std::cout << "// Internal node has too many keys: " << node->num_keys
                  << " > " << tree.internal_max_keys << std::endl;
        exit(0);
    }

    // Keys must be sorted in ascending order
    uint32_t *keys = get_keys(node);
    for (uint32_t i = 1; i < node->num_keys; i++) {
        if (keys[i] <= keys[i-1]) {
            std::cout << "// Internal keys not in ascending order: keys[" << i-1
                      << "] = " << keys[i-1] << " >= keys[" << i << "] = " << keys[i] << std::endl;
            exit(0);
        }
    }

    // Record size must be 0 for internal nodes in B+ trees
    if (node->record_size != 0) {
        std::cout << "// Internal node should have record_size = 0, got "
                  << node->record_size << std::endl;
        exit(0);
    }

    // Must have n+1 children for n keys
    uint32_t *children = get_children(tree, node);
    for (uint32_t i = 0; i <= node->num_keys; i++) {
        if (children[i] == 0) {
            std::cout << "// Internal node missing child at index " << i << std::endl;
            exit(0);
        }

        // Verify child exists and points back to this node as parent
        BPTreeNode *child = static_cast<BPTreeNode *>(pager_get(children[i]));
        if (!child) {
            std::cout << "// Cannot access child node at page " << children[i] << std::endl;
            exit(0);
        }

        if (child->parent != node->index) {
            std::cout << "// Child node's parent pointer does not point to this node" << std::endl;
            exit(0);
        }

        // Check no self-reference
        if (children[i] == node->index) {
            std::cout << "// Node references itself as child" << std::endl;
            exit(0);
        }
    }

    // Internal nodes should not have next/previous pointers (only leaves do)
    if (node->next != 0 || node->previous != 0) {
        std::cout << "// Internal node has sibling pointers (next=" << node->next
                  << ", prev=" << node->previous << "), but only leaves should" << std::endl;
        exit(0);
    }

    // If this is not the root, validate parent relationship
    if (node->parent != 0) {
        BPTreeNode *parent = static_cast<BPTreeNode *>(pager_get(node->parent));
        if (!parent) {
            std::cout << "// Cannot access parent node at page " << node->parent << std::endl;
            exit(0);
        }

        if (parent->is_leaf) {
            std::cout << "// Parent node is marked as leaf but has children" << std::endl;
            exit(0);
        }

        // Verify that parent actually points to this node
        uint32_t *parent_children = get_children(tree, parent);
        bool found_in_parent = false;
        for (uint32_t i = 0; i <= parent->num_keys; i++) {
            if (parent_children[i] == node->index) {
                found_in_parent = true;
                break;
            }
        }
        if (!found_in_parent) {
            std::cout << "// Node not found in parent's children array" << std::endl;
            exit(0);
        }
    }
}

void validate_btree_node(BPlusTree &tree, BPTreeNode *node) {
    if (!node) {
        std::cout << "// Node pointer is null" << std::endl;
        exit(0);
    }

    // For regular B-trees, both internal and leaf nodes store records
    // Key count must be within bounds (same for both leaf and internal in regular B-tree)
    uint32_t min_keys = (node->parent == 0) ? 1 : tree.leaf_min_keys;  // Using leaf limits since they're the same in BTREE
    uint32_t max_keys = tree.leaf_max_keys;  // Same for both in regular B-tree

    if (node->num_keys < min_keys) {
        std::cout << "// B-tree node has too few keys: " << node->num_keys
                  << " < " << min_keys << std::endl;
        exit(0);
    }

    if (node->num_keys > max_keys) {
        std::cout << "// B-tree node has too many keys: " << node->num_keys
                  << " > " << max_keys << std::endl;
        exit(0);
    }

    // Keys must be sorted in ascending order
    uint32_t *keys = get_keys(node);
    for (uint32_t i = 1; i < node->num_keys; i++) {
        if (keys[i] <= keys[i-1]) {
            std::cout << "// B-tree keys not in ascending order: keys[" << i-1
                      << "] = " << keys[i-1] << " >= keys[" << i << "] = " << keys[i] << std::endl;
            exit(0);
        }
    }

    // Record size must match tree's record size (both leaf and internal store records in B-tree)
    if (node->record_size != tree.record_size) {
        std::cout << "// B-tree node record size mismatch: node=" << node->record_size
                  << " != tree=" << tree.record_size << std::endl;
        exit(0);
    }

    // If internal node, validate children
    if (!node->is_leaf) {
        uint32_t *children = get_children(tree, node);
        for (uint32_t i = 0; i <= node->num_keys; i++) {
            if (children[i] == 0) {
                std::cout << "// B-tree internal node missing child at index " << i << std::endl;
                exit(0);
            }

            // Verify child exists and points back to this node as parent
            BPTreeNode *child = static_cast<BPTreeNode *>(pager_get(children[i]));
            if (!child) {
                std::cout << "// Cannot access child node at page " << children[i] << std::endl;
                exit(0);
            }

            if (child->parent != node->index) {
                std::cout << "// Child node's parent pointer does not point to this node" << std::endl;
                exit(0);
            }

            // Check no self-reference
            if (children[i] == node->index) {
                std::cout << "// Node references itself as child" << std::endl;
                exit(0);
            }
        }

        // Internal nodes in B-tree should not have next/previous pointers
        if (node->next != 0 || node->previous != 0) {
            std::cout << "// B-tree internal node has sibling pointers, but should not" << std::endl;
            exit(0);
        }
    } else {
        // Leaf node sibling validation (if B-tree supports leaf linking)
        if (node->next != 0) {
            BPTreeNode *next_node = static_cast<BPTreeNode *>(pager_get(node->next));
            if (next_node && next_node->previous != node->index) {
                std::cout << "// Next sibling's previous pointer does not point back to this node" << std::endl;
                exit(0);
            }
        }

        if (node->previous != 0) {
            BPTreeNode *prev_node = static_cast<BPTreeNode *>(pager_get(node->previous));
            if (prev_node && prev_node->next != node->index) {
                std::cout << "// Previous sibling's next pointer does not point to this node" << std::endl;
                exit(0);
            }
        }
    }

    // If this is not the root, validate parent relationship
    if (node->parent != 0) {
        BPTreeNode *parent = static_cast<BPTreeNode *>(pager_get(node->parent));
        if (!parent) {
            std::cout << "// Cannot access parent node at page " << node->parent << std::endl;
            exit(0);
        }

        if (parent->is_leaf) {
            std::cout << "// Parent node is marked as leaf but has children" << std::endl;
            exit(0);
        }

        // Verify that parent actually points to this node
        uint32_t *parent_children = get_children(tree, parent);
        bool found_in_parent = false;
        for (uint32_t i = 0; i <= parent->num_keys; i++) {
            if (parent_children[i] == node->index) {
                found_in_parent = true;
                break;
            }
        }
        if (!found_in_parent) {
            std::cout << "// Node not found in parent's children array" << std::endl;
            exit(0);
        }
    }
}
