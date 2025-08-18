#pragma once
#include "defs.hpp"
#include "btree.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

struct ColumnInfo {
  char name[TYPE_VARCHAR32];
  DataType type;
};

struct TableSchema {
  std::string table_name;
  uint32_t record_size;
  std::vector<ColumnInfo> columns;
  std::vector<uint32_t> column_offsets;

  DataType key_type() const { return columns[0].type; }
};

struct Index {
  BTree tree;
  std::string index_name;
  uint32_t column_index;
};

struct Table {
  TableSchema schema;
  BTree tree;
  std::unordered_map<uint32_t, Index> indexes;
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
uint32_t calculate_record_size(const std::vector<ColumnInfo>& columns);
void calculate_column_offsets(TableSchema* schema);
