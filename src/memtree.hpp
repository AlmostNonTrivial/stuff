// memtree.hpp
#pragma once
#include "defs.hpp"
#include "arena.hpp"
#include <cstdint>
#include <cstring>

// In-memory binary search tree for temporary storage
// Provides same cursor interface as BTree but lives entirely in QueryArena

struct MemTreeNode {
  uint8_t* key;
  uint8_t* record;
  MemTreeNode* left;
  MemTreeNode* right;
  MemTreeNode* parent;
};

struct MemTree {
  MemTreeNode* root;
  DataType key_type;
  uint32_t record_size;
  uint32_t node_count;

  // Store key/record size for allocation
  uint32_t key_size;
};

// Cursor for traversing the memory tree
struct MemCursor {
  MemTree* tree;
  MemTreeNode* current;

  enum State {
    INVALID,
    VALID,
    AT_END
  } state;
};

// ============================================================================
// Tree Creation and Management
// ============================================================================

inline MemTree memtree_create(DataType key_type, uint32_t record_size) {
  MemTree tree;
  tree.root = nullptr;
  tree.key_type = key_type;
  tree.record_size = record_size;
  tree.node_count = 0;
  tree.key_size = key_type;
  return tree;
}

inline void memtree_clear(MemTree* tree) {
  // Since we use arena allocation, we don't need to free individual nodes
  // Just reset the tree structure
  tree->root = nullptr;
  tree->node_count = 0;
}

// ============================================================================
// Internal Helper Functions
// ============================================================================

static int memtree_compare(const MemTree* tree, const uint8_t* a, const uint8_t* b) {
  return cmp(tree->key_type, a, b);
}

static MemTreeNode* memtree_create_node(MemTree* tree, const uint8_t* key, const uint8_t* record) {
  auto* node = (MemTreeNode*)arena::alloc<QueryArena>(sizeof(MemTreeNode));

  // Allocate and copy key
  node->key = (uint8_t*)arena::alloc<QueryArena>(tree->key_size);
  memcpy(node->key, key, tree->key_size);

  // Allocate and copy record if provided
  if (record && tree->record_size > 0) {
    node->record = (uint8_t*)arena::alloc<QueryArena>(tree->record_size);
    memcpy(node->record, record, tree->record_size);
  } else {
    node->record = nullptr;
  }

  node->left = nullptr;
  node->right = nullptr;
  node->parent = nullptr;

  tree->node_count++;
  return node;
}

static MemTreeNode* memtree_find_node(MemTree* tree, const uint8_t* key) {
  MemTreeNode* current = tree->root;

  while (current) {
    int cmp_result = memtree_compare(tree, key, current->key);
    if (cmp_result == 0) {
      return current;
    } else if (cmp_result < 0) {
      current = current->left;
    } else {
      current = current->right;
    }
  }

  return nullptr;
}

static MemTreeNode* memtree_find_min(MemTreeNode* node) {
  while (node && node->left) {
    node = node->left;
  }
  return node;
}

static MemTreeNode* memtree_find_max(MemTreeNode* node) {
  while (node && node->right) {
    node = node->right;
  }
  return node;
}

static MemTreeNode* memtree_successor(MemTreeNode* node) {
  if (node->right) {
    return memtree_find_min(node->right);
  }

  MemTreeNode* parent = node->parent;
  while (parent && node == parent->right) {
    node = parent;
    parent = parent->parent;
  }
  return parent;
}

static MemTreeNode* memtree_predecessor(MemTreeNode* node) {
  if (node->left) {
    return memtree_find_max(node->left);
  }

  MemTreeNode* parent = node->parent;
  while (parent && node == parent->left) {
    node = parent;
    parent = parent->parent;
  }
  return parent;
}

// ============================================================================
// Tree Operations
// ============================================================================

inline bool memtree_insert(MemTree* tree, const uint8_t* key, const uint8_t* record) {
  if (!tree->root) {
    tree->root = memtree_create_node(tree, key, record);
    return true;
  }

  MemTreeNode* current = tree->root;
  MemTreeNode* parent = nullptr;

  while (current) {
    parent = current;
    int cmp_result = memtree_compare(tree, key, current->key);

    if (cmp_result == 0) {
      // Key exists - update record
      if (record && tree->record_size > 0 && current->record) {
        memcpy(current->record, record, tree->record_size);
      }
      return true;
    } else if (cmp_result < 0) {
      current = current->left;
    } else {
      current = current->right;
    }
  }

  // Insert new node
  MemTreeNode* new_node = memtree_create_node(tree, key, record);
  new_node->parent = parent;

  if (memtree_compare(tree, key, parent->key) < 0) {
    parent->left = new_node;
  } else {
    parent->right = new_node;
  }

  return true;
}

inline bool memtree_delete(MemTree* tree, const uint8_t* key) {
  MemTreeNode* node = memtree_find_node(tree, key);
  if (!node) {
    return false;
  }

  // Case 1: Node with no children
  if (!node->left && !node->right) {
    if (node->parent) {
      if (node->parent->left == node) {
        node->parent->left = nullptr;
      } else {
        node->parent->right = nullptr;
      }
    } else {
      tree->root = nullptr;
    }
  }
  // Case 2: Node with one child
  else if (!node->left || !node->right) {
    MemTreeNode* child = node->left ? node->left : node->right;
    child->parent = node->parent;

    if (node->parent) {
      if (node->parent->left == node) {
        node->parent->left = child;
      } else {
        node->parent->right = child;
      }
    } else {
      tree->root = child;
    }
  }
  // Case 3: Node with two children
  else {
    MemTreeNode* successor = memtree_find_min(node->right);

    // Copy successor's data to node
    memcpy(node->key, successor->key, tree->key_size);
    if (node->record && successor->record) {
      memcpy(node->record, successor->record, tree->record_size);
    }

    // Delete successor (which has at most one child)
    if (successor->right) {
      successor->right->parent = successor->parent;
    }

    if (successor->parent->left == successor) {
      successor->parent->left = successor->right;
    } else {
      successor->parent->right = successor->right;
    }
  }

  tree->node_count--;
  return true;
}

// ============================================================================
// Cursor Operations - Matching BTree cursor interface
// ============================================================================

inline bool memcursor_seek(MemCursor* cursor, const void* key) {
  MemTreeNode* node = memtree_find_node(cursor->tree, (const uint8_t*)key);
  if (node) {
    cursor->current = node;
    cursor->state = MemCursor::VALID;
    return true;
  }
  cursor->state = MemCursor::INVALID;
  return false;
}

inline bool memcursor_seek_ge(MemCursor* cursor, const void* key) {
  MemTreeNode* current = cursor->tree->root;
  MemTreeNode* best = nullptr;

  while (current) {
    int cmp_result = memtree_compare(cursor->tree, (const uint8_t*)key, current->key);

    if (cmp_result == 0) {
      cursor->current = current;
      cursor->state = MemCursor::VALID;
      return true;
    } else if (cmp_result < 0) {
      best = current;  // Current node is greater than key
      current = current->left;
    } else {
      current = current->right;
    }
  }

  if (best) {
    cursor->current = best;
    cursor->state = MemCursor::VALID;
    return true;
  }

  cursor->state = MemCursor::AT_END;
  return false;
}

inline bool memcursor_seek_gt(MemCursor* cursor, const void* key) {
  MemTreeNode* current = cursor->tree->root;
  MemTreeNode* best = nullptr;

  while (current) {
    int cmp_result = memtree_compare(cursor->tree, (const uint8_t*)key, current->key);

    if (cmp_result < 0) {
      best = current;
      current = current->left;
    } else {
      current = current->right;
    }
  }

  if (best) {
    cursor->current = best;
    cursor->state = MemCursor::VALID;
    return true;
  }

  cursor->state = MemCursor::AT_END;
  return false;
}

inline bool memcursor_seek_le(MemCursor* cursor, const void* key) {
  MemTreeNode* current = cursor->tree->root;
  MemTreeNode* best = nullptr;

  while (current) {
    int cmp_result = memtree_compare(cursor->tree, (const uint8_t*)key, current->key);

    if (cmp_result == 0) {
      cursor->current = current;
      cursor->state = MemCursor::VALID;
      return true;
    } else if (cmp_result > 0) {
      best = current;  // Current node is less than key
      current = current->right;
    } else {
      current = current->left;
    }
  }

  if (best) {
    cursor->current = best;
    cursor->state = MemCursor::VALID;
    return true;
  }

  cursor->state = MemCursor::AT_END;
  return false;
}

inline bool memcursor_seek_lt(MemCursor* cursor, const void* key) {
  MemTreeNode* current = cursor->tree->root;
  MemTreeNode* best = nullptr;

  while (current) {
    int cmp_result = memtree_compare(cursor->tree, (const uint8_t*)key, current->key);

    if (cmp_result > 0) {
      best = current;
      current = current->right;
    } else {
      current = current->left;
    }
  }

  if (best) {
    cursor->current = best;
    cursor->state = MemCursor::VALID;
    return true;
  }

  cursor->state = MemCursor::AT_END;
  return false;
}

inline bool memcursor_first(MemCursor* cursor) {
  if (!cursor->tree->root) {
    cursor->state = MemCursor::AT_END;
    return false;
  }

  cursor->current = memtree_find_min(cursor->tree->root);
  cursor->state = cursor->current ? MemCursor::VALID : MemCursor::AT_END;
  return cursor->current != nullptr;
}

inline bool memcursor_last(MemCursor* cursor) {
  if (!cursor->tree->root) {
    cursor->state = MemCursor::AT_END;
    return false;
  }

  cursor->current = memtree_find_max(cursor->tree->root);
  cursor->state = cursor->current ? MemCursor::VALID : MemCursor::AT_END;
  return cursor->current != nullptr;
}

inline bool memcursor_next(MemCursor* cursor) {
  if (cursor->state != MemCursor::VALID) {
    return false;
  }

  MemTreeNode* next = memtree_successor(cursor->current);
  if (next) {
    cursor->current = next;
    return true;
  }

  cursor->state = MemCursor::AT_END;
  return false;
}

inline bool memcursor_previous(MemCursor* cursor) {
  if (cursor->state != MemCursor::VALID) {
    return false;
  }

  MemTreeNode* prev = memtree_predecessor(cursor->current);
  if (prev) {
    cursor->current = prev;
    return true;
  }

  cursor->state = MemCursor::AT_END;
  return false;
}

inline uint8_t* memcursor_key(MemCursor* cursor) {
  if (cursor->state != MemCursor::VALID) {
    return nullptr;
  }
  return cursor->current->key;
}

inline uint8_t* memcursor_record(MemCursor* cursor) {
  if (cursor->state != MemCursor::VALID) {
    return nullptr;
  }
  return cursor->current->record;
}

inline bool memcursor_is_valid(MemCursor* cursor) {
  return cursor->state == MemCursor::VALID;
}

inline bool memcursor_insert(MemCursor* cursor, const void* key, const uint8_t* record) {
  return memtree_insert(cursor->tree, (const uint8_t*)key, record);
}

inline bool memcursor_delete(MemCursor* cursor) {
  if (cursor->state != MemCursor::VALID) {
    return false;
  }

  uint8_t* key = cursor->current->key;
  MemTreeNode* next = memtree_successor(cursor->current);

  bool result = memtree_delete(cursor->tree, key);

  if (result) {
    if (next) {
      cursor->current = next;
      cursor->state = MemCursor::VALID;
    } else {
      cursor->state = MemCursor::AT_END;
    }
  }

  return result;
}

inline bool memcursor_update(MemCursor* cursor, const uint8_t* record) {
  if (cursor->state != MemCursor::VALID || !cursor->current->record) {
    return false;
  }

  memcpy(cursor->current->record, record, cursor->tree->record_size);
  return true;
}

// ============================================================================
// Utility Functions
// ============================================================================

inline uint32_t memtree_count(const MemTree* tree) {
  return tree->node_count;
}

inline bool memtree_is_empty(const MemTree* tree) {
  return tree->root == nullptr;
}
