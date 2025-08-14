#include "vm.hpp"
#include "arena.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <vector>

#define HAS(map, val) (VM.map.find(val) != VM.map.end())
#define GET(map, val) (VM.map.find(val)->second)

static struct {
  // Execution state
  std::vector<VMInstruction> program;
  uint32_t pc;
  bool halted;

  // Registers
  VMValue registers[REGISTER_COUNT];

  // Cursors and schema
  std::unordered_map<uint32_t, VmCursor> cursors;
  std::unordered_map<std::string, Table> tables;

  // Comparison result for Jump opcode
  int32_t compare_result;

  // Result callback
  void (*result_callback)(VMValue **, uint32_t);

  // Transaction state
  bool in_transaction;
} VM = {};

// VmCursor implementation
uint8_t *VmCursor::column(uint32_t col_index) {
  if (col_index == 0) {
    // Column 0 is the key, caller should use key() instead
    return nullptr;
  }

  uint8_t *record = bt_cursor_read(&bt_cursor);

  return record + this->column_offsets[col_index];
}

const uint8_t *VmCursor::key() { return bt_cursor_get_key(&bt_cursor); }

// Helper to allocate and copy data to arena
static uint8_t *arena_copy_data(const uint8_t *src, uint32_t size) {
  uint8_t *dst = (uint8_t *)arena_alloc(size);
  if (dst && src) {
    memcpy(dst, src, size);
  }
  return dst;
}

// Helper to create a VMValue
static void vm_set_value(VMValue *val, DataType type, const void *data) {
  val->type = type;
  uint32_t size = VMValue::get_size(type);
  val->data = arena_copy_data((const uint8_t *)data, size);
}

void vm_init() {
  VM.pc = 0;
  VM.halted = false;
  VM.compare_result = 0;
  VM.in_transaction = false;
  VM.result_callback = nullptr;
}

void vm_reset() {
  VM.pc = 0;
  VM.halted = false;
  VM.compare_result = 0;

  // Clear registers
  for (uint32_t i = 0; i < REGISTER_COUNT; i++) {
    VM.registers[i].type = TYPE_NULL;
    VM.registers[i].data = nullptr;
  }

  // Close all cursors
  VM.cursors.clear();

  // Reset arena for next execution
  arena_reset();
}

bool vm_execute(std::vector<VMInstruction> &instructions) {
  vm_reset();
  VM.program = instructions;

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

  case OP_Integer: {
    uint32_t val = inst->p2;
    vm_set_value(&VM.registers[inst->p1], TYPE_INT32, &val);
    VM.pc++;
    return true;
  }

  case OP_String: {
    const char *str = (const char *)inst->p4;
    uint32_t size = inst->p1;
    vm_set_value(&VM.registers[inst->p1], (DataType)size, str);
    VM.pc++;
    return true;
  }
  case OP_Copy:
    VM.registers[inst->p2] = VM.registers[inst->p1];
    VM.pc++;
    return true;

  case OP_Move:
    VM.registers[inst->p2] = VM.registers[inst->p1];
    VM.registers[inst->p1].type = TYPE_NULL;
    VM.registers[inst->p1].data = nullptr;
    VM.pc++;
    return true;

  case OP_OpenRead:
  case OP_OpenWrite: {
    const char *table_name = (const char *)inst->p4;

    auto it = VM.tables.find(table_name);
    if (it == VM.tables.end()) {
      return false;
    }

    Table *table = &it->second;
    uint32_t cursor_id = inst->p1;

    VmCursor &cursor = VM.cursors[cursor_id];

    cursor.record_size = 0;
    // column 0 is key
    for (uint32_t i = 1; i < table->schema.columns.size(); i++) {
      cursor.column_offsets[i] = cursor.record_size;
      cursor.record_size += table->schema.columns[i].type;
    }

    cursor.bt_cursor = bt_cursor_create(&table->tree);
    cursor.schema = &table->schema;
    cursor.is_index = false;

    VM.pc++;
    return true;
  }

  case OP_Close:
    VM.cursors.erase(inst->p1);
    VM.pc++;
    return true;

  case OP_Last:
  case OP_Rewind:
  case OP_First: {
    auto it = VM.cursors.find(inst->p1);
    if (it == VM.cursors.end()) {
      return false;
    }
    VmCursor *cursor = &it->second;

    bool valid = inst->opcode == OP_Last ? bt_cursor_last(&cursor->bt_cursor)
                                         : bt_cursor_first(&cursor->bt_cursor);

    if (!valid) {
      if (inst->p2 > 0) {
        VM.pc = inst->p2;
      } else {
        VM.pc++;
      }
    } else {
      VM.pc++;
    }
    return true;
  }

  case OP_Next:
  case OP_Prev: {
    auto it = VM.cursors.find(inst->p1);
    if (it == VM.cursors.end()) {
      return false;
    }

    VmCursor *cursor = &it->second;
    bool has_more = (inst->opcode == OP_Next)
                        ? bt_cursor_next(&cursor->bt_cursor)
                        : bt_cursor_previous(&cursor->bt_cursor);

    if (has_more) {
      VM.pc++;
    } else if (inst->p2 > 0) {
      VM.pc = inst->p2;
    } else {
      VM.pc++;
    }
    return true;
  }

  case OP_SeekEQ:
  case OP_SeekGT:
  case OP_SeekGE:
  case OP_SeekLE:
  case OP_SeekLT: {
    auto it = VM.cursors.find(inst->p1);
    if (it == VM.cursors.end()) {
      return false;
    }

    VmCursor *cursor = &it->second;
    VMValue *key = &VM.registers[inst->p3];

    bool found = false;
    switch (inst->opcode) {
    case OP_SeekGE:
      found = bt_cursor_seek_ge(&cursor->bt_cursor, key->data);
      break;
    case OP_SeekGT:
      found = bt_cursor_seek_gt(&cursor->bt_cursor, key->data);
      break;
    case OP_SeekLE:
      found = bt_cursor_seek_le(&cursor->bt_cursor, key->data);
      break;
    case OP_SeekLT:
      found = bt_cursor_seek_lt(&cursor->bt_cursor, key->data);
      break;
    case OP_SeekEQ:
      found = bt_cursor_seek(&cursor->bt_cursor, key->data);
      break;
    }

    if (!found && inst->p2 > 0) {
      VM.pc = inst->p2;
    } else {
      VM.pc++;
    }
    return true;
  }

  case OP_Column: {
    auto it = VM.cursors.find(inst->p1);
    if (it == VM.cursors.end()) {
      return false;
    }

    VmCursor *cursor = &it->second;
    uint32_t col_index = inst->p2;

    if (col_index == 0) {
      // Column 0 is the key
      const uint8_t *key_data = cursor->key();
      DataType type = cursor->column_type(0);
      vm_set_value(&VM.registers[inst->p3], type, key_data);
    } else {
      // Other columns come from record
      uint8_t *col_data = cursor->column(col_index);
      DataType type = cursor->column_type(col_index);
      vm_set_value(&VM.registers[inst->p3], type, col_data);
    }

    VM.pc++;
    return true;
  }

  case OP_Key: {
    auto it = VM.cursors.find(inst->p1);
    if (it == VM.cursors.end()) {
      return false;
    }

    VmCursor *cursor = &it->second;
    const uint8_t *key_data = cursor->key();
    DataType type = cursor->column_type(0);
    vm_set_value(&VM.registers[inst->p2], type, key_data);

    VM.pc++;
    return true;
  }

  case OP_MakeRecord: {
    // P1 = starting register
    // P2 = number of columns (excluding key)
    // P3 = destination register

    uint32_t total_size = 0;
    for (int i = 0; i < inst->p2; i++) {
      VMValue *val = &VM.registers[inst->p1 + i];
      total_size += VMValue::get_size(val->type);
    }

    uint8_t *record = (uint8_t *)arena_alloc(total_size);
    uint32_t offset = 0;

    for (int i = 0; i < inst->p2; i++) {
      VMValue *val = &VM.registers[inst->p1 + i];
      uint32_t size = VMValue::get_size(val->type);
      memcpy(record + offset, val->data, size);
      offset += size;
    }

    VM.registers[inst->p3].type = TYPE_INT32; // Store as blob
    VM.registers[inst->p3].data = record;
    VM.pc++;
    return true;
  }

  case OP_Insert: {
    auto it = VM.cursors.find(inst->p1);
    if (it == VM.cursors.end()) {
      return false;
    }

    VmCursor *cursor = &it->second;
    VMValue *key = &VM.registers[inst->p2];
    VMValue *record = &VM.registers[inst->p3];

    bool success =
        bt_cursor_insert(&cursor->bt_cursor, key->data, record->data);
    if (!success) {
      return false;
    }

    VM.pc++;
    return true;
  }

  case OP_Delete: {
    auto it = VM.cursors.find(inst->p1);
    if (it == VM.cursors.end()) {
      return false;
    }

    VmCursor *cursor = &it->second;
    if (!bt_cursor_delete(&cursor->bt_cursor)) {
      return false;
    }

    VM.pc++;
    return true;
  }

  case OP_Update: {
    auto it = VM.cursors.find(inst->p1);
    if (it == VM.cursors.end()) {
      return false;
    }

    VmCursor *cursor = &it->second;
    VMValue *record = &VM.registers[inst->p2];

    if (!bt_cursor_update(&cursor->bt_cursor, record->data)) {
      return false;
    }

    VM.pc++;
    return true;
  }

  case OP_Compare: {
    VMValue *a = &VM.registers[inst->p1];
    VMValue *b = &VM.registers[inst->p3];

    VM.compare_result = cmp(a->type, a->data, b->data);
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

  case OP_Eq:
  case OP_Ne:
  case OP_Lt:
  case OP_Le:
  case OP_Gt:
  case OP_Ge: {
    VMValue *a = &VM.registers[inst->p1];
    VMValue *b = &VM.registers[inst->p3];

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

    if (condition && inst->p2 > 0) {
      VM.pc = inst->p2;
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

  case OP_CreateTable: {
    TableSchema *schema = (TableSchema *)inst->p4;

    if (VM.tables.find(schema->table_name) != VM.tables.end()) {
      // Table already exists
      return false;
    }

    Table new_table;
    new_table.schema = *schema;

    uint32_t record_size = new_table.schema.record_size();

    new_table.tree = bt_create(schema->key_type(), record_size, BPLUS);

    VM.tables[schema->table_name] = new_table;
    VM.pc++;
    return true;
  }
  case OP_DropIndex:
  case OP_DropTable: {
    char *table_name = (char *)inst->p4;
    uint32_t column = inst->p1;

    if (!HAS(tables, table_name)) {
      return false;
    }
    Table *table = &GET(tables, table_name);
    if (column != 0) {
      if (table->indexes.find(column) == table->indexes.end()) {
        return false;
      }

      Index *index = &table->indexes[column];
      // will need to clear index/table memory cache
      return bt_clear(&index->tree);
    }

    bt_clear(&table->tree);

    for (auto [col, index] : table->indexes) {
      bt_clear(&index.tree);
    }

    return true;
  }
  case OP_CreateIndex: {

    char *table_name = (char *)inst->p4;
    uint32_t column = inst->p1;

    if (VM.tables.find(table_name) == VM.tables.end()) {
      return false;
    }

    if (VM.indexes.find(table_name) != VM.indexes.end()) {
      return false;
    }

    Table *table = &VM.tables[table_name];
    ColumnInfo columnInfo = table->schema.columns.at(column);

    Index index = {0};
    index.tree = bt_create(columnInfo.type, table->schema.key_type(), BTREE);
    index.indexed_column = column;
    index.table_name = table_name;
    index.name = columnInfo.name + table_name;

    VM.indexes[index]

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

void vm_set_result_callback(void (*callback)(VMValue **, uint32_t)) {
  VM.result_callback = callback;
}
