#include "vm.hpp"
#include "arena.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include "schema.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <queue>
#include <unordered_map>
#include <vector>

/*------------VMCURSOR---------------- */

struct VmCursor {
  BtCursor btree_cursor;
  TableSchema *schema;
  bool is_index;
};

DataType vb_column_type(VmCursor *vb, uint32_t col_index) {
  return vb->schema->columns[col_index].type;
}

DataType vb_key_type(VmCursor *vb) {
  return vb->schema->columns[0].type;
}

uint8_t *vb_column(VmCursor *vb, uint32_t col_index) {
  if (col_index == 0) {
    return btree_cursor_key(&vb->btree_cursor);
  }

  if (vb->is_index) {
    return btree_cursor_record(&vb->btree_cursor);
  }

  uint8_t *record = btree_cursor_record(&vb->btree_cursor);
  return record + vb->schema->column_offsets[col_index];
}

uint8_t *vb_key(VmCursor *vb) {
  return vb_column(vb, 0);
}

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
  std::vector<VMInstruction> program;
  uint32_t pc;
  bool halted;

  VMValue registers[REGISTER_COUNT];
  std::unordered_map<uint32_t, VmCursor> cursors;
  std::queue<VmEvent> event_queue;

  int32_t compare_result;
  std::vector<std::vector<VMValue>> output_buffer;
  Aggregator aggregator;

  bool initialized;
} VM = {};

// Helper functions
static uint8_t *arena_copy_data(const uint8_t *src, uint32_t size) {
  uint8_t *dst = (uint8_t *)arena_alloc(size);
  if (dst && src) {
    memcpy(dst, src, size);
  }
  return dst;
}

static void vm_set_value(VMValue *val, DataType type, const void *data) {
  val->type = type;
  uint32_t size = VMValue::get_size(type);
  val->data = (uint8_t *)arena_alloc(size);

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
static void emit_event(EventType type, void* data = nullptr) {
  VmEvent event;
  event.type = type;
  event.data = data;
  VM.event_queue.push(event);
}

static void emit_table_event(EventType type, const char* table_name, uint32_t root_page = 0) {
  VmEvent event;
  event.type = type;
  event.data = nullptr;
  event.context.table_info.table_name = table_name;
  event.context.table_info.root_page = root_page;
  VM.event_queue.push(event);
}

static void emit_index_event(EventType type, const char* table_name,
                             uint32_t column_index, const char* index_name = nullptr) {
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



std::vector<std::vector<VMValue>> &vm_output_buffer() {
   return VM.output_buffer;
}

void vm_shutdown() {
  VM.initialized = false;
  VM.cursors.clear();
  VM.event_queue = std::queue<VmEvent>();
  VM.output_buffer.clear();
}
void vm_clear_events() {
  while (!VM.event_queue.empty()) {
    VM.event_queue.pop();
  }
}
void vm_reset() {
  VM.pc = 0;
  VM.halted = false;
  VM.compare_result = 0;

  for (uint32_t i = 0; i < REGISTER_COUNT; i++) {
    VM.registers[i].type = TYPE_NULL;
    VM.registers[i].data = nullptr;
  }

  VM.cursors.clear();
  vm_clear_events();
  VM.output_buffer.clear();
  VM.aggregator.reset();
}

// Public VM functions
void vm_init() {
  VM.initialized = true;
  vm_reset();
}


bool vm_is_halted() {
  return VM.halted;
}

std::queue<VmEvent>& vm_events() {
  return VM.event_queue;
}

// Main execution step
VM_RESULT vm_step() {
  if (VM.halted || VM.pc >= VM.program.size()) {
    return ERR;
  }

  VMInstruction *inst = &VM.program[VM.pc];

  switch (inst->opcode) {
  case OP_Halt:
    VM.halted = true;
    return OK;

  case OP_Goto:
    VM.pc = inst->p2;
    return OK;

  case OP_Integer: {
    uint32_t val = inst->p2;
    vm_set_value(&VM.registers[inst->p1], TYPE_UINT32, &val);
    VM.pc++;
    return OK;
  }

  case OP_String: {
    const char *str = (const char *)inst->p4;
    uint32_t size = inst->p2;
    vm_set_value(&VM.registers[inst->p1], (DataType)size, str);
    VM.pc++;
    return OK;
  }

  case OP_Copy:
    VM.registers[inst->p2] = VM.registers[inst->p1];
    VM.pc++;
    return OK;

  case OP_Move:
    VM.registers[inst->p2] = VM.registers[inst->p1];
    VM.registers[inst->p1].type = TYPE_NULL;
    VM.registers[inst->p1].data = nullptr;
    VM.pc++;
    return OK;

  case OP_OpenRead:
  case OP_OpenWrite: {
    char *table_name = (char *)inst->p4;
    uint32_t index_column = inst->p3;

    Table* table = get_table(table_name);
    if (!table) {
      return ERR;
    }

    uint32_t cursor_id = inst->p2;
    VmCursor &cursor = VM.cursors[cursor_id];

    // need a copy because we might alter the root
    // we we might need to rollback.
    BTree * tree = ARENA_ALLOC(BTree);

    if (index_column != 0) {
      Index *index = get_index(table_name, index_column);
      if (!index) {
        return ERR;
      }

      memcpy(tree, &index->tree, sizeof(BTree));
      cursor.btree_cursor.tree = tree;
      cursor.is_index = true;
    } else {

      memcpy(tree, &table->tree, sizeof(BTree));
      cursor.btree_cursor.tree = tree;
      cursor.is_index = false;
    }

    cursor.schema = &table->schema;
    VM.pc++;
    return OK;
  }

  case OP_Close:
    VM.cursors.erase(inst->p1);
    VM.pc++;
    return OK;

  case OP_Last:
  case OP_Rewind:
  case OP_First: {
    auto it = VM.cursors.find(inst->p1);
    if (it == VM.cursors.end()) {
      return ERR;
    }
    VmCursor *cursor = &it->second;

    bool valid = inst->opcode == OP_Last
                     ? btree_cursor_last(&cursor->btree_cursor)
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
    return OK;
  }

  case OP_Next:
  case OP_Prev: {
    auto it = VM.cursors.find(inst->p1);
    if (it == VM.cursors.end()) {
      return ERR;
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
    return OK;
  }

  case OP_SeekEQ:
  case OP_SeekGT:
  case OP_SeekGE:
  case OP_SeekLE:
  case OP_SeekLT: {
    auto it = VM.cursors.find(inst->p1);
    if (it == VM.cursors.end()) {
      return ERR;
    }

    VmCursor *cursor = &it->second;
    VMValue *key = &VM.registers[inst->p2];

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

    if (!found && inst->p3 > 0) {
      VM.pc = inst->p3;
    } else {
      VM.pc++;
    }
    return OK;
  }

  case OP_Column: {
    auto it = VM.cursors.find(inst->p1);
    if (it == VM.cursors.end()) {
      return ERR;
    }

    VmCursor *cursor = &it->second;
    uint32_t col_index = inst->p2;

    if (col_index == 0) {
      const uint8_t *key_data = vb_key(cursor);
      DataType type = vb_key_type(cursor);
      vm_set_value(&VM.registers[inst->p3], type, key_data);
    } else {
      uint8_t *col_data = vb_column(cursor, col_index);
      DataType type = vb_column_type(cursor, col_index);
      vm_set_value(&VM.registers[inst->p3], type, col_data);
    }
    VM.pc++;
    return OK;
  }

  case OP_MakeRecord: {
    uint32_t total_size = 0;
    for (int i = 1; i < inst->p2; i++) {
      VMValue *val = &VM.registers[inst->p1 + i];
      total_size += VMValue::get_size(val->type);
    }

    uint8_t *record = (uint8_t *)arena_alloc(total_size);
    uint32_t offset = 0;

    for (int i = 1; i < inst->p2; i++) {
      VMValue *val = &VM.registers[inst->p1 + i];
      uint32_t size = VMValue::get_size(val->type);
      memcpy(record + offset, val->data, size);
      offset += size;
    }

    VM.registers[inst->p3].type = TYPE_UINT32;
    VM.registers[inst->p3].data = record;
    VM.pc++;
    return OK;
  }

  case OP_Insert: {
    auto it = VM.cursors.find(inst->p1);
    if (it == VM.cursors.end()) {
      return ERR;
    }

    VmCursor *cursor = &it->second;
    VMValue *key = &VM.registers[inst->p2];
    VMValue *record = &VM.registers[inst->p3];

    uint32_t current_root = cursor->btree_cursor.tree->root_page_index;

    bool exists = btree_cursor_seek(&cursor->btree_cursor, (void*)key->data);
    if(exists) {
       return ERR;
    }

    bool success = btree_cursor_insert(&cursor->btree_cursor, key->data, record->data);
    if (!success) {
      return ERR;
    }

    if (cursor->btree_cursor.tree->root_page_index != current_root) {
      emit_event(EVT_BTREE_ROOT_CHANGED, cursor->btree_cursor.tree);
    }

    emit_row_event(EVT_ROWS_INSERTED, 1);
    VM.pc++;
    return OK;
  }

  case OP_Delete: {
    auto it = VM.cursors.find(inst->p1);
    if (it == VM.cursors.end()) {
      return ERR;
    }
    VmCursor *cursor = &it->second;

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
    auto it = VM.cursors.find(inst->p1);
    if (it == VM.cursors.end()) {
      return ERR;
    }

    VmCursor *cursor = &it->second;
    VMValue *record = &VM.registers[inst->p2];

    btree_cursor_update(&cursor->btree_cursor, record->data);
    emit_row_event(EVT_ROWS_UPDATED, 1);
    VM.pc++;
    return OK;
  }

  case OP_Compare: {
    VMValue *a = &VM.registers[inst->p1];
    VMValue *b = &VM.registers[inst->p3];

    VM.compare_result = cmp(a->type, a->data, b->data);
    VM.pc++;
    return OK;
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
    return OK;
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
    return OK;
  }

  case OP_ResultRow: {
    std::vector<VMValue> row;
    for (int i = 0; i < inst->p2; i++) {
      VMValue copy;
      copy.type = VM.registers[inst->p1 + i].type;
      uint32_t size = VMValue::get_size(copy.type);
      copy.data = (uint8_t *)arena_alloc(size);
      memcpy(copy.data, VM.registers[inst->p1 + i].data, size);
      row.push_back(copy);
    }

    VM.output_buffer.push_back(row);
    VM.pc++;
    return OK;
  }

  case Op_Sort: {
    uint32_t col = inst->p1;
    bool desc = inst->p2;

    std::sort(VM.output_buffer.begin(), VM.output_buffer.end(),
              [col, desc](const auto &a, const auto &b) {
                int cmp_result = cmp(a[col].type, a[col].data, b[col].data);
                return desc ? (cmp_result > 0) : (cmp_result < 0);
              });
    VM.pc++;
    return OK;
  }

  case Op_Flush: {
    for (auto& row : VM.output_buffer) {
      for (auto& val : row) {
        print_ptr(val.data, val.type);
      }
      std::cout << "\n";
    }
    VM.output_buffer.clear();
    VM.pc++;
    return OK;
  }

  case OP_AggStep: {
    VMValue *value = nullptr;
    if (inst->p1 >= 0) {
      value = &VM.registers[inst->p1];
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
    const char *function_name = (char *)inst->p4;

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
    uint32_t result;
    if (VM.aggregator.type == Aggregator::AVG && VM.aggregator.count > 0) {
      result = (uint32_t)(VM.aggregator.accumulator / VM.aggregator.count);
    } else {
      result = (uint32_t)VM.aggregator.accumulator;
    }

    vm_set_value(&VM.registers[inst->p1], TYPE_UINT32, &result);

    VM.aggregator.reset();
    VM.pc++;
    return OK;
  }

  case OP_CreateTable: {
    TableSchema *schema = (TableSchema *)inst->p4;

    // Check if table already exists
    if (get_table(schema->table_name.c_str())) {
      return ERR;
    }

    // Create table structure in arena
    Table *new_table = ARENA_ALLOC(Table);
    new_table->schema = *schema;

    // Calculate offsets and record size
    calculate_column_offsets(&new_table->schema);

    // Create btree for table
    new_table->tree = btree_create(
        new_table->schema.key_type(),
        new_table->schema.record_size,
        BPLUS
    );

    // Emit event with table data
    VmEvent event;
    event.type = EVT_TABLE_CREATED;
    event.data = new_table;
    event.context.table_info.table_name = schema->table_name.c_str();
    event.context.table_info.root_page = new_table->tree.root_page_index;
    VM.event_queue.push(event);

    VM.pc++;
    return OK;
  }

  case OP_CreateIndex: {
    char *table_name = (char *)inst->p4;
    uint32_t column = inst->p1;

    Table *table = get_table(table_name);
    if (!table) {
      return ERR;
    }

    // Check if index already exists
    if (get_index(table_name, column)) {
      return ERR;
    }

    // Create index structure in arena
    Index *index = ARENA_ALLOC(Index);
    index->column_index = column;
    index->tree = btree_create(
        table->schema.columns[column].type,
        table->schema.key_type(),
        BTREE
    );

    // Emit event
    VmEvent event;
    event.type = EVT_INDEX_CREATED;
    event.data = index;
    event.context.index_info.table_name = table_name;
    event.context.index_info.column_index = column;
    event.context.index_info.index_name = nullptr;
    VM.event_queue.push(event);

    VM.pc++;
    return OK;
  }

  case OP_DropTable: {
    char *table_name = (char *)inst->p4;

    Table *table = get_table(table_name);
    if (!table) {
      return ERR;
    }

    // Clear btrees
    btree_clear(&table->tree);
    for (auto& [col, index] : table->indexes) {
      btree_clear(&index.tree);
    }

    emit_table_event(EVT_TABLE_DROPPED, table_name);
    VM.pc++;
    return OK;
  }

  case OP_DropIndex: {
    char *table_name = (char *)inst->p4;
    uint32_t column = inst->p1;

    Table *table = get_table(table_name);
    if (!table) {
      return ERR;
    }

    Index *index = get_index(table_name, column);
    if (!index) {
      return ERR;
    }

    btree_clear(&index->tree);
    emit_index_event(EVT_INDEX_DROPPED, table_name, column);
    VM.pc++;
    return OK;
  }

  case OP_Begin:
    btree_begin_transaction();
    emit_event(EVT_TRANSACTION_BEGIN);
    VM.pc++;
    return OK;

  case OP_Commit:
    btree_commit();
    emit_event(EVT_TRANSACTION_COMMIT);
    VM.pc++;
    return OK;

  case OP_Rollback:
    btree_rollback();
    emit_event(EVT_TRANSACTION_ROLLBACK);
    VM.pc++;
    return ABORT;

  default:
    printf("Unknown opcode: %d\n", inst->opcode);
    return ERR;
  }
}

VM_RESULT vm_execute(std::vector<VMInstruction> &instructions) {
  if (!VM.initialized) {
    vm_init();
  }

  vm_reset();
  VM.program = instructions;

  while (!VM.halted && VM.pc < VM.program.size()) {
    VM_RESULT result = vm_step();
    if (result != OK) {
      return result;
    }
  }
  return OK;
}
