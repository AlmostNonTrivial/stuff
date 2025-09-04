// memtree.hpp
#pragma once

#include "defs.hpp"
#include "arena.hpp"
#include <cstdint>
#include <cstring>

// Red-Black in-memory binary search tree for temporary storage
// Provides same cursor interface as BTree but lives entirely in QueryArena
// Key and record stored contiguously: [key_bytes][record_bytes]

enum MemTreeColor : uint8_t
{
	RED = 0,
	BLACK = 1
};

struct MemTreeNode
{
	uint8_t		*data; // Single pointer: key at offset 0, record at offset key_size
	MemTreeNode *left;
	MemTreeNode *right;
	MemTreeNode *parent; // Parent pointer for red-black operations
	MemTreeColor color;	 // Node color for red-black tree
};

struct MemTree
{
	MemTreeNode *root;
	DataType	 key_type;
	uint32_t	 key_size;
	uint32_t	 record_size;
	uint32_t	 node_count;
	uint32_t	 data_size;		   // Total size: key_size + record_size
	bool allow_duplicates; // Whether to allow duplicate keys
	bool rebalance;
};

// Cursor for traversing the memory tree
struct MemCursor
{
	MemTree		   tree;
	MemTreeNode	  *current;
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

MemTree
memtree_create(DataType key_type, uint32_t record_size, uint8_t flags = 0b11000000);


void
memtree_clear(MemTree *tree);

// ============================================================================
// Tree Operations
// ============================================================================

bool
memtree_insert(MemTree *tree, const uint8_t *key, const uint8_t *record, MemoryContext *ctx);

bool
memtree_delete(MemTree *tree, const uint8_t *key);

bool
memtree_delete_exact(MemTree *tree, const uint8_t *key, const uint8_t *record);

// ============================================================================
// Cursor Operations - Matching BPlusTree cursor interface
// ============================================================================

// Cursor navigation functions
bool
memcursor_seek(MemCursor *cursor, const void *key, CompareOp op = EQ);

bool
memcursor_previous(MemCursor *cursor);

bool
memcursor_next(MemCursor *cursor);

bool
memcursor_last(MemCursor *cursor);

bool
memcursor_first(MemCursor *cursor);

// Cursor data modification functions
bool
memcursor_update(MemCursor *cursor, const uint8_t *record);

bool
memcursor_insert(MemCursor *cursor, const void *key, const uint8_t *record);

bool
memcursor_delete(MemCursor *cursor);

// Cursor data access functions
uint8_t *
memcursor_key(MemCursor *cursor);

uint8_t *
memcursor_record(MemCursor *cursor);

// Cursor state query functions
bool
memcursor_is_valid(MemCursor *cursor);

bool
memcursor_has_next(MemCursor *cursor);

bool
memcursor_has_previous(MemCursor *cursor);



// ============================================================================
// Utility Functions
// ============================================================================

uint32_t
memtree_count(const MemTree *tree);

bool
memtree_is_empty(const MemTree *tree);

void
memtree_validate(const MemTree *tree);

void
memtree_print(const MemTree *tree);
