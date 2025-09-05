/*
** 2024 SQL-FromScratch
**
** ephemeral_tree.hpp - Red-Black tree for temporary in-memory storage
**
** Provides a balanced binary search tree with the same cursor interface
** as the B+tree but optimized for in-memory operations without persistence.
*/

#pragma once
#include "common.hpp"
#include "types.hpp"
#include <cstdint>
#include <cstring>

/*
** TREE NODE STRUCTURE
**
** Nodes store key and record data contiguously in memory:
**   [node_struct][key_bytes][record_bytes]
**
** This layout maximizes cache efficiency during tree traversal.
*/

enum TreeColor : uint8_t
{
	RED = 0,
	BLACK = 1
};

struct ephemeral_tree_node
{
	uint8_t              *data;   // Points to key, record follows at offset key_size
	ephemeral_tree_node  *left;   // Left child (smaller keys)
	ephemeral_tree_node  *right;  // Right child (larger keys)
	ephemeral_tree_node  *parent; // Parent pointer for tree operations
	TreeColor             color;  // RED or BLACK for balancing
};

/*
** TREE STRUCTURE
**
** Configuration and root pointer for the Red-Black tree.
** The tree does not own memory - allocation is handled by MemoryContext.
*/
struct ephemeral_tree
{
	ephemeral_tree_node *root;           // Root node (null for empty tree)
	DataType             key_type;       // Type of keys stored
	uint32_t             key_size;       // Size of each key in bytes
	uint32_t             record_size;    // Size of each record in bytes
	uint32_t             node_count;     // Number of nodes in tree
	uint32_t             data_size;      // Total size: key_size + record_size
	bool                 allow_duplicates; // Whether duplicate keys are permitted
	bool                 rebalance;      // Whether to maintain Red-Black properties
};

/*
** CURSOR STRUCTURE
**
** Maintains position within the tree for iteration and updates.
** The cursor contains a copy of the tree structure (not a pointer)
** to avoid indirection during traversal.
*/
struct et_cursor
{
	ephemeral_tree       tree;    // Tree being traversed (copy)
	ephemeral_tree_node*current; // Current position in tree

	enum State
	{
		INVALID,  // Cursor position is invalid
		VALID,    // Cursor points to valid node
		AT_END    // Cursor has passed last node
	} state;
};

// ============================================================================
// TREE CREATION AND MANAGEMENT
// ============================================================================

/*
** Create a new ephemeral tree.
** Flags: bit 0 = allow_duplicates, bit 1 = enable rebalancing
*/
ephemeral_tree
et_create(DataType key_type, uint32_t record_size, uint8_t flags = 0x03);

/*
** Clear all nodes from tree (does not free memory).
*/
void
et_clear(ephemeral_tree *tree);

// ============================================================================
// TREE OPERATIONS
// ============================================================================

/*
** Insert key-value pair. Updates existing if no duplicates allowed.
*/
bool
et_insert(ephemeral_tree *tree, void *key, void *record);

/*
** Delete first occurrence of key.
*/
bool
et_delete(ephemeral_tree *tree, void *key);

/*
** Delete exact key-record pair (for duplicate handling).
*/
bool
et_delete_exact(ephemeral_tree *tree, void *key, void *record);

// ============================================================================
// CURSOR OPERATIONS
// ============================================================================

// Cursor positioning
bool et_cursor_seek(et_cursor *cursor, const void *key, comparison_op op = EQ);
bool et_cursor_first(et_cursor *cursor);
bool et_cursor_last(et_cursor *cursor);

// Cursor movement
bool et_cursor_next(et_cursor *cursor);
bool et_cursor_previous(et_cursor *cursor);
bool et_cursor_has_next(et_cursor *cursor);
bool et_cursor_has_previous(et_cursor *cursor);

// Cursor data access
void *et_cursor_key(et_cursor *cursor);
void *et_cursor_record(et_cursor *cursor);
bool  et_cursor_is_valid(et_cursor *cursor);

// Cursor modifications
bool et_cursor_insert(et_cursor *cursor, void *key, void *record);
bool et_cursor_update(et_cursor *cursor, void *record);
bool et_cursor_delete(et_cursor *cursor);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/*
** Query tree state.
*/
uint32_t et_count(const ephemeral_tree *tree);
bool     et_is_empty(const ephemeral_tree *tree);

/*
** Debugging and validation.
*/
void et_validate(const ephemeral_tree *tree);
void et_print(const ephemeral_tree *tree);
