


// memtree.cpp
#include "memtree.hpp"

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

static int memtree_compare_full(const MemTree* tree, const uint8_t* a_key, const uint8_t* a_rec,
                                const uint8_t* b_key, const uint8_t* b_rec) {
    // First compare keys
    int key_cmp = cmp(tree->key_type, a_key, b_key);
    if (key_cmp != 0 || !tree->allow_duplicates) {
        return key_cmp;
    }

    // For duplicates mode, use record as secondary sort key
    if (tree->record_size > 0 && a_rec && b_rec) {
        return memcmp(a_rec, b_rec, tree->record_size);
    }

    return 0;
}

static MemTreeNode* memtree_create_node(MemTree* tree, const uint8_t* key,
                                        const uint8_t* record, MemoryContext* ctx) {
    auto* node = (MemTreeNode*)ctx->alloc(sizeof(MemTreeNode));

    // Allocate single contiguous block for key + record
    node->data = (uint8_t*)ctx->alloc(tree->data_size);

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
    node->parent = nullptr;
    node->color = RED;  // New nodes are always red

    tree->node_count++;
    return node;
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

// ============================================================================
// Red-Black Tree Rotation Functions
// ============================================================================

static void rotate_left(MemTree* tree, MemTreeNode* x) {
    MemTreeNode* y = x->right;
    x->right = y->left;

    if (y->left) {
        y->left->parent = x;
    }

    y->parent = x->parent;

    if (!x->parent) {
        tree->root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }

    y->left = x;
    x->parent = y;
}

static void rotate_right(MemTree* tree, MemTreeNode* x) {
    MemTreeNode* y = x->left;
    x->left = y->right;

    if (y->right) {
        y->right->parent = x;
    }

    y->parent = x->parent;

    if (!x->parent) {
        tree->root = y;
    } else if (x == x->parent->right) {
        x->parent->right = y;
    } else {
        x->parent->left = y;
    }

    y->right = x;
    x->parent = y;
}

// ============================================================================
// Red-Black Tree Fixup Functions
// ============================================================================

static void insert_fixup(MemTree* tree, MemTreeNode* z) {
    while (z->parent && z->parent->color == RED) {
        if (z->parent == z->parent->parent->left) {
            MemTreeNode* y = z->parent->parent->right;
            if (y && y->color == RED) {
                // Case 1: Uncle is red
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    // Case 2: z is right child
                    z = z->parent;
                    rotate_left(tree, z);
                }
                // Case 3: z is left child
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                rotate_right(tree, z->parent->parent);
            }
        } else {
            // Mirror cases for right side
            MemTreeNode* y = z->parent->parent->left;
            if (y && y->color == RED) {
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rotate_right(tree, z);
                }
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                rotate_left(tree, z->parent->parent);
            }
        }
    }
    tree->root->color = BLACK;
}

static void transplant(MemTree* tree, MemTreeNode* u, MemTreeNode* v) {
    if (!u->parent) {
        tree->root = v;
    } else if (u == u->parent->left) {
        u->parent->left = v;
    } else {
        u->parent->right = v;
    }

    if (v) {
        v->parent = u->parent;
    }
}

static void delete_fixup(MemTree* tree, MemTreeNode* x, MemTreeNode* x_parent) {
    while (x != tree->root && (!x || x->color == BLACK)) {
        if (x == x_parent->left) {
            MemTreeNode* w = x_parent->right;
            if (w->color == RED) {
                w->color = BLACK;
                x_parent->color = RED;
                rotate_left(tree, x_parent);
                w = x_parent->right;
            }
            if ((!w->left || w->left->color == BLACK) &&
                (!w->right || w->right->color == BLACK)) {
                w->color = RED;
                x = x_parent;
                x_parent = x->parent;
            } else {
                if (!w->right || w->right->color == BLACK) {
                    if (w->left) w->left->color = BLACK;
                    w->color = RED;
                    rotate_right(tree, w);
                    w = x_parent->right;
                }
                w->color = x_parent->color;
                x_parent->color = BLACK;
                if (w->right) w->right->color = BLACK;
                rotate_left(tree, x_parent);
                x = tree->root;
            }
        } else {
            MemTreeNode* w = x_parent->left;
            if (w->color == RED) {
                w->color = BLACK;
                x_parent->color = RED;
                rotate_right(tree, x_parent);
                w = x_parent->left;
            }
            if ((!w->right || w->right->color == BLACK) &&
                (!w->left || w->left->color == BLACK)) {
                w->color = RED;
                x = x_parent;
                x_parent = x->parent;
            } else {
                if (!w->left || w->left->color == BLACK) {
                    if (w->right) w->right->color = BLACK;
                    w->color = RED;
                    rotate_left(tree, w);
                    w = x_parent->left;
                }
                w->color = x_parent->color;
                x_parent->color = BLACK;
                if (w->left) w->left->color = BLACK;
                rotate_right(tree, x_parent);
                x = tree->root;
            }
        }
    }
    if (x) x->color = BLACK;
}

// ============================================================================
// Tree Creation and Management
// ============================================================================

MemTree memtree_create(DataType key_type, uint32_t record_size, bool allow_duplicates) {
    MemTree tree;
    tree.root = nullptr;
    tree.key_type = key_type;
    tree.record_size = record_size;
    tree.node_count = 0;
    tree.data_size = key_type + record_size;
    tree.allow_duplicates = allow_duplicates;
    return tree;
}

void memtree_clear(MemTree* tree) {
    // Since we use arena allocation, we don't need to free individual nodes
    // Just reset the tree structure
    tree->root = nullptr;
    tree->node_count = 0;
}

// ============================================================================
// Tree Operations
// ============================================================================

bool memtree_insert(MemTree* tree, const uint8_t* key, const uint8_t* record, MemoryContext* ctx) {
    // Create new node
    MemTreeNode* new_node = memtree_create_node(tree, key, record, ctx);

    // Standard BST insertion
    MemTreeNode* parent = nullptr;
    MemTreeNode* current = tree->root;

    while (current) {
        parent = current;
        uint8_t* curr_key = node_key(current);
        int cmp_result = memtree_compare(tree, key, curr_key);

        if (cmp_result == 0) {
            if (!tree->allow_duplicates) {
                // No duplicates mode - update in place
                if (record && tree->record_size > 0) {
                    memcpy(node_record(current, tree->key_type), record, tree->record_size);
                }
                tree->node_count--;  // Compensate for increment in create_node
                return true;
            } else {
                // Duplicates allowed - use record as secondary sort key
                if (tree->record_size > 0 && record) {
                    uint8_t* curr_rec = node_record(current, tree->key_type);
                    int rec_cmp = memcmp(record, curr_rec, tree->record_size);

                    if (rec_cmp == 0) {
                        // Exact duplicate - update in place
                        memcpy(curr_rec, record, tree->record_size);
                        tree->node_count--;  // Compensate for increment
                        return true;
                    } else if (rec_cmp < 0) {
                        current = current->left;
                    } else {
                        current = current->right;
                    }
                } else {
                    // No record to compare, insert to right
                    current = current->right;
                }
            }
        } else if (cmp_result < 0) {
            current = current->left;
        } else {
            current = current->right;
        }
    }

    // Insert the node
    new_node->parent = parent;

    if (!parent) {
        tree->root = new_node;
    } else {
        uint8_t* parent_key = node_key(parent);
        int cmp_result = memtree_compare(tree, key, parent_key);

        if (cmp_result == 0 && tree->allow_duplicates && tree->record_size > 0) {
            // Compare records for duplicate keys
            uint8_t* parent_rec = node_record(parent, tree->key_type);
            int rec_cmp = memcmp(record, parent_rec, tree->record_size);
            if (rec_cmp < 0) {
                parent->left = new_node;
            } else {
                parent->right = new_node;
            }
        } else if (cmp_result < 0) {
            parent->left = new_node;
        } else {
            parent->right = new_node;
        }
    }

    // Fix red-black properties
    insert_fixup(tree, new_node);

    return true;
}

bool memtree_delete(MemTree* tree, const uint8_t* key) {
    MemTreeNode* z = tree->root;

    // Find node to delete
    while (z) {
        int cmp_result = memtree_compare(tree, key, node_key(z));
        if (cmp_result == 0) {
            break;  // Found first matching key
        } else if (cmp_result < 0) {
            z = z->left;
        } else {
            z = z->right;
        }
    }

    if (!z) return false;

    MemTreeNode* y = z;
    MemTreeNode* x = nullptr;
    MemTreeNode* x_parent = nullptr;
    MemTreeColor y_original_color = y->color;

    if (!z->left) {
        x = z->right;
        x_parent = z->parent;
        transplant(tree, z, z->right);
    } else if (!z->right) {
        x = z->left;
        x_parent = z->parent;
        transplant(tree, z, z->left);
    } else {
        y = memtree_find_min(z->right);
        y_original_color = y->color;
        x = y->right;

        if (y->parent == z) {
            x_parent = y;
        } else {
            x_parent = y->parent;
            transplant(tree, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }

        transplant(tree, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }

    tree->node_count--;

    if (y_original_color == BLACK) {
        delete_fixup(tree, x, x_parent);
    }

    return true;
}

bool memtree_delete_exact(MemTree* tree, const uint8_t* key, const uint8_t* record) {
    MemTreeNode* z = tree->root;

    // Find exact node to delete
    while (z) {
        uint8_t* curr_key = node_key(z);
        uint8_t* curr_rec = node_record(z, tree->key_type);

        int cmp_result = memtree_compare_full(tree, key, record, curr_key, curr_rec);

        if (cmp_result == 0) {
            break;  // Found exact match
        } else if (cmp_result < 0) {
            z = z->left;
        } else {
            z = z->right;
        }
    }

    if (!z) return false;

    // Same deletion logic as memtree_delete
    MemTreeNode* y = z;
    MemTreeNode* x = nullptr;
    MemTreeNode* x_parent = nullptr;
    MemTreeColor y_original_color = y->color;

    if (!z->left) {
        x = z->right;
        x_parent = z->parent;
        transplant(tree, z, z->right);
    } else if (!z->right) {
        x = z->left;
        x_parent = z->parent;
        transplant(tree, z, z->left);
    } else {
        y = memtree_find_min(z->right);
        y_original_color = y->color;
        x = y->right;

        if (y->parent == z) {
            x_parent = y;
        } else {
            x_parent = y->parent;
            transplant(tree, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }

        transplant(tree, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }

    tree->node_count--;

    if (y_original_color == BLACK) {
        delete_fixup(tree, x, x_parent);
    }

    return true;
}

// ============================================================================
// Cursor Navigation Helpers
// ============================================================================

static MemTreeNode* tree_successor(MemTreeNode* node) {
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

static MemTreeNode* tree_predecessor(MemTreeNode* node) {
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
// Cursor Operations
// ============================================================================
/*NOCOVER_START*/
// Replace the existing memcursor_seek function with this:

bool memcursor_seek(MemCursor* cursor, const void* key) {
    MemTreeNode* current = cursor->tree->root;
    MemTreeNode* found = nullptr;

    // Standard BST search, but keep searching left when duplicates are allowed
    while (current) {
        int cmp_result = memtree_compare(cursor->tree, (const uint8_t*)key, node_key(current));
        if (cmp_result == 0) {
            found = current;
            // If duplicates allowed, keep searching left for the first occurrence
            if (cursor->tree->allow_duplicates) {
                current = current->left;
            } else {
                // No duplicates, this is the only instance
                break;
            }
        } else if (cmp_result < 0) {
            current = current->left;
        } else {
            current = current->right;
        }
    }

    if (found) {
        cursor->current = found;
        cursor->state = MemCursor::VALID;
        return true;
    }

    cursor->state = MemCursor::INVALID;
    return false;
}

// Also update memcursor_count_duplicates to be more efficient:

uint32_t memcursor_count_duplicates(MemCursor* cursor, const void* key) {
    if (!cursor->tree->allow_duplicates) {
        return memcursor_seek(cursor, key) ? 1 : 0;
    }

    uint32_t count = 0;

    // memcursor_seek now positions at the first duplicate
    if (memcursor_seek(cursor, key)) {
        do {
            uint8_t* curr_key = memcursor_key(cursor);
            if (memtree_compare(cursor->tree, (const uint8_t*)key, curr_key) != 0) {
                break;
            }
            count++;
        } while (memcursor_next(cursor));
    }

    return count;
}

bool memcursor_seek_exact(MemCursor* cursor, const void* key, const void* record) {
    MemTreeNode* current = cursor->tree->root;

    while (current) {
        uint8_t* curr_key = node_key(current);
        uint8_t* curr_rec = node_record(current, cursor->tree->key_type);

        int cmp_result = memtree_compare_full(cursor->tree,
                                              (const uint8_t*)key, (const uint8_t*)record,
                                              curr_key, curr_rec);

        if (cmp_result == 0) {
            cursor->current = current;
            cursor->state = MemCursor::VALID;
            return true;
        } else if (cmp_result < 0) {
            current = current->left;
        } else {
            current = current->right;
        }
    }

    cursor->state = MemCursor::INVALID;
    return false;
}

bool memcursor_seek_ge(MemCursor* cursor, const void* key) {
    MemTreeNode* current = cursor->tree->root;
    MemTreeNode* best = nullptr;

    while (current) {
        int cmp_result = memtree_compare(cursor->tree, (const uint8_t*)key, node_key(current));

        if (cmp_result == 0) {
            cursor->current = current;
            cursor->state = MemCursor::VALID;
            return true;
        } else if (cmp_result < 0) {
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

bool memcursor_seek_gt(MemCursor* cursor, const void* key) {
    MemTreeNode* current = cursor->tree->root;
    MemTreeNode* best = nullptr;

    while (current) {
        int cmp_result = memtree_compare(cursor->tree, (const uint8_t*)key, node_key(current));

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

bool memcursor_seek_le(MemCursor* cursor, const void* key) {
    MemTreeNode* current = cursor->tree->root;
    MemTreeNode* best = nullptr;

    while (current) {
        int cmp_result = memtree_compare(cursor->tree, (const uint8_t*)key, node_key(current));

        if (cmp_result == 0) {
            // For duplicates, find the last one with this key
            if (cursor->tree->allow_duplicates) {
                best = current;
                current = current->right;  // Keep looking for larger records with same key
            } else {
                cursor->current = current;
                cursor->state = MemCursor::VALID;
                return true;
            }
        } else if (cmp_result > 0) {
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

bool memcursor_seek_lt(MemCursor* cursor, const void* key) {
    MemTreeNode* current = cursor->tree->root;
    MemTreeNode* best = nullptr;

    while (current) {
        int cmp_result = memtree_compare(cursor->tree, (const uint8_t*)key, node_key(current));

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

bool memcursor_first(MemCursor* cursor) {
    if (!cursor->tree->root) {
        cursor->state = MemCursor::AT_END;
        return false;
    }

    cursor->current = memtree_find_min(cursor->tree->root);
    cursor->state = cursor->current ? MemCursor::VALID : MemCursor::AT_END;
    return cursor->current != nullptr;
}

bool memcursor_last(MemCursor* cursor) {
    if (!cursor->tree->root) {
        cursor->state = MemCursor::AT_END;
        return false;
    }

    cursor->current = memtree_find_max(cursor->tree->root);
    cursor->state = cursor->current ? MemCursor::VALID : MemCursor::AT_END;
    return cursor->current != nullptr;
}

bool memcursor_next(MemCursor* cursor) {
    if (cursor->state != MemCursor::VALID) {
        return false;
    }

    cursor->current = tree_successor(cursor->current);

    if (cursor->current) {
        return true;
    }

    cursor->state = MemCursor::AT_END;
    return false;
}

bool memcursor_previous(MemCursor* cursor) {
    if (cursor->state != MemCursor::VALID) {
        return false;
    }

    cursor->current = tree_predecessor(cursor->current);

    if (cursor->current) {
        return true;
    }

    cursor->state = MemCursor::AT_END;
    return false;
}

uint8_t* memcursor_key(MemCursor* cursor) {
    if (cursor->state != MemCursor::VALID) {
        return nullptr;
    }
    return node_key(cursor->current);
}

uint8_t* memcursor_record(MemCursor* cursor) {
    if (cursor->state != MemCursor::VALID) {
        return nullptr;
    }
    return node_record(cursor->current, cursor->tree->key_type);
}

bool memcursor_is_valid(MemCursor* cursor) {
    return cursor->state == MemCursor::VALID;
}

bool memcursor_insert(MemCursor* cursor, const void* key, const uint8_t* record) {
    return memtree_insert(cursor->tree, (const uint8_t*)key, record, cursor->ctx);
}

bool memcursor_delete(MemCursor* cursor) {
    if (cursor->state != MemCursor::VALID) {
        return false;
    }

    // Save key (and record if duplicates allowed) for deletion
    uint8_t data_copy[cursor->tree->data_size];
    memcpy(data_copy, cursor->current->data, cursor->tree->data_size);

    // Move to next before deleting
    memcursor_next(cursor);

    if (cursor->tree->allow_duplicates) {
        // Delete exact match
        return memtree_delete_exact(cursor->tree, data_copy,
                                   data_copy + cursor->tree->key_type);
    } else {
        // Delete by key only
        return memtree_delete(cursor->tree, data_copy);
    }
}

bool memcursor_update(MemCursor* cursor, const uint8_t* record) {
    if (cursor->state != MemCursor::VALID || cursor->tree->record_size == 0) {
        return false;
    }

    memcpy(node_record(cursor->current, cursor->tree->key_type), record, cursor->tree->record_size);
    return true;
}

// ============================================================================
// Utility Functions
// ============================================================================

uint32_t memtree_count(const MemTree* tree) {
    return tree->node_count;
}

bool memtree_is_empty(const MemTree* tree) {
    return tree->root == nullptr;
}

bool memcursor_seek_cmp(MemCursor* cursor, const uint8_t* key, CompareOp op) {
    switch (op) {
        case GE: return memcursor_seek_ge(cursor, key);
        case GT: return memcursor_seek_gt(cursor, key);
        case LE: return memcursor_seek_le(cursor, key);
        case LT: return memcursor_seek_lt(cursor, key);
        case EQ: return memcursor_seek(cursor, key);
        default: return false;
    }
}

bool memcursor_has_duplicates(MemCursor* cursor) {
    if (cursor->state != MemCursor::VALID || !cursor->tree->allow_duplicates) {
        return false;
    }

    // Save current position
    MemTreeNode* saved = cursor->current;

    // Check next entry
    bool has_dup = false;
    if (memcursor_next(cursor)) {
        uint8_t* curr_key = node_key(saved);
        uint8_t* next_key = memcursor_key(cursor);
        has_dup = (memtree_compare(cursor->tree, curr_key, next_key) == 0);
    }

    // Restore position
    cursor->current = saved;
    cursor->state = MemCursor::VALID;

    return has_dup;
}

// Add these functions to memtree.cpp

#include <queue>
#include <unordered_set>
#include <iomanip>
#include <cassert>

// ============================================================================
// Tree Validation
// ============================================================================

// Forward declaration for recursive validation
static int validate_node_recursive(const MemTree* tree, MemTreeNode* node,
                                   MemTreeNode* expected_parent,
                                   const uint8_t* min_bound,
                                   const uint8_t* max_bound,
                                   std::unordered_set<MemTreeNode*>& visited);

void memtree_validate(const MemTree* tree) {
    assert(tree != nullptr);

    // Empty tree is valid
    if (!tree->root) {
        assert(tree->node_count == 0);
        return;
    }

    // Root must be black
    assert(tree->root->color == BLACK);
    assert(tree->root->parent == nullptr);

    // Track visited nodes to detect cycles
    std::unordered_set<MemTreeNode*> visited;

    // Validate recursively and get black height
    int black_height = validate_node_recursive(tree, tree->root, nullptr,
                                               nullptr, nullptr, visited);

    // Verify node count matches
    assert(visited.size() == tree->node_count);
}

static int validate_node_recursive(const MemTree* tree, MemTreeNode* node,
                                   MemTreeNode* expected_parent,
                                   const uint8_t* min_bound,
                                   const uint8_t* max_bound,
                                   std::unordered_set<MemTreeNode*>& visited) {
    if (!node) {
        return 0; // NIL nodes are black by definition
    }

    // Check for cycles
    assert(visited.find(node) == visited.end());
    visited.insert(node);

    // Verify parent pointer
    assert(node->parent == expected_parent);

    // Get node's key
    uint8_t* key = node_key(node);

    // Check bounds
    if (min_bound) {
        assert(memtree_compare(tree, key, min_bound) > 0);
    }
    if (max_bound) {
        assert(memtree_compare(tree, key, max_bound) < 0);
    }

    // Red-Black property: Red nodes have black children
    if (node->color == RED) {
        assert(!node->left || node->left->color == BLACK);
        assert(!node->right || node->right->color == BLACK);
        // Red nodes must have a parent (root is black)
        assert(node->parent != nullptr);
    }

    // Recursively validate children
    int left_black_height = validate_node_recursive(tree, node->left, node,
                                                    min_bound, key, visited);
    int right_black_height = validate_node_recursive(tree, node->right, node,
                                                     key, max_bound, visited);

    // Red-Black property: All paths have same black height
    assert(left_black_height == right_black_height);

    // Return black height including this node
    return left_black_height + (node->color == BLACK ? 1 : 0);
}

// ============================================================================
// Tree Printing
// ============================================================================

// Forward declaration
static void print_inorder(const MemTree* tree, MemTreeNode* node);

static void print_key_value(const MemTree* tree, uint8_t* key, uint8_t* record) {
    // Print key
    switch (tree->key_type) {
        case TYPE_4: {
            uint32_t val;
            memcpy(&val, key, 4);
            printf("%u", val);
            break;
        }
        case TYPE_8: {
            uint64_t val;
            memcpy(&val, key, 8);
            printf("%lu", val);
            break;
        }
        case TYPE_32:
        case TYPE_256: {
            printf("\"");
            for (uint32_t i = 0; i < tree->key_type && key[i]; i++) {
                if (key[i] >= 32 && key[i] < 127) {
                    printf("%c", key[i]);
                } else {
                    printf("\\x%02x", key[i]);
                }
                if (i > 10) {
                    printf("...");
                    break;
                }
            }
            printf("\"");
            break;
        }
        default:
            printf("?");
    }

    // Print record if exists
    if (tree->record_size > 0 && record) {
        printf(":");
        if (tree->record_size == sizeof(uint32_t)) {
            uint32_t val;
            memcpy(&val, record, sizeof(uint32_t));
            printf("%u", val);
        } else if (tree->record_size == sizeof(uint64_t)) {
            uint64_t val;
            memcpy(&val, record, sizeof(uint64_t));
            printf("%lu", val);
        } else {
            printf("<%u bytes>", tree->record_size);
        }
    }
}

void memtree_print(const MemTree* tree) {
    if (!tree || !tree->root) {
        printf("MemTree: EMPTY\n");
        return;
    }

    printf("====================================\n");
    printf("MemTree Structure (Red-Black Tree)\n");
    printf("====================================\n");
    printf("Key type: %s, Record size: %u bytes\n",
           type_to_string(tree->key_type), tree->record_size);
    printf("Allow duplicates: %s\n", tree->allow_duplicates ? "YES" : "NO");
    printf("Node count: %u\n", tree->node_count);
    printf("------------------------------------\n\n");

    // BFS traversal with level tracking
    struct NodeLevel {
        MemTreeNode* node;
        int level;
        bool is_left;  // Is this a left child of its parent?
    };

    std::queue<NodeLevel> queue;
    queue.push({tree->root, 0, false});

    int current_level = -1;

    while (!queue.empty()) {
        NodeLevel nl = queue.front();
        queue.pop();

        if (nl.level != current_level) {
            if (current_level >= 0) printf("\n");
            printf("LEVEL %d:\n", nl.level);
            current_level = nl.level;
        }

        // Print node info
        printf("  [");
        print_key_value(tree, node_key(nl.node),
                       node_record(nl.node, tree->key_type));
        printf("]");

        // Print color
        printf(" %s", nl.node->color == RED ? "RED" : "BLK");

        // Print parent info
        if (nl.node->parent) {
            printf(" (parent: ");
            print_key_value(tree, node_key(nl.node->parent), nullptr);
            printf(", %s child)", nl.is_left ? "L" : "R");
        } else {
            printf(" (ROOT)");
        }

        printf("\n");

        // Add children to queue
        if (nl.node->left) {
            queue.push({nl.node->left, nl.level + 1, true});
        }
        if (nl.node->right) {
            queue.push({nl.node->right, nl.level + 1, false});
        }
    }

    printf("\n====================================\n");

    // Print in-order traversal for verification
    printf("In-order traversal: ");
    print_inorder(tree, tree->root);
    printf("\n====================================\n\n");
}

// Helper for in-order traversal
static void print_inorder(const MemTree* tree, MemTreeNode* node) {
    if (!node) return;

    print_inorder(tree, node->left);

    print_key_value(tree, node_key(node),
                   node_record(node, tree->key_type));
    printf(" ");

    print_inorder(tree, node->right);
}

// Compact tree printer
void memtree_print_compact(const MemTree* tree) {
    if (!tree || !tree->root) {
        printf("MemTree: EMPTY\n");
        return;
    }

    printf("MemTree (key:color:parent): ");

    std::queue<MemTreeNode*> queue;
    std::queue<int> levels;

    queue.push(tree->root);
    levels.push(0);

    int current_level = 0;

    while (!queue.empty()) {
        MemTreeNode* node = queue.front();
        int level = levels.front();
        queue.pop();
        levels.pop();

        if (level != current_level) {
            printf("\n");
            current_level = level;
        }

        printf("[");
        print_key_value(tree, node_key(node), nullptr);
        printf(":%c", node->color == RED ? 'R' : 'B');
        if (node->parent) {
            printf(":");
            print_key_value(tree, node_key(node->parent), nullptr);
        }
        printf("] ");

        if (node->left) {
            queue.push(node->left);
            levels.push(level + 1);
        }
        if (node->right) {
            queue.push(node->right);
            levels.push(level + 1);
        }
    }
    printf("\n");
}

// Visual tree printer (shows tree structure)
static void print_tree_visual_helper(const MemTree* tree, MemTreeNode* node,
                                     const std::string& prefix, bool is_tail) {
    if (!node) return;

    printf("%s", prefix.c_str());
    printf("%s", is_tail ? "└── " : "├── ");

    // Print node
    print_key_value(tree, node_key(node), nullptr);
    printf(" %s\n", node->color == RED ? "(R)" : "(B)");

    // Print children
    std::string child_prefix = prefix + (is_tail ? "    " : "│   ");

    if (node->left || node->right) {
        if (node->right) {
            print_tree_visual_helper(tree, node->right, child_prefix, false);
        }
        if (node->left) {
            print_tree_visual_helper(tree, node->left, child_prefix, true);
        }
    }
}

void memtree_print_visual(const MemTree* tree) {
    if (!tree || !tree->root) {
        printf("MemTree: EMPTY\n");
        return;
    }

    printf("MemTree Visual:\n");
    print_tree_visual_helper(tree, tree->root, "", true);
}


/*NOCOVER_END*/