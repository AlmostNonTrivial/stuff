#pragma once
#include "arena.hpp"
#include "defs.hpp"
#include "btree.hpp"
#include <cstdint>

struct SchemaArena {};

struct ColumnInfo {
  ArenaString<SchemaArena> name;
  DataType type;
};

struct TableSchema {
  ArenaString<SchemaArena> table_name;
  uint32_t record_size;
  ArenaVector<ColumnInfo, SchemaArena> columns;
  ArenaVector<uint32_t, SchemaArena> column_offsets;
  DataType key_type() const { return columns[0].type; }
};

struct Index {
  BTree tree;
  ArenaString<SchemaArena> index_name;
  uint32_t column_index;
};

struct Table {
  TableSchema schema;
  BTree tree;
  ArenaMap<uint32_t, Index, SchemaArena> indexes;
};

// Schema registry functions
Table* get_table(const char* table_name);
Index* get_index(const char* table_name, uint32_t column_index);
uint32_t get_column_index(const char* table_name, const char* col_name);
DataType get_column_type(const char* table_name, uint32_t col_index);

// Schema manipulation (for executor use)
bool add_table(Table* table);
bool remove_table(const char* table_name);
bool add_index(const char* table_name, Index* index);
bool remove_index(const char* table_name, uint32_t column_index);
void clear_schema();

// Utility functions
void print_record(uint8_t* record, TableSchema* schema);
uint32_t calculate_record_size(const ArenaVector<ColumnInfo, SchemaArena>& columns);
void calculate_column_offsets(TableSchema* schema);
