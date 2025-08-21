#pragma once
#include "arena.hpp"
#include "defs.hpp"
#include "btree.hpp"
#include <cstdint>

struct RegistryArena {};

struct ColumnInfo {
  Str<RegistryArena> name;
  DataType type;
};

struct Schema {
  Str<RegistryArena> table_name;
  uint32_t record_size;
  Vec<ColumnInfo, RegistryArena> columns;
  Vec<uint32_t, RegistryArena> column_offsets;
  DataType key_type() const { return columns[0].type; }
};

struct Index {
  BTree tree;
  Str<RegistryArena> index_name;
  uint32_t column_index;
};

struct Table {
  Schema schema;
  BTree tree;
  Vec<Index, RegistryArena> indexes;
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
void print_record(uint8_t* record, Schema* schema);
uint32_t calculate_record_size(const Vec<ColumnInfo, RegistryArena>& columns);
void calculate_column_offsets(Schema* schema);
