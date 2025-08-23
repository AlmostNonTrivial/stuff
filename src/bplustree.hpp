// btree.hpp
#pragma once
#include "defs.hpp"
#include <cstdint>

#define MIN_ENTRY_COUNT 3
// #define MAX_COLS PAGE_SIZE - NODE_HEADER_SIZE

// B+Tree control structure
struct BPlusTree {
	uint32_t root_page_index;
	uint32_t internal_max_keys;
	uint32_t leaf_max_keys;
	uint32_t internal_min_keys;
	uint32_t leaf_min_keys;
	uint32_t internal_split_index;
	uint32_t leaf_split_index;
	uint32_t record_size; // Total size of each record
	DataType node_key_size;
	// TreeType tree_type;
	// bool in_memory;
};

BPlusTree
btree_create(DataType key, uint32_t record_size);
bool
btree_clear(BPlusTree *tree);

enum CursorState : uint32_t {
	CURSOR_INVALID = 0,
	CURSOR_VALID = 1,
	CURSOR_REQUIRESEEK = 2,
	CURSOR_FAULT = 3
};

#define MAX_BTREE_DEPTH 16

struct BPtCursor {
	BPlusTree *tree;
	MemoryContext *ctx;
	// actually, a bptree doesn't need a stack
	struct {
		uint32_t page_stack[MAX_BTREE_DEPTH];  // Page indexes
		uint32_t index_stack[MAX_BTREE_DEPTH]; // Key indexes within
						       // each page
		uint32_t
		    child_stack[MAX_BTREE_DEPTH]; // Child position we came from
		uint32_t stack_depth;
		uint32_t current_page;
		uint32_t current_index;
	} path, saved;
	CursorState state;
};

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
bool
btree_cursor_update(BtCursor *cursor, const uint8_t *record);
bool
btree_cursor_insert(BtCursor *cursor, const void *key, const uint8_t *record);
bool
btree_cursor_delete(BtCursor *cursor);

uint8_t *
btree_cursor_key(BtCursor *cursor);
uint8_t *
btree_cursor_record(BtCursor *cursor);

bool
btree_cursor_seek_ge(BtCursor *cursor, const void *key);
bool
btree_cursor_seek_gt(BtCursor *cursor, const void *key);
bool
btree_cursor_seek_le(BtCursor *cursor, const void *key);
bool
btree_cursor_seek_lt(BtCursor *cursor, const void *key);

bool
btree_cursor_is_valid(BtCursor *cursor);

bool
btree_cursor_has_next(BtCursor *cursor);
bool
btree_cursor_has_previous(BtCursor *cursor);

bool
btree_cursor_seek_cmp(BtCursor *cursor, const void *key, CompareOp op);
bool
btree_cursor_seek_exact(BtCursor *cursor, const void *key,
			const uint8_t *record);
