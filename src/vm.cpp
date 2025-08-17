#include "vm.hpp"
#include "arena.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ios>
#include <unordered_map>
#include <vector>


void prec(std::vector<VMValue*> x) {
    for(auto y : x)  {
        print_ptr(y->data,  y->type);
    }
}

/*------------VMCURSOR---------------- */

struct VmCursor {
  BtCursor btree_cursor;
  TableSchema *schema;
  bool is_index;
};

DataType vb_column_type(VmCursor *vb, uint32_t col_index) {
  return vb->schema->columns[col_index].type;
}
DataType vb_key_type(VmCursor *vb) { return vb->schema->columns[0].type; }

uint8_t *vb_column(VmCursor *vb, uint32_t col_index) {
  if (col_index == 0) {
    return btree_cursor_key(&vb->btree_cursor);
  }

  uint8_t *record = btree_cursor_record(&vb->btree_cursor);


auto col = record + vb->schema->column_offsets[col_index];

// debug_type(col, vb->schema->columns[col_index].type);


return col;
}

uint8_t *vb_key(VmCursor *vb) { return vb_column(vb, 0); }

/*------------VMCURSOR---------------- */

// Single aggregator structure
struct Aggregator {
  enum Type { NONE = 0, COUNT = 1, SUM = 2, MIN = 3, MAX = 4, AVG = 5 };
  Type type;
  double accumulator;
  uint32_t count;

  void reset() {
    type = Aggregator::NONE;
    accumulator = 0;
    count = 0;
  }
};

static struct {

  std::vector<VMInstruction> program;
  uint32_t pc;
  bool halted;

  VMValue registers[REGISTER_COUNT];

  std::unordered_map<uint32_t, VmCursor> cursors;
  std::unordered_map<std::string, Table> tables;

  int32_t compare_result;

  std::vector<std::vector<VMValue >> output_buffer;

  Aggregator aggregator;

  bool in_transaction;
} VM = {};



Table& vm_get_table(const std::string&name){
   if(VM.tables.find(name)!= VM.tables.end())  {
       return VM.tables[name];
   }
   exit(1);
}

std::unordered_map<uint32_t, Index> empty_map;
std::unordered_map<uint32_t, Index>& vm_get_table_indexes(const std::string&name) {
    if(VM.tables.find(name)!= VM.tables.end())  {
        return VM.tables[name].indexes;
    }
   return empty_map;
}

// Helper to allocate and copy data to arena
static uint8_t *arena_copy_data(const uint8_t *src, uint32_t size) {
  uint8_t *dst = (uint8_t *)arena_alloc(size);
  if (dst && src) {
    memcpy(dst, src, size);
  }
  return dst;
}

// Helper to create a VMValue
// Helper to create a VMValue
static void vm_set_value(VMValue *val, DataType type, const void *data) {
  val->type = type;
  uint32_t size = VMValue::get_size(type);

  // Allocate full size for the type
  val->data = (uint8_t*)arena_alloc(size);

  if (data) {
    if (type == TYPE_VARCHAR32 || type == TYPE_VARCHAR256) {
      // For strings, zero-fill first then copy what we have
      memset(val->data, 0, size);

      // Copy up to size bytes (data should already be properly sized from parser)
      memcpy(val->data, data, size);
    } else {
      // For fixed-size types, just copy
      memcpy(val->data, data, size);
    }
  } else {
    // No data provided, zero-fill
    memset(val->data, 0, size);
  }
}

void vm_init() {
    pager_init("db");
  VM.pc = 0;
  VM.halted = false;
  VM.compare_result = 0;
  VM.in_transaction = false;
  VM.output_buffer.clear();
  VM.aggregator.reset();
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

  // Clear output buffer
  VM.output_buffer.clear();

  // Reset aggregator
  VM.aggregator.reset();

}

// Get results from buffer
std::vector<std::vector<VMValue >> vm_get_results() {
  return VM.output_buffer;
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
    uint32_t size = inst->p2;
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

    cursor.btree_cursor.tree = &table->tree;
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

    bool valid = inst->opcode == OP_Last ? btree_cursor_last(&cursor->btree_cursor)
                                         : btree_cursor_first(&cursor->btree_cursor);

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
                        ? btree_cursor_next(&cursor->btree_cursor)
                        : btree_cursor_previous(&cursor->btree_cursor);

    if (has_more && inst->p2 > 0) {
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

    // Other columns come from record
    uint8_t *col_data = vb_column(cursor, col_index);
    DataType type = vb_column_type(cursor, col_index);
    vm_set_value(&VM.registers[inst->p3], type, col_data);

    VM.pc++;
    return true;
  }

  case OP_Key: {
    auto it = VM.cursors.find(inst->p1);
    if (it == VM.cursors.end()) {
      return false;
    }

    VmCursor *cursor = &it->second;
    const uint8_t *key_data = vb_key(cursor);
    DataType type = vb_key_type(cursor);
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
      // debug_type(val->data, val->type);
      uint32_t size = VMValue::get_size(val->type);
      memcpy(record + offset, val->data, size);
      offset += size;
    }


    // print_record(record, &VM.tables["Master"].schema);

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
        btree_cursor_insert(&cursor->btree_cursor, key->data, record->data);
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
    if (!btree_cursor_delete(&cursor->btree_cursor)) {
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

    if (!btree_cursor_update(&cursor->btree_cursor, record->data)) {
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
    // Add row to output buffer
    std::vector<VMValue > row;
    for (int i = 0; i < inst->p2; i++) {
        VMValue copy;
               copy.type = VM.registers[inst->p1 + i].type;
               uint32_t size = VMValue::get_size(copy.type);
               copy.data = (uint8_t*)arena_alloc(size);
               memcpy(copy.data, VM.registers[inst->p1 + i].data, size);
               row.push_back(copy);

    }


    VM.output_buffer.push_back(row);
    VM.pc++;
    return true;
  }

  case Op_Sort: {
    // P1 = column index to sort by
    // P2 = 0 for ASC, 1 for DESC
    uint32_t col = inst->p1;
    bool desc = inst->p2;

    std::sort(VM.output_buffer.begin(), VM.output_buffer.end(),
              [col, desc](const auto &a, const auto &b) {
                int cmp_result = cmp(a[col].type, a[col].data, b[col].data);
                return desc ? (cmp_result > 0) : (cmp_result < 0);
              });
    VM.pc++;
    return true;
  }

  case Op_Flush: {
      for(auto x : VM.output_buffer) {
         for(auto y : x)  {
             print_ptr(y.data,  y.type);
         }
         std::cout << "\n";
      }
    // Clear the output buffer
    VM.output_buffer.clear();
    VM.pc++;
    return true;
  }

  case OP_AggStep: {
    // P1 = value register (or -1 for COUNT)
    VMValue *value = nullptr;
    if (inst->p1 >= 0) {
      value = (&VM.registers[inst->p1]);
    }
    Aggregator &aggr = VM.aggregator;

    if (aggr.type == Aggregator::COUNT) {
      aggr.accumulator++;
      aggr.count++;
    } else if (value && value->type == TYPE_INT32) {
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

        break;
      }
    }

    VM.pc++;
    return true;
  }

  case OP_AggFinal: {
    // P1 = output register

    uint32_t result;
    if (VM.aggregator.type == Aggregator::AVG && VM.aggregator.count > 0) {
      result = (uint32_t)(VM.aggregator.accumulator / VM.aggregator.count);
    } else {
      result = (uint32_t)VM.aggregator.accumulator;
    }

    vm_set_value(&VM.registers[inst->p1], TYPE_INT32, &result);

    VM.aggregator.type = Aggregator::NONE;
    VM.aggregator.accumulator = 0;
    VM.aggregator.count = 0;

    VM.pc++;
    return true;
  }

  case OP_CreateTable:

  {
    TableSchema *schema = (TableSchema *)inst->p4;

    if (VM.tables.find(schema->table_name) != VM.tables.end()) {
      // Table already exists
      return false;
    }

    Table new_table;
    new_table.schema = *schema;

    new_table.schema.record_size = 0;
    // column 0 is key
    for (uint32_t i = 1; i < new_table.schema.columns.size(); i++) {
      new_table.schema.column_offsets[i] = new_table.schema.record_size;
      new_table.schema.record_size += new_table.schema.columns[i].type;
    }

    new_table.tree = btree_create(new_table.schema.key_type(),
                               new_table.schema.record_size, BPLUS);

    VM.tables[schema->table_name] = new_table;
    VM.pc++;
    return true;
  }

  case OP_CreateIndex: {
    char *table_name = (char *)inst->p4;
    uint32_t column = inst->p1;

    if (VM.tables.find(table_name) == VM.tables.end()) {
      return false;
    }

    Table *table = &VM.tables[table_name];

    if (table->indexes.find(column) != table->indexes.end()) {
      return false;
    }

    ColumnInfo columnInfo = table->schema.columns.at(column);

    Index index;
    index.tree = btree_create(columnInfo.type, table->schema.key_type(), BTREE);

    table->indexes[column] = index;
    VM.pc++;
    return true;
  }

  case OP_DropTable:
  case OP_DropIndex: {
    char *table_name = (char *)inst->p4;
    uint32_t column = inst->p1;

    if (VM.tables.find(table_name) != VM.tables.end()) {
      return false;
    }
    Table *table = &VM.tables[table_name];
    if (column == 0) {
      btree_clear(&table->tree);
      for (auto [col, index] : table->indexes) {
        btree_clear(&index.tree);
      }
    } else {
      if (table->indexes.find(column) == table->indexes.end()) {
        return false;
      }

      Index *index = &table->indexes[column];
      btree_clear(&index->tree);
      table->indexes.erase(column);
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
