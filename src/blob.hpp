#pragma once
#include "defs.hpp"
#include "pager.hpp"

#include <cstdint>
#define BLOB_HEADER_SIZE 12
#define BLOB_DATA_SIZE PAGE_SIZE - BLOB_HEADER_SIZE
struct BlobNode {
    uint32_t index;
    uint32_t next;
    uint16_t size;
    uint16_t flags;
    uint8_t data[BLOB_DATA_SIZE];
};


struct BlobCursor {
    uint32_t blob_id;   // Current blob page index
    bool valid;         // Whether pointing to valid blob
    MemoryContext*ctx;
};
