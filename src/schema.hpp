#pragma once
#include "defs.hpp"
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

void print_record(uint8_t *record, TableSchema *schema);
