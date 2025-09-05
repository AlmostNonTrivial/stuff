// blob.hpp
#pragma once
#include <cstdint>

// ============================================================================
// BLOB OVERFLOW STORAGE
//
// Provides page-based overflow storage for large data that doesn't fit in
// btree pages. Each blob is a linked list of pages containing chunks of data.
//
// USAGE:
//   - Store large values that overflow from btree nodes
//   - First page index is stored in the btree as a reference
//   - Data is immutable once written (no update operations)
//
// THREAD SAFETY:
//   Not thread-safe. Requires external synchronization.
// ============================================================================

// Single page in a blob chain
struct blob_page
{
	uint32_t next; // Next page index (0 if last)
	uint16_t size; // Size of data in this page
	uint8_t *data; // Pointer to page data
};

// ============================================================================
// Core Blob Operations
// ============================================================================

// Create a new blob from data, returns first page index (0 on failure)
uint32_t
blob_create(void *data, uint32_t size);

// Delete entire blob chain starting from first_page
void
blob_delete(uint32_t first_page);

// Read a single page from blob chain (for VM streaming)
blob_page
blob_read_page(uint32_t page_index);

// Get total size of blob (walks chain to calculate)
uint32_t
blob_get_size(uint32_t first_page);

// ============================================================================
// Streaming Interface (for arena-based full reads)
// ============================================================================

// Read entire blob into arena-allocated buffer

void *
blob_read_full(uint32_t first_page, uint64_t *size);
