// memtree.hpp
#pragma once
#include "defs.hpp"
#include "arena.hpp"
#include <cstdint>
#include <cstring>

// In-memory binary search tree for temporary storage
// Provides same cursor interface as BTree but lives entirely in QueryArena
// Key and record stored contiguously: [key_bytes][record_bytes]

struct MemTreeNode {
  uint8_t* data;  // Single pointer: key at offset 0, record at offset key_size
  MemTreeNode* left;
  MemTreeNode* right;
};

struct MemTree {
  MemTreeNode* root;
  DataType key_type;  // Also serves as key_size in bytes
  uint32_t record_size;
  uint32_t node_count;
  uint32_t data_size;  // Total size: key_type + record_size
};

// Stack for cursor traversal (avoiding parent pointers)
static constexpr uint32_t MAX_TREE_DEPTH = 64;

struct NodeStack {
  MemTreeNode* nodes[MAX_TREE_DEPTH];
  uint32_t depth;

  inline void clear() { depth = 0; }
  inline void push(MemTreeNode* node) { nodes[depth++] = node; }
  inline MemTreeNode* pop() { return depth > 0 ? nodes[--depth] : nullptr; }
  inline MemTreeNode* top() { return depth > 0 ? nodes[depth - 1] : nullptr; }
  inline bool empty() { return depth == 0; }
};

// Cursor for traversing the memory tree
struct MemCursor {
  MemTree* tree;
  MemTreeNode* current;
  NodeStack stack;  // Stack for traversal without parent pointers

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
  tree.data_size = key_type + record_size;
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

static inline uint8_t* node_key(MemTreeNode* node) {
  return node->data;
}

static inline uint8_t* node_record(MemTreeNode* node, uint32_t key_size) {
  return node->data + key_size;
}

static int memtree_compare(const MemTree* tree, const uint8_t* a, const uint8_t* b) {
  return cmp(tree->key_type, a, b);
}

static MemTreeNode* memtree_create_node(MemTree* tree, const uint8_t* key, const uint8_t* record) {
  auto* node = (MemTreeNode*)arena::alloc<QueryArena>(sizeof(MemTreeNode));

  // Allocate single contiguous block for key + record
  node->data = (uint8_t*)arena::alloc<QueryArena>(tree->data_size);

  // Copy key at offset 0
  memcpy(node->data, key, tree->key_type);

  // Copy record at offset key_type (if provided)
  if (record && tree->record_size > 0) {
    memcpy(node->data + tree->key_type, record, tree->record_size);
  } else if (tree->record_size > 0) {
    // Zero out record portion if no record provided
    memset(node->data + tree->key_type, 0, tree->record_size);
  }

  node->left = nullptr;
  node->right = nullptr;

  tree->node_count++;
  return node;
}

static MemTreeNode* memtree_find_node(MemTree* tree, const uint8_t* key) {
  MemTreeNode* current = tree->root;

  while (current) {
    int cmp_result = memtree_compare(tree, key, node_key(current));
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

// Find node and maintain path in stack for deletion
static MemTreeNode* memtree_find_node_with_stack(MemTree* tree, const uint8_t* key, NodeStack* stack) {
  stack->clear();
  MemTreeNode* current = tree->root;

  while (current) {
    int cmp_result = memtree_compare(tree, key, node_key(current));
    if (cmp_result == 0) {
      return current;
    }

    stack->push(current);
    if (cmp_result < 0) {
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

// Build stack to leftmost node
static void stack_push_left_path(NodeStack* stack, MemTreeNode* node) {
  while (node) {
    stack->push(node);
    node = node->left;
  }
}

// Build stack to rightmost node
static void stack_push_right_path(NodeStack* stack, MemTreeNode* node) {
  while (node) {
    stack->push(node);
    node = node->right;
  }
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
  bool went_left = false;

  while (current) {
    parent = current;
    int cmp_result = memtree_compare(tree, key, node_key(current));

    if (cmp_result == 0) {
      // Key exists - update record portion only
      if (record && tree->record_size > 0) {
        memcpy(node_record(current, tree->key_type), record, tree->record_size);
      }
      return true;
    } else if (cmp_result < 0) {
      went_left = true;
      current = current->left;
    } else {
      went_left = false;
      current = current->right;
    }
  }

  // Insert new node
  MemTreeNode* new_node = memtree_create_node(tree, key, record);

  if (went_left) {
    parent->left = new_node;
  } else {
    parent->right = new_node;
  }

  return true;
}

inline bool memtree_delete(MemTree* tree, const uint8_t* key) {
  NodeStack stack;
  MemTreeNode* node = memtree_find_node_with_stack(tree, key, &stack);
  if (!node) {
    return false;
  }

  MemTreeNode* parent = stack.top();

  // Case 1: Node with no children
  if (!node->left && !node->right) {
    if (parent) {
      if (parent->left == node) {
        parent->left = nullptr;
      } else {
        parent->right = nullptr;
      }
    } else {
      tree->root = nullptr;
    }
  }
  // Case 2: Node with one child
  else if (!node->left || !node->right) {
    MemTreeNode* child = node->left ? node->left : node->right;

    if (parent) {
      if (parent->left == node) {
        parent->left = child;
      } else {
        parent->right = child;
      }
    } else {
      tree->root = child;
    }
  }
  // Case 3: Node with two children
  else {
    // Find in-order successor (leftmost in right subtree)
    MemTreeNode* successor = node->right;
    MemTreeNode* successor_parent = node;

    while (successor->left) {
      successor_parent = successor;
      successor = successor->left;
    }

    // Copy successor's entire data block to node
    memcpy(node->data, successor->data, tree->data_size);

    // Delete successor (which has at most right child)
    if (successor_parent->left == successor) {
      successor_parent->left = successor->right;
    } else {
      successor_parent->right = successor->right;
    }
  }

  tree->node_count--;
  return true;
}

// ============================================================================
// Cursor Operations - Matching BTree cursor interface
// ============================================================================

inline bool memcursor_seek(MemCursor* cursor, const void* key) {
  cursor->stack.clear();
  MemTreeNode* current = cursor->tree->root;

  while (current) {
    int cmp_result = memtree_compare(cursor->tree, (const uint8_t*)key, node_key(current));
    if (cmp_result == 0) {
      cursor->current = current;
      cursor->state = MemCursor::VALID;
      return true;
    }

    cursor->stack.push(current);
    if (cmp_result < 0) {
      current = current->left;
    } else {
      current = current->right;
    }
  }

  cursor->state = MemCursor::INVALID;
  return false;
}

inline bool memcursor_seek_ge(MemCursor* cursor, const void* key) {
  cursor->stack.clear();
  MemTreeNode* current = cursor->tree->root;
  MemTreeNode* best = nullptr;
  NodeStack best_stack;

  while (current) {
    int cmp_result = memtree_compare(cursor->tree, (const uint8_t*)key, node_key(current));

    if (cmp_result == 0) {
      cursor->current = current;
      cursor->state = MemCursor::VALID;
      return true;
    } else if (cmp_result < 0) {
      best = current;
      best_stack = cursor->stack;
      cursor->stack.push(current);
      current = current->left;
    } else {
      cursor->stack.push(current);
      current = current->right;
    }
  }

  if (best) {
    cursor->current = best;
    cursor->stack = best_stack;
    cursor->state = MemCursor::VALID;
    return true;
  }

  cursor->state = MemCursor::AT_END;
  return false;
}

inline bool memcursor_seek_gt(MemCursor* cursor, const void* key) {
  cursor->stack.clear();
  MemTreeNode* current = cursor->tree->root;
  MemTreeNode* best = nullptr;
  NodeStack best_stack;

  while (current) {
    int cmp_result = memtree_compare(cursor->tree, (const uint8_t*)key, node_key(current));

    if (cmp_result < 0) {
      best = current;
      best_stack = cursor->stack;
      cursor->stack.push(current);
      current = current->left;
    } else {
      cursor->stack.push(current);
      current = current->right;
    }
  }

  if (best) {
    cursor->current = best;
    cursor->stack = best_stack;
    cursor->state = MemCursor::VALID;
    return true;
  }

  cursor->state = MemCursor::AT_END;
  return false;
}

inline bool memcursor_seek_le(MemCursor* cursor, const void* key) {
  cursor->stack.clear();
  MemTreeNode* current = cursor->tree->root;
  MemTreeNode* best = nullptr;
  NodeStack best_stack;

  while (current) {
    int cmp_result = memtree_compare(cursor->tree, (const uint8_t*)key, node_key(current));

    if (cmp_result == 0) {
      cursor->current = current;
      cursor->state = MemCursor::VALID;
      return true;
    } else if (cmp_result > 0) {
      best = current;
      best_stack = cursor->stack;
      cursor->stack.push(current);
      current = current->right;
    } else {
      cursor->stack.push(current);
      current = current->left;
    }
  }

  if (best) {
    cursor->current = best;
    cursor->stack = best_stack;
    cursor->state = MemCursor::VALID;
    return true;
  }

  cursor->state = MemCursor::AT_END;
  return false;
}

inline bool memcursor_seek_lt(MemCursor* cursor, const void* key) {
  cursor->stack.clear();
  MemTreeNode* current = cursor->tree->root;
  MemTreeNode* best = nullptr;
  NodeStack best_stack;

  while (current) {
    int cmp_result = memtree_compare(cursor->tree, (const uint8_t*)key, node_key(current));

    if (cmp_result > 0) {
      best = current;
      best_stack = cursor->stack;
      cursor->stack.push(current);
      current = current->right;
    } else {
      cursor->stack.push(current);
      current = current->left;
    }
  }

  if (best) {
    cursor->current = best;
    cursor->stack = best_stack;
    cursor->state = MemCursor::VALID;
    return true;
  }

  cursor->state = MemCursor::AT_END;
  return false;
}

inline bool memcursor_first(MemCursor* cursor) {
  cursor->stack.clear();

  if (!cursor->tree->root) {
    cursor->state = MemCursor::AT_END;
    return false;
  }

  stack_push_left_path(&cursor->stack, cursor->tree->root);
  cursor->current = cursor->stack.pop();
  cursor->state = cursor->current ? MemCursor::VALID : MemCursor::AT_END;
  return cursor->current != nullptr;
}

inline bool memcursor_last(MemCursor* cursor) {
  cursor->stack.clear();

  if (!cursor->tree->root) {
    cursor->state = MemCursor::AT_END;
    return false;
  }

  stack_push_right_path(&cursor->stack, cursor->tree->root);
  cursor->current = cursor->stack.pop();
  cursor->state = cursor->current ? MemCursor::VALID : MemCursor::AT_END;
  return cursor->current != nullptr;
}

inline bool memcursor_next(MemCursor* cursor) {
  if (cursor->state != MemCursor::VALID) {
    return false;
  }

  // If current has right child, go right then all the way left
  if (cursor->current->right) {
    stack_push_left_path(&cursor->stack, cursor->current->right);
    cursor->current = cursor->stack.pop();
    return true;
  }

  // Otherwise, pop from stack (parent where we went left)
  cursor->current = cursor->stack.pop();
  if (cursor->current) {
    return true;
  }

  cursor->state = MemCursor::AT_END;
  return false;
}

inline bool memcursor_previous(MemCursor* cursor) {
  if (cursor->state != MemCursor::VALID) {
    return false;
  }

  // If current has left child, go left then all the way right
  if (cursor->current->left) {
    stack_push_right_path(&cursor->stack, cursor->current->left);
    cursor->current = cursor->stack.pop();
    return true;
  }

  // Otherwise need to find ancestor where we came from the right
  // This requires rebuilding the stack from root
  NodeStack new_stack;
  MemTreeNode* target = cursor->current;
  MemTreeNode* prev = nullptr;
  MemTreeNode* current = cursor->tree->root;

  new_stack.clear();
  while (current && current != target) {
    int cmp_result = memtree_compare(cursor->tree, node_key(target), node_key(current));
    if (cmp_result < 0) {
      new_stack.push(current);
      current = current->left;
    } else {
      prev = current;  // Last node we went right from
      current = current->right;
    }
  }

  if (prev) {
    cursor->current = prev;
    cursor->stack = new_stack;
    return true;
  }

  cursor->state = MemCursor::AT_END;
  return false;
}

inline uint8_t* memcursor_key(MemCursor* cursor) {
  if (cursor->state != MemCursor::VALID) {
    return nullptr;
  }
  return node_key(cursor->current);
}

inline uint8_t* memcursor_record(MemCursor* cursor) {
  if (cursor->state != MemCursor::VALID) {
    return nullptr;
  }
  return node_record(cursor->current, cursor->tree->key_type);
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

  // Save key for deletion
  uint8_t key_copy[cursor->tree->key_type];
  memcpy(key_copy, node_key(cursor->current), cursor->tree->key_type);

  // Move to next before deleting
  memcursor_next(cursor);

  return memtree_delete(cursor->tree, key_copy);
}

inline bool memcursor_update(MemCursor* cursor, const uint8_t* record) {
  if (cursor->state != MemCursor::VALID || cursor->tree->record_size == 0) {
    return false;
  }

  memcpy(node_record(cursor->current, cursor->tree->key_type), record, cursor->tree->record_size);
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

inline bool memcursor_seek_cmp(MemCursor* cursor, const uint8_t* key, CompareOp op) {
  switch (op) {
    case GE: return memcursor_seek_ge(cursor, key);
    case GT: return memcursor_seek_gt(cursor, key);
    case LE: return memcursor_seek_le(cursor, key);
    case LT: return memcursor_seek_lt(cursor, key);
    case EQ: return memcursor_seek(cursor, key);
  }
  return false;
}
