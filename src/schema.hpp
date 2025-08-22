// schema.hpp (updated with statistics)
#pragma once
#include "arena.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "str.hpp"

#include <cstdint>

#define TABLE_NAME_SIZE TYPE_32
#define COLUMN_NAME_SIZE TYPE_32
#define MAX_RECORD_LAYOUT 32

struct RegistryArena {};

// ============================================================================
// RecordLayout - Pure record interpretation (VM's concern)
// ============================================================================
struct RecordLayout {
    EmbVec<DataType, MAX_RECORD_LAYOUT> layout;
    EmbVec<uint32_t, MAX_RECORD_LAYOUT> offsets;
    uint32_t record_size;

    DataType key_type() const { return layout[0]; }
    uint32_t column_count() const { return layout.size(); }

    // Factory methods (unchanged)
    static RecordLayout create(EmbVec<DataType, MAX_RECORD_LAYOUT> &column_types);
    static RecordLayout create(DataType key, DataType rec = TYPE_NULL);
    uint32_t get_offset(uint32_t col_index) const { return offsets[col_index]; }
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
// Index - Secondary index metadata
// ============================================================================
struct Index {
    EmbStr<TABLE_NAME_SIZE + 1 + COLUMN_NAME_SIZE> index_name;
    BTree tree;
    uint32_t column_index;
    IndexStats stats; // Cached statistics
};

// ============================================================================
// Table - Complete table with indexes
// ============================================================================
struct Table {
    BTree tree;
    EmbStr<TABLE_NAME_SIZE> table_name;
    Vec<Index, RegistryArena> indexes;
    Vec<ColumnInfo, RegistryArena> columns;
    TableStats stats; // Cached statistics

    RecordLayout to_layout() const {
        EmbVec<DataType, MAX_RECORD_LAYOUT> types;


        for (uint32_t i = 0; i < columns.size(); i++) {
            types.push_back(columns[i].type);
        }
        return RecordLayout::create(types);
    }

    // Mark all statistics as stale (called by VM on modifications)
    void invalidate_stats() {
        stats.mark_stale();
        // for (auto& idx : indexes) {
        for (uint32_t i = 0; i < indexes.size(); i++) {
            indexes[i].stats.mark_stale();
        }
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
void print_table_info(const char *table_name);
void print_all_tables();
bool validate_schema();
