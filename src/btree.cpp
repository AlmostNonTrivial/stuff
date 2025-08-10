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

#define PRINT(x,y) std::cout << x << y << "\n"

static int cmp(BPlusTree& tree, const uint8_t* key1, const uint8_t* key2) {
    switch (tree.node_key_size) {
        case TYPE_INT32: {
            uint32_t val1 = *reinterpret_cast<const uint32_t*>(key1);
            uint32_t val2 = *reinterpret_cast<const uint32_t*>(key2);
            if (val1 < val2) return -1;
            if (val1 > val2) return 1;
            return 0;
        }
        case TYPE_INT64: {
            uint64_t val1 = *reinterpret_cast<const uint64_t*>(key1);
            uint64_t val2 = *reinterpret_cast<const uint64_t*>(key2);
            if (val1 < val2) return -1;
            if (val1 > val2) return 1;
            return 0;
        }
        case TYPE_VARCHAR32:
        case TYPE_VARCHAR256: {
            return memcmp(key1, key2, tree.node_key_size);
        }
    }
}



// Node management
BPTreeNode* bp_create_node(BPlusTree& tree, bool is_leaf);
void bp_destroy_node(BPTreeNode* node);

void bp_set_next(BPTreeNode *node, uint32_t index);
void bp_set_prev(BPTreeNode *node, uint32_t index);
void bp_insert(BPlusTree& tree, BPTreeNode* node, uint32_t key, const uint8_t* data);
void bp_insert_repair(BPlusTree& tree, BPTreeNode* node);
BPTreeNode* bp_split(BPlusTree& tree, BPTreeNode* node);
void bp_do_delete(BPlusTree& tree, BPTreeNode* node, uint32_t key);
void bp_repair_after_delete(BPlusTree& tree, BPTreeNode* node);
BPTreeNode* bp_merge_right(BPlusTree& tree, BPTreeNode* node);
BPTreeNode* bp_steal_from_left(BPlusTree& tree, BPTreeNode* node, uint32_t parent_index);
BPTreeNode* bp_steal_from_right(BPlusTree& tree, BPTreeNode* node, uint32_t parent_index);
void bp_update_parent_keys(BPlusTree& tree, BPTreeNode* node, uint32_t deleted_key);

void bp_print_node(BPlusTree &tree, BPTreeNode *node);
// Helper functions to access arrays within the node's data area
uint32_t *get_keys(BPTreeNode *node) {
  return reinterpret_cast<uint32_t *>(node->data);
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

uint8_t *get_record_at(BPlusTree &tree, BPTreeNode *node,
                              uint32_t index) {
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
static bool bp_find_in_tree(BPlusTree &tree, BPTreeNode *node, uint32_t key);

// Combined btree.cpp functions - calculate_capacity and create merged

/*

internal:
A tree with n keys will have n+1 children


 */

BPlusTree bp_create(DataType key, uint32_t record_size,
                    TreeType tree_type) {
  BPlusTree tree;
  tree.node_key_size = key;
  tree.tree_type = tree_type;

  // Calculate record size

  tree.record_size = record_size;

  constexpr uint32_t USABLE_SPACE = PAGE_SIZE - NODE_HEADER_SIZE;
  const uint32_t minimum_entry_count = 3U;

  // Sanity check
  if ((record_size * minimum_entry_count) > USABLE_SPACE) {
    tree.tree_type = INVALID;
    return tree;
  }

  if (tree_type == BTREE) {
    // B-tree internal nodes: [keys][records][children]
    // Need space for n keys + n records + (n+1) child pointers

    uint32_t key_record_size = tree.node_key_size + record_size;
    uint32_t child_ptr_size = tree.node_key_size;

    // Calculate max keys for internal nodes
    // n*(key + record) + (n+1)*ptr <= USABLE_SPACE
    // n*(key + record + ptr) + ptr <= USABLE_SPACE
    // n <= (USABLE_SPACE - ptr) / (key + record + ptr)
    uint32_t max_internal =
        (USABLE_SPACE - child_ptr_size) / (key_record_size + child_ptr_size);

    // Leaf nodes just have [keys][records]
    uint32_t max_leaf = USABLE_SPACE / key_record_size;

    // Use smaller for consistency
    uint32_t max_keys = std::min(max_internal, max_leaf);
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

    // B+tree: check for existing key to update
    // B-tree: allow duplicates
    if (tree.tree_type == BPLUS) {
      for (uint32_t i = 0; i < node->num_keys; i++) {
        if (keys[i] == key) {
          bp_mark_dirty(node);
          memcpy(record_data + i * tree.record_size, data, tree.record_size);
          return;
        }
      }
    }

    // Check if split needed
    if (node->num_keys >= tree.leaf_max_keys) {
      bp_insert_repair(tree, node);
      bp_insert(tree, bp_find_leaf_node(tree, bp_get_root(tree), key), key,
                data);
      return;
    }

    bp_mark_dirty(node);

    // Find insertion position
    uint32_t insert_index = 0;
    while (insert_index < node->num_keys && keys[insert_index] < key) {
      insert_index++;
    }

    // For B-tree duplicates: insert after existing equal keys
    if (tree.tree_type == BTREE) {
      while (insert_index < node->num_keys && keys[insert_index] == key) {
        insert_index++;
      }
    }

    // Shift elements right from insert_index
    for (uint32_t i = node->num_keys; i > insert_index; i--) {
      keys[i] = keys[i - 1];
      memcpy(record_data + i * tree.record_size,
             record_data + (i - 1) * tree.record_size, tree.record_size);
    }

    // Insert new key and record
    keys[insert_index] = key;
    memcpy(record_data + insert_index * tree.record_size, data,
           tree.record_size);
    node->num_keys++;

  } else {
    // Internal node
    uint32_t *keys = get_keys(node);
    uint32_t find_index = 0;

    // Find child to descend into
    while (find_index < node->num_keys && keys[find_index] < key) {
      find_index++;
    }

    // For B-tree: if key equals internal key, go to right subtree
    // This ensures duplicates end up in right subtree
    if (tree.tree_type == BTREE && find_index < node->num_keys &&
        keys[find_index] == key) {
      find_index++;
    }

    BPTreeNode *child_node = bp_get_child(tree, node, find_index);
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
  uint32_t *node_keys = get_keys(node);
  uint32_t rising_key = node_keys[split_index];
  uint32_t parent_index = 0;

  // Parent handling (same as before)
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

    // Shift parent's keys and children to make room
    // First shift children (need to shift n+1 children for n keys)
    for (uint32_t i = current_parent->num_keys; i > parent_index; i--) {
      parent_children[i + 1] = parent_children[i];
    }
    // Then shift keys
    for (uint32_t i = current_parent->num_keys; i > parent_index; i--) {
      parent_keys[i] = parent_keys[i - 1];
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
    parent_keys[parent_index] = rising_key;

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

  uint32_t *right_keys = get_keys(right_node);

  // Handle the split differently for leaf vs internal, and B-tree vs B+tree
  if (node->is_leaf) {
    // Leaf nodes: split at split_index
    right_node->num_keys = node->num_keys - split_index;

    // Copy keys to right node
    for (uint32_t i = split_index; i < node->num_keys; i++) {
      right_keys[i - split_index] = node_keys[i];
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
        right_keys[i - split_index - 1] = node_keys[i];
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
        right_keys[i - split_index - 1] = node_keys[i];
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
    uint32_t *new_root_keys = get_keys(new_root);

    new_root_keys[0] = rising_key;
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

  for (i = 0; i < node->num_keys && keys[i] < key; i++);

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

// The issue is that we need to delete the predecessor from the leaf
// without triggering repairs that might free the current node
static void bp_delete_internal_btree(BPlusTree &tree, BPTreeNode *node,
                                     uint32_t index) {
  uint32_t *keys = get_keys(node);

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
  uint32_t pred_key = get_keys(curr)[pred_index];
  uint8_t *pred_record = get_record_at(tree, curr, pred_index);

  // Replace the key and record in the internal node
  bp_mark_dirty(node);
  keys[index] = pred_key;

  uint8_t *internal_records = get_internal_record_data(tree, node);
  memcpy(internal_records + index * tree.record_size, pred_record,
         tree.record_size);

  // Now delete the predecessor from the leaf node directly
  // without going through bp_do_delete which might restructure the tree
  bp_mark_dirty(curr);
  uint32_t *leaf_keys = get_keys(curr);
  uint8_t *leaf_records = get_record_data(tree, curr);

  // Shift keys and records in the leaf to remove the predecessor
  for (uint32_t j = pred_index; j < curr->num_keys - 1; j++) {
    leaf_keys[j] = leaf_keys[j + 1];
    memcpy(leaf_records + j * tree.record_size,
           leaf_records + (j + 1) * tree.record_size, tree.record_size);
  }
  curr->num_keys--;

  // Now repair the leaf if needed
  // This is safe because we're done modifying the internal node
  bp_repair_after_delete(tree, curr);
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
// Simplified B-tree deletion that reuses existing logic

void bp_do_delete(BPlusTree &tree, BPTreeNode *node, uint32_t key) {
  if (!node)
    return;

  uint32_t *keys = get_keys(node);
  uint32_t i;

  // Find position of key
  for (i = 0; i < node->num_keys && keys[i] < key; i++)
    ;

  if (tree.tree_type == BTREE) {
    // B-tree specific handling
    if (i < node->num_keys && keys[i] == key) {
      // Found key in this node
      if (node->is_leaf) {
        // Delete from leaf - same as B+tree leaf deletion
        bp_mark_dirty(node);
        uint8_t *record_data = get_record_data(tree, node);

        // Shift left
        for (uint32_t j = i; j < node->num_keys - 1; j++) {
          keys[j] = keys[j + 1];
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
      if (i < node->num_keys && keys[i] > key) {
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

  uint32_t *node_keys = get_keys(node);
  uint32_t *parent_keys = get_keys(parent_node);
  uint32_t *sibling_keys = get_keys(left_sibling);

  bp_mark_dirty(node);
  bp_mark_dirty(parent_node);
  bp_mark_dirty(left_sibling);

  // Shift node's keys and records right
  for (uint32_t i = node->num_keys; i > 0; i--) {
    node_keys[i] = node_keys[i - 1];
  }

  if (node->is_leaf) {
    // Leaf: move sibling's last key/record to node's first position
    node_keys[0] = sibling_keys[left_sibling->num_keys - 1];

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
    parent_keys[parent_index - 1] = node_keys[0];
  } else {
    // Internal node: rotate through parent

    // Move parent key down to node
    node_keys[0] = parent_keys[parent_index - 1];

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
      parent_keys[parent_index - 1] = sibling_keys[left_sibling->num_keys - 1];
      memcpy(parent_records + (parent_index - 1) * tree.record_size,
             sibling_records + (left_sibling->num_keys - 1) * tree.record_size,
             tree.record_size);
    } else {
      // B+tree: just move key up from sibling
      parent_keys[parent_index - 1] = sibling_keys[left_sibling->num_keys - 1];
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

  uint32_t *node_keys = get_keys(node);
  uint32_t *parent_keys = get_keys(parent_node);
  uint32_t *sibling_keys = get_keys(right_sibling);

  bp_mark_dirty(node);
  bp_mark_dirty(parent_node);
  bp_mark_dirty(right_sibling);

  if (node->is_leaf) {
    // Leaf: move sibling's first key/record to node's last position
    node_keys[node->num_keys] = sibling_keys[0];

    uint8_t *node_records = get_record_data(tree, node);
    uint8_t *sibling_records = get_record_data(tree, right_sibling);

    // Copy sibling's first record to node's last
    memcpy(node_records + node->num_keys * tree.record_size, sibling_records,
           tree.record_size);

    // Shift sibling's keys and records left
    for (uint32_t i = 0; i < right_sibling->num_keys - 1; i++) {
      sibling_keys[i] = sibling_keys[i + 1];
      memcpy(sibling_records + i * tree.record_size,
             sibling_records + (i + 1) * tree.record_size, tree.record_size);
    }

    // Update parent key
    parent_keys[parent_index] = sibling_keys[0];
  } else {
    // Internal node: rotate through parent

    // Move parent key down to node
    node_keys[node->num_keys] = parent_keys[parent_index];

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
      parent_keys[parent_index] = sibling_keys[0];
      memcpy(parent_records + parent_index * tree.record_size, sibling_records,
             tree.record_size);

      // Shift sibling's records left
      for (uint32_t i = 0; i < right_sibling->num_keys - 1; i++) {
        memcpy(sibling_records + i * tree.record_size,
               sibling_records + (i + 1) * tree.record_size, tree.record_size);
      }
    } else {
      // B+tree: just move key up from sibling
      parent_keys[parent_index] = sibling_keys[0];
    }

    // Shift sibling's keys left
    for (uint32_t i = 0; i < right_sibling->num_keys - 1; i++) {
      sibling_keys[i] = sibling_keys[i + 1];
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

  uint32_t *parent_keys = get_keys(parent);
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

  uint32_t *node_keys = get_keys(node);
  uint32_t *sibling_keys = get_keys(right_sibling);

  bp_mark_dirty(node);
  bp_mark_dirty(parent);

  if (node->is_leaf) {
    // Merge leaf nodes
    uint8_t *node_records = get_record_data(tree, node);
    uint8_t *sibling_records = get_record_data(tree, right_sibling);

    // Copy all keys and records from sibling to node
    for (uint32_t i = 0; i < right_sibling->num_keys; i++) {
      node_keys[node->num_keys + i] = sibling_keys[i];
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
    node_keys[node->num_keys] = parent_keys[node_index];

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
      node_keys[node->num_keys + 1 + i] = sibling_keys[i];
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
    parent_keys[i] = parent_keys[i + 1];
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

  // Print keys
  uint32_t *keys = get_keys(node);
  std::cout << "Keys: [";
  for (uint32_t i = 0; i < node->num_keys; i++) {
    if (i > 0)
      std::cout << ", ";
    std::cout << keys[i];
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
  } else {
    // Print record data for leaf nodes
    // if (tree.tree_type == BPLUS || tree.tree_type == BTREE) {
    //   std::cout << "Records:" << std::endl;
    //   for (uint32_t i = 0; i < node->num_keys; i++) {
    //     const uint8_t *record = get_record_at(tree, node, i);
    //     if (record) {
    //       std::cout << "  [" << i << "] Key=" << keys[i] << " Data=";

    //       // Print first few bytes of record in hex
    //       uint32_t bytes_to_show = std::min(16U, tree.record_size);
    //       for (uint32_t j = 0; j < bytes_to_show; j++) {
    //         std::cout << std::hex << std::setw(2) << std::setfill('0')
    //                   << static_cast<int>(record[j]);
    //         if (j < bytes_to_show - 1)
    //           std::cout << " ";
    //       }
    //       std::cout << std::dec;

    //       if (tree.record_size > 16) {
    //         std::cout << "... (" << tree.record_size << " bytes total)";
    //       }

    //       // For simple 4-byte records, also show as integer
    //       if (tree.record_size == 4) {
    //         uint32_t value;
    //         memcpy(&value, record, 4);
    //         std::cout << " (int: " << value << ")";
    //       }

    //       std::cout << std::endl;
    //     }
    //   }
    // }
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
    uint32_t *keys = get_keys(node);
    for (uint32_t i = 0; i < node->num_keys; i++) {
      hash ^= keys[i];
      hash *= prime;
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
        if (tree.tree_type == BTREE) {
          if (child_keys[j] > upper_separator) {
            std::cerr << "INVARIANT VIOLATION: Key " << child_keys[j]
                      << " in child " << child->index
                      << " violates upper bound " << upper_separator
                      << " from parent " << node->index << std::endl;

            exit(1);
          }
        } else {
          if (child_keys[j] >= upper_separator) {
            std::cerr << "INVARIANT VIOLATION: Key " << child_keys[j]
                      << " in child " << child->index
                      << " violates upper bound " << upper_separator
                      << " from parent " << node->index << std::endl;

            // bp_debug_print_tree(tree);
            exit(1);
          }
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
  uint32_t *keys = get_keys(node);
  for (uint32_t i = 1; i < node->num_keys; i++) {
    if (keys[i] <= keys[i - 1]) {
      std::cout << "// Leaf keys not in ascending order: keys[" << i - 1
                << "] = " << keys[i - 1] << " >= keys[" << i
                << "] = " << keys[i] << std::endl;
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
  uint32_t *keys = get_keys(node);
  for (uint32_t i = 1; i < node->num_keys; i++) {
    if (keys[i] <= keys[i - 1]) {
      std::cout << "// Internal keys not in ascending order: keys[" << i - 1
                << "] = " << keys[i - 1] << " >= keys[" << i
                << "] = " << keys[i] << std::endl;
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
  uint32_t *keys = get_keys(node);
  for (uint32_t i = 1; i < node->num_keys; i++) {
    if (keys[i] < keys[i - 1]) {
      std::cout << "// B-tree keys not in ascending order: keys[" << i - 1
                << "] = " << keys[i - 1] << " > keys[" << i << "] = " << keys[i]
                << std::endl;
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
  } else {
    // just junk, might just leave
    // // Leaf node sibling validation (if B-tree supports leaf linking)
    // if (node->next != 0) {
    //   BPTreeNode *next_node = static_cast<BPTreeNode
    //   *>(pager_get(node->next)); if (next_node && next_node->previous !=
    //   node->index) {
    //     std::cout << "// Next sibling's previous pointer does not point back
    //     "
    //                  "to this node"
    //               << std::endl;
    //     exit(0);
    //   }
    // }

    // if (node->previous != 0) {
    //   BPTreeNode *prev_node =
    //       static_cast<BPTreeNode *>(pager_get(node->previous));
    //   if (prev_node && prev_node->next != node->index) {
    //     std::cout << "// Previous sibling's next pointer does not point to "
    //                  "this node"
    //               << std::endl;
    //     exit(0);
    //   }
    // }
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
  while (root&& !root->is_leaf) {
    root= bp_get_child(tree, root, 0);
    expected_height++;
  }


  // Verify all nodes
  std::queue<BPTreeNode *> to_visit;
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
