#pragma once

#include <iomanip>
#include <cstdint>
#define PAGE_SIZE 512

enum DataType : uint32_t {
    TYPE_INT32 = 4,      // 4-byte integer
    TYPE_INT64 = 8,      // 8-byte integer
    TYPE_VARCHAR32 = 32, // Variable char up to 32 bytes
    TYPE_VARCHAR256 = 256 // Variable char up to 256 bytes
};

int cmp(DataType key_size, const uint8_t *key1, const uint8_t *key2) ;
