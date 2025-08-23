// blob.hpp
#pragma once
#include "defs.hpp"
#include "pager.hpp"
#include "arena.hpp"
#include <cstdint>
#include <cstring>

// Blob page layout
#define BLOB_HEADER_SIZE 12
#define BLOB_DATA_SIZE (PAGE_SIZE - BLOB_HEADER_SIZE)

struct BlobNode {
    uint32_t index;      // Page index of this node
    uint32_t next;       // Next page in chain (0 if last)
    uint16_t size;       // Size of data in this node
    uint16_t flags;      // Reserved for future use
    uint8_t data[BLOB_DATA_SIZE];
};

// Blob cursor matches the pattern of BtCursor and MemCursor
struct BlobCursor {
    uint32_t blob_id;           // Current blob page index (0 means invalid)
    MemoryContext* ctx;         // Memory context for temporary buffers

    // Cached blob data for current position
    bool valid;                 // Whether cursor points to valid blob
};

// ============================================================================
// Core Blob Operations (internal)
// ============================================================================
BlobNode* get_blob(uint32_t index);
BlobNode* allocate_blob_page();
uint32_t blob_store(uint8_t* data, uint32_t size);
void blob_delete_chain(uint32_t index);
Buffer blob_fetch(uint32_t blob_id);

// ============================================================================
// Cursor Interface (matches BtCursor/MemCursor pattern)
// ============================================================================

// Navigation (mostly no-ops for blobs as they're unordered)
bool blob_cursor_first(BlobCursor* cursor);
bool blob_cursor_last(BlobCursor* cursor);
bool blob_cursor_next(BlobCursor* cursor);
bool blob_cursor_previous(BlobCursor* cursor);

// Seeking
bool blob_cursor_seek(BlobCursor* cursor, const void* key);
bool blob_cursor_seek_cmp(BlobCursor* cursor, const void* key, CompareOp op);
bool blob_cursor_seek_exact(BlobCursor* cursor, const void* key, const uint8_t* record);
bool blob_cursor_seek_ge(BlobCursor* cursor, const void* key);
bool blob_cursor_seek_gt(BlobCursor* cursor, const void* key);
bool blob_cursor_seek_le(BlobCursor* cursor, const void* key);
bool blob_cursor_seek_lt(BlobCursor* cursor, const void* key);

// Data Access
uint8_t* blob_cursor_key(BlobCursor* cursor);
uint8_t* blob_cursor_record(BlobCursor* cursor);
bool blob_cursor_is_valid(BlobCursor* cursor);

// Modification
bool blob_cursor_insert(BlobCursor* cursor, const void* key, const uint8_t* record, const uint32_t size);
bool blob_cursor_update(BlobCursor* cursor, const uint8_t* record);
bool blob_cursor_delete(BlobCursor* cursor);

// Blob-specific operations
bool blob_cursor_store_data(BlobCursor* cursor, uint8_t* data, uint32_t size);
uint32_t blob_cursor_get_size(BlobCursor* cursor);
