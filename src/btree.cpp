#include "btree.hpp"
#include "arena.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <queue>
#include <sys/types.h>

// Constants
#define NODE_HEADER_SIZE 24
#define MIN_ENTRY_COUNT 3

// B+Tree node structure - fits in a single page
struct BTreeNode {
  // Node header (24 bytes)
  uint32_t index;     // Page index
  uint32_t parent;    // Parent page index (0 if root)
  uint32_t next;      // Next sibling (for leaf nodes)
  uint32_t previous;  // Previous sibling (for leaf nodes)
  uint32_t num_keys;  // Number of keys in this node
  uint8_t is_leaf;    // 1 if leaf, 0 if internal
  uint8_t padding[3]; // Alignment padding
  // Data area - stores keys, children pointers, and data
  // Layout for internal nodes: [keys][children]
  // Layout for leaf nodes: [keys][records]
  uint8_t data[PAGE_SIZE - NODE_HEADER_SIZE]; // Rest of the page (4064 bytes)
};

static_assert(sizeof(BTreeNode) == PAGE_SIZE,
              "BTreeNode must be exactly PAGE_SIZE");

// Forward declarations
struct FindResult {
  BTreeNode *node;
  uint32_t index;
  bool found;
};

enum SeekOp { SEEK_GE, SEEK_GT, SEEK_LE, SEEK_LT };

// Internal function declarations
static void repair_after_delete(BTree &tree, BTreeNode *node);

// Helper functions
static uint32_t get_max_keys(BTree &tree, BTreeNode *node) {
  return node->is_leaf ? tree.leaf_max_keys : tree.internal_max_keys;
}

static uint32_t get_min_keys(BTree &tree, BTreeNode *node) {
  return node->is_leaf ? tree.leaf_min_keys : tree.internal_min_keys;
}

static uint32_t get_split_index(BTree &tree, BTreeNode *node) {
  return node->is_leaf ? tree.leaf_split_index : tree.internal_split_index;
}

static BTreeNode *get_root(BTree &tree) {
  return static_cast<BTreeNode *>(pager_get(tree.root_page_index));
}

static void mark_dirty(BTreeNode *node) { pager_mark_dirty(node->index); }

static uint8_t *get_keys(BTreeNode *node) { return node->data; }

static uint8_t *get_key_at(BTree &tree, BTreeNode *node, uint32_t index) {
  return get_keys(node) + index * tree.node_key_size;
}

static uint32_t *get_children(BTree &tree, BTreeNode *node) {
  if (node->is_leaf) {
    return nullptr;
  }

  if (tree.tree_type == BTREE) {
    return reinterpret_cast<uint32_t *>(
        node->data + tree.internal_max_keys * tree.node_key_size +
        tree.internal_max_keys * tree.record_size);
  }
  return reinterpret_cast<uint32_t *>(node->data + tree.internal_max_keys *
                                                       tree.node_key_size);
}

static uint8_t *get_internal_record_data(BTree &tree, BTreeNode *node) {
  return node->data + tree.internal_max_keys * tree.node_key_size;
}

static uint8_t *get_internal_record_at(BTree &tree, BTreeNode *node,
                                       uint32_t index) {
  return get_internal_record_data(tree, node) + (index * tree.record_size);
}

static uint8_t *get_leaf_record_data(BTree &tree, BTreeNode *node) {
  return node->data + tree.leaf_max_keys * tree.node_key_size;
}

static uint8_t *get_leaf_record_at(BTree &tree, BTreeNode *node,
                                   uint32_t index) {
  return get_leaf_record_data(tree, node) + (index * tree.record_size);
}

static uint8_t *get_records(BTree &tree, BTreeNode *node) {
  return node->is_leaf ? get_leaf_record_data(tree, node)
                       : get_internal_record_data(tree, node);
}

static uint8_t *get_record_at(BTree &tree, BTreeNode *node, uint32_t index) {
  if (node->is_leaf) {
    return get_leaf_record_at(tree, node, index);
  }
  return get_internal_record_at(tree, node, index);
}

static uint32_t binary_search(BTree &tree, BTreeNode *node,
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

  return left;
}

BTree btree_create(DataType key, uint32_t record_size, TreeType tree_type) {
  BTree tree = {0};
  tree.node_key_size = key;
  tree.tree_type = tree_type;
  tree.record_size = record_size;

  constexpr uint32_t USABLE_SPACE = PAGE_SIZE - NODE_HEADER_SIZE;

  if ((record_size * MIN_ENTRY_COUNT) > USABLE_SPACE) {
    std::cout <<"btree record to big\n";
    exit(1);
    tree.tree_type = INVALID;
    return tree;
  }

  if (tree_type == BTREE) {
    uint32_t key_record_size = tree.node_key_size + record_size;
    uint32_t child_ptr_size = TYPE_UINT32;

    uint32_t max_keys =
        (USABLE_SPACE - child_ptr_size) / (key_record_size + child_ptr_size);

    uint32_t min_keys;
    if (max_keys % 2 == 0) {
      min_keys = (max_keys / 2) - 1;
    } else {
      min_keys = (max_keys) / 2;
    }

    max_keys = std::max((uint32_t)MIN_ENTRY_COUNT, max_keys);

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

    tree.leaf_max_keys = std::max((uint32_t)MIN_ENTRY_COUNT, leaf_max_entries);
    tree.leaf_min_keys = tree.leaf_max_keys / 2;
    tree.leaf_split_index = tree.leaf_max_keys / 2;

    uint32_t child_ptr_size = TYPE_UINT32;
    uint32_t internal_max_entries =
        (USABLE_SPACE - child_ptr_size) / (tree.node_key_size + child_ptr_size);

    if (internal_max_entries % 2 == 0) {
      tree.internal_min_keys = (internal_max_entries / 2) - 1;
    } else {
      tree.internal_min_keys = (internal_max_entries) / 2;
    }

    tree.internal_max_keys =
        std::max((uint32_t)MIN_ENTRY_COUNT, internal_max_entries);
    tree.internal_split_index = tree.internal_max_keys / 2;
  } else {
    tree.tree_type = INVALID;
    return tree;
  }

  tree.root_page_index = 0;
  return tree;
}

static BTreeNode *create_node(BTree &tree, bool is_leaf) {
  uint32_t page_index = pager_new();
  BTreeNode *node = static_cast<BTreeNode *>(pager_get(page_index));

  node->index = page_index;
  node->parent = 0;
  node->next = 0;
  node->previous = 0;
  node->num_keys = 0;
  node->is_leaf = is_leaf ? 1 : 0;

  pager_mark_dirty(page_index);
  return node;
}

static void set_next(BTreeNode *node, uint32_t index) {
  mark_dirty(node);
  node->next = index;
}

static void set_prev(BTreeNode *node, uint32_t index) {
  mark_dirty(node);
  node->previous = index;
}

static BTreeNode *get_parent(BTreeNode *node) {
  if (!node || node->parent == 0) {
    return nullptr;
  }
  return static_cast<BTreeNode *>(pager_get(node->parent));
}

static BTreeNode *get_child(BTree &tree, BTreeNode *node, uint32_t index) {
  if (!node || node->is_leaf) {
    return nullptr;
  }
  uint32_t *children = get_children(tree, node);
  return static_cast<BTreeNode *>(pager_get(children[index]));
}

static BTreeNode *get_next(BTreeNode *node) {
  if (!node || node->next == 0) {
    return nullptr;
  }
  return static_cast<BTreeNode *>(pager_get(node->next));
}

static BTreeNode *get_prev(BTreeNode *node) {
  if (!node || node->previous == 0) {
    return nullptr;
  }
  return static_cast<BTreeNode *>(pager_get(node->previous));
}

static void set_parent(BTreeNode *node, uint32_t parent_index) {
  mark_dirty(node);
  node->parent = parent_index;

  if (parent_index != 0) {
    pager_mark_dirty(parent_index);
  }
}

static void set_child(BTree &tree, BTreeNode *node, uint32_t child_index,
                      uint32_t node_index) {
  if (!node || node->is_leaf) {
    return;
  }

  mark_dirty(node);
  uint32_t *children = get_children(tree, node);
  children[child_index] = node_index;

  if (node_index != 0) {
    BTreeNode *child_node = static_cast<BTreeNode *>(pager_get(node_index));
    if (child_node) {
      set_parent(child_node, node->index);
    }
  }
}

static void destroy_node(BTreeNode *node) {
  if (node->is_leaf) {
    if (node->previous != 0) {
      BTreeNode *prev_node = get_prev(node);
      if (prev_node) {
        set_next(prev_node, node->next);
      }
    }

    if (node->next != 0) {
      BTreeNode *next_node = get_next(node);
      if (next_node) {
        set_prev(next_node, node->previous);
      }
    }
  }

  pager_delete(node->index);
}

static bool is_match(BTree &tree, BTreeNode *node, uint32_t index,
                     const uint8_t *key) {
  return cmp(tree.node_key_size, get_key_at(tree, node, index), key) == 0;
}

static BTreeNode *split(BTree &tree, BTreeNode *node) {
  BTreeNode *right_node = create_node(tree, node->is_leaf);
  uint32_t split_index = get_split_index(tree, node);
  uint8_t *rising_key = get_key_at(tree, node, split_index);

  mark_dirty(right_node);
  mark_dirty(node);

  BTreeNode *parent = get_parent(node);
  uint32_t parent_index = 0;

  if (!parent) {
    parent = create_node(tree, false);
    mark_dirty(parent);
    tree.root_page_index = parent->index;
    set_child(tree, parent, 0, node->index);
  } else {
    mark_dirty(parent);

    uint32_t *parent_children = get_children(tree, parent);
    while (parent_children[parent_index] != node->index)
      parent_index++;

    memcpy(parent_children + parent_index + 2,
           parent_children + parent_index + 1,
           (parent->num_keys - parent_index) * TYPE_UINT32);

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
  set_child(tree, parent, parent_index + 1, right_node->index);
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
      BTreeNode *next = get_next(node);
      if (next)
        set_prev(next, right_node->index);
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
          set_child(tree, right_node, i, child);
          src_children[split_index + 1 + i] = 0;
        }
      }
    }
  }

  node->num_keys = split_index;
  return parent;
}

static void insert_repair(BTree &tree, BTreeNode *node) {
  if (node->num_keys < get_max_keys(tree, node)) {
    return;
  } else if (node->parent == 0) {
    BTreeNode *new_root = split(tree, node);
    tree.root_page_index = new_root->index;
  } else {
    BTreeNode *new_node = split(tree, node);
    insert_repair(tree, new_node);
  }
}

static bool insert(BTree &tree, BTreeNode *node, uint8_t *key,
                   const uint8_t *data) {
  if (node->is_leaf) {
    uint8_t *record_data = get_leaf_record_data(tree, node);
    uint32_t insert_index = binary_search(tree, node, key);

    if (tree.tree_type == BPLUS && insert_index < node->num_keys &&
        is_match(tree, node, insert_index, key)) {
      mark_dirty(node);
      memcpy(record_data + insert_index * tree.record_size, data,
             tree.record_size);
      return true;
    }

    if (node->num_keys >= tree.leaf_max_keys) {
      insert_repair(tree, node);
      return false;
    }

    mark_dirty(node);

    if (tree.tree_type == BTREE) {
      while (insert_index < node->num_keys &&
             is_match(tree, node, insert_index, key)) {
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
    uint32_t child_index = binary_search(tree, node, key);
    BTreeNode *child_node = get_child(tree, node, child_index);
    if (child_node) {
      return insert(tree, child_node, key, data);
    }
  }
  return false;
}

static void insert_element(BTree &tree, void *key, const uint8_t *data) {


  if (tree.root_page_index == 0) {
    BTreeNode *root = create_node(tree, true);
    tree.root_page_index = root->index;
  }

  BTreeNode *root = get_root(tree);

  if (root->num_keys == 0) {
    mark_dirty(root);
    uint8_t *keys = get_keys(root);
    uint8_t *record_data = get_leaf_record_data(tree, root);

    memcpy(keys, key, tree.node_key_size);
    memcpy(record_data, data, tree.record_size);
    root->num_keys = 1;
  } else {
    if (!insert(tree, root, static_cast<uint8_t *>(key), data)) {
      insert(tree, get_root(tree), static_cast<uint8_t *>(key), data);
    }
  }

}

static void do_delete_btree(BTree &tree, BTreeNode *node, const uint8_t *key,
                            uint32_t i) {
  if (node->is_leaf) {
    mark_dirty(node);
    uint8_t *record_data = get_leaf_record_data(tree, node);

    uint32_t shift_count = node->num_keys - i - 1;

    memcpy(get_key_at(tree, node, i), get_key_at(tree, node, i + 1),
           tree.node_key_size * shift_count);
    memcpy(record_data + i * tree.record_size,
           record_data + (i + 1) * tree.record_size,
           tree.record_size * shift_count);

    node->num_keys--;
    repair_after_delete(tree, node);
  } else {
    uint32_t index = i;
    BTreeNode *curr = get_child(tree, node, index);

    while (!curr->is_leaf) {
      curr = get_child(tree, curr, curr->num_keys);
    }

    uint32_t pred_index = curr->num_keys - 1;
    uint8_t *pred_key = get_key_at(tree, curr, pred_index);
    uint8_t *pred_record = get_record_at(tree, curr, pred_index);

    mark_dirty(node);
    memcpy(get_key_at(tree, node, index), pred_key, tree.node_key_size);
    uint8_t *internal_records = get_internal_record_data(tree, node);
    memcpy(internal_records + index * tree.record_size, pred_record,
           tree.record_size);

    mark_dirty(curr);
    uint8_t *leaf_records = get_leaf_record_data(tree, curr);

    uint32_t elements_to_shift = curr->num_keys - 1 - pred_index;
    memcpy(get_key_at(tree, curr, pred_index),
           get_key_at(tree, curr, pred_index + 1),
           elements_to_shift * tree.node_key_size);
    memcpy(leaf_records + pred_index * tree.record_size,
           leaf_records + (pred_index + 1) * tree.record_size,
           elements_to_shift * tree.record_size);

    curr->num_keys--;
    repair_after_delete(tree, curr);
  }
}

static void update_parent_keys(BTree &tree, BTreeNode *node,
                               const uint8_t *deleted_key) {
  uint8_t *next_smallest = nullptr;
  BTreeNode *parent_node = get_parent(node);

  uint32_t *parent_children = get_children(tree, parent_node);
  uint32_t parent_index;

  for (parent_index = 0; parent_children[parent_index] != node->index;
       parent_index++)
    ;

  if (node->num_keys == 0) {
    if (parent_index == parent_node->num_keys) {
      next_smallest = nullptr;
    } else {
      BTreeNode *next_sibling = get_child(tree, parent_node, parent_index + 1);
      if (next_sibling) {
        next_smallest = get_key_at(tree, next_sibling, 0);
      }
    }
  } else {
    next_smallest = get_key_at(tree, node, 0);
  }

  BTreeNode *current_parent = parent_node;
  while (current_parent) {
    if (parent_index > 0 &&
        cmp(tree.node_key_size,
            get_key_at(tree, current_parent, parent_index - 1),
            deleted_key) == 0) {
      mark_dirty(current_parent);
      if (next_smallest) {
        memcpy(get_key_at(tree, current_parent, parent_index - 1),
               next_smallest, tree.node_key_size);
      }
    }

    BTreeNode *grandparent = get_parent(current_parent);
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

static void do_delete_bplus(BTree &tree, BTreeNode *node, const uint8_t *key,
                            uint32_t i) {
  if (node->is_leaf) {
    mark_dirty(node);

    uint8_t *record_data = get_leaf_record_data(tree, node);
    uint32_t shift_count = node->num_keys - i - 1;

    memcpy(get_key_at(tree, node, i), get_key_at(tree, node, i + 1),
           tree.node_key_size * shift_count);
    memcpy(record_data + i * tree.record_size,
           record_data + (i + 1) * tree.record_size,
           tree.record_size * shift_count);

    node->num_keys--;

    if (i == 0 && node->parent != 0) {
      update_parent_keys(tree, node, key);
    }

    repair_after_delete(tree, node);
  }
}

static void do_delete(BTree &tree, BTreeNode *node, const uint8_t *key,
                      uint32_t index) {
  if (node->parent == 0 && node->num_keys <= 1 && node->is_leaf) {
    mark_dirty(node);
    node->num_keys = 0;
    return;
  }

  if (tree.tree_type == BTREE) {
    do_delete_btree(tree, node, key, index);
  } else {
    do_delete_bplus(tree, node, key, index);
  }
}

static BTreeNode *steal_from_right(BTree &tree, BTreeNode *node,
                                   uint32_t parent_index) {
  BTreeNode *parent_node = get_parent(node);
  BTreeNode *right_sibling = get_child(tree, parent_node, parent_index + 1);

  mark_dirty(node);
  mark_dirty(parent_node);
  mark_dirty(right_sibling);

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

    set_child(tree, node, node->num_keys + 1, sibling_children[0]);

    for (uint32_t i = 0; i < right_sibling->num_keys; i++) {
      set_child(tree, right_sibling, i, sibling_children[i + 1]);
    }
  }

  node->num_keys++;
  right_sibling->num_keys--;

  return parent_node;
}

static BTreeNode *merge_right(BTree &tree, BTreeNode *node) {
  BTreeNode *parent = get_parent(node);
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

  BTreeNode *right_sibling = get_child(tree, parent, node_index + 1);
  if (!right_sibling) {
    return node;
  }

  mark_dirty(node);
  mark_dirty(parent);

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
      set_next(node, right_sibling->next);
      if (right_sibling->next != 0) {
        BTreeNode *next_node = get_next(right_sibling);
        if (next_node) {
          set_prev(next_node, node->index);
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
      set_child(tree, node, node->num_keys + 1 + i, sibling_children[i]);
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

  destroy_node(right_sibling);

  if (parent->num_keys < get_min_keys(tree, parent)) {
    if (parent->parent == 0 && parent->num_keys == 0) {
      tree.root_page_index = node->index;
      set_parent(node, 0);
      destroy_node(parent);
      return node;
    } else {
      repair_after_delete(tree, parent);
    }
  }

  return node;
}

static BTreeNode *steal_from_left(BTree &tree, BTreeNode *node,
                                  uint32_t parent_index) {
  BTreeNode *parent_node = get_parent(node);
  BTreeNode *left_sibling = get_child(tree, parent_node, parent_index - 1);

  mark_dirty(node);
  mark_dirty(parent_node);
  mark_dirty(left_sibling);

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
      set_child(tree, node, i, node_children[i - 1]);
    }
    set_child(tree, node, 0, sibling_children[left_sibling->num_keys]);
  }

  node->num_keys++;
  left_sibling->num_keys--;

  return parent_node;
}

static void repair_after_delete(BTree &tree, BTreeNode *node) {
  if (node->num_keys >= get_min_keys(tree, node)) {
    return;
  }

  if (node->parent == 0) {
    if (node->num_keys == 0 && !node->is_leaf) {
      BTreeNode *only_child = get_child(tree, node, 0);
      if (only_child) {
        tree.root_page_index = only_child->index;
        set_parent(only_child, 0);
        destroy_node(node);
      }
    }
    return;
  }

  BTreeNode *parent = get_parent(node);
  uint32_t *parent_children = get_children(tree, parent);

  uint32_t node_index = 0;
  for (; node_index <= parent->num_keys; node_index++) {
    if (parent_children[node_index] == node->index) {
      break;
    }
  }

  if (node_index > 0) {
    BTreeNode *left_sibling = get_child(tree, parent, node_index - 1);
    if (left_sibling &&
        left_sibling->num_keys > get_min_keys(tree, left_sibling)) {
      steal_from_left(tree, node, node_index);
      return;
    }
  }

  if (node_index < parent->num_keys) {
    BTreeNode *right_sibling = get_child(tree, parent, node_index + 1);
    if (right_sibling &&
        right_sibling->num_keys > get_min_keys(tree, right_sibling)) {
      steal_from_right(tree, node, node_index);
      return;
    }
  }

  if (node_index < parent->num_keys) {
    merge_right(tree, node);
  } else if (node_index > 0) {
    BTreeNode *left_sibling = get_child(tree, parent, node_index - 1);
    if (left_sibling) {
      merge_right(tree, left_sibling);
    }
  }
}

/*
 * The btree module doesn't really need dynamic memory as it operates
 * on the pagers cached memory, but in this instance we don't know how
 * big the btree is going to be;
 *
 * TODO, calculate exact max memory from t
 */
struct BTreeArena;
bool btree_clear(BTree *tree) {
  arena::init<BTreeArena>(tree->internal_max_keys);
  if (tree->root_page_index == 0) {
    return false;
  }

  ArenaQueue<uint32_t, BTreeArena, 1024> bfs;
  bfs.push(tree->root_page_index);

  while (bfs.size()) {
    uint32_t index = bfs.front();
    bfs.pop();

    auto node = static_cast<BTreeNode *>(pager_get(index));
    if (!node) {
      arena::shutdown<BTreeArena>();
      return false;
    }

    if (!node->is_leaf) {
      uint32_t *children = get_children(*tree, node);
      for (uint32_t i = 0; i < node->num_keys + 1; i++) {
        bfs.push(children[i]);
      }
    }

    pager_delete(node->index);
  }

  arena::shutdown<BTreeArena>();
  return true;
}

/* ------ CURSOR ----------- */

// Internal cursor helper functions
static void cursor_push_stack(BtCursor *cursor, uint32_t page, uint32_t index,
                              uint32_t child_pos) {
  if (cursor->path.stack_depth < MAX_BTREE_DEPTH) {
    cursor->path.page_stack[cursor->path.stack_depth] = page;
    cursor->path.index_stack[cursor->path.stack_depth] = index;
    cursor->path.child_stack[cursor->path.stack_depth] = child_pos;
    cursor->path.stack_depth++;
  }
}

static FindResult find_containing_node(BTree &tree, BTreeNode *node,
                                       const uint8_t *key, BtCursor *cursor) {
  uint32_t index = binary_search(tree, node, key);
  auto stack = &cursor->path;

  if (node->is_leaf) {
    if (stack) {
      stack->page_stack[stack->stack_depth] = node->index;
      stack->index_stack[stack->stack_depth] = index;
      stack->child_stack[stack->stack_depth] = index;
      stack->stack_depth++;
    }

    return {node, index,
            (index < node->num_keys && is_match(tree, node, index, key))};
  }

  // For internal nodes in B-tree, check if key matches
  if (tree.tree_type == BTREE) {
    if (index < node->num_keys && is_match(tree, node, index, key)) {
      if (stack) {
        stack->page_stack[stack->stack_depth] = node->index;
        stack->index_stack[stack->stack_depth] = index;
        stack->child_stack[stack->stack_depth] = index;
        stack->stack_depth++;
      }
      return {node, index, true};
    }
  }

  // Need to descend to child
  uint32_t child_pos = index;

  // Add current node to stack BEFORE descending
  if (stack) {
    stack->page_stack[stack->stack_depth] = node->index;
    stack->index_stack[stack->stack_depth] =
        (child_pos > 0) ? child_pos - 1 : 0;
    stack->child_stack[stack->stack_depth] = child_pos;
    stack->stack_depth++;
  }

  BTreeNode *child = get_child(tree, node, child_pos);
  if (!child) {
    return {node, index, false};
  }

  return find_containing_node(tree, child, key, cursor);
}

static void cursor_pop_stack(BtCursor *cursor) {
  if (cursor->path.stack_depth > 0) {
    cursor->path.stack_depth--;
    cursor->path.current_page =
        cursor->path.page_stack[cursor->path.stack_depth];
    cursor->path.current_index =
        cursor->path.index_stack[cursor->path.stack_depth];
  }
}

static void cursor_clear(BtCursor *cursor) {
  cursor->path.stack_depth = 0;
  cursor->path.current_page = 0;
  cursor->path.current_index = 0;
  cursor->state = CURSOR_INVALID;
}

static void cursor_save_state(BtCursor *cursor) {
  cursor->saved = cursor->path;
}

static void cursor_restore_state(BtCursor *cursor) {
  cursor->path = cursor->saved;
}

static bool cursor_move_in_subtree(BtCursor *cursor, BTreeNode *root,
                                   bool left) {
  BTreeNode *current = root;

  while (!current->is_leaf) {
    if (left) {
      cursor_push_stack(cursor, current->index, 0, 0);
      current = get_child(*cursor->tree, current, 0);
    } else {
      uint32_t child_pos = current->num_keys;
      cursor_push_stack(cursor, current->index, current->num_keys - 1,
                        child_pos);
      current = get_child(*cursor->tree, current, child_pos);
    }

    if (!current) {
      cursor->state = CURSOR_FAULT;
      return false;
    }
  }

  if (left) {
    cursor->path.current_page = current->index;
    cursor->path.current_index = 0;
  } else {
    cursor->path.current_page = current->index;
    cursor->path.current_index = current->num_keys - 1;
  }
  cursor->state = CURSOR_VALID;
  return true;
}

static bool cursor_move_to_leftmost_in_subtree(BtCursor *cursor,
                                               BTreeNode *root) {
  return cursor_move_in_subtree(cursor, root, true);
}

static bool cursor_move_to_rightmost_in_subtree(BtCursor *cursor,
                                                BTreeNode *root) {
  return cursor_move_in_subtree(cursor, root, false);
}

static bool cursor_move_end(BtCursor *cursor, bool first) {
  cursor_clear(cursor);

  BTreeNode *root = get_root(*cursor->tree);
  if (!root || root->num_keys == 0) {
    cursor->state = CURSOR_INVALID;
    return false;
  }

  return cursor_move_in_subtree(cursor, root, first);
}

static bool cursor_seek_cmp(BtCursor *cursor, const void *key, SeekOp op) {
  bool exact_match_ok = (op == SEEK_GE || op == SEEK_LE);
  bool forward = (op == SEEK_GE || op == SEEK_GT);

  if (exact_match_ok && btree_cursor_seek(cursor, key)) {
    return true;
  } else {
    btree_cursor_seek(cursor, key);
  }

  do {
    const uint8_t *current_key = btree_cursor_key(cursor);
    if (!current_key)
      continue;

    int cmp_result = cmp(cursor->tree->node_key_size, current_key,
                         static_cast<const uint8_t *>(key));

    bool satisfied = (op == SEEK_GE && cmp_result >= 0) ||
                     (op == SEEK_GT && cmp_result > 0) ||
                     (op == SEEK_LE && cmp_result <= 0) ||
                     (op == SEEK_LT && cmp_result < 0);
    if (satisfied)
      return true;
  } while (forward ? btree_cursor_next(cursor) : btree_cursor_previous(cursor));

  return false;
}

// Public cursor functions
bool btree_cursor_is_valid(BtCursor *cursor) {
  return cursor->state == CURSOR_VALID;
}

uint8_t *btree_cursor_key(BtCursor *cursor) {
  if (cursor->state != CURSOR_VALID) {
    return nullptr;
  }

  BTreeNode *node =
      static_cast<BTreeNode *>(pager_get(cursor->path.current_page));
  if (!node || cursor->path.current_index >= node->num_keys) {
    return nullptr;
  }

  return get_key_at(*cursor->tree, node, cursor->path.current_index);
}

uint8_t *btree_cursor_record(BtCursor *cursor) {
  if (cursor->state != CURSOR_VALID) {
    return nullptr;
  }

  BTreeNode *node =
      static_cast<BTreeNode *>(pager_get(cursor->path.current_page));
  if (!node || cursor->path.current_index >= node->num_keys) {
    return nullptr;
  }

  return get_record_at(*cursor->tree, node, cursor->path.current_index);
}

bool btree_cursor_seek(BtCursor *cursor, const void *key) {
  cursor_clear(cursor);

  if(!cursor->tree->root_page_index) {
      return false;
  }

  FindResult result =
      find_containing_node(*cursor->tree, get_root(*cursor->tree),
                           static_cast<const uint8_t *>(key), cursor);

  cursor->path.current_page = result.node->index;
  cursor->path.current_index = result.index;

  if (result.found) {
    cursor->state = CURSOR_VALID;
    return true;
  }

  cursor->state =
      (result.index < result.node->num_keys) ? CURSOR_VALID : CURSOR_INVALID;
  return false;
}

bool btree_cursor_delete(BtCursor *cursor) {
  if (cursor->state != CURSOR_VALID) {
    return false;
  }

  const uint8_t *key = btree_cursor_key(cursor);
  if (!key) {
    return false;
  }

  auto node = static_cast<BTreeNode *>(pager_get(cursor->path.current_page));
  uint32_t index = node->index;

  do_delete(*cursor->tree, node, key, cursor->path.current_index);

  // After delete, cursor position depends on what happened
  node = static_cast<BTreeNode *>(pager_get(cursor->path.current_page));
  if (!node || node->index != index) {
    cursor->state = CURSOR_INVALID;
    return true;
  }

  if (cursor->path.current_index >= node->num_keys) {
    if (node->num_keys > 0) {
      cursor->path.current_index = node->num_keys - 1;
    } else {
      cursor->state = CURSOR_INVALID;
    }
  }

  return true;
}

bool btree_cursor_insert(BtCursor *cursor, const void *key,
                      const uint8_t *record) {

  insert_element(*cursor->tree, const_cast<void *>(key), record);
  return true;
}

bool btree_cursor_update(BtCursor *cursor, const uint8_t *record) {
  if (cursor->state != CURSOR_VALID) {
    return false;
  }
  pager_mark_dirty(cursor->path.current_page);
  uint8_t *data = btree_cursor_record(cursor);
  memcpy(data, record, cursor->tree->record_size);
  return true;
}

bool btree_cursor_seek_ge(BtCursor *cursor, const void *key) {
  return cursor_seek_cmp(cursor, key, SEEK_GE);
}

bool btree_cursor_seek_gt(BtCursor *cursor, const void *key) {
  return cursor_seek_cmp(cursor, key, SEEK_GT);
}

bool btree_cursor_seek_le(BtCursor *cursor, const void *key) {
  return cursor_seek_cmp(cursor, key, SEEK_LE);
}

bool btree_cursor_seek_lt(BtCursor *cursor, const void *key) {
  return cursor_seek_cmp(cursor, key, SEEK_LT);
}

bool btree_cursor_first(BtCursor *cursor) { return cursor_move_end(cursor, true); }

bool btree_cursor_last(BtCursor *cursor) { return cursor_move_end(cursor, false); }

bool btree_cursor_next(BtCursor *cursor) {
  if (cursor->state != CURSOR_VALID) {
    return false;
  }

  BTreeNode *node =
      static_cast<BTreeNode *>(pager_get(cursor->path.current_page));
  if (!node) {
    cursor->state = CURSOR_FAULT;
    return false;
  }

  cursor_save_state(cursor);

  // If leaf node in B+tree, use next pointer for efficiency
  if (cursor->tree->tree_type == BPLUS) {
    if (!node->is_leaf) {
      cursor_restore_state(cursor);
      return false;
    }

    cursor->path.current_index++;
    if (cursor->path.current_index >= node->num_keys) {
      if (node->next != 0) {
        BTreeNode *next_node = get_next(node);
        if (next_node && next_node->num_keys > 0) {
          cursor->path.current_page = next_node->index;
          cursor->path.current_index = 0;
          return true;
        }
      }

      cursor_restore_state(cursor);
      return false;
    }
    return true;
  }

  // BTREE traversal
  if (node->is_leaf) {
    cursor->path.current_index++;
    if (cursor->path.current_index < node->num_keys) {
      return true;
    }

    // Need to go up to find next key
    while (cursor->path.stack_depth > 0) {
      uint32_t child_pos =
          cursor->path.child_stack[cursor->path.stack_depth - 1];

      cursor_pop_stack(cursor);

      if (cursor->path.stack_depth == 0) {
        cursor_restore_state(cursor);
        return false;
      }

      BTreeNode *parent =
          static_cast<BTreeNode *>(pager_get(cursor->path.current_page));

      // In B-tree, after visiting left subtree (child i), visit key i
      if (child_pos <= cursor->path.current_index) {
        cursor->state = CURSOR_VALID;
        return true;
      }

      // After visiting key i, go to child i+1
      if (child_pos == cursor->path.current_index + 1 &&
          child_pos <= parent->num_keys) {
        cursor->path.current_index++;
        if (cursor->path.current_index < parent->num_keys) {
          return true;
        }
      }
    }
  } else {
    // Internal node - we need to visit right subtree
    uint32_t next_child = cursor->path.current_index + 1;
    BTreeNode *child = get_child(*cursor->tree, node, next_child);
    if (child) {
      cursor_push_stack(cursor, node->index, cursor->path.current_index,
                        next_child);
      return cursor_move_to_leftmost_in_subtree(cursor, child);
    }
  }

  cursor_restore_state(cursor);
  return false;
}

bool btree_cursor_previous(BtCursor *cursor) {
  if (cursor->state != CURSOR_VALID) {
    return false;
  }

  BTreeNode *node =
      static_cast<BTreeNode *>(pager_get(cursor->path.current_page));
  if (!node) {
    cursor->state = CURSOR_FAULT;
    return false;
  }

  cursor_save_state(cursor);

  // If leaf node in B+tree, use previous pointer for efficiency
  if (cursor->tree->tree_type == BPLUS) {
    if (!node->is_leaf) {
      cursor_restore_state(cursor);
      return false;
    }
    if (cursor->path.current_index > 0) {
      cursor->path.current_index--;
      return true;
    }

    // Move to previous leaf
    if (node->previous != 0) {
      BTreeNode *prev_node = get_prev(node);
      if (prev_node && prev_node->num_keys > 0) {
        cursor->path.current_page = prev_node->index;
        cursor->path.current_index = prev_node->num_keys - 1;
        return true;
      }
    }

    cursor_restore_state(cursor);
    return false;
  }

  // For B-tree traversal
  if (node->is_leaf) {
    if (cursor->path.current_index > 0) {
      cursor->path.current_index--;
      return true;
    }

    // Need to go up to find previous key
    while (cursor->path.stack_depth > 0) {
      uint32_t child_pos =
          cursor->path.child_stack[cursor->path.stack_depth - 1];

      cursor_pop_stack(cursor);

      if (cursor->path.stack_depth == 0) {
        cursor_restore_state(cursor);
        return false;
      }

      BTreeNode *parent =
          static_cast<BTreeNode *>(pager_get(cursor->path.current_page));
      if (!parent) {
        cursor_restore_state(cursor);
        return false;
      }

      // In B-tree, after visiting right subtree (child i+1), visit key i
      if (child_pos > 0 && child_pos == cursor->path.current_index + 1) {
        cursor->state = CURSOR_VALID;
        return true;
      }

      // If we came from child 0, need to continue up
      if (child_pos == 0) {
        continue;
      }

      // Move to previous key's right subtree
      if (cursor->path.current_index > 0) {
        cursor->path.current_index--;
        BTreeNode *child =
            get_child(*cursor->tree, parent, cursor->path.current_index + 1);
        if (child) {
          cursor_push_stack(cursor, parent->index, cursor->path.current_index,
                            cursor->path.current_index + 1);
          return cursor_move_to_rightmost_in_subtree(cursor, child);
        }
      }
    }
  } else {
    // Internal node - visit left subtree's rightmost
    BTreeNode *child =
        get_child(*cursor->tree, node, cursor->path.current_index);
    if (child) {
      cursor_push_stack(cursor, node->index, cursor->path.current_index,
                        cursor->path.current_index);
      return cursor_move_to_rightmost_in_subtree(cursor, child);
    }
  }

  cursor_restore_state(cursor);
  return false;
}

bool btree_cursor_has_next(BtCursor *cursor) {
  if (btree_cursor_next(cursor)) {
    btree_cursor_previous(cursor);
    return true;
  }
  return false;
}

bool btree_cursor_has_previous(BtCursor *cursor) {
  if (btree_cursor_previous(cursor)) {
    btree_cursor_next(cursor);
    return true;
  }
  return false;
}

// wrap pager

bool btree_init(const char*filename) {return pager_init(filename);}
void btree_begin_transaction() { pager_begin_transaction(); }
void btree_commit() { pager_commit(); }
void btree_rollback() { pager_rollback(); }
void btree_close() { pager_close(); }
