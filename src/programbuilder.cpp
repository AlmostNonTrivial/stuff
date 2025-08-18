#include "programbuilder.hpp"
#include "arena.hpp"
#include "defs.hpp"
#include "schema.hpp"
#include "vm.hpp"
#include <algorithm>
#include <cstring>



// Original functions that still work with ParsedParameters
ArenaVector<VMInstruction>
build_create_table(const ArenaString &table_name,
                  const ArenaVector<ColumnInfo> &columns);

ArenaVector<VMInstruction>build_drop_table(const ArenaString &table_name);
ArenaVector<VMInstruction>build_drop_index(const ArenaString &index_name);
ArenaVector<VMInstruction>build_create_index(const ArenaString &table_name,
                                              uint32_t column_index,
                                              DataType key_type);

ArenaVector<VMInstruction>build_insert(const ArenaString &table_name,
                                        const ArenaVector<SET_PAIR> &values
                                        );

ArenaVector<VMInstruction>build_select(const ParsedParameters&options);
ArenaVector<VMInstruction>build_update(const ParsedParameters&options
                                        );
ArenaVector<VMInstruction>build_delete(const ParsedParameters&options
                                        );

ArenaVector<VMInstruction>
aggregate(const ArenaString &table_name, const char *agg_func,
          uint32_t *column_index,
          const ArenaVector<WhereCondition> &where_conditions);

// Forward declarations for AST traversal
static ArenaVector<WhereCondition>
extract_where_conditions(WhereNode *where, const ArenaString &table_name);
static WhereCondition
extract_condition_from_binary_op(BinaryOpNode *op,
                                 const ArenaString &table_name);
static uint32_t resolve_column_index(const char *col_name,
                                     const ArenaString &table_name);
ArenaVector<VMInstruction>
update_or_delete_or_select(const ParsedParameters &options);

// Global tables reference
extern ArenaMap<ArenaString, Table> tables;

// RegisterAllocator implementation
int RegisterAllocator::get(const ArenaString &name) {
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
void resolve_labels(ArenaVector<VMInstruction>&program,
                    const ArenaMap<ArenaString, int> &map) {
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
  return (value.type == TYPE_UINT32 || value.type == TYPE_UINT64) ? OP_Integer
                                                                : OP_String;
}

uint8_t set_p5(uint8_t current, uint8_t flag) { return current | flag; }

void load_value(ArenaVector<VMInstruction>&instructions, const VMValue &value,
                int target_reg) {
  if (value.type == TYPE_UINT32 || value.type == TYPE_UINT64) {
    uint32_t val = *(uint32_t *)value.data;
    instructions.push_back(make_integer(target_reg, (int32_t)val));
  } else {
    instructions.push_back(
        make_string(target_reg, (int32_t)value.type, value.data));
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

// Helper to resolve column name to index
static uint32_t resolve_column_index(const char *col_name,
                                     const ArenaString &table_name) {
  return get_column_index(const_cast<char *>(table_name.c_str()),
                          const_cast<char *>(col_name));
}

// Extract a single condition from a binary op node
static WhereCondition
extract_condition_from_binary_op(BinaryOpNode *op,
                                 const ArenaString &table_name) {
  WhereCondition cond;

  // Left side should be column reference
  if (op->left->type == AST_COLUMN_REF) {
    ColumnRefNode *col = (ColumnRefNode *)op->left;
    cond.column_name = ArenaString(col->name);
    cond.column_index = resolve_column_index(col->name, table_name);
  }

  // Right side should be literal
  if (op->right->type == AST_LITERAL) {
    LiteralNode *lit = (LiteralNode *)op->right;
    cond.value = lit->value;
  }

  cond.operator_type = op->op;
  cond.selectivity = 0.5; // Default selectivity

  return cond;
}

// Recursively extract WHERE conditions from AST
static ArenaVector<WhereCondition>
extract_where_conditions(WhereNode *where, const ArenaString &table_name) {
  ArenaVector<WhereCondition> conditions;

  if (!where || !where->condition) {
    return conditions;
  }

  // Traverse the condition tree
  std::function<void(ASTNode *)> traverse = [&](ASTNode *node) {
    if (!node)
      return;

    if (node->type == AST_BINARY_OP) {
      BinaryOpNode *binop = (BinaryOpNode *)node;

      if (binop->is_and) {
        // AND node - traverse both sides
        traverse(binop->left);
        traverse(binop->right);
      } else {
        // Comparison node - extract condition
        conditions.push_back(
            extract_condition_from_binary_op(binop, table_name));
      }
    }
  };

  traverse(where->condition);
  return conditions;
}

// Build SELECT from AST
static ArenaVector<VMInstruction>build_select_from_ast(SelectNode *node) {
  // Handle aggregate functions
  if (node->aggregate) {
    AggregateNode *agg = node->aggregate;
    uint32_t *column_index = nullptr;
    uint32_t col_idx;

    if (agg->arg && agg->arg->type == AST_COLUMN_REF) {
      ColumnRefNode *col = (ColumnRefNode *)agg->arg;
      col_idx = resolve_column_index(col->name, node->table);
      column_index = &col_idx;
    }

    ArenaVector<WhereCondition> conditions =
        extract_where_conditions(node->where, node->table);

    return aggregate(node->table, agg->function, column_index, conditions);
  }

  // Regular SELECT
  ParsedParameters params;
  params.table_name = node->table;
  params.operation = ParsedParameters::SELECT;
  params.where_conditions = extract_where_conditions(node->where, node->table);

  // Extract column list
  ArenaVector<ArenaString> select_columns;
  if (!node->columns.empty()) {
    for (ASTNode *col_node : node->columns) {
      if (col_node->type == AST_COLUMN_REF) {
        ColumnRefNode *col = (ColumnRefNode *)col_node;
        select_columns.push_back(ArenaString(col->name));
      }
    }
  }
  params.select_columns = select_columns;

  // Handle ORDER BY
  if (node->order_by) {
    params.order_by.column_name = node->order_by->column;
    params.order_by.asc = node->order_by->ascending;
  }

  return build_select(params);
}

// Build INSERT from AST
static ArenaVector<VMInstruction>build_insert_from_ast(InsertNode *node) {
  ArenaVector<SET_PAIR> values;

  // Get table to know column names
  Table *table = get_table(const_cast<char *>(node->table));
  if (!table) {
    return {make_halt()};
  }

  for (size_t i = 0;
       i < node->values.size() && i < table->schema.columns.size(); i++) {
    if (node->values[i]->type == AST_LITERAL) {
      LiteralNode *lit = (LiteralNode *)node->values[i];
      values.push_back(
          {ArenaString(table->schema.columns[i].name), lit->value});
    }
  }



  return build_insert(node->table, values);
}

// Build UPDATE from AST
static ArenaVector<VMInstruction>build_update_from_ast(UpdateNode *node) {
  ParsedParameters params;
  params.table_name = node->table;
  params.operation = ParsedParameters::UPDATE;
  params.where_conditions = extract_where_conditions(node->where, node->table);

  // Extract SET clauses
  for (SetClauseNode *set : node->set_clauses) {
    if (set->value->type == AST_LITERAL) {
      LiteralNode *lit = (LiteralNode *)set->value;
      uint32_t col_idx = resolve_column_index(set->column, node->table);
      params.set_columns.push_back({ArenaString(set->column), lit->value});
    }
  }

  return build_update(params);
}

// Build DELETE from AST
static ArenaVector<VMInstruction>build_delete_from_ast(DeleteNode *node) {
  ParsedParameters params;
  params.table_name = node->table;
  params.operation = ParsedParameters::DELETE;
  params.where_conditions = extract_where_conditions(node->where, node->table);

  return build_delete(params);
}

// Build CREATE TABLE from AST
static ArenaVector<VMInstruction>
build_create_table_from_ast(CreateTableNode *node) {
  return build_create_table(node->table, node->columns);
}

// Build CREATE INDEX from AST
static ArenaVector<VMInstruction>
build_create_index_from_ast(CreateIndexNode *node) {
  Table *table = get_table(const_cast<char *>(node->table));
  if (!table) {
    return {make_halt()};
  }

  uint32_t col_idx = resolve_column_index(node->column, node->table);
  DataType key_type = table->schema.columns[col_idx].type;

  return build_create_index(node->table, col_idx, key_type);
}

// Build transaction commands from AST
static ArenaVector<VMInstruction>build_begin_from_ast(BeginNode *node) {
    ArenaVector<VMInstruction> vec;
    vec.push_back(make_begin());
    vec.push_back(make_halt());
    return vec;
}

static ArenaVector<VMInstruction>build_commit_from_ast(CommitNode *node) {
  return {make_commit(), make_halt()};
}

static ArenaVector<VMInstruction>build_rollback_from_ast(RollbackNode *node) {
  return {make_rollback(), make_halt()};
}

// Main entry point - builds VM instructions from AST
ArenaVector<VMInstruction>build_from_ast(ASTNode *ast) {
  if (!ast) {
    return {make_halt()};
  }

  switch (ast->type) {
  case AST_SELECT:
    return build_select_from_ast((SelectNode *)ast);

  case AST_INSERT:
    return build_insert_from_ast((InsertNode *)ast);

  case AST_UPDATE:
    return build_update_from_ast((UpdateNode *)ast);

  case AST_DELETE:
    return build_delete_from_ast((DeleteNode *)ast);

  case AST_CREATE_TABLE:
    return build_create_table_from_ast((CreateTableNode *)ast);

  case AST_CREATE_INDEX:
    return build_create_index_from_ast((CreateIndexNode *)ast);

  case AST_BEGIN:
    return build_begin_from_ast((BeginNode *)ast);

  case AST_COMMIT:
    return build_commit_from_ast((CommitNode *)ast);

  case AST_ROLLBACK:
    return build_rollback_from_ast((RollbackNode *)ast);

  default:
    return {make_halt()};
  }
}

// Build where checks helper
void build_where_checks(ArenaVector<VMInstruction>&instructions, int cursor_id,
                        const ArenaVector<WhereCondition> &conditions,
                        const ArenaString &skip_label,
                        RegisterAllocator &regs) {
  for (size_t i = 0; i < conditions.size(); i++) {
    int col_reg = regs.get("where_col_" + std::to_string(i));
    instructions.push_back(
        make_column(cursor_id, (int32_t)conditions[i].column_index, col_reg));

    int compare_reg = regs.get("compare_" + std::to_string(i));
    load_value(instructions, conditions[i].value, compare_reg);

    OpCode negated = get_negated_opcode(conditions[i].operator_type);

    char *label_str = (char *)arena_alloc(skip_label.size() + 1);
    strcpy(label_str, skip_label.c_str());

    // Build the appropriate comparison with label
    switch (negated) {
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

// Create table
ArenaVector<VMInstruction>
build_create_table(const ArenaString &table_name,
                   const ArenaVector<ColumnInfo> &columns) {
  TableSchema *schema = ARENA_ALLOC(TableSchema);
  schema->table_name = table_name;
  schema->columns = columns;

  return {make_create_table(schema), make_halt()};
}

// Drop table
ArenaVector<VMInstruction>build_drop_table(const ArenaString &table_name) {
  char *name = (char *)arena_alloc(table_name.size() + 1);
  strcpy(name, table_name.c_str());

  return {make_drop_table(name), make_halt()};
}

// Drop index
ArenaVector<VMInstruction>build_drop_index(const ArenaString &index_name) {
  char *name = (char *)arena_alloc(index_name.size() + 1);
  strcpy(name, index_name.c_str());

  return {make_drop_index(0, name), make_halt()};
}

// Create index
ArenaVector<VMInstruction>build_create_index(const ArenaString &table_name,
                                              uint32_t column_index,
                                              DataType key_type) {
  RegisterAllocator regs;
  ArenaVector<VMInstruction>instructions;
  ArenaMap<ArenaString, int> labels;

  const int table_cursor_id = 0;
  const int index_cursor_id = 1;

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  instructions.push_back(make_create_index(column_index, table_name_str));
  instructions.push_back(make_open_read(table_cursor_id, table_name_str));
  instructions.push_back(
      make_open_write(index_cursor_id, table_name_str, column_index));

  instructions.push_back(make_rewind_label(table_cursor_id, "end"));

  labels["loop_start"] = instructions.size();

  int rowid_reg = regs.get("rowid");
  instructions.push_back(make_key(table_cursor_id, rowid_reg));

  int column_reg = regs.get("column_value");
  instructions.push_back(
      make_column(table_cursor_id, (int32_t)column_index, column_reg));
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
ArenaVector<VMInstruction>build_insert(const ArenaString &table_name,
                                        const ArenaVector<SET_PAIR> &values) {
  RegisterAllocator regs;
  ArenaVector<VMInstruction>instructions;

  const int table_cursor_id = 0;

  // Get indexes for this table
  Table *table = get_table(const_cast<char *>(table_name.c_str()));
  if (!table) {
    return {make_halt()};
  }

  ArenaMap<uint32_t, int> indexes_to_insert;
  int cursor_id = 1;
  for (const auto &[col, index] : table->indexes) {
    indexes_to_insert[col] = cursor_id++;
  }

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  instructions.push_back(make_open_write(table_cursor_id, table_name_str));

  // Open index cursors
  for (const auto &[col_idx, cursor_idx] : indexes_to_insert) {
    instructions.push_back(
        make_open_write(cursor_idx, table_name_str, col_idx));
  }

  // Load values
  ArenaVector<int> value_regs;
  for (size_t i = 0; i < values.size(); i++) {
    int reg = regs.get("value_" + std::to_string(i));
    value_regs.push_back(reg);

    load_value(instructions, values[i].second, reg);

    // Find column index for this value
    uint32_t col_idx =
        get_column_index(const_cast<char *>(table_name.c_str()),
                         const_cast<char *>(values[i].first.c_str()));

    // Insert into indexes if needed
    if (indexes_to_insert.find(col_idx) != indexes_to_insert.end()) {
      instructions.push_back(
          make_insert(indexes_to_insert[col_idx], reg, value_regs[0]));
    }
  }

  int record_reg = regs.get("record");
  instructions.push_back(
      make_record(value_regs[0], (int32_t)values.size(), record_reg));
  instructions.push_back(
      make_insert(table_cursor_id, value_regs[0], record_reg));
  instructions.push_back(make_close(table_cursor_id));
  instructions.push_back(make_halt());

  return instructions;
}

// Generate aggregate instructions
ArenaVector<VMInstruction>
aggregate(const ArenaString &table_name, const char *agg_func,
          uint32_t *column_index,
          const ArenaVector<WhereCondition> &where_conditions) {

  if (strcmp(agg_func, "COUNT") != 0 && column_index == nullptr) {
    return {};
  }

  Table *table = get_table(const_cast<char *>(table_name.c_str()));
  if (!table) {
    return {make_halt()};
  }

  if (where_conditions.size() > 0) {
    ParsedParameters options;
    options.table_name = table_name;
    options.where_conditions = where_conditions;
    options.operation = ParsedParameters::AGGREGATE;
    options.aggregate = ArenaString(agg_func);

    return update_or_delete_or_select(options);
  }

  // Simple aggregate without WHERE
  RegisterAllocator regs;
  ArenaVector<VMInstruction>instructions;
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
    instructions.push_back(
        make_column(cursor_id, (int32_t)*column_index, value_reg));
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

// Optimization functions
double estimate_selectivity(const WhereCondition &condition,
                            const ArenaString &table_name) {
  ArenaString column_name = condition.column_name;

  Table *table = get_table(const_cast<char *>(table_name.c_str()));
  if (!table) {
    return 0.5;
  }

  bool is_indexed =
      (table->indexes.find(condition.column_index) != table->indexes.end());

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

ArenaVector<WhereCondition>
optimize_where_conditions(const ArenaVector<WhereCondition> &conditions,
                          const ArenaString &table_name) {

  ArenaVector<WhereCondition> optimized = conditions;

  // Sort by selectivity
  std::sort(optimized.begin(), optimized.end(),
            [&](const WhereCondition &a, const WhereCondition &b) {
              return estimate_selectivity(a, table_name) <
                     estimate_selectivity(b, table_name);
            });

  return optimized;
}

AccessMethod choose_access_method(const ArenaVector<WhereCondition> &conditions,
                                  const ArenaString &table_name) {
  ArenaVector<WhereCondition> sorted_conditions = conditions;
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

  Table *table = get_table(const_cast<char *>(table_name.c_str()));
  if (table) {
    for (auto &cond : sorted_conditions) {
      if (table->indexes.find(cond.column_index) != table->indexes.end()) {
        return {.type = AccessMethod::INDEX_SCAN,
                .primary_condition = nullptr,
                .index_condition = const_cast<WhereCondition *>(&cond),
                .index_col = cond.column_index};
      }
    }
  }

  return {.type = AccessMethod::FULL_TABLE_SCAN,
          .primary_condition = nullptr,
          .index_condition = nullptr,
          .index_col = 0};
}

// Forward declarations for implementation functions
ArenaVector<VMInstruction>build_direct_rowid_operation(
    const ArenaString &table_name, const ArenaVector<SET_PAIR> &set_columns,
    const WhereCondition &primary_condition,
    const ArenaVector<WhereCondition> &remaining_conditions,
    RegisterAllocator &regs, ParsedParameters::Operation operation,
    const ArenaVector<ArenaString> &select_columns,
    const ArenaString &aggregate_func);

ArenaVector<VMInstruction>build_index_scan_operation(
    const ArenaString &table_name, const ArenaVector<SET_PAIR> &set_columns,
    const WhereCondition &index_condition,
    const ArenaVector<WhereCondition> &remaining_conditions, uint32_t index_col,
    RegisterAllocator &regs, ParsedParameters::Operation operation,
    const ArenaVector<ArenaString> &select_columns,
    const ArenaString &aggregate_func);

ArenaVector<VMInstruction>build_full_table_scan_operation(
    const ArenaString &table_name, const ArenaVector<SET_PAIR> &set_columns,
    const ArenaVector<WhereCondition> &conditions, RegisterAllocator &regs,
    ParsedParameters::Operation operation,
    const ArenaVector<ArenaString> &select_columns,
    const ArenaString &aggregate_func);

// Main unified function
ArenaVector<VMInstruction>
update_or_delete_or_select(const ParsedParameters &options) {
  RegisterAllocator regs;

  auto optimized_conditions =
      optimize_where_conditions(options.where_conditions, options.table_name);

  auto access_method =
      choose_access_method(optimized_conditions, options.table_name);

  ArenaVector<VMInstruction>instructions;

  switch (access_method.type) {
  case AccessMethod::DIRECT_ROWID:
    instructions = build_direct_rowid_operation(
        options.table_name, options.set_columns,
        *access_method.primary_condition,
        [&]() {
          ArenaVector<WhereCondition> remaining;
          for (const auto &c : optimized_conditions) {
            if (&c != access_method.primary_condition) {
              remaining.push_back(c);
            }
          }
          return remaining;
        }(),
        regs, options.operation, options.select_columns, options.aggregate);
    break;

  case AccessMethod::INDEX_SCAN:
    instructions = build_index_scan_operation(
        options.table_name, options.set_columns, *access_method.index_condition,
        [&]() {
          ArenaVector<WhereCondition> remaining;
          for (const auto &c : optimized_conditions) {
            if (&c != access_method.index_condition) {
              remaining.push_back(c);
            }
          }
          return remaining;
        }(),
        access_method.index_col, regs, options.operation,
        options.select_columns, options.aggregate);
    break;

  case AccessMethod::FULL_TABLE_SCAN:
  default:
    instructions = build_full_table_scan_operation(
        options.table_name, options.set_columns, optimized_conditions, regs,
        options.operation, options.select_columns, options.aggregate);
  }

  if (options.operation == ParsedParameters::SELECT &&
      !options.order_by.column_name.empty()) {
    uint32_t col_idx = resolve_column_index(
        options.order_by.column_name.c_str(), options.table_name);
    instructions.push_back(make_sort(col_idx, !options.order_by.asc));
  }

  instructions.push_back(make_flush());
  instructions.push_back(make_halt());

  return instructions;
}

// Public wrapper functions
ArenaVector<VMInstruction>build_select(const ParsedParameters &options) {
  return update_or_delete_or_select(options);
}

ArenaVector<VMInstruction>build_update(const ParsedParameters &options) {
  return update_or_delete_or_select(options);
}

ArenaVector<VMInstruction>build_delete(const ParsedParameters &options) {
  return update_or_delete_or_select(options);
}

// Build full table scan operation
ArenaVector<VMInstruction>build_full_table_scan_operation(
    const ArenaString &table_name, const ArenaVector<SET_PAIR> &set_columns,
    const ArenaVector<WhereCondition> &conditions, RegisterAllocator &regs,
    ParsedParameters::Operation operation,
    const ArenaVector<ArenaString> &select_columns,
    const ArenaString &aggregate_func) {

  ArenaVector<VMInstruction>instructions;
  ArenaMap<ArenaString, int> labels;
  const int cursor_id = 0;

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  if (operation == ParsedParameters::SELECT ||
      operation == ParsedParameters::AGGREGATE) {
    instructions.push_back(make_open_read(cursor_id, table_name_str));
  } else {
    instructions.push_back(make_open_write(cursor_id, table_name_str));
  }

  Table *table = get_table(const_cast<char *>(table_name.c_str()));
  if (!table) {
    return {make_halt()};
  }

  // Initialize aggregate if needed
  int agg_reg = -1;
  if (operation == ParsedParameters::AGGREGATE) {
    agg_reg = regs.get("agg");
    instructions.push_back(make_agg_reset(aggregate_func.c_str()));
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
    // Handle aggregate step
    instructions.push_back(make_agg_step());
  } else if (operation == ParsedParameters::SELECT) {
    ArenaVector<int> output_regs;
    ArenaVector<uint32_t> columns_to_select;

    if (!select_columns.empty()) {
      for (const auto &col_name : select_columns) {
        columns_to_select.push_back(
            resolve_column_index(col_name.c_str(), table_name));
      }
    } else {
      for (size_t i = 0; i < table->schema.columns.size(); i++) {
        columns_to_select.push_back(i);
      }
    }

    for (size_t i = 0; i < columns_to_select.size(); i++) {
      int col_reg = regs.get("output_col_" + std::to_string(i));
      output_regs.push_back(col_reg);
      instructions.push_back(
          make_column(cursor_id, (int32_t)columns_to_select[i], col_reg));
    }

    instructions.push_back(
        make_result_row(output_regs[0], (int32_t)output_regs.size()));
  } else {
    // UPDATE
    ArenaVector<int> current_regs;
    for (size_t i = 0; i < table->schema.columns.size(); i++) {
      int col_reg = regs.get("current_col_" + std::to_string(i));
      current_regs.push_back(col_reg);
      instructions.push_back(make_column(cursor_id, (int32_t)i, col_reg));
    }

    for (const auto &set_col : set_columns) {
      uint32_t col_idx =
          resolve_column_index(set_col.first.c_str(), table_name);
      int reg = regs.get("update_col_" + std::to_string(col_idx));
      load_value(instructions, set_col.second, reg);
      instructions.push_back(make_move(reg, current_regs[col_idx]));
    }

    int record_reg = regs.get("record");
    instructions.push_back(make_record(
        current_regs[0], (int32_t)table->schema.columns.size(), record_reg));

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
ArenaVector<VMInstruction>build_direct_rowid_operation(
    const ArenaString &table_name, const ArenaVector<SET_PAIR> &set_columns,
    const WhereCondition &primary_condition,
    const ArenaVector<WhereCondition> &remaining_conditions,
    RegisterAllocator &regs, ParsedParameters::Operation operation,
    const ArenaVector<ArenaString> &select_columns,
    const ArenaString &aggregate_func) {

  ArenaVector<VMInstruction>instructions;
  ArenaMap<ArenaString, int> labels;
  const int cursor_id = 0;

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  if (operation == ParsedParameters::SELECT ||
      operation == ParsedParameters::AGGREGATE) {
    instructions.push_back(make_open_read(cursor_id, table_name_str));
  } else {
    instructions.push_back(make_open_write(cursor_id, table_name_str));
  }

  Table *table = get_table(const_cast<char *>(table_name.c_str()));
  if (!table) {
    return {make_halt()};
  }

  int agg_reg = -1;
  if (operation == ParsedParameters::AGGREGATE) {
    agg_reg = regs.get("agg");
    instructions.push_back(make_agg_reset(aggregate_func.c_str()));
  }

  ArenaMap<uint32_t, int> indexes_to_update;
  if (operation == ParsedParameters::UPDATE) {
    int cursor_idx = 1;
    for (const auto &[column, index] : table->indexes) {
      indexes_to_update[column] = cursor_idx++;
    }

    for (const auto &[ci, cursor_idx] : indexes_to_update) {
      instructions.push_back(make_open_write(cursor_idx, table_name_str, ci));
    }
  }

  int rowid_reg = regs.get("rowid_value");
  load_value(instructions, primary_condition.value, rowid_reg);

  instructions.push_back(make_seek_eq_label(cursor_id, rowid_reg, "end"));

  build_where_checks(instructions, cursor_id, remaining_conditions, "end",
                     regs);

  // Perform operation
  if (operation == ParsedParameters::DELETE) {
    instructions.push_back(make_delete(cursor_id));
  } else if (operation == ParsedParameters::AGGREGATE) {
    instructions.push_back(make_agg_step());
  } else if (operation == ParsedParameters::SELECT) {
    ArenaVector<int> output_regs;
    ArenaVector<uint32_t> columns_to_select;

    if (!select_columns.empty()) {
      for (const auto &col_name : select_columns) {
        columns_to_select.push_back(
            resolve_column_index(col_name.c_str(), table_name));
      }
    } else {
      for (size_t i = 0; i < table->schema.columns.size(); i++) {
        columns_to_select.push_back(i);
      }
    }

    for (size_t i = 0; i < columns_to_select.size(); i++) {
      int col_reg = regs.get("output_col_" + std::to_string(i));
      output_regs.push_back(col_reg);
      instructions.push_back(
          make_column(cursor_id, (int32_t)columns_to_select[i], col_reg));
    }

    instructions.push_back(
        make_result_row(output_regs[0], (int32_t)output_regs.size()));
  } else {
    // UPDATE
    ArenaVector<int> current_regs;
    for (size_t i = 0; i < table->schema.columns.size(); i++) {
      int col_reg = regs.get("current_col_" + std::to_string(i));
      current_regs.push_back(col_reg);
      instructions.push_back(make_column(cursor_id, (int32_t)i, col_reg));

      if (indexes_to_update.find(i) != indexes_to_update.end()) {
        instructions.push_back(
            make_seek_eq_label(indexes_to_update[i], col_reg, "end"));
      }
    }

    for (const auto &set_col : set_columns) {
      uint32_t col_idx =
          resolve_column_index(set_col.first.c_str(), table_name);
      int reg = regs.get("update_col_" + std::to_string(col_idx));
      load_value(instructions, set_col.second, reg);
      instructions.push_back(make_move(reg, current_regs[col_idx]));

      if (indexes_to_update.find(col_idx) != indexes_to_update.end()) {
        instructions.push_back(make_delete(indexes_to_update[col_idx]));
        instructions.push_back(make_insert(indexes_to_update[col_idx],
                                           current_regs[col_idx], rowid_reg));
      }
    }

    int record_reg = regs.get("record");
    instructions.push_back(make_record(
        current_regs[0], (int32_t)table->schema.columns.size(), record_reg));
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
    for (const auto &[ci, cursor_idx] : indexes_to_update) {
      instructions.push_back(make_close(cursor_idx));
    }
  }

  resolve_labels(instructions, labels);
  return instructions;
}

// Build index scan operation
ArenaVector<VMInstruction>build_index_scan_operation(
    const ArenaString &table_name, const ArenaVector<SET_PAIR> &set_columns,
    const WhereCondition &index_condition,
    const ArenaVector<WhereCondition> &remaining_conditions, uint32_t index_col,
    RegisterAllocator &regs, ParsedParameters::Operation operation,
    const ArenaVector<ArenaString> &select_columns,
    const ArenaString &aggregate_func) {

  ArenaVector<VMInstruction>instructions;
  ArenaMap<ArenaString, int> labels;
  const int table_cursor_id = 1;
  const int index_cursor_id = 0;

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  if (operation == ParsedParameters::SELECT ||
      operation == ParsedParameters::AGGREGATE) {
    instructions.push_back(make_open_read(index_cursor_id, table_name_str));
    instructions.push_back(make_open_read(table_cursor_id, table_name_str));
  } else {
    instructions.push_back(make_open_write(index_cursor_id, table_name_str,
                                           index_condition.column_index));
    instructions.push_back(make_open_write(table_cursor_id, table_name_str));
  }

  Table *table = get_table(const_cast<char *>(table_name.c_str()));
  if (!table) {
    return {make_halt()};
  }

  int agg_reg = -1;
  if (operation == ParsedParameters::AGGREGATE) {
    agg_reg = regs.get("agg");
    instructions.push_back(make_agg_reset(aggregate_func.c_str()));
  }

  int ii = 2;
  ArenaMap<uint32_t, int> indexes_to_update;
  if (operation == ParsedParameters::UPDATE) {
    for (const auto &[column, index] : table->indexes) {
      if (column != index_col) {
        indexes_to_update[column] = ii++;
        instructions.push_back(
            make_open_write(indexes_to_update[column], table_name_str, column));
      }
    }
  }

  int index_key_reg = regs.get("index_key");
  load_value(instructions, index_condition.value, index_key_reg);

  // Build seek based on operator type
  switch (index_condition.operator_type) {
  case EQ:
    instructions.push_back(
        make_seek_eq_label(index_cursor_id, index_key_reg, "end"));
    break;
  case GE:
    instructions.push_back(
        make_seek_ge_label(index_cursor_id, index_key_reg, "end"));
    break;
  case GT:
    instructions.push_back(
        make_seek_gt_label(index_cursor_id, index_key_reg, "end"));
    break;
  case LE:
    instructions.push_back(
        make_seek_le_label(index_cursor_id, index_key_reg, "end"));
    break;
  case LT:
    instructions.push_back(
        make_seek_lt_label(index_cursor_id, index_key_reg, "end"));
    break;
  default:
    instructions.push_back(
        make_seek_eq_label(index_cursor_id, index_key_reg, "end"));
  }

  labels["loop_start"] = instructions.size();

  int current_key_reg = regs.get("current_key");
  instructions.push_back(make_key(index_cursor_id, current_key_reg));

  OpCode negated_op = get_negated_opcode(index_condition.operator_type);
  switch (negated_op) {
  case OP_Eq:
    instructions.push_back(
        make_eq_label(current_key_reg, index_key_reg, "end"));
    break;
  case OP_Ne:
    instructions.push_back(
        make_ne_label(current_key_reg, index_key_reg, "end"));
    break;
  case OP_Lt:
    instructions.push_back(
        make_lt_label(current_key_reg, index_key_reg, "end"));
    break;
  case OP_Le:
    instructions.push_back(
        make_le_label(current_key_reg, index_key_reg, "end"));
    break;
  case OP_Gt:
    instructions.push_back(
        make_gt_label(current_key_reg, index_key_reg, "end"));
    break;
  case OP_Ge:
    instructions.push_back(
        make_ge_label(current_key_reg, index_key_reg, "end"));
    break;
  }

  int rowid_reg = regs.get("rowid");
  instructions.push_back(make_column(index_cursor_id, 0, rowid_reg));

  instructions.push_back(
      make_seek_eq_label(table_cursor_id, rowid_reg, "next_iteration"));

  build_where_checks(instructions, table_cursor_id, remaining_conditions,
                     "next_iteration", regs);

  // Perform operation
  if (operation == ParsedParameters::DELETE) {
    instructions.push_back(make_delete(table_cursor_id));
  } else if (operation == ParsedParameters::AGGREGATE) {
    instructions.push_back(make_agg_step());
  } else if (operation == ParsedParameters::SELECT) {
    ArenaVector<int> output_regs;
    ArenaVector<uint32_t> columns_to_select;

    if (!select_columns.empty()) {
      for (const auto &col_name : select_columns) {
        columns_to_select.push_back(
            resolve_column_index(col_name.c_str(), table_name));
      }
    } else {
      for (size_t i = 0; i < table->schema.columns.size(); i++) {
        columns_to_select.push_back(i);
      }
    }

    for (size_t i = 0; i < columns_to_select.size(); i++) {
      int col_reg = regs.get("output_col_" + std::to_string(i));
      output_regs.push_back(col_reg);
      instructions.push_back(
          make_column(table_cursor_id, (int32_t)columns_to_select[i], col_reg));
    }

    instructions.push_back(
        make_result_row(output_regs[0], (int32_t)output_regs.size()));
  } else {
    // UPDATE
    ArenaVector<int> current_regs;
    for (size_t i = 0; i < table->schema.columns.size(); i++) {
      int col_reg = regs.get("current_col_" + std::to_string(i));
      current_regs.push_back(col_reg);
      instructions.push_back(make_column(table_cursor_id, (int32_t)i, col_reg));

      if (indexes_to_update.find(i) != indexes_to_update.end()) {
        if (indexes_to_update[i] != index_cursor_id) {
          instructions.push_back(
              make_seek_eq_label(indexes_to_update[i], col_reg, "end"));
        }
      }
    }

    for (const auto &set_col : set_columns) {
      uint32_t col_idx =
          resolve_column_index(set_col.first.c_str(), table_name);
      int reg = regs.get("update_col_" + std::to_string(col_idx));
      load_value(instructions, set_col.second, reg);
      instructions.push_back(make_move(reg, current_regs[col_idx]));

      if (indexes_to_update.find(col_idx) != indexes_to_update.end()) {
        instructions.push_back(make_delete(indexes_to_update[col_idx]));
        instructions.push_back(make_insert(indexes_to_update[col_idx],
                                           current_regs[col_idx], rowid_reg));
      }
    }

    int record_reg = regs.get("record");
    instructions.push_back(make_record(
        current_regs[0], (int32_t)table->schema.columns.size(), record_reg));
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
    for (const auto &[ci, cursor_idx] : indexes_to_update) {
      instructions.push_back(make_close(cursor_idx));
    }
  }

  resolve_labels(instructions, labels);
  return instructions;
}
