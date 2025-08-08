// btree.hpp
#pragma once
#include "pager.hpp"
#include <vector>
#include <cstdint>

// Data type sizes in bytes
enum DataType : uint32_t {
    TYPE_INT32 = 4,      // 4-byte integer
    TYPE_INT64 = 8,      // 8-byte integer
    TYPE_VARCHAR32 = 32, // Variable char up to 32 bytes
    TYPE_VARCHAR256 = 256 // Variable char up to 256 bytes
};

// Column information for schema
struct ColumnInfo {
    DataType type;
};

// B+Tree capacity information
struct BPlusTreeCapacity {
    uint32_t max_keys;
    uint32_t min_keys;
};

// B+Tree control structure
struct BPlusTree {
    uint32_t root_page_index;
    uint32_t internal_max_keys;
    uint32_t leaf_max_keys;
    uint32_t internal_min_keys;
    uint32_t leaf_min_keys;
    uint32_t internal_split_index;
    uint32_t leaf_split_index;
    uint32_t record_size;  // Total size of each record
};
#define NODE_HEADER_SIZE 32
// B+Tree node structure - fits in a single page
struct BTreeNode {
    // Node header (32 bytes)
    uint32_t index;          // Page index
    uint32_t parent;         // Parent page index (0 if root)
    uint32_t next;           // Next sibling (for leaf nodes)
    uint32_t previous;       // Previous sibling (for leaf nodes)
    uint32_t num_keys;       // Number of keys in this node
    uint32_t max_keys;       // Maximum keys this node can hold
    uint32_t record_size;    // Size of each record (for leaves)
    uint8_t is_leaf;         // 1 if leaf, 0 if internal
    uint8_t padding[3];      // Alignment padding
    // 32 bytes ^
    // Data area - stores keys, children pointers, and data
    // Layout for internal nodes: [keys][children]
    // Layout for leaf nodes: [keys][records]
    uint8_t data[PAGE_SIZE - NODE_HEADER_SIZE]; // Rest of the page (4064 bytes)



};

static_assert(sizeof(BTreeNode) == PAGE_SIZE, "BTreeNode must be exactly PAGE_SIZE");

// Capacity calculation
BPlusTreeCapacity bp_calculate_capacity(const std::vector<ColumnInfo>& schema);

// Tree management
BPlusTree bp_create(const std::vector<ColumnInfo>& schema);
void bp_init(BPlusTree& tree);
void bp_reset(BPlusTree& tree);

// Node management
BTreeNode* bp_create_node(BPlusTree& tree, bool is_leaf);
void bp_destroy_node(BTreeNode* node);
void bp_mark_dirty(BTreeNode* node);

// Node navigation
BTreeNode* bp_get_parent(BTreeNode* node);
BTreeNode* bp_get_child(BTreeNode* node, uint32_t index);
BTreeNode* bp_get_next(BTreeNode* node);
BTreeNode* bp_get_prev(BTreeNode* node);

// Node linking
void bp_set_parent(BTreeNode* node, uint32_t parent_index);
void bp_set_child(BTreeNode* node, uint32_t child_index, uint32_t node_index);
void bp_set_next(BTreeNode* node, uint32_t index);
void bp_set_prev(BTreeNode* node, uint32_t index);

// Tree properties
uint32_t bp_get_max_keys(const BPlusTree& tree, const BTreeNode* node);
uint32_t bp_get_min_keys(const BPlusTree& tree, const BTreeNode* node);
uint32_t bp_get_split_index(const BPlusTree& tree, const BTreeNode* node);

// Core operations - data is now a buffer containing the record
void bp_insert_element(BPlusTree& tree, uint32_t key, const uint8_t* data);
void bp_delete_element(BPlusTree& tree, uint32_t key);

// Search operations - returns pointer to record data within node
bool bp_find_element(const BPlusTree& tree, uint32_t key);
const uint8_t* bp_get(const BPlusTree& tree, uint32_t key);
BTreeNode* bp_find_leaf_node(BTreeNode* node, uint32_t key);

// Tree traversal
BTreeNode* bp_get_root(const BPlusTree& tree);
BTreeNode* bp_left_most(const BPlusTree& tree);
std::vector<std::pair<uint32_t, const uint8_t*>> bp_print_leaves(const BPlusTree& tree);

// Internal operations
void bp_insert(BPlusTree& tree, BTreeNode* node, uint32_t key, const uint8_t* data);
void bp_insert_repair(BPlusTree& tree, BTreeNode* node);
BTreeNode* bp_split(BPlusTree& tree, BTreeNode* node);

void bp_do_delete(BPlusTree& tree, BTreeNode* node, uint32_t key);
void bp_repair_after_delete(BPlusTree& tree, BTreeNode* node);
BTreeNode* bp_merge_right(BPlusTree& tree, BTreeNode* node);
BTreeNode* bp_steal_from_left(BPlusTree& tree, BTreeNode* node, uint32_t parent_index);
BTreeNode* bp_steal_from_right(BPlusTree& tree, BTreeNode* node, uint32_t parent_index);
void bp_update_parent_keys(BPlusTree& tree, BTreeNode* node, uint32_t deleted_key);

void bp_debug_print_tree(const BPlusTree& tree);
void bp_debug_print_structure(const BPlusTree& tree);
uint64_t debug_hash_tree(const BPlusTree& tree);
