// blob.cpp
#include "blob.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>

// ============================================================================
// Internal Blob Storage Functions
// ============================================================================

BlobNode* get_blob(uint32_t index) {
    if (index == 0) return nullptr;
    return reinterpret_cast<BlobNode*>(pager_get(index));
}

BlobNode* allocate_blob_page() {
    uint32_t page_index = pager_new();
    BlobNode* node = reinterpret_cast<BlobNode*>(pager_get(page_index));

    node->index = page_index;
    node->next = 0;
    node->size = 0;
    node->flags = 0;

    pager_mark_dirty(node->index);
    return node;
}

uint32_t blob_store(uint8_t* data, uint32_t size) {
    if (!data || size == 0) return 0;

    int nodes_required = (size + BLOB_DATA_SIZE - 1) / BLOB_DATA_SIZE;

    uint32_t first_page = 0;
    uint32_t prev_page = 0;
    uint32_t remaining = size;
    uint8_t* current_data = data;

    for (int i = 0; i < nodes_required; i++) {
        BlobNode* node = allocate_blob_page();
        node->size = std::min(remaining, (uint32_t)BLOB_DATA_SIZE);
        node->next = 0;

        memcpy(node->data, current_data, node->size);

        if (i == 0) {
            first_page = node->index;
        } else {
            BlobNode* prev = get_blob(prev_page);
            prev->next = node->index;
            pager_mark_dirty(prev_page);
        }

        current_data += node->size;
        remaining -= node->size;
        prev_page = node->index;

        pager_mark_dirty(node->index);
    }

    return first_page;
}

void blob_delete_chain(uint32_t index) {
    if (index == 0) return;

    BlobNode* blob = get_blob(index);
    if (!blob) return;

    if (blob->next) {
        blob_delete_chain(blob->next);
    }

    pager_delete(index);
}

// ============================================================================
// Cursor Navigation (mostly no-ops for unordered blobs)
// ============================================================================

bool blob_cursor_first(BlobCursor* cursor) {
    return cursor->valid;
}

bool blob_cursor_last(BlobCursor* cursor) {
    return cursor->valid;
}

bool blob_cursor_next(BlobCursor* cursor) {
    return false;
}

bool blob_cursor_previous(BlobCursor* cursor) {
    return false;
}

// ============================================================================
// Cursor Seeking
// ============================================================================

bool blob_cursor_seek(BlobCursor* cursor, const void* key) {
    if (!key) {
        cursor->valid = false;
        cursor->blob_id = 0;
        return false;
    }

    // Key is the blob_id (uint32_t)
    uint32_t blob_id = *(const uint32_t*)key;

    if (blob_id == 0) {
        cursor->valid = false;
        cursor->blob_id = 0;
        return false;
    }

    // Lazy: just verify the blob exists
    BlobNode* node = get_blob(blob_id);
    if (!node) {
        cursor->valid = false;
        cursor->blob_id = 0;
        return false;
    }

    cursor->blob_id = blob_id;
    cursor->valid = true;

    return true;
}

bool blob_cursor_seek_cmp(BlobCursor* cursor, const void* key, CompareOp op) {
    if (op == EQ) {
        return blob_cursor_seek(cursor, key);
    }
    return false;
}

bool blob_cursor_seek_exact(BlobCursor* cursor, const void* key, const uint8_t* record) {
    return blob_cursor_seek(cursor, key);
}

// ============================================================================
// Data Access
// ============================================================================

uint8_t* blob_cursor_key(BlobCursor* cursor) {
    if (!cursor->valid) {
        return nullptr;
    }

    // Allocate space for the key and return it
    uint32_t* key_storage = (uint32_t*)cursor->ctx->alloc(sizeof(uint32_t));
    *key_storage = cursor->blob_id;
    return (uint8_t*)key_storage;
}

uint8_t* blob_cursor_record(BlobCursor* cursor) {
    if (!cursor->valid || cursor->blob_id == 0) {
        return nullptr;
    }

    // First pass: calculate total size
    uint32_t total_size = 0;
    uint32_t current = cursor->blob_id;

    while (current) {
        BlobNode* node = get_blob(current);
        if (!node) return nullptr;

        total_size += node->size;
        current = node->next;
    }

    if (total_size == 0) {
        return nullptr;
    }

    // Allocate from context
    uint8_t* data = (uint8_t*)cursor->ctx->alloc(total_size);

    // Second pass: copy data
    uint32_t offset = 0;
    current = cursor->blob_id;

    while (current) {
        BlobNode* node = get_blob(current);
        memcpy(data + offset, node->data, node->size);
        offset += node->size;
        current = node->next;
    }

    return data;
}

bool blob_cursor_is_valid(BlobCursor* cursor) {
    return cursor->valid && cursor->blob_id != 0;
}

// ============================================================================
// Modification Operations
// ============================================================================

bool blob_cursor_insert(BlobCursor* cursor, const void* key, const uint8_t* record, const uint32_t size) {
    // For blob insertion through the standard cursor interface:
    // - key: contains the size as uint32_t
    // - record: points to the actual data

    if (!key || !record) {
        return false;
    }

    if (size == 0) {
        return false;
    }

    // Store the blob and update cursor
    uint32_t blob_id = blob_store((uint8_t*)record, size);
    if (blob_id == 0) {
        return false;
    }

    cursor->blob_id = blob_id;
    cursor->valid = true;

    return true;
}

bool blob_cursor_update(BlobCursor* cursor, const uint8_t* record) {
    // Blobs are immutable - cannot update in place
    return false;
}

bool blob_cursor_delete(BlobCursor* cursor) {
    if (!cursor->valid || cursor->blob_id == 0) {
        return false;
    }

    blob_delete_chain(cursor->blob_id);
    cursor->valid = false;
    cursor->blob_id = 0;
    return true;
}
