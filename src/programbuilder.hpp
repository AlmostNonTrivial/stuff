#pragma once
#include "vm.hpp"
#include "parser.hpp"
#include "arena.hpp"
#include "schema.hpp"
#include <utility>


struct SetColumns {
    Str<QueryArena> first;
    TypedValue second;
};


struct WhereCondition {
  Str<QueryArena> column_name;
  uint32_t column_index;
  CompareOp operator_type;
  TypedValue value;
  double selectivity = 0.5;
};

struct OrderBy {
  Str<QueryArena> column_name;
  bool asc; // true for ASC, false for DESC
};

struct ParsedParameters {
  Str<QueryArena> table_name;
  Vector<SetColumns, QueryArena> set_columns;
  Vector<WhereCondition, QueryArena> where_conditions;
  enum Operation { UPDATE, DELETE, SELECT, AGGREGATE } operation;
  Vector<Str<QueryArena>, QueryArena> select_columns;
  OrderBy order_by;
  Str<QueryArena> aggregate;
};
 enum AccessMethodEnum{ DIRECT_ROWID, INDEX_SCAN, FULL_TABLE_SCAN } ;
struct AccessMethod {
AccessMethodEnum type;
  WhereCondition *primary_condition = nullptr;
  WhereCondition *index_condition = nullptr;
  uint32_t index_col;
};

// Main entry points for AST-based building
Vector<VMInstruction, QueryArena> build_from_ast(ASTNode* ast);
