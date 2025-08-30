#pragma once
#include "arena.hpp"
#include "btree.hpp"
#include "bplustree.hpp"
#include "defs.hpp"
#include "parser.hpp"
#include <cstdint>

// Arena tag for schema storage
struct SchemaArena {};

// Column in a table schema
struct Column {
    const char* name;  // Interned string
    DataType type;
};

// Record layout information
struct RecordLayout {
    array<DataType, SchemaArena> layout;
    array<uint32_t, SchemaArena> offsets;
    uint32_t record_size;

    static RecordLayout create(array<DataType, SchemaArena>& column_types);
    static RecordLayout create(DataType key, DataType rec);
};

// Index metadata
struct Index {
    string<SchemaArena> index_name;
    string<SchemaArena> table_name;
    uint32_t column_index;
    BTree btree;

    RecordLayout to_layout() const;
};

// Table metadata
struct Table {
    string<SchemaArena> table_name;
    array<Column, SchemaArena> columns;
    hash_map<uint32_t, Index*, SchemaArena> indexes;  // column_index -> Index
    BPlusTree bplustree;

    RecordLayout to_layout() const;
};

// ============================================================================
// Schema Operations
// ============================================================================

// Initialize schema system
void schema_init();

// Clear all schema data
void schema_clear();

// Table operations
Table* get_table(const char* table_name);
void remove_table(const char* table_name);

// Index operations
Index* get_index(const char* table_name, uint32_t column_index);
Index* get_index(const char* table_name, const char* index_name);
Index* get_index(const char* index_name);
void remove_index(const char* table_name, uint32_t column_index);

// Schema queries
uint32_t get_column_index(const char* table_name, const char* col_name);
DataType get_column_type(const char* table_name, uint32_t col_index);

// Factory functions
Table* create_table(CreateTableStmt* node, int root_page = 0);
Index* create_index(CreateIndexStmt* node, int root_page = 0);
void create_master(bool existed);
