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
