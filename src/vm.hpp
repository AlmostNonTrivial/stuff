#pragma once
#include "arena.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
typedef void (*ResultCallback)(void *result, size_t result_size);
inline const char *datatype_to_string(DataType type);
extern bool _debug;



// VM value - uses arena allocation for data
struct VMValue {
  DataType type;
  union {
    uint32_t u32;
    uint64_t u64;
    int32_t i32;
    int64_t i64;
    uint8_t *data; // Points to arena-allocated memory
  };

  // Helper to get size based on type
  static uint32_t get_size(DataType t) { return static_cast<uint32_t>(t); }
};

// Forward declaration for schema
struct TableSchema;

enum OpCode : uint32_t {
  // Control flow
  OP_Trace = 0,
  OP_Goto = 1,
  OP_Halt = 2,

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

  // Data operations
  OP_Column = 30,
  OP_MakeRecord = 32,
  OP_Insert = 34,
  OP_Delete = 35,
  OP_Update = 36,

  // Register operations
  OP_Integer = 40,
  OP_String = 41,
  OP_Copy = 43,
  OP_Move = 44,

  // Comparison operations
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

  // Aggregation
  OP_AggReset = 93,
  OP_AggStep = 94,
  OP_AggFinal = 95,

  // Sorting and output
  Op_Flush = 97,
  Op_OpenMemTree = 98

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
// Opcode Descriptors - Each opcode knows how to create, access, and print
// itself
// ============================================================================

namespace Opcodes {

// Helper for printing string values
inline void print_string_value(const void *data, DataType type) {
  if (!data) {
    printf("NULL");
    return;
  }

  printf("\"");
  const char *str = (const char *)data;
  for (size_t i = 0; i < (size_t)type && str[i]; i++) {
    if (str[i] >= 32 && str[i] < 127) {
      printf("%c", str[i]);
    } else {
      printf("\\x%02x", (unsigned char)str[i]);
    }
  }
  printf("\"");
}

// Control Flow Operations
struct Trace {
  static VMInstruction create(const char *message) {
    return {OP_Trace, 0, 0, 0, (void *)message, 0};
  }
  static const char *message(const VMInstruction &inst) {
    return (const char *)inst.p4;
  }
  static void print(const VMInstruction &inst) {
    printf("msg=\"%s\"", message(inst) ? message(inst) : "");
  }
};

struct Goto {
  static VMInstruction create(int32_t target) {
    return {OP_Goto, 0, target, 0, nullptr, 0};
  }
  static VMInstruction create_label(const char *label) {
    return {OP_Goto, 0, -1, 0, (void *)label, 0};
  }
  static int32_t target(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("-> %d", target(inst));
  }
};

struct Halt {
  static VMInstruction create(int32_t exit_code = 0) {
    return {OP_Halt, exit_code, 0, 0, nullptr, 0};
  }
  static int32_t exit_code(const VMInstruction &inst) { return inst.p1; }
  static void print(const VMInstruction &inst) {
    printf("exit_code=%d", exit_code(inst));
  }
};

// Cursor Operations
struct OpenRead {
  static VMInstruction create(int32_t cursor_id, const char *table_name,
                              int32_t index_col = 0) {
    return {OP_OpenRead, 0, cursor_id, index_col, (void *)table_name, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p2; }
  static int32_t index_col(const VMInstruction &inst) { return inst.p3; }
  static const char *table_name(const VMInstruction &inst) {
    return (const char *)inst.p4;
  }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d table=\"%s\"", cursor_id(inst),
           table_name(inst) ? table_name(inst) : "?");
    if (index_col(inst) != 0) {
      printf(" index_col=%d", index_col(inst));
    }
  }
};

struct OpenWrite {
  static VMInstruction create(int32_t cursor_id, const char *table_name,
                              int32_t index_col = 0) {
    return {OP_OpenWrite, 0, cursor_id, index_col, (void *)table_name, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p2; }
  static int32_t index_col(const VMInstruction &inst) { return inst.p3; }
  static const char *table_name(const VMInstruction &inst) {
    return (const char *)inst.p4;
  }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d table=\"%s\"", cursor_id(inst),
           table_name(inst) ? table_name(inst) : "?");
    if (index_col(inst) != 0) {
      printf(" index_col=%d", index_col(inst));
    }
  }
};

struct Close {
  static VMInstruction create(int32_t cursor_id) {
    return {OP_Close, cursor_id, 0, 0, nullptr, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d", cursor_id(inst));
  }
};

struct Rewind {
  static VMInstruction create(int32_t cursor_id, int32_t jump_if_empty = -1) {
    return {OP_Rewind, cursor_id, jump_if_empty, 0, nullptr, 0};
  }
  static VMInstruction create_label(int32_t cursor_id, const char *label) {
    return {OP_Rewind, cursor_id, -1, 0, (void *)label, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t jump_if_empty(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d", cursor_id(inst));
    if (jump_if_empty(inst) >= 0)
      printf(" empty->%d", jump_if_empty(inst));
  }
};

struct Next {
  static VMInstruction create(int32_t cursor_id, int32_t jump_if_done = -1) {
    return {OP_Next, cursor_id, jump_if_done, 0, nullptr, 0};
  }
  static VMInstruction create_label(int32_t cursor_id, const char *label) {
    return {OP_Next, cursor_id, -1, 0, (void *)label, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t jump_if_done(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d", cursor_id(inst));
    if (jump_if_done(inst) >= 0)
      printf(" done->%d", jump_if_done(inst));
  }
};

struct Prev {
  static VMInstruction create(int32_t cursor_id, int32_t jump_if_done = -1) {
    return {OP_Prev, cursor_id, jump_if_done, 0, nullptr, 0};
  }
  static VMInstruction create_label(int32_t cursor_id, const char *label) {
    return {OP_Prev, cursor_id, -1, 0, (void *)label, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t jump_if_done(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d", cursor_id(inst));
    if (jump_if_done(inst) >= 0)
      printf(" done->%d", jump_if_done(inst));
  }
};

struct First {
  static VMInstruction create(int32_t cursor_id, int32_t jump_if_empty = -1) {
    return {OP_First, cursor_id, jump_if_empty, 0, nullptr, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t jump_if_empty(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d", cursor_id(inst));
    if (jump_if_empty(inst) >= 0)
      printf(" empty->%d", jump_if_empty(inst));
  }
};

struct Last {
  static VMInstruction create(int32_t cursor_id, int32_t jump_if_empty = -1) {
    return {OP_Last, cursor_id, jump_if_empty, 0, nullptr, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t jump_if_empty(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d", cursor_id(inst));
    if (jump_if_empty(inst) >= 0)
      printf(" empty->%d", jump_if_empty(inst));
  }
};

// Seek Operations
struct SeekGE {
  static VMInstruction create(int32_t cursor_id, int32_t key_reg,
                              int32_t jump_if_not_found = -1) {
    return {OP_SeekGE, cursor_id, key_reg, jump_if_not_found, nullptr, 0};
  }
  static VMInstruction create_label(int32_t cursor_id, int32_t key_reg,
                                    const char *label) {
    return {OP_SeekGE, cursor_id, key_reg, -1, (void *)label, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t key_reg(const VMInstruction &inst) { return inst.p2; }
  static int32_t jump_if_not_found(const VMInstruction &inst) {
    return inst.p3;
  }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d key=r%d", cursor_id(inst), key_reg(inst));
    if (jump_if_not_found(inst) >= 0)
      printf(" notfound->%d", jump_if_not_found(inst));
  }
};

struct SeekGT {
  static VMInstruction create(int32_t cursor_id, int32_t key_reg,
                              int32_t jump_if_not_found = -1) {
    return {OP_SeekGT, cursor_id, key_reg, jump_if_not_found, nullptr, 0};
  }
  static VMInstruction create_label(int32_t cursor_id, int32_t key_reg,
                                    const char *label) {
    return {OP_SeekGT, cursor_id, key_reg, -1, (void *)label, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t key_reg(const VMInstruction &inst) { return inst.p2; }
  static int32_t jump_if_not_found(const VMInstruction &inst) {
    return inst.p3;
  }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d key=r%d", cursor_id(inst), key_reg(inst));
    if (jump_if_not_found(inst) >= 0)
      printf(" notfound->%d", jump_if_not_found(inst));
  }
};

struct SeekLE {
  static VMInstruction create(int32_t cursor_id, int32_t key_reg,
                              int32_t jump_if_not_found = -1) {
    return {OP_SeekLE, cursor_id, key_reg, jump_if_not_found, nullptr, 0};
  }
  static VMInstruction create_label(int32_t cursor_id, int32_t key_reg,
                                    const char *label) {
    return {OP_SeekLE, cursor_id, key_reg, -1, (void *)label, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t key_reg(const VMInstruction &inst) { return inst.p2; }
  static int32_t jump_if_not_found(const VMInstruction &inst) {
    return inst.p3;
  }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d key=r%d", cursor_id(inst), key_reg(inst));
    if (jump_if_not_found(inst) >= 0)
      printf(" notfound->%d", jump_if_not_found(inst));
  }
};

struct SeekLT {
  static VMInstruction create(int32_t cursor_id, int32_t key_reg,
                              int32_t jump_if_not_found = -1) {
    return {OP_SeekLT, cursor_id, key_reg, jump_if_not_found, nullptr, 0};
  }
  static VMInstruction create_label(int32_t cursor_id, int32_t key_reg,
                                    const char *label) {
    return {OP_SeekLT, cursor_id, key_reg, -1, (void *)label, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t key_reg(const VMInstruction &inst) { return inst.p2; }
  static int32_t jump_if_not_found(const VMInstruction &inst) {
    return inst.p3;
  }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d key=r%d", cursor_id(inst), key_reg(inst));
    if (jump_if_not_found(inst) >= 0)
      printf(" notfound->%d", jump_if_not_found(inst));
  }
};

struct SeekEQ {
  static VMInstruction create(int32_t cursor_id, int32_t key_reg,
                              int32_t jump_if_not_found = -1) {
    return {OP_SeekEQ, cursor_id, key_reg, jump_if_not_found, nullptr, 0};
  }
  static VMInstruction create_label(int32_t cursor_id, int32_t key_reg,
                                    const char *label) {
    return {OP_SeekEQ, cursor_id, key_reg, -1, (void *)label, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t key_reg(const VMInstruction &inst) { return inst.p2; }
  static int32_t jump_if_not_found(const VMInstruction &inst) {
    return inst.p3;
  }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d key=r%d", cursor_id(inst), key_reg(inst));
    if (jump_if_not_found(inst) >= 0)
      printf(" notfound->%d", jump_if_not_found(inst));
  }
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
  static void print(const VMInstruction &inst) {
    printf("cursor=%d col=%d -> r%d", cursor_id(inst), column_index(inst),
           dest_reg(inst));
  }
};

struct MakeRecord {
  static VMInstruction create(int32_t first_reg, int32_t reg_count,
                              int32_t dest_reg) {
    return {OP_MakeRecord, first_reg, reg_count, dest_reg, nullptr, 0};
  }
  static int32_t first_reg(const VMInstruction &inst) { return inst.p1; }
  static int32_t reg_count(const VMInstruction &inst) { return inst.p2; }
  static int32_t dest_reg(const VMInstruction &inst) { return inst.p3; }
  static void print(const VMInstruction &inst) {
    printf("r%d..r%d (%d regs) -> r%d", first_reg(inst),
           first_reg(inst) + reg_count(inst) - 1, reg_count(inst),
           dest_reg(inst));
  }
};

struct Insert {
  static VMInstruction create(int32_t cursor_id, int32_t key_reg,
                              int32_t record_reg) {
    return {OP_Insert, cursor_id, key_reg, record_reg, nullptr, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t key_reg(const VMInstruction &inst) { return inst.p2; }
  static int32_t record_reg(const VMInstruction &inst) { return inst.p3; }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d key=r%d record=r%d", cursor_id(inst), key_reg(inst),
           record_reg(inst));
  }
};

struct Delete {
  static VMInstruction create(int32_t cursor_id) {
    return {OP_Delete, cursor_id, 0, 0, nullptr, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d", cursor_id(inst));
  }
};

struct Update {
  static VMInstruction create(int32_t cursor_id, int32_t record_reg) {
    return {OP_Update, cursor_id, record_reg, 0, nullptr, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t record_reg(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d record=r%d", cursor_id(inst), record_reg(inst));
  }
};

// Register Operations
struct Integer {
  static VMInstruction create(int32_t dest_reg, int32_t value) {
    return {OP_Integer, dest_reg, value, 0, nullptr, 0};
  }
  static int32_t dest_reg(const VMInstruction &inst) { return inst.p1; }
  static int32_t value(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("r%d = %d", dest_reg(inst), value(inst));
  }
};

struct String {
  static VMInstruction create(int32_t dest_reg, int32_t size, const void *str) {
    return {OP_String, dest_reg, size, 0, (void *)str, 0};
  }
  static int32_t dest_reg(const VMInstruction &inst) { return inst.p1; }
  static int32_t size(const VMInstruction &inst) { return inst.p2; }
  static const void *str(const VMInstruction &inst) { return inst.p4; }
  static void print(const VMInstruction &inst) {
    printf("r%d = ", dest_reg(inst));
    print_string_value(str(inst), (DataType)size(inst));
    printf(" (type=%s)", datatype_to_string((DataType)size(inst)));
  }
};

struct Copy {
  static VMInstruction create(int32_t src_reg, int32_t dest_reg) {
    return {OP_Copy, src_reg, dest_reg, 0, nullptr, 0};
  }
  static int32_t src_reg(const VMInstruction &inst) { return inst.p1; }
  static int32_t dest_reg(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("r%d -> r%d", src_reg(inst), dest_reg(inst));
  }
};

struct Move {
  static VMInstruction create(int32_t src_reg, int32_t dest_reg) {
    return {OP_Move, src_reg, dest_reg, 0, nullptr, 0};
  }
  static int32_t src_reg(const VMInstruction &inst) { return inst.p1; }
  static int32_t dest_reg(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("r%d => r%d", src_reg(inst), dest_reg(inst));
  }
};

// Comparison Operations
struct Compare {
  static VMInstruction create(int32_t reg_a, int32_t reg_b) {
    return {OP_Compare, reg_a, 0, reg_b, nullptr, 0};
  }
  static int32_t reg_a(const VMInstruction &inst) { return inst.p1; }
  static int32_t reg_b(const VMInstruction &inst) { return inst.p3; }
  static void print(const VMInstruction &inst) {
    printf("r%d cmp r%d", reg_a(inst), reg_b(inst));
  }
};

struct Jump {
  static VMInstruction create(int32_t jump_lt, int32_t jump_eq,
                              int32_t jump_gt) {
    return {OP_Jump, jump_lt, jump_eq, jump_gt, nullptr, 0};
  }
  static int32_t jump_lt(const VMInstruction &inst) { return inst.p1; }
  static int32_t jump_eq(const VMInstruction &inst) { return inst.p2; }
  static int32_t jump_gt(const VMInstruction &inst) { return inst.p3; }
  static void print(const VMInstruction &inst) {
    printf("lt->%d eq->%d gt->%d", jump_lt(inst), jump_eq(inst), jump_gt(inst));
  }
};

struct Eq {
  static VMInstruction create(int32_t reg_a, int32_t reg_b,
                              int32_t jump_target = -1) {
    return {OP_Eq, reg_a, jump_target, reg_b, nullptr, 0};
  }
  static VMInstruction create_label(int32_t reg_a, int32_t reg_b,
                                    const char *label) {
    return {OP_Eq, reg_a, -1, reg_b, (void *)label, 0};
  }
  static int32_t reg_a(const VMInstruction &inst) { return inst.p1; }
  static int32_t reg_b(const VMInstruction &inst) { return inst.p3; }
  static int32_t jump_target(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("r%d == r%d", reg_a(inst), reg_b(inst));
    if (jump_target(inst) >= 0)
      printf(" true->%d", jump_target(inst));
  }
};

struct Ne {
  static VMInstruction create(int32_t reg_a, int32_t reg_b,
                              int32_t jump_target = -1) {
    return {OP_Ne, reg_a, jump_target, reg_b, nullptr, 0};
  }
  static VMInstruction create_label(int32_t reg_a, int32_t reg_b,
                                    const char *label) {
    return {OP_Ne, reg_a, -1, reg_b, (void *)label, 0};
  }
  static int32_t reg_a(const VMInstruction &inst) { return inst.p1; }
  static int32_t reg_b(const VMInstruction &inst) { return inst.p3; }
  static int32_t jump_target(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("r%d != r%d", reg_a(inst), reg_b(inst));
    if (jump_target(inst) >= 0)
      printf(" true->%d", jump_target(inst));
  }
};

struct Lt {
  static VMInstruction create(int32_t reg_a, int32_t reg_b,
                              int32_t jump_target = -1) {
    return {OP_Lt, reg_a, jump_target, reg_b, nullptr, 0};
  }
  static VMInstruction create_label(int32_t reg_a, int32_t reg_b,
                                    const char *label) {
    return {OP_Lt, reg_a, -1, reg_b, (void *)label, 0};
  }
  static int32_t reg_a(const VMInstruction &inst) { return inst.p1; }
  static int32_t reg_b(const VMInstruction &inst) { return inst.p3; }
  static int32_t jump_target(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("r%d < r%d", reg_a(inst), reg_b(inst));
    if (jump_target(inst) >= 0)
      printf(" true->%d", jump_target(inst));
  }
};

struct Le {
  static VMInstruction create(int32_t reg_a, int32_t reg_b,
                              int32_t jump_target = -1) {
    return {OP_Le, reg_a, jump_target, reg_b, nullptr, 0};
  }
  static VMInstruction create_label(int32_t reg_a, int32_t reg_b,
                                    const char *label) {
    return {OP_Le, reg_a, -1, reg_b, (void *)label, 0};
  }
  static int32_t reg_a(const VMInstruction &inst) { return inst.p1; }
  static int32_t reg_b(const VMInstruction &inst) { return inst.p3; }
  static int32_t jump_target(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("r%d <= r%d", reg_a(inst), reg_b(inst));
    if (jump_target(inst) >= 0)
      printf(" true->%d", jump_target(inst));
  }
};

struct Gt {
  static VMInstruction create(int32_t reg_a, int32_t reg_b,
                              int32_t jump_target = -1) {
    return {OP_Gt, reg_a, jump_target, reg_b, nullptr, 0};
  }
  static VMInstruction create_label(int32_t reg_a, int32_t reg_b,
                                    const char *label) {
    return {OP_Gt, reg_a, -1, reg_b, (void *)label, 0};
  }
  static int32_t reg_a(const VMInstruction &inst) { return inst.p1; }
  static int32_t reg_b(const VMInstruction &inst) { return inst.p3; }
  static int32_t jump_target(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("r%d > r%d", reg_a(inst), reg_b(inst));
    if (jump_target(inst) >= 0)
      printf(" true->%d", jump_target(inst));
  }
};

struct Ge {
  static VMInstruction create(int32_t reg_a, int32_t reg_b,
                              int32_t jump_target = -1) {
    return {OP_Ge, reg_a, jump_target, reg_b, nullptr, 0};
  }
  static VMInstruction create_label(int32_t reg_a, int32_t reg_b,
                                    const char *label) {
    return {OP_Ge, reg_a, -1, reg_b, (void *)label, 0};
  }
  static int32_t reg_a(const VMInstruction &inst) { return inst.p1; }
  static int32_t reg_b(const VMInstruction &inst) { return inst.p3; }
  static int32_t jump_target(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("r%d >= r%d", reg_a(inst), reg_b(inst));
    if (jump_target(inst) >= 0)
      printf(" true->%d", jump_target(inst));
  }
};

// Results
struct ResultRow {
  static VMInstruction create(int32_t first_reg, int32_t reg_count) {
    return {OP_ResultRow, first_reg, reg_count, 0, nullptr, 0};
  }
  static int32_t first_reg(const VMInstruction &inst) { return inst.p1; }
  static int32_t reg_count(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("r%d..r%d (%d cols)", first_reg(inst),
           first_reg(inst) + reg_count(inst) - 1, reg_count(inst));
  }
};

// Schema Operations
struct CreateTable {
  static VMInstruction create(TableSchema *schema) {
    return {OP_CreateTable, 0, 0, 0, schema, 0};
  }
  static TableSchema *schema(const VMInstruction &inst) {
    return (TableSchema *)inst.p4;
  }
  static void print(const VMInstruction &inst) { printf("schema=%p", inst.p4); }
};

struct DropTable {
  static VMInstruction create(const char *table_name) {
    return {OP_DropTable, 0, 0, 0, (void *)table_name, 0};
  }
  static const char *table_name(const VMInstruction &inst) {
    return (const char *)inst.p4;
  }
  static void print(const VMInstruction &inst) {
    printf("table=\"%s\"", table_name(inst) ? table_name(inst) : "?");
  }
};

struct CreateIndex {
  static VMInstruction create(int32_t column_index, const char *table_name) {
    return {OP_CreateIndex, column_index, 0, 0, (void *)table_name, 0};
  }
  static int32_t column_index(const VMInstruction &inst) { return inst.p1; }
  static const char *table_name(const VMInstruction &inst) {
    return (const char *)inst.p4;
  }
  static void print(const VMInstruction &inst) {
    printf("col=%d table=\"%s\"", column_index(inst),
           table_name(inst) ? table_name(inst) : "?");
  }
};

struct DropIndex {
  static VMInstruction create(int32_t column_index, const char *table_name) {
    return {OP_DropIndex, column_index, 0, 0, (void *)table_name, 0};
  }
  static int32_t column_index(const VMInstruction &inst) { return inst.p1; }
  static const char *table_name(const VMInstruction &inst) {
    return (const char *)inst.p4;
  }
  static void print(const VMInstruction &inst) {
    printf("col=%d table=\"%s\"", column_index(inst),
           table_name(inst) ? table_name(inst) : "?");
  }
};

// Transactions
struct Begin {
  static VMInstruction create() { return {OP_Begin, 0, 0, 0, nullptr, 0}; }
  static void print(const VMInstruction &inst) { /* No parameters */ }
};

struct Commit {
  static VMInstruction create() { return {OP_Commit, 0, 0, 0, nullptr, 0}; }
  static void print(const VMInstruction &inst) { /* No parameters */ }
};

struct Rollback {
  static VMInstruction create() { return {OP_Rollback, 0, 0, 0, nullptr, 0}; }
  static void print(const VMInstruction &inst) { /* No parameters */ }
};

// Aggregation
struct AggReset {
  static VMInstruction create(const char *function_name) {
    return {OP_AggReset, 0, 0, 0, (void *)function_name, 0};
  }
  static const char *function_name(const VMInstruction &inst) {
    return (const char *)inst.p4;
  }
  static void print(const VMInstruction &inst) {
    printf("func=\"%s\"", function_name(inst) ? function_name(inst) : "?");
  }
};

struct AggStep {
  static VMInstruction create(int32_t value_reg = -1) {
    return {OP_AggStep, value_reg, 0, 0, nullptr, 0};
  }
  static int32_t value_reg(const VMInstruction &inst) { return inst.p1; }
  static void print(const VMInstruction &inst) {
    if (value_reg(inst) >= 0)
      printf("value=r%d", value_reg(inst));
    else
      printf("COUNT(*)");
  }
};

struct AggFinal {
  static VMInstruction create(int32_t dest_reg) {
    return {OP_AggFinal, dest_reg, 0, 0, nullptr, 0};
  }
  static int32_t dest_reg(const VMInstruction &inst) { return inst.p1; }
  static void print(const VMInstruction &inst) {
    printf("-> r%d", dest_reg(inst));
  }
};


struct Flush {
  static VMInstruction create() { return {Op_Flush, 0, 0, 0, nullptr, 0}; }
  static void print(const VMInstruction &inst) { /* No parameters */ }
};

struct OpenMemTree {
  static VMInstruction create(int32_t cursor_id, DataType key_type,
                              int32_t record_size) {
    return {Op_OpenMemTree, cursor_id, (int32_t)key_type, record_size,
            nullptr, 0};
  }

  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static DataType key_type(const VMInstruction &inst) {
    return (DataType)inst.p2;
  }
  static int32_t record_size(const VMInstruction &inst) { return inst.p3; }

  static void print(const VMInstruction &inst) {
    printf("cursor=%d key_type=%s record_size=%d",
           cursor_id(inst),
           datatype_to_string(key_type(inst)),
           record_size(inst));
  }
};



} // namespace Opcodes

// ============================================================================
// Compatibility layer - old-style make_* functions redirect to new descriptors
// ============================================================================

// Control flow
inline VMInstruction make_trace(const char *message) {
  return Opcodes::Trace::create(message);
}
inline VMInstruction make_goto(int32_t target) {
  return Opcodes::Goto::create(target);
}
inline VMInstruction make_halt(int32_t exit_code = 0) {
  return Opcodes::Halt::create(exit_code);
}

// Cursor operations
inline VMInstruction make_open_read(int32_t cursor_id, const char *table_name,
                                    int32_t index_col = 0) {
  return Opcodes::OpenRead::create(cursor_id, table_name, index_col);
}
inline VMInstruction make_open_write(int32_t cursor_id, const char *table_name,
                                     int32_t index_col = 0) {
  return Opcodes::OpenWrite::create(cursor_id, table_name, index_col);
}
inline VMInstruction make_close(int32_t cursor_id) {
  return Opcodes::Close::create(cursor_id);
}
inline VMInstruction make_rewind(int32_t cursor_id,
                                 int32_t jump_if_empty = -1) {
  return Opcodes::Rewind::create(cursor_id, jump_if_empty);
}
inline VMInstruction make_next(int32_t cursor_id, int32_t jump_if_done = -1) {
  return Opcodes::Next::create(cursor_id, jump_if_done);
}
inline VMInstruction make_prev(int32_t cursor_id, int32_t jump_if_done = -1) {
  return Opcodes::Prev::create(cursor_id, jump_if_done);
}
inline VMInstruction make_first(int32_t cursor_id, int32_t jump_if_empty = -1) {
  return Opcodes::First::create(cursor_id, jump_if_empty);
}
inline VMInstruction make_last(int32_t cursor_id, int32_t jump_if_empty = -1) {
  return Opcodes::Last::create(cursor_id, jump_if_empty);
}

// Seek operations
inline VMInstruction make_seek_ge(int32_t cursor_id, int32_t key_reg,
                                  int32_t jump_if_not_found = -1) {
  return Opcodes::SeekGE::create(cursor_id, key_reg, jump_if_not_found);
}
inline VMInstruction make_seek_gt(int32_t cursor_id, int32_t key_reg,
                                  int32_t jump_if_not_found = -1) {
  return Opcodes::SeekGT::create(cursor_id, key_reg, jump_if_not_found);
}
inline VMInstruction make_seek_le(int32_t cursor_id, int32_t key_reg,
                                  int32_t jump_if_not_found = -1) {
  return Opcodes::SeekLE::create(cursor_id, key_reg, jump_if_not_found);
}
inline VMInstruction make_seek_lt(int32_t cursor_id, int32_t key_reg,
                                  int32_t jump_if_not_found = -1) {
  return Opcodes::SeekLT::create(cursor_id, key_reg, jump_if_not_found);
}
inline VMInstruction make_seek_eq(int32_t cursor_id, int32_t key_reg,
                                  int32_t jump_if_not_found = -1) {
  return Opcodes::SeekEQ::create(cursor_id, key_reg, jump_if_not_found);
}

// Data operations
inline VMInstruction make_column(int32_t cursor_id, int32_t column_index,
                                 int32_t dest_reg) {
  return Opcodes::Column::create(cursor_id, column_index, dest_reg);
}
inline VMInstruction make_key(int32_t cursor_id, int32_t dest_reg) {
  return Opcodes::Column::create(cursor_id, 0, dest_reg);
}
inline VMInstruction make_record(int32_t first_reg /* of record, not key */,
                                 int32_t reg_count, int32_t dest_reg) {
  return Opcodes::MakeRecord::create(first_reg, reg_count, dest_reg);
}
inline VMInstruction make_insert(int32_t cursor_id, int32_t key_reg,
                                 int32_t record_reg) {
  return Opcodes::Insert::create(cursor_id, key_reg, record_reg);
}
inline VMInstruction make_delete(int32_t cursor_id) {
  return Opcodes::Delete::create(cursor_id);
}
inline VMInstruction make_update(int32_t cursor_id, int32_t record_reg) {
  return Opcodes::Update::create(cursor_id, record_reg);
}

// Register operations
inline VMInstruction make_integer(int32_t dest_reg, int32_t value) {
  return Opcodes::Integer::create(dest_reg, value);
}
inline VMInstruction make_string(int32_t dest_reg, int32_t size,
                                 const void *str) {
  return Opcodes::String::create(dest_reg, size, str);
}
inline VMInstruction make_copy(int32_t src_reg, int32_t dest_reg) {
  return Opcodes::Copy::create(src_reg, dest_reg);
}
inline VMInstruction make_move(int32_t src_reg, int32_t dest_reg) {
  return Opcodes::Move::create(src_reg, dest_reg);
}

// Comparison operations
inline VMInstruction make_compare(int32_t reg_a, int32_t reg_b) {
  return Opcodes::Compare::create(reg_a, reg_b);
}
inline VMInstruction make_jump(int32_t jump_lt, int32_t jump_eq,
                               int32_t jump_gt) {
  return Opcodes::Jump::create(jump_lt, jump_eq, jump_gt);
}
inline VMInstruction make_eq(int32_t reg_a, int32_t reg_b,
                             int32_t jump_target = -1) {
  return Opcodes::Eq::create(reg_a, reg_b, jump_target);
}
inline VMInstruction make_ne(int32_t reg_a, int32_t reg_b,
                             int32_t jump_target = -1) {
  return Opcodes::Ne::create(reg_a, reg_b, jump_target);
}
inline VMInstruction make_lt(int32_t reg_a, int32_t reg_b,
                             int32_t jump_target = -1) {
  return Opcodes::Lt::create(reg_a, reg_b, jump_target);
}
inline VMInstruction make_le(int32_t reg_a, int32_t reg_b,
                             int32_t jump_target = -1) {
  return Opcodes::Le::create(reg_a, reg_b, jump_target);
}
inline VMInstruction make_gt(int32_t reg_a, int32_t reg_b,
                             int32_t jump_target = -1) {
  return Opcodes::Gt::create(reg_a, reg_b, jump_target);
}
inline VMInstruction make_ge(int32_t reg_a, int32_t reg_b,
                             int32_t jump_target = -1) {
  return Opcodes::Ge::create(reg_a, reg_b, jump_target);
}

// Results
inline VMInstruction make_result_row(int32_t first_reg, int32_t reg_count) {
  return Opcodes::ResultRow::create(first_reg, reg_count);
}

// Schema operations
inline VMInstruction make_create_table(TableSchema *schema) {
  return Opcodes::CreateTable::create(schema);
}
inline VMInstruction make_drop_table(const char *table_name) {
  return Opcodes::DropTable::create(table_name);
}
inline VMInstruction make_create_index(int32_t column_index,
                                       const char *table_name) {
  return Opcodes::CreateIndex::create(column_index, table_name);
}
inline VMInstruction make_drop_index(int32_t column_index,
                                     const char *table_name) {
  return Opcodes::DropIndex::create(column_index, table_name);
}

// Transactions
inline VMInstruction make_begin() { return Opcodes::Begin::create(); }
inline VMInstruction make_commit() { return Opcodes::Commit::create(); }
inline VMInstruction make_rollback() { return Opcodes::Rollback::create(); }

// Aggregation
inline VMInstruction make_agg_reset(const char *function_name) {
  return Opcodes::AggReset::create(function_name);
}
inline VMInstruction make_agg_step(int32_t value_reg = -1) {
  return Opcodes::AggStep::create(value_reg);
}
inline VMInstruction make_agg_final(int32_t dest_reg) {
  return Opcodes::AggFinal::create(dest_reg);
}

// Sorting and output
inline VMInstruction make_open_memtree(int32_t cursor_id, DataType key_type,
                                       int32_t record_size) {
  return Opcodes::OpenMemTree::create(cursor_id, key_type, record_size);
}
inline VMInstruction make_flush() { return Opcodes::Flush::create(); }

// Label-based factories (for unresolved jumps)
inline VMInstruction make_goto_label(const char *label) {
  return Opcodes::Goto::create_label(label);
}
inline VMInstruction make_rewind_label(int32_t cursor_id, const char *label) {
  return Opcodes::Rewind::create_label(cursor_id, label);
}
inline VMInstruction make_next_label(int32_t cursor_id, const char *label) {
  return Opcodes::Next::create_label(cursor_id, label);
}
inline VMInstruction make_prev_label(int32_t cursor_id, const char *label) {
  return Opcodes::Prev::create_label(cursor_id, label);
}
inline VMInstruction make_seek_ge_label(int32_t cursor_id, int32_t key_reg,
                                        const char *label) {
  return Opcodes::SeekGE::create_label(cursor_id, key_reg, label);
}
inline VMInstruction make_seek_gt_label(int32_t cursor_id, int32_t key_reg,
                                        const char *label) {
  return Opcodes::SeekGT::create_label(cursor_id, key_reg, label);
}
inline VMInstruction make_seek_le_label(int32_t cursor_id, int32_t key_reg,
                                        const char *label) {
  return Opcodes::SeekLE::create_label(cursor_id, key_reg, label);
}
inline VMInstruction make_seek_lt_label(int32_t cursor_id, int32_t key_reg,
                                        const char *label) {
  return Opcodes::SeekLT::create_label(cursor_id, key_reg, label);
}
inline VMInstruction make_seek_eq_label(int32_t cursor_id, int32_t key_reg,
                                        const char *label) {
  return Opcodes::SeekEQ::create_label(cursor_id, key_reg, label);
}
inline VMInstruction make_eq_label(int32_t reg_a, int32_t reg_b,
                                   const char *label) {
  return Opcodes::Eq::create_label(reg_a, reg_b, label);
}
inline VMInstruction make_ne_label(int32_t reg_a, int32_t reg_b,
                                   const char *label) {
  return Opcodes::Ne::create_label(reg_a, reg_b, label);
}
inline VMInstruction make_lt_label(int32_t reg_a, int32_t reg_b,
                                   const char *label) {
  return Opcodes::Lt::create_label(reg_a, reg_b, label);
}
inline VMInstruction make_le_label(int32_t reg_a, int32_t reg_b,
                                   const char *label) {
  return Opcodes::Le::create_label(reg_a, reg_b, label);
}
inline VMInstruction make_gt_label(int32_t reg_a, int32_t reg_b,
                                   const char *label) {
  return Opcodes::Gt::create_label(reg_a, reg_b, label);
}
inline VMInstruction make_ge_label(int32_t reg_a, int32_t reg_b,
                                   const char *label) {
  return Opcodes::Ge::create_label(reg_a, reg_b, label);
}

// ============================================================================
// Debug Functions
// ============================================================================

inline const char *opcode_to_string(OpCode op) {
  switch (op) {
  case OP_Trace:
    return "Trace";
  case OP_Goto:
    return "Goto";
  case OP_Halt:
    return "Halt";
  case OP_OpenRead:
    return "OpenRead";
  case OP_OpenWrite:
    return "OpenWrite";
  case OP_Close:
    return "Close";
  case OP_Rewind:
    return "Rewind";
  case OP_Next:
    return "Next";
  case OP_Prev:
    return "Prev";
  case OP_First:
    return "First";
  case OP_Last:
    return "Last";
  case OP_SeekGE:
    return "SeekGE";
  case OP_SeekGT:
    return "SeekGT";
  case OP_SeekLE:
    return "SeekLE";
  case OP_SeekLT:
    return "SeekLT";
  case OP_SeekEQ:
    return "SeekEQ";
  case OP_Column:
    return "Column";
  case OP_MakeRecord:
    return "MakeRecord";
  case OP_Insert:
    return "Insert";
  case OP_Delete:
    return "Delete";
  case OP_Update:
    return "Update";
  case OP_Integer:
    return "Integer";
  case OP_String:
    return "String";
  case OP_Copy:
    return "Copy";
  case OP_Move:
    return "Move";
  case OP_Compare:
    return "Compare";
  case OP_Jump:
    return "Jump";
  case OP_Eq:
    return "Eq";
  case OP_Ne:
    return "Ne";
  case OP_Lt:
    return "Lt";
  case OP_Le:
    return "Le";
  case OP_Gt:
    return "Gt";
  case OP_Ge:
    return "Ge";
  case OP_ResultRow:
    return "ResultRow";
  case OP_CreateTable:
    return "CreateTable";
  case OP_DropTable:
    return "DropTable";
  case OP_CreateIndex:
    return "CreateIndex";
  case OP_DropIndex:
    return "DropIndex";
  case OP_Begin:
    return "Begin";
  case OP_Commit:
    return "Commit";
  case OP_Rollback:
    return "Rollback";
  case OP_AggReset:
    return "AggReset";
  case OP_AggStep:
    return "AggStep";
  case OP_AggFinal:
    return "AggFinal";
  case Op_OpenMemTree:
    return "OpenMemTree";
  case Op_Flush:
    return "Flush";
  default:
    return "Unknown";
  }
}

inline const char *datatype_to_string(DataType type) {
  switch (type) {
  case TYPE_NULL:
    return "NULL";
  case TYPE_UINT32:
    return "UINT32";
  case TYPE_UINT64:
    return "UINT64";
  case TYPE_VARCHAR32:
    return "VARCHAR32";
  case TYPE_VARCHAR256:
    return "VARCHAR256";
  default:
    return "UNKNOWN";
  }
}

inline void debug_print_instruction(const VMInstruction &inst, size_t index) {
  printf("[%3zu] %-12s ", index, opcode_to_string(inst.opcode));

  // Use the appropriate descriptor's print function
  switch (inst.opcode) {
  case OP_Trace:
    Opcodes::Trace::print(inst);
    break;
  case OP_Goto:
    Opcodes::Goto::print(inst);
    break;
  case OP_Halt:
    Opcodes::Halt::print(inst);
    break;
  case OP_OpenRead:
    Opcodes::OpenRead::print(inst);
    break;
  case OP_OpenWrite:
    Opcodes::OpenWrite::print(inst);
    break;
  case OP_Close:
    Opcodes::Close::print(inst);
    break;
  case OP_Rewind:
    Opcodes::Rewind::print(inst);
    break;
  case OP_Next:
    Opcodes::Next::print(inst);
    break;
  case OP_Prev:
    Opcodes::Prev::print(inst);
    break;
  case OP_First:
    Opcodes::First::print(inst);
    break;
  case OP_Last:
    Opcodes::Last::print(inst);
    break;
  case OP_SeekGE:
    Opcodes::SeekGE::print(inst);
    break;
  case OP_SeekGT:
    Opcodes::SeekGT::print(inst);
    break;
  case OP_SeekLE:
    Opcodes::SeekLE::print(inst);
    break;
  case OP_SeekLT:
    Opcodes::SeekLT::print(inst);
    break;
  case OP_SeekEQ:
    Opcodes::SeekEQ::print(inst);
    break;
  case OP_Column:
    Opcodes::Column::print(inst);
    break;
  case OP_MakeRecord:
    Opcodes::MakeRecord::print(inst);
    break;
  case OP_Insert:
    Opcodes::Insert::print(inst);
    break;
  case OP_Delete:
    Opcodes::Delete::print(inst);
    break;
  case OP_Update:
    Opcodes::Update::print(inst);
    break;
  case OP_Integer:
    Opcodes::Integer::print(inst);
    break;
  case OP_String:
    Opcodes::String::print(inst);
    break;
  case OP_Copy:
    Opcodes::Copy::print(inst);
    break;
  case OP_Move:
    Opcodes::Move::print(inst);
    break;
  case OP_Compare:
    Opcodes::Compare::print(inst);
    break;
  case OP_Jump:
    Opcodes::Jump::print(inst);
    break;
  case OP_Eq:
    Opcodes::Eq::print(inst);
    break;
  case OP_Ne:
    Opcodes::Ne::print(inst);
    break;
  case OP_Lt:
    Opcodes::Lt::print(inst);
    break;
  case OP_Le:
    Opcodes::Le::print(inst);
    break;
  case OP_Gt:
    Opcodes::Gt::print(inst);
    break;
  case OP_Ge:
    Opcodes::Ge::print(inst);
    break;
  case OP_ResultRow:
    Opcodes::ResultRow::print(inst);
    break;
  case OP_CreateTable:
    Opcodes::CreateTable::print(inst);
    break;
  case OP_DropTable:
    Opcodes::DropTable::print(inst);
    break;
  case OP_CreateIndex:
    Opcodes::CreateIndex::print(inst);
    break;
  case OP_DropIndex:
    Opcodes::DropIndex::print(inst);
    break;
  case OP_Begin:
    Opcodes::Begin::print(inst);
    break;
  case OP_Commit:
    Opcodes::Commit::print(inst);
    break;
  case OP_Rollback:
    Opcodes::Rollback::print(inst);
    break;
  case OP_AggReset:
    Opcodes::AggReset::print(inst);
    break;
  case OP_AggStep:
    Opcodes::AggStep::print(inst);
    break;
  case OP_AggFinal:
    Opcodes::AggFinal::print(inst);
    break;
case Op_OpenMemTree:
    Opcodes::OpenMemTree::print(inst);
  case Op_Flush:
    Opcodes::Flush::print(inst);
    break;

  default:
    printf("p1=%d p2=%d p3=%d p4=%p p5=%d", inst.p1, inst.p2, inst.p3, inst.p4,
           inst.p5);
  }

  // Check for unresolved labels
  if (inst.p4 && (inst.p2 == -1 || inst.p3 == -1)) {
    printf(" [unresolved label: \"%s\"]", (const char *)inst.p4);
  }

  printf("\n");
}

inline void
debug_print_program(const ArenaVector<VMInstruction, QueryArena> &program) {
  printf("\n=== VM Program (%zu instructions) ===\n", program.size());
  printf("Idx  Opcode       Parameters\n");
  printf("---  ------------ --------------------------------\n");

  for (size_t i = 0; i < program.size(); i++) {
    debug_print_instruction(program[i], i);
  }

  // Print jump targets for easier navigation
  printf("\n=== Jump Targets ===\n");
  for (size_t i = 0; i < program.size(); i++) {
    const auto &inst = program[i];
    bool is_target = false;

    // Check if this instruction is a jump target
    for (size_t j = 0; j < program.size(); j++) {
      const auto &check = program[j];
      if ((check.opcode == OP_Goto && check.p2 == (int)i) ||
          (check.opcode >= OP_Eq && check.opcode <= OP_Ge &&
           check.p2 == (int)i) ||
          (check.opcode == OP_Jump &&
           (check.p1 == (int)i || check.p2 == (int)i || check.p3 == (int)i)) ||
          ((check.opcode == OP_Next || check.opcode == OP_Prev) &&
           check.p2 == (int)i) ||
          ((check.opcode == OP_Rewind || check.opcode == OP_First ||
            check.opcode == OP_Last) &&
           check.p2 == (int)i) ||
          ((check.opcode >= OP_SeekGE && check.opcode <= OP_SeekEQ) &&
           check.p3 == (int)i)) {
        is_target = true;
        break;
      }
    }

    if (is_target) {
      printf("  Label_%zu: instruction [%zu] %s\n", i, i,
             opcode_to_string(inst.opcode));
    }
  }

  printf("\n");
}

// ============================================================================
// VM Runtime Definitions
// ============================================================================

#define REGISTER_COUNT 20

enum VM_RESULT { OK, ABORT, ERR };

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

// VM Functions
VM_RESULT vm_execute(ArenaVector<VMInstruction, QueryArena> &instructions);
ArenaQueue<VmEvent, QueryArena> &vm_events();
ArenaVector<ArenaVector<VMValue, QueryArena>, QueryArena> &vm_output_buffer();
