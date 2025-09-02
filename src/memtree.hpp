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
	uint32_t	 data_size;		   // Total size: key_type + record_size
	bool		 allow_duplicates; // Whether to allow duplicate keys
};

// Stack for cursor traversal (avoiding parent pointers in traversal)
static constexpr uint32_t MAX_TREE_DEPTH = 64;

struct NodeStack
{
	MemTreeNode *nodes[MAX_TREE_DEPTH];
	uint32_t	 depth;

	inline void
	clear()
	{
		depth = 0;
	}
	inline void
	push(MemTreeNode *node)
	{
		nodes[depth++] = node;
	}
	inline MemTreeNode *
	pop()
	{
		return depth > 0 ? nodes[--depth] : nullptr;
	}
	inline MemTreeNode *
	top()
	{
		return depth > 0 ? nodes[depth - 1] : nullptr;
	}
	inline bool
	empty()
	{
		return depth == 0;
	}
};

// Cursor for traversing the memory tree
struct MemCursor
{
	MemTree		  *tree;
	MemTreeNode	  *current;
	NodeStack	   stack; // Only used for algorithms that need it
	MemoryContext *ctx;	  // Context for allocations

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
memtree_create(DataType key_type, uint32_t record_size, bool allow_duplicates = false);
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
// Cursor Operations - Matching BTree cursor interface
// ============================================================================

bool
memcursor_seek(MemCursor *cursor, const void *key);
bool
memcursor_seek_exact(MemCursor *cursor, const void *key, const void *record);
bool
memcursor_seek_ge(MemCursor *cursor, const void *key);
bool
memcursor_seek_gt(MemCursor *cursor, const void *key);
bool
memcursor_seek_le(MemCursor *cursor, const void *key);
bool
memcursor_seek_lt(MemCursor *cursor, const void *key);
bool
memcursor_first(MemCursor *cursor);
bool
memcursor_last(MemCursor *cursor);
bool
memcursor_next(MemCursor *cursor);
bool
memcursor_previous(MemCursor *cursor);
uint8_t *
memcursor_key(MemCursor *cursor);
uint8_t *
memcursor_record(MemCursor *cursor);
bool
memcursor_is_valid(MemCursor *cursor);
bool
memcursor_insert(MemCursor *cursor, const void *key, const uint8_t *record);
bool
memcursor_delete(MemCursor *cursor);
bool
memcursor_update(MemCursor *cursor, const uint8_t *record);

// ============================================================================
// Utility Functions
// ============================================================================

uint32_t
memtree_count(const MemTree *tree);
bool
memtree_is_empty(const MemTree *tree);
bool
memcursor_seek_cmp(MemCursor *cursor, const uint8_t *key, CompareOp op);
bool
memcursor_has_duplicates(MemCursor *cursor);
uint32_t
memcursor_count_duplicates(MemCursor *cursor, const void *key);

void
memtree_validate(const MemTree *tree);
void
memtree_print(const MemTree *tree);
