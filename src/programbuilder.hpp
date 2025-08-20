#pragma once
#include "vm.hpp"
#include "parser.hpp"
#include "arena.hpp"
#include "schema.hpp"
#include <utility>


struct SetColumns {
    ArenaString<QueryArena> first;
    TypedValue second;
};


struct WhereCondition {
  ArenaString<QueryArena> column_name;
  uint32_t column_index;
  CompareOp operator_type;
  TypedValue value;
  double selectivity = 0.5;
};

struct OrderBy {
  ArenaString<QueryArena> column_name;
  bool asc; // true for ASC, false for DESC
};

struct ParsedParameters {
  ArenaString<QueryArena> table_name;
  ArenaVector<SetColumns, QueryArena> set_columns;
  ArenaVector<WhereCondition, QueryArena> where_conditions;
  enum Operation { UPDATE, DELETE, SELECT, AGGREGATE } operation;
  ArenaVector<ArenaString<QueryArena>, QueryArena> select_columns;
  OrderBy order_by;
  ArenaString<QueryArena> aggregate;
};
 enum AccessMethodEnum{ DIRECT_ROWID, INDEX_SCAN, FULL_TABLE_SCAN } ;
struct AccessMethod {
AccessMethodEnum type;
  WhereCondition *primary_condition = nullptr;
  WhereCondition *index_condition = nullptr;
  uint32_t index_col;
};

// Main entry points for AST-based building
ArenaVector<VMInstruction, QueryArena> build_from_ast(ASTNode* ast);
