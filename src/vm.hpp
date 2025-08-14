#pragma once
#include "btree.hpp"
#include "defs.hpp"
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>


// Tagged union for VM values
struct VMValue {
    DataType type;
    // instead of a union
    uint8_t data[TYPE_VARCHAR256];
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
    std::string table_name;
    std::vector<ColumnInfo> columns;
    uint32_t record_size;

    DataType key() {
        return columns[0].type;
    }

};


struct Table {
    TableSchema schema;
    BPlusTree tree;
};

struct Index {
    BPlusTree tree;
    uint32_t column_index;
    std::string name;
    uint32_t root_page;
};

#define REGISTER_COUNT 20




// Program control

bool vm_execute(VMInstruction* instructions, uint32_t count);
bool vm_step();
