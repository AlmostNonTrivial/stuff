// vm.hpp
#pragma once
#include "arena.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "schema.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

struct QueryContext {
  TableSchema *schema;
  bool has_key_prefix;
};

typedef void (*ResultCallback)(void *result, size_t result_size);
extern bool _debug;

enum EventType {
  EVT_TABLE_CREATED,
  EVT_TABLE_DROPPED,
  EVT_INDEX_CREATED,
  EVT_INDEX_DROPPED,
  EVT_BTREE_ROOT_CHANGED,
  EVT_ROWS_INSERTED,
  EVT_ROWS_DELETED,
  EVT_ROWS_UPDATED,
  EVT_TRANSACTION_BEGIN,
  EVT_TRANSACTION_COMMIT,
  EVT_TRANSACTION_ROLLBACK
};

struct VmEvent {
  EventType type;
  void *data;

  union {
    struct {
      const char *table_name;
      uint32_t root_page;
      uint32_t column;
    } table_info;

    struct {
      const char *table_name;
      uint32_t column_index;
      const char *index_name;
    } index_info;

    struct {
      uint32_t count;
    } row_info;
  } context;
};

Queue<VmEvent, QueryArena> &vm_events();

enum SchemaOp : uint8_t {
  SCHEMA_CREATE_TABLE = 0,
  SCHEMA_DROP_TABLE = 1,
  SCHEMA_CREATE_INDEX = 2,
  SCHEMA_DROP_INDEX = 3
};

enum TransactionOp : uint8_t {
  TXN_BEGIN = 0,
  TXN_COMMIT = 1,
  TXN_ROLLBACK = 2
};

enum OpCode : uint32_t {
  // Control flow
  OP_Goto = 1,
  OP_Halt = 2,

  // Cursor operations
  OP_Open = 10,
  OP_Close = 12,
  OP_Rewind = 13,
  OP_Step = 14,
  OP_Seek = 20,

  // Data operations
  OP_Column = 30,
  OP_MakeRecord = 32,
  OP_Insert = 34,
  OP_Delete = 35,
  OP_Update = 36,

  // Register operations
  OP_Load = 40,
  OP_Copy = 43,

  // Computation
  OP_Test = 60,
  OP_Arithmetic = 51,
  OP_JumpIf = 52,
  OP_Logic = 53,
  OP_ResultRow = 54,

  // Schema operations
  OP_Schema = 80,

  // Transactions
  OP_Transaction = 90,
};

struct VMInstruction {
  OpCode opcode;
  int32_t p1;
  int32_t p2;
  int32_t p3;
  void *p4;
  uint8_t p5;
};

// ============================================================================
// Opcode Descriptors
// ============================================================================

namespace Opcodes {

// Control Flow Operations
struct Goto {
  static VMInstruction create(int32_t target) {
    return {OP_Goto, 0, target, 0, nullptr, 0};
  }
  static int32_t target(const VMInstruction &inst) { return inst.p2; }
};

struct Halt {
  static VMInstruction create(int32_t exit_code = 0) {
    return {OP_Halt, exit_code, 0, 0, nullptr, 0};
  }
  static int32_t exit_code(const VMInstruction &inst) { return inst.p1; }
};

// Cursor Operations
struct Open {
  static VMInstruction create_btree(int32_t cursor_id, const char *table_name,
                                    int32_t index_col = 0,
                                    bool is_write = false) {

    uint8_t flags = (is_write ? 0x01 : 0);
    return {OP_Open, cursor_id, index_col, 0, (void *)table_name, flags};
  }

  static VMInstruction create_ephemeral(int32_t cursor_id, int32_t key_type,
                                        int32_t record_size) {
    uint8_t flags = (0x01) | (0x02);
    return {OP_Open, cursor_id, key_type, record_size, nullptr, flags};
  }

  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static DataType ephemeral_key_type(const VMInstruction &inst) {
    return (DataType)inst.p2;
  }
  static DataType ephemeral_record_size(const VMInstruction &inst) {
    return (DataType)inst.p3;
  }
  static int32_t index_col(const VMInstruction &inst) { return inst.p3; }
  static const char *table_name(const VMInstruction &inst) {
    return (const char *)inst.p4;
  }
  static bool is_write(const VMInstruction &inst) { return inst.p5 & 0x01; }
  static bool is_ephemeral(const VMInstruction &inst) { return inst.p5 & 0x02; }
};

struct Close {
  static VMInstruction create(int32_t cursor_id) {
    return {OP_Close, cursor_id, 0, 0, nullptr, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
};

struct Rewind {
  static VMInstruction create(int32_t cursor_id, int32_t jump_if_empty = -1,
                              bool to_end = false) {
    return {OP_Rewind, cursor_id, jump_if_empty, 0, nullptr, (uint8_t)to_end};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t jump_if_empty(const VMInstruction &inst) { return inst.p2; }
  static bool to_end(const VMInstruction &inst) { return inst.p5 != 0; }
};

struct Step {
  static VMInstruction create(int32_t cursor_id, int32_t jump_if_done = -1,
                              bool forward = true) {
    return {OP_Step, cursor_id, jump_if_done, 0, nullptr, (uint8_t)forward};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t jump_if_done(const VMInstruction &inst) { return inst.p2; }
  static bool forward(const VMInstruction &inst) { return inst.p5 != 0; }
};

struct Seek {
  static VMInstruction create(int32_t cursor_id, int32_t key_reg,
                              int32_t jump_if_not, CompareOp op) {
    return {OP_Seek, cursor_id, key_reg, jump_if_not, nullptr, (uint8_t)op};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t key_reg(const VMInstruction &inst) { return inst.p2; }
  static int32_t jump_if_not(const VMInstruction &inst) { return inst.p3; }
  static CompareOp op(const VMInstruction &inst) { return (CompareOp)inst.p5; }
};

// Data Operations
struct Column {
  static VMInstruction create(int32_t cursor_id, int32_t column_index,
                              int32_t dest_reg) {
    return {OP_Column, cursor_id, column_index, dest_reg, nullptr, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t column_index(const VMInstruction &inst) { return inst.p2; }
  static int32_t dest_reg(const VMInstruction &inst) { return inst.p3; }
};

struct MakeRecord {
  static VMInstruction create(int32_t first_reg, int32_t reg_count,
                              int32_t dest_reg) {
    return {OP_MakeRecord, first_reg, reg_count, dest_reg, nullptr, 0};
  }
  static int32_t first_reg(const VMInstruction &inst) { return inst.p1; }
  static int32_t reg_count(const VMInstruction &inst) { return inst.p2; }
  static int32_t dest_reg(const VMInstruction &inst) { return inst.p3; }
};

struct Insert {
  static VMInstruction create(int32_t cursor_id, int32_t key_reg,
                              int32_t record_reg) {
    return {OP_Insert, cursor_id, key_reg, record_reg, nullptr, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t key_reg(const VMInstruction &inst) { return inst.p2; }
  static int32_t record_reg(const VMInstruction &inst) { return inst.p3; }
};

struct Delete {
  static VMInstruction create(int32_t cursor_id) {
    return {OP_Delete, cursor_id, 0, 0, nullptr, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
};

struct Update {
  static VMInstruction create(int32_t cursor_id, int32_t record_reg) {
    return {OP_Update, cursor_id, record_reg, 0, nullptr, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t record_reg(const VMInstruction &inst) { return inst.p2; }
};

// Register Operations
struct Load {
  static VMInstruction create(int32_t dest_reg, TypedValue *value) {
    return {OP_Load, dest_reg, 0, 0, value, 0};
  }

  static int32_t dest_reg(const VMInstruction &inst) { return inst.p1; }
  static TypedValue*value(const VMInstruction &inst) { return (TypedValue*)inst.p4; }
};

struct Copy {
  static VMInstruction create(int32_t src_reg, int32_t dest_reg) {
    return {OP_Copy, src_reg, dest_reg, 0, nullptr, 0};
  }
  static int32_t src_reg(const VMInstruction &inst) { return inst.p1; }
  static int32_t dest_reg(const VMInstruction &inst) { return inst.p2; }
};

// Computation
struct Test {
  static VMInstruction create(int32_t dest_reg, int32_t left_reg,
                              int32_t right_reg, CompareOp op) {
    return {OP_Test, dest_reg, left_reg, right_reg, nullptr, (uint8_t)op};
  }
  static int32_t dest_reg(const VMInstruction &inst) { return inst.p1; }
  static int32_t left_reg(const VMInstruction &inst) { return inst.p2; }
  static int32_t right_reg(const VMInstruction &inst) { return inst.p3; }
  static CompareOp op(const VMInstruction &inst) { return (CompareOp)inst.p5; }
};

struct Arithmetic {
  static VMInstruction create(int32_t dest_reg, int32_t left_reg,
                              int32_t right_reg, ArithOp op) {
    return {OP_Arithmetic, dest_reg, left_reg, right_reg, nullptr, (uint8_t)op};
  }
  static int32_t dest_reg(const VMInstruction &inst) { return inst.p1; }
  static int32_t left_reg(const VMInstruction &inst) { return inst.p2; }
  static int32_t right_reg(const VMInstruction &inst) { return inst.p3; }
  static ArithOp op(const VMInstruction &inst) { return (ArithOp)inst.p5; }
};

struct JumpIf {
  static VMInstruction create(int32_t test_reg, int32_t jump_target,
                              bool jump_on_true = true) {
    return {OP_JumpIf, test_reg, jump_target,
            0,         nullptr,  (uint8_t)jump_on_true};
  }
  static int32_t test_reg(const VMInstruction &inst) { return inst.p1; }
  static int32_t jump_target(const VMInstruction &inst) { return inst.p2; }
  static bool jump_on_true(const VMInstruction &inst) { return inst.p5 != 0; }
};

struct Logic {
  static VMInstruction create(int32_t dest_reg, int32_t left_reg,
                              int32_t right_reg, LogicOp op) {
    return {OP_Logic, dest_reg, left_reg, right_reg, nullptr, (uint8_t)op};
  }
  static VMInstruction create_not(int32_t dest_reg, int32_t src_reg) {
    return {OP_Logic, dest_reg, src_reg, 0, nullptr, (uint8_t)LOGIC_NOT};
  }
  static int32_t dest_reg(const VMInstruction &inst) { return inst.p1; }
  static int32_t left_reg(const VMInstruction &inst) { return inst.p2; }
  static int32_t right_reg(const VMInstruction &inst) { return inst.p3; }
  static LogicOp op(const VMInstruction &inst) { return (LogicOp)inst.p5; }
};

struct ResultRow {
  static VMInstruction create(int32_t first_reg, int32_t reg_count) {
    return {OP_ResultRow, first_reg, reg_count, 0, nullptr, 0};
  }
  static int32_t first_reg(const VMInstruction &inst) { return inst.p1; }
  static int32_t reg_count(const VMInstruction &inst) { return inst.p2; }
};

// Schema Operations
struct Schema {
  static VMInstruction create_table(TableSchema *schema) {
    return {OP_Schema, 0, 0, 0, schema, SCHEMA_CREATE_TABLE};
  }
  static VMInstruction drop_table(const char *table_name) {
    return {OP_Schema, 0, 0, 0, (void *)table_name, SCHEMA_DROP_TABLE};
  }
  static VMInstruction create_index(const char *table_name,
                                    int32_t column_index) {
    return {OP_Schema, column_index,       0,
            0,         (void *)table_name, SCHEMA_CREATE_INDEX};
  }
  static VMInstruction drop_index(const char *table_name,
                                  int32_t column_index) {
    return {OP_Schema, column_index,       0,
            0,         (void *)table_name, SCHEMA_DROP_INDEX};
  }
  static SchemaOp op_type(const VMInstruction &inst) {
    return (SchemaOp)inst.p5;
  }
  static TableSchema *table_schema(const VMInstruction &inst) {
    return (TableSchema *)inst.p4;
  }
  static const char *table_name(const VMInstruction &inst) {
    return (const char *)inst.p4;
  }
  static int32_t column_index(const VMInstruction &inst) { return inst.p1; }
};

// Transactions
struct Transaction {
  static VMInstruction begin() {
    return {OP_Transaction, 0, 0, 0, nullptr, TXN_BEGIN};
  }
  static VMInstruction commit() {
    return {OP_Transaction, 0, 0, 0, nullptr, TXN_COMMIT};
  }
  static VMInstruction rollback() {
    return {OP_Transaction, 0, 0, 0, nullptr, TXN_ROLLBACK};
  }
  static TransactionOp op_type(const VMInstruction &inst) {
    return (TransactionOp)inst.p5;
  }
};

} // namespace Opcodes

// VM Runtime Definitions
#define REGISTERS 20
#define CURSORS 10

enum VM_RESULT { OK, ABORT, ERR };

// VM Functions
VM_RESULT vm_execute(Vector<VMInstruction, QueryArena> &instructions);
void vm_init();
void vm_reset();
void vm_shutdown();
void vm_set_result_callback(ResultCallback callback);

// Debug functions would go here...
