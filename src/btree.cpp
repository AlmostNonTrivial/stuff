#include "btree.hpp"
#include "defs.hpp"
#include "pager.hpp"
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

#define PRINT(x, y) std::cout << x << y << "\n"

BPTreeNode *bp_find_containing_node(BPlusTree &tree, BPTreeNode *node,
                                    const uint8_t *key);
static int cmp(BPlusTree &tree, const uint8_t *key1, const uint8_t *key2) {
  switch (tree.node_key_size) {
  case TYPE_INT32: {
    uint32_t val1 = *reinterpret_cast<const uint32_t *>(key1);
    uint32_t val2 = *reinterpret_cast<const uint32_t *>(key2);
    if (val1 < val2)
      return -1;
    if (val1 > val2)
      return 1;
    return 0;
  }
  case TYPE_INT64: {
    uint64_t val1 = *reinterpret_cast<const uint64_t *>(key1);
    uint64_t val2 = *reinterpret_cast<const uint64_t *>(key2);
    if (val1 < val2)
      return -1;
    if (val1 > val2)
      return 1;
    return 0;
  }
  case TYPE_VARCHAR32:
  case TYPE_VARCHAR256: {
    return memcmp(key1, key2, tree.node_key_size);
  }
  default:
    return 0;
  }
}

// Node management
BPTreeNode *bp_create_node(BPlusTree &tree, bool is_leaf);
void bp_destroy_node(BPTreeNode *node);

void bp_set_next(BPTreeNode *node, uint32_t index);
void bp_set_prev(BPTreeNode *node, uint32_t index);
void bp_insert(BPlusTree &tree, BPTreeNode *node, uint8_t *key,
               const uint8_t *data);

void bp_insert_repair(BPlusTree &tree, BPTreeNode *node);
BPTreeNode *bp_split(BPlusTree &tree, BPTreeNode *node);
void bp_do_delete(BPlusTree &tree, BPTreeNode *node, const uint8_t *key);
void bp_repair_after_delete(BPlusTree &tree, BPTreeNode *node);
BPTreeNode *bp_merge_right(BPlusTree &tree, BPTreeNode *node);
BPTreeNode *bp_steal_from_left(BPlusTree &tree, BPTreeNode *node,
                               uint32_t parent_index);
BPTreeNode *bp_steal_from_right(BPlusTree &tree, BPTreeNode *node,
                                uint32_t parent_index);
void bp_update_parent_keys(BPlusTree &tree, BPTreeNode *node,
                           const uint8_t *deleted_key);

void bp_print_node(BPlusTree &tree, BPTreeNode *node);

// Helper functions to access arrays within the node's data area
uint8_t *get_keys(BPTreeNode *node) { return node->data; }

// Helper function to get a specific key
uint8_t *get_key_at(BPlusTree &tree, BPTreeNode *node, uint32_t index) {
  return get_keys(node) + index * tree.node_key_size;
}

static uint32_t *get_children(BPlusTree &tree, BPTreeNode *node) {
  if (tree.tree_type == BTREE && !node->is_leaf) {
    // In B-trees, children come after keys AND records
    return reinterpret_cast<uint32_t *>(
        node->data + tree.internal_max_keys * tree.node_key_size + // keys
        tree.internal_max_keys * tree.record_size                  // records
    );
  } else {
    // B+tree layout unchanged
    return reinterpret_cast<uint32_t *>(node->data + tree.internal_max_keys *
                                                         tree.node_key_size);
  }
}

uint8_t *get_internal_record_data(BPlusTree &tree, BPTreeNode *node) {
  // For B-trees, records come after keys in internal nodes
  return node->data + tree.internal_max_keys * tree.node_key_size;
}

uint8_t *get_internal_record_at(BPlusTree &tree, BPTreeNode *node,
                                uint32_t index) {
  if (node->is_leaf || index >= node->num_keys)
    return nullptr;
  return get_internal_record_data(tree, node) + (index * tree.record_size);
}

static uint8_t *get_record_data(BPlusTree &tree, BPTreeNode *node) {
  return node->data + tree.leaf_max_keys * tree.node_key_size;
}

uint8_t *get_record_at(BPlusTree &tree, BPTreeNode *node, uint32_t index) {
  if (index >= node->num_keys)
    return nullptr;

  if (node->is_leaf) {
    return get_record_data(tree, node) + (index * tree.record_size);
  } else if (tree.tree_type == BTREE) {
    return get_internal_record_at(tree, node, index);
  }
  return nullptr;
}

// Forward declaration
static bool bp_find_in_tree(BPlusTree &tree, BPTreeNode *node,
                            const uint8_t *key);

std::vector<uint32_t> db_get_keys(BPlusTree &tree, BPTreeNode *n) {
  std::vector<uint32_t> result;
  result.reserve(n->num_keys);

  for (size_t i = 0; i < n->num_keys; ++i) {
    uint32_t value;
    // Use memcpy to avoid undefined behavior due to alignment or aliasing
    std::memcpy(&value, n->data + i * sizeof(uint32_t), sizeof(uint32_t));
    result.push_back(value);
  }
  return result;
}
// Combined btree.cpp functions - calculate_capacity and create merged

/*

internal:
A tree with n keys will have n+1 children


 */
// Returns position/index based on node type and tree type
// For leaf nodes: returns exact key position or insertion point
// For internal nodes: returns child index to follow
static uint32_t bp_binary_search(BPlusTree &tree, BPTreeNode *node,
                                 const uint8_t *key) {
  uint32_t left = 0;
  uint32_t right = node->num_keys;

  while (left < right) {
    uint32_t mid = left + (right - left) / 2;
    int cmp_result = cmp(tree, get_key_at(tree, node, mid), key);

    if (cmp_result < 0) {
      left = mid + 1;
    } else if (cmp_result == 0) {
      // Found exact match - handle based on node type and tree type

      // Btree: return found internal or leaf
      if (tree.tree_type == BTREE) {
        return mid;
      }
      // B+tree leaf
      if (node->is_leaf) {
        // Leaf node: return exact position regardless of tree type
        return mid;
      }

      // B+tree internal: equal keys in internal nodes are separators
      // Key exists in right subtree, so go right
      return mid + 1;

    } else {
      right = mid;
    }
  }

  // No exact match found
  if (node->is_leaf) {
    // Leaf node: return insertion point
    return left;
  } else {
    // Internal node: return child index to follow
    return left;
  }
}
BPlusTree bt_create(DataType key, uint32_t record_size, TreeType tree_type) {
  BPlusTree tree;
  tree.node_key_size = key;
  tree.tree_type = tree_type;

  tree.record_size = record_size;

  constexpr uint32_t USABLE_SPACE = PAGE_SIZE - NODE_HEADER_SIZE;
  const uint32_t minimum_entry_count = 3U;

  /* require minimum for algorithm to work */
  if ((record_size * minimum_entry_count) > USABLE_SPACE) {
    tree.tree_type = INVALID;
    return tree;
  }

  if (tree_type == BTREE) {
    /*
    B-tree internal and leaf nodes: [keys][records][children]
    Need space for n keys + n records + (n+1) child pointers

    [ptr, <key, record>, ptr, <key, record>, ptr] <- conceptual layout
    [key, key, record, record, ptr, ptr, ptr] <- actual layout
    */
    uint32_t key_record_size = tree.node_key_size + record_size;
    uint32_t child_ptr_size = TYPE_INT32;

    uint32_t max_keys =
        (USABLE_SPACE - child_ptr_size) / (key_record_size + child_ptr_size);

    max_keys = std::max(minimum_entry_count, max_keys);

    uint32_t min_keys = max_keys / 2;
    uint32_t split_index = max_keys / 2;

    tree.leaf_max_keys = max_keys;
    tree.leaf_min_keys = min_keys;
    tree.leaf_split_index = split_index;
    tree.internal_max_keys = max_keys;
    tree.internal_min_keys = min_keys;
    tree.internal_split_index = split_index;
  } else if (tree_type == BPLUS) {

    // Leaf nodes: [keys][records]
    uint32_t leaf_entry_size = tree.node_key_size + record_size;
    uint32_t leaf_max_entries = USABLE_SPACE / leaf_entry_size;

    tree.leaf_max_keys = std::max(minimum_entry_count, leaf_max_entries);
    tree.leaf_min_keys = tree.leaf_max_keys / 2;
    tree.leaf_split_index = tree.leaf_max_keys / 2;

    uint32_t child_ptr_size = TYPE_INT32;
    // Internal nodes: [keys][children] (no records)
    uint32_t internal_max_entries =
        (USABLE_SPACE - child_ptr_size) / (tree.node_key_size + child_ptr_size);
    tree.internal_max_keys =
        std::max(minimum_entry_count, internal_max_entries);
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
    PRINT("nah", "");
    return nullptr;
  }

  pager_mark_dirty(page_index);

  node->index = page_index;
  node->parent = 0;
  node->next = 0;
  node->previous = 0;
  node->num_keys = 0;
  node->is_leaf = is_leaf ? 1 : 0;
  // node->record_size = is_leaf ? tree.record_size : 0;

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
              << " its own parent!" << std::endl;
    // exit(1);
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
  if (node->index == node_index) {
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

void bp_insert_element(BPlusTree &tree, void *key, const uint8_t *data) {
  BPTreeNode *root = bp_get_root(tree);

  if (root->num_keys == 0) {
    bp_mark_dirty(root);
    uint8_t *keys = get_keys(root);
    uint8_t *record_data = get_record_data(tree, root);

    memcpy(keys, key, tree.node_key_size);
    memcpy(record_data, data, tree.record_size);
    root->num_keys = 1;
  } else {
    auto ss = db_get_keys(tree, root);
    bp_insert(tree, root, (uint8_t *)key, data);
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

void bp_insert(BPlusTree &tree, BPTreeNode *node, uint8_t *key,
               const uint8_t *data) {
  if (node->is_leaf) {
    uint8_t *keys = get_keys(node);
    uint8_t *record_data = get_record_data(tree, node);

    uint32_t insert_index = bp_binary_search(tree, node, key);

    // B+tree: check for existing key to update
    // B-tree: allow duplicates
    if (tree.tree_type == BPLUS && insert_index < node->num_keys &&
        cmp(tree, get_key_at(tree, node, insert_index), key) == 0) {
      bp_mark_dirty(node);
      memcpy(record_data + insert_index * tree.record_size, data,
             tree.record_size);
      return;
    }

    // Check if split needed
    if (node->num_keys >= tree.leaf_max_keys) {
      bp_insert_repair(tree, node);
      bp_insert(tree, bp_find_containing_node(tree, bp_get_root(tree), key),
                key, data);
      return;
    }

    bp_mark_dirty(node);

    // For B-tree duplicates: insert after existing equal keys
    if (tree.tree_type == BTREE) {
      while (insert_index < node->num_keys &&
             cmp(tree, get_key_at(tree, node, insert_index), key) == 0) {
        insert_index++;
      }
    }

    uint32_t num_to_shift = node->num_keys - insert_index;

    // Shift keys right by one position
    memcpy(get_key_at(tree, node, insert_index + 1),
           get_key_at(tree, node, insert_index),
           num_to_shift * tree.node_key_size);

    // Shift records right by one position
    memcpy(record_data + (insert_index + 1) * tree.record_size,
           record_data + insert_index * tree.record_size,
           num_to_shift * tree.record_size);

    // Insert new key and record
    memcpy(get_key_at(tree, node, insert_index), key, tree.node_key_size);
    memcpy(record_data + insert_index * tree.record_size, data,
           tree.record_size);

    node->num_keys++;
  } else {
    // Internal node - use binary search to find child
    uint32_t child_index = bp_binary_search(tree, node, key);

    BPTreeNode *child_node = bp_get_child(tree, node, child_index);
    if (child_node) {
      bp_insert(tree, child_node, key, data);
    }
  }
}

// ============================================
// 4. MODIFY bp_split FOR B-TREE RECORDS
// ============================================

BPTreeNode *bp_split(BPlusTree &tree, BPTreeNode *node) {
  BPTreeNode *right_node = bp_create_node(tree, node->is_leaf);

  if (!right_node || right_node->index == 0) {
    std::cerr << "ERROR: Failed to create right_node in split" << std::endl;
    exit(1);
  }

  if (right_node->index == node->index) {
    std::cerr << "ERROR: right_node has same index as original node: "
              << node->index << std::endl;
    exit(1);
  }

  uint32_t split_index = bp_get_split_index(tree, node);
  uint8_t *node_keys = get_keys(node);
  uint8_t *rising_key = get_key_at(tree, node, split_index);
  uint32_t parent_index = 0;

  // Parent handling (same as before)
  if (node->parent != 0) {
    BPTreeNode *current_parent = bp_get_parent(node);
    uint32_t *parent_children = get_children(tree, current_parent);

    for (parent_index = 0; parent_index < current_parent->num_keys + 1 &&
                           parent_children[parent_index] != node->index;
         parent_index++)
      ;

    if (parent_index == current_parent->num_keys + 1) {
      throw std::runtime_error("Couldn't find which child we were!");
    }

    bp_mark_dirty(current_parent);

    // Shift parent's keys and children to make room
    // First shift children (need to shift n+1 children for n keys)
    for (uint32_t i = current_parent->num_keys; i > parent_index; i--) {
      parent_children[i + 1] = parent_children[i];
    }
    // Then shift keys
    for (uint32_t i = current_parent->num_keys; i > parent_index; i--) {
      memcpy(get_key_at(tree, current_parent, i),
             get_key_at(tree, current_parent, i - 1), tree.node_key_size);
    }

    // For B-tree internal nodes, also shift records
    if (tree.tree_type == BTREE && !current_parent->is_leaf) {
      uint8_t *parent_records = get_internal_record_data(tree, current_parent);
      for (uint32_t i = current_parent->num_keys; i > parent_index; i--) {
        memcpy(parent_records + i * tree.record_size,
               parent_records + (i - 1) * tree.record_size, tree.record_size);
      }
    }

    current_parent->num_keys++;
    memcpy(get_key_at(tree, current_parent, parent_index), rising_key,
           tree.node_key_size);

    // Debug check before setting child
    if (right_node->index == current_parent->index) {
      std::cerr << "ERROR: About to make parent " << current_parent->index
                << " and right_node the same at position " << (parent_index + 1)
                << std::endl;
      exit(1);
    }

    bp_set_child(tree, current_parent, parent_index + 1, right_node->index);

    // For B-tree: copy the rising key's record to parent
    if (tree.tree_type == BTREE && !node->is_leaf) {
      uint8_t *parent_records = get_internal_record_data(tree, current_parent);
      uint8_t *node_records = get_internal_record_data(tree, node);
      memcpy(parent_records + parent_index * tree.record_size,
             node_records + split_index * tree.record_size, tree.record_size);
    }
  }

  // Handle leaf sibling links
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

  uint8_t *right_keys = get_keys(right_node);

  // Handle the split differently for leaf vs internal, and B-tree vs B+tree
  if (node->is_leaf) {
    // Leaf nodes: split at split_index
    right_node->num_keys = node->num_keys - split_index;

    // Copy keys to right node
    for (uint32_t i = split_index; i < node->num_keys; i++) {
      memcpy(get_key_at(tree, right_node, i - split_index),
             get_key_at(tree, node, i), tree.node_key_size);
    }

    // Copy records
    uint8_t *node_records = get_record_data(tree, node);
    uint8_t *right_records = get_record_data(tree, right_node);
    memcpy(right_records, node_records + split_index * tree.record_size,
           right_node->num_keys * tree.record_size);

  } else {
    // Internal nodes
    if (tree.tree_type == BTREE) {
      // B-tree: rising key at split_index goes to parent
      // Keys after split_index go to right node
      right_node->num_keys = node->num_keys - split_index - 1;

      // Copy keys after the rising key to right node
      for (uint32_t i = split_index + 1; i < node->num_keys; i++) {
        memcpy(get_key_at(tree, right_node, i - split_index - 1),
               get_key_at(tree, node, i), tree.node_key_size);
      }

      // Copy records (excluding the rising key's record which went to parent)
      uint8_t *node_records = get_internal_record_data(tree, node);
      uint8_t *right_records = get_internal_record_data(tree, right_node);
      for (uint32_t i = split_index + 1; i < node->num_keys; i++) {
        memcpy(right_records + (i - split_index - 1) * tree.record_size,
               node_records + i * tree.record_size, tree.record_size);
      }

      // Copy children (starting from split_index + 1)
      uint32_t *node_children = get_children(tree, node);
      for (uint32_t i = split_index + 1; i <= node->num_keys; i++) {
        uint32_t child_to_move = node_children[i];
        uint32_t target_index = i - split_index - 1;

        std::cerr << "DEBUG: Moving child from node " << node->index
                  << " position " << i << " (child=" << child_to_move
                  << ") to right_node " << right_node->index << " position "
                  << target_index << std::endl;

        if (child_to_move != 0) {
          if (child_to_move == right_node->index) {
            std::cerr << "ERROR: Trying to make right_node "
                      << right_node->index << " its own child!" << std::endl;
            exit(1);
          }
          bp_set_child(tree, right_node, target_index, child_to_move);
          node_children[i] = 0;
        }
      }

    } else {
      // B+tree: rising key stays in nodes
      right_node->num_keys = node->num_keys - split_index - 1;

      // Copy keys after split_index to right node
      for (uint32_t i = split_index + 1; i < node->num_keys; i++) {
        memcpy(get_key_at(tree, right_node, i - split_index - 1),
               get_key_at(tree, node, i), tree.node_key_size);
      }

      // Copy children
      uint32_t *node_children = get_children(tree, node);
      uint32_t *right_children = get_children(tree, right_node);
      for (uint32_t i = split_index + 1; i <= node->num_keys; i++) {
        uint32_t child_to_move = node_children[i];
        if (child_to_move != 0) {
          bp_set_child(tree, right_node, i - split_index - 1, child_to_move);
          node_children[i] = 0;
        }
      }
    }
  }

  // Update left node's key count
  node->num_keys = split_index;

  // Handle root creation if needed
  if (node->parent != 0) {
    return bp_get_parent(node);
  } else {
    // Create new root
    BPTreeNode *new_root = bp_create_node(tree, false);
    uint8_t *new_root_keys = get_keys(new_root);

    memcpy(get_key_at(tree, new_root, 0), rising_key, tree.node_key_size);
    new_root->num_keys = 1;

    // For B-tree: copy record to new root
    if (tree.tree_type == BTREE && !node->is_leaf) {
      uint8_t *new_root_records = get_internal_record_data(tree, new_root);
      uint8_t *node_records = get_internal_record_data(tree, node);
      memcpy(new_root_records, node_records + split_index * tree.record_size,
             tree.record_size);
    }

    bp_set_child(tree, new_root, 0, node->index);
    bp_set_child(tree, new_root, 1, right_node->index);
    tree.root_page_index = new_root->index;
    return new_root;
  }
}

bool bp_find_element(BPlusTree &tree, void *key) {
  BPTreeNode *root = bp_get_root(tree);
  return bp_find_in_tree(tree, root, (uint8_t *)key);
}

static bool bp_find_in_tree(BPlusTree &tree, BPTreeNode *node,
                            const uint8_t *key) {
  if (!node)
    return false;

  uint32_t i;

  for (i = 0;
       i < node->num_keys && cmp(tree, get_key_at(tree, node, i), key) < 0; i++)
    ;

  if (i == node->num_keys) {
    if (!node->is_leaf) {
      return bp_find_in_tree(tree, bp_get_child(tree, node, node->num_keys),
                             key);
    } else {
      return false;
    }
  } else if (cmp(tree, get_key_at(tree, node, i), key) > 0) {
    if (!node->is_leaf) {
      return bp_find_in_tree(tree, bp_get_child(tree, node, i), key);
    } else {
      return false;
    }
  } else {
    if (cmp(tree, get_key_at(tree, node, i), key) == 0 && node->is_leaf) {
      return true;
    } else {
      return bp_find_in_tree(tree, bp_get_child(tree, node, i + 1), key);
    }
  }
}

const uint8_t *bp_get(BPlusTree &tree, void *key) {

  BPTreeNode *root = bp_get_root(tree);
  BPTreeNode *leaf_node = bp_find_containing_node(tree, root, (uint8_t *)key);
  if (!leaf_node)
    return nullptr;

  uint32_t pos = bp_binary_search(tree, leaf_node, (uint8_t *)key);
  if (pos < leaf_node->num_keys &&
      cmp(tree, get_key_at(tree, leaf_node, pos), (uint8_t *)key) == 0) {
    return get_record_at(tree, leaf_node, pos);
  }

  return nullptr;
}

BPTreeNode *bp_find_containing_node(BPlusTree &tree, BPTreeNode *node,
                                    const uint8_t *key) {
  if (node->is_leaf) {
    return const_cast<BPTreeNode *>(node);
  }

  uint32_t child_or_key_index = bp_binary_search(tree, node, key);
  if (tree.tree_type == BTREE) {
    if (cmp(tree, key, get_key_at(tree, node, child_or_key_index)) == 0) {
      return const_cast<BPTreeNode *>(node);
    }
  }

  return bp_find_containing_node(
      tree, bp_get_child(tree, node, child_or_key_index), key);
}

BPTreeNode *bp_left_most(BPlusTree &tree) {
  BPTreeNode *temp = bp_get_root(tree);

  while (temp && !temp->is_leaf) {
    temp = bp_get_child(tree, temp, 0);
  }

  return temp;
}

// The issue is that we need to delete the predecessor from the leaf
// without triggering repairs that might free the current node
static void bp_delete_internal_btree(BPlusTree &tree, BPTreeNode *node,
                                     uint32_t index) {
  // Find predecessor in left subtree
  BPTreeNode *left_child = bp_get_child(tree, node, index);
  if (!left_child)
    return;

  // Navigate to rightmost leaf in left subtree
  BPTreeNode *curr = left_child;
  while (!curr->is_leaf) {
    curr = bp_get_child(tree, curr, curr->num_keys);
  }

  // Get the predecessor (rightmost key in the leaf)
  uint32_t pred_index = curr->num_keys - 1;
  uint8_t *pred_key = get_key_at(tree, curr, pred_index);
  uint8_t *pred_record = get_record_at(tree, curr, pred_index);

  // Replace the key and record in the internal node
  bp_mark_dirty(node);
  memcpy(get_key_at(tree, node, index), pred_key, tree.node_key_size);

  uint8_t *internal_records = get_internal_record_data(tree, node);
  memcpy(internal_records + index * tree.record_size, pred_record,
         tree.record_size);

  // Now delete the predecessor from the leaf node directly
  // without going through bp_do_delete which might restructure the tree
  bp_mark_dirty(curr);
  uint8_t *leaf_records = get_record_data(tree, curr);

  // Shift keys and records in the leaf to remove the predecessor
  for (uint32_t j = pred_index; j < curr->num_keys - 1; j++) {
    memcpy(get_key_at(tree, curr, j), get_key_at(tree, curr, j + 1),
           tree.node_key_size);
    memcpy(leaf_records + j * tree.record_size,
           leaf_records + (j + 1) * tree.record_size, tree.record_size);
  }
  curr->num_keys--;

  // Now repair the leaf if needed
  // This is safe because we're done modifying the internal node
  bp_repair_after_delete(tree, curr);
}

void bp_delete_element(BPlusTree &tree, void *key) {
  BPTreeNode *root = bp_get_root(tree);
  if (!root)
    return;

  bp_do_delete(tree, root, (uint8_t *)key);

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

// Simplified B-tree deletion that reuses existing logic
void bp_do_delete(BPlusTree &tree, BPTreeNode *node, const uint8_t *key) {
  if (!node)
    return;

  uint32_t i;

  // Find position of key
  for (i = 0;
       i < node->num_keys && cmp(tree, get_key_at(tree, node, i), key) < 0; i++)
    ;

  if (tree.tree_type == BTREE) {
    // B-tree specific handling
    if (i < node->num_keys && cmp(tree, get_key_at(tree, node, i), key) == 0) {
      // Found key in this node
      if (node->is_leaf) {
        // Delete from leaf - same as B+tree leaf deletion
        bp_mark_dirty(node);
        uint8_t *record_data = get_record_data(tree, node);

        // Shift left
        for (uint32_t j = i; j < node->num_keys - 1; j++) {
          memcpy(get_key_at(tree, node, j), get_key_at(tree, node, j + 1),
                 tree.node_key_size);
          memcpy(record_data + j * tree.record_size,
                 record_data + (j + 1) * tree.record_size, tree.record_size);
        }
        node->num_keys--;

        // Use existing repair logic
        bp_repair_after_delete(tree, node);
      } else {
        // Delete from internal node - use predecessor/successor replacement
        // This is the only truly new logic needed
        bp_delete_internal_btree(tree, node, i);
      }
    } else if (!node->is_leaf) {
      // Not in this node, recurse to appropriate child
      if (i < node->num_keys && cmp(tree, get_key_at(tree, node, i), key) > 0) {
        bp_do_delete(tree, bp_get_child(tree, node, i), key);
      } else {
        bp_do_delete(tree, bp_get_child(tree, node, i), key);
      }
    }
  } else {
    // B+tree - existing logic unchanged
    if (i == node->num_keys) {
      if (!node->is_leaf) {
        bp_do_delete(tree, bp_get_child(tree, node, node->num_keys), key);
      }
    } else if (!node->is_leaf &&
               cmp(tree, get_key_at(tree, node, i), key) == 0) {
      bp_do_delete(tree, bp_get_child(tree, node, i + 1), key);
    } else if (!node->is_leaf) {
      bp_do_delete(tree, bp_get_child(tree, node, i), key);
    } else if (node->is_leaf &&
               cmp(tree, get_key_at(tree, node, i), key) == 0) {
      bp_mark_dirty(node);
      uint8_t *record_data = get_record_data(tree, node);

      for (uint32_t j = i; j < node->num_keys - 1; j++) {
        memcpy(get_key_at(tree, node, j), get_key_at(tree, node, j + 1),
               tree.node_key_size);
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
}

// The existing bp_repair_after_delete already handles most of what we need!
// It already:
// - Checks minimum key requirements
// - Steals from siblings (bp_steal_from_left/right)
// - Merges nodes (bp_merge_right)
// - Updates parent keys
// All this logic works for B-trees too!

// We just need small tweaks to stealing/merging to handle internal records:

BPTreeNode *bp_steal_from_left(BPlusTree &tree, BPTreeNode *node,
                               uint32_t parent_index) {

  BPTreeNode *parent_node = bp_get_parent(node);
  BPTreeNode *left_sibling = bp_get_child(tree, parent_node, parent_index - 1);

  bp_mark_dirty(node);
  bp_mark_dirty(parent_node);
  bp_mark_dirty(left_sibling);

  // Shift node's keys and records right
  for (uint32_t i = node->num_keys; i > 0; i--) {
    memcpy(get_key_at(tree, node, i), get_key_at(tree, node, i - 1),
           tree.node_key_size);
  }

  if (node->is_leaf) {
    // Leaf: move sibling's last key/record to node's first position
    memcpy(get_key_at(tree, node, 0),
           get_key_at(tree, left_sibling, left_sibling->num_keys - 1),
           tree.node_key_size);

    uint8_t *node_records = get_record_data(tree, node);
    uint8_t *sibling_records = get_record_data(tree, left_sibling);

    // Shift node's records right
    memmove(node_records + tree.record_size, node_records,
            node->num_keys * tree.record_size);

    // Copy sibling's last record to node's first
    memcpy(node_records,
           sibling_records + (left_sibling->num_keys - 1) * tree.record_size,
           tree.record_size);

    // Update parent key
    memcpy(get_key_at(tree, parent_node, parent_index - 1),
           get_key_at(tree, node, 0), tree.node_key_size);
  } else {
    // Internal node: rotate through parent

    // Move parent key down to node
    memcpy(get_key_at(tree, node, 0),
           get_key_at(tree, parent_node, parent_index - 1), tree.node_key_size);

    // For B-tree: also handle records in internal nodes
    if (tree.tree_type == BTREE) {
      uint8_t *node_records = get_internal_record_data(tree, node);
      uint8_t *parent_records = get_internal_record_data(tree, parent_node);
      uint8_t *sibling_records = get_internal_record_data(tree, left_sibling);

      // Shift node's records right
      memmove(node_records + tree.record_size, node_records,
              node->num_keys * tree.record_size);

      // Move parent record down to node
      memcpy(node_records,
             parent_records + (parent_index - 1) * tree.record_size,
             tree.record_size);

      // Move sibling's last record up to parent
      memcpy(get_key_at(tree, parent_node, parent_index - 1),
             get_key_at(tree, left_sibling, left_sibling->num_keys - 1),
             tree.node_key_size);
      memcpy(parent_records + (parent_index - 1) * tree.record_size,
             sibling_records + (left_sibling->num_keys - 1) * tree.record_size,
             tree.record_size);
    } else {
      // B+tree: just move key up from sibling
      memcpy(get_key_at(tree, parent_node, parent_index - 1),
             get_key_at(tree, left_sibling, left_sibling->num_keys - 1),
             tree.node_key_size);
    }

    // Move child pointer
    uint32_t *node_children = get_children(tree, node);
    uint32_t *sibling_children = get_children(tree, left_sibling);

    for (uint32_t i = node->num_keys + 1; i > 0; i--) {
      bp_set_child(tree, node, i, node_children[i - 1]);
    }
    bp_set_child(tree, node, 0, sibling_children[left_sibling->num_keys]);
  }

  node->num_keys++;
  left_sibling->num_keys--;

  return parent_node;
}

// Similar small modifications needed for:
// - bp_steal_from_right (mirror of above)
// - bp_merge_right (needs to handle internal records for B-tree)

void bp_update_parent_keys(BPlusTree &tree, BPTreeNode *node,
                           const uint8_t *deleted_key) {
  uint8_t *next_smallest = nullptr;
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
      next_smallest = nullptr;
    } else {
      BPTreeNode *next_sibling =
          bp_get_child(tree, parent_node, parent_index + 1);
      if (next_sibling) {
        next_smallest = get_key_at(tree, next_sibling, 0);
      }
    }
  } else {
    next_smallest = get_key_at(tree, node, 0);
  }

  BPTreeNode *current_parent = parent_node;
  while (current_parent) {
    if (parent_index > 0 &&
        cmp(tree, get_key_at(tree, current_parent, parent_index - 1),
            deleted_key) == 0) {
      bp_mark_dirty(current_parent);
      if (next_smallest) {
        memcpy(get_key_at(tree, current_parent, parent_index - 1),
               next_smallest, tree.node_key_size);
      }
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
        bp_repair_after_delete(tree, parent_node);
      } else if (left_sibling) {
        BPTreeNode *next_node = bp_merge_right(tree, left_sibling);
        bp_repair_after_delete(tree, parent_node);
      }
    }
  }
}

// B-tree modifications for steal_from_right and merge operations

BPTreeNode *bp_steal_from_right(BPlusTree &tree, BPTreeNode *node,
                                uint32_t parent_index) {
  BPTreeNode *parent_node = bp_get_parent(node);
  BPTreeNode *right_sibling = bp_get_child(tree, parent_node, parent_index + 1);

  bp_mark_dirty(node);
  bp_mark_dirty(parent_node);
  bp_mark_dirty(right_sibling);

  if (node->is_leaf) {
    // Leaf: move sibling's first key/record to node's last position
    memcpy(get_key_at(tree, node, node->num_keys),
           get_key_at(tree, right_sibling, 0), tree.node_key_size);

    uint8_t *node_records = get_record_data(tree, node);
    uint8_t *sibling_records = get_record_data(tree, right_sibling);

    // Copy sibling's first record to node's last
    memcpy(node_records + node->num_keys * tree.record_size, sibling_records,
           tree.record_size);

    // Shift sibling's keys and records left
    for (uint32_t i = 0; i < right_sibling->num_keys - 1; i++) {
      memcpy(get_key_at(tree, right_sibling, i),
             get_key_at(tree, right_sibling, i + 1), tree.node_key_size);
      memcpy(sibling_records + i * tree.record_size,
             sibling_records + (i + 1) * tree.record_size, tree.record_size);
    }

    // Update parent key
    memcpy(get_key_at(tree, parent_node, parent_index),
           get_key_at(tree, right_sibling, 0), tree.node_key_size);
  } else {
    // Internal node: rotate through parent

    // Move parent key down to node
    memcpy(get_key_at(tree, node, node->num_keys),
           get_key_at(tree, parent_node, parent_index), tree.node_key_size);

    // For B-tree: also handle records in internal nodes
    if (tree.tree_type == BTREE) {
      uint8_t *node_records = get_internal_record_data(tree, node);
      uint8_t *parent_records = get_internal_record_data(tree, parent_node);
      uint8_t *sibling_records = get_internal_record_data(tree, right_sibling);

      // Move parent record down to node
      memcpy(node_records + node->num_keys * tree.record_size,
             parent_records + parent_index * tree.record_size,
             tree.record_size);

      // Move sibling's first key and record up to parent
      memcpy(get_key_at(tree, parent_node, parent_index),
             get_key_at(tree, right_sibling, 0), tree.node_key_size);
      memcpy(parent_records + parent_index * tree.record_size, sibling_records,
             tree.record_size);

      // Shift sibling's records left
      for (uint32_t i = 0; i < right_sibling->num_keys - 1; i++) {
        memcpy(sibling_records + i * tree.record_size,
               sibling_records + (i + 1) * tree.record_size, tree.record_size);
      }
    } else {
      // B+tree: just move key up from sibling
      memcpy(get_key_at(tree, parent_node, parent_index),
             get_key_at(tree, right_sibling, 0), tree.node_key_size);
    }

    // Shift sibling's keys left
    for (uint32_t i = 0; i < right_sibling->num_keys - 1; i++) {
      memcpy(get_key_at(tree, right_sibling, i),
             get_key_at(tree, right_sibling, i + 1), tree.node_key_size);
    }

    // Move child pointer
    uint32_t *node_children = get_children(tree, node);
    uint32_t *sibling_children = get_children(tree, right_sibling);

    // Take sibling's first child
    bp_set_child(tree, node, node->num_keys + 1, sibling_children[0]);

    // Shift sibling's children left
    for (uint32_t i = 0; i < right_sibling->num_keys; i++) {
      bp_set_child(tree, right_sibling, i, sibling_children[i + 1]);
    }
  }

  node->num_keys++;
  right_sibling->num_keys--;

  return parent_node;
}

BPTreeNode *bp_merge_right(BPlusTree &tree, BPTreeNode *node) {
  BPTreeNode *parent = bp_get_parent(node);
  if (!parent)
    return node;

  uint32_t *parent_children = get_children(tree, parent);

  // Find this node's position in parent
  uint32_t node_index = 0;
  for (; node_index <= parent->num_keys; node_index++) {
    if (parent_children[node_index] == node->index)
      break;
  }

  if (node_index >= parent->num_keys)
    return node; // No right sibling

  BPTreeNode *right_sibling = bp_get_child(tree, parent, node_index + 1);
  if (!right_sibling)
    return node;

  bp_mark_dirty(node);
  bp_mark_dirty(parent);

  if (node->is_leaf) {
    // Merge leaf nodes
    uint8_t *node_records = get_record_data(tree, node);
    uint8_t *sibling_records = get_record_data(tree, right_sibling);

    // Copy all keys and records from sibling to node
    for (uint32_t i = 0; i < right_sibling->num_keys; i++) {
      memcpy(get_key_at(tree, node, node->num_keys + i),
             get_key_at(tree, right_sibling, i), tree.node_key_size);
      memcpy(node_records + (node->num_keys + i) * tree.record_size,
             sibling_records + i * tree.record_size, tree.record_size);
    }

    node->num_keys += right_sibling->num_keys;

    // Update leaf links (only for B+tree)
    if (tree.tree_type == BPLUS) {
      bp_set_next(node, right_sibling->next);
      if (right_sibling->next != 0) {
        BPTreeNode *next_node = bp_get_next(right_sibling);
        if (next_node) {
          bp_set_prev(next_node, node->index);
        }
      }
    }
  } else {
    // Merge internal nodes

    // Pull down parent key (this becomes the separator)
    memcpy(get_key_at(tree, node, node->num_keys),
           get_key_at(tree, parent, node_index), tree.node_key_size);

    // For B-tree: also copy parent's record
    if (tree.tree_type == BTREE) {
      uint8_t *node_records = get_internal_record_data(tree, node);
      uint8_t *parent_records = get_internal_record_data(tree, parent);
      uint8_t *sibling_records = get_internal_record_data(tree, right_sibling);

      // Copy parent's record for the separator key
      memcpy(node_records + node->num_keys * tree.record_size,
             parent_records + node_index * tree.record_size, tree.record_size);

      // Copy all sibling's records
      for (uint32_t i = 0; i < right_sibling->num_keys; i++) {
        memcpy(node_records + (node->num_keys + 1 + i) * tree.record_size,
               sibling_records + i * tree.record_size, tree.record_size);
      }
    }

    // Copy all keys from sibling
    for (uint32_t i = 0; i < right_sibling->num_keys; i++) {
      memcpy(get_key_at(tree, node, node->num_keys + 1 + i),
             get_key_at(tree, right_sibling, i), tree.node_key_size);
    }

    // Copy all child pointers from sibling
    uint32_t *node_children = get_children(tree, node);
    uint32_t *sibling_children = get_children(tree, right_sibling);

    for (uint32_t i = 0; i <= right_sibling->num_keys; i++) {
      bp_set_child(tree, node, node->num_keys + 1 + i, sibling_children[i]);
    }

    node->num_keys += 1 + right_sibling->num_keys;
  }

  // Remove sibling from parent
  for (uint32_t i = node_index; i < parent->num_keys - 1; i++) {
    memcpy(get_key_at(tree, parent, i), get_key_at(tree, parent, i + 1),
           tree.node_key_size);
    parent_children[i + 1] = parent_children[i + 2];

    // For B-tree: also shift parent's internal records
    if (tree.tree_type == BTREE && !parent->is_leaf) {
      uint8_t *parent_records = get_internal_record_data(tree, parent);
      memcpy(parent_records + i * tree.record_size,
             parent_records + (i + 1) * tree.record_size, tree.record_size);
    }
  }

  parent->num_keys--;

  // Clean up sibling
  bp_destroy_node(right_sibling);

  // Check if parent needs repair
  if (parent->num_keys < bp_get_min_keys(tree, parent)) {
    if (parent->parent == 0 && parent->num_keys == 0) {
      // Parent is root and now empty - make node the new root
      tree.root_page_index = node->index;
      bp_set_parent(node, 0);
      bp_destroy_node(parent);
      return node;
    } else {
      bp_repair_after_delete(tree, parent);
    }
  }

  return node;
}

void print_uint8_as_chars(const uint8_t *data, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    printf("%c", data[i]);
  }
  printf("\n");
}

void bp_print_node(BPlusTree &tree, BPTreeNode *node) {
  if (!node) {
    std::cout << "NULL node" << std::endl;
    return;
  }

  // Print basic node information
  std::cout << "=== NODE " << node->index << " ===" << std::endl;

  std::cout << "Node Type: ";
  if (node->is_leaf) {
    std::cout << "LEAF";
  } else {
    std::cout << "INTERNAL";
  }
  std::cout << std::endl;

  // Print node metadata
  std::cout << "Page Index: " << node->index << std::endl;
  std::cout << "Parent: "
            << (node->parent == 0 ? "ROOT" : std::to_string(node->parent))
            << std::endl;
  std::cout << "Keys: " << node->num_keys;

  // Print capacity info based on node type
  if (node->is_leaf) {
    std::cout << "/" << tree.leaf_max_keys << " (min: " << tree.leaf_min_keys
              << ")";
  } else {
    std::cout << "/" << tree.internal_max_keys
              << " (min: " << tree.internal_min_keys << ")";
  }
  std::cout << std::endl;

  // Print sibling links for leaf nodes
  if (node->is_leaf) {
    std::cout << "Previous: "
              << (node->previous == 0 ? "NULL" : std::to_string(node->previous))
              << std::endl;
    std::cout << "Next: "
              << (node->next == 0 ? "NULL" : std::to_string(node->next))
              << std::endl;
  }

  std::cout << "Record Size: " << tree.record_size << " bytes" << std::endl;

  // Print keys (assuming they're uint32_t for display purposes)
  std::cout << "Keys: [";
  for (uint32_t i = 0; i < node->num_keys; i++) {
    if (tree.node_key_size == TYPE_INT32) {
      std::cout << *reinterpret_cast<const uint32_t *>(
                       get_key_at(tree, node, i))
                << ",";
    } else if (tree.node_key_size == TYPE_INT64) {
      std::cout << *reinterpret_cast<const uint64_t *>(
                       get_key_at(tree, node, i))
                << ",";
    } else {
      print_uint8_as_chars(get_key_at(tree, node, i), tree.node_key_size);
    }
  }

  std::cout << "]" << std::endl;

  // Print children for internal nodes
  if (!node->is_leaf) {
    uint32_t *children = get_children(tree, node);
    std::cout << "Children: [";
    for (uint32_t i = 0; i <= node->num_keys; i++) {
      if (i > 0)
        std::cout << ", ";
      std::cout << children[i];
    }
    std::cout << "]" << std::endl;
  }

  // Show memory layout information
  std::cout << "Memory Layout:" << std::endl;
  if (node->is_leaf) {
    uint32_t keys_size = tree.leaf_max_keys * tree.node_key_size;
    uint32_t records_size = tree.leaf_max_keys * tree.record_size;
    std::cout << "  Keys area: " << keys_size
              << " bytes (used: " << (node->num_keys * tree.node_key_size)
              << ")" << std::endl;
    std::cout << "  Records area: " << records_size
              << " bytes (used: " << (node->num_keys * tree.record_size) << ")"
              << std::endl;
    std::cout << "  Total data: " << (keys_size + records_size) << " / "
              << (PAGE_SIZE - NODE_HEADER_SIZE) << " bytes" << std::endl;
  } else {
    uint32_t keys_size = tree.internal_max_keys * tree.node_key_size;
    uint32_t children_size = (tree.internal_max_keys + 1) * tree.node_key_size;
    std::cout << "  Keys area: " << keys_size
              << " bytes (used: " << (node->num_keys * tree.node_key_size)
              << ")" << std::endl;
    std::cout << "  Children area: " << children_size
              << " bytes (used: " << ((node->num_keys + 1) * tree.node_key_size)
              << ")" << std::endl;
    std::cout << "  Total data: " << (keys_size + children_size) << " / "
              << (PAGE_SIZE - NODE_HEADER_SIZE) << " bytes" << std::endl;
  }

  // For B-tree nodes, show that internal nodes also have records
  if (tree.tree_type == BTREE && !node->is_leaf) {
    std::cout
        << "Note: B-TREE internal nodes also store records (not shown above)"
        << std::endl;
  }

  std::cout << "=====================" << std::endl;
}

#include <queue>

void print_tree(BPlusTree &tree) {
  BPTreeNode *root = bp_get_root(tree);
  if (!root) {
    std::cout << "Tree is empty" << std::endl;
    return;
  }

  std::queue<BPTreeNode *> to_visit;
  to_visit.push(root);

  while (!to_visit.empty()) {
    // Get the number of nodes at the current level
    size_t level_size = to_visit.size();

    // Print all nodes at the current level
    for (size_t i = 0; i < level_size; i++) {
      BPTreeNode *node = to_visit.front();
      to_visit.pop();

      // Print the current node using the existing bp_print_node function
      bp_print_node(tree, node);

      // Add children to the queue if the node is not a leaf
      if (!node->is_leaf) {
        uint32_t *children = get_children(tree, node);
        for (uint32_t j = 0; j <= node->num_keys; j++) {
          if (children[j] != 0) {
            BPTreeNode *child = bp_get_child(tree, node, j);
            if (child) {
              to_visit.push(child);
            }
          }
        }
      }
    }
    // Print a separator between levels
    std::cout << "\n=== END OF LEVEL ===\n" << std::endl;
  }
}

/*------------- HASH -------------- */

uint64_t debug_hash_tree(BPlusTree &tree) {
  uint64_t hash = 0xcbf29ce484222325ULL;
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

  // Recursive node hashing function
  std::function<void(BPTreeNode *, int)> hash_node = [&](BPTreeNode *node,
                                                         int depth) {
    if (!node)
      return;

    // Hash node metadata
    hash ^= node->index;
    hash *= prime;
    hash ^= node->parent;
    hash *= prime;
    hash ^= node->next;
    hash *= prime;
    hash ^= node->previous;
    hash *= prime;
    hash ^= node->num_keys;
    hash *= prime;
    hash ^= (node->is_leaf ? 1 : 0) | (depth << 1);
    hash *= prime;

    // Hash keys
    for (uint32_t i = 0; i < node->num_keys; i++) {
      uint8_t *key = get_key_at(tree, node, i);
      for (uint32_t j = 0; j < tree.node_key_size; j++) {
        hash ^= key[j];
        hash *= prime;
      }
    }

    if (node->is_leaf) {
      // Hash leaf node records
      uint8_t *record_data = get_record_data(tree, node);
      for (uint32_t i = 0; i < node->num_keys; i++) {
        const uint8_t *record = record_data + i * tree.record_size;
        uint32_t bytes_to_hash = std::min(8U, tree.record_size);
        for (uint32_t j = 0; j < bytes_to_hash; j++) {
          hash ^= record[j];
          hash *= prime;
        }
      }
    } else {
      // Recursively hash children
      uint32_t *children = get_children(tree, node);
      for (uint32_t i = 0; i <= node->num_keys; i++) {
        if (children[i] != 0) {
          BPTreeNode *child = bp_get_child(tree, node, i);
          if (child) {
            hash_node(child, depth + 1);
          }
        }
      }
    }
  };

  // Hash the tree starting from root
  if (tree.root_page_index != 0) {
    BPTreeNode *root = bp_get_root(tree);
    if (root) {
      hash_node(root, 0);
    }
  }

  return hash;
}

/* DBGINV */

#include <cassert>
#include <random>
#include <set>

static void validate_key_separation(BPlusTree &tree, BPTreeNode *node) {
  if (!node || node->is_leaf)
    return;

  for (uint32_t i = 0; i <= node->num_keys; i++) {
    BPTreeNode *child = bp_get_child(tree, node, i);
    if (!child)
      continue;

    // For child[i], check constraints based on separators
    // child[0] has keys < keys[0]
    // child[1] has keys >= keys[0] and < keys[1]
    // child[2] has keys >= keys[1] and < keys[2]
    // etc.

    // Check upper bound: child[i] should have keys < keys[i] (except for
    // rightmost child)
    if (i < node->num_keys) {
      uint8_t *upper_separator = get_key_at(tree, node, i);
      for (uint32_t j = 0; j < child->num_keys; j++) {
        if (tree.tree_type == BTREE) {
          if (cmp(tree, get_key_at(tree, child, j), upper_separator) > 0) {
            std::cerr << "INVARIANT VIOLATION: Key in child " << child->index
                      << " violates upper bound from parent " << node->index
                      << std::endl;
            exit(1);
          }
        } else {
          if (cmp(tree, get_key_at(tree, child, j), upper_separator) >= 0) {
            std::cerr << "INVARIANT VIOLATION: Key in child " << child->index
                      << " violates upper bound from parent " << node->index
                      << std::endl;
            exit(1);
          }
        }
      }
    }

    // Check lower bound: child[i] should have keys >= keys[i-1] (except for
    // leftmost child)
    if (i > 0) {
      uint8_t *lower_separator = get_key_at(tree, node, i - 1);
      for (uint32_t j = 0; j < child->num_keys; j++) {
        if (cmp(tree, get_key_at(tree, child, j), lower_separator) < 0) {
          std::cerr << "INVARIANT VIOLATION: Key in child " << child->index
                    << " violates lower bound from parent " << node->index
                    << std::endl;
          exit(1);
        }
      }
    }

    // Recursively check children
    validate_key_separation(tree, child);
  }
}

static void validate_leaf_links(BPlusTree &tree) {
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

static void validate_tree_height(BPlusTree &tree, BPTreeNode *node,
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
        validate_tree_height(tree, child, expected_height, current_height + 1);
      }
    }
  }
}

static void validate_bplus_leaf_node(BPlusTree &tree, BPTreeNode *node) {
  return;
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
  uint32_t min_keys = (node->parent == 0)
                          ? 0
                          : tree.leaf_min_keys; // Root can have as few as 1 key
  if (node->num_keys < min_keys) {
    bp_print_node(tree, node);
    bp_print_node(tree, bp_get_parent(node));
    std::cout << node << "// Leaf node has too few keys: " << node->num_keys
              << " < " << min_keys << std::endl;
    exit(0);
  }

  if (node->num_keys > tree.leaf_max_keys) {
    std::cout << "// Leaf node has too many keys: " << node->num_keys << " > "
              << tree.leaf_max_keys << std::endl;
    exit(0);
  }

  // Keys must be sorted in ascending order
  for (uint32_t i = 1; i < node->num_keys; i++) {
    if (cmp(tree, get_key_at(tree, node, i), get_key_at(tree, node, i - 1)) <=
        0) {
      std::cout << "// Leaf keys not in ascending order at positions " << i - 1
                << " and " << i << std::endl;
      exit(0);
    }
  }

  // If this is not the root, validate parent relationship
  if (node->parent != 0) {
    BPTreeNode *parent = static_cast<BPTreeNode *>(pager_get(node->parent));
    if (!parent) {
      std::cout << "// Cannot access parent node at page " << node->parent
                << std::endl;
      exit(0);
    }

    if (parent->is_leaf) {
      std::cout << "// Parent node is marked as leaf but has children"
                << std::endl;
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
      // std::cout << "// Next sibling's previous pointer does not point back to
      // "
      //              "this node"
      //           << std::endl;
      exit(0);
    }
  }

  if (node->previous != 0) {
    BPTreeNode *prev_node =
        static_cast<BPTreeNode *>(pager_get(node->previous));
    if (prev_node && prev_node->next != node->index) {
      // std::cout << "/////Previous sibling's next pointer does not point to
      // this node"
      //     << std::endl;
      exit(0);
    }
  }
}

static void validate_bplus_internal_node(BPlusTree &tree, BPTreeNode *node) {
  if (!node) {
    std::cout << "// Node pointer is null" << std::endl;
    exit(0);
  }

  // Must not be marked as leaf
  if (node->is_leaf) {
    std::cout << "// Node is marked as leaf but should be internal"
              << std::endl;
    exit(0);
  }

  // Key count must be within bounds
  uint32_t min_keys =
      (node->parent == 0)
          ? 1
          : tree.internal_min_keys; // Root can have as few as 1 key
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
  for (uint32_t i = 1; i < node->num_keys; i++) {
    if (cmp(tree, get_key_at(tree, node, i), get_key_at(tree, node, i - 1)) <=
        0) {
      std::cout << "// Internal keys not in ascending order at positions "
                << i - 1 << " and " << i << std::endl;
      exit(0);
    }
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
      std::cout << "// Cannot access child node at page " << children[i]
                << std::endl;
      exit(0);
    }

    if (child->parent != node->index) {

      std::cout << "// Child node's parent pointer does not point to this node"
                << std::endl;
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
              << ", prev=" << node->previous << "), but only leaves should"
              << std::endl;
    exit(0);
  }

  // If this is not the root, validate parent relationship
  if (node->parent != 0) {
    BPTreeNode *parent = static_cast<BPTreeNode *>(pager_get(node->parent));
    if (!parent) {
      std::cout << "// Cannot access parent node at page " << node->parent
                << std::endl;
      exit(0);
    }

    if (parent->is_leaf) {
      std::cout << "// Parent node is marked as leaf but has children"
                << std::endl;
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

static void validate_btree_node(BPlusTree &tree, BPTreeNode *node) {
  if (!node) {
    std::cout << "// Node pointer is null" << std::endl;
    exit(0);
  }

  // For regular B-trees, both internal and leaf nodes store records
  // Key count must be within bounds (same for both leaf and internal in
  // regular B-tree)
  uint32_t min_keys =
      (node->parent == 0) ? 0 : tree.leaf_min_keys; // Using leaf limits since
                                                    // they're the same in BTREE
  uint32_t max_keys = tree.leaf_max_keys; // Same for both in regular B-tree

  if (node->num_keys < min_keys) {
    std::cout << "// B-tree node has too few keys: " << node->num_keys << " < "
              << min_keys << std::endl;
    exit(0);
  }

  if (node->num_keys > max_keys) {
    std::cout << "// B-tree node has too many keys: " << node->num_keys << " > "
              << max_keys << std::endl;
    exit(0);
  }

  // Keys must be sorted in ascending order
  for (uint32_t i = 1; i < node->num_keys; i++) {
    if (cmp(tree, get_key_at(tree, node, i), get_key_at(tree, node, i - 1)) <
        0) {
      std::cout << "// B-tree keys not in ascending order at positions "
                << i - 1 << " and " << i << std::endl;
      exit(0);
    }
  }

  // If internal node, validate children
  if (!node->is_leaf) {
    uint32_t *children = get_children(tree, node);
    for (uint32_t i = 0; i <= node->num_keys; i++) {
      if (children[i] == 0) {
        std::cout << "// B-tree internal node missing child at index " << i
                  << std::endl;
        exit(0);
      }

      // Verify child exists and points back to this node as parent
      BPTreeNode *child = static_cast<BPTreeNode *>(pager_get(children[i]));
      if (!child) {
        std::cout << "// Cannot access child node at page " << children[i]
                  << std::endl;
        exit(0);
      }

      if (child->parent != node->index) {
        std::cout
            << "// Child node's parent pointer does not point to this node"
            << std::endl;
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
      std::cout
          << "// B-tree internal node has sibling pointers, but should not"
          << std::endl;
      exit(0);
    }
  }

  // If this is not the root, validate parent relationship
  if (node->parent != 0) {
    BPTreeNode *parent = static_cast<BPTreeNode *>(pager_get(node->parent));
    if (!parent) {
      std::cout << "// Cannot access parent node at page " << node->parent
                << std::endl;
      exit(0);
    }

    if (parent->is_leaf) {
      std::cout << "// Parent node is marked as leaf but has children"
                << std::endl;
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

void bp_validate_all_invariants(BPlusTree &tree) {
  BPTreeNode *root = bp_get_root(tree);
  if (!root)
    return;

  int expected_height = 0;
  while (root && !root->is_leaf) {
    root = bp_get_child(tree, root, 0);
    expected_height++;
  }

  // Verify all nodes
  std::queue<BPTreeNode *> to_visit;
  root = bp_get_root(tree);
  to_visit.push(root);

  while (!to_visit.empty()) {
    BPTreeNode *node = to_visit.front();
    to_visit.pop();

    if (tree.tree_type == BTREE) {
      validate_btree_node(tree, node);
    } else {
      if (node->is_leaf) {
        validate_bplus_leaf_node(tree, node);
      } else {
        validate_bplus_internal_node(tree, node);
      }
    }

    if (!node->is_leaf) {
      for (uint32_t i = 0; i <= node->num_keys; i++) {
        BPTreeNode *child = bp_get_child(tree, node, i);
        if (child) {
          to_visit.push(child);
        }
      }
    }
  }

  root = bp_get_root(tree);
  // Verify key separation
  validate_key_separation(tree, root);

  // Verify leaf links
  validate_leaf_links(tree);

  // Verify uniform height
  validate_tree_height(tree, root, expected_height);
}

/*---------------- CURSOR --------------------- */

// Add these to btree.hpp after BPlusTree struct

BtCursor *bt_cursor_create(BPlusTree *tree, bool write_cursor) {
  if (!tree)
    return nullptr;

  BtCursor *cursor = new BtCursor();
  cursor->tree = tree;
  cursor->root_page = tree->root_page_index;
  cursor->stack_depth = 0;
  cursor->current_page = 0;
  cursor->current_index = 0;
  cursor->state = CURSOR_INVALID;
  cursor->is_write_cursor = write_cursor;

  return cursor;
}

void bt_cursor_destroy(BtCursor *cursor) {
  if (cursor) {
    delete cursor;
  }
}

void bt_cursor_clear(BtCursor *cursor) {
  if (!cursor)
    return;
  cursor->state = CURSOR_INVALID;
  cursor->stack_depth = 0;
  cursor->current_page = 0;
  cursor->current_index = 0;
}

bool bt_cursor_first(BtCursor *cursor) {
  if (!cursor || !cursor->tree) {
    if (cursor)
      cursor->state = CURSOR_FAULT;
    return false;
  }

  bt_cursor_clear(cursor);

  // Navigate to leftmost leaf using your existing functions
  BPTreeNode *current = bp_get_root(*cursor->tree);
  if (!current || current->num_keys == 0) {
    cursor->state = CURSOR_INVALID;
    return false;
  }

  // Build stack going down tree
  while (!current->is_leaf) {
    cursor->page_stack[cursor->stack_depth] = current->index;
    cursor->index_stack[cursor->stack_depth] = 0; // Always leftmost child
    cursor->stack_depth++;

    current = bp_get_child(*cursor->tree, current, 0);
    if (!current) {
      cursor->state = CURSOR_FAULT;
      return false;
    }
  }

  cursor->current_page = current->index;
  cursor->current_index = 0;
  cursor->state = (current->num_keys > 0) ? CURSOR_VALID : CURSOR_INVALID;
  return cursor->state == CURSOR_VALID;
}

bool bt_cursor_last(BtCursor *cursor) {
  if (!cursor || !cursor->tree) {
    if (cursor)
      cursor->state = CURSOR_FAULT;
    return false;
  }

  bt_cursor_clear(cursor);

  BPTreeNode *current = bp_get_root(*cursor->tree);
  if (!current || current->num_keys == 0) {
    cursor->state = CURSOR_INVALID;
    return false;
  }

  // Navigate to rightmost leaf
  while (!current->is_leaf) {
    cursor->page_stack[cursor->stack_depth] = current->index;
    cursor->index_stack[cursor->stack_depth] =
        current->num_keys; // Rightmost child
    cursor->stack_depth++;

    current = bp_get_child(*cursor->tree, current, current->num_keys);
    if (!current) {
      cursor->state = CURSOR_FAULT;
      return false;
    }
  }

  cursor->current_page = current->index;
  if (current->num_keys > 0) {
    cursor->current_index = current->num_keys - 1;
    cursor->state = CURSOR_VALID;
    return true;
  }

  cursor->state = CURSOR_INVALID;
  return false;
}

bool bt_cursor_next(BtCursor *cursor) {
  if (!cursor || cursor->state != CURSOR_VALID) {
    return false;
  }

  BPTreeNode *current_node =
      static_cast<BPTreeNode *>(pager_get(cursor->current_page));
  if (!current_node) {
    cursor->state = CURSOR_FAULT;
    return false;
  }

  // Case 1: More keys in current node (leaf or internal)
  if (cursor->current_index + 1 < current_node->num_keys) {
    cursor->current_index++;
    return true;
  }

  // Case 2: B+ tree leaf - use sibling links
  if (cursor->tree->tree_type == BPLUS && current_node->is_leaf) {

    if (0 == current_node->next) {
      cursor->state = CURSOR_INVALID;
      return false;
    }

    cursor->current_page = current_node->next;
    cursor->current_index = 0;

    return true;
  }

  // Case 3: B-tree or B+ internal - navigate up stack
  if (cursor->tree->tree_type == BTREE && !current_node->is_leaf) {
    // For B-tree internal nodes, move to next child after visiting this key
    BPTreeNode *next_child =
        bp_get_child(*cursor->tree, current_node, cursor->current_index + 1);
    if (next_child) {
      cursor->page_stack[cursor->stack_depth] = cursor->current_page;
      cursor->index_stack[cursor->stack_depth] = cursor->current_index + 1;
      cursor->stack_depth++;
      return bt_cursor_move_to_leftmost_in_subtree(cursor, next_child);
    }
  }

  // Navigate up the stack
  while (cursor->stack_depth > 0) {
    cursor->stack_depth--;
    uint32_t parent_page = cursor->page_stack[cursor->stack_depth];
    uint32_t parent_index = cursor->index_stack[cursor->stack_depth];

    BPTreeNode *parent = static_cast<BPTreeNode *>(pager_get(parent_page));
    if (!parent) {
      cursor->state = CURSOR_FAULT;
      return false;
    }

    // For B-tree, visit internal node key if coming from left subtree
    if (cursor->tree->tree_type == BTREE && !parent->is_leaf &&
        parent_index < parent->num_keys) {
      cursor->current_page = parent_page;
      cursor->current_index = parent_index;
      cursor->state = CURSOR_VALID;
      return true;
    }

    // Try next child in parent
    if (parent_index + 1 <= parent->num_keys) {
      cursor->index_stack[cursor->stack_depth] = parent_index + 1;
      cursor->stack_depth++;

      BPTreeNode *next_child =
          bp_get_child(*cursor->tree, parent, parent_index + 1);
      if (next_child) {
        return bt_cursor_move_to_leftmost_in_subtree(cursor, next_child);
      }
    }
  }

  cursor->state = CURSOR_INVALID;
  return false;
}

bool bt_cursor_previous(BtCursor *cursor) {
  if (!cursor || cursor->state != CURSOR_VALID) {
    return false;
  }

  // Case 1: More keys in current node
  if (cursor->current_index > 0) {
    cursor->current_index--;
    return true;
  }

  BPTreeNode *current_node =
      static_cast<BPTreeNode *>(pager_get(cursor->current_page));
  if (!current_node) {
    cursor->state = CURSOR_FAULT;
    return false;
  }

  // Case 2: B+ tree leaf - use sibling functions
  if (cursor->tree->tree_type == BPLUS && current_node->is_leaf &&
      current_node->previous != 0) {
    cursor->current_page = current_node->previous;

    BPTreeNode *prev_node = bp_get_prev(current_node);
    if (!prev_node || prev_node->num_keys == 0) {
      cursor->state = CURSOR_INVALID;
      return false;
    }

    cursor->current_index = prev_node->num_keys - 1;
    return true;
  }

  // Case 3: B-tree or need to navigate up
  if (cursor->tree->tree_type == BTREE && !current_node->is_leaf &&
      cursor->current_index > 0) {
    // For B-tree internal nodes, move to rightmost of left subtree
    BPTreeNode *left_child =
        bp_get_child(*cursor->tree, current_node, cursor->current_index - 1);
    if (left_child) {
      cursor->page_stack[cursor->stack_depth] = cursor->current_page;
      cursor->index_stack[cursor->stack_depth] = cursor->current_index - 1;
      cursor->stack_depth++;
      return bt_cursor_move_to_rightmost_in_subtree(cursor, left_child);
    }
  }

  // Navigate up the stack
  while (cursor->stack_depth > 0) {
    cursor->stack_depth--;
    uint32_t parent_page = cursor->page_stack[cursor->stack_depth];
    uint32_t parent_index = cursor->index_stack[cursor->stack_depth];

    BPTreeNode *parent = static_cast<BPTreeNode *>(pager_get(parent_page));
    if (!parent) {
      cursor->state = CURSOR_FAULT;
      return false;
    }

    // For B-tree, visit internal node key if coming from right subtree
    if (cursor->tree->tree_type == BTREE && !parent->is_leaf &&
        parent_index > 0) {
      cursor->current_page = parent_page;
      cursor->current_index = parent_index - 1;
      cursor->state = CURSOR_VALID;
      return true;
    }

    // Try previous child in parent
    if (parent_index > 0) {
      cursor->index_stack[cursor->stack_depth] = parent_index - 1;
      cursor->stack_depth++;

      BPTreeNode *prev_child =
          bp_get_child(*cursor->tree, parent, parent_index - 1);
      if (prev_child) {
        return bt_cursor_move_to_rightmost_in_subtree(cursor, prev_child);
      }
    }
  }

  cursor->state = CURSOR_INVALID;
  return false;
}

bool bt_cursor_seek(BtCursor *cursor, const void *key) {
  if (!cursor || !cursor->tree || !key) {
    if (cursor)
      cursor->state = CURSOR_FAULT;
    return false;
  }

  bt_cursor_clear(cursor);
  const uint8_t *search_key = static_cast<const uint8_t *>(key);

  // Use your existing bp_find_leaf_node function
  BPTreeNode *root = bp_get_root(*cursor->tree);
  if (!root) {
    cursor->state = CURSOR_FAULT;
    return false;
  }

  BPTreeNode *leaf = bp_find_containing_node(*cursor->tree, root, search_key);
  if (!leaf) {
    cursor->state = CURSOR_FAULT;
    return false;
  }

  // Use your existing bp_binary_search function
  cursor->current_page = leaf->index;
  uint32_t pos = bp_binary_search(*cursor->tree, leaf, search_key);

  if (pos < leaf->num_keys) {

    uint32_t xxx = (uint32_t)*get_key_at(*cursor->tree, leaf, pos);
    uint32_t sdsds = (uint32_t)*search_key;
    if (cmp(*cursor->tree, get_key_at(*cursor->tree, leaf, pos), search_key) ==
        0) {
      // Exact match
      cursor->current_index = pos;
      cursor->state = CURSOR_VALID;
      return true;
    }
  }

  // No exact match - position at nearest key
  if (pos > 0) {
    cursor->current_index = pos - 1;
    cursor->state = CURSOR_VALID;
    return false; // Not exact match
  }

  cursor->state = CURSOR_INVALID;
  return false;
}

const uint8_t *bt_cursor_get_key(BtCursor *cursor) {
  if (!cursor || cursor->state != CURSOR_VALID)
    return nullptr;

  BPTreeNode *node = static_cast<BPTreeNode *>(pager_get(cursor->current_page));
  if (!node || cursor->current_index >= node->num_keys)
    return nullptr;

  // Use your existing get_key_at function
  return get_key_at(*cursor->tree, node, cursor->current_index);
}

const uint8_t *bt_cursor_get_record(BtCursor *cursor) {
  if (!cursor || cursor->state != CURSOR_VALID)
    return nullptr;

  BPTreeNode *node = static_cast<BPTreeNode *>(pager_get(cursor->current_page));
  if (!node || cursor->current_index >= node->num_keys)
    return nullptr;

  // Use your existing get_record_at function
  return get_record_at(*cursor->tree, node, cursor->current_index);
}

bool bt_cursor_is_valid(BtCursor *cursor) {
  return cursor && cursor->state == CURSOR_VALID;
}

// HELPER FUNCTIONS YOU NEED TO IMPLEMENT:
bool bt_cursor_move_to_leftmost_in_subtree(BtCursor *cursor, BPTreeNode *root) {
  BPTreeNode *current = root;

  while (!current->is_leaf) {
    cursor->page_stack[cursor->stack_depth] = current->index;
    cursor->index_stack[cursor->stack_depth] = 0;
    cursor->stack_depth++;

    current = bp_get_child(*cursor->tree, current, 0);
    if (!current) {
      cursor->state = CURSOR_FAULT;
      return false;
    }
  }

  cursor->current_page = current->index;
  cursor->current_index = 0;
  cursor->state = (current->num_keys > 0) ? CURSOR_VALID : CURSOR_INVALID;
  return cursor->state == CURSOR_VALID;
}

bool bt_cursor_move_to_rightmost_in_subtree(BtCursor *cursor,
                                            BPTreeNode *root) {
  BPTreeNode *current = root;

  while (!current->is_leaf) {
    cursor->page_stack[cursor->stack_depth] = current->index;
    cursor->index_stack[cursor->stack_depth] = current->num_keys;
    cursor->stack_depth++;

    current = bp_get_child(*cursor->tree, current, current->num_keys);
    if (!current) {
      cursor->state = CURSOR_FAULT;
      return false;
    }
  }

  cursor->current_page = current->index;
  if (current->num_keys > 0) {
    cursor->current_index = current->num_keys - 1;
    cursor->state = CURSOR_VALID;
    return true;
  }

  cursor->state = CURSOR_INVALID;
  return false;
}

// ADDITIONAL HELPER FUNCTION FOR STACK REBUILDING:
void bt_cursor_rebuild_stack_to_current(BtCursor *cursor) {
  // This function rebuilds the stack from root to current position
  // Useful when cursor becomes invalid due to tree modifications

  if (!cursor || !cursor->tree)
    return;

  BPTreeNode *current_node =
      static_cast<BPTreeNode *>(pager_get(cursor->current_page));
  if (!current_node) {
    cursor->state = CURSOR_FAULT;
    return;
  }

  uint8_t *current_key =
      get_key_at(*cursor->tree, current_node, cursor->current_index);
  if (!current_key) {
    cursor->state = CURSOR_FAULT;
    return;
  }

  // Clear stack and rebuild by navigating from root
  cursor->stack_depth = 0;
  BPTreeNode *node = bp_get_root(*cursor->tree);

  while (node && !node->is_leaf) {
    cursor->page_stack[cursor->stack_depth] = node->index;
    uint32_t child_index = bp_binary_search(*cursor->tree, node, current_key);
    cursor->index_stack[cursor->stack_depth] = child_index;
    cursor->stack_depth++;

    node = bp_get_child(*cursor->tree, node, child_index);
  }

  cursor->state = CURSOR_REQUIRESEEK;
}

bool bt_cursor_update(BtCursor *cursor, const uint8_t *record) {
  if (!cursor || !cursor->tree || !record || cursor->state != CURSOR_VALID) {
    return false;
  }

  BPTreeNode *node = static_cast<BPTreeNode *>(pager_get(cursor->current_page));
  if (!node || cursor->current_index >= node->num_keys) {
    return false;
  }

  // Mark page as dirty since we're modifying it
  pager_mark_dirty(cursor->current_page);

  // Get pointer to current record location
  uint8_t *current_record = const_cast<uint8_t *>(
      get_record_at(*cursor->tree, node, cursor->current_index));
  if (!current_record) {
    return false;
  }

  // Overwrite the record in place
  memcpy(current_record, record, cursor->tree->record_size);

  return true;
}

bool bt_cursor_delete(BtCursor *cursor) {
  if (!cursor || !cursor->tree || cursor->state != CURSOR_VALID) {
    return false;
  }

  // Get the key to delete
  const uint8_t *key_to_delete = bt_cursor_get_key(cursor);
  if (!key_to_delete) {
    return false;
  }
  BPTreeNode *node = static_cast<BPTreeNode *>(pager_get(cursor->current_page));
  uint32_t keys_before = node->num_keys;
  bp_delete_element(*cursor->tree, const_cast<uint8_t *>(key_to_delete));

  node = static_cast<BPTreeNode *>(pager_get(cursor->current_page));

  if (!node) {
    cursor->state = CURSOR_INVALID;
    return true; // Delete succeeded, but cursor is now invalid
  }

  // Check if we're still within bounds
  if (cursor->current_index >= node->num_keys) {
    // We were at the last element, now cursor is past end
    cursor->state = CURSOR_INVALID;
  } else {
    // Cursor is still valid, now points to next record
    cursor->state = CURSOR_VALID;
  }

  // if true, don't nessessarly call next
  return node->num_keys < keys_before;
}
