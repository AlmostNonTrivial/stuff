// vm.cpp
#include "vm.hpp"
#include "arena.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "memtree.hpp"
#include "schema.hpp"
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
  uint32_t column_index;
  bool is_memory;
  MemTree mem_tree;

  bool seek(CompareOp op, TypedValue *value) {
    auto key = value;
    auto cursor = this;
    if (cursor->is_memory) {
      return memcursor_seek_cmp(&cursor->mem_cursor, key->data, op);
    }
    return btree_cursor_seek_cmp(&cursor->btree_cursor, key, op);
  }

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

  uint8_t *column(uint32_t col_index) {
    auto vb = this;
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

  DataType column_type(uint32_t col_index) {
    auto vb = this;
    if (vb->is_memory) {
      // For memory trees, we need to handle this differently
      if (col_index == 0) {
        return vb->mem_tree.key_type;
      }
      // The record in memtree doesn't have schema
      return TYPE_NULL;
    }
    return vb->schema->columns[col_index].type;
  }
};

// Helper function for column access

/*------------VM STATE---------------- */

static struct {
  ResultCallback callback;
  Vec<VMInstruction, QueryArena> program;
  uint32_t pc;
  bool halted;
  Vec<VmEvent, QueryArena> event_queue;
  QueryContext current_query_context;
  TypedValue registers[REGISTERS];
  VmCursor cursors[CURSORS];
} VM = {};

static void set_register(TypedValue *dest, TypedValue *src) {
  dest->type = src->type;
  dest->data = (uint8_t *)arena::alloc<QueryArena>((uint32_t)dest->type);
  memcpy(dest->data, src->data, (uint32_t)dest->type);
}

// Public VM functions


static void reset() {
  VM.pc = 0;
  VM.halted = false;

  for (uint32_t i = 0; i < REGISTERS; i++) {
    VM.registers[i].type = TYPE_NULL;
    VM.registers[i].data = nullptr;
  }

  VM.program.clear();
  VM.event_queue.clear();
  VM.event_queue.enable_queue_mode();
}



// Main execution step
static VM_RESULT step() {
  VMInstruction *inst = &VM.program[VM.pc];

  switch (inst->opcode) {
  case OP_Halt:
    VM.halted = true;
    return OK;

  case OP_Goto:
    VM.pc = Opcodes::Goto::target(*inst);
    return OK;

  case OP_Load: {
    int32_t dest_reg = Opcodes::Load::dest_reg(*inst);
    TypedValue *value = Opcodes::Load::value(*inst);
    set_register(&VM.registers[dest_reg], value);
    VM.pc++;
    return OK;
  }

  case OP_Copy: {
    int32_t src = Opcodes::Copy::src_reg(*inst);
    int32_t dest = Opcodes::Copy::dest_reg(*inst);
    set_register(&VM.registers[dest], &VM.registers[src]);
    VM.pc++;
    return OK;
  }

  case OP_Test: {
    int32_t dest = Opcodes::Test::dest_reg(*inst);
    int32_t left = Opcodes::Test::left_reg(*inst);
    int32_t right = Opcodes::Test::right_reg(*inst);
    CompareOp op = Opcodes::Test::op(*inst);

    TypedValue *a = &VM.registers[left];
    TypedValue *b = &VM.registers[right];

    int cmp_result = cmp(a->type, a->data, b->data);

    TypedValue result = {.type = TYPE_UINT32};

    switch (op) {
    case EQ:
      *result.data = (cmp_result == 0) ? 1 : 0;
      break;
    case NE:
      *result.data = (cmp_result != 0) ? 1 : 0;
      break;
    case LT:
      *result.data = (cmp_result < 0) ? 1 : 0;
      break;
    case LE:
      *result.data = (cmp_result <= 0) ? 1 : 0;
      break;
    case GT:
      *result.data = (cmp_result > 0) ? 1 : 0;
      break;
    case GE:
      *result.data = (cmp_result >= 0) ? 1 : 0;
      break;
    }

    set_register(&VM.registers[dest], &result);
    VM.pc++;
    return OK;
  }

  case OP_JumpIf: {
    int32_t test_reg = Opcodes::JumpIf::test_reg(*inst);
    int32_t target = Opcodes::JumpIf::jump_target(*inst);
    bool jump_on_true = Opcodes::JumpIf::jump_on_true(*inst);

    TypedValue *val = &VM.registers[test_reg];
    bool is_true = (*(uint32_t *)val->data != 0);

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

    TypedValue result{
        .type = TYPE_UINT32,
    };

    uint32_t a = *(uint32_t *)VM.registers[left].data;
    uint32_t b = *(uint32_t *)VM.registers[right].data;

    switch (op) {
    case LOGIC_AND:
      *result.data = (a && b) ? 1 : 0;
      break;
    case LOGIC_OR:
      *result.data = (a || b) ? 1 : 0;
      break;
    default:
      return ERR;
    }

    set_register(&VM.registers[dest], &result);
    VM.pc++;
    return OK;
  }

  case OP_ResultRow: {
    int32_t first = Opcodes::ResultRow::first_reg(*inst);
    int32_t count = Opcodes::ResultRow::reg_count(*inst);

    if (VM.callback) {
      // Build result from registers
      uint32_t total_size = 0;
      for (int i = 0; i < count; i++) {
        TypedValue *val = &VM.registers[first + i];
        total_size += val->type;
      }

      uint8_t *result = (uint8_t *)arena::alloc<QueryArena>(total_size);
      uint32_t offset = 0;

      for (int i = 0; i < count; i++) {
        TypedValue *val = &VM.registers[first + i];
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
    result->type = output_type;

    if (!do_arithmetic(op, output_type, result->data, a->data, b->data)) {
      return ERR; // Division by zero
    }

    VM.pc++;
    return OK;
  }

  case OP_Open: {
    int32_t cursor_id = Opcodes::Open::cursor_id(*inst);
    bool is_ephemeral = Opcodes::Open::is_ephemeral(*inst);
    VmCursor &cursor = VM.cursors[cursor_id];

    if (is_ephemeral) {

      // Open memory tree
      DataType key_type = Opcodes::Open::ephemeral_key_type(*inst);
      // For ephemeral, p3 contains record size
      int32_t record_size = Opcodes::Open::ephemeral_record_size(*inst);

      cursor.mem_tree = memtree_create(key_type, record_size);
      cursor.mem_cursor.tree = &cursor.mem_tree;
      cursor.mem_cursor.state = MemCursor::INVALID;
      cursor.mem_cursor.current = nullptr;

      cursor.is_memory = true;
      cursor.is_index = false;
      cursor.schema = nullptr;
    } else {

      const char *table_name = Opcodes::Open::table_name(*inst);
      int32_t index_column = Opcodes::Open::index_col(*inst);
      // Open regular table or index
      Table *table = get_table(table_name);
      if (!table) {
        return ERR;
      }

      cursor.is_memory = false;

      if (index_column != 0) {
        Index *index = get_index(table_name, index_column);
        if (!index) {
          return ERR;
        }

        cursor.btree_cursor.tree = &index->tree;
        cursor.column_index = index_column;
        cursor.is_index = true;
      } else {
        cursor.btree_cursor.tree = &table->tree;
        cursor.is_index = false;
      }

      cursor.schema = &table->schema;
    }

    VM.pc++;
    return OK;
  }

  case OP_Close: {
    VM.pc++;
    return OK;
  }

  case OP_Rewind: {
    int32_t cursor_id = Opcodes::Rewind::cursor_id(*inst);
    int32_t jump_if_empty = Opcodes::Rewind::jump_if_empty(*inst);
    bool to_end = Opcodes::Rewind::to_end(*inst);

    VmCursor *cursor = &VM.cursors[cursor_id];

    bool valid;
    if (cursor->is_memory) {
      valid = to_end ? memcursor_last(&cursor->mem_cursor)
                     : memcursor_first(&cursor->mem_cursor);
    } else {
      valid = to_end ? btree_cursor_last(&cursor->btree_cursor)
                     : btree_cursor_first(&cursor->btree_cursor);
    }

    if (!valid && jump_if_empty >= 0) {
      VM.pc = jump_if_empty;
    } else {
      VM.pc++;
    }
    return OK;
  }

  case OP_Step: {
    int32_t cursor_id = Opcodes::Step::cursor_id(*inst);
    int32_t jump_if_done = Opcodes::Step::jump_if_done(*inst);
    bool forward = Opcodes::Step::forward(*inst);

    VmCursor *cursor = &VM.cursors[cursor_id];
    bool has_more;

    if (cursor->is_memory) {
      has_more = forward ? memcursor_next(&cursor->mem_cursor)
                         : memcursor_previous(&cursor->mem_cursor);
    } else {
      has_more = forward ? btree_cursor_next(&cursor->btree_cursor)
                         : btree_cursor_previous(&cursor->btree_cursor);
    }

    if (!has_more && jump_if_done >= 0) {
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

    bool found = cursor->seek(op, key);

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

    TypedValue value = {.type = cursor->column_type(col_index),
                        .data = cursor->column(col_index)};

    set_register(&VM.registers[dest_reg], &value);
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
        event.table_name =
            cursor->schema->table_name.c_str();
        event.column =
            cursor->is_index ? 0 : cursor->column_index;

        VM.event_queue.push_back(event);
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
        event.table_name =
            cursor->schema->table_name.c_str();
        event.column =
            cursor->is_index ? 0 : cursor->column_index;
        VM.event_queue.push_back(event);
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

  case OP_Schema: {
    SchemaOp op_type = Opcodes::Schema::op_type(*inst);

    switch (op_type) {
    case SCHEMA_CREATE_TABLE: {
      TableSchema *schema = Opcodes::Schema::table_schema(*inst);

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
      event.table_name =
          new_table->schema.table_name.c_str();
      VM.event_queue.push_back(event);
      break;
    }

    case SCHEMA_DROP_TABLE: {
      const char *table_name = Opcodes::Schema::table_name(*inst);

      Table *table = get_table(table_name);
      if (!table) {
        return ERR;
      }

      btree_clear(&table->tree);
      for (size_t i = 0; i < table->indexes.size(); i++) {
        BTree *tree = &table->indexes[i].tree;
        btree_clear(tree);
      }

      remove_table(table_name);
      VmEvent event;
      event.type = EVT_TABLE_DROPPED;
      event.table_name = table_name;
      VM.event_queue.push_back(event);
      break;
    }

    case SCHEMA_CREATE_INDEX: {
      const char *table_name = Opcodes::Schema::table_name(*inst);
      int32_t column = Opcodes::Schema::column_index(*inst);

      Table *table = get_table(table_name);

      Index *index = (Index *)arena::alloc<QueryArena>(sizeof(Index));
      index->column_index = column;
      index->tree = btree_create(table->schema.columns[column].type,
                                 table->schema.key_type(), BTREE);

      add_index(table_name, index);

      VmEvent event;
      event.type = EVT_INDEX_CREATED;
      event.table_name = table_name;
      event.column = column;
      VM.event_queue.push_back(event);
      break;
    }

    case SCHEMA_DROP_INDEX: {
      const char *table_name = Opcodes::Schema::table_name(*inst);
      int32_t column = Opcodes::Schema::column_index(*inst);

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
      event.table_name = table_name;
      event.column = column;
      VM.event_queue.push_back(event);
      break;
    }
    }

    VM.pc++;
    return OK;
  }

  case OP_Transaction: {
    TransactionOp op_type = Opcodes::Transaction::op_type(*inst);

    switch (op_type) {
    case TXN_BEGIN:
      btree_begin_transaction();
      VM.pc++;
      return OK;

    case TXN_COMMIT:
      btree_commit();
      VM.pc++;
      return OK;

    case TXN_ROLLBACK:
      btree_rollback();
      VM.pc++;
      return ABORT;
    }

    return ERR;
  }

  default:
    printf("Unknown opcode: %d\n", inst->opcode);
    return ERR;
  }
}

VM_RESULT vm_execute(Vec<VMInstruction, QueryArena> &instructions) {
  reset();
  VM.program = instructions;

  while (!VM.halted && VM.pc < VM.program.size()) {
    VM_RESULT result = step();
    if (result != OK) {
      return result;
    }
  }
  return OK;
}

void vm_set_result_callback(ResultCallback callback) { VM.callback = callback; }
Vec<VmEvent, QueryArena> &vm_events() { return VM.event_queue; }
