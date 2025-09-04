// memtree.cpp - Refactored for tighter implementation
#include "memtree.hpp"
#include <cstdint>
#include <queue>
#include "defs.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <unordered_set>

// ============================================================================
// Internal Helpers - Consolidated
// ============================================================================

static inline uint8_t* node_key(MemTreeNode* node) {
    return node->data;
}

static inline uint8_t* node_record(MemTreeNode* node, uint32_t key_size) {
    return node->data + key_size;
}

// Single unified comparison function
static inline int node_compare_key(const MemTree* tree, const uint8_t* key, MemTreeNode* node) {
    return type_compare(tree->key_type, key, node->data);
}

static inline int node_compare_full(const MemTree* tree, MemTreeNode* a, MemTreeNode* b) {
    int cmp = type_compare(tree->key_type, a->data, b->data);
    if (cmp != 0 || !tree->allow_duplicates || tree->record_size == 0)
        return cmp;
    return memcmp(a->data + tree->key_size, b->data + tree->key_size, tree->record_size);
}

// Node allocation - single allocation for node + data
static MemTreeNode* alloc_node(MemTree* tree, const uint8_t* key,
                                const uint8_t* record, MemoryContext* ctx) {
    auto* node = (MemTreeNode*)ctx->alloc(sizeof(MemTreeNode));
    node->data = (uint8_t*)ctx->alloc(tree->data_size);

    memcpy(node->data, key, tree->key_size);
    if (record && tree->record_size > 0) {
        memcpy(node->data + tree->key_size, record, tree->record_size);
    } else if (tree->record_size > 0) {
        memset(node->data + tree->key_size, 0, tree->record_size);
    }

    node->left = node->right = node->parent = nullptr;
    node->color = RED;
    tree->node_count++;
    return node;
}

// Tree navigation
static MemTreeNode* tree_minimum(MemTreeNode* node) {
    while (node && node->left) node = node->left;
    return node;
}

static MemTreeNode* tree_maximum(MemTreeNode* node) {
    while (node && node->right) node = node->right;
    return node;
}

static MemTreeNode* tree_successor(MemTreeNode* node) {
    if (node->right) return tree_minimum(node->right);

    MemTreeNode* parent = node->parent;
    while (parent && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

static MemTreeNode* tree_predecessor(MemTreeNode* node) {
    if (node->left) return tree_maximum(node->left);

    MemTreeNode* parent = node->parent;
    while (parent && node == parent->left) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

// ============================================================================
// Red-Black Tree Operations - Consolidated
// ============================================================================

static void rotate_left(MemTree* tree, MemTreeNode* x) {
    MemTreeNode* y = x->right;
    x->right = y->left;
    if (y->left) y->left->parent = x;

    y->parent = x->parent;
    if (!x->parent) tree->root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;

    y->left = x;
    x->parent = y;
}

static void rotate_right(MemTree* tree, MemTreeNode* x) {
    MemTreeNode* y = x->left;
    x->left = y->right;
    if (y->right) y->right->parent = x;

    y->parent = x->parent;
    if (!x->parent) tree->root = y;
    else if (x == x->parent->right) x->parent->right = y;
    else x->parent->left = y;

    y->right = x;
    x->parent = y;
}

// Consolidated fixup using direction-based approach
static void insert_fixup(MemTree* tree, MemTreeNode* z) {
    if(!tree->rebalance) {
        return;
    }
    while (z->parent && z->parent->color == RED) {
        MemTreeNode* grandparent = z->parent->parent;
        bool parent_is_left = (z->parent == grandparent->left);

        MemTreeNode* uncle = parent_is_left ? grandparent->right : grandparent->left;

        if (uncle && uncle->color == RED) {
            // Case 1: Uncle is red
            z->parent->color = BLACK;
            uncle->color = BLACK;
            grandparent->color = RED;
            z = grandparent;
        } else {
            // Cases 2 & 3: Uncle is black
            if (parent_is_left) {
                if (z == z->parent->right) {
                    z = z->parent;
                    rotate_left(tree, z);
                }
                z->parent->color = BLACK;
                grandparent->color = RED;
                rotate_right(tree, grandparent);
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rotate_right(tree, z);
                }
                z->parent->color = BLACK;
                grandparent->color = RED;
                rotate_left(tree, grandparent);
            }
        }
    }
    tree->root->color = BLACK;
}

static void transplant(MemTree* tree, MemTreeNode* u, MemTreeNode* v) {
    if (!u->parent) tree->root = v;
    else if (u == u->parent->left) u->parent->left = v;
    else u->parent->right = v;

    if (v) v->parent = u->parent;
}

// Consolidated delete fixup
static void delete_fixup(MemTree* tree, MemTreeNode* x, MemTreeNode* x_parent) {
    if(!tree->rebalance) {
        return;
    }
    while (x != tree->root && (!x || x->color == BLACK)) {
        bool is_left = (x == x_parent->left);
        MemTreeNode* sibling = is_left ? x_parent->right : x_parent->left;

        if (sibling->color == RED) {
            sibling->color = BLACK;
            x_parent->color = RED;
            is_left ? rotate_left(tree, x_parent) : rotate_right(tree, x_parent);
            sibling = is_left ? x_parent->right : x_parent->left;
        }

        bool left_black = !sibling->left || sibling->left->color == BLACK;
        bool right_black = !sibling->right || sibling->right->color == BLACK;

        if (left_black && right_black) {
            sibling->color = RED;
            x = x_parent;
            x_parent = x->parent;
        } else {
            if (is_left) {
                if (right_black) {
                    if (sibling->left) sibling->left->color = BLACK;
                    sibling->color = RED;
                    rotate_right(tree, sibling);
                    sibling = x_parent->right;
                }
                sibling->color = x_parent->color;
                x_parent->color = BLACK;
                if (sibling->right) sibling->right->color = BLACK;
                rotate_left(tree, x_parent);
            } else {
                if (left_black) {
                    if (sibling->right) sibling->right->color = BLACK;
                    sibling->color = RED;
                    rotate_left(tree, sibling);
                    sibling = x_parent->left;
                }
                sibling->color = x_parent->color;
                x_parent->color = BLACK;
                if (sibling->left) sibling->left->color = BLACK;
                rotate_right(tree, x_parent);
            }
            x = tree->root;
        }
    }
    if (x) x->color = BLACK;
}

// Single internal delete function
static bool delete_node(MemTree* tree, MemTreeNode* z) {
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
        y = tree_minimum(z->right);
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
// Individual Seek Functions
// ============================================================================

static MemTreeNode* seek_eq(MemTree* tree, const uint8_t* key) {
    MemTreeNode* current = tree->root;
    MemTreeNode* found = nullptr;

    while (current) {
        int cmp = node_compare_key(tree, key, current);

        if (cmp == 0) {
            found = current;
            // For duplicates, find the leftmost
            if (tree->allow_duplicates) {
                current = current->left;
            } else {
                return found;
            }
        } else {
            current = (cmp < 0) ? current->left : current->right;
        }
    }

    return found;
}

static MemTreeNode* seek_ge(MemTree* tree, const uint8_t* key) {
    MemTreeNode* current = tree->root;
    MemTreeNode* best = nullptr;

    while (current) {
        int cmp = node_compare_key(tree, key, current);

        if (cmp <= 0) {  // current.key >= key
            best = current;
            current = current->left;  // Look for smaller valid node
        } else {
            current = current->right;
        }
    }

    return best;
}

static MemTreeNode* seek_gt(MemTree* tree, const uint8_t* key) {
    MemTreeNode* current = tree->root;
    MemTreeNode* best = nullptr;

    while (current) {
        int cmp = node_compare_key(tree, key, current);

        if (cmp < 0) {  // current.key > key
            best = current;
            current = current->left;  // Look for smaller valid node
        } else {
            current = current->right;
        }
    }

    return best;
}

static MemTreeNode* seek_le(MemTree* tree, const uint8_t* key) {
    MemTreeNode* current = tree->root;
    MemTreeNode* best = nullptr;

    while (current) {
        int cmp = node_compare_key(tree, key, current);

        if (cmp >= 0) {  // current.key <= key
            best = current;
            current = current->right;  // Look for larger valid node
        } else {
            current = current->left;
        }
    }

    return best;
}

static MemTreeNode* seek_lt(MemTree* tree, const uint8_t* key) {
    MemTreeNode* current = tree->root;
    MemTreeNode* best = nullptr;

    while (current) {
        int cmp = node_compare_key(tree, key, current);

        if (cmp > 0) {  // current.key < key
            best = current;
            current = current->right;  // Look for larger valid node
        } else {
            current = current->left;
        }
    }

    return best;
}

// ============================================================================
// Tree Creation and Core Operations
// ============================================================================

MemTree memtree_create(DataType key_type, uint32_t record_size, uint8_t flags) {
    MemTree tree = {};
    tree.key_type = key_type;
    tree.key_size = type_size(key_type);
    tree.record_size = record_size;
    tree.data_size = tree.key_size + record_size;
    tree.allow_duplicates = flags & 0x01; // First bit (bit 0)
    tree.rebalance = (flags & 0x02) >> 1; // Second bit (bit 1)
    return tree;
}

void memtree_clear(MemTree* tree) {
    tree->root = nullptr;
    tree->node_count = 0;
}

bool memtree_insert(MemTree* tree, const uint8_t* key, const uint8_t* record, MemoryContext* ctx) {
    // Find insertion point
    MemTreeNode *parent = nullptr, *current = tree->root;

    while (current) {
        parent = current;
        int cmp = node_compare_key(tree, key, current);

        if (cmp == 0 && !tree->allow_duplicates) {
            // Update in place
            if (record && tree->record_size > 0) {
                memcpy(node_record(current, tree->key_size), record, tree->record_size);
            }
            return true;
        }

        // For duplicates with records, use full comparison
        if (cmp == 0 && tree->allow_duplicates && tree->record_size > 0 && record) {
            int rec_cmp = memcmp(record, node_record(current, tree->key_size), tree->record_size);
            if (rec_cmp == 0) {
                // Exact duplicate - update
                memcpy(node_record(current, tree->key_size), record, tree->record_size);
                return true;
            }
            current = (rec_cmp < 0) ? current->left : current->right;
        } else {
            current = (cmp < 0) ? current->left : current->right;
        }
    }

    // Insert new node
    MemTreeNode* node = alloc_node(tree, key, record, ctx);
    node->parent = parent;

    if (!parent) {
        tree->root = node;
    } else {
        int cmp = node_compare_key(tree, key, parent);
        if (cmp == 0 && tree->allow_duplicates && tree->record_size > 0 && record) {
            // Compare records for placement
            int rec_cmp = memcmp(record, node_record(parent, tree->key_size), tree->record_size);
            (rec_cmp < 0 ? parent->left : parent->right) = node;
        } else {
            (cmp < 0 ? parent->left : parent->right) = node;
        }
    }

    insert_fixup(tree, node);
    return true;
}

bool memtree_delete(MemTree* tree, const uint8_t* key) {
    MemTreeNode* current = tree->root;

    while (current) {
        int cmp = node_compare_key(tree, key, current);
        if (cmp == 0) return delete_node(tree, current);
        current = (cmp < 0) ? current->left : current->right;
    }

    return false;
}

bool memtree_delete_exact(MemTree* tree, const uint8_t* key, const uint8_t* record) {
    MemTreeNode* current = tree->root;

    while (current) {
        int cmp = node_compare_key(tree, key, current);

        if (cmp == 0) {
            // Check record match
            if (!tree->allow_duplicates || tree->record_size == 0) {
                return delete_node(tree, current);
            }

            int rec_cmp = memcmp(record, node_record(current, tree->key_size), tree->record_size);
            if (rec_cmp == 0) return delete_node(tree, current);
            current = (rec_cmp < 0) ? current->left : current->right;
        } else {
            current = (cmp < 0) ? current->left : current->right;
        }
    }

    return false;
}

// ============================================================================
// Cursor Operations - Matching BPlusTree Interface
// ============================================================================


bool memcursor_first(MemCursor* cursor) {
    if (!cursor->tree.root) {
        cursor->state = MemCursor::AT_END;
        return false;
    }

    cursor->current = tree_minimum(cursor->tree.root);
    cursor->state = cursor->current ? MemCursor::VALID : MemCursor::AT_END;
    return cursor->current != nullptr;
}

bool memcursor_last(MemCursor* cursor) {
    if (!cursor->tree.root) {
        cursor->state = MemCursor::AT_END;
        return false;
    }

    cursor->current = tree_maximum(cursor->tree.root);
    cursor->state = cursor->current ? MemCursor::VALID : MemCursor::AT_END;
    return cursor->current != nullptr;
}

bool memcursor_next(MemCursor* cursor) {
    if (cursor->state != MemCursor::VALID) return false;

    cursor->current = tree_successor(cursor->current);
    if (cursor->current) return true;

    cursor->state = MemCursor::AT_END;
    return false;
}

bool memcursor_previous(MemCursor* cursor) {
    if (cursor->state != MemCursor::VALID) return false;

    cursor->current = tree_predecessor(cursor->current);
    if (cursor->current) return true;

    cursor->state = MemCursor::AT_END;
    return false;
}

bool memcursor_has_next(MemCursor* cursor) {
    if (cursor->state != MemCursor::VALID) return false;
    return tree_successor(cursor->current) != nullptr;
}

bool memcursor_has_previous(MemCursor* cursor) {
    if (cursor->state != MemCursor::VALID) return false;
    return tree_predecessor(cursor->current) != nullptr;
}

bool memcursor_seek(MemCursor* cursor, const void* key, CompareOp op ) {
    const uint8_t* key_bytes = (const uint8_t*)key;
    MemTreeNode* result = nullptr;

    switch (op) {
        case EQ: result = seek_eq(&cursor->tree, key_bytes); break;
        case GE: result = seek_ge(&cursor->tree, key_bytes); break;
        case GT: result = seek_gt(&cursor->tree, key_bytes); break;
        case LE: result = seek_le(&cursor->tree, key_bytes); break;
        case LT: result = seek_lt(&cursor->tree, key_bytes); break;
        default:
            cursor->state = MemCursor::INVALID;
            return false;
    }

    if (result) {
        cursor->current = result;
        cursor->state = MemCursor::VALID;
        return true;
    }

    cursor->state = (op == EQ) ? MemCursor::INVALID : MemCursor::AT_END;
    return false;
}

uint8_t* memcursor_key(MemCursor* cursor) {
    if (cursor->state != MemCursor::VALID) return nullptr;
    return node_key(cursor->current);
}

uint8_t* memcursor_record(MemCursor* cursor) {
    if (cursor->state != MemCursor::VALID) return nullptr;
    return node_record(cursor->current, cursor->tree.key_size);
}

bool memcursor_is_valid(MemCursor* cursor) {
    return cursor->state == MemCursor::VALID;
}

bool memcursor_insert(MemCursor* cursor, const void* key, const uint8_t* record) {
    return memtree_insert(&cursor->tree, (const uint8_t*)key, record, cursor->ctx);
}

bool memcursor_delete(MemCursor* cursor) {
    if (cursor->state != MemCursor::VALID) return false;

    // Save next position
    MemTreeNode* next = tree_successor(cursor->current);

    // Delete current
    bool result = delete_node(&cursor->tree, cursor->current);

    // Move to next
    if (next) {
        cursor->current = next;
        cursor->state = MemCursor::VALID;
    } else {
        cursor->state = MemCursor::AT_END;
    }

    return result;
}

bool memcursor_update(MemCursor* cursor, const uint8_t* record) {
    if (cursor->state != MemCursor::VALID || cursor->tree.record_size == 0) {
        return false;
    }

    memcpy(node_record(cursor->current, cursor->tree.key_size), record, cursor->tree.record_size);
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

    // Could add more checks here if needed
    (void)black_height; // Suppress unused warning
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
    assert(visited.find(node) == visited.end() && "Cycle detected in tree");
    visited.insert(node);

    // Verify parent pointer
    assert(node->parent == expected_parent && "Parent pointer mismatch");

    // Get node's key
    uint8_t* key = node_key(node);

    // Check BST bounds
    if (min_bound) {
        assert(node_compare_key(tree, min_bound, node) < 0 && "BST violation: node smaller than min bound");
    }
    if (max_bound) {
        assert(node_compare_key(tree, max_bound, node) > 0 && "BST violation: node larger than max bound");
    }

    // Red-Black property: Red nodes have black children
    if (node->color == RED) {
        assert(!node->left || node->left->color == BLACK);
        assert(!node->right || node->right->color == BLACK);
        // Red nodes must have a parent (root is black)
        assert(node->parent != nullptr);
        // Red node cannot have red parent
        assert(node->parent->color == BLACK);
    }

    // Recursively validate children
    int left_black_height = validate_node_recursive(tree, node->left, node,
                                                    min_bound, key, visited);
    int right_black_height = validate_node_recursive(tree, node->right, node,
                                                     key, max_bound, visited);

    // Red-Black property: All paths have same black height
    assert(left_black_height == right_black_height && "Black height mismatch");

    // Return black height including this node
    return left_black_height + (node->color == BLACK ? 1 : 0);
}

// ============================================================================
// Tree Printing - Multiple Formats
// ============================================================================

// Helper for in-order traversal
static void print_inorder_recursive(const MemTree* tree, MemTreeNode* node, bool& first) {
    if (!node) return;

    print_inorder_recursive(tree, node->left, first);

    if (!first) printf(", ");
    first = false;
    printf("[");
    type_print(tree->key_type, node_key(node));
    if (tree->record_size > 0) {
        printf(":");
        // Print first few bytes of record as hex
        uint8_t* rec = node_record(node, tree->key_size);
        for (uint32_t i = 0; i < std::min(tree->record_size, 4u); i++) {
            printf("%02x", rec[i]);
        }
        if (tree->record_size > 4) printf("...");
    }
    printf("]");

    print_inorder_recursive(tree, node->right, first);
}

// Visual tree printer (shows tree structure graphically)
static void print_tree_visual_helper(const MemTree* tree, MemTreeNode* node,
                                     const std::string& prefix, bool is_tail) {
    if (!node) return;

    printf("%s", prefix.c_str());
    printf("%s", is_tail ? "└── " : "├── ");

    // Print node
    type_print(tree->key_type, node_key(node));
    printf(" %s", node->color == RED ? "(R)" : "(B)");

    if (tree->record_size > 0) {
        printf(" rec:");
        uint8_t* rec = node_record(node, tree->key_size);
        for (uint32_t i = 0; i < std::min(tree->record_size, 4u); i++) {
            printf("%02x", rec[i]);
        }
        if (tree->record_size > 4) printf("...");
    }
    printf("\n");

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

// Main print function with BFS level-order traversal
void memtree_print(const MemTree* tree) {
    if (!tree || !tree->root) {
        printf("MemTree: EMPTY\n");
        return;
    }

    printf("====================================\n");
    printf("MemTree Structure (Red-Black Tree)\n");
    printf("====================================\n");
    printf("Key type: %s, Key size: %u bytes\n",
           type_name(tree->key_type), tree->key_size);
    printf("Record size: %u bytes\n", tree->record_size);
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
    int nodes_in_level = 0;

    printf("Level-Order Traversal:\n");
    while (!queue.empty()) {
        NodeLevel nl = queue.front();
        queue.pop();

        if (nl.level != current_level) {
            if (current_level >= 0) {
                printf(" (%d nodes)\n", nodes_in_level);
            }
            printf("Level %d: ", nl.level);
            current_level = nl.level;
            nodes_in_level = 0;
        }

        // Print node info
        if (nodes_in_level > 0) printf("  ");
        printf("[");
        type_print(tree->key_type, node_key(nl.node));
        printf("]");

        // Print color
        printf("-%c", nl.node->color == RED ? 'R' : 'B');

        nodes_in_level++;

        // Add children to queue
        if (nl.node->left) {
            queue.push({nl.node->left, nl.level + 1, true});
        }
        if (nl.node->right) {
            queue.push({nl.node->right, nl.level + 1, false});
        }
    }
    if (nodes_in_level > 0) {
        printf(" (%d nodes)\n", nodes_in_level);
    }

    printf("\n------------------------------------\n");
    printf("In-order traversal: ");
    bool first = true;
    print_inorder_recursive(tree, tree->root, first);
    printf("\n");

    // Also show visual structure
    printf("\nVisual Structure:\n");
    print_tree_visual_helper(tree, tree->root, "", true);
    printf("====================================\n\n");
}
