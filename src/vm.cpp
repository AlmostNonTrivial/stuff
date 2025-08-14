#include "vm.hpp"
#include "arena.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#define HAS(map, val) (VM.map.find(val) != VM.map.end())
#define GET(map, val) (VM.map.find(val)->second)

struct VmCursor {
  BtCursor *bt_cursor;
  //[id(4), name(32), age(4)]
  // [0,4,36]
  std::vector<uint32_t> column_offsets;
  TableSchema *schema;

  void calculate(TableSchema *schema) {
    this->schema = schema;
    uint32_t rolling = 0;
    // 0 is the schema is the key.
    for (int i = 1; i < schema->columns.size(); i++) {
      column_offsets[i] = rolling;
      rolling += schema->columns[i].type;
    }
  }

  uint8_t *column(uint32_t index) {
    if (index == 0) {
      PRINT "use get key" END;
      exit(0);
    }
    return bt_cursor_read(this->bt_cursor) + column_offsets[index];
  }

  const uint8_t *key(uint32_t index) {
    return bt_cursor_get_key(this->bt_cursor);
  }

  DataType key_type() { return get_type(0); }

  DataType get_type(uint32_t index) { this->schema->columns[index]; }
};

static struct {
  // Execution state
  std::vector<VMInstruction> program;
  uint32_t pc;
  bool halted;

  // Registers
  VMValue registers[REGISTER_COUNT];

  std::unordered_map<uint32_t, VmCursor> cursors;
  std::unordered_map<std::string, Table> tables;
  std::unordered_map<std::string, Index> indexes;

  int32_t compare_result;

  void (*result_callback)(VMValue **, uint32_t);
  bool in_transaction;
} VM = {};

void vm_reset() {
  VM.pc = 0;
  VM.halted = false;
  VM.compare_result = 0;

  // Clear registers
  for (uint32_t i = 0; i < REGISTER_COUNT; i++) {
    VM.registers[i].type = TYPE_NULL;
  }

  VM.cursors.clear();
}

bool vm_step() {
  if (VM.halted || VM.pc >= VM.program.size()) {
    return false;
  }

  VMInstruction *inst = &VM.program[VM.pc];

  switch (inst->opcode) {
  case OP_Halt:
    VM.halted = true;
    return true;

  case OP_Goto:
    VM.pc = inst->p2;
    return true;

  case OP_Integer:
    VM.registers[inst->p1].type = TYPE_INT32;
    VM.registers[inst->p1].i32 = inst->p2;
    VM.pc++;
    return true;
  case OP_String:
    VM.registers[inst->p1].type = TYPE_VARCHAR32;
    VM.registers[inst->p1].str32 = (char *)inst->p4;
    VM.pc++;
    return true;

  case OP_Copy:
    VM.registers[inst->p2] = VM.registers[inst->p1];
    VM.pc++;
    return true;

  case OP_Move:
    VM.registers[inst->p2] = VM.registers[inst->p1];
    VM.registers[inst->p1].type = TYPE_NULL;
    VM.pc++;
    return true;

  case OP_OpenRead:
  case OP_OpenWrite: {
    const char *table_name = (const char *)inst->p4;

    if (!HAS(tables, table_name)) {
      return false;
    }

    uint32_t cursor_id = inst->p2;

    Table *table = &GET(tables, table_name);

    if (!table) {
      return false;
    }

    VM.cursors[cursor_id] = bt_cursor_create(&table->tree);
    VM.pc++;
    return true;
  }

  case OP_CreateTable:
    // case OP_CreateIndex:
    {
      TableSchema schema = *(TableSchema *)inst->p4;
      if (VM.tables.find(schema.table_name) != VM.tables.end()) {
        /* table already exists */
        return false;
      }

      Table new_tables = {0};
      new_tables.schema = schema;

      uint32_t record_size = 0;
      for (uint32_t i = 1; i < schema.columns.size(); i++) {
        record_size += schema.columns[i].type;
      }

      if (record_size == 0) {
        return false;
      }

      new_tables.tree = bt_create(schema.key(), record_size, BPLUS);
      bp_init(new_tables.tree);

      return true;
    }

  case OP_Close:
    VM.cursors[inst->p1] = nullptr;
    VM.pc++;
    return true;

  case OP_Rewind: {
    VmCursor *cursor = &VM.cursors[inst->p1];
    if (!cursor) {
      return false;
    }

    if (!bt_cursor_first(cursor->bt_cursor) && inst->p2 > 0) {
      VM.pc = inst->p2;
    } else {
      VM.pc++;
    }
    return true;
  }

  case OP_Next:
  case OP_Prev: {
    VmCursor *cursor = &VM.cursors[inst->p1];
    if (!cursor) {
      return false;
    }
    uint32_t jump_address = inst->p2;

    bool has_more = (inst->opcode == OP_Next)
                        ? bt_cursor_next(cursor->bt_cursor)
                        : bt_cursor_previous(cursor->bt_cursor);

    if (has_more && jump_address) {
      VM.pc = jump_address;
    }

    return true;
  }

  case OP_SeekEQ:
  case OP_SeekGT:
  case OP_SeekGE:
  case OP_SeekLE:
  case OP_SeekLT: {
    BtCursor *cursor = &VM.cursors[inst->p1];
    VMValue key = VM.registers[inst->p2];

    uint32_t jump_if_not_found = inst->p3;

    bool found;

    switch (inst->opcode) {
    case OP_SeekGE:
      found = bt_cursor_seek_ge(cursor, &key.data);
      break;
    case OP_SeekGT:
      found = bt_cursor_seek_gt(cursor, &key.data);
      break;
    case OP_SeekLE:
      found = bt_cursor_seek_le(cursor, &key.data);
      break;
    case OP_SeekLT:
      found = bt_cursor_seek_lt(cursor, &key.data);
      break;
    case OP_SeekEQ:
      found = bt_cursor_seek(cursor, &key.data);
      break;
    default:
      return false;
    }

    if (!found && jump_if_not_found > 0) {
      VM.pc = jump_if_not_found - 1;
    }

    return true;
  }

  case OP_Column: {
    VmCursor *cursor = &VM.cursors[inst->p1];
    uint8_t *data = cursor->column(inst->p2);

    DataType col_type = cursor->get_type(inst->p2);
    VMValue *reg = &VM.registers[inst->p3];
    memcpy(reg->data, data, col_type);

    VM.pc++;
    return true;
  }

  case OP_Rowid: {
    BtCursor *cursor = &VM.cursors[inst->p1];
    VM.registers[inst->p2].type = VMValue::TYPE_INT32;
    VM.registers[inst->p2].v.i32 = vmc_get_rowid(cursor);
    VM.pc++;
    return true;
  }

  case OP_Insert: {
    VMCursor *cursor = VM.cursors[inst->p1];
    uint32_t rowid = VM.registers[inst->p3].v.i32;
    void *record = VM.registers[inst->p2].v.blob.data;

    if (!vmc_insert(cursor, rowid, record)) {
      return false;
    }
    VM.pc++;
    return true;
  }

  case OP_Delete: {
    VMCursor *cursor = VM.cursors[inst->p1];
    if (!vmc_delete(cursor)) {
      return false;
    }
    VM.pc++;
    return true;
  }

  case OP_Compare: {
    VMValue *a = &VM.registers[inst->p1];
    VMValue *b = &VM.registers[inst->p2];

    VM.compare_result = cmp(a->type, a, b);
    VM.pc++;
    return true;
  }

  case OP_Jump: {
    if (VM.compare_result < 0 && inst->p1 > 0) {
      VM.pc = inst->p1;
    } else if (VM.compare_result == 0 && inst->p2 > 0) {
      VM.pc = inst->p2;
    } else if (VM.compare_result > 0 && inst->p3 > 0) {
      VM.pc = inst->p3;
    } else {
      VM.pc++;
    }
    return true;
  }

  case OP_ResultRow: {
    if (VM.result_callback) {
      VMValue **row = ARENA_ALLOC_ARRAY(VMValue *, inst->p2);
      for (int i = 0; i < inst->p2; i++) {
        row[i] = &VM.registers[inst->p1 + i];
      }
      VM.result_callback(row, inst->p2);
    }
    VM.pc++;
    return true;
  }

  case OP_Begin:
    pager_begin_transaction();
    VM.in_transaction = true;
    VM.pc++;
    return true;

  case OP_Commit:
    pager_commit();
    VM.in_transaction = false;
    VM.pc++;
    return true;

  case OP_Rollback:
    pager_rollback();
    VM.in_transaction = false;
    VM.pc++;
    return true;

  default:
    printf("Unknown opcode: %d\n", inst->opcode);
    return false;
  }
}

bool vm_execute(std::vector<VMInstruction> instructions, uint32_t count) {
  VM.program = instructions;
  VM.pc = 0;
  VM.halted = false;
  while (!VM.halted && VM.pc < VM.program.size()) {
    if (!vm_step()) {
      if (VM.in_transaction) {
        pager_rollback();
        VM.in_transaction = false;
      }
      return false;
    }
  }
  return true;
}
