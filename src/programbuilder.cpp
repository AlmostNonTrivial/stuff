#include "programbuilder.hpp"
#include "arena.hpp"
#include "defs.hpp"
#include "schema.hpp"
#include "vm.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>

void err(const char *msg) {
  std::cout << msg;
  exit(1);
}

// Helper 8: Comparison Instruction Builder
inline void
add_comparison_with_label(ArenaVector<VMInstruction, QueryArena> &instructions,
                          OpCode op, int reg1, int reg2, const char *label) {

  switch (op) {
  case OP_Eq:
    instructions.push_back(make_eq_label(reg1, reg2, label));
    break;
  case OP_Ne:
    instructions.push_back(make_ne_label(reg1, reg2, label));
    break;
  case OP_Lt:
    instructions.push_back(make_lt_label(reg1, reg2, label));
    break;
  case OP_Le:
    instructions.push_back(make_le_label(reg1, reg2, label));
    break;
  case OP_Gt:
    instructions.push_back(make_gt_label(reg1, reg2, label));
    break;
  case OP_Ge:
    instructions.push_back(make_ge_label(reg1, reg2, label));
    break;
  }
}

// Helper 9: Seek Instruction Builder
inline void
add_seek_instruction(ArenaVector<VMInstruction, QueryArena> &instructions,
                     CompareOp op, int cursor_id, int key_reg,
                     const char *label) {

  switch (op) {
  case EQ:
    instructions.push_back(make_seek_eq_label(cursor_id, key_reg, label));
    break;
  case GE:
    instructions.push_back(make_seek_ge_label(cursor_id, key_reg, label));
    break;
  case GT:
    instructions.push_back(make_seek_gt_label(cursor_id, key_reg, label));
    break;
  case LE:
    instructions.push_back(make_seek_le_label(cursor_id, key_reg, label));
    break;
  case LT:
    instructions.push_back(make_seek_lt_label(cursor_id, key_reg, label));
    break;
  default:
    instructions.push_back(make_seek_eq_label(cursor_id, key_reg, label));
    break;
  }
}

struct RegisterAllocator {
  ArenaMap<ArenaString<QueryArena>, uint32_t, QueryArena, REGISTER_COUNT>
      name_to_register;
  int next_register = 0;

  int get(const char *name) {
    auto it = name_to_register.find(name);
    if (it == nullptr) {
      name_to_register[name] = next_register;
      return next_register++;
    }
    return *it;
  }

  void clear() {
    name_to_register.clear();
    next_register = 0;
  }
};

// ============================================================================
// Helper Functions
// ============================================================================

void resolve_labels(
    ArenaVector<VMInstruction, QueryArena> &program,
    const ArenaMap<ArenaString<QueryArena>, int, QueryArena> &map) {
  for (size_t i = 0; i < program.size(); i++) {
    auto &inst = program[i];
    // Check p2 for label (stored as string in p4)
    if (inst.p4 && inst.p2 == -1) {
      auto it = map.find((const char *)inst.p4);
      if (it != nullptr) {
        inst.p2 = *it;
        inst.p4 = nullptr;
      }
    }
    // Check p3 for label
    if (inst.p4 && inst.p3 == -1) {
      auto it = map.find((const char *)inst.p4);
      if (it != nullptr) {
        inst.p3 = *it;
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

void load_value(ArenaVector<VMInstruction, QueryArena> &instructions,
                const VMValue &value, int target_reg) {
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

/* Because the tree scans left to right ... */
bool ascending(CompareOp op) { return op == GE || op == GT || op == EQ; }

// Helper to resolve column name to index
static uint32_t
resolve_column_index(const char *col_name,
                     const ArenaString<QueryArena> &table_name) {
  return get_column_index(const_cast<char *>(table_name.c_str()),
                          const_cast<char *>(col_name));
}

// Extract a single condition from a binary op node
static WhereCondition
extract_condition_from_binary_op(BinaryOpNode *op,
                                 const ArenaString<QueryArena> &table_name) {
  WhereCondition cond;

  // Left side should be column reference
  if (op->left->type == AST_COLUMN_REF) {
    ColumnRefNode *col = (ColumnRefNode *)op->left;
    cond.column_name = ArenaString<QueryArena>(col->name);
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
static ArenaVector<WhereCondition, QueryArena>
extract_where_conditions(WhereNode *where,
                         const ArenaString<QueryArena> &table_name) {
  ArenaVector<WhereCondition, QueryArena> conditions;

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

// Build where checks helper
void build_where_checks(
    ArenaVector<VMInstruction, QueryArena> &instructions, int cursor_id,
    const ArenaVector<WhereCondition, QueryArena> &conditions,
    const ArenaString<QueryArena> &skip_label, RegisterAllocator &regs) {

  for (size_t i = 0; i < conditions.size(); i++) {
    int col_reg = regs.get(("where_col_" + std::to_string(i)).c_str());
    instructions.push_back(
        make_column(cursor_id, (int32_t)conditions[i].column_index, col_reg));

    int compare_reg = regs.get(("compare_" + std::to_string(i)).c_str());
    load_value(instructions, conditions[i].value, compare_reg);

    OpCode negated = get_negated_opcode(conditions[i].operator_type);

    char *label_str = (char *)arena::alloc<QueryArena>(skip_label.size() + 1);
    strcpy(label_str, skip_label.c_str());

    add_comparison_with_label(instructions, negated, col_reg, compare_reg,
                              label_str);
  }
}

// ============================================================================
// Optimization Functions
// ============================================================================

double estimate_selectivity(const WhereCondition &condition,
                            const ArenaString<QueryArena> &table_name) {
  ArenaString<QueryArena> column_name = condition.column_name;

  Table *table = get_table(const_cast<char *>(table_name.c_str()));
  if (!table) {
    return 0.5;
  }

  auto idx_it = table->indexes.find(condition.column_index);
  bool is_indexed = (idx_it != nullptr);

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

ArenaVector<WhereCondition, QueryArena> optimize_where_conditions(
    const ArenaVector<WhereCondition, QueryArena> &conditions,
    const ArenaString<QueryArena> &table_name) {

  ArenaVector<WhereCondition, QueryArena> optimized = conditions;

  // Sort by selectivity
  std::sort(optimized.begin(), optimized.end(),
            [&](const WhereCondition &a, const WhereCondition &b) {
              return estimate_selectivity(a, table_name) <
                     estimate_selectivity(b, table_name);
            });

  return optimized;
}

AccessMethod
choose_access_method(const ArenaVector<WhereCondition, QueryArena> &conditions,
                     const ArenaString<QueryArena> &table_name) {
  ArenaVector<WhereCondition, QueryArena> sorted_conditions = conditions;
  std::stable_partition(
      sorted_conditions.begin(), sorted_conditions.end(),
      [](const WhereCondition &c) { return c.operator_type == EQ; });

  for (size_t i = 0; i < sorted_conditions.size(); i++) {
    auto &cond = sorted_conditions[i];
    if (cond.operator_type == EQ && cond.column_index == 0) {
      return {.type = AccessMethodEnum::DIRECT_ROWID,
              .primary_condition = const_cast<WhereCondition *>(&cond),
              .index_condition = nullptr,
              .index_col = cond.column_index};
    }
  }

  Table *table = get_table(const_cast<char *>(table_name.c_str()));
  if (table) {
    for (size_t i = 0; i < sorted_conditions.size(); i++) {
      auto &cond = sorted_conditions[i];

      if (table->indexes.contains(cond.column_index)) {
        return {.type = AccessMethodEnum::INDEX_SCAN,
                .primary_condition = nullptr,
                .index_condition = const_cast<WhereCondition *>(&cond),
                .index_col = cond.column_index};
      }
    }
  }

  return {.type = AccessMethodEnum::FULL_TABLE_SCAN,
          .primary_condition = nullptr,
          .index_condition = nullptr,
          .index_col = 0};
}

// ============================================================================
// SELECT Helper Functions
// ============================================================================

static void build_select_output(
    ArenaVector<VMInstruction, QueryArena> &instructions, int cursor_id,
    const ArenaVector<ArenaString<QueryArena>, QueryArena> &select_columns,
    const ArenaString<QueryArena> &table_name, RegisterAllocator &regs) {

  ArenaVector<int, QueryArena> output_regs;
  ArenaVector<uint32_t, QueryArena> columns_to_select;

  Table *table = get_table(const_cast<char *>(table_name.c_str()));

  if (!select_columns.empty()) {
    for (size_t i = 0; i < select_columns.size(); i++) {
      const auto &col_name = select_columns[i];
      columns_to_select.push_back(
          resolve_column_index(col_name.c_str(), table_name));
    }
  } else {
    for (size_t i = 0; i < table->schema.columns.size(); i++) {
      columns_to_select.push_back(i);
    }
  }

  for (size_t i = 0; i < columns_to_select.size(); i++) {
    int col_reg = regs.get(("output_col_" + std::to_string(i)).c_str());
    output_regs.push_back(col_reg);
    instructions.push_back(
        make_column(cursor_id, (int32_t)columns_to_select[i], col_reg));
  }

  instructions.push_back(
      make_result_row(output_regs[0], (int32_t)output_regs.size()));
}

// ============================================================================
// UPDATE Helper Functions
// ============================================================================

struct IndexUpdate {
  uint32_t column_index;
  int cursor_id;
  bool is_scan_index;
};

static ArenaVector<IndexUpdate, QueryArena>
setup_update_indexes(ArenaVector<VMInstruction, QueryArena> &instructions,
                     const ArenaVector<SetColumns, QueryArena> &set_columns,
                     const ArenaString<QueryArena> &table_name,
                     int scan_index_cursor_id, uint32_t scan_index_col,
                     int starting_cursor_id) {

  ArenaVector<IndexUpdate, QueryArena> indexes_to_update;
  Table *table = get_table(const_cast<char *>(table_name.c_str()));



  // Determine which columns are being updated
  ArenaSet<uint32_t, QueryArena> updated_columns;
  for (size_t i = 0; i < set_columns.size(); i++) {
    uint32_t col_idx =
        resolve_column_index(set_columns[i].first.c_str(), table_name);
    updated_columns.insert(col_idx);
  }

  // For each index on the table, check if its column is being updated
  int next_cursor_id = starting_cursor_id;
  for (int i = 0; i < table->indexes.size(); i++) {
    uint32_t indexed_col = *table->indexes.key_at(i);

    // If this indexed column is being updated
    if (updated_columns.contains(indexed_col)) {
      IndexUpdate idx_update;
      idx_update.column_index = indexed_col;
      idx_update.is_scan_index =
          (scan_index_cursor_id >= 0 && indexed_col == scan_index_col);

      if (idx_update.is_scan_index) {
        // Reuse the scan index cursor
        idx_update.cursor_id = scan_index_cursor_id;
      } else {
        // Allocate a new cursor and open it
        idx_update.cursor_id = next_cursor_id;
        instructions.push_back(
            make_open_write(next_cursor_id, table_name.c_str(), indexed_col));
        next_cursor_id++;
      }

      indexes_to_update.push_back(idx_update);
    }
  }

  return indexes_to_update;
}

static void build_update_record(
    ArenaVector<VMInstruction, QueryArena> &instructions, int table_cursor_id,
    const ArenaVector<SetColumns, QueryArena> &set_columns,
    const ArenaVector<IndexUpdate, QueryArena> &indexes_to_update,
    const ArenaString<QueryArena> &table_name, RegisterAllocator &regs,
    ArenaMap<ArenaString<QueryArena>, int, QueryArena> &labels) {

  Table *table = get_table(const_cast<char *>(table_name.c_str()));

  // Read all current column values
  ArenaVector<int, QueryArena> current_regs;
  for (size_t i = 0; i < table->schema.columns.size(); i++) {
    int col_reg = regs.get(("current_col_" + std::to_string(i)).c_str());
    current_regs.push_back(col_reg);
    instructions.push_back(make_column(table_cursor_id, (int32_t)i, col_reg));
  }

  // Get the rowid (column 0)
  int rowid_reg = regs.get("rowid");
  instructions.push_back(make_column(table_cursor_id, 0, rowid_reg));

  // Position and delete from non-scan indexes that are being updated
  for (size_t i = 0; i < indexes_to_update.size(); i++) {
    if (!indexes_to_update[i].is_scan_index) {
      int cursor_id = indexes_to_update[i].cursor_id;
      uint32_t col_idx = indexes_to_update[i].column_index;
      instructions.push_back(
          make_seek_eq_label(cursor_id, current_regs[col_idx], "end"));
      instructions.push_back(make_delete(cursor_id));
    }
  }

  // Apply updates to column values
  for (size_t i = 0; i < set_columns.size(); i++) {
    uint32_t col_idx =
        resolve_column_index(set_columns[i].first.c_str(), table_name);
    int reg = regs.get(("update_col_" + std::to_string(col_idx)).c_str());
    load_value(instructions, set_columns[i].second, reg);
    instructions.push_back(make_move(reg, current_regs[col_idx]));
  }

  // Insert new entries into non-scan indexes
  for (size_t i = 0; i < indexes_to_update.size(); i++) {
    if (!indexes_to_update[i].is_scan_index) {
      int cursor_id = indexes_to_update[i].cursor_id;
      uint32_t col_idx = indexes_to_update[i].column_index;
      instructions.push_back(
          make_insert(cursor_id, current_regs[col_idx], rowid_reg));
    }
  }

  // Write updated record to table
  // IMPORTANT FIX: Record excludes column 0 (the key)!
  // Start from register containing column 1, and include (columns.size() - 1) columns
  int record_reg = regs.get("record");
  instructions.push_back(make_record(
      current_regs[1], (int32_t)(table->schema.columns.size() - 1), record_reg));
  instructions.push_back(make_update(table_cursor_id, record_reg));
}

// ============================================================================
// SELECT Operation Builders
// ============================================================================

static ArenaVector<VMInstruction, QueryArena> build_select_full_table_scan(
    const ArenaString<QueryArena> &table_name,
    const ArenaVector<WhereCondition, QueryArena> &conditions,
    const ArenaVector<ArenaString<QueryArena>, QueryArena> &select_columns,
    const ArenaString<QueryArena> &aggregate_func) {

  RegisterAllocator regs;
  ArenaVector<VMInstruction, QueryArena> instructions;
  ArenaMap<ArenaString<QueryArena>, int, QueryArena> labels;
  const int cursor_id = 0;



  instructions.push_back(make_open_read(cursor_id, table_name.c_str()));

  Table *table = get_table(const_cast<char *>(table_name.c_str()));
  if (!table) {
    ArenaVector<VMInstruction, QueryArena> vec;
    vec.push_back(make_halt());
    return vec;
  }

  // Initialize aggregate if needed
  if (!aggregate_func.empty()) {
    instructions.push_back(make_agg_reset(aggregate_func.c_str()));
  }

  // Load comparison values
  for (size_t i = 0; i < conditions.size(); i++) {
    int reg = regs.get(("compare_" + std::to_string(i)).c_str());
    load_value(instructions, conditions[i].value, reg);
  }

  instructions.push_back(make_rewind_label(cursor_id, "end"));

  labels["loop_start"] = instructions.size();

  build_where_checks(instructions, cursor_id, conditions, "next_record", regs);

  if (!aggregate_func.empty()) {
    instructions.push_back(make_agg_step());
  } else {
    build_select_output(instructions, cursor_id, select_columns, table_name,
                        regs);
  }

  labels["next_record"] = instructions.size();
  instructions.push_back(make_next_label(cursor_id, "loop_start"));

  labels["end"] = instructions.size();

  if (!aggregate_func.empty()) {
    int output_reg = regs.get("output");
    instructions.push_back(make_agg_final(output_reg));
    instructions.push_back(make_result_row(output_reg, 1));
  }

  instructions.push_back(make_close(cursor_id));

  resolve_labels(instructions, labels);
  return instructions;
}

static ArenaVector<VMInstruction, QueryArena> build_select_direct_rowid(
    const ArenaString<QueryArena> &table_name,
    const WhereCondition &primary_condition,
    const ArenaVector<WhereCondition, QueryArena> &remaining_conditions,
    const ArenaVector<ArenaString<QueryArena>, QueryArena> &select_columns,
    const ArenaString<QueryArena> &aggregate_func) {

  RegisterAllocator regs;
  ArenaVector<VMInstruction, QueryArena> instructions;
  ArenaMap<ArenaString<QueryArena>, int, QueryArena> labels;
  const int cursor_id = 0;

  instructions.push_back(make_open_read(cursor_id, table_name.c_str()));

  Table *table = get_table(const_cast<char *>(table_name.c_str()));
  if (!table) {
    ArenaVector<VMInstruction, QueryArena> vec;
    vec.push_back(make_halt());
    return vec;
  }

  if (!aggregate_func.empty()) {
    instructions.push_back(make_agg_reset(aggregate_func.c_str()));
  }

  int rowid_reg = regs.get("rowid_value");
  load_value(instructions, primary_condition.value, rowid_reg);

  instructions.push_back(make_seek_eq_label(cursor_id, rowid_reg, "end"));

  build_where_checks(instructions, cursor_id, remaining_conditions, "end",
                     regs);

  if (!aggregate_func.empty()) {
    instructions.push_back(make_agg_step());
  } else {
    build_select_output(instructions, cursor_id, select_columns, table_name,
                        regs);
  }

  labels["end"] = instructions.size();

  if (!aggregate_func.empty()) {
    int output_reg = regs.get("output");
    instructions.push_back(make_agg_final(output_reg));
    instructions.push_back(make_result_row(output_reg, 1));
  }

  instructions.push_back(make_close(cursor_id));

  resolve_labels(instructions, labels);
  return instructions;
}

static ArenaVector<VMInstruction, QueryArena> build_select_index_scan(
    const ArenaString<QueryArena> &table_name,
    const WhereCondition &index_condition,
    const ArenaVector<WhereCondition, QueryArena> &remaining_conditions,
    uint32_t index_col,
    const ArenaVector<ArenaString<QueryArena>, QueryArena> &select_columns,
    const ArenaString<QueryArena> &aggregate_func) {

  RegisterAllocator regs;
  ArenaVector<VMInstruction, QueryArena> instructions;
  ArenaMap<ArenaString<QueryArena>, int, QueryArena> labels;
  const int table_cursor_id = 1;
  const int index_cursor_id = 0;

  instructions.push_back(make_open_read(index_cursor_id, table_name.c_str(), index_col));
  instructions.push_back(make_open_read(table_cursor_id, table_name.c_str()));

  Table *table = get_table(const_cast<char *>(table_name.c_str()));

  if (!aggregate_func.empty()) {
    instructions.push_back(make_agg_reset(aggregate_func.c_str()));
  }

  int index_key_reg = regs.get("index_key");
  load_value(instructions, index_condition.value, index_key_reg);

  // Build seek based on operator type
  add_seek_instruction(instructions, index_condition.operator_type, index_cursor_id, index_key_reg, "end");

  labels["loop_start"] = instructions.size();


  int current_key_reg = regs.get("current_key");
  // the col of the index is 0, so make key
  instructions.push_back(make_key(index_cursor_id, current_key_reg));

  OpCode negated_op = get_negated_opcode(index_condition.operator_type);
  add_comparison_with_label(instructions, negated_op, current_key_reg, index_key_reg, "end");

  int rowid_reg = regs.get("rowid");
  // load the 'record' of the index, which we'll then search for the record for
  instructions.push_back(make_column(index_cursor_id, 1, rowid_reg));

  instructions.push_back(make_seek_eq_label(table_cursor_id, rowid_reg, "next_iteration"));

  build_where_checks(instructions, table_cursor_id, remaining_conditions, "next_iteration", regs);

  if (!aggregate_func.empty()) {
    instructions.push_back(make_agg_step());
  } else {
    build_select_output(instructions, table_cursor_id, select_columns, table_name, regs);
  }

  labels["next_iteration"] = instructions.size();

  bool use_next = ascending(index_condition.operator_type);
  if (use_next) {
    instructions.push_back(make_next_label(index_cursor_id, "loop_start"));
  } else {
    instructions.push_back(make_prev_label(index_cursor_id, "loop_start"));
  }

  labels["end"] = instructions.size();

  if (!aggregate_func.empty()) {
    int output_reg = regs.get("output");
    instructions.push_back(make_agg_final(output_reg));
    instructions.push_back(make_result_row(output_reg, 1));
  }

  instructions.push_back(make_close(index_cursor_id));
  instructions.push_back(make_close(table_cursor_id));

  resolve_labels(instructions, labels);
  return instructions;
}

// ============================================================================
// DELETE Operation Builders
// ============================================================================

static ArenaVector<VMInstruction, QueryArena> build_delete_full_table_scan(
    const ArenaString<QueryArena> &table_name,
    const ArenaVector<WhereCondition, QueryArena> &conditions) {

  RegisterAllocator regs;
  ArenaVector<VMInstruction, QueryArena> instructions;
  ArenaMap<ArenaString<QueryArena>, int, QueryArena> labels;
  const int cursor_id = 0;

  instructions.push_back(make_open_write(cursor_id, table_name.c_str()));

  Table *table = get_table(const_cast<char *>(table_name.c_str()));
  if (!table) {
    ArenaVector<VMInstruction, QueryArena> vec;
    vec.push_back(make_halt());
    return vec;
  }

  // Load comparison values
  for (size_t i = 0; i < conditions.size(); i++) {
    int reg = regs.get(("compare_" + std::to_string(i)).c_str());
    load_value(instructions, conditions[i].value, reg);
  }

  instructions.push_back(make_rewind_label(cursor_id, "end"));

  labels["loop_start"] = instructions.size();

  build_where_checks(instructions, cursor_id, conditions, "next_record", regs);

  instructions.push_back(make_delete(cursor_id));

  labels["next_record"] = instructions.size();
  instructions.push_back(make_next_label(cursor_id, "loop_start"));

  labels["end"] = instructions.size();

  instructions.push_back(make_close(cursor_id));

  resolve_labels(instructions, labels);
  return instructions;
}

static ArenaVector<VMInstruction, QueryArena> build_delete_direct_rowid(
    const ArenaString<QueryArena> &table_name,
    const WhereCondition &primary_condition,
    const ArenaVector<WhereCondition, QueryArena> &remaining_conditions) {

  RegisterAllocator regs;
  ArenaVector<VMInstruction, QueryArena> instructions;
  ArenaMap<ArenaString<QueryArena>, int, QueryArena> labels;
  const int cursor_id = 0;



  instructions.push_back(make_open_write(cursor_id, table_name.c_str()));

  Table *table = get_table(const_cast<char *>(table_name.c_str()));
  if (!table) {
    ArenaVector<VMInstruction, QueryArena> vec;
    vec.push_back(make_halt());
    return vec;
  }

  int rowid_reg = regs.get("rowid_value");
  load_value(instructions, primary_condition.value, rowid_reg);

  instructions.push_back(make_seek_eq_label(cursor_id, rowid_reg, "end"));

  build_where_checks(instructions, cursor_id, remaining_conditions, "end",
                     regs);

  instructions.push_back(make_delete(cursor_id));

  labels["end"] = instructions.size();
  instructions.push_back(make_close(cursor_id));

  resolve_labels(instructions, labels);
  return instructions;
}

static ArenaVector<VMInstruction, QueryArena> build_delete_index_scan(
    const ArenaString<QueryArena> &table_name,
    const WhereCondition &index_condition,
    const ArenaVector<WhereCondition, QueryArena> &remaining_conditions,
    uint32_t index_col) {

  RegisterAllocator regs;
  ArenaVector<VMInstruction, QueryArena> instructions;
  ArenaMap<ArenaString<QueryArena>, int, QueryArena> labels;
  const int table_cursor_id = 1;
  const int index_cursor_id = 0;



  instructions.push_back(
      make_open_write(index_cursor_id, table_name.c_str(), index_col));
  instructions.push_back(make_open_write(table_cursor_id, table_name.c_str()));

  Table *table = get_table(const_cast<char *>(table_name.c_str()));

  int index_key_reg = regs.get("index_key");
  load_value(instructions, index_condition.value, index_key_reg);

  // Build seek based on operator type
  add_seek_instruction(instructions, index_condition.operator_type,
                       index_cursor_id, index_key_reg, "end");

  labels["loop_start"] = instructions.size();

  int current_key_reg = regs.get("current_key");
  instructions.push_back(make_key(index_cursor_id, current_key_reg));

  OpCode negated_op = get_negated_opcode(index_condition.operator_type);
  add_comparison_with_label(instructions, negated_op, current_key_reg,
                            index_key_reg, "end");

  int rowid_reg = regs.get("rowid");
  instructions.push_back(make_column(index_cursor_id, 1, rowid_reg));

  instructions.push_back(
      make_seek_eq_label(table_cursor_id, rowid_reg, "next_iteration"));

  build_where_checks(instructions, table_cursor_id, remaining_conditions,
                     "next_iteration", regs);

  instructions.push_back(make_delete(table_cursor_id));

  labels["next_iteration"] = instructions.size();

  bool use_next = ascending(index_condition.operator_type);
  if (use_next) {
    instructions.push_back(make_next_label(index_cursor_id, "loop_start"));
  } else {
    instructions.push_back(make_prev_label(index_cursor_id, "loop_start"));
  }

  labels["end"] = instructions.size();

  instructions.push_back(make_close(index_cursor_id));
  instructions.push_back(make_close(table_cursor_id));

  resolve_labels(instructions, labels);
  return instructions;
}

// ============================================================================
// UPDATE Operation Builders
// ============================================================================

static ArenaVector<VMInstruction, QueryArena> build_update_full_table_scan(
    const ArenaString<QueryArena> &table_name,
    const ArenaVector<SetColumns, QueryArena> &set_columns,
    const ArenaVector<WhereCondition, QueryArena> &conditions) {

  RegisterAllocator regs;
  ArenaVector<VMInstruction, QueryArena> instructions;
  ArenaMap<ArenaString<QueryArena>, int, QueryArena> labels;
  const int cursor_id = 0;



  instructions.push_back(make_open_write(cursor_id, table_name.c_str()));

  Table *table = get_table(const_cast<char *>(table_name.c_str()));
  if (!table) {
    ArenaVector<VMInstruction, QueryArena> vec;
    vec.push_back(make_halt());
    return vec;
  }

  // Setup indexes that need updating
  auto indexes_to_update =
      setup_update_indexes(instructions, set_columns, table_name, -1, 0, 1);

  // Load comparison values
  for (size_t i = 0; i < conditions.size(); i++) {
    int reg = regs.get(("compare_" + std::to_string(i)).c_str());
    load_value(instructions, conditions[i].value, reg);
  }

  instructions.push_back(make_rewind_label(cursor_id, "end"));

  labels["loop_start"] = instructions.size();

  build_where_checks(instructions, cursor_id, conditions, "next_record", regs);

  build_update_record(instructions, cursor_id, set_columns, indexes_to_update,
                      table_name, regs, labels);

  labels["next_record"] = instructions.size();
  instructions.push_back(make_next_label(cursor_id, "loop_start"));

  labels["end"] = instructions.size();

  instructions.push_back(make_close(cursor_id));

  // Close additional index cursors
  for (size_t i = 0; i < indexes_to_update.size(); i++) {
    if (!indexes_to_update[i].is_scan_index) {
      instructions.push_back(make_close(indexes_to_update[i].cursor_id));
    }
  }

  resolve_labels(instructions, labels);
  return instructions;
}

static ArenaVector<VMInstruction, QueryArena> build_update_direct_rowid(
    const ArenaString<QueryArena> &table_name,
    const ArenaVector<SetColumns, QueryArena> &set_columns,
    const WhereCondition &primary_condition,
    const ArenaVector<WhereCondition, QueryArena> &remaining_conditions) {

  RegisterAllocator regs;
  ArenaVector<VMInstruction, QueryArena> instructions;
  ArenaMap<ArenaString<QueryArena>, int, QueryArena> labels;
  const int cursor_id = 0;



  instructions.push_back(make_open_write(cursor_id, table_name.c_str()));

  Table *table = get_table(const_cast<char *>(table_name.c_str()));
  if (!table) {
    ArenaVector<VMInstruction, QueryArena> vec;
    vec.push_back(make_halt());
    return vec;
  }

  // Setup indexes that need updating
  auto indexes_to_update =
      setup_update_indexes(instructions, set_columns, table_name, -1, 0, 1);

  int rowid_reg = regs.get("rowid_value");
  load_value(instructions, primary_condition.value, rowid_reg);

  instructions.push_back(make_seek_eq_label(cursor_id, rowid_reg, "end"));

  build_where_checks(instructions, cursor_id, remaining_conditions, "end",
                     regs);

  build_update_record(instructions, cursor_id, set_columns, indexes_to_update,
                      table_name, regs, labels);

  labels["end"] = instructions.size();
  instructions.push_back(make_close(cursor_id));

  // Close additional index cursors
  for (size_t i = 0; i < indexes_to_update.size(); i++) {
    if (!indexes_to_update[i].is_scan_index) {
      instructions.push_back(make_close(indexes_to_update[i].cursor_id));
    }
  }

  resolve_labels(instructions, labels);
  return instructions;
}

static ArenaVector<VMInstruction, QueryArena> build_update_index_scan(
    const ArenaString<QueryArena> &table_name,
    const ArenaVector<SetColumns, QueryArena> &set_columns,
    const WhereCondition &index_condition,
    const ArenaVector<WhereCondition, QueryArena> &remaining_conditions,
    uint32_t index_col) {

  RegisterAllocator regs;
  ArenaVector<VMInstruction, QueryArena> instructions;
  ArenaMap<ArenaString<QueryArena>, int, QueryArena> labels;
  const int table_cursor_id = 1;
  const int index_cursor_id = 0;

  instructions.push_back(make_open_write(index_cursor_id, table_name.c_str(), index_col));
  instructions.push_back(make_open_write(table_cursor_id, table_name.c_str()));

  Table *table = get_table(const_cast<char *>(table_name.c_str()));

  // Setup indexes that need updating
  auto indexes_to_update = setup_update_indexes(
      instructions, set_columns, table_name, index_cursor_id, index_col, 2);

  int index_key_reg = regs.get("index_key");
  load_value(instructions, index_condition.value, index_key_reg);

  // Build seek based on operator type
  //
  add_seek_instruction(instructions, index_condition.operator_type,
                       index_cursor_id, index_key_reg, "end");

  labels["loop_start"] = instructions.size();

  int current_key_reg = regs.get("current_key");
  instructions.push_back(make_key(index_cursor_id, current_key_reg));

  OpCode negated_op = get_negated_opcode(index_condition.operator_type);
  add_comparison_with_label(instructions, negated_op, current_key_reg,
                            index_key_reg, "end");

  int rowid_reg = regs.get("rowid");
  instructions.push_back(make_column(index_cursor_id, 1, rowid_reg));


  instructions.push_back(
      make_seek_eq_label(table_cursor_id, rowid_reg, "next_iteration"));

  build_where_checks(instructions, table_cursor_id, remaining_conditions,
                     "next_iteration", regs);

  build_update_record(instructions, table_cursor_id, set_columns,
                      indexes_to_update, table_name, regs, labels);

  labels["next_iteration"] = instructions.size();

  bool use_next = ascending(index_condition.operator_type);
  if (use_next) {
    instructions.push_back(make_next_label(index_cursor_id, "loop_start"));
  } else {
    instructions.push_back(make_prev_label(index_cursor_id, "loop_start"));
  }

  labels["end"] = instructions.size();

  instructions.push_back(make_close(index_cursor_id));
  instructions.push_back(make_close(table_cursor_id));

  // Close additional index cursors for UPDATE
  for (size_t i = 0; i < indexes_to_update.size(); i++) {
    if (!indexes_to_update[i].is_scan_index) {
      instructions.push_back(make_close(indexes_to_update[i].cursor_id));
    }
  }

  resolve_labels(instructions, labels);
  return instructions;
}

// ============================================================================
// Main Public Operation Functions
// ============================================================================

ArenaVector<VMInstruction, QueryArena>
build_select(const ParsedParameters &options) {
  RegisterAllocator regs;

  auto optimized_conditions =
      optimize_where_conditions(options.where_conditions, options.table_name);

  auto access_method =
      choose_access_method(optimized_conditions, options.table_name);

  ArenaVector<VMInstruction, QueryArena> instructions;

  switch (access_method.type) {
  case AccessMethodEnum::DIRECT_ROWID: {
    ArenaVector<WhereCondition, QueryArena> remaining;
    for (size_t i = 0; i < optimized_conditions.size(); i++) {
      const auto &c = optimized_conditions[i];
      if (&c != access_method.primary_condition) {
        remaining.push_back(c);
      }
    }
    instructions = build_select_direct_rowid(
        options.table_name, *access_method.primary_condition, remaining,
        options.select_columns, options.aggregate);
    break;
  }
  case AccessMethodEnum::INDEX_SCAN: {
    ArenaVector<WhereCondition, QueryArena> remaining;
    for (size_t i = 0; i < optimized_conditions.size(); i++) {
      const auto &c = optimized_conditions[i];
      if (&c != access_method.index_condition) {
        remaining.push_back(c);
      }
    }
    instructions = build_select_index_scan(
        options.table_name, *access_method.index_condition, remaining,
        access_method.index_col, options.select_columns, options.aggregate);
    break;
  }
  case AccessMethodEnum::FULL_TABLE_SCAN:
  default:
    instructions =
        build_select_full_table_scan(options.table_name, optimized_conditions,
                                     options.select_columns, options.aggregate);
  }

  if (!options.order_by.column_name.empty()) {
    uint32_t col_idx = resolve_column_index(
        options.order_by.column_name.c_str(), options.table_name);
    instructions.push_back(make_sort(col_idx, !options.order_by.asc));
  }

  instructions.push_back(make_flush());
  instructions.push_back(make_halt());

  return instructions;
}

ArenaVector<VMInstruction, QueryArena>
build_update(const ParsedParameters &options) {
  RegisterAllocator regs;

  auto optimized_conditions =
      optimize_where_conditions(options.where_conditions, options.table_name);

  auto access_method =
      choose_access_method(optimized_conditions, options.table_name);

  ArenaVector<VMInstruction, QueryArena> instructions;

  switch (access_method.type) {
  case AccessMethodEnum::DIRECT_ROWID: {
    ArenaVector<WhereCondition, QueryArena> remaining;
    for (size_t i = 0; i < optimized_conditions.size(); i++) {
      const auto &c = optimized_conditions[i];
      if (&c != access_method.primary_condition) {
        remaining.push_back(c);
      }
    }
    instructions =
        build_update_direct_rowid(options.table_name, options.set_columns,
                                  *access_method.primary_condition, remaining);
    break;
  }
  case AccessMethodEnum::INDEX_SCAN: {
    ArenaVector<WhereCondition, QueryArena> remaining;
    for (size_t i = 0; i < optimized_conditions.size(); i++) {
      const auto &c = optimized_conditions[i];
      if (&c != access_method.index_condition) {
        remaining.push_back(c);
      }
    }
    instructions = build_update_index_scan(
        options.table_name, options.set_columns, *access_method.index_condition,
        remaining, access_method.index_col);
    break;
  }
  case AccessMethodEnum::FULL_TABLE_SCAN:
  default:
    instructions = build_update_full_table_scan(
        options.table_name, options.set_columns, optimized_conditions);
  }

  instructions.push_back(make_flush());
  instructions.push_back(make_halt());

  return instructions;
}

ArenaVector<VMInstruction, QueryArena>
build_delete(const ParsedParameters &options) {
  RegisterAllocator regs;

  auto optimized_conditions =
      optimize_where_conditions(options.where_conditions, options.table_name);

  auto access_method =
      choose_access_method(optimized_conditions, options.table_name);

  ArenaVector<VMInstruction, QueryArena> instructions;

  switch (access_method.type) {
  case AccessMethodEnum::DIRECT_ROWID: {
    ArenaVector<WhereCondition, QueryArena> remaining;
    for (size_t i = 0; i < optimized_conditions.size(); i++) {
      const auto &c = optimized_conditions[i];
      if (&c != access_method.primary_condition) {
        remaining.push_back(c);
      }
    }
    instructions = build_delete_direct_rowid(
        options.table_name, *access_method.primary_condition, remaining);
    break;
  }
  case AccessMethodEnum::INDEX_SCAN: {
    ArenaVector<WhereCondition, QueryArena> remaining;
    for (size_t i = 0; i < optimized_conditions.size(); i++) {
      const auto &c = optimized_conditions[i];
      if (&c != access_method.index_condition) {
        remaining.push_back(c);
      }
    }
    instructions = build_delete_index_scan(options.table_name,
                                           *access_method.index_condition,
                                           remaining, access_method.index_col);
    break;
  }
  case AccessMethodEnum::FULL_TABLE_SCAN:
  default:
    instructions =
        build_delete_full_table_scan(options.table_name, optimized_conditions);
  }

  instructions.push_back(make_flush());
  instructions.push_back(make_halt());

  return instructions;
}

// ============================================================================
// Other Operations (unchanged)
// ============================================================================

// Generate aggregate instructions
ArenaVector<VMInstruction, QueryArena>
aggregate(const ArenaString<QueryArena> &table_name, const char *agg_func,
          uint32_t *column_index,
          const ArenaVector<WhereCondition, QueryArena> &where_conditions) {

  if (strcmp(agg_func, "COUNT") != 0 && column_index == nullptr) {
    ArenaVector<VMInstruction, QueryArena> vec;
    return vec;
  }

  Table *table = get_table(const_cast<char *>(table_name.c_str()));
  if (!table) {
    ArenaVector<VMInstruction, QueryArena> vec;
    vec.push_back(make_halt());
    return vec;
  }

  if (where_conditions.size() > 0) {
    ParsedParameters options;
    options.table_name = table_name;
    options.where_conditions = where_conditions;
    options.operation = ParsedParameters::SELECT;
    options.aggregate = ArenaString<QueryArena>(agg_func);

    return build_select(options);
  }

  // Simple aggregate without WHERE
  RegisterAllocator regs;
  ArenaVector<VMInstruction, QueryArena> instructions;
  const int cursor_id = 0;
  int agg_reg = regs.get("agg");
  int output_reg = regs.get("output");



  instructions.push_back(make_open_read(cursor_id, table_name.c_str()));
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

// Create table
ArenaVector<VMInstruction, QueryArena>
build_create_table(const ArenaString<QueryArena> &table_name,
                   const ArenaVector<ColumnInfo, QueryArena> &columns) {

  TableSchema *schema =
      (TableSchema *)arena::alloc<QueryArena>(sizeof(TableSchema));
  schema->table_name.assign(table_name.c_str());
  schema->columns.set(columns);

  ArenaVector<VMInstruction, QueryArena> vec;
  vec.push_back(make_create_table(schema));
  vec.push_back(make_halt());
  return vec;
}

// Drop table
ArenaVector<VMInstruction, QueryArena>
build_drop_table(const ArenaString<QueryArena> &table_name) {
  char *name = (char *)arena::alloc<QueryArena>(table_name.size() + 1);
  strcpy(name, table_name.c_str());

  ArenaVector<VMInstruction, QueryArena> vec;
  vec.push_back(make_drop_table(name));
  vec.push_back(make_halt());
  return vec;
}

// Drop index
ArenaVector<VMInstruction, QueryArena>
build_drop_index(const ArenaString<QueryArena> &index_name) {
  char *name = (char *)arena::alloc<QueryArena>(index_name.size() + 1);
  strcpy(name, index_name.c_str());

  ArenaVector<VMInstruction, QueryArena> vec;
  vec.push_back(make_drop_index(0, name));
  vec.push_back(make_halt());
  return vec;
}

// Create index
ArenaVector<VMInstruction, QueryArena>
build_create_index(const ArenaString<QueryArena> &table_name,
                   uint32_t column_index, DataType key_type) {
  RegisterAllocator regs;
  ArenaVector<VMInstruction, QueryArena> instructions;
  ArenaMap<ArenaString<QueryArena>, int, QueryArena> labels;

  const int table_cursor_id = 0;
  const int index_cursor_id = 1;


  instructions.push_back(make_create_index(column_index, table_name.c_str()));
  instructions.push_back(make_open_read(table_cursor_id, table_name.c_str()));
  instructions.push_back(make_open_write(index_cursor_id, table_name.c_str(), column_index));

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
// In build_insert function, fix the MakeRecord call:
ArenaVector<VMInstruction, QueryArena>
build_insert(const ArenaString<QueryArena> &table_name,
             const ArenaVector<SetColumns, QueryArena> &values) {
  RegisterAllocator regs;
  ArenaVector<VMInstruction, QueryArena> instructions;

  const int table_cursor_id = 0;

  // Get indexes for this table
  Table *table = get_table(const_cast<char *>(table_name.c_str()));


  // <column_index, cursorid>
  ArenaMap<uint32_t, int, QueryArena> indexes_to_insert;
  int cursor_id = 1;

  instructions.push_back(make_open_write(table_cursor_id, table_name.c_str()));

  // as we make a full record, we need to update all indexes
  for (int i = 0; i < table->indexes.size(); i++) {
    uint32_t column_index = *table->indexes.key_at(i);
    auto index_cursor = cursor_id++;
    indexes_to_insert[column_index] = index_cursor;
    instructions.push_back(make_open_write(index_cursor, table_name.c_str(), column_index));
  }

  // Load values into registers, full record on insert
  ArenaVector<int, QueryArena> value_regs;
  for (size_t i = 0; i < values.size(); i++) {
    // key is at value_regs[0]
    int reg = regs.get(("value_" + std::to_string(i)).c_str());
    value_regs.push_back(reg);
    load_value(instructions, values[i].second, reg);

    // Find column index for this value
    uint32_t col_idx = get_column_index(const_cast<char *>(table_name.c_str()),
                         const_cast<char *>(values[i].first.c_str()));

    // Insert into indexes if needed
    auto index_cursor_id = indexes_to_insert.find(col_idx);
    if (index_cursor_id != nullptr) {
      instructions.push_back(make_insert(*index_cursor_id, reg, value_regs[0]));
    }
  }

  int record_reg = regs.get("record");

  // IMPORTANT FIX: Record excludes column 0 (the key)!
  // Start from value_regs[1] and use (values.size() - 1) columns
  instructions.push_back(make_record(value_regs[1], (int32_t)(values.size() - 1), record_reg));

  // Key is in value_regs[0], record is in record_reg
  instructions.push_back(make_insert(table_cursor_id, value_regs[0], record_reg));
  instructions.push_back(make_close(table_cursor_id));
  instructions.push_back(make_halt());

  return instructions;
}

// ============================================================================
// AST Building Functions (unchanged)
// ============================================================================

// Build SELECT from AST
static ArenaVector<VMInstruction, QueryArena>
build_select_from_ast(SelectNode *node) {
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

    ArenaVector<WhereCondition, QueryArena> conditions =
        extract_where_conditions(node->where, node->table);

    return aggregate(node->table, agg->function, column_index, conditions);
  }

  // Regular SELECT
  ParsedParameters params;
  params.table_name = node->table;
  params.operation = ParsedParameters::SELECT;
  params.where_conditions = extract_where_conditions(node->where, node->table);

  // Extract column list
  ArenaVector<ArenaString<QueryArena>, QueryArena> select_columns;
  if (!node->columns.empty()) {
    for (size_t i = 0; i < node->columns.size(); i++) {
      ASTNode *col_node = node->columns[i];
      if (col_node->type == AST_COLUMN_REF) {
        ColumnRefNode *col = (ColumnRefNode *)col_node;
        select_columns.push_back(ArenaString<QueryArena>(col->name));
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
static ArenaVector<VMInstruction, QueryArena>
build_insert_from_ast(InsertNode *node) {
  ArenaVector<SetColumns, QueryArena> values;

  // Get table to know column names
  Table *table = get_table(const_cast<char *>(node->table));
  if (!table) {
    ArenaVector<VMInstruction, QueryArena> vec;
    vec.push_back(make_halt());
    return vec;
  }

  for (size_t i = 0;
       i < node->values.size() && i < table->schema.columns.size(); i++) {
    if (node->values[i]->type == AST_LITERAL) {
      LiteralNode *lit = (LiteralNode *)node->values[i];
      SetColumns col = {.second = lit->value};
      col.first.assign(table->schema.columns[i].name.c_str());
      values.push_back(col);
    }
  }

  return build_insert(node->table, values);
}

// Build UPDATE from AST
static ArenaVector<VMInstruction, QueryArena>
build_update_from_ast(UpdateNode *node) {
  ParsedParameters params;
  params.table_name = node->table;
  params.operation = ParsedParameters::UPDATE;
  params.where_conditions = extract_where_conditions(node->where, node->table);

  // Extract SET clauses
  for (size_t i = 0; i < node->set_clauses.size(); i++) {
    SetClauseNode *set = node->set_clauses[i];
    if (set->value->type == AST_LITERAL) {
      LiteralNode *lit = (LiteralNode *)set->value;
      uint32_t col_idx = resolve_column_index(set->column, node->table);
      params.set_columns.push_back(
          SetColumns{ArenaString<QueryArena>(set->column), lit->value});
    }
  }

  return build_update(params);
}

// Build DELETE from AST
static ArenaVector<VMInstruction, QueryArena>
build_delete_from_ast(DeleteNode *node) {
  ParsedParameters params;
  params.table_name = node->table;
  params.operation = ParsedParameters::DELETE;
  params.where_conditions = extract_where_conditions(node->where, node->table);

  return build_delete(params);
}

// Build CREATE TABLE from AST
static ArenaVector<VMInstruction, QueryArena>
build_create_table_from_ast(CreateTableNode *node) {
  return build_create_table(node->table, node->columns);
}

// Build CREATE INDEX from AST
static ArenaVector<VMInstruction, QueryArena>
build_create_index_from_ast(CreateIndexNode *node) {
  Table *table = get_table(const_cast<char *>(node->table));
  if (!table) {
    ArenaVector<VMInstruction, QueryArena> vec;
    vec.push_back(make_halt());
    return vec;
  }

  uint32_t col_idx = resolve_column_index(node->column, node->table);
  DataType key_type = table->schema.columns[col_idx].type;

  return build_create_index(node->table, col_idx, key_type);
}

// Build transaction commands from AST
static ArenaVector<VMInstruction, QueryArena>
build_begin_from_ast(BeginNode *node) {
  ArenaVector<VMInstruction, QueryArena> vec;
  vec.push_back(make_begin());
  vec.push_back(make_halt());
  return vec;
}

static ArenaVector<VMInstruction, QueryArena>
build_commit_from_ast(CommitNode *node) {
  ArenaVector<VMInstruction, QueryArena> vec;
  vec.push_back(make_commit());
  vec.push_back(make_halt());
  return vec;
}

static ArenaVector<VMInstruction, QueryArena>
build_rollback_from_ast(RollbackNode *node) {
  ArenaVector<VMInstruction, QueryArena> vec;
  vec.push_back(make_rollback());
  vec.push_back(make_halt());
  return vec;
}

// Main entry point - builds VM instructions from AST
ArenaVector<VMInstruction, QueryArena> build_from_ast(ASTNode *ast) {
  if (!ast) {
    ArenaVector<VMInstruction, QueryArena> vec;
    vec.push_back(make_halt());
    return vec;
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
    ArenaVector<VMInstruction, QueryArena> vec;
    vec.push_back(make_halt());
    return vec;
  }
}



/* DEBUG */



// Debug utilities for VM instructions





// vm_debug.cpp
