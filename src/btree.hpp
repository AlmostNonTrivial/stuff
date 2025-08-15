// btree.hpp
#pragma once
#include "defs.hpp"
#include <cstdint>

enum TreeType : uint32_t { BPLUS = 0, BTREE = 1, INVALID = 2 };

// B+Tree control structure
struct BTree {
  uint32_t root_page_index;
  uint32_t internal_max_keys;
  uint32_t leaf_max_keys;
  uint32_t internal_min_keys;
  uint32_t leaf_min_keys;
  uint32_t internal_split_index;
  uint32_t leaf_split_index;
  uint32_t record_size; // Total size of each record
  DataType node_key_size;
  TreeType tree_type;
};

#define NODE_HEADER_SIZE 24

// B+Tree node structure - fits in a single page
struct BTreeNode {
  // Node header (24 bytes)
  uint32_t index;     // Page index
  uint32_t parent;    // Parent page index (0 if root)
  uint32_t next;      // Next sibling (for leaf nodes)
  uint32_t previous;  // Previous sibling (for leaf nodes)
  uint32_t num_keys;  // Number of keys in this node
  uint8_t is_leaf;    // 1 if leaf, 0 if internal
  uint8_t padding[3]; // Alignment padding (increased due to max_keys removal)
  // 24 bytes ^
  // Data area - stores keys, children pointers, and data
  // Layout for internal nodes: [keys][children]
  // Layout for leaf nodes: [keys][records]
  uint8_t data[PAGE_SIZE - NODE_HEADER_SIZE]; // Rest of the page (4064 bytes)
};

static_assert(sizeof(BTreeNode) == PAGE_SIZE,
              "BTreeNode must be exactly PAGE_SIZE");

BTree bt_create(DataType key, uint32_t record_size, TreeType tree_type);
bool bt_clear(BTree *tree);

enum CursorState : uint32_t {
  CURSOR_INVALID = 0,
  CURSOR_VALID = 1,
  CURSOR_REQUIRESEEK = 2,
  CURSOR_FAULT = 3
};


struct CursorPath {
    uint32_t page_stack[16];  // Page indexes
    uint32_t index_stack[16]; // Key indexes within each page
    uint32_t child_stack[16]; // Child position we came from
    uint32_t stack_depth;
    uint32_t current_page;
    uint32_t current_index;
};

struct BtCursor {
  BTree *tree;
  CursorPath path;
  CursorPath saved;
  CursorState state;
};

bool bt_cursor_seek(BtCursor *cursor, const void *key);
bool bt_cursor_previous(BtCursor *cursor);
bool bt_cursor_next(BtCursor *cursor);
bool bt_cursor_last(BtCursor *cursor);
bool bt_cursor_first(BtCursor *cursor);
bool bt_cursor_is_end(BtCursor *cursor);
bool bt_cursor_is_start(BtCursor *cursor);
bool bt_cursor_update(BtCursor *cursor, const uint8_t *record);
bool bt_cursor_insert(BtCursor *cursor, const void *key, const uint8_t *record);
bool bt_cursor_delete(BtCursor *cursor);

uint8_t *bt_cursor_key(BtCursor *cursor);
uint8_t *bt_cursor_record(BtCursor *cursor);

bool bt_cursor_seek_ge(BtCursor *cursor, const void *key);
bool bt_cursor_seek_gt(BtCursor *cursor, const void *key);
bool bt_cursor_seek_le(BtCursor *cursor, const void *key);
bool bt_cursor_seek_lt(BtCursor *cursor, const void *key);

bool bt_cursor_is_valid(BtCursor *cursor);

bool bt_cursor_has_next(BtCursor *cursor);
bool bt_cursor_has_previous(BtCursor *cursor);
