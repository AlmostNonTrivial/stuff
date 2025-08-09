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

enum TreeType : uint32_t {
    BPLUS = 0,
    BTREE = 1,
    INVALID = 2
};

// Column information for schema
struct ColumnInfo {
    DataType type;
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
    uint32_t node_key_size;
    TreeType tree_type;
};

#define NODE_HEADER_SIZE 28

// B+Tree node structure - fits in a single page
struct BPTreeNode {
    // Node header (28 bytes)
    uint32_t index;          // Page index
    uint32_t parent;         // Parent page index (0 if root)
    uint32_t next;           // Next sibling (for leaf nodes)
    uint32_t previous;       // Previous sibling (for leaf nodes)
    uint32_t num_keys;       // Number of keys in this node
    uint32_t record_size;    // Size of each record (for leaves)
    uint8_t is_leaf;         // 1 if leaf, 0 if internal
    uint8_t padding[3];      // Alignment padding (increased due to max_keys removal)
    // 28 bytes ^
    // Data area - stores keys, children pointers, and data
    // Layout for internal nodes: [keys][children]
    // Layout for leaf nodes: [keys][records]
    uint8_t data[PAGE_SIZE - NODE_HEADER_SIZE]; // Rest of the page (4064 bytes)
};

struct LeafDataEntry {
    uint32_t key;
    uint32_t node_page;  // Which leaf node this came from
    std::vector<uint8_t> data;  // Copied record data
};

static_assert(sizeof(BPTreeNode) == PAGE_SIZE, "BTreeNode must be exactly PAGE_SIZE");

// Capacity calculation




// Tree management

BPlusTree bp_create(DataType key, const std::vector<ColumnInfo> &schema,
                    TreeType tree_type);
void bp_init(BPlusTree& tree);
void bp_reset(BPlusTree& tree);

// Node management
BPTreeNode* bp_create_node(BPlusTree& tree, bool is_leaf);
void bp_destroy_node(BPTreeNode* node);
void bp_mark_dirty(BPTreeNode* node);

// Node navigation
BPTreeNode* bp_get_parent(BPTreeNode* node);
BPTreeNode* bp_get_child(BPlusTree& tree, BPTreeNode* node, uint32_t index);
BPTreeNode* bp_get_next(BPTreeNode* node);
BPTreeNode* bp_get_prev(BPTreeNode* node);

// Node linking
void bp_set_parent(BPTreeNode* node, uint32_t parent_index);
void bp_set_child(BPlusTree& tree, BPTreeNode* node, uint32_t child_index, uint32_t node_index);
void bp_set_next(BPTreeNode* node, uint32_t index);
void bp_set_prev(BPTreeNode* node, uint32_t index);

// Tree properties
uint32_t bp_get_max_keys(BPlusTree& tree, BPTreeNode* node);
uint32_t bp_get_min_keys(BPlusTree& tree, BPTreeNode* node);
uint32_t bp_get_split_index(BPlusTree& tree, BPTreeNode* node);

// Core operations - data is a buffer containing the record
void bp_insert_element(BPlusTree& tree, uint32_t key, const uint8_t* data);
void bp_delete_element(BPlusTree& tree, uint32_t key);

// Search operations - returns pointer to record data within node
bool bp_find_element(BPlusTree& tree, uint32_t key);
const uint8_t* bp_get(BPlusTree& tree, uint32_t key);
BPTreeNode* bp_find_leaf_node(BPlusTree& tree, BPTreeNode* node, uint32_t key);

// Tree traversal
BPTreeNode* bp_get_root(BPlusTree& tree);
BPTreeNode* bp_left_most(BPlusTree& tree);
std::vector<std::pair<uint32_t, const uint8_t*>> bp_print_leaves(BPlusTree& tree);

// Internal operations
void bp_insert(BPlusTree& tree, BPTreeNode* node, uint32_t key, const uint8_t* data);
void bp_insert_repair(BPlusTree& tree, BPTreeNode* node);
BPTreeNode* bp_split(BPlusTree& tree, BPTreeNode* node);
void bp_do_delete(BPlusTree& tree, BPTreeNode* node, uint32_t key);
void bp_repair_after_delete(BPlusTree& tree, BPTreeNode* node);
BPTreeNode* bp_merge_right(BPlusTree& tree, BPTreeNode* node);
BPTreeNode* bp_steal_from_left(BPlusTree& tree, BPTreeNode* node, uint32_t parent_index);
BPTreeNode* bp_steal_from_right(BPlusTree& tree, BPTreeNode* node, uint32_t parent_index);
void bp_update_parent_keys(BPlusTree& tree, BPTreeNode* node, uint32_t deleted_key);

// Debug operations
void bp_debug_print_tree(BPlusTree& tree);
void bp_debug_print_structure(BPlusTree& tree);
uint64_t debug_hash_tree(BPlusTree& tree);


void bp_debug_capacity_calculation(BPlusTree& tree, const std::vector<ColumnInfo> &schema);
void bp_debug_node_layout(BPlusTree& tree, BPTreeNode* node, std::vector<ColumnInfo>& schema);
std::vector<LeafDataEntry> bp_extract_leaf_data(BPlusTree& tree);
void bp_verify_all_invariants(BPlusTree &tree);

uint32_t *get_key_at(BPlusTree & tree, BPTreeNode *node, uint32_t index);


void validate_bplus_leaf_node(BPlusTree &tree, BPTreeNode *node);
void validate_bplus_internal_node(BPlusTree &tree, BPTreeNode *node);
void validate_btree_node(BPlusTree &tree, BPTreeNode *node);
