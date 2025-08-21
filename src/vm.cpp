#include "vm.hpp"
#include "arena.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "memtree.hpp"
#include "pager.hpp"
#include "schema.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

bool _debug;

/*------------VMCURSOR---------------- */

struct VmCursor {
  BtCursor btree_cursor;
  MemCursor mem_cursor;
  TableSchema *schema;
  bool is_index;
  uint32_t column; // for index
  bool is_memory;
  MemTree mem_tree;

  // Unified interface helpers
  uint32_t record_size() const {
    if (is_memory) {
      return mem_tree.record_size;
    }
    return schema ? schema->record_size : 0;
  }

  DataType key_type() const {
    if (is_memory) {
      return mem_tree.key_type;
    }
    return schema ? schema->columns[0].type : TYPE_NULL;
  }
};

// Helper function for column access
static uint8_t *vb_column(VmCursor *vb, uint32_t col_index) {
  if (vb->is_memory) {
    if (col_index == 0) {
      return memcursor_key(&vb->mem_cursor);
    }
    // For memory trees, the record is the whole value
    return memcursor_record(&vb->mem_cursor);
  }

  // Original btree logic
  if (col_index == 0) {
    return btree_cursor_key(&vb->btree_cursor);
  }

  if (vb->is_index) {
    return btree_cursor_record(&vb->btree_cursor);
  }

  uint8_t *record = btree_cursor_record(&vb->btree_cursor);
  return record + vb->schema->column_offsets[col_index];
}

DataType vb_column_type(VmCursor *vb, uint32_t col_index) {
  return vb->schema->columns[col_index].type;
}

DataType vb_key_type(VmCursor *vb) { return vb->schema->columns[0].type; }

uint8_t *vb_key(VmCursor *vb) { return vb_column(vb, 0); }

/*------------VM STATE---------------- */

static struct {
  ResultCallback callback;
  Vector<VMInstruction, QueryArena> program;
  uint32_t pc;
  bool halted;
  Queue<VmEvent, QueryArena> events;
  QueryContext current_query_context;
  TypedValue registers[REGISTERS];
  VmCursor cursors[CURSORS];
  bool initialized;
} VM = {};

static void vm_set_value(TypedValue *val, DataType type, const void *data) {
  val->type = type;
  val->data = (uint8_t *)arena::alloc<QueryArena>((uint32_t)type);
  memcpy(val->data, data, (uint32_t)type);
}

// Public VM functions
void vm_shutdown() { VM.initialized = false; }

void vm_reset() {
  VM.pc = 0;
  VM.halted = false;

  for (uint32_t i = 0; i < REGISTERS; i++) {
    VM.registers[i].type = TYPE_NULL;
    VM.registers[i].data = nullptr;
  }

  VM.program.clear();
  VM.events.clear();
}

void vm_init() {
  VM.initialized = true;
  vm_reset();
}

bool vm_is_halted() { return VM.halted; }

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


  case OP_Test: {
    int32_t dest = Opcodes::Test::dest_reg(*inst);
    int32_t left = Opcodes::Test::left_reg(*inst);
    int32_t right = Opcodes::Test::right_reg(*inst);
    CompareOp op = Opcodes::Test::op(*inst);

    TypedValue* a = &VM.registers[left];
    TypedValue* b = &VM.registers[right];

    int cmp_result = cmp(a->type, a->data, b->data);
    uint32_t result = 0;

    switch (op) {
      case EQ: result = (cmp_result == 0) ? 1 : 0; break;
      case NE: result = (cmp_result != 0) ? 1 : 0; break;
      case LT: result = (cmp_result < 0) ? 1 : 0; break;
      case LE: result = (cmp_result <= 0) ? 1 : 0; break;
      case GT: result = (cmp_result > 0) ? 1 : 0; break;
      case GE: result = (cmp_result >= 0) ? 1 : 0; break;
    }

    vm_set_value(&VM.registers[dest], TYPE_UINT32, &result);
    VM.pc++;
    return OK;
  }

  case OP_JumpIf: {
    int32_t test_reg = Opcodes::JumpIf::test_reg(*inst);
    int32_t target = Opcodes::JumpIf::jump_target(*inst);
    bool jump_on_true = Opcodes::JumpIf::jump_on_true(*inst);

    TypedValue* val = &VM.registers[test_reg];
    bool is_true = false;

    if (val->type == TYPE_UINT32) {
      is_true = (*(uint32_t*)val->data != 0);
    } else if (val->type == TYPE_UINT64) {
      is_true = (*(uint64_t*)val->data != 0);
    }

    if ((is_true && jump_on_true) || (!is_true && !jump_on_true)) {
      VM.pc = target;
    } else {
      VM.pc++;
    }
    return OK;
  }

  case OP_Logic: {
    int32_t dest = Opcodes::Logic::dest_reg(*inst);
    int32_t left = Opcodes::Logic::left_reg(*inst);
    int32_t right = Opcodes::Logic::right_reg(*inst);
    LogicOp op = Opcodes::Logic::op(*inst);

    uint32_t result = 0;

    if (op == LOGIC_NOT) {
      TypedValue* val = &VM.registers[left];
      uint32_t is_true = 0;
      if (val->type == TYPE_UINT32) {
        is_true = *(uint32_t*)val->data;
      }
      result = is_true ? 0 : 1;
    } else {
      TypedValue* a = &VM.registers[left];
      TypedValue* b = &VM.registers[right];

      uint32_t a_val = (a->type == TYPE_UINT32) ? *(uint32_t*)a->data : 0;
      uint32_t b_val = (b->type == TYPE_UINT32) ? *(uint32_t*)b->data : 0;

      if (op == LOGIC_AND) {
        result = (a_val && b_val) ? 1 : 0;
      } else if (op == LOGIC_OR) {
        result = (a_val || b_val) ? 1 : 0;
      }
    }

    vm_set_value(&VM.registers[dest], TYPE_UINT32, &result);
    VM.pc++;
    return OK;
  }

  case OP_ResultRow: {
    int32_t first = Opcodes::Result::first_reg(*inst);
    int32_t count = Opcodes::Result::reg_count(*inst);

    if (VM.callback) {
      // Build result from registers
      uint32_t total_size = 0;
      for (int i = 0; i < count; i++) {
        TypedValue* val = &VM.registers[first + i];
        total_size += val->type;
      }

      uint8_t* result = (uint8_t*)arena::alloc<QueryArena>(total_size);
      uint32_t offset = 0;

      for (int i = 0; i < count; i++) {
        TypedValue* val = &VM.registers[first + i];
        memcpy(result + offset, val->data, val->type);
        offset += val->type;
      }

      VM.callback(result, total_size);
    }

    VM.pc++;
    return OK;
  }

  case OP_Arithmetic: {
    int32_t dest = Opcodes::Arithmetic::dest_reg(*inst);
    int32_t left = Opcodes::Arithmetic::left_reg(*inst);
    int32_t right = Opcodes::Arithmetic::right_reg(*inst);
    ArithOp op = Opcodes::Arithmetic::op(*inst);

    TypedValue *a = &VM.registers[left];
    TypedValue *b = &VM.registers[right];
    TypedValue *result = &VM.registers[dest];

    // Determine output type (use larger of the two)
    DataType output_type = (a->type > b->type) ? a->type : b->type;

    // Convert both operands to uint64_t for calculation
    uint64_t val_a = 0, val_b = 0;

    // Extract value based on type
    switch (a->type) {
    case TYPE_UINT32:
      val_a = *(uint32_t *)a->data;
      break;
    case TYPE_UINT64:
      val_a = *(uint64_t *)a->data;
      break;
    default:
      return ERR; // Non-numeric type
    }

    switch (b->type) {
    case TYPE_UINT32:
      val_b = *(uint32_t *)b->data;
      break;
    case TYPE_UINT64:
      val_b = *(uint64_t *)b->data;
      break;
    default:
      return ERR; // Non-numeric type
    }

    // Perform arithmetic
    uint64_t val_result = 0;
    switch (op) {
    case ARITH_ADD:
      val_result = val_a + val_b;
      break;
    case ARITH_SUB:
      val_result = val_a - val_b;
      break;
    case ARITH_MUL:
      val_result = val_a * val_b;
      break;
    case ARITH_DIV:
      if (val_b == 0)
        return ERR;
      val_result = val_a / val_b;
      break;
    case ARITH_MOD:
      if (val_b == 0)
        return ERR;
      val_result = val_a % val_b;
      break;
    }

    // Store result in appropriate type
    if (output_type == TYPE_UINT32) {
      uint32_t val32 = (uint32_t)val_result;
      vm_set_value(result, TYPE_UINT32, &val32);
    } else {
      vm_set_value(result, TYPE_UINT64, &val_result);
    }

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
    cursor.is_memory = false;

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
    VM.pc++;
    return OK;
  }

  case OP_First: {
    int32_t cursor_id = Opcodes::First::cursor_id(*inst);
    int32_t jump_if_empty = Opcodes::First::jump_if_empty(*inst);

    VmCursor *cursor = &VM.cursors[cursor_id];

    bool valid;
    if (cursor->is_memory) {
      valid = memcursor_first(&cursor->mem_cursor);
    } else {
      valid = btree_cursor_first(&cursor->btree_cursor);
    }

    if (!valid && jump_if_empty >= 0) {
      VM.pc = jump_if_empty;
    } else {
      VM.pc++;
    }
    return OK;
  }

  case OP_Last: {
    int32_t cursor_id = Opcodes::Last::cursor_id(*inst);
    int32_t jump_if_empty = Opcodes::Last::jump_if_empty(*inst);

    VmCursor *cursor = &VM.cursors[cursor_id];

    bool valid;
    if (cursor->is_memory) {
      valid = memcursor_last(&cursor->mem_cursor);
    } else {
      valid = btree_cursor_last(&cursor->btree_cursor);
    }

    if (!valid && jump_if_empty >= 0) {
      VM.pc = jump_if_empty;
    } else {
      VM.pc++;
    }
    return OK;
  }

  case OP_Next: {
    int32_t cursor_id = Opcodes::Next::cursor_id(*inst);
    int32_t jump_if_done = Opcodes::Next::jump_if_done(*inst);

    VmCursor *cursor = &VM.cursors[cursor_id];
    bool has_more;
    if (cursor->is_memory) {
      has_more = memcursor_next(&cursor->mem_cursor);
    } else {
      has_more = btree_cursor_next(&cursor->btree_cursor);
    }

    if (has_more && jump_if_done >= 0) {
      VM.pc = jump_if_done;
    } else {
      VM.pc++;
    }
    return OK;
  }

  case OP_Prev: {
    int32_t cursor_id = Opcodes::Prev::cursor_id(*inst);
    int32_t jump_if_done = Opcodes::Prev::jump_if_done(*inst);

    VmCursor *cursor = &VM.cursors[cursor_id];
    bool has_more;
    if (cursor->is_memory) {
      has_more = memcursor_previous(&cursor->mem_cursor);
    } else {
      has_more = btree_cursor_previous(&cursor->btree_cursor);
    }

    if (has_more && jump_if_done >= 0) {
      VM.pc = jump_if_done;
    } else {
      VM.pc++;
    }
    return OK;
  }

  case OP_Seek: {
    int32_t cursor_id = Opcodes::Seek::cursor_id(*inst);
    int32_t key_reg = Opcodes::Seek::key_reg(*inst);
    int32_t jump_if_not = Opcodes::Seek::jump_if_not(*inst);
    CompareOp op = Opcodes::Seek::op(*inst);

    VmCursor *cursor = &VM.cursors[cursor_id];
    TypedValue *key = &VM.registers[key_reg];

    bool found = false;
    if (cursor->is_memory) {
      switch (op) {
      case EQ:
        found = memcursor_seek(&cursor->mem_cursor, key->data);
        break;
      case GE:
        found = memcursor_seek_ge(&cursor->mem_cursor, key->data);
        break;
      case GT:
        found = memcursor_seek_gt(&cursor->mem_cursor, key->data);
        break;
      case LE:
        found = memcursor_seek_le(&cursor->mem_cursor, key->data);
        break;
      case LT:
        found = memcursor_seek_lt(&cursor->mem_cursor, key->data);
        break;
      default:
        found = false;
      }
    } else {
      switch (op) {
      case EQ:
        found = btree_cursor_seek(&cursor->btree_cursor, key->data);
        break;
      case GE:
        found = btree_cursor_seek_ge(&cursor->btree_cursor, key->data);
        break;
      case GT:
        found = btree_cursor_seek_gt(&cursor->btree_cursor, key->data);
        break;
      case LE:
        found = btree_cursor_seek_le(&cursor->btree_cursor, key->data);
        break;
      case LT:
        found = btree_cursor_seek_lt(&cursor->btree_cursor, key->data);
        break;
      default:
        found = false;
      }
    }

    if (!found && jump_if_not >= 0) {
      VM.pc = jump_if_not;
    } else {
      VM.pc++;
    }
    return OK;
  }

  case OP_Column: {
    int32_t cursor_id = Opcodes::Column::cursor_id(*inst);
    int32_t col_index = Opcodes::Column::column_index(*inst);
    int32_t dest_reg = Opcodes::Column::dest_reg(*inst);

    VmCursor *cursor = &VM.cursors[cursor_id];
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
      TypedValue *val = &VM.registers[first_reg + i];
      total_size += val->type;
    }

    uint8_t *record = (uint8_t *)arena::alloc<QueryArena>(total_size);

    uint32_t offset = 0;
    for (int i = 0; i < reg_count; i++) {
      TypedValue *val = &VM.registers[first_reg + i];
      uint32_t size = val->type;
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

    VmCursor *cursor = &VM.cursors[cursor_id];
    TypedValue *key = &VM.registers[key_reg];
    TypedValue *record = &VM.registers[record_reg];

    bool success;
    if (cursor->is_memory) {
      success = memcursor_insert(&cursor->mem_cursor, key->data, record->data);
    } else {
      if (!cursor->is_index) {
        // Check for duplicate key in table
        bool exists = btree_cursor_seek(&cursor->btree_cursor, key->data);
        if (exists) {
          return ERR;
        }
      }
      uint32_t current_root = cursor->btree_cursor.tree->root_page_index;
      success =
          btree_cursor_insert(&cursor->btree_cursor, key->data, record->data);
      if (current_root != cursor->btree_cursor.tree->root_page_index) {
        VmEvent event;
        event.type = EVT_BTREE_ROOT_CHANGED;
        event.context.table_info.table_name =
            cursor->schema->table_name.c_str();
        event.context.table_info.column = cursor->is_index ? 0 : cursor->column;

        VM.events.push(event);
      }
    }

    if (!success) {
      return ERR;
    }

    VM.pc++;
    return OK;
  }

  case OP_Delete: {
    int32_t cursor_id = Opcodes::Delete::cursor_id(*inst);

    VmCursor *cursor = &VM.cursors[cursor_id];

    if (cursor->is_memory) {
      memcursor_delete(&cursor->mem_cursor);
    } else {
      btree_cursor_delete(&cursor->btree_cursor);
      uint32_t current_root = cursor->btree_cursor.tree->root_page_index;
      if (current_root != cursor->btree_cursor.tree->root_page_index) {
        VmEvent event;
        event.type = EVT_BTREE_ROOT_CHANGED;
        event.context.table_info.table_name =
            cursor->schema->table_name.c_str();
        event.context.table_info.column = cursor->is_index ? 0 : cursor->column;
        VM.events.push(event);
      }
    }

    VM.pc++;
    return OK;
  }

  case OP_Update: {
    int32_t cursor_id = Opcodes::Update::cursor_id(*inst);
    int32_t record_reg = Opcodes::Update::record_reg(*inst);

    VmCursor *cursor = &VM.cursors[cursor_id];
    TypedValue *record = &VM.registers[record_reg];

    if (cursor->is_index) {
      // updates only on b+tree
      return ERR;
    }

    if (cursor->is_memory) {
      memcursor_update(&cursor->mem_cursor, record->data);
    } else {
      btree_cursor_update(&cursor->btree_cursor, record->data);
    }

    VM.pc++;
    return OK;
  }


  case OP_OpenMemTree: {
    int32_t cursor_id = Opcodes::OpenMemTree::cursor_id(*inst);
    DataType key_type = Opcodes::OpenMemTree::key_type(*inst);
    int32_t record_size = Opcodes::OpenMemTree::record_size(*inst);

    VmCursor &cursor = VM.cursors[cursor_id];

    // Initialize memory tree
    cursor.mem_tree = memtree_create(key_type, record_size);
    cursor.mem_cursor.tree = &cursor.mem_tree;
    cursor.mem_cursor.state = MemCursor::INVALID;
    cursor.mem_cursor.current = nullptr;

    // Mark as memory cursor
    cursor.is_memory = true;
    cursor.is_index = false;
    cursor.schema = nullptr;

    VM.pc++;
    return OK;
  }

  case OP_CreateTable: {
    TableSchema *schema = Opcodes::CreateTable::schema(*inst);

    if (get_table(schema->table_name.c_str())) {
      return ERR;
    }

    Table *new_table = (Table *)arena::alloc<QueryArena>(sizeof(Table));
    new_table->schema = *schema;

    calculate_column_offsets(&new_table->schema);

    new_table->tree = btree_create(new_table->schema.key_type(),
                                   new_table->schema.record_size, BPLUS);

    add_table(new_table);
    VmEvent event;
    event.type = EVT_TABLE_CREATED;
    event.context.table_info.table_name = new_table->schema.table_name.c_str();
    VM.events.push(event);
    VM.pc++;
    return OK;
  }

  case OP_CreateIndex: {
    const char *table_name = Opcodes::CreateIndex::table_name(*inst);
    int32_t column = Opcodes::CreateIndex::column_index(*inst);

    Table *table = get_table(table_name);

    Index *index = (Index *)arena::alloc<QueryArena>(sizeof(Index));
    index->column_index = column;
    index->tree = btree_create(table->schema.columns[column].type,
                               table->schema.key_type(), BTREE);

    add_index(table_name, index);

    VmEvent event;
    event.type = EVT_INDEX_CREATED;
    event.context.table_info.table_name = table_name;
    event.context.table_info.column = column;

    VM.events.push(event);
    VM.pc++;
    return OK;
  }

  case OP_DropTable: {
    const char *table_name = Opcodes::DropTable::table_name(*inst);

    Table *table = get_table(table_name);
    if (!table) {
      return ERR;
    }

    btree_clear(&table->tree);
    for (int i = 0; i < table->indexes.size(); i++) {
      btree_clear(&table->indexes.value_at(i)->tree);
    }

    remove_table(table_name);
    VmEvent event;
    event.type = EVT_TABLE_DROPPED;
    event.context.table_info.table_name = table_name;

    VM.events.push(event);
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
    VmEvent event;
    event.type = EVT_INDEX_DROPPED;
    event.context.table_info.table_name = table_name;
    event.context.table_info.column = column;
    VM.events.push(event);
    VM.pc++;
    return OK;
  }

  case OP_Begin:
    btree_begin_transaction();
    VM.pc++;
    return OK;

  case OP_Commit:
    btree_commit();
    VM.pc++;
    return OK;

  case OP_Rollback:
    btree_rollback();
    VM.pc++;
    return ABORT;

  default:
    printf("Unknown opcode: %d\\n", inst->opcode);
    return ERR;
  }
}

VM_RESULT vm_execute(Vector<VMInstruction, QueryArena> &instructions) {
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

void vm_set_result_callback(ResultCallback callback) { VM.callback = callback; }

Queue<VmEvent, QueryArena> vm_events() { return VM.events; }
