#include "btree.hpp"
#include "arena.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ios>
#include <iostream>
#include <sys/types.h>

struct FindResult {
  BPTreeNode *node;
  uint32_t index;
  bool found;
};

void bp_repair_after_delete(BPlusTree &tree, BPTreeNode *node);

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

void bp_mark_dirty(BPTreeNode *node) { pager_mark_dirty(node->index); }
//

uint8_t *get_keys(BPTreeNode *node) { return node->data; }

uint8_t *get_key_at(BPlusTree &tree, BPTreeNode *node, uint32_t index) {
  return get_keys(node) + index * tree.node_key_size;
}

uint32_t *get_children(BPlusTree &tree, BPTreeNode *node) {
  if (node->is_leaf) {
    return nullptr; // Leaf nodes don't have children!
  }

  if (tree.tree_type == BTREE) {
    return reinterpret_cast<uint32_t *>(
        node->data + tree.internal_max_keys * tree.node_key_size +
        tree.internal_max_keys * tree.record_size);
  }
  return reinterpret_cast<uint32_t *>(node->data + tree.internal_max_keys *
                                                       tree.node_key_size);
}

uint8_t *get_internal_record_data(BPlusTree &tree, BPTreeNode *node) {
  return node->data + tree.internal_max_keys * tree.node_key_size;
}

uint8_t *get_internal_record_at(BPlusTree &tree, BPTreeNode *node,
                                uint32_t index) {
  return get_internal_record_data(tree, node) + (index * tree.record_size);
}

uint8_t *get_leaf_record_data(BPlusTree &tree, BPTreeNode *node) {
  return node->data + tree.leaf_max_keys * tree.node_key_size;
}

uint8_t *get_leaf_record_at(BPlusTree &tree, BPTreeNode *node, uint32_t index) {
  return get_leaf_record_data(tree, node) + (index * tree.record_size);
}

uint8_t *get_records(BPlusTree &tree, BPTreeNode *node) {
  return node->is_leaf ? get_leaf_record_data(tree, node)
                       : get_internal_record_data(tree, node);
}

uint8_t *get_record_at(BPlusTree &tree, BPTreeNode *node, uint32_t index) {
  if (node->is_leaf) {
    return get_leaf_record_at(tree, node, index);
  }
  return get_internal_record_at(tree, node, index);
}

uint32_t bp_binary_search(BPlusTree &tree, BPTreeNode *node,
                          const uint8_t *key) {
  uint32_t left = 0;
  uint32_t right = node->num_keys;

  while (left < right) {
    uint32_t mid = left + (right - left) / 2;
    int cmp_result = cmp(tree.node_key_size, get_key_at(tree, node, mid), key);

    if (cmp_result < 0) {
      left = mid + 1;
    } else if (cmp_result == 0) {
      if (tree.tree_type == BTREE) {
        return mid;
      }
      if (node->is_leaf) {
        return mid;
      }

      return mid + 1;
    } else {

      right = mid;
    }
  }

  if (node->is_leaf) {
    return left;
  } else {

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

  if ((record_size * minimum_entry_count) > USABLE_SPACE) {
    tree.tree_type = INVALID;
    return tree;
  }

  if (tree_type == BTREE) {
    uint32_t key_record_size = tree.node_key_size + record_size;
    uint32_t child_ptr_size = TYPE_INT32;

    uint32_t max_keys =
        (USABLE_SPACE - child_ptr_size) / (key_record_size + child_ptr_size);

    uint32_t min_keys;
    if (max_keys % 2 == 0) {
      min_keys = (max_keys / 2) - 1; // For max=12, min=5
    } else {
      min_keys = (max_keys) / 2; // For max=11, min=6
    }

    max_keys = std::max(minimum_entry_count, max_keys);

    uint32_t split_index = max_keys / 2;

    tree.leaf_max_keys = max_keys;
    tree.leaf_min_keys = min_keys;
    tree.leaf_split_index = split_index;

    tree.internal_max_keys = max_keys;
    tree.internal_min_keys = min_keys;
    tree.internal_split_index = split_index;
  } else if (tree_type == BPLUS) {
    uint32_t leaf_entry_size = tree.node_key_size + record_size;
    uint32_t leaf_max_entries = USABLE_SPACE / leaf_entry_size;

    tree.leaf_max_keys = std::max(minimum_entry_count, leaf_max_entries);
    tree.leaf_min_keys = tree.leaf_max_keys / 2;
    tree.leaf_split_index = tree.leaf_max_keys / 2;

    uint32_t child_ptr_size = TYPE_INT32;
    uint32_t internal_max_entries =
        (USABLE_SPACE - child_ptr_size) / (tree.node_key_size + child_ptr_size);

    if (internal_max_entries % 2 == 0) {
      tree.internal_min_keys =
          (internal_max_entries / 2) - 1; // For max=12, min=5
    } else {
      tree.internal_min_keys = (internal_max_entries) / 2; // For max=11, min=6
    }

    tree.internal_max_keys =
        std::max(minimum_entry_count, internal_max_entries);
    // tree.internal_min_keys = tree.internal_max_keys / 2;
    tree.internal_split_index = tree.internal_max_keys / 2;
  } else {

    tree.tree_type = INVALID;
    return tree;
  }

  tree.root_page_index = 0;
  return tree;
}

BPTreeNode *bp_create_node(BPlusTree &tree, bool is_leaf) {
  uint32_t page_index = pager_new();
  BPTreeNode *node = static_cast<BPTreeNode *>(pager_get(page_index));

  node->index = page_index;
  node->parent = 0;
  node->next = 0;
  node->previous = 0;
  node->num_keys = 0;
  node->is_leaf = is_leaf ? 1 : 0;

  pager_mark_dirty(page_index);
  return node;
}

void bp_init(BPlusTree &tree) {
  if (tree.root_page_index == 0) {

    BPTreeNode *root = bp_create_node(tree, true);
    tree.root_page_index = root->index;
  }
}

void bp_set_next(BPTreeNode *node, uint32_t index) {
  bp_mark_dirty(node);
  node->next = index;
}

void bp_set_prev(BPTreeNode *node, uint32_t index) {
  bp_mark_dirty(node);
  node->previous = index;
}

BPTreeNode *bp_get_parent(BPTreeNode *node) {
  if (!node || node->parent == 0) {
    return nullptr;
  }

  return static_cast<BPTreeNode *>(pager_get(node->parent));
}

BPTreeNode *bp_get_child(BPlusTree &tree, BPTreeNode *node, uint32_t index) {
  if (!node || node->is_leaf) {
    return nullptr;
  }
  uint32_t *children = get_children(tree, node);

  return static_cast<BPTreeNode *>(pager_get(children[index]));
}

BPTreeNode *bp_get_next(BPTreeNode *node) {
  if (!node || node->next == 0) {
    return nullptr;
  }

  return static_cast<BPTreeNode *>(pager_get(node->next));
}

BPTreeNode *bp_get_prev(BPTreeNode *node) {
  if (!node || node->previous == 0) {
    return nullptr;
  }

  return static_cast<BPTreeNode *>(pager_get(node->previous));
}

void bp_set_parent(BPTreeNode *node, uint32_t parent_index) {

  bp_mark_dirty(node);
  node->parent = parent_index;

  if (parent_index != 0) {
    pager_mark_dirty(parent_index);
  }
}

void bp_set_child(BPlusTree &tree, BPTreeNode *node, uint32_t child_index,
                  uint32_t node_index) {
  if (!node || node->is_leaf) {
    return;
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

void bp_destroy_node(BPTreeNode *node) {
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

bool is_match(BPlusTree &tree, const uint8_t *key, BPTreeNode *node,
              uint32_t index) {
  return cmp(tree.node_key_size, key, get_key_at(tree, node, index)) == 0;
}

// Fixed bp_find_containing_node that properly tracks child positions
FindResult bp_find_containing_node(BPlusTree &tree, BPTreeNode *node,
                                   const uint8_t *key, BtCursor *cursor) {
  uint32_t index = bp_binary_search(tree, node, key);

  auto stack = &cursor->stack;

  if (node->is_leaf) {
    // For leaf nodes, add to stack with child_pos = index (though not used for
    // leaves)
    if (stack) {
      stack->page_stack[stack->stack_depth] = node->index;
      stack->index_stack[stack->stack_depth] = index;
      stack->child_stack[stack->stack_depth] = index;
      stack->stack_depth++;
    }

    return {.node = const_cast<BPTreeNode *>(node),
            .index = index,
            .found =
                (index < node->num_keys && is_match(tree, key, node, index))};
  }

  // For internal nodes in B-tree, check if key matches
  if (tree.tree_type == BTREE) {
    if (index < node->num_keys && is_match(tree, key, node, index)) {
      // Found in internal node
      if (stack) {
        stack->page_stack[stack->stack_depth] = node->index;
        stack->index_stack[stack->stack_depth] = index;
        stack->child_stack[stack->stack_depth] =
            index; // Not descending, but at this key
        stack->stack_depth++;
      }
      return {.node = const_cast<BPTreeNode *>(node),
              .index = index,
              .found = true};
    }
  }

  // Need to descend to child
  uint32_t child_pos = index; // Which child we're descending to

  // Add current node to stack BEFORE descending
  if (stack) {
    stack->page_stack[stack->stack_depth] = node->index;
    stack->index_stack[stack->stack_depth] =
        (child_pos > 0) ? child_pos - 1 : 0; // Parent key index
    stack->child_stack[stack->stack_depth] =
        child_pos; // Child position we're descending through
    stack->stack_depth++;
  }

  BPTreeNode *child = bp_get_child(tree, node, child_pos);
  if (!child) {
    // Error case - return not found
    return {
        .node = const_cast<BPTreeNode *>(node), .index = index, .found = false};
  }

  return bp_find_containing_node(tree, child, key, cursor);
}

BPTreeNode *bp_split(BPlusTree &tree, BPTreeNode *node) {
  BPTreeNode *right_node = bp_create_node(tree, node->is_leaf);
  uint32_t split_index = bp_get_split_index(tree, node);
  uint8_t *rising_key = get_key_at(tree, node, split_index);

  bp_mark_dirty(right_node);
  bp_mark_dirty(node);

  BPTreeNode *parent = bp_get_parent(node);
  uint32_t parent_index = 0;

  if (!parent) {

    parent = bp_create_node(tree, false);
    bp_mark_dirty(parent);
    tree.root_page_index = parent->index;
    bp_set_child(tree, parent, 0, node->index);

  } else {

    bp_mark_dirty(parent);

    uint32_t *parent_children = get_children(tree, parent);
    while (parent_children[parent_index] != node->index)
      parent_index++;

    memcpy(parent_children + parent_index + 2,
           parent_children + parent_index + 1,
           (parent->num_keys - parent_index) * TYPE_INT32);

    memcpy(get_key_at(tree, parent, parent_index + 1),
           get_key_at(tree, parent, parent_index),
           (parent->num_keys - parent_index) * tree.node_key_size);

    if (tree.tree_type == BTREE) {

      uint8_t *parent_records = get_internal_record_data(tree, parent);
      memcpy(parent_records + (parent_index + 1) * tree.record_size,
             parent_records + parent_index * tree.record_size,
             (parent->num_keys - parent_index) * tree.record_size);
    }
  }

  memcpy(get_key_at(tree, parent, parent_index), rising_key,
         tree.node_key_size);
  bp_set_child(tree, parent, parent_index + 1, right_node->index);
  parent->num_keys++;

  if (tree.tree_type == BTREE) {

    uint8_t *parent_records = get_internal_record_data(tree, parent);
    uint8_t *source_records = get_records(tree, node);
    memcpy(parent_records + parent_index * tree.record_size,
           source_records + split_index * tree.record_size, tree.record_size);
  }

  if (node->is_leaf && tree.tree_type == BPLUS) {

    right_node->num_keys = node->num_keys - split_index;
    memcpy(get_keys(right_node), rising_key,
           tree.node_key_size * right_node->num_keys);
    memcpy(get_leaf_record_data(tree, right_node),
           get_record_at(tree, node, split_index),
           right_node->num_keys * tree.record_size);

    right_node->next = node->next;
    right_node->previous = node->index;
    if (node->next != 0) {
      BPTreeNode *next = bp_get_next(node);
      if (next)
        bp_set_prev(next, right_node->index);
    }
    node->next = right_node->index;
  } else {

    right_node->num_keys = node->num_keys - split_index - 1;
    memcpy(get_keys(right_node), get_key_at(tree, node, split_index + 1),
           right_node->num_keys * tree.node_key_size);

    if (tree.tree_type == BTREE) {

      uint8_t *src_records = get_records(tree, node);
      uint8_t *dst_records = get_records(tree, right_node);
      memcpy(dst_records, src_records + (split_index + 1) * tree.record_size,
             right_node->num_keys * tree.record_size);
    }

    if (!node->is_leaf) {

      uint32_t *src_children = get_children(tree, node);
      for (uint32_t i = 0; i <= right_node->num_keys; i++) {
        uint32_t child = src_children[split_index + 1 + i];
        if (child) {

          bp_set_child(tree, right_node, i, child);
          src_children[split_index + 1 + i] = 0;
        }
      }
    }
  }

  node->num_keys = split_index;
  return parent;
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

bool bp_insert(BPlusTree &tree, BPTreeNode *node, uint8_t *key,
               const uint8_t *data) {

  if (node->is_leaf) {
    uint8_t *keys = get_keys(node);
    uint8_t *record_data = get_leaf_record_data(tree, node);

    uint32_t insert_index = bp_binary_search(tree, node, key);

    if (tree.tree_type == BPLUS && insert_index < node->num_keys &&
        is_match(tree, key, node, insert_index)) {

      bp_mark_dirty(node);
      memcpy(record_data + insert_index * tree.record_size, data,
             tree.record_size);
      return true;
    }

    if (node->num_keys >= tree.leaf_max_keys) {
      bp_insert_repair(tree, node);
      return false;
    }

    bp_mark_dirty(node);

    if (tree.tree_type == BTREE) {
      while (insert_index < node->num_keys &&
             // handle duplicates
             is_match(tree, key, node, insert_index)) {
        insert_index++;
      }
    }

    uint32_t num_to_shift = node->num_keys - insert_index;

    memcpy(get_key_at(tree, node, insert_index + 1),
           get_key_at(tree, node, insert_index),
           num_to_shift * tree.node_key_size);

    memcpy(record_data + (insert_index + 1) * tree.record_size,
           record_data + insert_index * tree.record_size,
           num_to_shift * tree.record_size);

    memcpy(get_key_at(tree, node, insert_index), key, tree.node_key_size);
    memcpy(record_data + insert_index * tree.record_size, data,
           tree.record_size);

    node->num_keys++;
    return true;
  } else {

    uint32_t child_index = bp_binary_search(tree, node, key);

    BPTreeNode *child_node = bp_get_child(tree, node, child_index);
    if (child_node) {
      return bp_insert(tree, child_node, key, data);
    }
  }
}

void bp_insert_element(BPlusTree &tree, void *key, const uint8_t *data) {
  BPTreeNode *root = bp_get_root(tree);

  if (root->num_keys == 0) {
    bp_mark_dirty(root);
    uint8_t *keys = get_keys(root);
    uint8_t *record_data = get_leaf_record_data(tree, root);

    memcpy(keys, key, tree.node_key_size);
    memcpy(record_data, data, tree.record_size);
    root->num_keys = 1;
  } else {
    if (!bp_insert(tree, root, (uint8_t *)key, data)) {
      // was full, did repair, try again
      bp_insert(tree, bp_get_root(tree), (uint8_t *)key, data);
    }
  }
}

void bp_do_delete_btree(BPlusTree &tree, BPTreeNode *node, const uint8_t *key,
                        uint32_t i) {
  if (node->is_leaf) {
    bp_mark_dirty(node);
    uint8_t *record_data = get_leaf_record_data(tree, node);

    uint32_t shift_count = node->num_keys - i - 1;

    memcpy(get_key_at(tree, node, i), get_key_at(tree, node, i + 1),
           tree.node_key_size * shift_count);
    memcpy(record_data + i * tree.record_size,
           record_data + (i + 1) * tree.record_size,
           tree.record_size * shift_count);

    node->num_keys--;
    bp_repair_after_delete(tree, node);
  } else {
    uint32_t index = i;
    BPTreeNode *curr = bp_get_child(tree, node, index);

    while (!curr->is_leaf) {
      curr = bp_get_child(tree, curr, curr->num_keys);
    }

    uint32_t pred_index = curr->num_keys - 1;
    uint8_t *pred_key = get_key_at(tree, curr, pred_index);
    uint8_t *pred_record = get_record_at(tree, curr, pred_index);

    bp_mark_dirty(node);
    memcpy(get_key_at(tree, node, index), pred_key, tree.node_key_size);
    uint8_t *internal_records = get_internal_record_data(tree, node);
    memcpy(internal_records + index * tree.record_size, pred_record,
           tree.record_size);

    bp_mark_dirty(curr);
    uint8_t *leaf_records = get_leaf_record_data(tree, curr);

    uint32_t elements_to_shift = curr->num_keys - 1 - pred_index;
    memcpy(get_key_at(tree, curr, pred_index),
           get_key_at(tree, curr, pred_index + 1),
           elements_to_shift * tree.node_key_size);
    memcpy(leaf_records + pred_index * tree.record_size,
           leaf_records + (pred_index + 1) * tree.record_size,
           elements_to_shift * tree.record_size);

    curr->num_keys--;
    bp_repair_after_delete(tree, curr);
  }
}

void bp_update_parent_keys(BPlusTree &tree, BPTreeNode *node,
                           const uint8_t *deleted_key) {
  uint8_t *next_smallest = nullptr;
  BPTreeNode *parent_node = bp_get_parent(node);

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
        cmp(tree.node_key_size,
            get_key_at(tree, current_parent, parent_index - 1),
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

void bp_do_delete_bplus(BPlusTree &tree, BPTreeNode *node, const uint8_t *key,
                        uint32_t i) {

  if (node->is_leaf) {
    bp_mark_dirty(node);

    uint8_t *record_data = get_leaf_record_data(tree, node);
    uint32_t shift_count = node->num_keys - i - 1;

    memcpy(get_key_at(tree, node, i), get_key_at(tree, node, i + 1),
           tree.node_key_size * shift_count);
    memcpy(record_data + i * tree.record_size,
           record_data + (i + 1) * tree.record_size,
           tree.record_size * shift_count);

    node->num_keys--;

    if (i == 0 && node->parent != 0) {
      bp_update_parent_keys(tree, node, key);
    }

    bp_repair_after_delete(tree, node);
  }
}

void bp_do_delete(BPlusTree &tree, BPTreeNode *node, const uint8_t *key,
                  uint32_t index) {

  /* 0 or 1 key in a root && leaf node is a special case
   */
  if (node->parent == 0 && node->num_keys <= 1 && node->is_leaf) {
    bp_mark_dirty(node);
    node->num_keys = 0;
    return;
  }

  if (tree.tree_type == BTREE) {
    bp_do_delete_btree(tree, node, key, index);
  } else {
    bp_do_delete_bplus(tree, node, key, index);
  }
}

BPTreeNode *bp_steal_from_right(BPlusTree &tree, BPTreeNode *node,
                                uint32_t parent_index) {
  BPTreeNode *parent_node = bp_get_parent(node);
  BPTreeNode *right_sibling = bp_get_child(tree, parent_node, parent_index + 1);

  bp_mark_dirty(node);
  bp_mark_dirty(parent_node);
  bp_mark_dirty(right_sibling);

  if (node->is_leaf) {
    memcpy(get_key_at(tree, node, node->num_keys),
           get_key_at(tree, right_sibling, 0), tree.node_key_size);

    uint8_t *node_records = get_leaf_record_data(tree, node);
    uint8_t *sibling_records = get_leaf_record_data(tree, right_sibling);

    memcpy(node_records + node->num_keys * tree.record_size, sibling_records,
           tree.record_size);

    uint32_t shift_count = right_sibling->num_keys - 1;
    memcpy(get_key_at(tree, right_sibling, 0),
           get_key_at(tree, right_sibling, 1),
           shift_count * tree.node_key_size);
    memcpy(sibling_records, sibling_records + tree.record_size,
           shift_count * tree.record_size);

    memcpy(get_key_at(tree, parent_node, parent_index),
           get_key_at(tree, right_sibling, 0), tree.node_key_size);
  } else {
    memcpy(get_key_at(tree, node, node->num_keys),
           get_key_at(tree, parent_node, parent_index), tree.node_key_size);

    if (tree.tree_type == BTREE) {
      uint8_t *node_records = get_internal_record_data(tree, node);
      uint8_t *parent_records = get_internal_record_data(tree, parent_node);
      uint8_t *sibling_records = get_internal_record_data(tree, right_sibling);

      memcpy(node_records + node->num_keys * tree.record_size,
             parent_records + parent_index * tree.record_size,
             tree.record_size);

      memcpy(get_key_at(tree, parent_node, parent_index),
             get_key_at(tree, right_sibling, 0), tree.node_key_size);
      memcpy(parent_records + parent_index * tree.record_size, sibling_records,
             tree.record_size);

      uint32_t shift_count = right_sibling->num_keys - 1;
      memcpy(sibling_records, sibling_records + tree.record_size,
             shift_count * tree.record_size);
    } else {

      memcpy(get_key_at(tree, parent_node, parent_index),
             get_key_at(tree, right_sibling, 0), tree.node_key_size);
    }

    uint32_t shift_count = right_sibling->num_keys - 1;
    memcpy(get_key_at(tree, right_sibling, 0),
           get_key_at(tree, right_sibling, 1),
           shift_count * tree.node_key_size);

    uint32_t *node_children = get_children(tree, node);
    uint32_t *sibling_children = get_children(tree, right_sibling);

    bp_set_child(tree, node, node->num_keys + 1, sibling_children[0]);

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

  uint32_t node_index = 0;
  for (; node_index <= parent->num_keys; node_index++) {

    if (parent_children[node_index] == node->index) {
      break;
    }
  }

  if (node_index >= parent->num_keys) {
    return node;
  }

  BPTreeNode *right_sibling = bp_get_child(tree, parent, node_index + 1);
  if (!right_sibling) {
    return node;
  }

  bp_mark_dirty(node);
  bp_mark_dirty(parent);

  if (node->is_leaf) {
    uint8_t *node_records = get_leaf_record_data(tree, node);
    uint8_t *sibling_records = get_leaf_record_data(tree, right_sibling);

    memcpy(get_key_at(tree, node, node->num_keys),
           get_key_at(tree, right_sibling, 0),
           right_sibling->num_keys * tree.node_key_size);
    memcpy(node_records + node->num_keys * tree.record_size, sibling_records,
           right_sibling->num_keys * tree.record_size);

    node->num_keys += right_sibling->num_keys;

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
    memcpy(get_key_at(tree, node, node->num_keys),
           get_key_at(tree, parent, node_index), tree.node_key_size);

    if (tree.tree_type == BTREE) {
      uint8_t *node_records = get_internal_record_data(tree, node);
      uint8_t *parent_records = get_internal_record_data(tree, parent);
      uint8_t *sibling_records = get_internal_record_data(tree, right_sibling);

      memcpy(node_records + node->num_keys * tree.record_size,
             parent_records + node_index * tree.record_size, tree.record_size);

      memcpy(node_records + (node->num_keys + 1) * tree.record_size,
             sibling_records, right_sibling->num_keys * tree.record_size);
    }

    memcpy(get_key_at(tree, node, node->num_keys + 1),
           get_key_at(tree, right_sibling, 0),
           right_sibling->num_keys * tree.node_key_size);

    uint32_t *node_children = get_children(tree, node);
    uint32_t *sibling_children = get_children(tree, right_sibling);

    for (uint32_t i = 0; i <= right_sibling->num_keys; i++) {
      bp_set_child(tree, node, node->num_keys + 1 + i, sibling_children[i]);
    }

    node->num_keys += 1 + right_sibling->num_keys;
  }

  uint32_t shift_count = parent->num_keys - node_index - 1;

  memcpy(get_key_at(tree, parent, node_index),
         get_key_at(tree, parent, node_index + 1),
         shift_count * tree.node_key_size);
  memcpy(parent_children + node_index + 1, parent_children + node_index + 2,
         shift_count * sizeof(uint32_t));

  if (tree.tree_type == BTREE && !parent->is_leaf) {
    uint8_t *parent_records = get_internal_record_data(tree, parent);
    memcpy(parent_records + node_index * tree.record_size,
           parent_records + (node_index + 1) * tree.record_size,
           shift_count * tree.record_size);
  }

  parent->num_keys--;

  bp_destroy_node(right_sibling);

  if (parent->num_keys < bp_get_min_keys(tree, parent)) {
    if (parent->parent == 0 && parent->num_keys == 0) {
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

BPTreeNode *bp_steal_from_left(BPlusTree &tree, BPTreeNode *node,
                               uint32_t parent_index) {
  BPTreeNode *parent_node = bp_get_parent(node);
  BPTreeNode *left_sibling = bp_get_child(tree, parent_node, parent_index - 1);

  bp_mark_dirty(node);
  bp_mark_dirty(parent_node);
  bp_mark_dirty(left_sibling);

  memcpy(get_key_at(tree, node, 1), get_key_at(tree, node, 0),
         tree.node_key_size * node->num_keys);

  if (node->is_leaf) {
    memcpy(get_key_at(tree, node, 0),
           get_key_at(tree, left_sibling, left_sibling->num_keys - 1),
           tree.node_key_size);

    uint8_t *node_records = get_leaf_record_data(tree, node);
    uint8_t *sibling_records = get_leaf_record_data(tree, left_sibling);

    memcpy(node_records + tree.record_size, node_records,
           node->num_keys * tree.record_size);

    memcpy(node_records,
           sibling_records + (left_sibling->num_keys - 1) * tree.record_size,
           tree.record_size);

    memcpy(get_key_at(tree, parent_node, parent_index - 1),
           get_key_at(tree, node, 0), tree.node_key_size);
  } else {
    memcpy(get_key_at(tree, node, 0),
           get_key_at(tree, parent_node, parent_index - 1), tree.node_key_size);

    if (tree.tree_type == BTREE) {
      uint8_t *node_records = get_internal_record_data(tree, node);
      uint8_t *parent_records = get_internal_record_data(tree, parent_node);
      uint8_t *sibling_records = get_internal_record_data(tree, left_sibling);

      memcpy(node_records + tree.record_size, node_records,
             node->num_keys * tree.record_size);

      memcpy(node_records,
             parent_records + (parent_index - 1) * tree.record_size,
             tree.record_size);

      memcpy(get_key_at(tree, parent_node, parent_index - 1),
             get_key_at(tree, left_sibling, left_sibling->num_keys - 1),
             tree.node_key_size);
      memcpy(parent_records + (parent_index - 1) * tree.record_size,
             sibling_records + (left_sibling->num_keys - 1) * tree.record_size,
             tree.record_size);
    } else {

      memcpy(get_key_at(tree, parent_node, parent_index - 1),
             get_key_at(tree, left_sibling, left_sibling->num_keys - 1),
             tree.node_key_size);
    }

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

void bp_repair_after_delete(BPlusTree &tree, BPTreeNode *node) {
  if (node->num_keys >= bp_get_min_keys(tree, node)) {
    return;
  }

  if (node->parent == 0) {
    if (node->num_keys == 0 && !node->is_leaf) {
      BPTreeNode *only_child = bp_get_child(tree, node, 0);
      if (only_child) {

        tree.root_page_index = only_child->index;
        bp_set_parent(only_child, 0);
        bp_destroy_node(node);
      }
    }
    return;
  }

  BPTreeNode *parent = bp_get_parent(node);
  uint32_t *parent_children = get_children(tree, parent);

  uint32_t node_index = 0;
  for (; node_index <= parent->num_keys; node_index++) {
    if (parent_children[node_index] == node->index) {
      break;
    }
  }

  if (node_index > 0) {
    BPTreeNode *left_sibling = bp_get_child(tree, parent, node_index - 1);
    if (left_sibling &&
        left_sibling->num_keys > bp_get_min_keys(tree, left_sibling)) {
      bp_steal_from_left(tree, node, node_index);
      return;
    }
  }

  if (node_index < parent->num_keys) {
    BPTreeNode *right_sibling = bp_get_child(tree, parent, node_index + 1);
    if (right_sibling &&
        right_sibling->num_keys > bp_get_min_keys(tree, right_sibling)) {
      bp_steal_from_right(tree, node, node_index);
      return;
    }
  }

  if (node_index < parent->num_keys) {
    bp_merge_right(tree, node);
  } else if (node_index > 0) {
    BPTreeNode *left_sibling = bp_get_child(tree, parent, node_index - 1);
    if (left_sibling) {

      bp_merge_right(tree, left_sibling);
    }
  }
}

/* ------ CURSOR ----------- */
// Add to btree.cpp - B-tree/B+tree Cursor Implementation

// - new
//
// Internal helper functions
static void cursor_push_stack(BtCursor *cursor, uint32_t page, uint32_t index,
                              uint32_t child_pos) {
  if (cursor->stack.stack_depth < 16) {
    cursor->stack.page_stack[cursor->stack.stack_depth] = page;
    cursor->stack.index_stack[cursor->stack.stack_depth] = index;
    cursor->stack.child_stack[cursor->stack.stack_depth] = child_pos;
    cursor->stack.stack_depth++;
  }
}

static void cursor_pop_stack(BtCursor *cursor) {
  if (cursor->stack.stack_depth > 0) {
    cursor->stack.stack_depth--;
    cursor->current_page = cursor->stack.page_stack[cursor->stack.stack_depth];
    cursor->current_index =
        cursor->stack.index_stack[cursor->stack.stack_depth];
  }
}

static void cursor_clear_stack(BtCursor *cursor) {
  cursor->stack.stack_depth = 0;
}

// Save complete cursor state
struct CursorSaveState {
  uint32_t current_page;
  uint32_t current_index;
  uint32_t page_stack[16];
  uint32_t index_stack[16];
  uint32_t child_stack[16];
  uint32_t stack_depth;
};

// Create a new cursor
BtCursor bt_cursor_create(BPlusTree *tree) {

  return {.tree = tree,
          .stack =
              {.stack_depth = 0},
          .current_index = 0,
          .current_page = 0,
          .state = CURSOR_INVALID

  };
}

// Destroy cursor

static void cursor_clear_stack(BtCursor *cursor);

// Clear cursor state
void bt_cursor_clear(BtCursor *cursor) {
  cursor->stack.stack_depth = 0;
  cursor->current_page = 0;
  cursor->current_index = 0;
  cursor->state = CURSOR_INVALID;
}

// Check if cursor is valid
bool bt_cursor_is_valid(BtCursor *cursor) {
  return cursor->state == CURSOR_VALID;
}

// Get current key
const uint8_t *bt_cursor_get_key(BtCursor *cursor) {
  if (cursor->state != CURSOR_VALID) {
    return nullptr;
  }

  BPTreeNode *node = static_cast<BPTreeNode *>(pager_get(cursor->current_page));
  if (!node || cursor->current_index >= node->num_keys) {
    return nullptr;
  }

  return get_key_at(*cursor->tree, node, cursor->current_index);
}

// Read current record
uint8_t *bt_cursor_read(BtCursor *cursor) {
  if (cursor->state != CURSOR_VALID) {
    return nullptr;
  }

  BPTreeNode *node = static_cast<BPTreeNode *>(pager_get(cursor->current_page));
  if (!node || cursor->current_index >= node->num_keys) {
    return nullptr;
  }

  return get_record_at(*cursor->tree, node, cursor->current_index);
}

// Seek to specific key (positions at first occurrence if duplicates exist)
bool bt_cursor_seek(BtCursor *cursor, const void *key) {
  bt_cursor_clear(cursor);
  cursor_clear_stack(cursor);

  FindResult result = bp_find_containing_node(
      *cursor->tree, bp_get_root(*cursor->tree), (const uint8_t *)key, cursor);

  cursor->current_page = result.node->index;
  cursor->current_index = result.index;

  if (result.found) {
    cursor->state = CURSOR_VALID;
    return true;
  }

  cursor->state =
      (result.index < result.node->num_keys) ? CURSOR_VALID : CURSOR_INVALID;
  return false;
}

// Delete at cursor position
bool bt_cursor_delete(BtCursor *cursor) {
  if (cursor->state != CURSOR_VALID) {
    return false;
  }

  const uint8_t *key = bt_cursor_get_key(cursor);

  if (!key) {
    return false;
  }

  auto node = static_cast<BPTreeNode *>(pager_get(cursor->current_page));
  uint32_t index = node->index;

  bp_do_delete(*cursor->tree, node, key, cursor->current_index);

  // After delete, cursor position depends on what happened
  // Re-fetch the node as it might have changed

  node = static_cast<BPTreeNode *>(pager_get(cursor->current_page));
  if (!node || node->index != index) {
    // Node was deleted/merged
    cursor->state = CURSOR_INVALID;
    return true;
  }

  if (cursor->current_index >= node->num_keys) {
    if (node->num_keys > 0) {
      cursor->current_index = node->num_keys - 1;
    } else {
      cursor->state = CURSOR_INVALID;
    }
  }

  return true;
}

// Insert at cursor position
bool bt_cursor_insert(BtCursor *cursor, const void *key,
                      const uint8_t *record) {

  // Insert uses the regular insert function
  bp_insert_element(*cursor->tree, (void *)key, record);

  // Reposition cursor to the newly inserted element
  return bt_cursor_seek(cursor, key);
}

// Update record at cursor position (update is just insert for existing key)
bool bt_cursor_update(BtCursor *cursor, const uint8_t *record) {
  if (cursor->state != CURSOR_VALID) {
    return false;
  }

  uint8_t *data = bt_cursor_read(cursor);
  memcpy(data, record, cursor->tree->record_size);

  return true;
}

enum SeekOp { SEEK_GE, SEEK_GT, SEEK_LE, SEEK_LT };

bool bt_cursor_seek_cmp(BtCursor *cursor, const void *key, SeekOp op) {
  bool exact_match_ok = (op == SEEK_GE || op == SEEK_LE);
  bool forward = (op == SEEK_GE || op == SEEK_GT);

  if (exact_match_ok && bt_cursor_seek(cursor, key)) {
    return true;
  } else {
    bt_cursor_seek(cursor, key); // Position somewhere
  }

  do {
    const uint8_t *current_key = bt_cursor_get_key(cursor);
    if (!current_key)
      continue;

    int cmp_result =
        cmp(cursor->tree->node_key_size, current_key, (const uint8_t *)key);

    switch (op) {
    case SEEK_GE:
      if (cmp_result >= 0)
        return true;
      break;
    case SEEK_GT:
      if (cmp_result > 0)
        return true;
      break;
    case SEEK_LE:
      if (cmp_result <= 0)
        return true;
      break;
    case SEEK_LT:
      if (cmp_result < 0)
        return true;
      break;
    }
  } while (forward ? bt_cursor_next(cursor) : bt_cursor_previous(cursor));

  return false;
}

// Then just:
bool bt_cursor_seek_ge(BtCursor *cursor, const void *key) {
  return bt_cursor_seek_cmp(cursor, key, SEEK_GE);
}
bool bt_cursor_seek_gt(BtCursor *cursor, const void *key) {
  return bt_cursor_seek_cmp(cursor, key, SEEK_GT);
}
bool bt_cursor_seek_le(BtCursor *cursor, const void *key) {
  return bt_cursor_seek_cmp(cursor, key, SEEK_LE);
}
bool bt_cursor_seek_lt(BtCursor *cursor, const void *key) {
  return bt_cursor_seek_cmp(cursor, key, SEEK_LT);
}

static void cursor_save_state(BtCursor *cursor, CursorSaveState *save) {
  save->current_page = cursor->current_page;
  save->current_index = cursor->current_index;
  save->stack_depth = cursor->stack.stack_depth;
  memcpy(save->page_stack, cursor->stack.page_stack, sizeof(save->page_stack));
  memcpy(save->index_stack, cursor->stack.index_stack,
         sizeof(save->index_stack));
  memcpy(save->child_stack, cursor->stack.child_stack,
         sizeof(save->child_stack));
}

static void cursor_restore_state(BtCursor *cursor,
                                 const CursorSaveState *save) {
  cursor->current_page = save->current_page;
  cursor->current_index = save->current_index;
  cursor->stack.stack_depth = save->stack_depth;
  memcpy(cursor->stack.page_stack, save->page_stack, sizeof(save->page_stack));
  memcpy(cursor->stack.index_stack, save->index_stack,
         sizeof(save->index_stack));
  memcpy(cursor->stack.child_stack, save->child_stack,
         sizeof(save->child_stack));
}

static bool cursor_move_in_subtree(BtCursor *cursor, BPTreeNode *root,
                                   bool left) {
  BPTreeNode *current = root;

  while (!current->is_leaf) {
    if (left) {

      cursor_push_stack(cursor, current->index, 0,
                        0); // child_pos = 0 for leftmost
      current = bp_get_child(*cursor->tree, current, 0);
    } else {

      uint32_t child_pos = current->num_keys; // Rightmost child
      cursor_push_stack(cursor, current->index, current->num_keys - 1,
                        child_pos);
      current = bp_get_child(*cursor->tree, current, child_pos);
    }

    if (!current) {
      cursor->state = CURSOR_FAULT;
      return false;
    }
  }

  if (left) {
    cursor->current_page = current->index;
    cursor->current_index = 0;
  } else {
    cursor->current_page = current->index;
    cursor->current_index = current->num_keys - 1;
  }
  cursor->state = CURSOR_VALID;
  return true;
}

// Move to leftmost key in subtree
static bool cursor_move_to_leftmost_in_subtree(BtCursor *cursor,
                                               BPTreeNode *root) {
  return cursor_move_in_subtree(cursor, root, true);
}

// Move to rightmost key in subtree
static bool cursor_move_to_rightmost_in_subtree(BtCursor *cursor,
                                                BPTreeNode *root) {

  return cursor_move_in_subtree(cursor, root, false);
}

// Move to first key in tree
bool bt_cursor_move_end(BtCursor *cursor, bool first) {
  bt_cursor_clear(cursor);

  BPTreeNode *root = bp_get_root(*cursor->tree);
  if (!root || root->num_keys == 0) {
    cursor->state = CURSOR_INVALID;
    return false;
  }

  return cursor_move_in_subtree(cursor, root, first);
}

bool bt_cursor_first(BtCursor *cursor) {
  return bt_cursor_move_end(cursor, true);
}
// Move to last key in tree
bool bt_cursor_last(BtCursor *cursor) {
  return bt_cursor_move_end(cursor, false);
}

// Move to next key
bool bt_cursor_next(BtCursor *cursor) {
  if (cursor->state != CURSOR_VALID) {
    return false;
  }

  BPTreeNode *node = static_cast<BPTreeNode *>(pager_get(cursor->current_page));
  if (!node) {
    cursor->state = CURSOR_FAULT;
    return false;
  }

  // Save complete state for restoration on failure
  CursorSaveState saved_state;
  cursor_save_state(cursor, &saved_state);

  // If leaf node in B+tree, use next pointer for efficiency
  if (cursor->tree->tree_type == BPLUS) {
    if (!node->is_leaf) {
      cursor_restore_state(cursor, &saved_state);
      printf("Cannot traverse internal b+tree nodes");
      exit(0);
      return false;
    }

    cursor->current_index++;
    if (cursor->current_index >= node->num_keys) {
      if (node->next != 0) {
        BPTreeNode *next_node = bp_get_next(node);
        if (next_node && next_node->num_keys > 0) {
          cursor->current_page = next_node->index;
          cursor->current_index = 0;
          return true;
        }
      }

      cursor_restore_state(cursor, &saved_state);
      return false;
    }
    return true;
  }

  // BTREE
  if (node->is_leaf) {
    // Try next key in current leaf
    cursor->current_index++;
    if (cursor->current_index < node->num_keys) {
      return true;
    }

    // Need to go up to find next key
    while (cursor->stack.stack_depth > 0) {
      // Get which child position we were at
      uint32_t child_pos =
          cursor->stack.child_stack[cursor->stack.stack_depth - 1];

      cursor_pop_stack(cursor);

      if (cursor->stack.stack_depth == 0) {
        // Reached root, no more elements
        cursor_restore_state(cursor, &saved_state);
        return false;
      }

      BPTreeNode *parent =
          static_cast<BPTreeNode *>(pager_get(cursor->current_page));

      // In B-tree, after visiting left subtree (child i), visit key i
      if (child_pos <= cursor->current_index) {
        // We came from left subtree, now visit the parent key
        cursor->state = CURSOR_VALID;
        return true;
      }

      // After visiting key i, go to child i+1
      if (child_pos == cursor->current_index + 1 &&
          child_pos <= parent->num_keys) {
        cursor->current_index++;
        if (cursor->current_index < parent->num_keys) {
          // Visit next key in parent
          return true;
        }
        // Otherwise continue going up
      }
    }
  } else {
    // Internal node - we need to visit right subtree
    uint32_t next_child = cursor->current_index + 1;
    BPTreeNode *child = bp_get_child(*cursor->tree, node, next_child);
    if (child) {
      cursor_push_stack(cursor, node->index, cursor->current_index, next_child);
      return cursor_move_to_leftmost_in_subtree(cursor, child);
    }
  }

  // No next element found - restore state
  cursor_restore_state(cursor, &saved_state);
  return false;
}

// Move to previous key
bool bt_cursor_previous(BtCursor *cursor) {
  if (cursor->state != CURSOR_VALID) {
    return false;
  }

  BPTreeNode *node = static_cast<BPTreeNode *>(pager_get(cursor->current_page));
  if (!node) {
    cursor->state = CURSOR_FAULT;
    return false;
  }

  // Save complete state for restoration on failure
  CursorSaveState saved_state;
  cursor_save_state(cursor, &saved_state);

  // If leaf node in B+tree, use previous pointer for efficiency
  if (cursor->tree->tree_type == BPLUS) {
    if (!node->is_leaf) {
      PRINT "BPLUS LEAF PREV SHOULD BE" END;
      exit(0);
      return false;
    }
    if (cursor->current_index > 0) {
      cursor->current_index--;
      return true;
    }

    // Move to previous leaf
    if (node->previous != 0) {
      BPTreeNode *prev_node = bp_get_prev(node);
      if (prev_node && prev_node->num_keys > 0) {
        cursor->current_page = prev_node->index;
        cursor->current_index = prev_node->num_keys - 1;
        return true;
      }
    }

    // No previous node - restore and return false
    cursor_restore_state(cursor, &saved_state);
    return false;
  }

  // For B-tree traversal

  if (node->is_leaf) {
    if (cursor->current_index > 0) {
      cursor->current_index--;
      return true;
    }

    // Need to go up to find previous key
    while (cursor->stack.stack_depth > 0) {
      uint32_t child_pos =
          cursor->stack.child_stack[cursor->stack.stack_depth - 1];

      cursor_pop_stack(cursor);

      if (cursor->stack.stack_depth == 0) {
        // Reached root, no more elements
        cursor_restore_state(cursor, &saved_state);
        return false;
      }

      BPTreeNode *parent =
          static_cast<BPTreeNode *>(pager_get(cursor->current_page));
      if (!parent) {
        cursor_restore_state(cursor, &saved_state);
        return false;
      }

      // In B-tree, after visiting right subtree (child i+1), visit key i
      if (child_pos > 0 && child_pos == cursor->current_index + 1) {
        // We came from right subtree, visit the parent key
        cursor->state = CURSOR_VALID;
        return true;
      }

      // If we came from child 0, need to continue up
      if (child_pos == 0) {
        continue;
      }

      // Move to previous key's right subtree
      if (cursor->current_index > 0) {
        cursor->current_index--;
        BPTreeNode *child =
            bp_get_child(*cursor->tree, parent, cursor->current_index + 1);
        if (child) {
          cursor_push_stack(cursor, parent->index, cursor->current_index,
                            cursor->current_index + 1);
          return cursor_move_to_rightmost_in_subtree(cursor, child);
        }
      }
    }
  } else {
    // Internal node - visit left subtree's rightmost
    BPTreeNode *child =
        bp_get_child(*cursor->tree, node, cursor->current_index);
    if (child) {
      cursor_push_stack(cursor, node->index, cursor->current_index,
                        cursor->current_index);
      return cursor_move_to_rightmost_in_subtree(cursor, child);
    }
  }

  // No previous element found - restore state
  cursor_restore_state(cursor, &saved_state);
  return false;
}

bool bt_cursor_has_next(BtCursor *cursor) {
  if (bt_cursor_next(cursor)) {
    bt_cursor_previous(cursor);
    return true;
  }
  return false;
}

bool bt_cursor_has_previous(BtCursor *cursor) {
  if (bt_cursor_previous(cursor)) {
    bt_cursor_next(cursor);
    return true;
  }
  return false;
}
