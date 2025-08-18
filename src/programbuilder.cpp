#include "programbuilder.hpp"
#include "defs.hpp"
#include "schema.hpp"
#include "vm.hpp"





std::vector<VMInstruction> build_direct_rowid_operation(
    const std::string &table_name, const std::vector<ColumnInfo> &schema,
    const std::vector<SET_PAIR> &set_columns,
    const WhereCondition &primary_condition,
    const std::vector<WhereCondition> &remaining_conditions,
    RegisterAllocator &regs, ParsedParameters::Operation operation,
    bool implicit_begin, std::vector<uint32_t> *select_columns,
    const char *aggregate_func, uint32_t *aggregate_column);

std::vector<VMInstruction> build_index_scan_operation(
    const std::string &table_name, const std::vector<ColumnInfo> &schema,
    const std::vector<SET_PAIR> &set_columns, const WhereCondition &index_condition,
    const std::vector<WhereCondition> &remaining_conditions, uint32_t index_col,
    RegisterAllocator &regs, ParsedParameters::Operation operation,
    bool implicit_begin, std::vector<uint32_t> *select_columns,
    const char *aggregate_func, uint32_t *aggregate_column);

std::vector<VMInstruction> build_full_table_scan_operation(
    const std::string &table_name,
    const std::vector<ColumnInfo> &schema,
    const std::vector<SET_PAIR> &set_columns,
    const std::vector<WhereCondition> &conditions, RegisterAllocator &regs,
    ParsedParameters::Operation operation, bool implicit_begin,
    std::vector<uint32_t> *select_columns, const char *aggregate_func,
    uint32_t *aggregate_column);

std::vector<VMInstruction>
update_or_delete_or_select(const ParsedParameters &options, bool implicit_begin);

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



OpCode str_or_int(const VMValue &value) {
  return (value.type == TYPE_INT32 || value.type == TYPE_INT64) ? OP_Integer
                                                                : OP_String;
}

uint8_t set_p5(uint8_t current, uint8_t flag) { return current | flag; }

void add_begin(std::vector<VMInstruction> &instructions) {
  instructions.insert(instructions.begin(), make_begin());
}

void load_value(std::vector<VMInstruction> &instructions, const VMValue &value,
                int target_reg) {
  if (value.type == TYPE_INT32 || value.type == TYPE_INT64) {
    uint32_t val = *(uint32_t *)value.data;
    instructions.push_back(make_integer(target_reg, (int32_t)val));
  } else {
    instructions.push_back(make_string(target_reg, (int32_t)value.type, value.data));
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
build_creat_table(const std::string &table_name,
             const std::vector<ColumnInfo> &columns) {
  TableSchema *schema = ARENA_ALLOC(TableSchema);
  schema->table_name = table_name;
  schema->columns = columns;
  schema->column_offsets.resize(columns.size());

  return {make_create_table(schema), make_halt()};
}

// Drop table
std::vector<VMInstruction> build_drop_table(const std::string &table_name) {
  char *name = (char *)arena_alloc(table_name.size() + 1);
  strcpy(name, table_name.c_str());

  return {make_drop_table(name), make_halt()};
}

// Drop index
std::vector<VMInstruction> build_drop_index(const std::string &index_name) {
  char *name = (char *)arena_alloc(index_name.size() + 1);
  strcpy(name, index_name.c_str());

  return {make_drop_index(0, name), make_halt()};
}

// Create index
std::vector<VMInstruction> build_create_index(const std::string &table_name,
                                        uint32_t column_index,
                                        DataType key_type) {
  RegisterAllocator regs;
  std::vector<VMInstruction> instructions;
  std::unordered_map<std::string, int> labels;

  const int table_cursor_id = 0;
  const int index_cursor_id = 1;

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  instructions.push_back(make_create_index(column_index, table_name_str));
  instructions.push_back(make_open_read(table_cursor_id, table_name_str));
  instructions.push_back(make_open_write(index_cursor_id, table_name_str, column_index));

  instructions.push_back(make_rewind_label(table_cursor_id, "end"));

  labels["loop_start"] = instructions.size();

  int rowid_reg = regs.get("rowid");
  instructions.push_back(make_key(table_cursor_id, rowid_reg));

  int column_reg = regs.get("column_value");
  instructions.push_back(make_column(table_cursor_id, (int32_t)column_index, column_reg));
  instructions.push_back(make_insert(index_cursor_id, column_reg, rowid_reg));
  instructions.push_back(make_next_label(table_cursor_id, "loop_start"));

  labels["end"] = instructions.size();

  instructions.push_back(make_close(table_cursor_id));
  instructions.push_back(make_close(index_cursor_id));
  instructions.push_back(make_halt());

  resolve_labels(instructions, labels);
  return instructions;
}

// Insert
std::vector<VMInstruction> build_insert(const std::string &table_name,
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

  instructions.push_back(make_open_write(table_cursor_id, table_name_str));

  // Open index cursors
  for (const auto &[col_idx, idx_pair] : indexes_to_insert) {
    instructions.push_back(make_open_write(idx_pair.second, table_name_str, col_idx));
  }

  // Load values
  std::vector<int> value_regs;
  for (size_t i = 0; i < values.size(); i++) {
    int reg = regs.get("value_" + std::to_string(i));
    value_regs.push_back(reg);

    load_value(instructions, values[i].value, reg);

    // Insert into indexes if needed
    if (indexes_to_insert.find(values[i].column_index) != indexes_to_insert.end()) {
      auto &idx_pair = indexes_to_insert[values[i].column_index];
      instructions.push_back(make_insert(idx_pair.second, reg, 0));
    }
  }

  int record_reg = regs.get("record");
  instructions.push_back(make_record(value_regs[0], (int32_t)values.size(), record_reg));
  instructions.push_back(make_insert(table_cursor_id, value_regs[0], record_reg));
  instructions.push_back(make_close(table_cursor_id));
  instructions.push_back(make_halt());

  return instructions;
}

// Build where checks helper
void build_where_checks(std::vector<VMInstruction> &instructions, int cursor_id,
                        const std::vector<WhereCondition> &conditions,
                        const std::string &skip_label,
                        RegisterAllocator &regs) {
  for (size_t i = 0; i < conditions.size(); i++) {
    int col_reg = regs.get("where_col_" + std::to_string(i));
    instructions.push_back(make_column(cursor_id, (int32_t)conditions[i].column_index, col_reg));

    int compare_reg = regs.get("compare_" + std::to_string(i));
    load_value(instructions, conditions[i].value, compare_reg);

    OpCode negated = get_negated_opcode(conditions[i].operator_type);

    char *label_str = (char *)arena_alloc(skip_label.size() + 1);
    strcpy(label_str, skip_label.c_str());

    // Build the appropriate comparison with label
    switch(negated) {
      case OP_Eq:
        instructions.push_back(make_eq_label(col_reg, compare_reg, label_str));
        break;
      case OP_Ne:
        instructions.push_back(make_ne_label(col_reg, compare_reg, label_str));
        break;
      case OP_Lt:
        instructions.push_back(make_lt_label(col_reg, compare_reg, label_str));
        break;
      case OP_Le:
        instructions.push_back(make_le_label(col_reg, compare_reg, label_str));
        break;
      case OP_Gt:
        instructions.push_back(make_gt_label(col_reg, compare_reg, label_str));
        break;
      case OP_Ge:
        instructions.push_back(make_ge_label(col_reg, compare_reg, label_str));
        break;
    }
  }
}

// Generate aggregate instructions
std::vector<VMInstruction>
aggregate(const std::string &table_name, const char *agg_func,
          uint32_t *column_index,
          const std::vector<WhereCondition> &where_conditions) {

  if (strcmp(agg_func, "COUNT") != 0 && column_index == nullptr) {
    return {};
  }

  if (where_conditions.size() > 0) {
    auto table = vm_get_table(table_name);

    ParsedParameters options = {.table_name = table_name,
                              .schema = table.schema.columns,
                              .set_columns = {},
                              .where_conditions = where_conditions,
                              .operation = ParsedParameters::AGGREGATE,
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

  instructions.push_back(make_open_read(cursor_id, table_name_str));
  instructions.push_back(make_agg_reset(agg_func));

  int rewind_jump = 6 + (strcmp(agg_func, "COUNT") == 0 ? 0 : 1);
  instructions.push_back(make_rewind(cursor_id, rewind_jump));

  int loop_start = 3;
  if (strcmp(agg_func, "COUNT") != 0) {
    int value_reg = regs.get("value");
    instructions.push_back(make_column(cursor_id, (int32_t)*column_index, value_reg));
    loop_start = 4;
    instructions.push_back(make_agg_step(value_reg));
  } else {
    instructions.push_back(make_agg_step());
  }

  instructions.push_back(make_next(cursor_id, loop_start));
  instructions.push_back(make_agg_final(output_reg));
  instructions.push_back(make_result_row(output_reg, 1));
  instructions.push_back(make_close(cursor_id));
  instructions.push_back(make_flush());
  instructions.push_back(make_halt());

  return instructions;
}

// Optimization functions (unchanged)
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

    bool is_equal = false;
    bool has_index = false;
    WhereCondition cond;
    for(
        int i = 0; i < conditions.size(); i++
    ) {
        if(vm_get_index(table_name, conditions[i].column_name) != nullptr) {
            if(!has_index) {
                cond = conditions[i];
                has_index = true;
            } else if (has_index && !is_equal && conditions[i].operator_type == EQ) {
                cond = conditions[i];
                is_equal = true;
                WhereCondition last = conditions[i];
                // conditions[] = conditions[0];

                break;
            }

        }
    }

  return optimized;
}

AccessMethod choose_access_method(const std::vector<WhereCondition> &conditions,
                                  const std::vector<ColumnInfo> &schema,
                                  const std::string &table_name) {
  std::vector<WhereCondition> sorted_conditions = conditions;
  std::stable_partition(
      sorted_conditions.begin(), sorted_conditions.end(),
      [](const WhereCondition &c) { return c.operator_type == EQ; });

  for (auto &cond : sorted_conditions) {
    if (cond.operator_type == EQ && cond.column_index == 0) {
      return {.type = AccessMethod::DIRECT_ROWID,
              .primary_condition = const_cast<WhereCondition *>(&cond),
              .index_condition = nullptr,
              .index_col = cond.column_index};
    }
  }

  auto indexes = vm_get_table_indexes(table_name);
  for (auto &cond : sorted_conditions) {
    if (indexes.find(cond.column_index) != indexes.end()) {
      return {.type = AccessMethod::INDEX_SCAN,
              .primary_condition = nullptr,
              .index_condition = const_cast<WhereCondition *>(&cond),
              .index_col = cond.column_index};
    }
  }

  return {.type = AccessMethod::FULL_TABLE_SCAN,
          .primary_condition = nullptr,
          .index_condition = nullptr,
          .index_col = 0};
}

// Main unified function
std::vector<VMInstruction>
update_or_delete_or_select(const ParsedParameters &options, bool implicit_begin) {
  RegisterAllocator regs;

  auto optimized_conditions = optimize_where_conditions(
      options.where_conditions, options.schema, options.table_name);



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

  if (options.operation == ParsedParameters::SELECT && options.order_by.column_name != nullptr) {
    instructions.push_back(make_sort(options.order_by->column_index,
                                     !options.order_by->asc));
  }

  instructions.push_back(make_flush());
  instructions.push_back(make_halt());

  return instructions;
}

// Public wrapper functions
std::vector<VMInstruction> build_select(const SelectOptions &options) {
  ParsedParameters unified = {.table_name = options.table_name,

                            .set_columns = {},
                            .where_conditions = options.where_conditions,
                            .operation = ParsedParameters::SELECT,
                            .select_columns = options.column_names,
                            .order_by = options.order_by,
                            .aggregate_func = nullptr};

  return update_or_delete_or_select(unified, false);
}

std::vector<VMInstruction> build_update(const UpdateOptions &options,
                                        bool implicit_begin) {
  ParsedParameters unified = {.table_name = options.table_name,
                            .schema = options.schema,
                            .set_columns = options.set_columns,
                            .where_conditions = options.where_conditions,
                            .operation = ParsedParameters::UPDATE,
                            .select_columns = nullptr,
                            .order_by = nullptr,
                            .aggregate_func = nullptr,
                            .aggregate_column = nullptr};

  return update_or_delete_or_select(unified, implicit_begin);
}

std::vector<VMInstruction> build_delete(const UpdateOptions &options,
                                        bool implicit_begin) {
  ParsedParameters unified = {.table_name = options.table_name,
                            .schema = options.schema,
                            .set_columns = {},
                            .where_conditions = options.where_conditions,
                            .operation = ParsedParameters::DELETE,
                            .select_columns = nullptr,
                            .order_by = nullptr,
                            .aggregate_func = nullptr,
                            .aggregate_column = nullptr};

  return update_or_delete_or_select(unified, implicit_begin);
}

// Build full table scan operation
std::vector<VMInstruction> build_full_table_scan_operation(
    const std::string &table_name,
    const std::vector<ColumnInfo> &schema,
    const std::vector<Pair> &set_columns,
    const std::vector<WhereCondition> &conditions, RegisterAllocator &regs,
    ParsedParameters::Operation operation, bool implicit_begin,
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

  if (operation == ParsedParameters::SELECT || operation == ParsedParameters::AGGREGATE) {
    instructions.push_back(make_open_read(cursor_id, table_name_str));
  } else {
    instructions.push_back(make_open_write(cursor_id, table_name_str));
  }

  // Initialize aggregate if needed
  int agg_reg = -1;
  if (operation == ParsedParameters::AGGREGATE) {
    agg_reg = regs.get("agg");
    instructions.push_back(make_agg_reset(aggregate_func));
  }

  // Load comparison values
  for (size_t i = 0; i < conditions.size(); i++) {
    int reg = regs.get("compare_" + std::to_string(i));
    load_value(instructions, conditions[i].value, reg);
  }

  instructions.push_back(make_rewind_label(cursor_id, "end"));

  labels["loop_start"] = instructions.size();

  build_where_checks(instructions, cursor_id, conditions, "next_record", regs);

  // Perform operation
  if (operation == ParsedParameters::DELETE) {
    instructions.push_back(make_delete(cursor_id));
  } else if (operation == ParsedParameters::AGGREGATE) {
    if (strcmp(aggregate_func, "COUNT") != 0 && aggregate_column != nullptr) {
      int value_reg = regs.get("agg_value");
      instructions.push_back(make_column(cursor_id, (int32_t)*aggregate_column, value_reg));
      instructions.push_back(make_agg_step(value_reg));
    } else {
      instructions.push_back(make_agg_step());
    }
  } else if (operation == ParsedParameters::SELECT) {
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
      instructions.push_back(make_column(cursor_id, (int32_t)columns_to_select[i], col_reg));
    }

    instructions.push_back(make_result_row(output_regs[0], (int32_t)output_regs.size()));
  } else {
    // UPDATE
    std::vector<int> current_regs;
    for (size_t i = 0; i < schema.size(); i++) {
      int col_reg = regs.get("current_col_" + std::to_string(i));
      current_regs.push_back(col_reg);
      instructions.push_back(make_column(cursor_id, (int32_t)i, col_reg));
    }

    for (const auto &set_col : set_columns) {
      int reg = regs.get("update_col_" + std::to_string(set_col.column_index));
      load_value(instructions, set_col.value, reg);
      instructions.push_back(make_move(reg, current_regs[set_col.column_index]));
    }

    int record_reg = regs.get("record");
    instructions.push_back(make_record(current_regs[0], (int32_t)schema.size(), record_reg));

    int rowid_reg = regs.get("rowid");
    instructions.push_back(make_key(cursor_id, rowid_reg));
    instructions.push_back(make_insert(cursor_id, rowid_reg, record_reg));
  }

  labels["next_record"] = instructions.size();

  instructions.push_back(make_next_label(cursor_id, "loop_start"));

  labels["end"] = instructions.size();

  if (operation == ParsedParameters::AGGREGATE) {
    int output_reg = regs.get("output");
    instructions.push_back(make_agg_final(output_reg));
    instructions.push_back(make_result_row(output_reg, 1));
  }

  instructions.push_back(make_close(cursor_id));

  resolve_labels(instructions, labels);
  return instructions;
}

// Build direct rowid operation
std::vector<VMInstruction> build_direct_rowid_operation(
    const std::string &table_name, const std::vector<ColumnInfo> &schema,
    const std::vector<Pair> &set_columns,
    const WhereCondition &primary_condition,
    const std::vector<WhereCondition> &remaining_conditions,
    RegisterAllocator &regs, ParsedParameters::Operation operation,
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

  if (operation == ParsedParameters::SELECT || operation == ParsedParameters::AGGREGATE) {
    instructions.push_back(make_open_read(cursor_id, table_name_str));
  } else {
    instructions.push_back(make_open_write(cursor_id, table_name_str));
  }

  int agg_reg = -1;
  if (operation == ParsedParameters::AGGREGATE) {
    agg_reg = regs.get("agg");
    instructions.push_back(make_agg_reset(aggregate_func));
  }

  std::unordered_map<uint32_t, std::pair<Index, int>> indexes_to_update;
  if (operation == ParsedParameters::UPDATE) {
    auto table_indexes = vm_get_table_indexes(table_name);
    int cursor_idx = 1;
    for (const auto &[column, index] : table_indexes) {
      indexes_to_update[column] = {index, cursor_idx++};
    }

    for (const auto &[ci, idx_pair] : indexes_to_update) {
      instructions.push_back(make_open_write(idx_pair.second, table_name_str, ci));
    }
  }

  int rowid_reg = regs.get("rowid_value");
  load_value(instructions, primary_condition.value, rowid_reg);

  instructions.push_back(make_seek_eq_label(cursor_id, rowid_reg, "end"));

  build_where_checks(instructions, cursor_id, remaining_conditions, "end", regs);

  // Perform operation
  if (operation == ParsedParameters::DELETE) {
    instructions.push_back(make_delete(cursor_id));
  } else if (operation == ParsedParameters::AGGREGATE) {
    if (strcmp(aggregate_func, "COUNT") != 0 && aggregate_column != nullptr) {
      int value_reg = regs.get("agg_value");
      instructions.push_back(make_column(cursor_id, (int32_t)*aggregate_column, value_reg));
      instructions.push_back(make_agg_step(value_reg));
    } else {
      instructions.push_back(make_agg_step());
    }
  } else if (operation == ParsedParameters::SELECT) {
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
      instructions.push_back(make_column(cursor_id, (int32_t)columns_to_select[i], col_reg));
    }

    instructions.push_back(make_result_row(output_regs[0], (int32_t)output_regs.size()));
  } else {
    // UPDATE
    std::vector<int> current_regs;
    for (size_t i = 0; i < schema.size(); i++) {
      int col_reg = regs.get("current_col_" + std::to_string(i));
      current_regs.push_back(col_reg);
      instructions.push_back(make_column(cursor_id, (int32_t)i, col_reg));

      if (indexes_to_update.find(i) != indexes_to_update.end()) {
        const auto &idx_pair = indexes_to_update[i];
        instructions.push_back(make_seek_eq_label(idx_pair.second, col_reg, "end"));
      }
    }

    for (const auto &set_col : set_columns) {
      int reg = regs.get("update_col_" + std::to_string(set_col.column_index));
      load_value(instructions, set_col.value, reg);
      instructions.push_back(make_move(reg, current_regs[set_col.column_index]));

      if (indexes_to_update.find(set_col.column_index) != indexes_to_update.end()) {
        const auto &idx_pair = indexes_to_update[set_col.column_index];
        instructions.push_back(make_delete(idx_pair.second));
        instructions.push_back(make_insert(idx_pair.second, current_regs[set_col.column_index], rowid_reg));
      }
    }

    int record_reg = regs.get("record");
    instructions.push_back(make_record(current_regs[0], (int32_t)schema.size(), record_reg));
    instructions.push_back(make_insert(cursor_id, rowid_reg, record_reg));
  }

  labels["end"] = instructions.size();

  if (operation == ParsedParameters::AGGREGATE) {
    int output_reg = regs.get("output");
    instructions.push_back(make_agg_final(output_reg));
    instructions.push_back(make_result_row(output_reg, 1));
  }

  instructions.push_back(make_close(cursor_id));

  if (operation == ParsedParameters::UPDATE) {
    for (const auto &[ci, idx_pair] : indexes_to_update) {
      instructions.push_back(make_close(idx_pair.second));
    }
  }

  resolve_labels(instructions, labels);
  return instructions;
}

// Build index scan operation
std::vector<VMInstruction> build_index_scan_operation(
    const std::string &table_name, const std::vector<ColumnInfo> &schema,
    const std::vector<Pair> &set_columns, const WhereCondition &index_condition,
    const std::vector<WhereCondition> &remaining_conditions, uint32_t index_col,
    RegisterAllocator &regs, ParsedParameters::Operation operation,
    bool implicit_begin, std::vector<uint32_t> *select_columns,
    const char *aggregate_func, uint32_t *aggregate_column) {

  std::vector<VMInstruction> instructions;
  std::unordered_map<std::string, int> labels;
  const int table_cursor_id = 1;
  const int index_cursor_id = 0;

  if (implicit_begin) {
    add_begin(instructions);
  }

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  if (operation == ParsedParameters::SELECT || operation == ParsedParameters::AGGREGATE) {
    instructions.push_back(make_open_read(index_cursor_id, table_name_str));
    instructions.push_back(make_open_read(table_cursor_id, table_name_str));
  } else {
    instructions.push_back(make_open_write(index_cursor_id, table_name_str, index_condition.column_index));
    instructions.push_back(make_open_write(table_cursor_id, table_name_str));
  }

  int agg_reg = -1;
  if (operation == ParsedParameters::AGGREGATE) {
    agg_reg = regs.get("agg");
    instructions.push_back(make_agg_reset(aggregate_func));
  }

  int ii = 2;
  std::unordered_map<uint32_t, std::pair<Index, int>> indexes_to_update;
  if (operation == ParsedParameters::UPDATE) {
    auto table_indexes = vm_get_table_indexes(table_name);
    auto table = vm_get_table(table_name);
    for (const auto &[column, index] : table_indexes) {
      auto info = table.schema.columns[column];
      if (column != index_col) {
        indexes_to_update[column] = {index, ii++};
        instructions.push_back(make_open_write(indexes_to_update[column].second, table_name_str, column));
      }
    }
  }

  int index_key_reg = regs.get("index_key");
  load_value(instructions, index_condition.value, index_key_reg);

  // Build seek based on operator type
  switch(index_condition.operator_type) {
    case EQ:
      instructions.push_back(make_seek_eq_label(index_cursor_id, index_key_reg, "end"));
      break;
    case GE:
      instructions.push_back(make_seek_ge_label(index_cursor_id, index_key_reg, "end"));
      break;
    case GT:
      instructions.push_back(make_seek_gt_label(index_cursor_id, index_key_reg, "end"));
      break;
    case LE:
      instructions.push_back(make_seek_le_label(index_cursor_id, index_key_reg, "end"));
      break;
    case LT:
      instructions.push_back(make_seek_lt_label(index_cursor_id, index_key_reg, "end"));
      break;
    default:
      instructions.push_back(make_seek_eq_label(index_cursor_id, index_key_reg, "end"));
  }

  labels["loop_start"] = instructions.size();

  int current_key_reg = regs.get("current_key");
  instructions.push_back(make_key(index_cursor_id, current_key_reg));

  OpCode negated_op = get_negated_opcode(index_condition.operator_type);
  switch(negated_op) {
    case OP_Eq:
      instructions.push_back(make_eq_label(current_key_reg, index_key_reg, "end"));
      break;
    case OP_Ne:
      instructions.push_back(make_ne_label(current_key_reg, index_key_reg, "end"));
      break;
    case OP_Lt:
      instructions.push_back(make_lt_label(current_key_reg, index_key_reg, "end"));
      break;
    case OP_Le:
      instructions.push_back(make_le_label(current_key_reg, index_key_reg, "end"));
      break;
    case OP_Gt:
      instructions.push_back(make_gt_label(current_key_reg, index_key_reg, "end"));
      break;
    case OP_Ge:
      instructions.push_back(make_ge_label(current_key_reg, index_key_reg, "end"));
      break;
  }

  int rowid_reg = regs.get("rowid");
  instructions.push_back(make_column(index_cursor_id, 0, rowid_reg));

  instructions.push_back(make_seek_eq_label(table_cursor_id, rowid_reg, "next_iteration"));

  build_where_checks(instructions, table_cursor_id, remaining_conditions, "next_iteration", regs);

  // Perform operation
  if (operation == ParsedParameters::DELETE) {
    instructions.push_back(make_delete(table_cursor_id));
  } else if (operation == ParsedParameters::AGGREGATE) {
    if (strcmp(aggregate_func, "COUNT") != 0 && aggregate_column != nullptr) {
      int value_reg = regs.get("agg_value");
      instructions.push_back(make_column(table_cursor_id, (int32_t)*aggregate_column, value_reg));
      instructions.push_back(make_agg_step(value_reg));
    } else {
      instructions.push_back(make_agg_step());
    }
  } else if (operation == ParsedParameters::SELECT) {
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
      instructions.push_back(make_column(table_cursor_id, (int32_t)columns_to_select[i], col_reg));
    }

    instructions.push_back(make_result_row(output_regs[0], (int32_t)output_regs.size()));
  } else {
    // UPDATE
    std::vector<int> current_regs;
    for (size_t i = 0; i < schema.size(); i++) {
      int col_reg = regs.get("current_col_" + std::to_string(i));
      current_regs.push_back(col_reg);
      instructions.push_back(make_column(table_cursor_id, (int32_t)i, col_reg));

      if (indexes_to_update.find(i) != indexes_to_update.end()) {
        const auto &idx_pair = indexes_to_update[i];
        if (idx_pair.second != index_cursor_id) {
          instructions.push_back(make_seek_eq_label(idx_pair.second, col_reg, "end"));
        }
      }
    }

    for (const auto &set_col : set_columns) {
      int reg = regs.get("update_col_" + std::to_string(set_col.column_index));
      load_value(instructions, set_col.value, reg);
      instructions.push_back(make_move(reg, current_regs[set_col.column_index]));

      if (indexes_to_update.find(set_col.column_index) != indexes_to_update.end()) {
        const auto &idx_pair = indexes_to_update[set_col.column_index];
        instructions.push_back(make_delete(idx_pair.second));
        instructions.push_back(make_insert(idx_pair.second, current_regs[set_col.column_index], rowid_reg));
      }
    }

    int record_reg = regs.get("record");
    instructions.push_back(make_record(current_regs[0], (int32_t)schema.size(), record_reg));
    instructions.push_back(make_insert(table_cursor_id, rowid_reg, record_reg));
  }

  labels["next_iteration"] = instructions.size();

  bool use_next = ascending(index_condition.operator_type);
  if (use_next) {
    instructions.push_back(make_next_label(index_cursor_id, "loop_start"));
  } else {
    instructions.push_back(make_prev_label(index_cursor_id, "loop_start"));
  }

  labels["end"] = instructions.size();

  if (operation == ParsedParameters::AGGREGATE) {
    int output_reg = regs.get("output");
    instructions.push_back(make_agg_final(output_reg));
    instructions.push_back(make_result_row(output_reg, 1));
  }

  instructions.push_back(make_close(index_cursor_id));
  instructions.push_back(make_close(table_cursor_id));

  if (operation == ParsedParameters::UPDATE) {
    for (const auto &[ci, idx_pair] : indexes_to_update) {
      instructions.push_back(make_close(idx_pair.second));
    }
  }

  resolve_labels(instructions, labels);
  return instructions;
}


// #include "programbuilder.hpp"
// #include "parser.hpp"
// #include "defs.hpp"
// #include "schema.hpp"
// #include "vm.hpp"
// #include <cstring>

// // Forward declarations for AST traversal
// static std::vector<WhereCondition> extract_where_conditions(WhereNode* where, const TableSchema& schema);
// static WhereCondition extract_condition_from_binary_op(BinaryOpNode* op, const TableSchema& schema);
// static uint32_t resolve_column_index(const char* col_name, const std::vector<ColumnInfo>& columns);
// static std::vector<VMInstruction> build_from_ast(ASTNode* ast);

// // Helper to resolve column name to index
// static uint32_t resolve_column_index(const char* col_name, const std::vector<ColumnInfo>& columns) {
//     for (size_t i = 0; i < columns.size(); i++) {
//         if (strcmp(columns[i].name, col_name) == 0) {
//             return i;
//         }
//     }
//     return 0; // Default to first column if not found
// }

// // Extract a single condition from a binary op node
// static WhereCondition extract_condition_from_binary_op(BinaryOpNode* op, const TableSchema& schema) {
//     WhereCondition cond;

//     // Left side should be column reference
//     if (op->left->type == AST_COLUMN_REF) {
//         ColumnRefNode* col = (ColumnRefNode*)op->left;
//         cond.column_name = col->name;
//         cond.column_index = resolve_column_index(col->name, schema.columns);
//     }

//     // Right side should be literal
//     if (op->right->type == AST_LITERAL) {
//         LiteralNode* lit = (LiteralNode*)op->right;
//         cond.value = lit->value;
//     }

//     cond.operator_type = op->op;
//     cond.selectivity = 0.5; // Default selectivity

//     return cond;
// }

// // Recursively extract WHERE conditions from AST
// static std::vector<WhereCondition> extract_where_conditions(WhereNode* where, const TableSchema& schema) {
//     std::vector<WhereCondition> conditions;

//     if (!where || !where->condition) {
//         return conditions;
//     }

//     // Traverse the condition tree
//     std::function<void(ASTNode*)> traverse = [&](ASTNode* node) {
//         if (!node) return;

//         if (node->type == AST_BINARY_OP) {
//             BinaryOpNode* binop = (BinaryOpNode*)node;

//             if (binop->is_and) {
//                 // AND node - traverse both sides
//                 traverse(binop->left);
//                 traverse(binop->right);
//             } else {
//                 // Comparison node - extract condition
//                 conditions.push_back(extract_condition_from_binary_op(binop, schema));
//             }
//         }
//     };

//     traverse(where->condition);
//     return conditions;
// }

// // Build SELECT from AST
// static std::vector<VMInstruction> build_select_from_ast(SelectNode* node) {
//     // Get table schema
//     Table& table = vm_get_table(node->table);

//     // Handle aggregate functions
//     if (node->aggregate) {
//         AggregateNode* agg = node->aggregate;
//         uint32_t* column_index = nullptr;
//         uint32_t col_idx;

//         if (agg->arg && agg->arg->type == AST_COLUMN_REF) {
//             ColumnRefNode* col = (ColumnRefNode*)agg->arg;
//             col_idx = resolve_column_index(col->name, table.schema.columns);
//             column_index = &col_idx;
//         }

//         std::vector<WhereCondition> conditions = extract_where_conditions(node->where, table.schema);

//         return aggregate(node->table, agg->function, column_index, conditions);
//     }

//     // Regular SELECT
//     ParsedParameters params;
//     params.table_name = node->table;
//     params.operation = ParsedParameters::SELECT;
//     params.where_conditions = extract_where_conditions(node->where, table.schema);

//     // Extract column list
//     if (!node->columns.empty()) {
//         for (ASTNode* col_node : node->columns) {
//             if (col_node->type == AST_COLUMN_REF) {
//                 ColumnRefNode* col = (ColumnRefNode*)col_node;
//                 params.select_columns.push_back(col->name);
//             }
//         }
//     }

//     // Handle ORDER BY
//     if (node->order_by) {
//         params.order_by.column_name = node->order_by->column;
//         params.order_by.asc = node->order_by->ascending;
//     }

//     return build_select(params);
// }

// // Build INSERT from AST
// static std::vector<VMInstruction> build_insert_from_ast(InsertNode* node) {
//     Table& table = vm_get_table(node->table);

//     std::vector<SET_PAIR> values;
//     for (size_t i = 0; i < node->values.size() && i < table.schema.columns.size(); i++) {
//         if (node->values[i]->type == AST_LITERAL) {
//             LiteralNode* lit = (LiteralNode*)node->values[i];
//             values.push_back({table.schema.columns[i].name, lit->value});
//         }
//     }

//     // Check if we're in a transaction
//     bool implicit_begin = !VM.in_transaction;

//     return build_insert(node->table, values, implicit_begin);
// }

// // Build UPDATE from AST
// static std::vector<VMInstruction> build_update_from_ast(UpdateNode* node) {
//     Table& table = vm_get_table(node->table);

//     ParsedParameters params;
//     params.table_name = node->table;
//     params.operation = ParsedParameters::UPDATE;
//     params.where_conditions = extract_where_conditions(node->where, table.schema);

//     // Extract SET clauses
//     for (SetClauseNode* set : node->set_clauses) {
//         if (set->value->type == AST_LITERAL) {
//             LiteralNode* lit = (LiteralNode*)set->value;
//             uint32_t col_idx = resolve_column_index(set->column, table.schema.columns);
//             params.set_columns.push_back({set->column, lit->value});
//         }
//     }

//     bool implicit_begin = !VM.in_transaction;

//     return build_update(params, implicit_begin);
// }

// // Build DELETE from AST
// static std::vector<VMInstruction> build_delete_from_ast(DeleteNode* node) {
//     Table& table = vm_get_table(node->table);

//     ParsedParameters params;
//     params.table_name = node->table;
//     params.operation = ParsedParameters::DELETE;
//     params.where_conditions = extract_where_conditions(node->where, table.schema);

//     bool implicit_begin = !VM.in_transaction;

//     return build_delete(params, implicit_begin);
// }

// // Build CREATE TABLE from AST
// static std::vector<VMInstruction> build_create_table_from_ast(CreateTableNode* node) {
//     return build_create_table(node->table, node->columns);
// }

// // Build CREATE INDEX from AST
// static std::vector<VMInstruction> build_create_index_from_ast(CreateIndexNode* node) {
//     Table& table = vm_get_table(node->table);
//     uint32_t col_idx = resolve_column_index(node->column, table.schema.columns);
//     DataType key_type = table.schema.columns[col_idx].type;

//     return build_create_index(node->table, col_idx, key_type);
// }

// // Build transaction commands from AST
// static std::vector<VMInstruction> build_begin_from_ast(BeginNode* node) {
//     return {make_begin(), make_halt()};
// }

// static std::vector<VMInstruction> build_commit_from_ast(CommitNode* node) {
//     return {make_commit(), make_halt()};
// }

// static std::vector<VMInstruction> build_rollback_from_ast(RollbackNode* node) {
//     return {make_rollback(), make_halt()};
// }

// // Main entry point - builds VM instructions from AST
// std::vector<VMInstruction> build_from_ast(ASTNode* ast) {
//     if (!ast) {
//         return {make_halt()};
//     }

//     switch (ast->type) {
//         case AST_SELECT:
//             return build_select_from_ast((SelectNode*)ast);

//         case AST_INSERT:
//             return build_insert_from_ast((InsertNode*)ast);

//         case AST_UPDATE:
//             return build_update_from_ast((UpdateNode*)ast);

//         case AST_DELETE:
//             return build_delete_from_ast((DeleteNode*)ast);

//         case AST_CREATE_TABLE:
//             return build_create_table_from_ast((CreateTableNode*)ast);

//         case AST_CREATE_INDEX:
//             return build_create_index_from_ast((CreateIndexNode*)ast);

//         case AST_BEGIN:
//             return build_begin_from_ast((BeginNode*)ast);

//         case AST_COMMIT:
//             return build_commit_from_ast((CommitNode*)ast);

//         case AST_ROLLBACK:
//             return build_rollback_from_ast((RollbackNode*)ast);

//         default:
//             return {make_halt()};
//     }
// }

// // Public wrapper that combines parsing and building
// std::vector<VMInstruction> parse_and_build(const char* sql) {
//     ASTNode* ast = parse_sql(sql);
//     if (!ast) {
//         return {make_halt()};
//     }

//     return build_from_ast(ast);
// }

// // Keep the existing implementation functions below unchanged...
// // (All the build_direct_rowid_operation, build_index_scan_operation, etc. remain the same)
