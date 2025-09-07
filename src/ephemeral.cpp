/*
** 2024 SQL-FromScratch
**
** OVERVIEW
**
** This file implements a Red-Black tree data structure optimized for
** temporary in-memory storage. Red-Black trees provide O(log n) operations
** while maintaining balance through color-based constraints.
**
** KEY CONCEPTS
**
** Node Structure: Each node contains a key-value pair stored contiguously
** in memory for cache efficiency. Nodes are colored either RED or BLACK
** to maintain balance properties.
**
** Balance Invariants: The tree maintains:
**   1. Root is always BLACK
**   2. RED nodes cannot have RED children
**   3. All paths from root to leaves contain the same number of BLACK nodes
**   4. New nodes start as RED to minimize rotations
**
** Memory Layout: Node structure and data are allocated in a single block:
**   [node_struct][key_bytes][record_bytes]
** This improves cache locality and reduces allocation overhead.
**
** Rebalance Flag: Unique feature allowing the tree to operate as either:
**   - Balanced Red-Black tree (rebalance=true): Guarantees O(log n)
**   - Unbalanced BST (rebalance=false): Faster inserts, degraded search
** This is useful for workloads where data is inserted once then queried.
**
** IMPLEMENTATION NOTES
**
** Memory Management: All nodes allocated through asdasdas, enabling
** bulk deallocation and transaction support. The tree itself maintains
** no memory ownership - the context handles lifecycle.
**
** Duplicate Handling: When duplicates are allowed, the tree uses full
** key+record comparison to maintain strict ordering. This ensures
** identical entries are truly identical.
*/

#include "ephemeral.hpp"
#include <cstdint>
#include <queue>
#include "common.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include "arena.hpp"


// ============================================================================
// NODE ACCESS MACROS
// ============================================================================

/*
** Node data accessors
**
** These macros provide zero-cost abstractions for accessing node data.
** The key is always at the start of the data area, with the record
** immediately following.
*/
#define GET_KEY(node)         ((node)->data)
#define GET_RECORD(node, tree) ((node)->data + (tree)->key_size)

/*
** Node color predicates
**
** Explicit macros for color checking improve readability of the
** Red-Black tree algorithms.
*/
#define IS_RED(node)   ((node) && (node)->color == RED)
#define IS_BLACK(node) (!(node) || (node)->color == BLACK)

/*
** Node relationship predicates
*/
#define IS_LEFT_CHILD(node)  ((node)->parent && (node) == (node)->parent->left)
#define IS_RIGHT_CHILD(node) ((node)->parent && (node) == (node)->parent->right)
#define IS_ROOT(node)        ((node)->parent == nullptr)

// ============================================================================
// Internal Helpers
// ============================================================================

/*
** Compare a search key against a node's key.
**
** Returns: <0 if key < node_key, 0 if equal, >0 if key > node_key
*/
static inline int
node_compare_key(const ephemeral_tree *tree, void *key, ephemeral_tree_node *node)
{
	return type_compare(tree->key_type, key, GET_KEY(node));
}

/*
** Full comparison between two nodes including record data.
**
** Used when duplicates are allowed to maintain strict ordering.
** Falls back to key-only comparison when duplicates are disabled
** or nodes have no records.
*/
static inline int
node_compare_full(const ephemeral_tree *tree, ephemeral_tree_node *a, ephemeral_tree_node *b)
{
	int cmp = type_compare(tree->key_type, GET_KEY(a), GET_KEY(b));
	if (cmp != 0 || !tree->allow_duplicates || tree->record_size == 0)
	{
		return cmp;
	}
	return memcmp(GET_RECORD(a, tree), GET_RECORD(b, tree), tree->record_size);
}

/*
** Allocate and initialize a new node.
**
** Performs single allocation for both node structure and data,
** improving cache locality. The data area immediately follows
** the node structure in memory.
*/
static ephemeral_tree_node *
alloc_node(ephemeral_tree *tree, void *key, void *record)
{
	size_t total_size = sizeof(ephemeral_tree_node) + tree->data_size;
	auto  *node = (ephemeral_tree_node *)arena<query_arena>::alloc(total_size);
	node->data = (uint8_t *)(node + 1); // Data follows immediately after node

	memcpy(GET_KEY(node), key, tree->key_size);
	if (record && tree->record_size > 0)
	{
		memcpy(GET_RECORD(node, tree), record, tree->record_size);
	}
	else if (tree->record_size > 0)
	{
		memset(GET_RECORD(node, tree), 0, tree->record_size);
	}

	node->left = node->right = node->parent = nullptr;
	node->color = RED;  // New nodes start RED to minimize rotations
	tree->node_count++;
	return node;
}

// ============================================================================
// Tree Navigation
// ============================================================================

/*
** Find the minimum (leftmost) node in a subtree.
**
** Used for finding successors and the first node in traversal.
*/
static ephemeral_tree_node *
tree_minimum(ephemeral_tree_node *node)
{
	while (node && node->left)
	{
		node = node->left;
	}
	return node;
}

/*
** Find the maximum (rightmost) node in a subtree.
**
** Used for finding predecessors and the last node in traversal.
*/
static ephemeral_tree_node *
tree_maximum(ephemeral_tree_node *node)
{
	while (node && node->right)
	{
		node = node->right;
	}
	return node;
}

/*
** Find the in-order successor of a node.
**
** If the node has a right subtree, successor is its minimum.
** Otherwise, successor is the first ancestor where node is in left subtree.
*/
static ephemeral_tree_node *
tree_successor(ephemeral_tree_node *node)
{
	if (node->right)
	{
		return tree_minimum(node->right);
	}

	ephemeral_tree_node *parent = node->parent;
	while (parent && node == parent->right)
	{
		node = parent;
		parent = parent->parent;
	}
	return parent;
}

/*
** Find the in-order predecessor of a node.
**
** Mirror operation of tree_successor.
*/
static ephemeral_tree_node *
tree_predecessor(ephemeral_tree_node *node)
{
	if (node->left)
	{
		return tree_maximum(node->left);
	}

	ephemeral_tree_node *parent = node->parent;
	while (parent && node == parent->left)
	{
		node = parent;
		parent = parent->parent;
	}
	return parent;
}

// ============================================================================
// Red-Black Tree Rotations
// ============================================================================

/*
** Left rotation around node x.
**
**     x                y
**    / \              / \
**   a   y    ==>     x   c
**      / \          / \
**     b   c        a   b
**
** Preserves BST property while rebalancing the tree.
*/
static void
rotate_left(ephemeral_tree *tree, ephemeral_tree_node *x)
{
	ephemeral_tree_node *y = x->right;
	x->right = y->left;
	if (y->left)
	{
		y->left->parent = x;
	}

	y->parent = x->parent;
	if (!x->parent)
	{
		tree->root = y;
	}
	else if (x == x->parent->left)
	{
		x->parent->left = y;
	}
	else
	{
		x->parent->right = y;
	}

	y->left = x;
	x->parent = y;
}

/*
** Right rotation around node x.
**
** Mirror operation of rotate_left.
*/
static void
rotate_right(ephemeral_tree *tree, ephemeral_tree_node *x)
{
	ephemeral_tree_node *y = x->left;
	x->left = y->right;
	if (y->right)
	{
		y->right->parent = x;
	}

	y->parent = x->parent;
	if (!x->parent)
	{
		tree->root = y;
	}
	else if (x == x->parent->right)
	{
		x->parent->right = y;
	}
	else
	{
		x->parent->left = y;
	}

	y->right = x;
	x->parent = y;
}

/*
** Fix Red-Black tree violations after insertion.
**
** New RED nodes may violate the no-red-red-child rule.
** This function restores the invariants through recoloring
** and rotations, working up from the inserted node.
**
** Cases handled:
**   1. Uncle is RED: Recolor parent, uncle, grandparent
**   2. Uncle is BLACK, node is "inside": Rotate to outside
**   3. Uncle is BLACK, node is "outside": Rotate and recolor
*/
static void
insert_fixup(ephemeral_tree *tree, ephemeral_tree_node *z)
{
	if (!tree->rebalance)
	{
		return;  // Skip balancing if disabled
	}

	while (z->parent && IS_RED(z->parent))
	{
		ephemeral_tree_node *grandparent = z->parent->parent;
		bool parent_is_left = (z->parent == grandparent->left);

		ephemeral_tree_node *uncle = parent_is_left ? grandparent->right : grandparent->left;

		if (IS_RED(uncle))
		{
			// Case 1: Uncle is red - recolor and continue up
			z->parent->color = BLACK;
			uncle->color = BLACK;
			grandparent->color = RED;
			z = grandparent;
		}
		else
		{
			// Cases 2 & 3: Uncle is black - rotate and recolor
			if (parent_is_left)
			{
				if (IS_RIGHT_CHILD(z))
				{
					// Case 2: Left-Right - rotate to Left-Left
					z = z->parent;
					rotate_left(tree, z);
				}
				// Case 3: Left-Left - rotate and recolor
				z->parent->color = BLACK;
				grandparent->color = RED;
				rotate_right(tree, grandparent);
			}
			else
			{
				if (IS_LEFT_CHILD(z))
				{
					// Case 2: Right-Left - rotate to Right-Right
					z = z->parent;
					rotate_right(tree, z);
				}
				// Case 3: Right-Right - rotate and recolor
				z->parent->color = BLACK;
				grandparent->color = RED;
				rotate_left(tree, grandparent);
			}
		}
	}
	tree->root->color = BLACK;  // Root must always be black
}

/*
** Replace subtree rooted at u with subtree rooted at v.
**
** Helper for deletion - maintains parent pointers correctly.
*/
static void
transplant(ephemeral_tree *tree, ephemeral_tree_node *u, ephemeral_tree_node *v)
{
	if (!u->parent)
	{
		tree->root = v;
	}
	else if (IS_LEFT_CHILD(u))
	{
		u->parent->left = v;
	}
	else
	{
		u->parent->right = v;
	}

	if (v)
	{
		v->parent = u->parent;
	}
}

/*
** Fix Red-Black tree violations after deletion.
**
** Deletion of BLACK nodes may violate the black-height property.
** This function restores balance through rotations and recoloring.
**
** The algorithm handles four symmetric cases based on the
** relationship between the node and its sibling.
*/
static void
delete_fixup(ephemeral_tree *tree, ephemeral_tree_node *x, ephemeral_tree_node *x_parent)
{
	if (!tree->rebalance)
	{
		return;  // Skip balancing if disabled
	}

	while (x != tree->root && IS_BLACK(x))
	{
		bool is_left = (x == x_parent->left);
		ephemeral_tree_node *sibling = is_left ? x_parent->right : x_parent->left;

		if (IS_RED(sibling))
		{
			// Case 1: Sibling is red - rotate and recolor
			sibling->color = BLACK;
			x_parent->color = RED;
			if (is_left)
			{
				rotate_left(tree, x_parent);
			}
			else
			{
				rotate_right(tree, x_parent);
			}
			sibling = is_left ? x_parent->right : x_parent->left;
		}

		bool left_black = IS_BLACK(sibling->left);
		bool right_black = IS_BLACK(sibling->right);

		if (left_black && right_black)
		{
			// Case 2: Both sibling's children are black
			sibling->color = RED;
			x = x_parent;
			x_parent = x->parent;
		}
		else
		{
			if (is_left)
			{
				if (right_black)
				{
					// Case 3: Sibling's far child is black
					if (sibling->left)
					{
						sibling->left->color = BLACK;
					}
					sibling->color = RED;
					rotate_right(tree, sibling);
					sibling = x_parent->right;
				}
				// Case 4: Sibling's far child is red
				sibling->color = x_parent->color;
				x_parent->color = BLACK;
				if (sibling->right)
				{
					sibling->right->color = BLACK;
				}
				rotate_left(tree, x_parent);
			}
			else
			{
				if (left_black)
				{
					// Case 3: Sibling's far child is black
					if (sibling->right)
					{
						sibling->right->color = BLACK;
					}
					sibling->color = RED;
					rotate_left(tree, sibling);
					sibling = x_parent->left;
				}
				// Case 4: Sibling's far child is red
				sibling->color = x_parent->color;
				x_parent->color = BLACK;
				if (sibling->left)
				{
					sibling->left->color = BLACK;
				}
				rotate_right(tree, x_parent);
			}
			x = tree->root;  // Force loop termination
		}
	}
	if (x)
	{
		x->color = BLACK;
	}
}

/*
** Delete a node from the tree.
**
** Handles three cases:
**   1. Node has no children: Simply remove
**   2. Node has one child: Replace with child
**   3. Node has two children: Replace with successor, delete successor
**
** The fixup is needed when a BLACK node is deleted.
*/
static bool
delete_node(ephemeral_tree *tree, ephemeral_tree_node *z)
{
	if (!z)
	{
		return false;
	}

	ephemeral_tree_node *y = z;
	ephemeral_tree_node *x = nullptr;
	ephemeral_tree_node *x_parent = nullptr;
	TreeColor y_original_color = y->color;

	if (!z->left)
	{
		// Case 1: No left child
		x = z->right;
		x_parent = z->parent;
		transplant(tree, z, z->right);
	}
	else if (!z->right)
	{
		// Case 2: No right child
		x = z->left;
		x_parent = z->parent;
		transplant(tree, z, z->left);
	}
	else
	{
		// Case 3: Two children - replace with successor
		y = tree_minimum(z->right);
		y_original_color = y->color;
		x = y->right;

		if (y->parent == z)
		{
			x_parent = y;
		}
		else
		{
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

	if (y_original_color == BLACK)
	{
		delete_fixup(tree, x, x_parent);
	}

	return true;
}

// ============================================================================
// Seek Operations
// ============================================================================

/*
** Find node with exact key match.
**
** For duplicates, returns the leftmost matching node.
*/
static ephemeral_tree_node *
seek_eq(ephemeral_tree *tree, void *key)
{
	ephemeral_tree_node *current = tree->root;
	ephemeral_tree_node *found = nullptr;

	while (current)
	{
		int cmp = node_compare_key(tree, key, current);

		if (cmp == 0)
		{
			found = current;
			// For duplicates, find the leftmost
			if (tree->allow_duplicates)
			{
				current = current->left;
			}
			else
			{
				return found;
			}
		}
		else
		{
			current = (cmp < 0) ? current->left : current->right;
		}
	}

	return found;
}

/*
** Find smallest node with key >= target.
*/
static ephemeral_tree_node *
seek_ge(ephemeral_tree *tree, void *key)
{
	ephemeral_tree_node *current = tree->root;
	ephemeral_tree_node *best = nullptr;

	while (current)
	{
		int cmp = node_compare_key(tree, key, current);

		if (cmp <= 0)
		{
			// current.key >= key
			best = current;
			current = current->left; // Look for smaller valid node
		}
		else
		{
			current = current->right;
		}
	}

	return best;
}

/*
** Find smallest node with key > target.
*/
static ephemeral_tree_node *
seek_gt(ephemeral_tree *tree, void *key)
{
	ephemeral_tree_node *current = tree->root;
	ephemeral_tree_node *best = nullptr;

	while (current)
	{
		int cmp = node_compare_key(tree, key, current);

		if (cmp < 0)
		{
			// current.key > key
			best = current;
			current = current->left; // Look for smaller valid node
		}
		else
		{
			current = current->right;
		}
	}

	return best;
}

/*
** Find largest node with key <= target.
*/
static ephemeral_tree_node *
seek_le(ephemeral_tree *tree, void *key)
{
	ephemeral_tree_node *current = tree->root;
	ephemeral_tree_node *best = nullptr;

	while (current)
	{
		int cmp = node_compare_key(tree, key, current);

		if (cmp >= 0)
		{
			// current.key <= key
			best = current;
			current = current->right; // Look for larger valid node
		}
		else
		{
			current = current->left;
		}
	}

	return best;
}

/*
** Find largest node with key < target.
*/
static ephemeral_tree_node *
seek_lt(ephemeral_tree *tree, void *key)
{
	ephemeral_tree_node *current = tree->root;
	ephemeral_tree_node *best = nullptr;

	while (current)
	{
		int cmp = node_compare_key(tree, key, current);

		if (cmp > 0)
		{
			// current.key < key
			best = current;
			current = current->right; // Look for larger valid node
		}
		else
		{
			current = current->left;
		}
	}

	return best;
}

// ============================================================================
// PUBLIC TREE INTERFACE
// ============================================================================

/*
** Create a new ephemeral tree.
**
** Parameters:
**   key_type    - Data type of keys
**   record_size - Size of value records in bytes
**   flags       - Bit flags: 0x01 = allow_duplicates, 0x02 = rebalance
**
** Returns: Initialized tree structure
*/
ephemeral_tree
et_create(DataType key_type, uint32_t record_size, uint8_t flags)
{
	ephemeral_tree tree = {};
	tree.key_type = key_type;
	tree.key_size = type_size(key_type);
	tree.record_size = record_size;
	tree.data_size = tree.key_size + record_size;
	tree.allow_duplicates = flags & 0x01;
	tree.rebalance = (flags & 0x02) >> 1;
	return tree;
}

/*
** Clear all nodes from the tree.
**
** Note: Does not free memory
*/
void
et_clear(ephemeral_tree *tree)
{
	tree->root = nullptr;
	tree->node_count = 0;
}

/*
** Insert a key-value pair into the tree.
**
** For existing keys without duplicates, updates the record.
** For duplicates with matching records, updates in place.
**
** Returns: true (always succeeds with sufficient memory)
*/
bool
et_insert(ephemeral_tree *tree, void *key, void *record)
{
	// Find insertion point
	ephemeral_tree_node *parent = nullptr, *current = tree->root;

	while (current)
	{
		parent = current;
		int cmp = node_compare_key(tree, key, current);

		if (cmp == 0 && !tree->allow_duplicates)
		{
			// Update existing entry
			if (record && tree->record_size > 0)
			{
				memcpy(GET_RECORD(current, tree), record, tree->record_size);
			}
			return true;
		}

		// For duplicates with records, use full comparison
		if (cmp == 0 && tree->allow_duplicates && tree->record_size > 0 && record)
		{
			int rec_cmp = memcmp(record, GET_RECORD(current, tree), tree->record_size);
			if (rec_cmp == 0)
			{
				// Exact duplicate - update
				memcpy(GET_RECORD(current, tree), record, tree->record_size);
				return true;
			}
			current = (rec_cmp < 0) ? current->left : current->right;
		}
		else
		{
			current = (cmp < 0) ? current->left : current->right;
		}
	}

	// Insert new node
	ephemeral_tree_node *node = alloc_node(tree, key, record);
	node->parent = parent;

	if (!parent)
	{
		tree->root = node;
	}
	else
	{
		int cmp = node_compare_key(tree, key, parent);
		if (cmp == 0 && tree->allow_duplicates && tree->record_size > 0 && record)
		{
			// Compare records for placement
			int rec_cmp = memcmp(record, GET_RECORD(parent, tree), tree->record_size);
			if (rec_cmp < 0)
			{
				parent->left = node;
			}
			else
			{
				parent->right = node;
			}
		}
		else
		{
			if (cmp < 0)
			{
				parent->left = node;
			}
			else
			{
				parent->right = node;
			}
		}
	}

	insert_fixup(tree, node);
	return true;
}

/*
** Delete first occurrence of key from the tree.
**
** Returns: true if key was found and deleted, false otherwise
*/
bool
et_delete(ephemeral_tree *tree, void *key)
{
	ephemeral_tree_node *current = tree->root;

	while (current)
	{
		int cmp = node_compare_key(tree, key, current);
		if (cmp == 0)
		{
			return delete_node(tree, current);
		}
		current = (cmp < 0) ? current->left : current->right;
	}

	return false;
}

/*
** Delete exact key-record pair from the tree.
**
** Only deletes if both key and record match exactly.
**
** Returns: true if exact match was found and deleted
*/
bool
et_delete_exact(ephemeral_tree *tree, void *key, void *record)
{
	ephemeral_tree_node *current = tree->root;

	while (current)
	{
		int cmp = node_compare_key(tree, key, current);

		if (cmp == 0)
		{
			// Check record match
			if (!tree->allow_duplicates || tree->record_size == 0)
			{
				return delete_node(tree, current);
			}

			int rec_cmp = memcmp(record, GET_RECORD(current, tree), tree->record_size);
			if (rec_cmp == 0)
			{
				return delete_node(tree, current);
			}
			current = (rec_cmp < 0) ? current->left : current->right;
		}
		else
		{
			current = (cmp < 0) ? current->left : current->right;
		}
	}

	return false;
}

// ============================================================================
// CURSOR OPERATIONS
// ============================================================================

/*
** Position cursor at first (leftmost) node.
*/
bool
et_cursor_first(et_cursor *cursor)
{
	if (!cursor->tree.root)
	{
		cursor->state = et_cursor::AT_END;
		return false;
	}

	cursor->current = tree_minimum(cursor->tree.root);
	cursor->state = cursor->current ? et_cursor::VALID : et_cursor::AT_END;
	return cursor->current != nullptr;
}

/*
** Position cursor at last (rightmost) node.
*/
bool
et_cursor_last(et_cursor *cursor)
{
	if (!cursor->tree.root)
	{
		cursor->state = et_cursor::AT_END;
		return false;
	}

	cursor->current = tree_maximum(cursor->tree.root);
	cursor->state = cursor->current ? et_cursor::VALID : et_cursor::AT_END;
	return cursor->current != nullptr;
}

/*
** Move cursor to next node in order.
*/
bool
et_cursor_next(et_cursor *cursor)
{
	if (cursor->state != et_cursor::VALID)
	{
		return false;
	}

	cursor->current = tree_successor(cursor->current);
	if (cursor->current)
	{
		return true;
	}

	cursor->state = et_cursor::AT_END;
	return false;
}

/*
** Move cursor to previous node in order.
*/
bool
et_cursor_previous(et_cursor *cursor)
{
	if (cursor->state != et_cursor::VALID)
	{
		return false;
	}

	cursor->current = tree_predecessor(cursor->current);
	if (cursor->current)
	{
		return true;
	}

	cursor->state = et_cursor::AT_END;
	return false;
}

/*
** Check if cursor can move forward.
*/
bool
et_cursor_has_next(et_cursor *cursor)
{
	if (cursor->state != et_cursor::VALID)
	{
		return false;
	}
	return tree_successor(cursor->current) != nullptr;
}

/*
** Check if cursor can move backward.
*/
bool
et_cursor_has_previous(et_cursor *cursor)
{
	if (cursor->state != et_cursor::VALID)
	{
		return false;
	}
	return tree_predecessor(cursor->current) != nullptr;
}

/*
** Position cursor based on key and comparison operator.
**
** Supports: EQ (exact), GE, GT, LE, LT
*/
bool
et_cursor_seek(et_cursor *cursor, const void *key, comparison_op op)
{
	void *key_bytes = (void *)key;
	ephemeral_tree_node *result = nullptr;

	switch (op)
	{
	case EQ:
		result = seek_eq(&cursor->tree, key_bytes);
		break;
	case GE:
		result = seek_ge(&cursor->tree, key_bytes);
		break;
	case GT:
		result = seek_gt(&cursor->tree, key_bytes);
		break;
	case LE:
		result = seek_le(&cursor->tree, key_bytes);
		break;
	case LT:
		result = seek_lt(&cursor->tree, key_bytes);
		break;
	default:
		cursor->state = et_cursor::INVALID;
		return false;
	}

	if (result)
	{
		cursor->current = result;
		cursor->state = et_cursor::VALID;
		return true;
	}

	cursor->state = (op == EQ) ? et_cursor::INVALID : et_cursor::AT_END;
	return false;
}

/*
** Get key at current cursor position.
*/
void *
et_cursor_key(et_cursor *cursor)
{
	if (cursor->state != et_cursor::VALID)
	{
		return nullptr;
	}
	return GET_KEY(cursor->current);
}

/*
** Get record at current cursor position.
*/
void *
et_cursor_record(et_cursor *cursor)
{
	if (cursor->state != et_cursor::VALID)
	{
		return nullptr;
	}
	return GET_RECORD(cursor->current, &cursor->tree);
}

/*
** Check if cursor is at valid position.
*/
bool
et_cursor_is_valid(et_cursor *cursor)
{
	return cursor->state == et_cursor::VALID;
}

/*
** Insert through cursor.
*/
bool
et_cursor_insert(et_cursor *cursor, void *key, void *record)
{
	return et_insert(&cursor->tree, key, record);
}

/*
** Delete at cursor position and advance.
*/
bool
et_cursor_delete(et_cursor *cursor)
{
	if (cursor->state != et_cursor::VALID)
	{
		return false;
	}

	// Save next position before deletion
	ephemeral_tree_node *next = tree_successor(cursor->current);

	// Delete current node
	bool result = delete_node(&cursor->tree, cursor->current);

	// Move to next position
	if (next)
	{
		cursor->current = next;
		cursor->state = et_cursor::VALID;
	}
	else
	{
		cursor->state = et_cursor::AT_END;
	}

	return result;
}

/*
** Update record at cursor position.
*/
bool
et_cursor_update(et_cursor *cursor, void *record)
{
	if (cursor->state != et_cursor::VALID || cursor->tree.record_size == 0)
	{
		return false;
	}

	memcpy(GET_RECORD(cursor->current, &cursor->tree), record, cursor->tree.record_size);
	return true;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/*
** Get count of nodes in tree.
*/
uint32_t
et_count(const ephemeral_tree *tree)
{
	return tree->node_count;
}

/*
** Check if tree is empty.
*/
bool
et_is_empty(const ephemeral_tree *tree)
{
	return tree->root == nullptr;
}

// ============================================================================
// TREE VALIDATION
// ============================================================================

/*
** Recursively validate tree structure and Red-Black properties.
**
** Checks:
**   - BST property (left < node < right)
**   - Red-Black coloring rules
**   - Black height consistency
**   - Parent pointer correctness
**   - No cycles
*/
static int
validate_node_recursive(const ephemeral_tree *tree, ephemeral_tree_node *node, ephemeral_tree_node *expected_parent,
						void *min_bound, void *max_bound, std::unordered_set<ephemeral_tree_node *> &visited)
{
	if (!node)
	{
		return 0; // NIL nodes are black by definition
	}

	// Check for cycles
	assert(visited.find(node) == visited.end() && "Cycle detected in tree");
	visited.insert(node);

	// Verify parent pointer
	assert(node->parent == expected_parent && "Parent pointer mismatch");

	// Get node's key
	void *key = GET_KEY(node);

	// Check BST bounds
	if (min_bound)
	{
		assert(node_compare_key(tree, min_bound, node) < 0 && "BST violation: node smaller than min bound");
	}
	if (max_bound)
	{
		assert(node_compare_key(tree, max_bound, node) > 0 && "BST violation: node larger than max bound");
	}

	// Red-Black property: Red nodes have black children
	if (IS_RED(node))
	{
		assert(IS_BLACK(node->left) && "Red node has red left child");
		assert(IS_BLACK(node->right) && "Red node has red right child");
		assert(node->parent != nullptr && "Red root node");
		assert(IS_BLACK(node->parent) && "Red node has red parent");
	}

	// Recursively validate children
	int left_black_height = validate_node_recursive(tree, node->left, node, min_bound, key, visited);
	int right_black_height = validate_node_recursive(tree, node->right, node, key, max_bound, visited);

	// Red-Black property: All paths have same black height
	assert(left_black_height == right_black_height && "Black height mismatch");

	// Return black height including this node
	return left_black_height + (IS_BLACK(node) ? 1 : 0);
}

/*
** Validate entire tree structure.
**
** Ensures all Red-Black tree invariants are maintained.
*/
void
et_validate(const ephemeral_tree *tree)
{
	assert(tree != nullptr);

	// Empty tree is valid
	if (!tree->root)
	{
		assert(tree->node_count == 0);
		return;
	}

	// Root must be black
	assert(IS_BLACK(tree->root) && "Root is not black");
	assert(IS_ROOT(tree->root) && "Root has parent");

	// Track visited nodes to detect cycles
	std::unordered_set<ephemeral_tree_node *> visited;

	// Validate recursively and get black height
	int black_height = validate_node_recursive(tree, tree->root, nullptr, nullptr, nullptr, visited);

	// Verify node count matches
	assert(visited.size() == tree->node_count && "Node count mismatch");

	(void)black_height; // Suppress unused warning
}

// ============================================================================
// TREE PRINTING
// ============================================================================

/*
** Helper for in-order traversal printing.
*/
static void
print_inorder_recursive(const ephemeral_tree *tree, ephemeral_tree_node *node, bool &first)
{
	if (!node)
	{
		return;
	}

	print_inorder_recursive(tree, node->left, first);

	if (!first)
	{
		printf(", ");
	}
	first = false;
	printf("[");
	type_print(tree->key_type, GET_KEY(node));
	if (tree->record_size > 0)
	{
		printf(":");
		// Print first few bytes of record as hex
		uint8_t *rec = (uint8_t*)GET_RECORD(node, tree);
		for (uint32_t i = 0; i < std::min(tree->record_size, 4u); i++)
		{
			printf("%02x", rec[i]);
		}
		if (tree->record_size > 4)
		{
			printf("...");
		}
	}
	printf("]");

	print_inorder_recursive(tree, node->right, first);
}

/*
** Visual tree printer - shows structure graphically.
*/
static void
print_tree_visual_helper(const ephemeral_tree *tree, ephemeral_tree_node *node, const std::string &prefix, bool is_tail)
{
	if (!node)
	{
		return;
	}

	printf("%s", prefix.c_str());
	printf("%s", is_tail ? "└── " : "├── ");

	// Print node
	type_print(tree->key_type, GET_KEY(node));
	printf(" %s", IS_RED(node) ? "(R)" : "(B)");

	if (tree->record_size > 0)
	{
		printf(" rec:");
		auto *rec = (uint8_t*)GET_RECORD(node, tree);
		for (uint32_t i = 0; i < std::min(tree->record_size, 4u); i++)
		{
			printf("%02x", rec[i]);
		}
		if (tree->record_size > 4)
		{
			printf("...");
		}
	}
	printf("\n");

	// Print children
	std::string child_prefix = prefix + (is_tail ? "    " : "│   ");

	if (node->left || node->right)
	{
		if (node->right)
		{
			print_tree_visual_helper(tree, node->right, child_prefix, false);
		}
		if (node->left)
		{
			print_tree_visual_helper(tree, node->left, child_prefix, true);
		}
	}
}

/*
** Main print function with BFS level-order traversal.
*/
void
et_print(const ephemeral_tree *tree)
{
	if (!tree || !tree->root)
	{
		printf("Ephemeral Tree: EMPTY\n");
		return;
	}

	printf("====================================\n");
	printf("Ephemeral Tree Structure (Red-Black Tree)\n");
	printf("====================================\n");
	printf("Key type: %s, Key size: %u bytes\n", type_name(tree->key_type), tree->key_size);
	printf("Record size: %u bytes\n", tree->record_size);
	printf("Allow duplicates: %s\n", tree->allow_duplicates ? "YES" : "NO");
	printf("Rebalancing: %s\n", tree->rebalance ? "ENABLED" : "DISABLED");
	printf("Node count: %u\n", tree->node_count);
	printf("------------------------------------\n\n");

	// BFS traversal with level tracking
	struct NodeLevel
	{
		ephemeral_tree_node *node;
		int level;
		bool is_left;
	};

	std::queue<NodeLevel> queue;
	queue.push({tree->root, 0, false});

	int current_level = -1;
	int nodes_in_level = 0;

	printf("Level-Order Traversal:\n");
	while (!queue.empty())
	{
		NodeLevel nl = queue.front();
		queue.pop();

		if (nl.level != current_level)
		{
			if (current_level >= 0)
			{
				printf(" (%d nodes)\n", nodes_in_level);
			}
			printf("Level %d: ", nl.level);
			current_level = nl.level;
			nodes_in_level = 0;
		}

		// Print node info
		if (nodes_in_level > 0)
		{
			printf("  ");
		}
		printf("[");
		type_print(tree->key_type, GET_KEY(nl.node));
		printf("]");

		// Print color
		printf("-%c", IS_RED(nl.node) ? 'R' : 'B');

		nodes_in_level++;

		// Add children to queue
		if (nl.node->left)
		{
			queue.push({nl.node->left, nl.level + 1, true});
		}
		if (nl.node->right)
		{
			queue.push({nl.node->right, nl.level + 1, false});
		}
	}
	if (nodes_in_level > 0)
	{
		printf(" (%d nodes)\n", nodes_in_level);
	}

	printf("\n------------------------------------\n");
	printf("In-order traversal: ");
	bool first = true;
	print_inorder_recursive(tree, tree->root, first);
	printf("\n");

	// Show visual structure
	printf("\nVisual Structure:\n");
	print_tree_visual_helper(tree, tree->root, "", true);
	printf("====================================\n\n");
}
