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
  bool has_key_prefix; // true for SELECT results that include key
};
typedef void (*ResultCallback)(void *result, size_t result_size);
inline const char *datatype_to_string(DataType type);
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

enum ArithOp : uint8_t {
  ARITH_ADD = 0,
  ARITH_SUB = 1,
  ARITH_MUL = 2,
  ARITH_DIV = 3,
  ARITH_MOD = 4,
};

enum LogicOp : uint8_t {
  LOGIC_AND = 0,
  LOGIC_OR = 1,
  LOGIC_NOT = 2,
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
Queue<VmEvent, QueryArena> vm_events();
// Forward declaration for schema
struct TableSchema;

inline const char *arith_op_to_string(ArithOp op) {
  switch (op) {
  case ARITH_ADD:
    return "ADD";
  case ARITH_SUB:
    return "SUB";
  case ARITH_MUL:
    return "MUL";
  case ARITH_DIV:
    return "DIV";
  case ARITH_MOD:
    return "MOD";
  default:
    return "???";
  }
}
enum OpCode : uint32_t {
  // Control flow
  OP_Trace = 0,
  OP_Goto = 1,
  OP_Halt = 2,

  // Cursor operations
  OP_OpenRead = 10,
  OP_OpenWrite = 11,
  OP_Close = 12,
  OP_First = 13,
  OP_Last = 14,
  OP_Next = 15,
  OP_Prev = 16,

  // Unified Seek operation
  OP_Seek = 20,

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

  // Unified Comparison operation
  OP_Test = 60,
  OP_Arithmetic = 51,
  OP_Result = 71,
  OP_JumpIf = 52,
  OP_Logic = 53,
  OP_ResultRow = 54,

  // Schema operations
  OP_CreateTable = 80,
  OP_DropTable = 81,
  OP_CreateIndex = 82,
  OP_DropIndex = 83,

  // Transactions
  OP_Begin = 90,
  OP_Commit = 91,
  OP_Rollback = 92,

  // Memory tree
  OP_OpenMemTree = 98,
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
      printf("\\\\x%02x", (unsigned char)str[i]);
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

struct Arithmetic {
  static VMInstruction create(int32_t dest_reg, int32_t left_reg,
                              int32_t right_reg, ArithOp op) {
    return {OP_Arithmetic, dest_reg, left_reg, right_reg, nullptr, (uint8_t)op};
  }
  static int32_t dest_reg(const VMInstruction &inst) { return inst.p1; }
  static int32_t left_reg(const VMInstruction &inst) { return inst.p2; }
  static int32_t right_reg(const VMInstruction &inst) { return inst.p3; }
  static ArithOp op(const VMInstruction &inst) { return (ArithOp)inst.p5; }

  static void print(const VMInstruction &inst) {
    const char *op_str[] = {"+", "-", "*", "/", "%"};
    printf("r%d = r%d %s r%d", dest_reg(inst), left_reg(inst), op_str[op(inst)],
           right_reg(inst));
  }
};

// Cursor Operations
struct OpenRead {
  static VMInstruction create(int32_t cursor_id, const char *table_name,
                              int32_t index_col = 0) {
    return {OP_OpenRead, cursor_id, 0, index_col, (void *)table_name, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
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
    return {OP_OpenWrite, cursor_id, 0, index_col, (void *)table_name, 0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
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

struct Next {
  static VMInstruction create(int32_t cursor_id, int32_t jump_if_done = -1) {
    return {OP_Next, cursor_id, jump_if_done, 0, nullptr, 0};
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
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t jump_if_done(const VMInstruction &inst) { return inst.p2; }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d", cursor_id(inst));
    if (jump_if_done(inst) >= 0)
      printf(" done->%d", jump_if_done(inst));
  }
};

// Unified Seek operation
struct Seek {
  static VMInstruction create(int32_t cursor_id, int32_t key_reg,
                              int32_t jump_if_not, CompareOp op) {
    return {OP_Seek, cursor_id, key_reg, jump_if_not, nullptr, (uint8_t)op};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static int32_t key_reg(const VMInstruction &inst) { return inst.p2; }
  static int32_t jump_if_not(const VMInstruction &inst) { return inst.p3; }
  static CompareOp op(const VMInstruction &inst) { return (CompareOp)inst.p5; }
  static void print(const VMInstruction &inst) {
    const char *op_str[] = {"EQ", "NE", "LT", "LE", "GT", "GE"};
    printf("cursor=%d key=r%d op=%s", cursor_id(inst), key_reg(inst),
           op_str[op(inst)]);
    if (jump_if_not(inst) >= 0)
      printf(" notfound->%d", jump_if_not(inst));
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
struct Test {
  static VMInstruction create(int32_t dest_reg, int32_t left_reg,
                              int32_t right_reg, CompareOp op) {
    return {OP_Test, dest_reg, left_reg, right_reg, nullptr, (uint8_t)op};
  }
  static int32_t dest_reg(const VMInstruction &inst) { return inst.p1; }
  static int32_t left_reg(const VMInstruction &inst) { return inst.p2; }
  static int32_t right_reg(const VMInstruction &inst) { return inst.p3; }
  static CompareOp op(const VMInstruction &inst) { return (CompareOp)inst.p5; }

  static void print(const VMInstruction &inst) {
    const char *op_str[] = {"==", "!=", "<", "<=", ">", ">="};
    printf("r%d = (r%d %s r%d)", dest_reg(inst), left_reg(inst),
           op_str[op(inst)], right_reg(inst));
  }
};

truct JumpIf {
  static VMInstruction create(int32_t test_reg, int32_t jump_target, bool jump_on_true = true) {
    return {OP_JumpIf, test_reg, jump_target, 0, nullptr, (uint8_t)jump_on_true};
  }
  static int32_t test_reg(const VMInstruction& inst) { return inst.p1; }
  static int32_t jump_target(const VMInstruction& inst) { return inst.p2; }
  static bool jump_on_true(const VMInstruction& inst) { return inst.p5 != 0; }

  static void print(const VMInstruction& inst) {
    printf("if (r%d %s 0) goto %d",
           test_reg(inst),
           jump_on_true(inst) ? "!=" : "==",
           jump_target(inst));
  }
};

struct Logic {
  static VMInstruction create(int32_t dest_reg, int32_t left_reg,
                              int32_t right_reg, LogicOp op) {
    return {OP_Logic, dest_reg, left_reg, right_reg, nullptr, (uint8_t)op};
  }
  static VMInstruction create_not(int32_t dest_reg, int32_t src_reg) {
    return {OP_Logic, dest_reg, src_reg, 0, nullptr, (uint8_t)LOGIC_NOT};
  }
  static int32_t dest_reg(const VMInstruction& inst) { return inst.p1; }
  static int32_t left_reg(const VMInstruction& inst) { return inst.p2; }
  static int32_t right_reg(const VMInstruction& inst) { return inst.p3; }
  static LogicOp op(const VMInstruction& inst) { return (LogicOp)inst.p5; }

  static void print(const VMInstruction& inst) {
    const char* op_str[] = {"AND", "OR", "NOT"};
    if (op(inst) == LOGIC_NOT) {
      printf("r%d = NOT r%d", dest_reg(inst), left_reg(inst));
    } else {
      printf("r%d = r%d %s r%d",
             dest_reg(inst), left_reg(inst), op_str[op(inst)], right_reg(inst));
    }
  }
};

struct Result {
  static VMInstruction create(int32_t first_reg, int32_t reg_count) {
    return {OP_ResultRow, first_reg, reg_count, 0, nullptr, 0};
  }
  static int32_t first_reg(const VMInstruction& inst) { return inst.p1; }
  static int32_t reg_count(const VMInstruction& inst) { return inst.p2; }

  static void print(const VMInstruction& inst) {
    printf("output r%d..r%d (%d regs)",
           first_reg(inst),
           first_reg(inst) + reg_count(inst) - 1,
           reg_count(inst));
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

struct OpenMemTree {
  static VMInstruction create(int32_t cursor_id, DataType key_type,
                              int32_t record_size) {
    return {OP_OpenMemTree, cursor_id, (int32_t)key_type,
            record_size,    nullptr,   0};
  }
  static int32_t cursor_id(const VMInstruction &inst) { return inst.p1; }
  static DataType key_type(const VMInstruction &inst) {
    return (DataType)inst.p2;
  }
  static int32_t record_size(const VMInstruction &inst) { return inst.p3; }
  static void print(const VMInstruction &inst) {
    printf("cursor=%d key_type=%s record_size=%d", cursor_id(inst),
           datatype_to_string(key_type(inst)), record_size(inst));
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
  case OP_First:
    return "First";
  case OP_Last:
    return "Last";
  case OP_Next:
    return "Next";
  case OP_Prev:
    return "Prev";
  case OP_Seek:
    return "Seek";
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
  case OP_Test:
    return "Test";
  case OP_Result:
    return "Result";
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
  case OP_Arithmetic:
    return "Arithmetic";
  case OP_Commit:
    return "Commit";
  case OP_Rollback:
    return "Rollback";
  case OP_OpenMemTree:
    return "OpenMemTree";
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

inline const char *compare_op_to_string(CompareOp op) {
  switch (op) {
  case EQ:
    return "EQ";
  case NE:
    return "NE";
  case LT:
    return "LT";
  case LE:
    return "LE";
  case GT:
    return "GT";
  case GE:
    return "GE";
  default:
    return "??";
  }
}

inline void debug_print_instruction(const VMInstruction &inst, size_t index) {
  printf("[%3zu] %-12s ", index, opcode_to_string(inst.opcode));

  switch (inst.opcode) {
  case OP_Trace:
    printf("msg=\"%s\"", inst.p4 ? (const char *)inst.p4 : "");
    break;

  case OP_Goto:
    printf("-> %d", inst.p2);
    break;

  case OP_Halt:
    printf("exit_code=%d", inst.p1);
    break;

  case OP_OpenRead:
  case OP_OpenWrite:
    printf("cursor=%d table=\"%s\"", inst.p1,
           inst.p4 ? (const char *)inst.p4 : "?");
    if (inst.p3 != 0) {
      printf(" index_col=%d", inst.p3);
    }
    break;

  case OP_Close:
    printf("cursor=%d", inst.p1);
    break;

  case OP_First:
  case OP_Last:
    printf("cursor=%d", inst.p1);
    if (inst.p2 >= 0) {
      printf(" empty->%d", inst.p2);
    }
    break;

  case OP_Next:
  case OP_Prev:
    printf("cursor=%d", inst.p1);
    if (inst.p2 >= 0) {
      printf(" done->%d", inst.p2);
    }
    break;

  case OP_Seek:
    printf("cursor=%d key=r%d op=%s", inst.p1, inst.p2,
           compare_op_to_string((CompareOp)inst.p5));
    if (inst.p3 >= 0) {
      printf(" notfound->%d", inst.p3);
    }
    break;

  case OP_Column:
    printf("cursor=%d col=%d -> r%d", inst.p1, inst.p2, inst.p3);
    break;

  case OP_MakeRecord:
    printf("r%d..r%d (%d regs) -> r%d", inst.p1, inst.p1 + inst.p2 - 1, inst.p2,
           inst.p3);
    break;

  case OP_Insert:
    printf("cursor=%d key=r%d record=r%d", inst.p1, inst.p2, inst.p3);
    break;

  case OP_Delete:
    printf("cursor=%d", inst.p1);
    break;

  case OP_Update:
    printf("cursor=%d record=r%d", inst.p1, inst.p2);
    break;

  case OP_Integer:
    printf("r%d = %d", inst.p1, inst.p2);
    break;

  case OP_String:
    printf("r%d = ", inst.p1);
    if (inst.p4) {
      printf("\"");
      const char *str = (const char *)inst.p4;
      for (size_t i = 0; i < (size_t)inst.p2 && str[i]; i++) {
        if (str[i] >= 32 && str[i] < 127) {
          printf("%c", str[i]);
        } else {
          printf("\\\\x%02x", (unsigned char)str[i]);
        }
      }
      printf("\"");
    } else {
      printf("NULL");
    }
    printf(" (type=%s)", datatype_to_string((DataType)inst.p2));
    break;

  case OP_Copy:
    printf("r%d -> r%d", inst.p1, inst.p2);
    break;

  case OP_Move:
    printf("r%d => r%d", inst.p1, inst.p2);
    break;

  case OP_CreateTable:
    printf("schema=%p", inst.p4);
    break;

  case OP_DropTable:
    printf("table=\"%s\"", inst.p4 ? (const char *)inst.p4 : "?");
    break;

  case OP_CreateIndex:
  case OP_DropIndex:
    printf("col=%d table=\"%s\"", inst.p1,
           inst.p4 ? (const char *)inst.p4 : "?");
    break;

  case OP_Begin:
  case OP_Commit:
  case OP_Rollback:
    // No parameters
    break;

  case OP_OpenMemTree:
    printf("cursor=%d key_type=%s record_size=%d", inst.p1,
           datatype_to_string((DataType)inst.p2), inst.p3);
    break;
  case OP_Arithmetic: {

    const char *op_symbols[] = {"+", "-", "*", "/", "%"};
    printf("r%d = r%d %s r%d", inst.p1, inst.p2, op_symbols[inst.p5], inst.p3);
    break;
  }

  case OP_Test:
    printf("r%d = (r%d %s r%d)", inst.p1, inst.p2,
           compare_op_to_string((CompareOp)inst.p5), inst.p3);
    break;

  case OP_JumpIf:
    printf("if (r%d %s 0) goto %d", inst.p1, inst.p5 ? "!=" : "==", inst.p2);
    break;

  case OP_Logic: {
    const char *logic_ops[] = {"AND", "OR", "NOT"};
    if (inst.p5 == LOGIC_NOT) {
      printf("r%d = NOT r%d", inst.p1, inst.p2);
    } else {
      printf("r%d = r%d %s r%d", inst.p1, inst.p2, logic_ops[inst.p5], inst.p3);
    }
    break;
  }

  case OP_Result:
    printf("output r%d..r%d (%d regs)", inst.p1, inst.p1 + inst.p2 - 1,
           inst.p2);
    break;

  default:
    printf("p1=%d p2=%d p3=%d p4=%p p5=%d", inst.p1, inst.p2, inst.p3, inst.p4,
           inst.p5);
  }

  printf("\n");
}

inline void
debug_print_program(const Vector<VMInstruction, QueryArena> &program) {
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
          (check.opcode == OP_Test && check.p3 == (int)i) ||
          ((check.opcode == OP_Next || check.opcode == OP_Prev) &&
           check.p2 == (int)i) ||
          ((check.opcode == OP_First || check.opcode == OP_Last) &&
           check.p2 == (int)i) ||
          (check.opcode == OP_Seek && check.p3 == (int)i)) {
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
