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


struct VmArena {};

/*------------VMCURSOR---------------- */

// vm.hpp (VmCursor section)

struct VmCursor {
    // Cursor type explicitly enumerated
    enum Type {
        TABLE,      // Primary table cursor
        INDEX,      // Secondary index cursor
        EPHEMERAL   // Memory-only temporary cursor
    };

    Type type;
    RecordLayout layout;  // Value type - no pointer needed!

    // Storage backends (union since only one is active)
    union {
        BtCursor btree;
        MemCursor mem;
    } cursor;

    // Storage trees
    union {
        BTree* btree_ptr;      // For TABLE/INDEX
        MemTree mem_tree;      // For EPHEMERAL (owned by cursor)
    } storage;

    // Metadata (only for persistent tables/indexes)
    const char* table_name;   // nullptr for ephemeral
    uint32_t index_column;     // 0 for primary table, >0 for index

    // ========================================================================
    // Initialization
    // ========================================================================

    void open_table(Table* table) {
        type = TABLE;
        layout = table->to_layout();
        storage.btree_ptr = &table->tree;
        cursor.btree.tree = storage.btree_ptr;
        cursor.btree.state = CURSOR_INVALID;
        table_name = table->table_name.c_str();
        index_column = 0;
    }

    void open_index(Table* table, Index* index) {
        type = INDEX;
        // Index layout: [indexed_column, rowid]
        layout = RecordLayout::create(
            table->columns[index->column_index].type,  // Key
            TYPE_8  // Rowid (assuming 8 bytes)
        );
        storage.btree_ptr = &index->tree;
        cursor.btree.tree = storage.btree_ptr;
        cursor.btree.state = CURSOR_INVALID;
        table_name = table->table_name.c_str();
        index_column = index->column_index;
    }

    void open_ephemeral(const RecordLayout& ephemeral_layout) {
        type = EPHEMERAL;
        layout = ephemeral_layout;  // Copy by value
        storage.mem_tree = memtree_create(layout.key_type(), layout.record_size);
        cursor.mem.tree = &storage.mem_tree;
        cursor.mem.state = MemCursor::INVALID;
        table_name = nullptr;
        index_column = 0;
    }

    // ========================================================================
    // Unified Navigation
    // ========================================================================

    bool rewind(bool to_end = false) {
        if (type == EPHEMERAL) {
            return to_end ? memcursor_last(&cursor.mem)
                         : memcursor_first(&cursor.mem);
        } else {
            return to_end ? btree_cursor_last(&cursor.btree)
                         : btree_cursor_first(&cursor.btree);
        }
    }

    bool step(bool forward = true) {
        if (type == EPHEMERAL) {
            return forward ? memcursor_next(&cursor.mem)
                          : memcursor_previous(&cursor.mem);
        } else {
            return forward ? btree_cursor_next(&cursor.btree)
                          : btree_cursor_previous(&cursor.btree);
        }
    }

    bool seek(CompareOp op, uint8_t* key) {
        if (type == EPHEMERAL) {
            return memcursor_seek_cmp(&cursor.mem, key, op);
        } else {
            return btree_cursor_seek_cmp(&cursor.btree, key, op);
        }
    }

    // ========================================================================
    // Data Access - The Critical Part
    // ========================================================================

    uint8_t* get_key() {
        if (type == EPHEMERAL) {
            return memcursor_key(&cursor.mem);
        } else {
            return btree_cursor_key(&cursor.btree);
        }
    }

    uint8_t* get_record() {
        if (type == EPHEMERAL) {
            return memcursor_record(&cursor.mem);
        } else {
            return btree_cursor_record(&cursor.btree);
        }
    }

    uint8_t* column(uint32_t col_index) {
        // Bounds check
        if (col_index >= layout.column_count()) {
            return nullptr;
        }

        // Column 0 is always the key
        if (col_index == 0) {
            return get_key();
        }

        // For other columns, get the record
        uint8_t* record = get_record();
        if (!record) return nullptr;

        // Special case: index cursors
        if (type == INDEX) {
            // Index record is just the rowid (column 1)
            return (col_index == 1) ? record : nullptr;
        }

        // Regular table/ephemeral: use pre-calculated offsets
        return record + layout.get_offset(col_index);
    }

    DataType column_type(uint32_t col_index) {
        if (col_index >= layout.column_count()) {
            return TYPE_NULL;
        }
        return layout.layout[col_index];
    }

    // ========================================================================
    // Modification Operations
    // ========================================================================

    bool insert(uint8_t* key, uint8_t* record) {
        if (type == EPHEMERAL) {
            return memcursor_insert(&cursor.mem, key, record);
        } else {
            uint32_t old_root = storage.btree_ptr->root_page_index;
            bool success = btree_cursor_insert(&cursor.btree, key, record);

            // Check if root changed (for event emission)
            if (success && old_root != storage.btree_ptr->root_page_index) {
                // VM can check this flag and emit event
                root_changed = true;
            }
            return success;
        }
    }

    bool update(uint8_t* record) {
        if (type == EPHEMERAL) {
            return memcursor_update(&cursor.mem, record);
        } else {
            return btree_cursor_update(&cursor.btree, record);
        }
    }

    bool remove() {
        if (type == EPHEMERAL) {
            return memcursor_delete(&cursor.mem);
        } else {
            uint32_t old_root = storage.btree_ptr->root_page_index;
            bool success = btree_cursor_delete(&cursor.btree);

            if (success && old_root != storage.btree_ptr->root_page_index) {
                root_changed = true;
            }
            return success;
        }
    }

    // ========================================================================
    // State & Metadata
    // ========================================================================

    bool is_valid() {
        if (type == EPHEMERAL) {
            return cursor.mem.state == MemCursor::VALID;
        } else {
            return cursor.btree.state == CURSOR_VALID;
        }
    }

    uint32_t record_size() const {
        return layout.record_size;
    }

    DataType key_type() const {
        return layout.key_type();
    }

    bool root_changed = false;  // Flag for VM to check
};

// Helper function for column access

/*------------VM STATE---------------- */

static struct {
  ResultCallback callback;
  Vec<VMInstruction, QueryArena> program;
  uint32_t pc;
  bool halted;
  Vec<VmEvent, QueryArena> event_queue;
  VMValue registers[REGISTERS];
  VmCursor cursors[CURSORS];
} VM = {};


static void set_register(VMValue *dest, VMValue *src) {
  dest->type = src->type;
  memcpy(dest->data, src->data, (uint32_t)src->type);
}
static void set_register(VMValue *dest, uint8_t*data, DataType type) {
  dest->type = type;
  memcpy(dest->data, data, type);
}

static void build_record(uint8_t *data, int32_t first_reg, int32_t count) {
  int32_t offset = 0;
  for (int i = 0; i < count; i++) {
    VMValue *val = &VM.registers[first_reg + i];
    uint32_t size = val->type;
    memcpy(data + offset, val->data, size);
    offset += size;
  }
}

static void emit_vm_event(EventType evt, VmCursor *cursor = nullptr,
                          const char *table_name = nullptr,
                          uint32_t col_index = 0) {
  VmEvent event;

  switch (evt) {
  case EVT_BTREE_ROOT_CHANGED: {
    event.type = evt;
    event.table_name = cursor->schema->table_name.c_str();
    event.column = cursor->is_index ? 0 : cursor->column_index;
    break;
  }
  case EVT_INDEX_CREATED:
  case EVT_TABLE_CREATED:
  case EVT_INDEX_DROPPED:
  case EVT_TABLE_DROPPED: {
    event.type = evt;
    event.table_name = table_name;
    event.column = col_index;
    break;
  }
  default:
    event.type = evt;
    break;
  }

  VM.event_queue.push_back(event);
}

static void reset() {
  VM.pc = 0;
  VM.halted = false;

  for (uint32_t i = 0; i < REGISTERS; i++) {
    VM.registers[i].type = TYPE_NULL;
  }

  VM.program.clear();
  VM.event_queue.clear();
  VM.event_queue.enable_queue_mode();
}

// Debug helper
void vm_debug_print_all_registers() {
  printf("===== REGISTERS =====\n");
  for (int i = 0; i < REGISTERS; i++) {
    if (VM.registers[i].type != TYPE_NULL) {
      Debug::print_register(i, VM.registers[i]);
    }
  }
  printf("====================\n");
}

// Main execution step
static VM_RESULT step() {
  VMInstruction *inst = &VM.program[VM.pc];

  if (_debug) {
    printf("\n[%3d] %-12s ", VM.pc, Debug::opcode_name(inst->opcode));
  }

  switch (inst->opcode) {
  case OP_Halt:
    VM.halted = true;
    return OK;

  case OP_Goto:
    VM.pc = Opcodes::Goto::target(*inst);
    return OK;

  case OP_Move: {
    int32_t dest_reg = Opcodes::Move::dest_reg(*inst);

    if (Opcodes::Move::is_load(*inst)) {
      uint8_t *data = Opcodes::Move::data(*inst);
      DataType type = Opcodes::Move::type(*inst);

      if (_debug) {
        printf("=> R[%d] = ", dest_reg);
        print_value(type, data);
        printf(" (type=%d)", type);
      }
      set_register(&VM.registers[dest_reg], data, type);
    } else {
      int32_t src_reg = Opcodes::Move::src_reg(*inst);
      auto *src = &VM.registers[src_reg];
      if (_debug) {
        printf("=> R[%d] = ", dest_reg);
        print_value(src->type, src->data);
        printf(" (type=%d)", src->type);
      }
      set_register(&VM.registers[dest_reg], src);
    }

    VM.pc++;
    return OK;
  }
  case OP_Test: {
    int32_t dest = Opcodes::Test::dest_reg(*inst);
    int32_t left = Opcodes::Test::left_reg(*inst);
    int32_t right = Opcodes::Test::right_reg(*inst);
    CompareOp op = Opcodes::Test::op(*inst);

    VMValue *a = &VM.registers[left];
    VMValue *b = &VM.registers[right];

    int cmp_result = cmp(a->type, a->data, b->data);

    VMValue result = {.type = TYPE_4};

    bool test_result = false;
    switch (op) {
    case EQ:
      test_result = (cmp_result == 0);
      break;
    case NE:
      test_result = (cmp_result != 0);
      break;
    case LT:
      test_result = (cmp_result < 0);
      break;
    case LE:
      test_result = (cmp_result <= 0);
      break;
    case GT:
      test_result = (cmp_result > 0);
      break;
    case GE:
      test_result = (cmp_result >= 0);
      break;
    }
    *(uint32_t *)result.data = test_result ? 1 : 0;

    if (_debug) {
      const char *op_names[] = {"==", "!=", "<", "<=", ">", ">="};
      printf("R[%d] = (", dest);
      print_value(a->type, a->data);
      printf(" %s ", op_names[op]);
      print_value(b->type, b->data);
      printf(") => %s", test_result ? "TRUE" : "FALSE");
    }

    set_register(&VM.registers[dest], &result);
    VM.pc++;
    return OK;
  }

  case OP_JumpIf: {
    int32_t test_reg = Opcodes::JumpIf::test_reg(*inst);
    int32_t target = Opcodes::JumpIf::jump_target(*inst);
    bool jump_on_true = Opcodes::JumpIf::jump_on_true(*inst);

    VMValue *val = &VM.registers[test_reg];
    bool is_true = (*(uint32_t *)val->data != 0);
    bool will_jump = (is_true && jump_on_true) || (!is_true && !jump_on_true);

    if (_debug) {
      printf("R[%d]=", test_reg);
      print_value(val->type, val->data);
      printf(" (%s), jump_on_%s => %s to PC=%d", is_true ? "TRUE" : "FALSE",
             jump_on_true ? "true" : "false",
             will_jump ? "JUMPING" : "CONTINUE",
             will_jump ? target : VM.pc + 1);
    }

    if (will_jump) {
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

    VMValue result{.type = TYPE_4};

    uint32_t a = *(uint32_t *)VM.registers[left].data;
    uint32_t b = *(uint32_t *)VM.registers[right].data;
    uint32_t res_val;

    switch (op) {
    case LOGIC_AND:
      res_val = (a && b) ? 1 : 0;
      break;
    case LOGIC_OR:
      res_val = (a || b) ? 1 : 0;
      break;
    default:
      return ERR;
    }
    *(uint32_t *)result.data = res_val;

    if (_debug) {
      const char *op_names[] = {"AND", "OR"};
      printf("R[%d] = %d %s %d => %d", dest, a, op_names[op], b, res_val);
    }

    set_register(&VM.registers[dest], &result);
    VM.pc++;
    return OK;
  }

  case OP_Result: {
    int32_t first_reg = Opcodes::Result::first_reg(*inst);
    int32_t reg_count = Opcodes::Result::reg_count(*inst);

    if (_debug) {
      printf("OUTPUT: ");
      for (int i = 0; i < reg_count; i++) {
        if (i > 0)
          printf(", ");
        VMValue *val = &VM.registers[first_reg + i];
        if (val->type != TYPE_NULL) {
          print_value(val->type, val->data);
        } else {
          printf("NULL");
        }
      }
    }

    if (!VM.callback) {
      VM.pc++;
      return OK;
    }

    Vec<TypedValue, QueryArena> output;

    for (int i = 0; i < reg_count; i++) {
      VMValue *val = &VM.registers[first_reg + i];
      TypedValue value = {.type = val->type};
      value.data = (uint8_t*)arena::alloc<QueryArena>(val->type);
      memcpy(value.data, val->data, val->type);
      output.push_back(value);
    }

    VM.callback(output);
    VM.pc++;
    return OK;
  }

  case OP_Arithmetic: {
    int32_t dest = Opcodes::Arithmetic::dest_reg(*inst);
    int32_t left = Opcodes::Arithmetic::left_reg(*inst);
    int32_t right = Opcodes::Arithmetic::right_reg(*inst);
    ArithOp op = Opcodes::Arithmetic::op(*inst);

    VMValue *a = &VM.registers[left];
    VMValue*b = &VM.registers[right];
    VMValue*dst = &VM.registers[dest];

    // Determine output type (use larger of the two)
    DataType output_type = (a->type > b->type) ? a->type : b->type;
    uint8_t data[output_type];
    VMValue result = {.type = output_type,};

    bool success =
        do_arithmetic(op, output_type, result.data, a->data, b->data);

    if (_debug) {
      const char *op_names[] = {"+", "-", "*", "/", "%"};
      printf("R[%d] = ", dest);
      print_value(a->type, a->data);
      printf(" %s ", op_names[op]);
      print_value(b->type, b->data);
      printf(" = ");
      if (success) {
        print_value(result.type, result.data);
      } else {
        printf("ERROR");
      }
    }

    if (!success) {
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
      Schema *schema = Opcodes::Open::ephemeral_schema(*inst);
      cursor.mem_tree = memtree_create(schema->key_type(), schema->record_size);
      cursor.mem_cursor.tree = &cursor.mem_tree;
      cursor.mem_cursor.state = MemCursor::INVALID;
      cursor.mem_cursor.current = nullptr;

      cursor.is_memory = true;
      cursor.is_index = false;
      cursor.schema = schema;
    } else {
      const char *table_name = Opcodes::Open::table_name(*inst);
      int32_t index_column = Opcodes::Open::index_col(*inst);

      // Open regular table or index
      Table *table = get_table(table_name);

      cursor.is_memory = false;

      if (index_column != 0) {
        Index *index = get_index(table_name, index_column);
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
    VMValue *key = &VM.registers[key_reg];

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


    uint8_t *data = cursor->column(col_index);
    DataType type = cursor->column_type(col_index);


    if (_debug) {
      printf("R[%d] = cursor[%d].col[%d] = ", dest_reg, cursor_id, col_index);
      if (data) {
        print_value(type, data);
      } else {
        printf("NULL");
      }
      printf(" (type=%d)", type);
    }

    set_register(&VM.registers[dest_reg], data, type);
    VM.pc++;
    return OK;
  }

  case OP_Delete: {
    int32_t cursor_id = Opcodes::Delete::cursor_id(*inst);

    VmCursor *cursor = &VM.cursors[cursor_id];

    if (cursor->is_memory) {
      memcursor_delete(&cursor->mem_cursor);
    } else {
      uint32_t current_root = cursor->btree_cursor.tree->root_page_index;
      btree_cursor_delete(&cursor->btree_cursor);
      if (current_root != cursor->btree_cursor.tree->root_page_index) {
        emit_vm_event(EVT_BTREE_ROOT_CHANGED, cursor);
      }
    }

    VM.pc++;
    return OK;
  }
  case OP_Insert: {
    int32_t cursor_id = Opcodes::Insert::cursor_id(*inst);
    int32_t key_reg = Opcodes::Insert::key_reg(*inst);
    int32_t record_reg = key_reg + 1;
    int32_t count = Opcodes::Insert::reg_count(*inst);

    VmCursor *cursor = &VM.cursors[cursor_id];
    VMValue *key = &VM.registers[key_reg];

    uint8_t data[cursor->record_size()];
    build_record(data, record_reg, count - 1);
    bool success;
    if (cursor->is_memory) {
      success = memcursor_insert(&cursor->mem_cursor, key->data, data);
    } else {
      uint32_t current_root = cursor->btree_cursor.tree->root_page_index;
      success = btree_cursor_insert(&cursor->btree_cursor, key->data, data);
      if (current_root != cursor->btree_cursor.tree->root_page_index) {
        emit_vm_event(EVT_BTREE_ROOT_CHANGED, cursor);
      }
    }
    if (!success) {
      return ERR;
    }

    VM.pc++;
    return OK;
  }

  case OP_Update: {
    int32_t cursor_id = Opcodes::Update::cursor_id(*inst);
    int32_t record_reg = Opcodes::Update::record_reg(*inst);

    VmCursor *cursor = &VM.cursors[cursor_id];

    // Calculate record size from schema

    uint8_t data[cursor->record_size()];
    build_record(data, record_reg, cursor->schema->columns.size() - 1);

    if (cursor->is_memory) {
      memcursor_update(&cursor->mem_cursor, data);
    } else {
      btree_cursor_update(&cursor->btree_cursor, data);
    }

    VM.pc++;
    return OK;
  }

  case OP_Schema: {
    SchemaOp op_type = Opcodes::Schema::op_type(*inst);

    switch (op_type) {
    case SCHEMA_CREATE_TABLE: {
      Schema *schema = Opcodes::Schema::table_schema(*inst);

      Table *new_table = (Table *)arena::alloc<RegistryArena>(sizeof(Table));

      new_table->schema = *schema;

      calculate_column_offsets(&new_table->schema);

      new_table->tree = btree_create(new_table->schema.key_type(),
                                     new_table->schema.record_size, BPLUS);

      add_table(new_table);

      emit_vm_event(EVT_TABLE_CREATED, nullptr,
                    new_table->schema.table_name.c_str());

      break;
    }

    case SCHEMA_DROP_TABLE: {
      const char *table_name = Opcodes::Schema::table_name(*inst);

      Table *table = get_table(table_name);

      btree_clear(&table->tree);
      for (size_t i = 0; i < table->indexes.size(); i++) {
        BTree *tree = &table->indexes[i].tree;
        btree_clear(tree);
      }

      emit_vm_event(EVT_TABLE_DROPPED, nullptr, table_name);
      remove_table(table_name);
      break;
    }

    case SCHEMA_CREATE_INDEX: {
      const char *table_name = Opcodes::Schema::table_name(*inst);
      int32_t column = Opcodes::Schema::column_index(*inst);

      Table *table = get_table(table_name);

      Index *index = (Index *)arena::alloc<RegistryArena>(sizeof(Index));
      index->column_index = column;
      index->tree = btree_create(table->schema.columns[column].type,
                                 table->schema.key_type(), BTREE);

      add_index(table_name, index);

      emit_vm_event(EVT_INDEX_CREATED, nullptr, table_name,
                    index->column_index);

      break;
    }

    case SCHEMA_DROP_INDEX: {
      const char *table_name = Opcodes::Schema::table_name(*inst);
      int32_t column = Opcodes::Schema::column_index(*inst);

      Index *index = get_index(table_name, column);

      emit_vm_event(EVT_INDEX_DROPPED, nullptr, table_name,
                    index->column_index);

      btree_clear(&index->tree);
      remove_index(table_name, column);
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

  if (_debug) {
    Debug::print_program(instructions);
  }

  while (!VM.halted && VM.pc < VM.program.size()) {
    VM_RESULT result = step();
    if (result != OK) {
      if (_debug) {
        printf("\nVM execution failed at PC=%d\n", VM.pc);
        vm_debug_print_all_registers();
      }
      return result;
    }
  }

  if (_debug) {
    printf("\n\nVM execution completed successfully\n");
  }

  return OK;
}

void vm_set_result_callback(ResultCallback callback) { VM.callback = callback; }

Vec<VmEvent, QueryArena> &vm_events() { return VM.event_queue; }
