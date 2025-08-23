// schema.hpp - Updated with support for both tree types
#pragma once
#include "arena.hpp"
#include "btree.hpp"
#include "bplustree.hpp"
#include "defs.hpp"
#include "str.hpp"
#include "vec.hpp"
#include <cstdint>

#define TABLE_NAME_SIZE TYPE_32
#define COLUMN_NAME_SIZE TYPE_32
#define MAX_RECORD_LAYOUT 32

DataType get_column_type(const char *table_name, uint32_t col_index);

struct RegistryArena {};

// Tree type enumeration
enum class TreeType {
	BTREE,      // Regular B-tree
	BPLUSTREE   // B+ tree
};

// ============================================================================
// RecordLayout - Pure record interpretation (VM's concern)
// ============================================================================
struct RecordLayout {
	EmbVec<DataType, MAX_RECORD_LAYOUT> layout;
	EmbVec<uint32_t, MAX_RECORD_LAYOUT> offsets;
	uint32_t record_size;

	DataType key_type() const { return layout[0]; }
	uint32_t column_count() const { return layout.size(); }

	// Factory methods
	static RecordLayout create(EmbVec<DataType, MAX_RECORD_LAYOUT> &column_types);
	static RecordLayout create(DataType key, DataType rec = TYPE_NULL);

	uint32_t get_offset(uint32_t col_index) const {
		return col_index < offsets.size() ? offsets[col_index] : 0;
	}
};

// ============================================================================
// Table Statistics - Cached from last ANALYZE
// ============================================================================
struct TableStats {
	uint32_t row_count = 0;
	uint32_t page_count = 0;
	uint32_t total_size = 0;    // In bytes
	uint64_t last_analyzed = 0; // Timestamp
	bool is_stale = true;       // True if data changed since last analyze

	void mark_stale() { is_stale = true; }
	void clear() { *this = TableStats{}; }
};

struct IndexStats {
	uint32_t distinct_keys = 0;
	uint32_t tree_depth = 0;
	uint32_t page_count = 0;
	double selectivity = 1.0; // Fraction of distinct values
	bool is_stale = true;

	void mark_stale() { is_stale = true; }
	void clear() { *this = IndexStats{}; }
};

// ============================================================================
// Schema - Persistent table metadata (Storage concern)
// ============================================================================
struct ColumnInfo {
	EmbStr<COLUMN_NAME_SIZE> name;
	DataType type;
};

// ============================================================================
// Index - Secondary index metadata (supports both tree types)
// ============================================================================
struct Index {
	EmbStr<TABLE_NAME_SIZE> index_name;
	TreeType tree_type;
	union {
		BTree btree;
		BPlusTree bplustree;
	} tree;
	uint32_t column_index;
	IndexStats stats; // Cached statistics

	RecordLayout to_layout() const {
		DataType key = get_column_type(nullptr, column_index); // Need table ref
		return RecordLayout::create(key, TYPE_4); // TYPE_4 for rowid
	}

	// Helper to get appropriate tree pointer
	void* get_tree_ptr() {
		return tree_type == TreeType::BTREE
			? static_cast<void*>(&tree.btree)
			: static_cast<void*>(&tree.bplustree);
	}

	bool is_btree() const { return tree_type == TreeType::BTREE; }
	bool is_bplustree() const { return tree_type == TreeType::BPLUSTREE; }
};

// ============================================================================
// Table - Complete table metadata (supports both tree types)
// ============================================================================
struct Table {
	EmbStr<TABLE_NAME_SIZE> table_name;
	Vec<ColumnInfo, RegistryArena> columns;
	Vec<Index, RegistryArena> indexes;
	TreeType tree_type;
	union {
		BTree btree;
		BPlusTree bplustree;
	} tree;
	TableStats stats; // Cached statistics

	RecordLayout to_layout() const {
		EmbVec<DataType, MAX_RECORD_LAYOUT> types;
		for (size_t i = 0; i < columns.size(); i++) {
			types.push_back(columns[i].type);
		}
		return RecordLayout::create(types);
	}

	// Helper to get appropriate tree pointer
	void* get_tree_ptr() {
		return tree_type == TreeType::BTREE
			? static_cast<void*>(&tree.btree)
			: static_cast<void*>(&tree.bplustree);
	}

	bool is_btree() const { return tree_type == TreeType::BTREE; }
	bool is_bplustree() const { return tree_type == TreeType::BPLUSTREE; }

	// Create index with specified tree type
	bool create_index(const char* index_name, uint32_t col_index, TreeType idx_tree_type) {
		Index idx;
		idx.index_name = index_name;
		idx.column_index = col_index;
		idx.tree_type = idx_tree_type;

		DataType key_type = columns[col_index].type;
		if (idx_tree_type == TreeType::BTREE) {
			idx.tree.btree = btree_create(key_type, TYPE_4);
		} else {
			idx.tree.bplustree = bplustree_create(key_type, TYPE_4);
		}

		indexes.push_back(idx);
		return true;
	}
};

// ============================================================================
// Schema Registry Functions
// ============================================================================
Table *get_table(const char *table_name);
Index *get_index(const char *table_name, uint32_t column_index);
uint32_t get_column_index(const char *table_name, const char *col_name);
DataType get_column_type(const char *table_name, uint32_t col_index);

bool add_table(Table *table);
bool remove_table(const char *table_name);
bool add_index(const char *table_name, Index *index);
bool remove_index(const char *table_name, uint32_t column_index);
void clear_schema();

// ============================================================================
// Statistics Management (called by VM/Executor)
// ============================================================================
void update_table_stats(const char *table_name, const TableStats &stats);
void update_index_stats(const char *table_name, uint32_t column_index, const IndexStats &stats);
void invalidate_table_stats(const char *table_name);
TableStats *get_table_stats(const char *table_name);
IndexStats *get_index_stats(const char *table_name, uint32_t column_index);

// ============================================================================
// Utility Functions
// ============================================================================
void print_record(uint8_t *record, const RecordLayout *layout);
void print_record_with_names(uint8_t *key, uint8_t *record, const Table *table);
void print_table_info(const char *table_name);
void print_all_tables();
bool validate_schema();
size_t get_table_size(const char *table_name);
RecordLayout build_layout_from_columns(const char *table_name,
                                       const Vec<const char *, QueryArena> &column_names);
Vec<const char *, QueryArena> get_all_table_names();
bool column_exists_anywhere(const char *col_name);
uint32_t total_index_count();
bool stats_are_fresh(const char *table_name);
