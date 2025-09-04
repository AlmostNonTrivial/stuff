
#include "blob.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>


#define BLOB_HEADER_SIZE 12
#define BLOB_DATA_SIZE (PAGE_SIZE - BLOB_HEADER_SIZE)

struct BlobNode {
    uint32_t index;      // Page index of this node
    uint32_t next;       // Next page in chain (0 if last)
    uint16_t size;       // Size of data in this node
    uint16_t flags;      // Reserved for future use
    uint8_t data[BLOB_DATA_SIZE];
};

// ============================================================================
// Internal Blob Storage Functions
// ============================================================================

BlobNode* get_blob(uint32_t index) {
    if (index == 0) return nullptr;
    return reinterpret_cast<BlobNode*>(pager_get(index));
}

BlobNode* allocate_blob_page() {
    uint32_t page_index = pager_new();
    BlobNode* node = get_blob(page_index);

    // Initialize the node FIRST, then mark dirty
    node->index = page_index;
    node->next = 0;
    node->size = 0;
    node->flags = 0;

    pager_mark_dirty(page_index);  // FIX: Use page_index directly

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
            pager_mark_dirty(prev_page);
            prev->next = node->index;
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


bool blob_cursor_seek(BlobCursor* cursor, uint32_t blob_id) {
    if (!blob_id) {
        cursor->blob_id = 0;
        return false;
    }

    BlobNode* node = get_blob(blob_id);
    if (!node) {
        cursor->blob_id = 0;
        return false;
    }

    cursor->blob_id = blob_id;

    return true;
}



Buffer blob_cursor_record(BlobCursor* cursor) {
    auto stream = arena::stream_begin<QueryArena>(BLOB_DATA_SIZE);

    uint32_t current = cursor->blob_id;
    while (current) {
        BlobNode* node = get_blob(current);
        if (!node) {
            arena::stream_abandon(&stream);
            return {nullptr, 0};
        }
        arena::stream_write(&stream, node->data, node->size);
        current = node->next;
    }

    return {arena::stream_finish(&stream), arena::stream_size<QueryArena>(&stream)};
}



// ============================================================================
// Modification Operations
// ============================================================================

uint32_t blob_cursor_insert(BlobCursor* cursor, const uint8_t* record, const uint32_t size) {
    if (size == 0) {
        return 0;  // FIX: Changed from false to 0
    }

    // Store the blob and update cursor
    uint32_t blob_id = blob_store((uint8_t*)record, size);
    if (blob_id == 0) {
        return 0;  // FIX: Changed from false to 0
    }

    cursor->blob_id = blob_id;

    return blob_id;
}



bool blob_cursor_delete(BlobCursor* cursor) {
    if (cursor->blob_id == 0) {
        return false;
    }

    blob_delete_chain(cursor->blob_id);
    cursor->blob_id = 0;
    return true;
}
