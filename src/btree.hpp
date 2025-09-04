/*
** btree.hpp - B+Tree interface for educational SQL engine
**
** This header defines the public API for a disk-based B+Tree implementation.
** The B+Tree provides ordered key-value storage with efficient range queries,
** making it suitable for database indexes and primary key storage.
**
** KEY FEATURES:
**   - Disk-based storage with page-level granularity
**   - Support for variable key types (integer, float, string)
**   - Efficient range queries through leaf-level linking
**   - Cursor-based iteration with bidirectional traversal
**   - Transaction support via pager integration
**
** USAGE EXAMPLE:
**   ```
**   // Create a B+Tree with integer keys and 100-byte records
**   BTree tree = btree_create(DataType::INT32, 100, true);
**
**   // Insert data using cursor
**   BtCursor cursor = {&tree, 0, 0, BT_CURSOR_INVALID};
**   int key = 42;
**   char record[100] = "example data";
**   btree_cursor_insert(&cursor, &key, record);
**
**   // Range query
**   int start = 10;
**   if (btree_cursor_seek(&cursor, &start, GE)) {
**       while (btree_cursor_is_valid(&cursor)) {
**           process_record(btree_cursor_record(&cursor));
**           btree_cursor_next(&cursor);
**       }
**   }
**   ```
**
** THREAD SAFETY:
**   This implementation is NOT thread-safe. External synchronization
**   is required for concurrent access.
**
** TRANSACTION REQUIREMENTS:
**   All modification operations (insert, update, delete) require an
**   active pager transaction. Read operations can be performed outside
**   transactions but may see uncommitted changes.
*/

#pragma once

#include "defs.hpp"
#include "types.hpp"
#include <cstdint>

/* ============================================================================
** B+TREE CONTROL STRUCTURE
**
** The BTree structure contains configuration and metadata for a B+Tree
** instance. It defines the tree's key type, record size, and capacity
** limits calculated during initialization.
**
** MEMBERS:
**   root_page_index: Page number of the root node
**
**   internal_max_keys: Maximum keys per internal node
**   leaf_max_keys: Maximum keys per leaf node
**
**   internal_min_keys: Minimum keys for non-root internal nodes
**   leaf_min_keys: Minimum keys for non-root leaf nodes
**
**   internal_split_index: Split point for internal nodes
**   leaf_split_index: Split point for leaf nodes
**
**   record_size: Size of each data record in bytes
**   node_key_size: Size of each key in bytes
**   node_key_type: Data type of keys (for comparison)
**
** NOTE: The min/max values are calculated to maximize page utilization
** while ensuring balanced splits and merges.
** ============================================================================ */

struct BTree {
    uint32_t root_page_index;      /* Root node location */

    /* Node capacity limits */
    uint32_t internal_max_keys;    /* Max keys in internal node */
    uint32_t leaf_max_keys;        /* Max keys in leaf node */
    uint32_t internal_min_keys;    /* Min keys (non-root internal) */
    uint32_t leaf_min_keys;        /* Min keys (non-root leaf) */

    /* Split points for overflow handling */
    uint32_t internal_split_index; /* Where to split internal nodes */
    uint32_t leaf_split_index;     /* Where to split leaf nodes */

    /* Data configuration */
    uint32_t record_size;          /* Size of value/record */
    uint32_t node_key_size;        /* Size of key */
    DataType node_key_type;        /* Key data type */
};

/* ============================================================================
** TREE LIFECYCLE MANAGEMENT
** ============================================================================ */

/*
** Create a new B+Tree instance.
**
** Parameters:
**   key: Data type for tree keys (INT32, INT64, FLOAT, DOUBLE, STRING)
**   record_size: Size of each data record in bytes
**   init: If true, create an empty root node; if false, tree is uninitialized
**
** Returns:
**   Configured BTree structure. Check root_page_index != 0 for success.
**
** NOTE:
**   - Requires active transaction if init=true
**   - Calculates optimal node capacities based on PAGE_SIZE
**   - Returns invalid tree (root_page_index=0) if record_size too large
*/
BTree btree_create(DataType key, uint32_t record_size, bool init);

/*
** Clear all nodes in a B+Tree.
**
** Performs a complete tree traversal, deleting all nodes and returning
** pages to the pager's free list. The tree structure remains valid but
** empty after this operation.
**
** Parameters:
**   tree: Pointer to the BTree to clear
**
** Returns:
**   true on success, false on error
**
** NOTE: Requires active transaction. This is a destructive operation
** that cannot be undone except by transaction rollback.
*/
bool btree_clear(BTree* tree);

/* ============================================================================
** CURSOR STATE MANAGEMENT
**
** Cursors provide stateful iteration over B+Tree contents. A cursor
** maintains a position within the tree and can move forward/backward
** through keys in sorted order.
** ============================================================================ */

/*
** Cursor state enumeration.
**
** BT_CURSOR_INVALID: Cursor is not positioned at any valid entry
** BT_CURSOR_VALID: Cursor points to a valid key-value pair
*/
enum BtCursorState : uint8_t {
    BT_CURSOR_INVALID = 0,
    BT_CURSOR_VALID = 1,
};

/*
** B+Tree cursor structure.
**
** Maintains position state for tree traversal. The cursor becomes
** invalid if the pointed-to entry is deleted or if navigation moves
** past the tree boundaries.
**
** MEMBERS:
**   tree: Pointer to the B+Tree being traversed
**   leaf_page: Current leaf node's page number
**   leaf_index: Index within current leaf node
**   state: Validity state of cursor position
**
** NOTE: Cursors do not survive tree modifications by other cursors.
** After external modifications, cursors should be re-positioned.
*/
struct BtCursor {
    BTree*        tree;        /* Tree being traversed */
    uint32_t      leaf_page;   /* Current leaf page */
    uint32_t      leaf_index;  /* Position in leaf */
    BtCursorState state;       /* Cursor validity */
};

/* ============================================================================
** CURSOR POSITIONING OPERATIONS
**
** These functions position a cursor at specific locations within the tree
** or move it relative to its current position.
** ============================================================================ */

/*
** Seek cursor to a key with specified comparison.
**
** Positions the cursor at the first key that satisfies the comparison
** operator relative to the provided key.
**
** Parameters:
**   cursor: Cursor to position
**   key: Key to search for
**   op: Comparison operator (EQ, GE, GT, LE, LT)
**
** Returns:
**   true if a matching key was found, false otherwise
**
** Examples:
**   - seek(cursor, &k, EQ): Find exact match for k
**   - seek(cursor, &k, GE): Find first key >= k
**   - seek(cursor, &k, LT): Find last key < k
*/
bool btree_cursor_seek(BtCursor* cursor, void* key, CompareOp op = EQ);

/*
** Move cursor to the previous key in sort order.
**
** Returns:
**   true if move succeeded, false if already at first key
**
** NOTE: Cursor becomes invalid if it moves before the first key.
*/
bool btree_cursor_previous(BtCursor* cursor);

/*
** Move cursor to the next key in sort order.
**
** Returns:
**   true if move succeeded, false if already at last key
**
** NOTE: Cursor becomes invalid if it moves past the last key.
*/
bool btree_cursor_next(BtCursor* cursor);

/*
** Position cursor at the last key in the tree.
**
** Returns:
**   true if tree is non-empty, false if tree is empty
*/
bool btree_cursor_last(BtCursor* cursor);

/*
** Position cursor at the first key in the tree.
**
** Returns:
**   true if tree is non-empty, false if tree is empty
*/
bool btree_cursor_first(BtCursor* cursor);

/* ============================================================================
** DATA MODIFICATION OPERATIONS
**
** These functions modify the tree contents through a cursor position.
** All modifications require an active pager transaction.
** ============================================================================ */

/*
** Update the record at cursor position.
**
** Overwrites the record data at the current cursor position with new data.
** The key remains unchanged.
**
** Parameters:
**   cursor: Valid cursor positioned at target entry
**   record: New record data (must be record_size bytes)
**
** Returns:
**   true on success, false if cursor invalid
**
** NOTE: Requires active transaction. Marks page as dirty.
*/
bool btree_cursor_update(BtCursor* cursor, void* record);

/*
** Insert a new key-value pair.
**
** Inserts the specified key-value pair into the tree. Fails if the key
** already exists (no duplicate keys allowed).
**
** Parameters:
**   cursor: Cursor for tree access (position doesn't matter)
**   key: Key to insert (must be node_key_size bytes)
**   record: Record data (must be record_size bytes)
**
** Returns:
**   true on success, false if key already exists
**
** NOTE:
**   - Requires active transaction
**   - May trigger node splits up to root
**   - Cursor position after insert is undefined
*/
bool btree_cursor_insert(BtCursor* cursor, void* key, void* record);

/*
** Delete entry at cursor position.
**
** Removes the key-value pair at the current cursor position. The cursor
** attempts to remain valid by moving to an adjacent entry if possible.
**
** Parameters:
**   cursor: Valid cursor positioned at target entry
**
** Returns:
**   true on success, false if cursor invalid
**
** NOTE:
**   - Requires active transaction
**   - May trigger node merges up to root
**   - Cursor may become invalid if tree becomes empty
*/
bool btree_cursor_delete(BtCursor* cursor);

/* ============================================================================
** DATA ACCESS OPERATIONS
**
** These functions retrieve data from the current cursor position without
** modifying the tree. They return nullptr if the cursor is invalid.
** ============================================================================ */

/*
** Get pointer to key at cursor position.
**
** Returns:
**   Pointer to key data, or nullptr if cursor invalid
**
** NOTE: Pointer is valid only until next tree modification or page eviction.
** Callers should copy data if persistence is needed.
*/
void* btree_cursor_key(BtCursor* cursor);

/*
** Get pointer to record at cursor position.
**
** Returns:
**   Pointer to record data, or nullptr if cursor invalid
**
** NOTE: Pointer is valid only until next tree modification or page eviction.
** Callers should copy data if persistence is needed.
*/
void* btree_cursor_record(BtCursor* cursor);

/* ============================================================================
** CURSOR STATE QUERIES
**
** These functions query cursor state without modifying position.
** ============================================================================ */

/*
** Check if cursor is positioned at a valid entry.
**
** Returns:
**   true if cursor points to valid data, false otherwise
*/
bool btree_cursor_is_valid(BtCursor* cursor);

/*
** Check if cursor can move to next entry.
**
** Returns:
**   true if btree_cursor_next() would succeed, false otherwise
**
** NOTE: Does not actually move the cursor.
*/
bool btree_cursor_has_next(BtCursor* cursor);

/*
** Check if cursor can move to previous entry.
**
** Returns:
**   true if btree_cursor_previous() would succeed, false otherwise
**
** NOTE: Does not actually move the cursor.
*/
bool btree_cursor_has_previous(BtCursor* cursor);

/* ============================================================================
** DEBUG AND VALIDATION OPERATIONS
**
** These functions are primarily for testing and debugging. They perform
** expensive tree traversals and should not be used in production code.
** ============================================================================ */

/*
** Validate B+Tree structure invariants.
**
** Performs a complete tree traversal checking:
**   - Node capacity constraints (min/max keys)
**   - Parent-child relationships
**   - Key ordering within nodes
**   - Leaf chain connectivity
**   - Tree height consistency
**
** Parameters:
**   tree: Tree to validate
**
** NOTE: This is an expensive O(n) operation. Use only for debugging.
** Assertion failures indicate tree corruption.
*/
void btree_validate(BTree* tree);

/*
** Print tree structure for debugging.
**
** Outputs a human-readable representation of the tree structure,
** including node contents, parent-child relationships, and statistics.
**
** Parameters:
**   tree: Tree to print
**
** NOTE: Output format is implementation-defined and may change.
** Intended for debugging only.
*/
void btree_print(BTree* tree);
