#include "program_builder.hpp"
#include "arena.hpp"
#include "vm.hpp"
#include <algorithm>
#include <cstring>

// RegisterAllocator implementation
int RegisterAllocator::get(const std::string &name) {
  auto it = name_to_register.find(name);
  if (it == name_to_register.end()) {
    name_to_register[name] = next_register;
    return next_register++;
  }
  return it->second;
}

void RegisterAllocator::clear() {
  name_to_register.clear();
  next_register = 0;
}

// Helper functions
void resolve_labels(std::vector<VMInstruction> &program,
                    const std::unordered_map<std::string, int> &map) {
  for (auto &inst : program) {
    // Check p2 for label (stored as string in p4)
    if (inst.p4 && inst.p2 == -1) {
      auto it = map.find((const char *)inst.p4);
      if (it != map.end()) {
        inst.p2 = it->second;
        inst.p4 = nullptr;
      }
    }
    // Check p3 for label
    if (inst.p4 && inst.p3 == -1) {
      auto it = map.find((const char *)inst.p4);
      if (it != map.end()) {
        inst.p3 = it->second;
        inst.p4 = nullptr;
      }
    }
  }
}

Pair make_pair(uint32_t index, const VMValue &value) { return {index, value}; }

OpCode str_or_int(const VMValue &value) {
  return (value.type == TYPE_INT32 || value.type == TYPE_INT64) ? OP_Integer
                                                                : OP_String;
}

uint8_t set_p5(uint8_t current, uint8_t flag) { return current | flag; }

void add_begin(std::vector<VMInstruction> &instructions) {
  instructions.insert(
      instructions.begin(),
      {.opcode = OP_Begin, .p1 = 0, .p2 = 0, .p3 = 0, .p4 = nullptr, .p5 = 0});
}

void load_value(std::vector<VMInstruction> &instructions, const VMValue &value,
                int target_reg) {
  if (value.type == TYPE_INT32 || value.type == TYPE_INT64) {
    uint32_t val = *(uint32_t *)value.data;
    instructions.push_back({.opcode = OP_Integer,
                            .p1 = target_reg,
                            .p2 = (int32_t)val,
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});
  } else {
    instructions.push_back({.opcode = OP_String,
                            .p1 = target_reg,
                            .p2 = (int32_t)value.type,
                            .p3 = 0,
                            .p4 = value.data,
                            .p5 = 0});
  }
}

OpCode get_negated_opcode(CompareOp op) {
  switch (op) {
  case EQ:
    return OP_Ne;
  case NE:
    return OP_Eq;
  case LT:
    return OP_Ge;
  case LE:
    return OP_Gt;
  case GT:
    return OP_Le;
  case GE:
    return OP_Lt;
  }
  return OP_Eq;
}

OpCode to_seek(CompareOp op) {
  switch (op) {
  case EQ:
    return OP_SeekEQ;
  case GE:
    return OP_SeekGE;
  case GT:
    return OP_SeekGT;
  case LE:
    return OP_SeekLE;
  case LT:
    return OP_SeekLT;
  default:
    return OP_SeekEQ;
  }
}

OpCode to_opcode(CompareOp op) {
  switch (op) {
  case EQ:
    return OP_Eq;
  case NE:
    return OP_Ne;
  case LT:
    return OP_Lt;
  case LE:
    return OP_Le;
  case GT:
    return OP_Gt;
  case GE:
    return OP_Ge;
  }
  return OP_Eq;
}

bool ascending(CompareOp op) { return op == GE || op == GT || op == EQ; }

// Create table
std::vector<VMInstruction>
create_table(const std::string &table_name,
             const std::vector<ColumnInfo> &columns) {
  TableSchema *schema = ARENA_ALLOC(TableSchema);
  schema->table_name = table_name;
  schema->columns = columns;

  return {{OP_CreateTable, 0, 0, 0, schema, 0}, {OP_Halt, 0, 0, 0, nullptr, 0}};
}

// Drop table
std::vector<VMInstruction> drop_table(const std::string &table_name) {
  char *name = (char *)arena_alloc(table_name.size() + 1);
  strcpy(name, table_name.c_str());

  return {{OP_DropTable, 0, 0, 0, name, 0}, {OP_Halt, 0, 0, 0, nullptr, 0}};
}

// Drop index
std::vector<VMInstruction> drop_index(const std::string &index_name) {
  char *name = (char *)arena_alloc(index_name.size() + 1);
  strcpy(name, index_name.c_str());

  return {{OP_DropIndex, 0, 0, 0, name, 0}, {OP_Halt, 0, 0, 0, nullptr, 0}};
}

// Create index
std::vector<VMInstruction> create_index(const std::string &table_name,
                                        uint32_t column_index,
                                        DataType key_type) {
  RegisterAllocator regs;
  std::vector<VMInstruction> instructions;
  std::unordered_map<std::string, int> labels;

  const int table_cursor_id = 0;
  const int index_cursor_id = 1;

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  instructions.push_back({.opcode = OP_CreateIndex,
                          .p1 = 0,
                          .p2 = (int32_t)column_index,
                          .p3 = (int32_t)key_type,
                          .p4 = table_name_str,
                          .p5 = 0});

  instructions.push_back({.opcode = OP_OpenRead,
                          .p1 = 0,
                          .p2 = table_cursor_id,
                          .p3 = 0,
                          .p4 = table_name_str,
                          .p5 = set_p5(0, P5_CURSOR_TABLE)});

  instructions.push_back({.opcode = OP_OpenWrite,
                          .p1 = 0,
                          .p2 = index_cursor_id,
                          .p3 = (int32_t)column_index,
                          .p4 = table_name_str,
                          .p5 = set_p5(0, P5_CURSOR_INDEX)});

  instructions.push_back({.opcode = OP_Rewind,
                          .p1 = table_cursor_id,
                          .p2 = -1,
                          .p3 = 0,
                          .p4 = (void *)"end",
                          .p5 = 0});

  labels["loop_start"] = instructions.size();

  int rowid_reg = regs.get("rowid");
  instructions.push_back({.opcode = OP_Key,
                          .p1 = table_cursor_id,
                          .p2 = rowid_reg,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  int column_reg = regs.get("column_value");
  instructions.push_back({.opcode = OP_Column,
                          .p1 = table_cursor_id,
                          .p2 = (int32_t)column_index,
                          .p3 = column_reg,
                          .p4 = nullptr,
                          .p5 = 0});

  instructions.push_back({.opcode = OP_Insert,
                          .p1 = index_cursor_id,
                          .p2 = column_reg,
                          .p3 = rowid_reg,
                          .p4 = nullptr,
                          .p5 = set_p5(0, P5_INSERT_INDEX)});

  instructions.push_back({.opcode = OP_Next,
                          .p1 = table_cursor_id,
                          .p2 = -1,
                          .p3 = 0,
                          .p4 = (void *)"loop_start",
                          .p5 = 0});

  labels["end"] = instructions.size();

  instructions.push_back({.opcode = OP_Close,
                          .p1 = table_cursor_id,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  instructions.push_back({.opcode = OP_Close,
                          .p1 = index_cursor_id,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  instructions.push_back(
      {.opcode = OP_Halt, .p1 = 0, .p2 = 0, .p3 = 0, .p4 = nullptr, .p5 = 0});

  resolve_labels(instructions, labels);
  return instructions;
}

// Insert
std::vector<VMInstruction> insert(const std::string &table_name,
                                  const std::vector<Pair> &values,
                                  bool implicit_begin) {
  RegisterAllocator regs;
  std::vector<VMInstruction> instructions;

  if (implicit_begin) {
    add_begin(instructions);
  }

  const int table_cursor_id = 0;

  // Get indexes for this table
  auto table_indexes = vm_get_table_indexes(table_name);
  std::unordered_map<uint32_t, std::pair<Index, int>> indexes_to_insert;
  int cursor_id = 1;
  for (const auto &[col, index] : table_indexes) {
    indexes_to_insert[col] = {index, cursor_id++};
  }

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  instructions.push_back({.opcode = OP_OpenWrite,
                          .p1 = 0,
                          .p2 = table_cursor_id,
                          .p3 = 0,
                          .p4 = table_name_str,
                          .p5 = set_p5(0, P5_CURSOR_TABLE)});

  // Open index cursors
  for (const auto &[col_idx, idx_pair] : indexes_to_insert) {
    instructions.push_back({.opcode = OP_OpenWrite,
                            .p1 = 0,
                            .p2 = idx_pair.second,
                            .p3 = (int32_t)col_idx,
                            .p4 = table_name_str,
                            .p5 = set_p5(0, P5_CURSOR_INDEX)});
  }

  // Load values
  std::vector<int> value_regs;
  for (size_t i = 0; i < values.size(); i++) {
    int reg = regs.get("value_" + std::to_string(i));
    value_regs.push_back(reg);

    load_value(instructions, values[i].value, reg);

    // Insert into indexes if needed
    if (indexes_to_insert.find(values[i].column_index) !=
        indexes_to_insert.end()) {
      auto &idx_pair = indexes_to_insert[values[i].column_index];
      instructions.push_back({.opcode = OP_Insert,
                              .p1 = idx_pair.second,
                              .p2 = reg,
                              .p3 = 0,
                              .p4 = nullptr,
                              .p5 = set_p5(0, P5_INSERT_INDEX)});
    }
  }

  int record_reg = regs.get("record");
  instructions.push_back({.opcode = OP_MakeRecord,
                          .p1 = value_regs[0],
                          .p2 = (int32_t)values.size(),
                          .p3 = record_reg,
                          .p4 = nullptr,
                          .p5 = 0});

  instructions.push_back({.opcode = OP_Insert,
                          .p1 = table_cursor_id,
                          .p2 = value_regs[0],
                          .p3 = record_reg,
                          .p4 = nullptr,
                          .p5 = 0});

  instructions.push_back({.opcode = OP_Close,
                          .p1 = table_cursor_id,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  instructions.push_back(
      {.opcode = OP_Halt, .p1 = 0, .p2 = 0, .p3 = 0, .p4 = nullptr, .p5 = 0});

  return instructions;
}

// Optimization functions
double estimate_selectivity(const WhereCondition &condition,
                            const std::vector<ColumnInfo> &schema,
                            const std::string &table_name) {
  std::string column_name = schema[condition.column_index].name;
  std::string index_name = table_name + "." + column_name;

  auto indexes = vm_get_table_indexes(table_name);
  auto it = indexes.find(condition.column_index);
  bool is_indexed = (it != indexes.end());

  switch (condition.operator_type) {
  case EQ:
    if (condition.column_index == 0)
      return 0.001;
    return is_indexed ? 0.01 : 0.1;
  case NE:
    return 0.9;
  case LT:
  case LE:
  case GT:
  case GE:
    return is_indexed ? 0.2 : 0.3;
  default:
    return 0.5;
  }
}

std::vector<WhereCondition>
optimize_where_conditions(const std::vector<WhereCondition> &conditions,
                          const std::vector<ColumnInfo> &schema,
                          const std::string &table_name) {

  std::vector<WhereCondition> optimized = conditions;

  for (auto &cond : optimized) {
    if (cond.selectivity == 0.5) {
      cond.selectivity = estimate_selectivity(cond, schema, table_name);
    }
  }

  std::sort(optimized.begin(), optimized.end(),
            [&](const WhereCondition &a, const WhereCondition &b) {
              if (a.column_index == 0 && a.operator_type == EQ)
                return true;
              if (b.column_index == 0 && b.operator_type == EQ)
                return false;

              auto indexes = vm_get_table_indexes(table_name);
              bool a_indexed =
                  indexes.find(
                      a.column_index) != indexes.end() &&
                  a.operator_type == EQ;
              bool b_indexed =
                  indexes.find(
                      b.column_index) != indexes.end() &&
                  b.operator_type == EQ;

              if (a_indexed && !b_indexed)
                return true;
              if (b_indexed && !a_indexed)
                return false;

              return a.selectivity < b.selectivity;
            });

  return optimized;
}

AccessMethod choose_access_method(const std::vector<WhereCondition> &conditions,
                                  const std::vector<ColumnInfo> &schema,
                                  const std::string &table_name) {
  // Sort conditions to prioritize EQ operations
  std::vector<WhereCondition> sorted_conditions = conditions;
  std::stable_partition(
      sorted_conditions.begin(), sorted_conditions.end(),
      [](const WhereCondition &c) { return c.operator_type == EQ; });

  // Check for direct rowid access
  for (auto &cond : sorted_conditions) {
    if (cond.operator_type == EQ && cond.column_index == 0) {
      return {.type = AccessMethod::DIRECT_ROWID,
              .primary_condition = const_cast<WhereCondition *>(&cond),
              .index_condition = nullptr,
              .index_col = cond.column_index};
    }
  }

  // Check for index scan
  auto indexes = vm_get_table_indexes(table_name);
  for (auto &cond : sorted_conditions) {



    if (indexes.find(cond.column_index) != indexes.end()) {
      return {.type = AccessMethod::INDEX_SCAN,
              .primary_condition = nullptr,
              .index_condition = const_cast<WhereCondition *>(&cond),
              .index_col= cond.column_index};
    }
  }

  return {.type = AccessMethod::FULL_TABLE_SCAN,
          .primary_condition = nullptr,
          .index_condition = nullptr,
        .index_col =0 };
}

void build_where_checks(std::vector<VMInstruction> &instructions, int cursor_id,
                        const std::vector<WhereCondition> &conditions,
                        const std::string &skip_label,
                        RegisterAllocator &regs) {
  for (size_t i = 0; i < conditions.size(); i++) {
    int col_reg = regs.get("where_col_" + std::to_string(i));
    instructions.push_back({.opcode = OP_Column,
                            .p1 = cursor_id,
                            .p2 = (int32_t)conditions[i].column_index,
                            .p3 = col_reg,
                            .p4 = nullptr,
                            .p5 = 0});

    int compare_reg = regs.get("compare_" + std::to_string(i));
    load_value(instructions, conditions[i].value, compare_reg);

    OpCode negated = get_negated_opcode(conditions[i].operator_type);

    char *label_str = (char *)arena_alloc(skip_label.size() + 1);
    strcpy(label_str, skip_label.c_str());

    instructions.push_back({.opcode = negated,
                            .p1 = col_reg,
                            .p2 = -1,
                            .p3 = compare_reg,
                            .p4 = label_str,
                            .p5 = 0});
  }
}

// Generate aggregate instructions
std::vector<VMInstruction> generate_aggregate_instructions(
    const std::string &table_name, const char *agg_func, uint32_t *column_index,
    const std::vector<WhereCondition> &where_conditions) {

  if (strcmp(agg_func, "COUNT") != 0 && column_index == nullptr) {
    // Error: non-COUNT aggregates need a column
    return {};
  }

  if (where_conditions.size() > 0) {
    auto table = vm_get_table(table_name);
    if (!table)
      return {};

    UnifiedOptions options = {.table_name = table_name,
                              .schema = table.schema.columns,
                              .set_columns = {},
                              .where_conditions = where_conditions,
                              .operation = UnifiedOptions::AGGREGATE,
                              .select_columns = nullptr,
                              .order_by = nullptr,
                              .aggregate_func = agg_func,
                              .aggregate_column = column_index};

    return update_or_delete_or_select(options, false);
  }

  // Simple aggregate without WHERE
  RegisterAllocator regs;
  std::vector<VMInstruction> instructions;
  const int cursor_id = 0;
  int agg_reg = regs.get("agg");
  int output_reg = regs.get("output");

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  instructions.push_back({.opcode = OP_OpenRead,
                          .p1 = 0,
                          .p2 = cursor_id,
                          .p3 = 0,
                          .p4 = table_name_str,
                          .p5 = P5_CURSOR_TABLE});

  instructions.push_back({.opcode = OP_AggReset,
                          .p1 = agg_reg,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = (void *)agg_func,
                          .p5 = 0});

  int rewind_jump = 6 + (strcmp(agg_func, "COUNT") == 0 ? 0 : 1);
  instructions.push_back({.opcode = OP_Rewind,
                          .p1 = cursor_id,
                          .p2 = rewind_jump,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  int loop_start = 3;
  if (strcmp(agg_func, "COUNT") != 0) {
    int value_reg = regs.get("value");
    instructions.push_back({.opcode = OP_Column,
                            .p1 = cursor_id,
                            .p2 = (int32_t)*column_index,
                            .p3 = value_reg,
                            .p4 = nullptr,
                            .p5 = 0});
    loop_start = 4;

    instructions.push_back({.opcode = OP_AggStep,
                            .p1 = agg_reg,
                            .p2 = value_reg,
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});
  } else {
    instructions.push_back({.opcode = OP_AggStep,
                            .p1 = agg_reg,
                            .p2 = 0,
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});
  }

  instructions.push_back({.opcode = OP_Next,
                          .p1 = cursor_id,
                          .p2 = loop_start,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  instructions.push_back({.opcode = OP_AggFinal,
                          .p1 = agg_reg,
                          .p2 = output_reg,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  instructions.push_back({.opcode = OP_ResultRow,
                          .p1 = output_reg,
                          .p2 = 1,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  instructions.push_back({.opcode = OP_Close,
                          .p1 = cursor_id,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  instructions.push_back(
      {.opcode = Op_Flush, .p1 = 0, .p2 = 0, .p3 = 0, .p4 = nullptr, .p5 = 0});

  instructions.push_back(
      {.opcode = OP_Halt, .p1 = 0, .p2 = 0, .p3 = 0, .p4 = nullptr, .p5 = 0});

  return instructions;
}

// Main unified function
std::vector<VMInstruction>
update_or_delete_or_select(const UnifiedOptions &options, bool implicit_begin) {
  RegisterAllocator regs;

  // Optimize WHERE conditions
  auto optimized_conditions = optimize_where_conditions(
      options.where_conditions, options.schema, options.table_name);

  // Choose access method
  auto access_method = choose_access_method(optimized_conditions,
                                            options.schema, options.table_name);

  std::vector<VMInstruction> instructions;

  switch (access_method.type) {
  case AccessMethod::DIRECT_ROWID:
    instructions = build_direct_rowid_operation(
        options.table_name, options.schema, options.set_columns,
        *access_method.primary_condition,
        [&]() {
          std::vector<WhereCondition> remaining;
          for (const auto &c : optimized_conditions) {
            if (&c != access_method.primary_condition) {
              remaining.push_back(c);
            }
          }
          return remaining;
        }(),
        regs, options.operation, implicit_begin, options.select_columns,
        options.aggregate_func, options.aggregate_column);
    break;

  case AccessMethod::INDEX_SCAN:
    instructions = build_index_scan_operation(
        options.table_name, options.schema, options.set_columns,
        *access_method.index_condition,
        [&]() {
          std::vector<WhereCondition> remaining;
          for (const auto &c : optimized_conditions) {
            if (&c != access_method.index_condition) {
              remaining.push_back(c);
            }
          }
          return remaining;
        }(),
        access_method.index_col, regs, options.operation, implicit_begin,
        options.select_columns, options.aggregate_func,
        options.aggregate_column);
    break;

  case AccessMethod::FULL_TABLE_SCAN:
  default:
    instructions = build_full_table_scan_operation(
        options.table_name, options.schema, options.set_columns,
        optimized_conditions, regs, options.operation, implicit_begin,
        options.select_columns, options.aggregate_func,
        options.aggregate_column);
  }

  // Add sorting for SELECT operations
  if (options.operation == UnifiedOptions::SELECT && options.order_by) {
    instructions.push_back(
        {.opcode = Op_Sort,
         .p1 = (int32_t)options.order_by->column_index,
         .p2 = (options.order_by->direction == "ASC") ? 0 : 1,
         .p3 = 0,
         .p4 = nullptr,
         .p5 = 0});
  }

  instructions.push_back(
      {.opcode = Op_Flush, .p1 = 0, .p2 = 0, .p3 = 0, .p4 = nullptr, .p5 = 0});

  instructions.push_back(
      {.opcode = OP_Halt, .p1 = 0, .p2 = 0, .p3 = 0, .p4 = nullptr, .p5 = 0});

  return instructions;
}

// Public wrapper functions
std::vector<VMInstruction> select(const SelectOptions &options) {
  UnifiedOptions unified = {.table_name = options.table_name,
                            .schema = options.schema,
                            .set_columns = {},
                            .where_conditions = options.where_conditions,
                            .operation = UnifiedOptions::SELECT,
                            .select_columns = options.column_indices,
                            .order_by = options.order_by,
                            .aggregate_func = nullptr,
                            .aggregate_column = nullptr};

  return update_or_delete_or_select(unified, false);
}

std::vector<VMInstruction> update(const UpdateOptions &options,
                                  bool implicit_begin) {
  UnifiedOptions unified = {.table_name = options.table_name,
                            .schema = options.schema,
                            .set_columns = options.set_columns,
                            .where_conditions = options.where_conditions,
                            .operation = UnifiedOptions::UPDATE,
                            .select_columns = nullptr,
                            .order_by = nullptr,
                            .aggregate_func = nullptr,
                            .aggregate_column = nullptr};

  return update_or_delete_or_select(unified, implicit_begin);
}

std::vector<VMInstruction> _delete(const UpdateOptions &options,
                                   bool implicit_begin) {
  UnifiedOptions unified = {.table_name = options.table_name,
                            .schema = options.schema,
                            .set_columns = {},
                            .where_conditions = options.where_conditions,
                            .operation = UnifiedOptions::DELETE,
                            .select_columns = nullptr,
                            .order_by = nullptr,
                            .aggregate_func = nullptr,
                            .aggregate_column = nullptr};

  return update_or_delete_or_select(unified, implicit_begin);
}

// Build operation implementations (I'll include the full_table_scan as example,
// others follow similar pattern)
std::vector<VMInstruction> build_full_table_scan_operation(
    const std::string &table_name, const std::vector<ColumnInfo> &schema,
    const std::vector<Pair> &set_columns,
    const std::vector<WhereCondition> &conditions, RegisterAllocator &regs,
    UnifiedOptions::Operation operation, bool implicit_begin,
    std::vector<uint32_t> *select_columns, const char *aggregate_func,
    uint32_t *aggregate_column) {

  std::vector<VMInstruction> instructions;
  std::unordered_map<std::string, int> labels;
  const int cursor_id = 0;

  if (implicit_begin) {
    add_begin(instructions);
  }

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  // Open cursor
  instructions.push_back({.opcode = (operation == UnifiedOptions::SELECT ||
                                     operation == UnifiedOptions::AGGREGATE)
                                        ? OP_OpenRead
                                        : OP_OpenWrite,
                          .p1 = 0,
                          .p2 = cursor_id,
                          .p3 = 0,
                          .p4 = table_name_str,
                          .p5 = set_p5(0, P5_CURSOR_TABLE)});

  // Initialize aggregate if needed
  int agg_reg = -1;
  if (operation == UnifiedOptions::AGGREGATE) {
    agg_reg = regs.get("agg");
    instructions.push_back({.opcode = OP_AggReset,
                            .p1 = agg_reg,
                            .p2 = 0,
                            .p3 = 0,
                            .p4 = (void *)aggregate_func,
                            .p5 = 0});
  }

  // Load comparison values
  for (size_t i = 0; i < conditions.size(); i++) {
    int reg = regs.get("compare_" + std::to_string(i));
    load_value(instructions, conditions[i].value, reg);
  }

  // Rewind cursor
  instructions.push_back({.opcode = OP_Rewind,
                          .p1 = cursor_id,
                          .p2 = -1,
                          .p3 = 0,
                          .p4 = (void *)"end",
                          .p5 = 0});

  labels["loop_start"] = instructions.size();

  // Check conditions
  build_where_checks(instructions, cursor_id, conditions, "next_record", regs);

  // Perform operation
  if (operation == UnifiedOptions::DELETE) {
    instructions.push_back({.opcode = OP_Delete,
                            .p1 = cursor_id,
                            .p2 = 0,
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});
  } else if (operation == UnifiedOptions::AGGREGATE) {
    if (strcmp(aggregate_func, "COUNT") != 0 && aggregate_column != nullptr) {
      int value_reg = regs.get("agg_value");
      instructions.push_back({.opcode = OP_Column,
                              .p1 = cursor_id,
                              .p2 = (int32_t)*aggregate_column,
                              .p3 = value_reg,
                              .p4 = nullptr,
                              .p5 = 0});
      instructions.push_back({.opcode = OP_AggStep,
                              .p1 = agg_reg,
                              .p2 = value_reg,
                              .p3 = 0,
                              .p4 = nullptr,
                              .p5 = 0});
    } else {
      instructions.push_back({.opcode = OP_AggStep,
                              .p1 = agg_reg,
                              .p2 = 0,
                              .p3 = 0,
                              .p4 = nullptr,
                              .p5 = 0});
    }
  } else if (operation == UnifiedOptions::SELECT) {
    std::vector<int> output_regs;
    std::vector<uint32_t> columns_to_select;

    if (select_columns) {
      columns_to_select = *select_columns;
    } else {
      for (size_t i = 0; i < schema.size(); i++) {
        columns_to_select.push_back(i);
      }
    }

    for (size_t i = 0; i < columns_to_select.size(); i++) {
      int col_reg = regs.get("output_col_" + std::to_string(i));
      output_regs.push_back(col_reg);
      instructions.push_back({.opcode = OP_Column,
                              .p1 = cursor_id,
                              .p2 = (int32_t)columns_to_select[i],
                              .p3 = col_reg,
                              .p4 = nullptr,
                              .p5 = 0});
    }

    instructions.push_back({.opcode = OP_ResultRow,
                            .p1 = output_regs[0],
                            .p2 = (int32_t)output_regs.size(),
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});
  } else {
    // UPDATE
    std::vector<int> current_regs;
    for (size_t i = 0; i < schema.size(); i++) {
      int col_reg = regs.get("current_col_" + std::to_string(i));
      current_regs.push_back(col_reg);
      instructions.push_back({.opcode = OP_Column,
                              .p1 = cursor_id,
                              .p2 = (int32_t)i,
                              .p3 = col_reg,
                              .p4 = nullptr,
                              .p5 = 0});
    }

    // Update columns
    for (const auto &set_col : set_columns) {
      int reg = regs.get("update_col_" + std::to_string(set_col.column_index));
      load_value(instructions, set_col.value, reg);
      instructions.push_back({.opcode = OP_Move,
                              .p1 = reg,
                              .p2 = current_regs[set_col.column_index],
                              .p3 = 0,
                              .p4 = nullptr,
                              .p5 = 0});
    }

    int record_reg = regs.get("record");
    instructions.push_back({.opcode = OP_MakeRecord,
                            .p1 = current_regs[0],
                            .p2 = (int32_t)schema.size(),
                            .p3 = record_reg,
                            .p4 = nullptr,
                            .p5 = 0});

    int rowid_reg = regs.get("rowid");
    instructions.push_back({.opcode = OP_Key,
                            .p1 = cursor_id,
                            .p2 = rowid_reg,
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});

    instructions.push_back({.opcode = OP_Insert,
                            .p1 = cursor_id,
                            .p2 = rowid_reg,
                            .p3 = record_reg,
                            .p4 = nullptr,
                            .p5 = 0});
  }

  labels["next_record"] = instructions.size();

  instructions.push_back({.opcode = OP_Next,
                          .p1 = cursor_id,
                          .p2 = -1,
                          .p3 = 0,
                          .p4 = (void *)"loop_start",
                          .p5 = 0});

  labels["end"] = instructions.size();

  // Finalize aggregate if needed
  if (operation == UnifiedOptions::AGGREGATE) {
    int output_reg = regs.get("output");
    instructions.push_back({.opcode = OP_AggFinal,
                            .p1 = agg_reg,
                            .p2 = output_reg,
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});
    instructions.push_back({.opcode = OP_ResultRow,
                            .p1 = output_reg,
                            .p2 = 1,
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});
  }

  instructions.push_back({.opcode = OP_Close,
                          .p1 = cursor_id,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  resolve_labels(instructions, labels);
  return instructions;
}

// Build direct rowid operation
std::vector<VMInstruction> build_direct_rowid_operation(
    const std::string &table_name, const std::vector<ColumnInfo> &schema,
    const std::vector<Pair> &set_columns,
    const WhereCondition &primary_condition,
    const std::vector<WhereCondition> &remaining_conditions,
    RegisterAllocator &regs, UnifiedOptions::Operation operation,
    bool implicit_begin, std::vector<uint32_t> *select_columns,
    const char *aggregate_func, uint32_t *aggregate_column) {

  std::vector<VMInstruction> instructions;
  std::unordered_map<std::string, int> labels;
  const int cursor_id = 0;

  if (implicit_begin) {
    add_begin(instructions);
  }

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  // Open cursor
  instructions.push_back({.opcode = (operation == UnifiedOptions::SELECT ||
                                     operation == UnifiedOptions::AGGREGATE)
                                        ? OP_OpenRead
                                        : OP_OpenWrite,
                          .p1 = 0,
                          .p2 = cursor_id,
                          .p3 = 0,
                          .p4 = table_name_str,
                          ,
                          .p5 = set_p5(0, P5_CURSOR_TABLE)});

  // Initialize aggregate if needed
  int agg_reg = -1;
  if (operation == UnifiedOptions::AGGREGATE) {
    agg_reg = regs.get("agg");
    instructions.push_back({.opcode = OP_AggReset,
                            .p1 = agg_reg,
                            .p2 = 0,
                            .p3 = 0,
                            .p4 = (void *)aggregate_func,
                            .p5 = 0});
  }

  // Only open index cursors for UPDATE
  std::unordered_map<uint32_t, std::pair<Index, int>> indexes_to_update;
  if (operation == UnifiedOptions::UPDATE) {
    auto table_indexes = vm_get_table_indexes(table_name);
    int cursor_idx = 1;
    for (const auto &[column, index] : table_indexes) {
      indexes_to_update[column] = {index, cursor_idx++};
    }

    for (const auto &[ci, idx_pair] : indexes_to_update) {
      instructions.push_back({.opcode = OP_OpenWrite,
                              .p1 = 0,
                              .p2 = idx_pair.second,
                              .p3 = (int32_t)ci,
                              .p4 = table_name_str,
                              .p5 = set_p5(0, P5_CURSOR_INDEX)});
    }
  }

  // Load rowid value
  int rowid_reg = regs.get("rowid_value");
  load_value(instructions, primary_condition.value, rowid_reg);

  // Seek to exact rowid
  instructions.push_back({.opcode = OP_SeekEQ,
                          .p1 = cursor_id,
                          .p2 = rowid_reg,
                          .p3 = -1,
                          .p4 = (void *)"end",
                          .p5 = 0});

  // Check remaining conditions
  build_where_checks(instructions, cursor_id, remaining_conditions, "end",
                     regs);

  // Perform operation
  if (operation == UnifiedOptions::DELETE) {
    instructions.push_back({.opcode = OP_Delete,
                            .p1 = cursor_id,
                            .p2 = 0,
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});
  } else if (operation == UnifiedOptions::AGGREGATE) {
    if (strcmp(aggregate_func, "COUNT") != 0 && aggregate_column != nullptr) {
      int value_reg = regs.get("agg_value");
      instructions.push_back({.opcode = OP_Column,
                              .p1 = cursor_id,
                              .p2 = (int32_t)*aggregate_column,
                              .p3 = value_reg,
                              .p4 = nullptr,
                              .p5 = 0});
      instructions.push_back({.opcode = OP_AggStep,
                              .p1 = agg_reg,
                              .p2 = value_reg,
                              .p3 = 0,
                              .p4 = nullptr,
                              .p5 = 0});
    } else {
      instructions.push_back({.opcode = OP_AggStep,
                              .p1 = agg_reg,
                              .p2 = 0,
                              .p3 = 0,
                              .p4 = nullptr,
                              .p5 = 0});
    }
  } else if (operation == UnifiedOptions::SELECT) {
    // Load and output selected columns
    std::vector<int> output_regs;
    std::vector<uint32_t> columns_to_select;

    if (select_columns) {
      columns_to_select = *select_columns;
    } else {
      for (size_t i = 0; i < schema.size(); i++) {
        columns_to_select.push_back(i);
      }
    }

    for (size_t i = 0; i < columns_to_select.size(); i++) {
      int col_reg = regs.get("output_col_" + std::to_string(i));
      output_regs.push_back(col_reg);
      instructions.push_back({.opcode = OP_Column,
                              .p1 = cursor_id,
                              .p2 = (int32_t)columns_to_select[i],
                              .p3 = col_reg,
                              .p4 = nullptr,
                              .p5 = 0});
    }

    instructions.push_back({.opcode = OP_ResultRow,
                            .p1 = output_regs[0],
                            .p2 = (int32_t)output_regs.size(),
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});
  } else {
    // UPDATE
    // Load current row values
    std::vector<int> current_regs;
    for (size_t i = 0; i < schema.size(); i++) {
      int col_reg = regs.get("current_col_" + std::to_string(i));
      current_regs.push_back(col_reg);
      instructions.push_back({.opcode = OP_Column,
                              .p1 = cursor_id,
                              .p2 = (int32_t)i,
                              .p3 = col_reg,
                              .p4 = nullptr,
                              .p5 = 0});

      // Seek to index entries that need updating
      if (indexes_to_update.find(i) != indexes_to_update.end()) {
        const auto &idx_pair = indexes_to_update[i];
        instructions.push_back({.opcode = OP_SeekEQ,
                                .p1 = idx_pair.second,
                                .p2 = col_reg,
                                .p3 = -1,
                                .p4 = (void *)"end",
                                .p5 = 0});
      }
    }

    // Load new values for updated columns
    for (const auto &set_col : set_columns) {
      int reg = regs.get("update_col_" + std::to_string(set_col.column_index));
      load_value(instructions, set_col.value, reg);
      instructions.push_back({.opcode = OP_Move,
                              .p1 = reg,
                              .p2 = current_regs[set_col.column_index],
                              .p3 = 0,
                              .p4 = nullptr,
                              .p5 = 0});

      // Update index if column is indexed
      if (indexes_to_update.find(set_col.column_index) !=
          indexes_to_update.end()) {
        const auto &idx_pair = indexes_to_update[set_col.column_index];
        // Delete old index entry
        instructions.push_back({.opcode = OP_Delete,
                                .p1 = idx_pair.second,
                                .p2 = 0,
                                .p3 = 0,
                                .p4 = nullptr,
                                .p5 = set_p5(0, P5_INSERT_INDEX)});
        // Insert new index entry
        instructions.push_back({.opcode = OP_Insert,
                                .p1 = idx_pair.second,
                                .p2 = current_regs[set_col.column_index],
                                .p3 = rowid_reg,
                                .p4 = nullptr,
                                .p5 = set_p5(0, P5_INSERT_INDEX)});
      }
    }

    int record_reg = regs.get("record");
    instructions.push_back({.opcode = OP_MakeRecord,
                            .p1 = current_regs[0],
                            .p2 = (int32_t)schema.size(),
                            .p3 = record_reg,
                            .p4 = nullptr,
                            .p5 = 0});

    instructions.push_back({.opcode = OP_Insert,
                            .p1 = cursor_id,
                            .p2 = rowid_reg,
                            .p3 = record_reg,
                            .p4 = nullptr,
                            .p5 = 0});
  }

  // End label
  labels["end"] = instructions.size();

  // Finalize aggregate if needed
  if (operation == UnifiedOptions::AGGREGATE) {
    int output_reg = regs.get("output");
    instructions.push_back({.opcode = OP_AggFinal,
                            .p1 = agg_reg,
                            .p2 = output_reg,
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});
    instructions.push_back({.opcode = OP_ResultRow,
                            .p1 = output_reg,
                            .p2 = 1,
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});
  }

  // Close cursors
  instructions.push_back({.opcode = OP_Close,
                          .p1 = cursor_id,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  if (operation == UnifiedOptions::UPDATE) {
    for (const auto &[ci, idx_pair] : indexes_to_update) {
      instructions.push_back({.opcode = OP_Close,
                              .p1 = idx_pair.second,
                              .p2 = 0,
                              .p3 = 0,
                              .p4 = nullptr,
                              .p5 = 0});
    }
  }

  resolve_labels(instructions, labels);
  return instructions;
}

// Build index scan operation
std::vector<VMInstruction> build_index_scan_operation(
    const std::string &table_name, const std::vector<ColumnInfo> &schema,
    const std::vector<Pair> &set_columns, const WhereCondition &index_condition,
    const std::vector<WhereCondition> &remaining_conditions,
   uint32_t index_col,
    RegisterAllocator &regs,
    UnifiedOptions::Operation operation, bool implicit_begin,
    std::vector<uint32_t> *select_columns, const char *aggregate_func,
    uint32_t *aggregate_column) {

  std::vector<VMInstruction> instructions;
  std::unordered_map<std::string, int> labels;
  const int table_cursor_id = 1;
  const int index_cursor_id = 0;

  if (implicit_begin) {
    add_begin(instructions);
  }

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  // Open index cursor
  instructions.push_back({.opcode = (operation == UnifiedOptions::SELECT ||
                                     operation == UnifiedOptions::AGGREGATE)
                                        ? OP_OpenRead
                                        : OP_OpenWrite,
                          .p1 = 0,
                          .p2 = index_cursor_id,
                          .p3 = (int32_t)index_condition.column_index,
                          .p4 = table_name_str,
                          .p5 = set_p5(0, P5_CURSOR_INDEX)});

  // Open table cursor
  instructions.push_back({.opcode = (operation == UnifiedOptions::SELECT ||
                                     operation == UnifiedOptions::AGGREGATE)
                                        ? OP_OpenRead
                                        : OP_OpenWrite,
                          .p1 = 0,
                          .p2 = table_cursor_id,
                          .p3 = 0,
                          .p4 = table_name_str,
                          .p5 = set_p5(0, P5_CURSOR_TABLE)});

  // Initialize aggregate if needed
  int agg_reg = -1;
  if (operation == UnifiedOptions::AGGREGATE) {
    agg_reg = regs.get("agg");
    instructions.push_back({.opcode = OP_AggReset,
                            .p1 = agg_reg,
                            .p2 = 0,
                            .p3 = 0,
                            .p4 = (void *)aggregate_func,
                            .p5 = 0});
  }

  // Only open additional index cursors for UPDATE
  int ii = 2;
  std::unordered_map<uint32_t, std::pair<Index, int>> indexes_to_update;
  if (operation == UnifiedOptions::UPDATE) {
    auto table_indexes = vm_get_table_indexes(table_name);
    auto table = vm_get_table(table_name);
    for (const auto &[column, index] : table_indexes) {
      auto info = table.schema.columns[column];
      if (column != index_col) {
        indexes_to_update[column] = {index, ii++};
        instructions.push_back({.opcode = OP_OpenWrite,
                                .p1 = 0,
                                .p2 = indexes_to_update[column].second,
                                .p3 = (int32_t)column,
                                .p4 = table_name_str,
                                .p5 = set_p5(0, P5_CURSOR_INDEX)});
      }
    }
  }

  // Load index key value
  int index_key_reg = regs.get("index_key");
  load_value(instructions, index_condition.value, index_key_reg);

  // Seek to first key >= search value
  instructions.push_back({.opcode = to_seek(index_condition.operator_type),
                          .p1 = index_cursor_id,
                          .p2 = index_key_reg,
                          .p3 = -1,
                          .p4 = (void *)"end",
                          .p5 = 0});

  // Loop start
  labels["loop_start"] = instructions.size();

  // Get current index key
  int current_key_reg = regs.get("current_key");
  instructions.push_back({.opcode = OP_Key,
                          .p1 = index_cursor_id,
                          .p2 = current_key_reg,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  // Check index condition
  OpCode negated_op = get_negated_opcode(index_condition.operator_type);
  instructions.push_back({.opcode = negated_op,
                          .p1 = current_key_reg,
                          .p2 = -1,
                          .p3 = index_key_reg,
                          .p4 = (void *)"end",
                          .p5 = 0});

  // Get rowid from index
  int rowid_reg = regs.get("rowid");
  instructions.push_back({.opcode = OP_Column,
                          .p1 = index_cursor_id,
                          .p2 = 0,
                          .p3 = rowid_reg,
                          .p4 = nullptr,
                          .p5 = 0});

  // Seek to table row
  instructions.push_back({.opcode = OP_SeekEQ,
                          .p1 = table_cursor_id,
                          .p2 = rowid_reg,
                          .p3 = -1,
                          .p4 = (void *)"next_iteration",
                          .p5 = 0});

  // Check remaining conditions
  build_where_checks(instructions, table_cursor_id, remaining_conditions,
                     "next_iteration", regs);

  // Perform operation
  if (operation == UnifiedOptions::DELETE) {
    instructions.push_back({.opcode = OP_Delete,
                            .p1 = table_cursor_id,
                            .p2 = 0,
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});
  } else if (operation == UnifiedOptions::AGGREGATE) {
    if (strcmp(aggregate_func, "COUNT") != 0 && aggregate_column != nullptr) {
      int value_reg = regs.get("agg_value");
      instructions.push_back({.opcode = OP_Column,
                              .p1 = table_cursor_id,
                              .p2 = (int32_t)*aggregate_column,
                              .p3 = value_reg,
                              .p4 = nullptr,
                              .p5 = 0});
      instructions.push_back({.opcode = OP_AggStep,
                              .p1 = agg_reg,
                              .p2 = value_reg,
                              .p3 = 0,
                              .p4 = nullptr,
                              .p5 = 0});
    } else {
      instructions.push_back({.opcode = OP_AggStep,
                              .p1 = agg_reg,
                              .p2 = 0,
                              .p3 = 0,
                              .p4 = nullptr,
                              .p5 = 0});
    }
  } else if (operation == UnifiedOptions::SELECT) {
    // Load and output selected columns
    std::vector<int> output_regs;
    std::vector<uint32_t> columns_to_select;

    if (select_columns) {
      columns_to_select = *select_columns;
    } else {
      for (size_t i = 0; i < schema.size(); i++) {
        columns_to_select.push_back(i);
      }
    }

    for (size_t i = 0; i < columns_to_select.size(); i++) {
      int col_reg = regs.get("output_col_" + std::to_string(i));
      output_regs.push_back(col_reg);
      instructions.push_back({.opcode = OP_Column,
                              .p1 = table_cursor_id,
                              .p2 = (int32_t)columns_to_select[i],
                              .p3 = col_reg,
                              .p4 = nullptr,
                              .p5 = 0});
    }

    instructions.push_back({.opcode = OP_ResultRow,
                            .p1 = output_regs[0],
                            .p2 = (int32_t)output_regs.size(),
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});
  } else {
    // UPDATE
    // Load current row values
    std::vector<int> current_regs;
    for (size_t i = 0; i < schema.size(); i++) {
      int col_reg = regs.get("current_col_" + std::to_string(i));
      current_regs.push_back(col_reg);
      instructions.push_back({.opcode = OP_Column,
                              .p1 = table_cursor_id,
                              .p2 = (int32_t)i,
                              .p3 = col_reg,
                              .p4 = nullptr,
                              .p5 = 0});

      // Seek to index entries that need updating
      if (indexes_to_update.find(i) != indexes_to_update.end()) {
        const auto &idx_pair = indexes_to_update[i];
        if (idx_pair.second != index_cursor_id) {
          instructions.push_back({.opcode = OP_SeekEQ,
                                  .p1 = idx_pair.second,
                                  .p2 = col_reg,
                                  .p3 = -1,
                                  .p4 = (void *)"end",
                                  .p5 = 0});
        }
      }
    }

    // Load new values for updated columns
    for (const auto &set_col : set_columns) {
      int reg = regs.get("update_col_" + std::to_string(set_col.column_index));
      load_value(instructions, set_col.value, reg);
      instructions.push_back({.opcode = OP_Move,
                              .p1 = reg,
                              .p2 = current_regs[set_col.column_index],
                              .p3 = 0,
                              .p4 = nullptr,
                              .p5 = 0});

      // Update index if column is indexed
      if (indexes_to_update.find(set_col.column_index) !=
          indexes_to_update.end()) {
        const auto &idx_pair = indexes_to_update[set_col.column_index];
        // Delete old index entry
        instructions.push_back({.opcode = OP_Delete,
                                .p1 = idx_pair.second,
                                .p2 = 0,
                                .p3 = 0,
                                .p4 = nullptr,
                                .p5 = set_p5(0, P5_INSERT_INDEX)});
        // Insert new index entry
        instructions.push_back({.opcode = OP_Insert,
                                .p1 = idx_pair.second,
                                .p2 = current_regs[set_col.column_index],
                                .p3 = rowid_reg,
                                .p4 = nullptr,
                                .p5 = set_p5(0, P5_INSERT_INDEX)});
      }
    }

    int record_reg = regs.get("record");
    instructions.push_back({.opcode = OP_MakeRecord,
                            .p1 = current_regs[0],
                            .p2 = (int32_t)schema.size(),
                            .p3 = record_reg,
                            .p4 = nullptr,
                            .p5 = 0});

    // Insert updated record
    instructions.push_back({.opcode = OP_Insert,
                            .p1 = table_cursor_id,
                            .p2 = rowid_reg,
                            .p3 = record_reg,
                            .p4 = nullptr,
                            .p5 = 0});
  }

  // Next iteration
  labels["next_iteration"] = instructions.size();

  instructions.push_back(
      {.opcode = ascending(index_condition.operator_type) ? OP_Next : OP_Prev,
       .p1 = index_cursor_id,
       .p2 = -1,
       .p3 = 0,
       .p4 = (void *)"loop_start",
       .p5 = 0});

  // End label
  labels["end"] = instructions.size();

  // Finalize aggregate if needed
  if (operation == UnifiedOptions::AGGREGATE) {
    int output_reg = regs.get("output");
    instructions.push_back({.opcode = OP_AggFinal,
                            .p1 = agg_reg,
                            .p2 = output_reg,
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});
    instructions.push_back({.opcode = OP_ResultRow,
                            .p1 = output_reg,
                            .p2 = 1,
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});
  }

  // Close cursors
  instructions.push_back({.opcode = OP_Close,
                          .p1 = index_cursor_id,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  instructions.push_back({.opcode = OP_Close,
                          .p1 = table_cursor_id,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  if (operation == UnifiedOptions::UPDATE) {
    for (const auto &[ci, idx_pair] : indexes_to_update) {
      instructions.push_back({.opcode = OP_Close,
                              .p1 = idx_pair.second,
                              .p2 = 0,
                              .p3 = 0,
                              .p4 = nullptr,
                              .p5 = 0});
    }
  }

  resolve_labels(instructions, labels);
  return instructions;
}
