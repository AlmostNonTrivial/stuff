// memtree.hpp
#pragma once

#include "defs.hpp"
#include "arena.hpp"
#include <cstdint>
#include <cstring>

// Red-Black in-memory binary search tree for temporary storage
// Provides same cursor interface as BTree but lives entirely in QueryArena
// Key and record stored contiguously: [key_bytes][record_bytes]

enum TreeColor : uint8_t
{
	RED = 0,
	BLACK = 1
};

struct ephemeral_tree_node
{
	uint8_t		*data; // Single pointer: key at offset 0, record at offset key_size
	ephemeral_tree_node *left;
	ephemeral_tree_node *right;
	ephemeral_tree_node *parent; // Parent pointer for red-black operations
	TreeColor color;	 // Node color for red-black tree
};

struct ephemeral_tree
{
	ephemeral_tree_node *root;
	DataType	 key_type;
	uint32_t	 key_size;
	uint32_t	 record_size;
	uint32_t	 node_count;
	uint32_t	 data_size;		   // Total size: key_size + record_size
	bool allow_duplicates; // Whether to allow duplicate keys
	bool rebalance;
};

// Cursor for traversing the memory tree
struct et_cursor
{
	ephemeral_tree		   tree;
	ephemeral_tree_node	  *current;
	MemoryContext *ctx; // Context for allocations

	enum State
	{
		INVALID,
		VALID,
		AT_END
	} state;
};

// ============================================================================
// Tree Creation and Management
// ============================================================================

ephemeral_tree
ephemeral_tree_create(DataType key_type, uint32_t record_size, uint8_t flags = 0b11000000);


void
ephemeral_tree_clear(ephemeral_tree *tree);

// ============================================================================
// Tree Operations
// ============================================================================

bool
ephemeral_tree_insert(ephemeral_tree *tree, void*key, void*record, MemoryContext *ctx);

bool
ephemeral_tree_delete(ephemeral_tree *tree, void*key);

bool
ephemeral_tree_delete_exact(ephemeral_tree *tree, void*key, void*record);

// ============================================================================
// Cursor Operations - Matching BPlusTree cursor interface
// ============================================================================

// Cursor navigation functions
bool
et_cursor_seek(et_cursor *cursor, const void *key, CompareOp op = EQ);

bool
et_cursor_previous(et_cursor *cursor);

bool
et_cursor_next(et_cursor *cursor);

bool
et_cursor_last(et_cursor *cursor);

bool
et_cursor_first(et_cursor *cursor);

// Cursor data modification functions
bool
et_cursor_update(et_cursor *cursor, void *record);

bool
et_cursor_insert(et_cursor *cursor, void *key, void*record);

bool
et_cursor_delete(et_cursor *cursor);

// Cursor data access functions
void *
et_cursor_key(et_cursor *cursor);

void *
et_cursor_record(et_cursor *cursor);

// Cursor state query functions
bool
et_cursor_is_valid(et_cursor *cursor);

bool
et_cursor_has_next(et_cursor *cursor);

bool
et_cursor_has_previous(et_cursor *cursor);



// ============================================================================
// Utility Functions
// ============================================================================

uint32_t
et_count(const ephemeral_tree *tree);

bool
et_is_empty(const ephemeral_tree *tree);

void
et_validate(const ephemeral_tree *tree);

void
ephemeral_tree_print(const ephemeral_tree *tree);
