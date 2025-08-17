#pragma once
#include "vm.hpp"
#include <string>
#include <unordered_map>
#include <vector>

struct RegisterAllocator {
  std::unordered_map<std::string, int> name_to_register;
  int next_register = 0;

  int get(const std::string &name);
  void clear();
};

struct Pair {
  uint32_t column_index;
  VMValue value;
};

struct WhereCondition {
  uint32_t column_index;
  CompareOp operator_type;
  VMValue value;
  double selectivity = 0.5;
};

struct OrderBy {
  uint32_t column_index;
  std::string direction; // "ASC" or "DESC"
};

struct SelectOptions {
  std::string table_name;
  std::vector<ColumnInfo> schema;
  std::vector<uint32_t> *column_indices; // nullptr = SELECT *
  std::vector<WhereCondition> where_conditions;
  OrderBy *order_by = nullptr;
};

struct UpdateOptions {
  std::string table_name;
  std::vector<ColumnInfo> schema;
  std::vector<Pair> set_columns;
  std::vector<WhereCondition> where_conditions;
};

struct UnifiedOptions {
  std::string table_name;
  std::vector<ColumnInfo> schema;
  std::vector<Pair> set_columns;
  std::vector<WhereCondition> where_conditions;

  enum Operation { UPDATE, DELETE, SELECT, AGGREGATE } operation;
  std::vector<uint32_t> *select_columns = nullptr;
  OrderBy *order_by = nullptr;
  const char *aggregate_func = nullptr;
  uint32_t *aggregate_column = nullptr;
};

struct AccessMethod {
  enum Type { DIRECT_ROWID, INDEX_SCAN, FULL_TABLE_SCAN } type;
  WhereCondition *primary_condition = nullptr;
  WhereCondition *index_condition = nullptr;
  uint32_t index_col;
};

// Main functions
std::vector<VMInstruction> build_creat_table(const std::string &table_name,
                                        const std::vector<ColumnInfo> &columns);
std::vector<VMInstruction> build_drop_table(const std::string &table_name);
std::vector<VMInstruction> build_drop_index(const std::string &index_name);
std::vector<VMInstruction> build_create_index(const std::string &table_name,
                                        uint32_t column_index,
                                        DataType key_type);

std::vector<VMInstruction> build_insert(const std::string &table_name,
                                  const std::vector<Pair> &values,
                                  bool implicit_begin);

std::vector<VMInstruction> build_select(const SelectOptions &options);
std::vector<VMInstruction> build_update(const UpdateOptions &options,
                                        bool implicit_begin);
std::vector<VMInstruction> build_delete(const UpdateOptions &options,
                                        bool implicit_begin);

std::vector<VMInstruction>
aggregate(const std::string &table_name, const char *agg_func,
          uint32_t *column_index,
          const std::vector<WhereCondition> &where_conditions);
