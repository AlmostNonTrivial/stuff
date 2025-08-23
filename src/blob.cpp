#include "blob.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include <algorithm>
#include <cstdint>





#include "blob.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include <algorithm>
#include <cstdint>


BlobNode* get_blob(uint32_t index) {
       return static_cast<BlobNode*>(pager_get(index));
}

BlobNode* allocate_blob_page() {
	uint32_t page_index = pager_new();
	BlobNode*node = static_cast<BlobNode*>(pager_get(page_index));

	node->index = page_index;
	node->next = 0;

	pager_mark_dirty(node->index);
	return node;
}

uint32_t insert(uint8_t* data, uint32_t size) {
    int nodes_required = (size + BLOB_DATA_SIZE - 1) / BLOB_DATA_SIZE; // Ceiling division

    uint32_t first_page = 0;
    uint32_t prev_page = 0;
    uint32_t remaining = size;
    uint8_t* current_data = data;

    for (int i = 0; i < nodes_required; i++) {
        // Allocate new page (you'll need a free list or allocator)

        BlobNode* node = allocate_blob_page();
        node->size = std::min(remaining, (uint32_t)BLOB_DATA_SIZE);
        node->next = 0;  // Will be updated if not last
        node->flags = 0;

        memcpy(node->data, current_data, node->size);

        // Link nodes
        if (i == 0) {
            first_page = node->index;
        } else {
            BlobNode* prev = static_cast<BlobNode*>(pager_get(prev_page));
            prev->next = node->index;
            pager_mark_dirty(prev_page);  // Important!
        }

        // Update for next iteration
        current_data += node->size;
        remaining -= node->size;
        prev_page = node->index;

        pager_mark_dirty(node->index);
    }

    return first_page;  // This is your blob_id
}


void delete_blob(uint32_t index) {

    auto blob = static_cast<BlobNode*>(pager_get(index));
    if(blob->next) {
        delete_blob(blob->next);
    }

    pager_delete(index);
}

Buffer get(uint32_t blob_id) {
    // First pass: calculate total size
    uint32_t total_size = 0;
    uint32_t current = blob_id;

    while (current) {
        BlobNode* node = static_cast<BlobNode*>(pager_get(current));
        if (!node) return Buffer{nullptr, 0}; // Error case

        total_size += node->size;
        current = node->next;
    }

    // Allocate contiguous buffer
    uint8_t* data = new uint8_t[total_size];

    // Second pass: copy data
    uint32_t offset = 0;
    current = blob_id;

    while (current) {
        BlobNode* node = static_cast<BlobNode*>(pager_get(current));
        memcpy(data + offset, node->data, node->size);
        offset += node->size;
        current = node->next;
    }

    return Buffer{data, total_size};
}



// Insert new blob and point cursor to it
void blob_cursor_insert(BlobCursor* cursor, uint8_t* data, uint32_t size) {
    cursor->blob_id = insert(data, size);  // Your existing insert
    cursor->valid = true;
}

// Get data from current blob
Buffer blob_cursor_get(BlobCursor* cursor) {
    if (!cursor->valid) return Buffer{nullptr, 0};
    return get(cursor->blob_id);  // Your existing get
}

// Delete current blob
void blob_cursor_delete(BlobCursor* cursor) {
    if (!cursor->valid) return;
    delete_blob(cursor->blob_id);  // Your existing delete_blob
    cursor->valid = false;
    cursor->blob_id = 0;
}

// No-ops for operations that don't make sense for blobs
bool blob_cursor_next(BlobCursor* cursor) {
    auto current = get_blob(cursor->blob_id);
    if(current && current->next) {
        current->next = current->next;
        return true;
    }
    return false;
}

void blob_cursor_prev(BlobCursor* cursor) {
    // No-op: blobs aren't ordered
}

void blob_cursor_first(BlobCursor* cursor) {
    // No-op: no ordering concept
}

void blob_cursor_last(BlobCursor* cursor) {
    // No-op: no ordering concept
}

bool blob_cursor_valid(BlobCursor* cursor) {
    return cursor->valid;
}

// Point cursor to specific blob
bool blob_cursor_seek(BlobCursor* cursor, uint32_t blob_id) {
    cursor->blob_id = blob_id;
    if(pager_get(blob_id) == nullptr) {
        cursor->valid = false;
        return false;
    }
    cursor->valid = true;
    return true;
}

// Get current blob_id (for storing in btree)
uint32_t blob_cursor_key(BlobCursor* cursor) {
    return cursor->blob_id;
}
