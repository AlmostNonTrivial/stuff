

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




void bp_repair_after_delete(BPlusTree &tree, BPTreeNode *node);


#define PRINT(x) std::cout << x << "\n"

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

void bp_mark_dirty(BPTreeNode *node) {

    pager_mark_dirty(node->index); }
//
int cmp(BPlusTree &tree, const uint8_t *key1, const uint8_t *key2) {
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

uint8_t *get_keys(BPTreeNode *node) {

    return node->data; }

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
    int cmp_result = cmp(tree, get_key_at(tree, node, mid), key);

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

BPTreeNode *bp_find_containing_node(BPlusTree &tree, BPTreeNode *node,
                                    const uint8_t *key, int iter = 0) {
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
      tree, bp_get_child(tree, node, child_or_key_index), key, iter + 1);
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

void bp_insert(BPlusTree &tree, BPTreeNode *node, uint8_t *key,
               const uint8_t *data) {
  if (node->is_leaf) {
    uint8_t *keys = get_keys(node);
    uint8_t *record_data = get_leaf_record_data(tree, node);

    uint32_t insert_index = bp_binary_search(tree, node, key);

    if (tree.tree_type == BPLUS && insert_index < node->num_keys &&
        cmp(tree, get_key_at(tree, node, insert_index), key) == 0) {
      bp_mark_dirty(node);
      memcpy(record_data + insert_index * tree.record_size, data,
             tree.record_size);
      return;
    }

    if (node->num_keys >= tree.leaf_max_keys) {
      bp_insert_repair(tree, node);
      bp_insert(tree, bp_find_containing_node(tree, bp_get_root(tree), key),
                key, data);
      return;
    }

    bp_mark_dirty(node);

    if (tree.tree_type == BTREE) {
      while (insert_index < node->num_keys &&
             cmp(tree, get_key_at(tree, node, insert_index), key) == 0) {
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
  } else {

    uint32_t child_index = bp_binary_search(tree, node, key);

    BPTreeNode *child_node = bp_get_child(tree, node, child_index);
    if (child_node) {
      bp_insert(tree, child_node, key, data);
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
    bp_insert(tree, root, (uint8_t *)key, data);
  }

  pager_sync();
}

bool bp_find_element(BPlusTree &tree, void *key) {
  auto containing_or_end_node =
      bp_find_containing_node(tree, bp_get_root(tree), (uint8_t *)key);

  uint32_t index =
      bp_binary_search(tree, containing_or_end_node, (uint8_t *)key);

  return index < containing_or_end_node->num_keys &&
         cmp(tree, get_key_at(tree, containing_or_end_node, index),
             (uint8_t *)key) == 0;
}

const uint8_t *bp_get(BPlusTree &tree, void *key) {
  BPTreeNode *root = bp_get_root(tree);
  BPTreeNode *leaf_node = bp_find_containing_node(tree, root, (uint8_t *)key);

  uint32_t pos = bp_binary_search(tree, leaf_node, (uint8_t *)key);
  if (pos < leaf_node->num_keys &&
      cmp(tree, get_key_at(tree, leaf_node, pos), (uint8_t *)key) == 0) {
    return get_record_at(tree, leaf_node, pos);
  }

  return nullptr;
}

BPTreeNode *bp_left_most(BPlusTree &tree) {
  BPTreeNode *temp = bp_get_root(tree);

  while (temp && !temp->is_leaf) {
    temp = bp_get_child(tree, temp, 0);
  }

  return temp;
}

static void bp_delete_internal_btree(BPlusTree &tree, BPTreeNode *node,
                                     uint32_t index) {
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

void bp_do_delete_btree(BPlusTree &tree, BPTreeNode *node, const uint8_t *key) {
  uint32_t i = bp_binary_search(tree, node, key);

  bool found_in_this_node =
      i < node->num_keys && cmp(tree, get_key_at(tree, node, i), key) == 0;

  if (found_in_this_node) {
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

      bp_delete_internal_btree(tree, node, i);
    }
  } else if (!node->is_leaf) {
    bp_do_delete_btree(tree, bp_get_child(tree, node, i), key);
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

void bp_do_delete_bplus(BPlusTree &tree, BPTreeNode *node, const uint8_t *key) {
  uint32_t i = bp_binary_search(tree, node, key);

  bool traverse_right = i == node->num_keys && !node->is_leaf;
  if (traverse_right) {
    bp_do_delete_bplus(tree, bp_get_child(tree, node, node->num_keys), key);
    return;
  }

  bool key_match = cmp(tree, get_key_at(tree, node, i), key) == 0;

  if (!node->is_leaf && key_match) {
    bp_do_delete_bplus(tree, bp_get_child(tree, node, i + 1), key);
    return;
  }

  if (!node->is_leaf) {
    bp_do_delete_bplus(tree, bp_get_child(tree, node, i), key);
    return;
  }

  if (node->is_leaf && key_match) {
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

void bp_do_delete(BPlusTree &tree, BPTreeNode *node, const uint8_t *key) {


  if (tree.tree_type == BTREE) {
    bp_do_delete_btree(tree, node, key);
  } else {
    bp_do_delete_bplus(tree, node, key);
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

void bp_delete_element(BPlusTree &tree, void *key) {
  BPTreeNode *root = bp_get_root(tree);
  if (!root) {
     return;
  }


  /* 0 or 1 key in a root && leaf node is a special case
   */
  if (root->num_keys <= 1 && root->is_leaf) {
    bp_mark_dirty(root);
    root->num_keys = 0;
    return;
  }

  bp_do_delete(tree, root, (uint8_t *)key);

  BPTreeNode *leaf = bp_find_containing_node(tree, root, (uint8_t *)key);

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
