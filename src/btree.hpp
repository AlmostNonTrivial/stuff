// btree.hpp - Complete B-Tree header with cursor definitions
#pragma once
#include "defs.hpp"
#include <cstdint>

#define MIN_ENTRY_COUNT 3

// Forward declaration
struct MemoryContext;

// B-Tree control structure (stores data in all nodes)
struct BTree {
	uint32_t root_page_index;
	uint32_t max_keys;
	uint32_t min_keys;
	uint32_t split_index;
	uint32_t record_size; // Total size of each record
	DataType node_key_size;
};

#define NODE_HEADER_SIZE 16
#define NODE_DATA_SIZE	 PAGE_SIZE - NODE_HEADER_SIZE
struct BTreeNode
{
	// Node header (24 bytes)
	uint32_t index;	 // Page index
	uint32_t parent; // Parent page
	uint32_t num_keys;	// Number of


	uint8_t is_leaf;	// 1 if leaf, 0
						// if internal
	uint8_t padding[3]; // Alignment
	uint8_t data[NODE_DATA_SIZE]; // Rest

};
// B-Tree management functions
BTree
btree_create(DataType key, uint32_t record_size, bool init);
bool
btree_clear(BTree *tree);

// Cursor state enumeration
enum CursorState : uint32_t {
	CURSOR_INVALID = 0,
	CURSOR_VALID = 1,
	CURSOR_REQUIRESEEK = 2,
	CURSOR_FAULT = 3
};

#define MAX_BTREE_DEPTH 16

// Path tracking for B-Tree cursor
// B-Tree cursor structure
struct BtCursor {
	BTree *tree;
	MemoryContext *ctx;

	struct {
		uint32_t page_stack[MAX_BTREE_DEPTH];  // Page indexes
		uint32_t index_stack[MAX_BTREE_DEPTH]; // Key indexes within
						       // each page
		uint32_t
		    child_stack[MAX_BTREE_DEPTH]; // Child position we came from
		uint32_t stack_depth;
		uint32_t current_page;
		uint32_t current_index;
	}

	path,
	    saved; // For save/restore operations
	CursorState state;
};

// B-Tree cursor navigation functions
bool
btree_cursor_seek(BtCursor *cursor, const void *key);
bool
btree_cursor_previous(BtCursor *cursor);
bool
btree_cursor_next(BtCursor *cursor);
bool
btree_cursor_last(BtCursor *cursor);
bool
btree_cursor_first(BtCursor *cursor);
bool
btree_cursor_is_end(BtCursor *cursor);
bool
btree_cursor_is_start(BtCursor *cursor);

// B-Tree cursor data modification functions
bool
btree_cursor_update(BtCursor *cursor, const uint8_t *record);
bool
btree_cursor_insert(BtCursor *cursor, const void *key, const uint8_t *record);
bool
btree_cursor_delete(BtCursor *cursor);

// B-Tree cursor data access functions
uint8_t *
btree_cursor_key(BtCursor *cursor);
uint8_t *
btree_cursor_record(BtCursor *cursor);

// B-Tree cursor comparison seek functions
bool
btree_cursor_seek_ge(BtCursor *cursor, const void *key);
bool
btree_cursor_seek_gt(BtCursor *cursor, const void *key);
bool
btree_cursor_seek_le(BtCursor *cursor, const void *key);
bool
btree_cursor_seek_lt(BtCursor *cursor, const void *key);

// B-Tree cursor state query functions
bool
btree_cursor_is_valid(BtCursor *cursor);
bool
btree_cursor_has_next(BtCursor *cursor);
bool
btree_cursor_has_previous(BtCursor *cursor);

// B-Tree cursor advanced operations
bool
btree_cursor_seek_cmp(BtCursor *cursor, const void *key, CompareOp op);
bool
btree_cursor_seek_exact(BtCursor *cursor, const void *key,
			const uint8_t *record);
