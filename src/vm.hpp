#pragma once
#include "btree.hpp"
#include "defs.hpp"
#include <cstddef>
#include <unordered_map>
#include <vector>


// Tagged union for VM values
struct VMValue {
    DataType type;
    union {
        nullptr_t n;
        int32_t i32;
        int64_t i64;
        uint32_t u32;
        uint64_t u64;
        char str32[TYPE_VARCHAR32];
        char str256[TYPE_VARCHAR256];
    };
};

enum OpCode : uint8_t {
    // Control flow
    OP_Trace = 0,
    OP_Goto = 1,
    OP_Halt = 2,
    OP_If = 3,
    OP_IfNot = 4,

    // Cursor operations
    OP_OpenRead = 5,
    OP_OpenWrite = 6,
    OP_Close = 7,
    OP_Rewind = 8,
    OP_Next = 9,
    OP_Prev = 10,
    OP_SeekGE = 11,
    OP_SeekGT = 12,
    OP_SeekLE = 13,
    OP_SeekLT = 14,
    OP_SeekEQ = 15,
    OP_Found = 16,
    OP_NotFound = 17,

    // Data operations
    OP_Column = 18,
    OP_Rowid = 19,
    OP_MakeRecord = 20,
    OP_Insert = 21,
    OP_Delete = 22,
    OP_NewRowid = 23,

    // Register operations
    OP_Integer = 24,
    OP_String = 25,
    OP_Copy = 28,
    OP_Move = 29,

    // Comparison
    OP_Compare = 30,
    OP_Jump = 31,

    // Results
    OP_ResultRow = 32,

    // Schema operations
    OP_CreateTable = 33,
    OP_DropTable = 34,
    OP_CreateIndex = 35,
    OP_DropIndex = 36,

    // Transactions
    OP_Begin = 59,
    OP_Commit = 60,
    OP_Rollback = 61,
};

struct VMInstruction {
    OpCode opcode;
    int32_t p1;
    int32_t p2;
    int32_t p3;
    void* p4;
    uint8_t p5;
};


struct ColumnInfo {
    char name[32];
    DataType type;
};

struct TableSchema {
    char table_name[64];
    std::vector<ColumnInfo> columns;
};


struct Table {
    TableSchema schema;
    BPlusTree tree;
};

struct Index {
    BPlusTree tree;
    uint32_t column_index;
    char name[64];
    uint32_t root_page;
};

#define REGISTER_COUNT 20

struct VM {
    // Execution state
    std::vector<VMInstruction> program;
    uint32_t pc;
    bool halted;

    // Registers
    VMValue registers[REGISTER_COUNT];

    std::unordered_map<uint32_t, BtCursor> cursors;
    std::unordered_map<uint32_t, Table> tables;
    std::unordered_map<uint32_t, Index> indexes;


    int32_t compare_result;

    void (*result_callback)(VMValue**, uint32_t);
    bool in_transaction;
};

// VM lifecycle
VM* vm_create(uint32_t max_registers, uint32_t max_cursors);
void vm_destroy(VM* vm);
void vm_reset(VM* vm);

// Program control
void vm_load_program(VM* vm, VMInstruction* instructions, uint32_t count);
bool vm_execute(VM* vm);
bool vm_step(VM* vm);

// Table/Index management
bool vm_create_table(VM* vm, TableSchema* schema);
bool vm_create_index(VM* vm, const char* table_name, uint32_t column_index);
Table* vm_get_table(VM* vm, const char* name);
Index* vm_get_index(VM* vm, const char* name);
