#pragma once
#include "defs.hpp"
#include "btree.hpp"
#include <cstdint>


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
};

struct Table {
  TableSchema schema;
  BTree tree;
  std::unordered_map<uint32_t, Index> indexes;
};



Table* get_table(char * table_name);
Index* get_index(char *table_name, uint32_t column_index);
uint32_t get_column_index(char * table_name, char* col_name);

void print_record(uint8_t *record, TableSchema *schema);
