// vm.hpp
#pragma once
#include "arena.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "schema.hpp"
#include "vec.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>


// #define ALLOC_VALUE()

struct VMValue {
    DataType type;
    uint8_t data[TYPE_256];
};

typedef void (*ResultCallback)(Vec<TypedValue, QueryArena> result);
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
    const char *table_name;
    uint32_t root_page;
    uint32_t column;
};

Vec<VmEvent, QueryArena> &vm_events();

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
  OP_Insert = 34,
  OP_Delete = 35,
  OP_Update = 36,

  // Register operations
  OP_Move = 40,
  OP_Load= 41,

  // Computation
  OP_Test = 60,
  OP_Arithmetic = 51,
  OP_JumpIf = 52,
  OP_Logic = 53,
  OP_Result = 54,


  // Schema operations
  OP_CreateTable = 81,
  OP_CreateIndex= 82,
  OP_DropIndex= 83,
  OP_DropTable= 84,
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
// Opcode Namespace
// ============================================================================

namespace Opcodes {

// Control Flow Operations
namespace Goto {
    inline VMInstruction create(int32_t target) {
        return {OP_Goto, 0, target, 0, nullptr, 0};
    }

    inline int32_t target(const VMInstruction &inst) {
        return inst.p2;
    }

    inline void print(const VMInstruction &inst) {
        printf("Goto %d", inst.p2);
    }
}

namespace Halt {
    inline VMInstruction create(int32_t exit_code = 0) {
        return {OP_Halt, exit_code, 0, 0, nullptr, 0};
    }

    inline int32_t exit_code(const VMInstruction &inst) {
        return inst.p1;
    }

    inline void print(const VMInstruction &inst) {
        printf("Halt %d", inst.p1);
    }
}

// Cursor Operations
namespace Open {
    inline VMInstruction create_btree(int32_t cursor_id, const char *table_name,
                                      int32_t index_col = 0, bool is_write = false) {
        uint8_t flags = (is_write ? 0x01 : 0);
        return {OP_Open, cursor_id, index_col, 0, (void *)table_name, flags};
    }

    inline VMInstruction create_ephemeral(int32_t cursor_id,  EmbVec<DataType, MAX_RECORD_LAYOUT>*layout) {
        uint8_t flags = (0x01) | (0x02);
        return {OP_Open, cursor_id, 0, 0, layout, flags};
    }


    inline int32_t cursor_id(const VMInstruction &inst) {
        return inst.p1;
    }

    inline RecordLayout* ephemeral_schema(const VMInstruction &inst) {
        return (RecordLayout*)inst.p4;
    }

    inline int32_t index_col(const VMInstruction &inst) {
        return inst.p2;
    }

    inline const char* table_name(const VMInstruction &inst) {
        return (const char *)inst.p4;
    }

    inline bool is_write(const VMInstruction &inst) {
        return inst.p5 & 0x01;
    }

    inline bool is_ephemeral(const VMInstruction &inst) {
        return inst.p5 & 0x02;
    }

    inline void print(const VMInstruction &inst) {
        if (is_ephemeral(inst)) {
            printf("Open cursor=%d ephemeral write=%d", inst.p1, is_write(inst));
        } else {
            printf("Open cursor=%d table=%s index_col=%d write=%d",
                   inst.p1, table_name(inst), inst.p2, is_write(inst));
        }
    }
}

namespace Close {
    inline VMInstruction create(int32_t cursor_id) {
        return {OP_Close, cursor_id, 0, 0, nullptr, 0};
    }

    inline int32_t cursor_id(const VMInstruction &inst) {
        return inst.p1;
    }

    inline void print(const VMInstruction &inst) {
        printf("Close cursor=%d", inst.p1);
    }
}

namespace Rewind {
    inline VMInstruction create(int32_t cursor_id, int32_t jump_if_empty = -1,
                               bool to_end = false) {
        return {OP_Rewind, cursor_id, jump_if_empty, 0, nullptr, (uint8_t)to_end};
    }

    inline int32_t cursor_id(const VMInstruction &inst) {
        return inst.p1;
    }

    inline int32_t jump_if_empty(const VMInstruction &inst) {
        return inst.p2;
    }

    inline bool to_end(const VMInstruction &inst) {
        return inst.p5 != 0;
    }

    inline void print(const VMInstruction &inst) {
        printf("Rewind cursor=%d jump_if_empty=%d to_end=%d",
               inst.p1, inst.p2, inst.p5);
    }
}

namespace Step {
    inline VMInstruction create(int32_t cursor_id, int32_t jump_if_done = -1,
                               bool forward = true) {
        return {OP_Step, cursor_id, jump_if_done, 0, nullptr, (uint8_t)forward};
    }

    inline int32_t cursor_id(const VMInstruction &inst) {
        return inst.p1;
    }

    inline int32_t jump_if_done(const VMInstruction &inst) {
        return inst.p2;
    }

    inline bool forward(const VMInstruction &inst) {
        return inst.p5 != 0;
    }

    inline void print(const VMInstruction &inst) {
        printf("Step cursor=%d jump_if_done=%d forward=%d",
               inst.p1, inst.p2, inst.p5);
    }
}

namespace Seek {
    inline VMInstruction create(int32_t cursor_id, int32_t key_reg,
                               int32_t jump_if_not, CompareOp op) {
        return {OP_Seek, cursor_id, key_reg, jump_if_not, nullptr, (uint8_t)op};
    }

    inline int32_t cursor_id(const VMInstruction &inst) {
        return inst.p1;
    }

    inline int32_t key_reg(const VMInstruction &inst) {
        return inst.p2;
    }

    inline int32_t jump_if_not(const VMInstruction &inst) {
        return inst.p3;
    }

    inline CompareOp op(const VMInstruction &inst) {
        return (CompareOp)inst.p5;
    }

    inline void print(const VMInstruction &inst) {
        const char* op_names[] = {"EQ", "NE", "LT", "LE", "GT", "GE"};
        printf("Seek cursor=%d key_reg=%d jump_if_not=%d op=%s",
               inst.p1, inst.p2, inst.p3, op_names[inst.p5]);
    }
}

// Data Operations
namespace Column {
    inline VMInstruction create(int32_t cursor_id, int32_t column_index,
                               int32_t dest_reg) {
        return {OP_Column, cursor_id, column_index, dest_reg, nullptr, 0};
    }

    inline int32_t cursor_id(const VMInstruction &inst) {
        return inst.p1;
    }

    inline int32_t column_index(const VMInstruction &inst) {
        return inst.p2;
    }

    inline int32_t dest_reg(const VMInstruction &inst) {
        return inst.p3;
    }

    inline void print(const VMInstruction &inst) {
        printf("Column cursor=%d col=%d dest_reg=%d",
               inst.p1, inst.p2, inst.p3);
    }
}

namespace Insert {
    inline VMInstruction create(int32_t cursor_id, int32_t start_reg, int32_t reg_count) {
        return {OP_Insert, cursor_id, start_reg, reg_count, nullptr, 0};
    }

    inline int32_t cursor_id(const VMInstruction &inst) {
        return inst.p1;
    }

    inline int32_t key_reg(const VMInstruction &inst) {
        return inst.p2;
    }

    inline int32_t reg_count(const VMInstruction &inst) {
        return inst.p3;
    }

    inline void print(const VMInstruction &inst) {
        printf("Insert cursor=%d key_reg=%d reg_count=%d",
               inst.p1, inst.p2, inst.p3);
    }
}

namespace Delete {
    inline VMInstruction create(int32_t cursor_id) {
        return {OP_Delete, cursor_id, 0, 0, nullptr, 0};
    }

    inline int32_t cursor_id(const VMInstruction &inst) {
        return inst.p1;
    }

    inline void print(const VMInstruction &inst) {
        printf("Delete cursor=%d", inst.p1);
    }
}

namespace Update {
    inline VMInstruction create(int32_t cursor_id, int32_t record_reg) {
        return {OP_Update, cursor_id, record_reg, 0, nullptr, 0};
    }

    inline int32_t cursor_id(const VMInstruction &inst) {
        return inst.p1;
    }

    inline int32_t record_reg(const VMInstruction &inst) {
        return inst.p2;
    }

    inline void print(const VMInstruction &inst) {
        printf("Update cursor=%d record_reg=%d", inst.p1, inst.p2);
    }
}

// Register Operations
namespace Move {
    inline VMInstruction create_load(int32_t dest_reg,  DataType type, void*data) {
        return {OP_Move, dest_reg, (int32_t)type, -1, data, 0};
    }

    inline VMInstruction create_create(int32_t dest_reg,  int32_t src_reg) {
        return {OP_Move, dest_reg, 0, src_reg, nullptr, 0};
    }

    inline int32_t is_load(const VMInstruction &inst) {
            return inst.p3 == -1;
        }

    inline int32_t dest_reg(const VMInstruction &inst) {
        return inst.p1;
    }
    inline int32_t src_reg(const VMInstruction &inst) {
        return inst.p3;
    }

    inline uint8_t*data(const VMInstruction &inst) {
       return (uint8_t*)inst.p4;
    }
    inline DataType type(const VMInstruction &inst) {
        return (DataType)inst.p2;
    }

    inline void print(const VMInstruction &inst) {
        printf("Load reg=%d type=%d", inst.p1, inst.p2);
    }
}



// Computation
namespace Test {
    inline VMInstruction create(int32_t dest_reg, int32_t left_reg,
                               int32_t right_reg, CompareOp op) {
        return {OP_Test, dest_reg, left_reg, right_reg, nullptr, (uint8_t)op};
    }

    inline int32_t dest_reg(const VMInstruction &inst) {
        return inst.p1;
    }

    inline int32_t left_reg(const VMInstruction &inst) {
        return inst.p2;
    }

    inline int32_t right_reg(const VMInstruction &inst) {
        return inst.p3;
    }

    inline CompareOp op(const VMInstruction &inst) {
        return (CompareOp)inst.p5;
    }

    inline void print(const VMInstruction &inst) {
        const char* op_names[] = {"EQ", "NE", "LT", "LE", "GT", "GE"};
        printf("Test dest=%d left=%d right=%d op=%s",
               inst.p1, inst.p2, inst.p3, op_names[inst.p5]);
    }
}

namespace Arithmetic {
    inline VMInstruction create(int32_t dest_reg, int32_t left_reg,
                               int32_t right_reg, ArithOp op) {
        return {OP_Arithmetic, dest_reg, left_reg, right_reg, nullptr, (uint8_t)op};
    }

    inline int32_t dest_reg(const VMInstruction &inst) {
        return inst.p1;
    }

    inline int32_t left_reg(const VMInstruction &inst) {
        return inst.p2;
    }

    inline int32_t right_reg(const VMInstruction &inst) {
        return inst.p3;
    }

    inline ArithOp op(const VMInstruction &inst) {
        return (ArithOp)inst.p5;
    }

    inline void print(const VMInstruction &inst) {
        const char* op_names[] = {"ADD", "SUB", "MUL", "DIV", "MOD"};
        printf("Arithmetic dest=%d left=%d right=%d op=%s",
               inst.p1, inst.p2, inst.p3, op_names[inst.p5]);
    }
}

namespace JumpIf {
    inline VMInstruction create(int32_t test_reg, int32_t jump_target,
                               bool jump_on_true = true) {
        return {OP_JumpIf, test_reg, jump_target, 0, nullptr, (uint8_t)jump_on_true};
    }

    inline int32_t test_reg(const VMInstruction &inst) {
        return inst.p1;
    }

    inline int32_t jump_target(const VMInstruction &inst) {
        return inst.p2;
    }

    inline bool jump_on_true(const VMInstruction &inst) {
        return inst.p5 != 0;
    }

    inline void print(const VMInstruction &inst) {
        printf("JumpIf test_reg=%d target=%d on_true=%d",
               inst.p1, inst.p2, inst.p5);
    }
}

namespace Logic {
    inline VMInstruction create(int32_t dest_reg, int32_t left_reg,
                               int32_t right_reg, LogicOp op) {
        return {OP_Logic, dest_reg, left_reg, right_reg, nullptr, (uint8_t)op};
    }

    inline int32_t dest_reg(const VMInstruction &inst) {
        return inst.p1;
    }

    inline int32_t left_reg(const VMInstruction &inst) {
        return inst.p2;
    }

    inline int32_t right_reg(const VMInstruction &inst) {
        return inst.p3;
    }

    inline LogicOp op(const VMInstruction &inst) {
        return (LogicOp)inst.p5;
    }

    inline void print(const VMInstruction &inst) {
        const char* op_names[] = {"AND", "OR"};
        printf("Logic dest=%d left=%d right=%d op=%s",
               inst.p1, inst.p2, inst.p3, op_names[inst.p5]);
    }
}

namespace Result {
    inline VMInstruction create(int32_t first_reg, int32_t reg_count) {
        return {OP_Result, first_reg, reg_count, 0, nullptr, 0};
    }

    inline int32_t first_reg(const VMInstruction &inst) {
        return inst.p1;
    }

    inline int32_t reg_count(const VMInstruction &inst) {
        return inst.p2;
    }

    inline void print(const VMInstruction &inst) {
        printf("Result first_reg=%d count=%d", inst.p1, inst.p2);
    }
}

// Schema Operations
namespace Schema {
    inline VMInstruction create_table(int start_reg, int count, char * name) {
        return {OP_Schema, start_reg, count, 0, name, SCHEMA_CREATE_TABLE};
    }

    inline int create_table_start_reg(const VMInstruction &inst) {
       return inst.p1;
    }
    inline int create_table_reg_count(const VMInstruction &inst) {
       return inst.p2;
    }
    inline char * create_table_table_name(const VMInstruction &inst) {
       return (char*)inst.p4;
    }

    inline VMInstruction create_index(const char *table_name, int32_t column_index) {
        return {OP_Schema, column_index, 0, 0, (void *)table_name, SCHEMA_CREATE_INDEX};
    }


    inline SchemaOp op_type(const VMInstruction &inst) {
        return (SchemaOp)inst.p5;
    }

    inline const char* table_name(const VMInstruction &inst) {
        return (const char *)inst.p4;
    }

    inline int column_name_register(const VMInstruction &inst) {
        return inst.p1;
    }


    inline VMInstruction drop_table(const char *table_name) {
        return {OP_Schema, 0, 0, 0, (void *)table_name, SCHEMA_DROP_TABLE};
    }

    inline VMInstruction drop_index(const char *table_name, int32_t column_index) {
        return {OP_Schema, column_index, 0, 0, (void *)table_name, SCHEMA_DROP_INDEX};
    }


    inline void print(const VMInstruction &inst) {
        const char* op_names[] = {"CREATE_TABLE", "DROP_TABLE", "CREATE_INDEX", "DROP_INDEX"};
        printf("Schema op=%s", op_names[inst.p5]);
        if (inst.p5 == SCHEMA_CREATE_TABLE) {
            // ::Schema* s = table_schema(inst);
            // printf(" table=%s", s ? s->table_name.c_str() : "null");
        } else if (inst.p5 == SCHEMA_DROP_TABLE) {
            printf(" table=%s", table_name(inst));
        } else {
            printf(" table=%s col=%d", table_name(inst), inst.p1);
        }
    }
}

// Transactions
namespace Transaction {
    inline VMInstruction begin() {
        return {OP_Transaction, 0, 0, 0, nullptr, TXN_BEGIN};
    }

    inline VMInstruction commit() {
        return {OP_Transaction, 0, 0, 0, nullptr, TXN_COMMIT};
    }

    inline VMInstruction rollback() {
        return {OP_Transaction, 0, 0, 0, nullptr, TXN_ROLLBACK};
    }

    inline TransactionOp op_type(const VMInstruction &inst) {
        return (TransactionOp)inst.p5;
    }

    inline void print(const VMInstruction &inst) {
        const char* op_names[] = {"BEGIN", "COMMIT", "ROLLBACK"};
        printf("Transaction %s", op_names[inst.p5]);
    }
}

} // namespace Opcodes

// ============================================================================
// Debug Namespace
// ============================================================================

namespace Debug {

inline const char* opcode_name(OpCode op) {
    switch(op) {
        case OP_Goto: return "Goto";
        case OP_Halt: return "Halt";
        case OP_Open: return "Open";
        case OP_Close: return "Close";
        case OP_Rewind: return "Rewind";
        case OP_Step: return "Step";
        case OP_Seek: return "Seek";
        case OP_Column: return "Column";
        case OP_Insert: return "Insert";
        case OP_Delete: return "Delete";
        case OP_Update: return "Update";
        case OP_Move: return "Move";

        case OP_Test: return "Test";
        case OP_Arithmetic: return "Arithmetic";
        case OP_JumpIf: return "JumpIf";
        case OP_Logic: return "Logic";
        case OP_Result: return "Result";
        case OP_Schema: return "Schema";
        case OP_Transaction: return "Transaction";
        default: return "Unknown";
    }
}

inline void print_instruction(const VMInstruction &inst, int pc = -1) {
    if (pc >= 0) {
        printf("%3d: ", pc);
    }

    printf("%-12s ", opcode_name(inst.opcode));

    switch(inst.opcode) {
        case OP_Goto: Opcodes::Goto::print(inst); break;
        case OP_Halt: Opcodes::Halt::print(inst); break;
        case OP_Open: Opcodes::Open::print(inst); break;
        case OP_Close: Opcodes::Close::print(inst); break;
        case OP_Rewind: Opcodes::Rewind::print(inst); break;
        case OP_Step: Opcodes::Step::print(inst); break;
        case OP_Seek: Opcodes::Seek::print(inst); break;
        case OP_Column: Opcodes::Column::print(inst); break;
        case OP_Insert: Opcodes::Insert::print(inst); break;
        case OP_Delete: Opcodes::Delete::print(inst); break;
        case OP_Update: Opcodes::Update::print(inst); break;
        case OP_Move: Opcodes::Move::print(inst); break;
        case OP_Test: Opcodes::Test::print(inst); break;
        case OP_Arithmetic: Opcodes::Arithmetic::print(inst); break;
        case OP_JumpIf: Opcodes::JumpIf::print(inst); break;
        case OP_Logic: Opcodes::Logic::print(inst); break;
        case OP_Result: Opcodes::Result::print(inst); break;
        case OP_Schema: Opcodes::Schema::print(inst); break;
        case OP_Transaction: Opcodes::Transaction::print(inst); break;
        default: printf("p1=%d p2=%d p3=%d", inst.p1, inst.p2, inst.p3);
    }
    printf("\n");
}

inline void print_program(const Vec<VMInstruction, QueryArena> &program) {
    printf("===== VM PROGRAM (%zu instructions) =====\n", program.size());
    for (size_t i = 0; i < program.size(); i++) {
        print_instruction(program[i], i);
    }
    printf("==========================================\n");
}

inline void print_register(int reg_num, const VMValue &value) {
    printf("R[%2d]: type=%3d ", reg_num, value.type);
    if (value.data) {
        printf("value=");
        print_value(value.type, value.data);
    } else {
        printf("(null)");
    }
    printf("\n");
}

inline void enable_trace(bool enable) {
    _debug = enable;
}

} // namespace Debug

// VM Runtime Definitions
#define REGISTERS 40
#define CURSORS 10

enum VM_RESULT { OK, ABORT, ERR };

// VM Functions
VM_RESULT vm_execute(Vec<VMInstruction, QueryArena> &instructions);
void vm_set_result_callback(ResultCallback callback);

// Debug function that can be called from VM to print registers
void vm_debug_print_all_registers();
