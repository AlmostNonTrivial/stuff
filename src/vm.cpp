#include "vm.hpp"
#include "arena.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include "schema.hpp"
#include "stack_containers.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

bool _debug;

/*------------VMCURSOR---------------- */

struct VmCursor {
  BtCursor btree_cursor;
  TableSchema *schema;
  bool is_index;
  uint32_t column; // for index
};

DataType vb_column_type(VmCursor *vb, uint32_t col_index) {
  return vb->schema->columns[col_index].type;
}

DataType vb_key_type(VmCursor *vb) { return vb->schema->columns[0].type; }

uint8_t *vb_column(VmCursor *vb, uint32_t col_index) {
  if (col_index == 0) {
    return btree_cursor_key(&vb->btree_cursor);
  }

  if (vb->is_index) { // index has single column record
    return btree_cursor_record(&vb->btree_cursor);
  }

  uint8_t *record = btree_cursor_record(&vb->btree_cursor);
  return record + vb->schema->column_offsets[col_index];
}

uint8_t *vb_key(VmCursor *vb) { return vb_column(vb, 0); }

/*------------AGGREGATOR---------------- */

struct Aggregator {
  enum Type { NONE = 0, COUNT = 1, SUM = 2, MIN = 3, MAX = 4, AVG = 5 };
  Type type;
  double accumulator;
  uint32_t count;

  void reset() {
    type = NONE;
    accumulator = 0;
    count = 0;
  }
};

/*------------VM STATE---------------- */

static struct {
  ArenaVector<VMInstruction, QueryArena> program;
  uint32_t pc;
  bool halted;

  VMValue registers[REGISTER_COUNT];
  ArenaMap<uint32_t, VmCursor, QueryArena, 20> cursors;
  ArenaQueue<VmEvent, QueryArena> event_queue;

  int32_t compare_result;
  ArenaVector<ArenaVector<VMValue, QueryArena>, QueryArena> output_buffer;
  Aggregator aggregator;

  bool initialized;
} VM = {};

static void vm_set_value(VMValue *val, DataType type, const void *data) {
  val->type = type;
  uint32_t size = VMValue::get_size(type);
  val->data = (uint8_t *)arena::alloc<QueryArena>(size);

  if (data) {
    if (type == TYPE_VARCHAR32 || type == TYPE_VARCHAR256) {
      memset(val->data, 0, size);
      memcpy(val->data, data, size);
    } else {
      memcpy(val->data, data, size);
    }
  } else {
    memset(val->data, 0, size);
  }
}

// Event emission helpers
static void emit_event(EventType type, void *data = nullptr) {
  VmEvent event;
  event.type = type;
  event.data = data;
  VM.event_queue.push(event);
}

static void emit_root_changed_event(EventType type, const char *table_name,
                                    uint32_t column) {
  VmEvent event;
  event.type = type;
  event.data = nullptr;
  event.context.table_info.table_name = table_name;
  event.context.table_info.column = column;
  VM.event_queue.push(event);
}

static void emit_drop_event(EventType type, const char *table_name,
                            uint32_t column) {
  VmEvent event;
  event.type = type;
  event.data = nullptr;
  event.context.table_info.table_name = table_name;
  event.context.table_info.column = column;
  VM.event_queue.push(event);
}

static void emit_index_event(EventType type, const char *table_name,
                             uint32_t column_index,
                             const char *index_name = nullptr) {
  VmEvent event;
  event.type = type;
  event.data = nullptr;
  event.context.index_info.table_name = table_name;
  event.context.index_info.column_index = column_index;
  event.context.index_info.index_name = index_name;
  VM.event_queue.push(event);
}

static void emit_row_event(EventType type, uint32_t count) {
  VmEvent event;
  event.type = type;
  event.data = nullptr;
  event.context.row_info.count = count;
  VM.event_queue.push(event);
}

ArenaVector<ArenaVector<VMValue, QueryArena>, QueryArena> &vm_output_buffer() {
  return VM.output_buffer;
}

void vm_shutdown() {
  VM.initialized = false;
  VM.cursors.clear();
  VM.event_queue.clear();
  VM.output_buffer.clear();
}

void vm_reset() {
  VM.pc = 0;
  VM.halted = false;
  VM.compare_result = 0;

  for (uint32_t i = 0; i < REGISTER_COUNT; i++) {
    VM.registers[i].type = TYPE_NULL;
    VM.registers[i].data = nullptr;
  }

  VM.program.clear();
  VM.cursors.clear();
  VM.event_queue.clear();
  VM.output_buffer.clear();
  VM.aggregator.reset();
}

// Public VM functions
void vm_init() {
  VM.initialized = true;
  vm_reset();
}

bool vm_is_halted() { return VM.halted; }

ArenaQueue<VmEvent, QueryArena> &vm_events() { return VM.event_queue; }

// Main execution step
VM_RESULT vm_step() {

  VMInstruction *inst = &VM.program[VM.pc];


  switch (inst->opcode) {
  case OP_Halt:
    VM.halted = true;
    return OK;

  case OP_Goto:
    VM.pc = Opcodes::Goto::target(*inst);
    return OK;

  case OP_Integer: {
    int32_t dest_reg = Opcodes::Integer::dest_reg(*inst);
    int32_t value = Opcodes::Integer::value(*inst);
    uint32_t val = (uint32_t)value;
    vm_set_value(&VM.registers[dest_reg], TYPE_UINT32, &val);
    VM.pc++;
    return OK;
  }

  case OP_String: {
    int32_t dest_reg = Opcodes::String::dest_reg(*inst);
    int32_t size = Opcodes::String::size(*inst);
    const void *str = Opcodes::String::str(*inst);
    vm_set_value(&VM.registers[dest_reg], (DataType)size, str);
    VM.pc++;
    return OK;
  }

  case OP_Copy: {
    int32_t src = Opcodes::Copy::src_reg(*inst);
    int32_t dest = Opcodes::Copy::dest_reg(*inst);
    VM.registers[dest] = VM.registers[src];
    VM.pc++;
    return OK;
  }

  case OP_Move: {
    int32_t src = Opcodes::Move::src_reg(*inst);
    int32_t dest = Opcodes::Move::dest_reg(*inst);
    VM.registers[dest] = VM.registers[src];
    VM.registers[src].type = TYPE_NULL;
    VM.registers[src].data = nullptr;
    VM.pc++;
    return OK;
  }

  case OP_OpenRead:
  case OP_OpenWrite: {
    int32_t cursor_id = (inst->opcode == OP_OpenRead)
                            ? Opcodes::OpenRead::cursor_id(*inst)
                            : Opcodes::OpenWrite::cursor_id(*inst);
    const char *table_name = (inst->opcode == OP_OpenRead)
                                 ? Opcodes::OpenRead::table_name(*inst)
                                 : Opcodes::OpenWrite::table_name(*inst);
    int32_t index_column = (inst->opcode == OP_OpenRead)
                               ? Opcodes::OpenRead::index_col(*inst)
                               : Opcodes::OpenWrite::index_col(*inst);

    Table *table = get_table(table_name);
    if (!table) {
      return ERR;
    }

    VmCursor &cursor = VM.cursors[cursor_id];

    if (index_column != 0) {
      Index *index = get_index(table_name, index_column);
      if (!index) {
        return ERR;
      }

      cursor.btree_cursor.tree = &index->tree;
      cursor.column = index_column;
      cursor.is_index = true;
    } else {
      cursor.btree_cursor.tree = &table->tree;
      cursor.is_index = false;
    }

    cursor.schema = &table->schema;
    VM.pc++;
    return OK;
  }

  case OP_Close: {
    int32_t cursor_id = Opcodes::Close::cursor_id(*inst);
    VM.cursors.erase(cursor_id);
    VM.pc++;
    return OK;
  }

  case OP_Last:
  case OP_Rewind:
  case OP_First: {
    int32_t cursor_id =
        (inst->opcode == OP_Last)     ? Opcodes::Last::cursor_id(*inst)
        : (inst->opcode == OP_Rewind) ? Opcodes::Rewind::cursor_id(*inst)
                                      : Opcodes::First::cursor_id(*inst);

    int32_t jump_if_empty =
        (inst->opcode == OP_Last)     ? Opcodes::Last::jump_if_empty(*inst)
        : (inst->opcode == OP_Rewind) ? Opcodes::Rewind::jump_if_empty(*inst)
                                      : Opcodes::First::jump_if_empty(*inst);

    if (!VM.cursors.contains(cursor_id)) {
      return ERR;
    }
    VmCursor *cursor = VM.cursors.find(cursor_id);

    bool valid = inst->opcode == OP_Last
                     ? btree_cursor_last(&cursor->btree_cursor)
                     : btree_cursor_first(&cursor->btree_cursor);

    if (!valid) {
      if (jump_if_empty > 0) {
        VM.pc = jump_if_empty;
      } else {
        VM.pc++;
      }
    } else {
      VM.pc++;
    }
    return OK;
  }

  case OP_Next:
  case OP_Prev: {
    int32_t cursor_id = (inst->opcode == OP_Next)
                            ? Opcodes::Next::cursor_id(*inst)
                            : Opcodes::Prev::cursor_id(*inst);

    int32_t jump_if_done = (inst->opcode == OP_Next)
                               ? Opcodes::Next::jump_if_done(*inst)
                               : Opcodes::Prev::jump_if_done(*inst);

    if (!VM.cursors.contains(cursor_id)) {
      return ERR;
    }

    VmCursor *cursor = VM.cursors.find(cursor_id);
    bool has_more = (inst->opcode == OP_Next)
                        ? btree_cursor_next(&cursor->btree_cursor)
                        : btree_cursor_previous(&cursor->btree_cursor);

    if (has_more && jump_if_done > 0) {
      VM.pc = jump_if_done;
    } else {
      VM.pc++;
    }
    return OK;
  }

  case OP_SeekEQ:
  case OP_SeekGT:
  case OP_SeekGE:
  case OP_SeekLE:
  case OP_SeekLT: {
    int32_t cursor_id, key_reg, jump_if_not_found;

    switch (inst->opcode) {
    case OP_SeekEQ:
      cursor_id = Opcodes::SeekEQ::cursor_id(*inst);
      key_reg = Opcodes::SeekEQ::key_reg(*inst);
      jump_if_not_found = Opcodes::SeekEQ::jump_if_not_found(*inst);
      break;
    case OP_SeekGT:
      cursor_id = Opcodes::SeekGT::cursor_id(*inst);
      key_reg = Opcodes::SeekGT::key_reg(*inst);
      jump_if_not_found = Opcodes::SeekGT::jump_if_not_found(*inst);
      break;
    case OP_SeekGE:
      cursor_id = Opcodes::SeekGE::cursor_id(*inst);
      key_reg = Opcodes::SeekGE::key_reg(*inst);
      jump_if_not_found = Opcodes::SeekGE::jump_if_not_found(*inst);
      break;
    case OP_SeekLE:
      cursor_id = Opcodes::SeekLE::cursor_id(*inst);
      key_reg = Opcodes::SeekLE::key_reg(*inst);
      jump_if_not_found = Opcodes::SeekLE::jump_if_not_found(*inst);
      break;
    case OP_SeekLT:
      cursor_id = Opcodes::SeekLT::cursor_id(*inst);
      key_reg = Opcodes::SeekLT::key_reg(*inst);
      jump_if_not_found = Opcodes::SeekLT::jump_if_not_found(*inst);
      break;
    }

    if (!VM.cursors.contains(cursor_id)) {
      return ERR;
    }

    VmCursor *cursor = VM.cursors.find(cursor_id);
    VMValue *key = &VM.registers[key_reg];

    bool found = false;
    switch (inst->opcode) {
    case OP_SeekGE:
      found = btree_cursor_seek_ge(&cursor->btree_cursor, key->data);
      break;
    case OP_SeekGT:
      found = btree_cursor_seek_gt(&cursor->btree_cursor, key->data);
      break;
    case OP_SeekLE:
      found = btree_cursor_seek_le(&cursor->btree_cursor, key->data);
      break;
    case OP_SeekLT:
      found = btree_cursor_seek_lt(&cursor->btree_cursor, key->data);
      break;
    case OP_SeekEQ:
      found = btree_cursor_seek(&cursor->btree_cursor, key->data);
      break;
    }
    // MAKE SURE THIS WORKS WITH GT/LT
    if (!found && jump_if_not_found > 0) {
      VM.pc = jump_if_not_found;
    } else {
      VM.pc++;
    }
    return OK;
  }

  case OP_Column: {
    int32_t cursor_id = Opcodes::Column::cursor_id(*inst);
    int32_t col_index = Opcodes::Column::column_index(*inst);
    int32_t dest_reg = Opcodes::Column::dest_reg(*inst);

    if (!VM.cursors.contains(cursor_id)) {
      return ERR;
    }

    VmCursor *cursor = VM.cursors.find(cursor_id);

    uint8_t *col_data = vb_column(cursor, col_index);
    DataType type = vb_column_type(cursor, col_index);
    vm_set_value(&VM.registers[dest_reg], type, col_data);

    VM.pc++;
    return OK;
  }

  case OP_MakeRecord: {
    int32_t first_reg = Opcodes::MakeRecord::first_reg(*inst);
    int32_t reg_count = Opcodes::MakeRecord::reg_count(*inst);
    int32_t dest_reg = Opcodes::MakeRecord::dest_reg(*inst);

    uint32_t total_size = 0;
    for (int i = 0; i < reg_count; i++) {
      VMValue *val = &VM.registers[first_reg + i];
      total_size += VMValue::get_size(val->type);
    }

    uint8_t *record = (uint8_t *)arena::alloc<QueryArena>(total_size);

    uint32_t offset = 0;
    for (int i = 0; i < reg_count; i++) {
      VMValue *val = &VM.registers[first_reg + i];
      uint32_t size = VMValue::get_size(val->type);
      memcpy(record + offset, val->data, size);
      offset += size;
    }

    VM.registers[dest_reg].type = TYPE_UINT32;
    VM.registers[dest_reg].data = record;
    VM.pc++;
    return OK;
  }

  case OP_Insert: {
    int32_t cursor_id = Opcodes::Insert::cursor_id(*inst);
    int32_t key_reg = Opcodes::Insert::key_reg(*inst);
    int32_t record_reg = Opcodes::Insert::record_reg(*inst);

    if (!VM.cursors.contains(cursor_id)) {
      return ERR;
    }

    VmCursor *cursor = VM.cursors.find(cursor_id);
    VMValue *key = &VM.registers[key_reg];
    VMValue *record = &VM.registers[record_reg];

    uint32_t current_root = cursor->btree_cursor.tree->root_page_index;

    if (!cursor->is_index) {
      // index can have duplicates, so we might need to match key and record
      bool exists = btree_cursor_seek(&cursor->btree_cursor, (void *)key->data);

      if (exists) {
        return ERR;
      }
    }

    bool success =
        btree_cursor_insert(&cursor->btree_cursor, key->data, record->data);
    if (!success) {
      return ERR;
    }

    // Handle root page changes
    if (cursor->btree_cursor.tree->root_page_index != current_root) {
      if (cursor->is_index) {
        emit_root_changed_event(EVT_BTREE_ROOT_CHANGED,
                                cursor->schema->table_name.c_str(),
                                cursor->column);
      } else {
        emit_root_changed_event(EVT_BTREE_ROOT_CHANGED,
                                cursor->schema->table_name.c_str(), 0);
      }
    }

    emit_row_event(EVT_ROWS_INSERTED, 1);
    VM.pc++;
    return OK;
  }

  case OP_Delete: {
    int32_t cursor_id = Opcodes::Delete::cursor_id(*inst);

    if (!VM.cursors.contains(cursor_id)) {
      return ERR;
    }
    VmCursor *cursor = VM.cursors.find(cursor_id);

    uint32_t current_root = cursor->btree_cursor.tree->root_page_index;
    btree_cursor_delete(&cursor->btree_cursor);

    if (cursor->btree_cursor.tree->root_page_index != current_root) {
      emit_event(EVT_BTREE_ROOT_CHANGED, cursor->btree_cursor.tree);
    }

    emit_row_event(EVT_ROWS_DELETED, 1);
    VM.pc++;
    return OK;
  }

  case OP_Update: {
    int32_t cursor_id = Opcodes::Update::cursor_id(*inst);
    int32_t record_reg = Opcodes::Update::record_reg(*inst);

    if (!VM.cursors.contains(cursor_id)) {
      return ERR;
    }

    VmCursor *cursor = VM.cursors.find(cursor_id);
    VMValue *record = &VM.registers[record_reg];

    btree_cursor_update(&cursor->btree_cursor, record->data);
    emit_row_event(EVT_ROWS_UPDATED, 1);
    VM.pc++;
    return OK;
  }

  case OP_Compare: {
    int32_t reg_a = Opcodes::Compare::reg_a(*inst);
    int32_t reg_b = Opcodes::Compare::reg_b(*inst);
    VMValue *a = &VM.registers[reg_a];
    VMValue *b = &VM.registers[reg_b];

    VM.compare_result = cmp(a->type, a->data, b->data);
    VM.pc++;
    return OK;
  }

  case OP_Jump: {
    int32_t jump_lt = Opcodes::Jump::jump_lt(*inst);
    int32_t jump_eq = Opcodes::Jump::jump_eq(*inst);
    int32_t jump_gt = Opcodes::Jump::jump_gt(*inst);

    if (VM.compare_result < 0 && jump_lt > 0) {
      VM.pc = jump_lt;
    } else if (VM.compare_result == 0 && jump_eq > 0) {
      VM.pc = jump_eq;
    } else if (VM.compare_result > 0 && jump_gt > 0) {
      VM.pc = jump_gt;
    } else {
      VM.pc++;
    }
    return OK;
  }

  case OP_Eq:
  case OP_Ne:
  case OP_Lt:
  case OP_Le:
  case OP_Gt:
  case OP_Ge: {
    int32_t reg_a, reg_b, jump_target;

    switch (inst->opcode) {
    case OP_Eq:
      reg_a = Opcodes::Eq::reg_a(*inst);
      reg_b = Opcodes::Eq::reg_b(*inst);
      jump_target = Opcodes::Eq::jump_target(*inst);
      break;
    case OP_Ne:
      reg_a = Opcodes::Ne::reg_a(*inst);
      reg_b = Opcodes::Ne::reg_b(*inst);
      jump_target = Opcodes::Ne::jump_target(*inst);
      break;
    case OP_Lt:
      reg_a = Opcodes::Lt::reg_a(*inst);
      reg_b = Opcodes::Lt::reg_b(*inst);
      jump_target = Opcodes::Lt::jump_target(*inst);
      break;
    case OP_Le:
      reg_a = Opcodes::Le::reg_a(*inst);
      reg_b = Opcodes::Le::reg_b(*inst);
      jump_target = Opcodes::Le::jump_target(*inst);
      break;
    case OP_Gt:
      reg_a = Opcodes::Gt::reg_a(*inst);
      reg_b = Opcodes::Gt::reg_b(*inst);
      jump_target = Opcodes::Gt::jump_target(*inst);
      break;
    case OP_Ge:
      reg_a = Opcodes::Ge::reg_a(*inst);
      reg_b = Opcodes::Ge::reg_b(*inst);
      jump_target = Opcodes::Ge::jump_target(*inst);
      break;
    }

    VMValue *a = &VM.registers[reg_a];
    VMValue *b = &VM.registers[reg_b];

    int cmp_result = cmp(a->type, a->data, b->data);
    bool condition = false;

    switch (inst->opcode) {
    case OP_Eq:
      condition = (cmp_result == 0);
      break;
    case OP_Ne:
      condition = (cmp_result != 0);
      break;
    case OP_Lt:
      condition = (cmp_result < 0);
      break;
    case OP_Le:
      condition = (cmp_result <= 0);
      break;
    case OP_Gt:
      condition = (cmp_result > 0);
      break;
    case OP_Ge:
      condition = (cmp_result >= 0);
      break;
    }

    if (condition && jump_target > 0) {
      VM.pc = jump_target;
    } else {
      VM.pc++;
    }
    return OK;
  }

  case OP_ResultRow: {
    int32_t first_reg = Opcodes::ResultRow::first_reg(*inst);
    int32_t reg_count = Opcodes::ResultRow::reg_count(*inst);

    ArenaVector<VMValue, QueryArena> row;
    for (int i = 0; i < reg_count; i++) {
      VMValue copy;
      copy.type = VM.registers[first_reg + i].type;
      uint32_t size = VMValue::get_size(copy.type);
      copy.data = (uint8_t *)arena::alloc<QueryArena>(size);
      memcpy(copy.data, VM.registers[first_reg + i].data, size);
      row.push_back(copy);
    }

    VM.output_buffer.push_back(row);
    VM.pc++;
    return OK;
  }

  case Op_Sort: {
    int32_t col = Opcodes::Sort::column_index(*inst);
    bool desc = Opcodes::Sort::descending(*inst);

    std::sort(VM.output_buffer.begin(), VM.output_buffer.end(),
              [col, desc](const auto &a, const auto &b) {
                int cmp_result = cmp(a[col].type, a[col].data, b[col].data);
                return desc ? (cmp_result > 0) : (cmp_result < 0);
              });
    VM.pc++;
    return OK;
  }

  case Op_Flush: {
    VM.pc++;
    return OK;
  }

  case OP_AggStep: {
    int32_t value_reg = Opcodes::AggStep::value_reg(*inst);
    VMValue *value = nullptr;
    if (value_reg >= 0) {
      value = &VM.registers[value_reg];
    }
    Aggregator &aggr = VM.aggregator;

    if (aggr.type == Aggregator::COUNT) {
      aggr.accumulator++;
      aggr.count++;
    } else if (value && value->type == TYPE_UINT32) {
      uint32_t val = *(uint32_t *)value->data;
      switch (aggr.type) {
      case Aggregator::SUM:
      case Aggregator::AVG:
        aggr.accumulator += val;
        aggr.count++;
        break;
      case Aggregator::MAX:
        if (aggr.count == 0 || val > aggr.accumulator) {
          aggr.accumulator = val;
        }
        aggr.count++;
        break;
      case Aggregator::MIN:
        if (aggr.count == 0 || val < aggr.accumulator) {
          aggr.accumulator = val;
        }
        aggr.count++;
        break;
      default:
        return ERR;
        break;
      }
    }

    VM.pc++;
    return OK;
  }

  case OP_AggReset: {
    VM.aggregator.reset();
    const char *function_name = Opcodes::AggReset::function_name(*inst);

    if (strcmp(function_name, "COUNT") == 0) {
      VM.aggregator.type = Aggregator::COUNT;
    } else if (strcmp(function_name, "MIN") == 0) {
      VM.aggregator.type = Aggregator::MIN;
    } else if (strcmp(function_name, "AVG") == 0) {
      VM.aggregator.type = Aggregator::AVG;
    } else if (strcmp(function_name, "MAX") == 0) {
      VM.aggregator.type = Aggregator::MAX;
    } else if (strcmp(function_name, "SUM") == 0) {
      VM.aggregator.type = Aggregator::SUM;
    }

    VM.pc++;
    return OK;
  }

  case OP_AggFinal: {
    int32_t dest_reg = Opcodes::AggFinal::dest_reg(*inst);
    uint32_t result;
    if (VM.aggregator.type == Aggregator::AVG && VM.aggregator.count > 0) {
      result = (uint32_t)(VM.aggregator.accumulator / VM.aggregator.count);
    } else {
      result = (uint32_t)VM.aggregator.accumulator;
    }

    vm_set_value(&VM.registers[dest_reg], TYPE_UINT32, &result);

    VM.aggregator.reset();
    VM.pc++;
    return OK;
  }

  case OP_CreateTable: {
    TableSchema *schema = Opcodes::CreateTable::schema(*inst);

    // Check if table already exists
    if (get_table(schema->table_name.c_str())) {
      return ERR;
    }

    // will be copied over to into the schema.
    Table *new_table = (Table *)arena::alloc<QueryArena>(sizeof(Table));
    new_table->schema = *schema;

    // Calculate offsets and record size
    calculate_column_offsets(&new_table->schema);

    // Create btree for table
    new_table->tree = btree_create(new_table->schema.key_type(),
                                   new_table->schema.record_size, BPLUS);

    add_table(new_table);
    VmEvent event;
    event.type = EVT_TABLE_CREATED;
    event.context.table_info.table_name = schema->table_name.c_str();
    VM.event_queue.push(event);
    VM.pc++;
    return OK;
  }

  case OP_CreateIndex: {
    const char *table_name = Opcodes::CreateIndex::table_name(*inst);
    int32_t column = Opcodes::CreateIndex::column_index(*inst);

    Table *table = get_table(table_name);
    if (!table) {
      return ERR;
    }

    // Check if index already exists
    if (get_index(table_name, column)) {
      return ERR;
    }

    // Create index structure in arena
    Index *index = (Index *)arena::alloc<QueryArena>(sizeof(Index));
    index->column_index = column;
    index->tree = btree_create(table->schema.columns[column].type,
                               table->schema.key_type(), BTREE);

    // apply immediately in memory,
    add_index(table_name, index);

    // Emit event
    VmEvent event;
    event.type = EVT_INDEX_CREATED;
    event.context.index_info.table_name = table_name;
    event.context.index_info.column_index = column;
    VM.event_queue.push(event);

    VM.pc++;
    return OK;
  }

  case OP_DropTable: {
    const char *table_name = Opcodes::DropTable::table_name(*inst);

    Table *table = get_table(table_name);
    if (!table) {
      return ERR;
    }

    // Clear btrees
    btree_clear(&table->tree);
    for (int i = 0; i < table->indexes.size(); i++) {
      btree_clear(&table->indexes.value_at(i)->tree);
    }

    remove_table(table_name);
    emit_drop_event(EVT_TABLE_DROPPED, table_name, 0);
    VM.pc++;
    return OK;
  }

  case OP_DropIndex: {
    const char *table_name = Opcodes::DropIndex::table_name(*inst);
    int32_t column = Opcodes::DropIndex::column_index(*inst);

    Table *table = get_table(table_name);
    if (!table) {
      return ERR;
    }

    Index *index = get_index(table_name, column);
    if (!index) {
      return ERR;
    }

    btree_clear(&index->tree);
    remove_index(table_name, column);
    emit_drop_event(EVT_INDEX_DROPPED, table_name, column);
    VM.pc++;
    return OK;
  }

  /*
   * the executor needs make sure that
   * the master catalog is synced within the transaction,
   * we can't let the program just commit, we let the executor
   * handle the transaction
   */
  case OP_Begin:
    emit_event(EVT_TRANSACTION_BEGIN);
    VM.pc++;
    return OK;

  case OP_Commit:
    emit_event(EVT_TRANSACTION_COMMIT);
    VM.pc++;
    return OK;

  case OP_Rollback:
    emit_event(EVT_TRANSACTION_ROLLBACK);
    VM.pc++;
    return ABORT;

  default:
    printf("Unknown opcode: %d\n", inst->opcode);
    exit(1);
    return ERR;
  }
}

VM_RESULT vm_execute(ArenaVector<VMInstruction, QueryArena> &instructions) {
  if (!VM.initialized) {
    vm_init();
  }

  vm_reset();
  VM.program.set(instructions);

  while (!VM.halted && VM.pc < VM.program.size()) {
    VM_RESULT result = vm_step();
    if (result != OK) {
      return result;
    }
  }
  return OK;
}
