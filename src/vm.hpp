#pragma once
#include "btree.hpp"
#include "defs.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <queue>
#include <unordered_map>
#include <vector>
#include "schema.hpp"

// VM value - uses arena allocation for data
struct VMValue {
  DataType type;
  uint8_t *data; // Points to arena-allocated memory

  // Helper to get size based on type
  static uint32_t get_size(DataType t) { return static_cast<uint32_t>(t); }
};

/*
Parameter Convention:
P1: Primary operand (cursor ID for cursor ops, destination register for data ops)
P2: Secondary operand (source register, jump target, or count)
P3: Tertiary operand (additional register or column index)
P4: Pointer data (strings, schemas, names)
P5: Flags byte
*/

enum OpCode : uint8_t {
  // Control flow
  // P1: unused, P2: unused, P3: unused, P4: trace message, P5: unused
  OP_Trace = 0,

  // P1: unused, P2: jump target, P3: unused, P4: unused, P5: unused
  OP_Goto = 1,

  // P1: exit code, P2: unused, P3: unused, P4: unused, P5: unused
  OP_Halt = 2,

  // Cursor operations
  // P1: unused, P2: cursor_id, P3: unused, P4: table_name, P5: unused
  OP_OpenRead = 10,

  // P1: unused, P2: cursor_id, P3: column_index (for index), P4: table_name, P5: unused
  OP_OpenWrite = 11,

  // P1: cursor_id, P2: unused, P3: unused, P4: unused, P5: unused
  OP_Close = 12,

  // P1: cursor_id, P2: jump_if_empty, P3: unused, P4: unused, P5: unused
  OP_Rewind = 13,

  // P1: cursor_id, P2: jump_if_done, P3: unused, P4: unused, P5: unused
  OP_Next = 14,

  // P1: cursor_id, P2: jump_if_done, P3: unused, P4: unused, P5: unused
  OP_Prev = 15,

  // P1: cursor_id, P2: jump_if_empty, P3: unused, P4: unused, P5: unused
  OP_First = 16,

  // P1: cursor_id, P2: jump_if_empty, P3: unused, P4: unused, P5: unused
  OP_Last = 17,

  // Seek operations
  // P1: cursor_id, P2: key_register, P3: jump_if_not_found, P4: unused, P5: unused
  OP_SeekGE = 20,
  OP_SeekGT = 21,
  OP_SeekLE = 22,
  OP_SeekLT = 23,
  OP_SeekEQ = 24,

  // Data operations
  // P1: cursor_id, P2: column_index, P3: dest_register, P4: unused, P5: unused
  OP_Column = 30,


  // P1: first_register, P2: register_count, P3: dest_register, P4: unused, P5: unused
  OP_MakeRecord = 32,

  // P1: cursor_id, P2: key_register, P3: record_register, P4: unused, P5: unused
  OP_Insert = 34,

  // P1: cursor_id, P2: unused, P3: unused, P4: unused, P5: unused
  OP_Delete = 35,

  // P1: cursor_id, P2: record_register, P3: unused, P4: unused, P5: unused
  OP_Update = 36,

  // Register operations
  // P1: dest_register, P2: integer_value, P3: unused, P4: unused, P5: unused
  OP_Integer = 40,

  // P1: dest_register, P2: string_size, P3: unused, P4: string_pointer, P5: unused
  OP_String = 41,

  // P1: src_register, P2: dest_register, P3: unused, P4: unused, P5: unused
  OP_Copy = 43,

  // P1: src_register, P2: dest_register, P3: unused, P4: unused, P5: unused
  OP_Move = 44,

  // Comparison operations
  // P1: register_a, P2: unused, P3: register_b, P4: unused, P5: unused
  OP_Compare = 55,

  // P1: jump_if_lt, P2: jump_if_eq, P3: jump_if_gt, P4: unused, P5: unused
  OP_Jump = 56,

  // P1: register_a, P2: jump_target, P3: register_b, P4: unused, P5: unused
  OP_Eq = 57,
  OP_Ne = 58,
  OP_Lt = 59,
  OP_Le = 60,
  OP_Gt = 61,
  OP_Ge = 62,

  // Results
  // P1: first_register, P2: register_count, P3: unused, P4: unused, P5: unused
  OP_ResultRow = 70,

  // Schema operations
  // P1: unused, P2: unused, P3: unused, P4: TableSchema*, P5: unused
  OP_CreateTable = 80,

  // P1: unused, P2: unused, P3: unused, P4: table_name, P5: unused
  OP_DropTable = 81,

  // P1: column_index, P2: unused, P3: unused, P4: table_name, P5: unused
  OP_CreateIndex = 82,

  // P1: column_index, P2: unused, P3: unused, P4: table_name, P5: unused
  OP_DropIndex = 83,

  // Transactions
  // P1: unused, P2: unused, P3: unused, P4: unused, P5: unused
  OP_Begin = 90,
  OP_Commit = 91,
  OP_Rollback = 92,

  // Aggregation
  // P1: unused, P2: unused, P3: unused, P4: function_name, P5: unused
  OP_AggReset = 93,

  // P1: value_register (or -1 for COUNT), P2: unused, P3: unused, P4: unused, P5: unused
  OP_AggStep = 94,

  // P1: dest_register, P2: unused, P3: unused, P4: unused, P5: unused
  OP_AggFinal = 95,

  // Sorting and output
  // P1: column_index, P2: direction (0=ASC, 1=DESC), P3: unused, P4: unused, P5: unused
  Op_Sort = 96,

  // P1: unused, P2: unused, P3: unused, P4: unused, P5: unused
  Op_Flush = 97,
};

struct VMInstruction {
  OpCode opcode;
  int32_t p1;
  int32_t p2;
  int32_t p3;
  void *p4;
  uint8_t p5;
};




#define REGISTER_COUNT 20

// VM execution




enum VM_RESULT  {
  OK,
  ABORT,
  ERR
};




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
    void * data;
};

VM_RESULT vm_execute(std::vector<VMInstruction> &instructions);
std::queue<VmEvent>& vm_events();
