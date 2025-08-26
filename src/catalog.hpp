// schema.hpp - STL container version
#pragma once
#include "btree.hpp"
#include "bplustree.hpp"
#include "defs.hpp"
#include "parser.hpp"
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#define MAX_RECORD_LAYOUT 32

// ============================================================================
// Core Types
// ============================================================================



// RecordLayout - Pure record interpretation
struct RecordLayout
{
    std::vector<DataType> layout;
    std::vector<uint32_t> offsets;
    uint32_t record_size;

    DataType key_type() const
    {
        return layout[0];
    }

    uint32_t column_count() const
    {
        return layout.size();
    }

    uint32_t get_offset(uint32_t col_index) const
    {
        return offsets[col_index];
    }

    static RecordLayout create(std::vector<DataType>& column_types);
    static RecordLayout create(DataType key, DataType rec = TYPE_NULL);
};

// Column metadata
struct ColumnInfo
{
    std::string name;
    DataType type;
};

// Index - Secondary index metadata
struct Index
{
    std::string index_name;
    std::string table_name;


    BTree btree;

    uint32_t column_index;

    RecordLayout to_layout() const;
};

// Table - Complete table metadata
struct Table
{
    std::string table_name;
    std::vector<ColumnInfo> columns;
    std::unordered_map<uint32_t, Index*> indexes;



    BPlusTree bplustree;


    RecordLayout to_layout() const;
};

// Schema snapshot for transaction support

// ============================================================================
// Registry Operations (Used by VM and Executor)
// ============================================================================

Table* get_table(const char* table_name);
Index* get_index(const char* table_name, uint32_t column_index);
Index* get_index(const char* table_name, const char* index_name);
Index* get_index(const char* index_name);
void remove_index(const char* table_name, uint32_t column_index);
void remove_table(const char* table_name);

// ============================================================================
// Schema Queries (Used by VM and Compiler)
// ============================================================================

uint32_t get_column_index(const char* table_name, const char* col_name);
DataType get_column_type(const char* table_name, uint32_t col_index);

// ============================================================================
// Factory Functions (Used by Executor)
// ============================================================================

Table* create_table(CreateTableNode* node, int root_page);
Index* create_index(CreateIndexNode* node, int root_page);
void create_master(bool existed);
// ============================================================================
// Transaction Support (Used by Executor)
// ============================================================================


void schema_clear();
