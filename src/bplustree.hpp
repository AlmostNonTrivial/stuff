// bplustree.hpp - Complete B+Tree header with cursor definitions
#pragma once
#include "defs.hpp"
#include "types.hpp"
#include <cstdint>

// B+Tree control structure (stores data only in leaves)
struct BTree
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
BTree
btree_create(DataType key, uint32_t record_size, bool init);

bool
btree_clear(BTree *tree);

// B+Tree cursor state enumeration
enum BtCursorState : uint8_t
{
	BT_CURSOR_INVALID = 0,
	BT_CURSOR_VALID = 1,
};

// Path tracking for B+Tree cursor (simplified for leaf-level operations)

// B+Tree cursor structure
struct BtCursor
{
	BTree		 *tree;
	uint32_t	  leaf_page;
	uint32_t	  leaf_index;
	BtCursorState state;
};

// B+Tree cursor navigation functions
bool
btree_cursor_seek(BtCursor *cursor, void *key, CompareOp op = EQ);
bool
btree_cursor_previous(BtCursor *cursor);
bool
btree_cursor_next(BtCursor *cursor);
bool
btree_cursor_last(BtCursor *cursor);
bool
btree_cursor_first(BtCursor *cursor);

// B+Tree cursor data modification functions
bool
btree_cursor_update(BtCursor *cursor, void *record);
bool
btree_cursor_insert(BtCursor *cursor, void *key, void *record);
bool
btree_cursor_delete(BtCursor *cursor);

// B+Tree cursor data access functions
void *
btree_cursor_key(BtCursor *cursor);
void *
btree_cursor_record(BtCursor *cursor);

// B+Tree cursor state query functions
bool
btree_cursor_is_valid(BtCursor *cursor);
bool
btree_cursor_has_next(BtCursor *cursor);
bool
btree_cursor_has_previous(BtCursor *cursor);

void
btree_validate(BTree *tree);

void
btree_print(BTree *tree);
