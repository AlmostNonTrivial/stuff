#pragma once
#include "btree.hpp"
#include "defs.hpp"
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// VM value - uses arena allocation for data
struct VMValue {
  DataType type;
  uint8_t *data; // Points to arena-allocated memory

  // Helper to get size based on type
  static uint32_t get_size(DataType t) { return static_cast<uint32_t>(t); }
};

enum OpCode : uint8_t {
  // Control flow
  OP_Trace = 0,
  OP_Goto = 1,
  OP_Halt = 2,
  OP_If = 3,
  OP_IfNot = 4,
  OP_IfNull = 5,
  OP_IfNotNull = 6,

  // Cursor operations
  OP_OpenRead = 10,
  OP_OpenWrite = 11,
  OP_Close = 12,
  OP_Rewind = 13,
  OP_Next = 14,
  OP_Prev = 15,
  OP_First = 16,
  OP_Last = 17,

  // Seek operations
  OP_SeekGE = 20,
  OP_SeekGT = 21,
  OP_SeekLE = 22,
  OP_SeekLT = 23,
  OP_SeekEQ = 24,
  OP_Found = 25,
  OP_NotFound = 26,

  // Data operations
  OP_Column = 30,
  OP_Key = 31,
  OP_MakeRecord = 32,
  // OP_MakeKey = 33,
  OP_Insert = 34,
  OP_Delete = 35,
  OP_Update = 36,
  // OP_NewRowid = 37,

  // Register operations
  OP_Integer = 40,
  OP_String = 41,
  // OP_Null = 42,
  OP_Copy = 43,
  OP_Move = 44,
  // OP_SCopy = 45, // Shallow copy

  // Comparison and arithmetic
  // OP_Add = 50,
  // OP_Subtract = 51,
  // OP_Multiply = 52,
  // OP_Divide = 53,
  // OP_Remainder = 54,
  OP_Compare = 55,
  OP_Jump = 56,
  OP_Eq = 57,
  OP_Ne = 58,
  OP_Lt = 59,
  OP_Le = 60,
  OP_Gt = 61,
  OP_Ge = 62,

  // Results
  OP_ResultRow = 70,

  // Schema operations
  OP_CreateTable = 80,
  OP_DropTable = 81,
  OP_CreateIndex = 82,
  OP_DropIndex = 83,

  // Transactions
  OP_Begin = 90,
  OP_Commit = 91,
  OP_Rollback = 92,


  OP_AggReset = 93,
  OP_AggStep = 94,
  OP_AggFinal = 95,

  // Sorting
  Op_Sort = 96,
  Op_Flush = 97,

OP_Analyize = 98,
};

struct VMInstruction {
  OpCode opcode;
  int32_t p1;
  int32_t p2;
  int32_t p3;
  void *p4;
  uint8_t p5;
};

struct ColumnInfo {
  char name[32];
  DataType type;
};

struct TableSchema {
  std::string table_name;
  uint32_t record_size;
  std::vector<ColumnInfo> columns;
  std::vector<uint32_t> column_offsets;

  DataType key_type() const { return columns[0].type; }

};

struct Index {
  BTree tree;
};

struct Table {
  TableSchema schema;
  BTree tree;
  std::unordered_map<uint32_t, Index> indexes;
};
#define REGISTER_COUNT 100

// VM execution
void vm_init();
void vm_reset();
bool vm_execute(std::vector<VMInstruction> &instructions);
bool vm_step();

Table& vm_get_table(const std::string&name);
std::unordered_map<uint32_t, Index>& vm_get_table_indexes(const std::string&name);
