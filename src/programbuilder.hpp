#pragma once
#include "vm.hpp"
#include "parser.hpp"
#include "arena.hpp"
#include "schema.hpp"
#include <utility>


struct SetColumns {
    ArenaString<QueryArena> first;
    VMValue second;
};


struct WhereCondition {
  ArenaString<QueryArena> column_name;
  uint32_t column_index;
  CompareOp operator_type;
  VMValue value;
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


// Control flow
inline VMInstruction make_trace(const char *message) {
  return {OP_Trace, 0, 0, 0, (void *)message, 0};
}

inline VMInstruction make_goto(int32_t target) {
  return {OP_Goto, 0, target, 0, nullptr, 0};
}

inline VMInstruction make_halt(int32_t exit_code = 0) {
  return {OP_Halt, exit_code, 0, 0, nullptr, 0};
}

// Cursor operations
inline VMInstruction make_open_read(int32_t cursor_id, const char *table_name, int32_t index_col = 0) {
  return {OP_OpenRead, 0, cursor_id, index_col, (void *)table_name, 0};
}

inline VMInstruction make_open_write(int32_t cursor_id, const char *table_name,
                                     int32_t index_column = 0) {
  return {OP_OpenWrite, 0, cursor_id, index_column, (void *)table_name, 0};
}

inline VMInstruction make_close(int32_t cursor_id) {
  return {OP_Close, cursor_id, 0, 0, nullptr, 0};
}

inline VMInstruction make_rewind(int32_t cursor_id,
                                 int32_t jump_if_empty = -1) {
  return {OP_Rewind, cursor_id, jump_if_empty, 0, nullptr, 0};
}

inline VMInstruction make_next(int32_t cursor_id, int32_t jump_if_done = -1) {
  return {OP_Next, cursor_id, jump_if_done, 0, nullptr, 0};
}

inline VMInstruction make_prev(int32_t cursor_id, int32_t jump_if_done = -1) {
  return {OP_Prev, cursor_id, jump_if_done, 0, nullptr, 0};
}

inline VMInstruction make_first(int32_t cursor_id, int32_t jump_if_empty = -1) {
  return {OP_First, cursor_id, jump_if_empty, 0, nullptr, 0};
}

inline VMInstruction make_last(int32_t cursor_id, int32_t jump_if_empty = -1) {
  return {OP_Last, cursor_id, jump_if_empty, 0, nullptr, 0};
}

// Seek operations
inline VMInstruction make_seek_ge(int32_t cursor_id, int32_t key_reg,
                                  int32_t jump_if_not_found = -1) {
  return {OP_SeekGE, cursor_id, key_reg, jump_if_not_found, nullptr, 0};
}

inline VMInstruction make_seek_gt(int32_t cursor_id, int32_t key_reg,
                                  int32_t jump_if_not_found = -1) {
  return {OP_SeekGT, cursor_id, key_reg, jump_if_not_found, nullptr, 0};
}

inline VMInstruction make_seek_le(int32_t cursor_id, int32_t key_reg,
                                  int32_t jump_if_not_found = -1) {
  return {OP_SeekLE, cursor_id, key_reg, jump_if_not_found, nullptr, 0};
}

inline VMInstruction make_seek_lt(int32_t cursor_id, int32_t key_reg,
                                  int32_t jump_if_not_found = -1) {
  return {OP_SeekLT, cursor_id, key_reg, jump_if_not_found, nullptr, 0};
}

inline VMInstruction make_seek_eq(int32_t cursor_id, int32_t key_reg,
                                  int32_t jump_if_not_found = -1) {
  return {OP_SeekEQ, cursor_id, key_reg, jump_if_not_found, nullptr, 0};
}

// Data operations
inline VMInstruction make_column(int32_t cursor_id, int32_t column_index,
                                 int32_t dest_reg) {
  return {OP_Column, cursor_id, column_index, dest_reg, nullptr, 0};
}

inline VMInstruction make_key(int32_t cursor_id, int32_t dest_reg) {
  return {OP_Column, cursor_id, 0, dest_reg, nullptr, 0};
}

inline VMInstruction make_record(int32_t first_reg, int32_t reg_count,
                                 int32_t dest_reg) {
  return {OP_MakeRecord, first_reg, reg_count, dest_reg, nullptr, 0};
}

inline VMInstruction make_insert(int32_t cursor_id, int32_t key_reg,
                                 int32_t record_reg) {
  return {OP_Insert, cursor_id, key_reg, record_reg, nullptr, 0};
}

inline VMInstruction make_delete(int32_t cursor_id) {
  return {OP_Delete, cursor_id, 0, 0, nullptr, 0};
}

inline VMInstruction make_update(int32_t cursor_id, int32_t record_reg) {
  return {OP_Update, cursor_id, record_reg, 0, nullptr, 0};
}

// Register operations
inline VMInstruction make_integer(int32_t dest_reg, int32_t value) {
  return {OP_Integer, dest_reg, value, 0, nullptr, 0};
}

inline VMInstruction make_string(int32_t dest_reg, int32_t size,
                                 const void *str) {
  return {OP_String, dest_reg, size, 0, (void *)str, 0};
}

inline VMInstruction make_copy(int32_t src_reg, int32_t dest_reg) {
  return {OP_Copy, src_reg, dest_reg, 0, nullptr, 0};
}

inline VMInstruction make_move(int32_t src_reg, int32_t dest_reg) {
  return {OP_Move, src_reg, dest_reg, 0, nullptr, 0};
}

// Comparison operations
inline VMInstruction make_compare(int32_t reg_a, int32_t reg_b) {
  return {OP_Compare, reg_a, 0, reg_b, nullptr, 0};
}

inline VMInstruction make_jump(int32_t jump_lt, int32_t jump_eq,
                               int32_t jump_gt) {
  return {OP_Jump, jump_lt, jump_eq, jump_gt, nullptr, 0};
}

inline VMInstruction make_eq(int32_t reg_a, int32_t reg_b,
                             int32_t jump_target = -1) {
  return {OP_Eq, reg_a, jump_target, reg_b, nullptr, 0};
}

inline VMInstruction make_ne(int32_t reg_a, int32_t reg_b,
                             int32_t jump_target = -1) {
  return {OP_Ne, reg_a, jump_target, reg_b, nullptr, 0};
}

inline VMInstruction make_lt(int32_t reg_a, int32_t reg_b,
                             int32_t jump_target = -1) {
  return {OP_Lt, reg_a, jump_target, reg_b, nullptr, 0};
}

inline VMInstruction make_le(int32_t reg_a, int32_t reg_b,
                             int32_t jump_target = -1) {
  return {OP_Le, reg_a, jump_target, reg_b, nullptr, 0};
}

inline VMInstruction make_gt(int32_t reg_a, int32_t reg_b,
                             int32_t jump_target = -1) {
  return {OP_Gt, reg_a, jump_target, reg_b, nullptr, 0};
}

inline VMInstruction make_ge(int32_t reg_a, int32_t reg_b,
                             int32_t jump_target = -1) {
  return {OP_Ge, reg_a, jump_target, reg_b, nullptr, 0};
}

// Results
inline VMInstruction make_result_row(int32_t first_reg, int32_t reg_count) {
  return {OP_ResultRow, first_reg, reg_count, 0, nullptr, 0};
}

// Schema operations
inline VMInstruction make_create_table(TableSchema *schema) {
  return {OP_CreateTable, 0, 0, 0, schema, 0};
}

inline VMInstruction make_drop_table(const char *table_name) {
  return {OP_DropTable, 0, 0, 0, (void *)table_name, 0};
}

inline VMInstruction make_create_index(int32_t column_index,
                                       const char *table_name) {
  return {OP_CreateIndex, column_index, 0, 0, (void *)table_name, 0};
}

inline VMInstruction make_drop_index(int32_t column_index,
                                     const char *table_name) {
  return {OP_DropIndex, column_index, 0, 0, (void *)table_name, 0};
}

// Transactions
inline VMInstruction make_begin() { return {OP_Begin, 0, 0, 0, nullptr, 0}; }

inline VMInstruction make_commit() { return {OP_Commit, 0, 0, 0, nullptr, 0}; }

inline VMInstruction make_rollback() {
  return {OP_Rollback, 0, 0, 0, nullptr, 0};
}

// Aggregation
inline VMInstruction make_agg_reset(const char *function_name) {
  return {OP_AggReset, 0, 0, 0, (void *)function_name, 0};
}

inline VMInstruction make_agg_step(int32_t value_reg = -1) {
  return {OP_AggStep, value_reg, 0, 0, nullptr, 0};
}

inline VMInstruction make_agg_final(int32_t dest_reg) {
  return {OP_AggFinal, dest_reg, 0, 0, nullptr, 0};
}

// Sorting and output
inline VMInstruction make_sort(int32_t column_index, bool descending = false) {
  return {Op_Sort, column_index, descending ? 1 : 0, 0, nullptr, 0};
}

inline VMInstruction make_flush() { return {Op_Flush, 0, 0, 0, nullptr, 0}; }

// Label-based factories for jump instructions (for use with label resolution)
inline VMInstruction make_goto_label(const char *label) {
  return {OP_Goto, 0, -1, 0, (void *)label, 0};
}

inline VMInstruction make_rewind_label(int32_t cursor_id, const char *label) {
  return {OP_Rewind, cursor_id, -1, 0, (void *)label, 0};
}

inline VMInstruction make_next_label(int32_t cursor_id, const char *label) {
  return {OP_Next, cursor_id, -1, 0, (void *)label, 0};
}

inline VMInstruction make_prev_label(int32_t cursor_id, const char *label) {
  return {OP_Prev, cursor_id, -1, 0, (void *)label, 0};
}

inline VMInstruction make_seek_ge_label(int32_t cursor_id, int32_t key_reg,
                                        const char *label) {
  return {OP_SeekGE, cursor_id, key_reg, -1, (void *)label, 0};
}

inline VMInstruction make_seek_gt_label(int32_t cursor_id, int32_t key_reg,
                                        const char *label) {
  return {OP_SeekGT, cursor_id, key_reg, -1, (void *)label, 0};
}

inline VMInstruction make_seek_le_label(int32_t cursor_id, int32_t key_reg,
                                        const char *label) {
  return {OP_SeekLE, cursor_id, key_reg, -1, (void *)label, 0};
}

inline VMInstruction make_seek_lt_label(int32_t cursor_id, int32_t key_reg,
                                        const char *label) {
  return {OP_SeekLT, cursor_id, key_reg, -1, (void *)label, 0};
}

inline VMInstruction make_seek_eq_label(int32_t cursor_id, int32_t key_reg,
                                        const char *label) {
  return {OP_SeekEQ, cursor_id, key_reg, -1, (void *)label, 0};
}

inline VMInstruction make_eq_label(int32_t reg_a, int32_t reg_b,
                                   const char *label) {
  return {OP_Eq, reg_a, -1, reg_b, (void *)label, 0};
}

inline VMInstruction make_ne_label(int32_t reg_a, int32_t reg_b,
                                   const char *label) {
  return {OP_Ne, reg_a, -1, reg_b, (void *)label, 0};
}

inline VMInstruction make_lt_label(int32_t reg_a, int32_t reg_b,
                                   const char *label) {
  return {OP_Lt, reg_a, -1, reg_b, (void *)label, 0};
}

inline VMInstruction make_le_label(int32_t reg_a, int32_t reg_b,
                                   const char *label) {
  return {OP_Le, reg_a, -1, reg_b, (void *)label, 0};
}

inline VMInstruction make_gt_label(int32_t reg_a, int32_t reg_b,
                                   const char *label) {
  return {OP_Gt, reg_a, -1, reg_b, (void *)label, 0};
}

inline VMInstruction make_ge_label(int32_t reg_a, int32_t reg_b,
                                   const char *label) {
  return {OP_Ge, reg_a, -1, reg_b, (void *)label, 0};
}
