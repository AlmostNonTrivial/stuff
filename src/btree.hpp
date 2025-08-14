// btree.hpp
#pragma once
#include "defs.hpp"
#include "pager.hpp"
#include <cstdint>
#include <vector>

enum TreeType : uint32_t { BPLUS = 0, BTREE = 1, INVALID = 2 };

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
  TreeType tree_type;
};

#define NODE_HEADER_SIZE 24

// B+Tree node structure - fits in a single page
struct BPTreeNode {
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

static_assert(sizeof(BPTreeNode) == PAGE_SIZE,
              "BTreeNode must be exactly PAGE_SIZE");

BPlusTree bt_create(DataType key, uint32_t record_size, TreeType tree_type);
void bp_init(BPlusTree &tree);

// // Core operations - data is a buffer containing the record
void bp_insert_element(BPlusTree &tree, void *key, const uint8_t *data);


uint8_t *get_key_at(BPlusTree &tree, BPTreeNode *node, uint32_t index);

BPTreeNode *bp_get_root(BPlusTree &tree);



BPTreeNode *bp_get_child(BPlusTree &tree, BPTreeNode *node, uint32_t index);
BPTreeNode *bp_get_parent(BPTreeNode *node);

uint32_t bp_get_max_keys(BPlusTree &tree, BPTreeNode *node);

uint32_t bp_get_split_index(BPlusTree &tree, BPTreeNode *node);
uint32_t bp_get_min_keys(BPlusTree &tree, BPTreeNode *node);

// BPTreeNode *bp_find_leaf_node(BPlusTree &tree, BPTreeNode *node, const
// uint8_t* key);

enum CursorState {
  CURSOR_INVALID = 0,
  CURSOR_VALID = 1,
  CURSOR_REQUIRESEEK = 2,
  CURSOR_FAULT = 3
};

// Cursor structure


uint32_t *get_children(BPlusTree &tree, BPTreeNode *node);

BPTreeNode *bp_get_next(BPTreeNode *node);
BPTreeNode *bp_get_prev(BPTreeNode *node);


uint8_t *get_leaf_record_data(BPlusTree &tree, BPTreeNode *node);


struct BtCursor {
  BPlusTree *tree;

  struct {
      uint32_t page_stack[16];     // Page indexes
      uint32_t index_stack[16];    // Key indexes within each page
      uint32_t child_stack[16];    // Child position we came from
      uint32_t stack_depth;

      uint32_t get() {
       return child_stack[0];
      };

  } stack;
  // Stack tracking path from root to current position
  // Current position
  uint32_t current_page;
  uint32_t current_index;

  CursorState state;
  bool is_write_cursor;
};
BtCursor *bt_cursor_create(BPlusTree *tree, bool write_cursor);

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
const uint8_t* bt_cursor_get_key(BtCursor* cursor);


uint8_t *bt_cursor_read(BtCursor *cursor);

bool bt_cursor_seek_ge(BtCursor *cursor, const void *key);
bool bt_cursor_seek_gt(BtCursor *cursor, const void *key) ;
bool bt_cursor_seek_le(BtCursor *cursor, const void *key);
bool bt_cursor_seek_lt(BtCursor *cursor, const void *key) ;
bool bt_cursor_is_valid(BtCursor *cursor) ;


void bt_cursor_clear(BtCursor *cursor);

bool bt_cursor_has_next(BtCursor*cursor);
