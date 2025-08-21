#pragma once

#include <iostream>

#define PAGE_SIZE 2048
#include <cstdint>

struct QueryArena {};
// #define FAIL(msg) ((std::cout << msg << std::endl; exit(1)))

enum DataType : uint32_t {

    TYPE_NULL= 0,       //
    TYPE_UINT32 = 4,      // 4-byte integer
    TYPE_UINT64 = 8,      // 8-byte integer
    // TYPE_INT32 = 4,      // 4-byte integer // would this work
    // TYPE_INT64 = 8,      // 8-byte integer
    TYPE_VARCHAR32 = 32, // Variable char up to 32 bytes
    TYPE_VARCHAR256 = 256 // Variable char up to 256 bytes
};

// VM value - uses arena allocation for data
struct TypedValue {
  DataType type;
  uint8_t *data; // Points to arena-allocated memory
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


enum CompareOp {
    EQ = 0,
    NE = 1,
    LT = 2,
    LE = 3,
    GT = 4,
    GE = 5
};

int cmp(DataType key_size, const uint8_t *key1, const uint8_t *key2) ;

void print_ptr(uint8_t *data, DataType type);

void debug_type(uint8_t *data, DataType type);
#define NL '\n'
#define END << '\n'
#define PRINT std::cout <<
#define COMMA << ", " <<

// Function type definitions
typedef int (*CmpFn)(const uint8_t*, const uint8_t*);
typedef void (*CopyFn)(uint8_t* dst, const uint8_t* src);
typedef uint64_t (*ToU64Fn)(const uint8_t*);
typedef void (*FromU64Fn)(uint8_t* dst, uint64_t val);
typedef void (*PrintFn)(const uint8_t*);
typedef size_t (*SizeFn)();

// Arithmetic operations return success/failure for div-by-zero
typedef bool (*ArithFn)(uint8_t* dst, const uint8_t* a, const uint8_t* b);

// Type operations dispatch table
struct TypeOps {
    CmpFn cmp;
    CopyFn copy;
    ToU64Fn to_u64;      // Convert to u64 for arithmetic
    FromU64Fn from_u64;  // Convert from u64 back
    PrintFn print;
    SizeFn size;

    // Arithmetic ops
    ArithFn add;
    ArithFn sub;
    ArithFn mul;
    ArithFn div;
    ArithFn mod;
};

// Global dispatch table indexed by DataType
extern TypeOps type_ops[257];

// Initialize dispatch tables (call once at startup)
void init_type_ops();

// Convenience functions that use dispatch tables
inline int cmp(DataType type, const uint8_t* a, const uint8_t* b) {
    return type_ops[type].cmp(a, b);
}

inline void copy_value(DataType type, uint8_t* dst, const uint8_t* src) {
    type_ops[type].copy(dst, src);
}

inline size_t type_size(DataType type) {
    return type_ops[type].size();
}

inline void print_value(DataType type, const uint8_t* data) {
    type_ops[type].print(data);
}

// Arithmetic with type promotion
bool do_arithmetic(ArithOp op, DataType type, uint8_t* dst,
                   const uint8_t* a, const uint8_t* b);
