// bplustree.hpp - Complete B+Tree header with cursor definitions
#pragma once
#include "defs.hpp"
#include "types.hpp"
#include <cstdint>

#define MIN_ENTRY_COUNT 3

// Forward declaration
struct MemoryContext;

// B+Tree control structure (stores data only in leaves)
struct BPlusTree
{
	uint32_t root_page_index;
	uint32_t internal_max_keys;
	uint32_t leaf_max_keys;
	uint32_t internal_min_keys;
	uint32_t leaf_min_keys;
	uint32_t internal_split_index;
	uint32_t leaf_split_index;
	uint32_t record_size; // Total size of each record
	uint32_t node_key_size;
	DataType node_key_type;
};

// B+Tree management functions
BPlusTree
bplustree_create(DataType key, uint32_t record_size, bool init);

bool
bplustree_clear(BPlusTree *tree);

// B+Tree cursor state enumeration
enum BPtCursorState : uint32_t
{
	BPT_CURSOR_INVALID = 0,
	BPT_CURSOR_VALID = 1,
	BPT_CURSOR_FAULT = 2
};



// Path tracking for B+Tree cursor (simplified for leaf-level operations)

// B+Tree cursor structure
struct BPtCursor
{
	BPlusTree	  *tree;
	MemoryContext *ctx;
	uint32_t	   leaf_page;
	uint32_t	   leaf_index;
	BPtCursorState state;
};

// B+Tree cursor navigation functions
bool
bplustree_cursor_seek(BPtCursor *cursor, const void *key, CompareOp op  = EQ);
bool
bplustree_cursor_previous(BPtCursor *cursor);
bool
bplustree_cursor_next(BPtCursor *cursor);
bool
bplustree_cursor_last(BPtCursor *cursor);
bool
bplustree_cursor_first(BPtCursor *cursor);



// B+Tree cursor data modification functions
bool
bplustree_cursor_update(BPtCursor *cursor, const uint8_t *record);
bool
bplustree_cursor_insert(BPtCursor *cursor, const void *key, const uint8_t *record);
bool
bplustree_cursor_delete(BPtCursor *cursor);

// B+Tree cursor data access functions
uint8_t *
bplustree_cursor_key(BPtCursor *cursor);
uint8_t *
bplustree_cursor_record(BPtCursor *cursor);

// B+Tree cursor state query functions
bool
bplustree_cursor_is_valid(BPtCursor *cursor);
bool
bplustree_cursor_has_next(BPtCursor *cursor);
bool
bplustree_cursor_has_previous(BPtCursor *cursor);


void
bplustree_validate(BPlusTree *tree);

void
bplustree_print(BPlusTree *tree);
