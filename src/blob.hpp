// blob.hpp
#pragma once
#include "defs.hpp"
#include "pager.hpp"
#include "arena.hpp"
#include <cstdint>
#include <cstring>

// Blob page layout

// Blob cursor matches the pattern of BtCursor and MemCursor
struct BlobCursor {
    uint32_t blob_id;           // Current blob page index (0 means invalid)
    MemoryContext* ctx;         // Memory context for temporary buffers
};

// ============================================================================
// Core Blob Operations (internal)
// ============================================================================




bool blob_cursor_seek(BlobCursor* cursor, uint32_t key);

uint8_t* blob_cursor_key(BlobCursor* cursor);
Buffer blob_cursor_record(BlobCursor* cursor);

uint32_t blob_cursor_insert(BlobCursor* cursor,  const uint8_t* record, const uint32_t size);
bool blob_cursor_delete(BlobCursor* cursor);
