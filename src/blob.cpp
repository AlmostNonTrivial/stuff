/**
 * blob.cpp - Binary Large Object (BLOB) Storage Implementation
 *
 * This module implements overflow page storage for large data that cannot fit
 * within standard btree nodes. BLOBs are stored as linked chains of pages,
 * where each page contains a portion of the data and a pointer to the next page.
 *
 * DESIGN DECISIONS:
 *   - Immutable: BLOBs are write-once, no update operations
 *   - Simple chaining: Linear linked list of pages, no indexing
 *   - Fixed overhead: 12 bytes per page for metadata
 *   - Zero-copy reads: Returns direct pointers to page data when possible
 *
 * PAGE LAYOUT:
 *   [0-3]   index:  Page number of this node
 *   [4-7]   next:   Next page in chain (0 terminates)
 *   [8-9]   size:   Bytes of data in this page
 *   [10-11] flags:  Reserved for future use
 *   [12+]   data:   Actual blob content (PAGE_SIZE - 12 bytes)
 *
 * USAGE PATTERN:
 *   1. btree stores large values using blob_create()
 *   2. btree column stores the returned first page index
 *   3. VM reads blob via blob_read_page() for streaming
 *   4. Query execution uses blob_read_full() for complete data
 *
 * THREAD SAFETY:
 *   Not thread-safe. Relies on pager's transaction serialization.
 *
 * MEMORY MANAGEMENT:
 *   - Page allocation/deallocation through pager
 *   - Full reads allocate from QueryArena (freed after query)
 *   - No caching - relies on pager's buffer pool
 */

#include "blob.hpp"
#include "common.hpp"
#include "pager.hpp"
#include "arena.hpp" #include "containers.hpp"
#include <cstring>

#define BLOB_HEADER_SIZE 12
#define BLOB_DATA_SIZE	 (PAGE_SIZE - BLOB_HEADER_SIZE)

// ============================================================================
// Internal Page Structure
// ============================================================================

/**
 * Internal representation of a blob page.
 * Not exposed in public API - accessed only through blob functions.
 */
struct blob_node
{
	uint32_t index; // Page index of this node
	uint32_t next;	// Next page in chain (0 if last)
	uint16_t size;	// Size of data in this node
	uint16_t flags; // Reserved for future use
	uint8_t	 data[BLOB_DATA_SIZE];
};

// ============================================================================
// Internal Helper Functions
// ============================================================================

/**
 * Get blob node from page index.
 * Returns nullptr for index 0 (invalid page).
 */
#define GET_BLOB_NODE(index) (reinterpret_cast<blob_node *>(pager_get(index)))

/**
 * Allocate and initialize a new blob page.
 * Sets all header fields to initial values and marks page dirty.
 */
inline static blob_node *
allocate_blob_node()
{
	uint32_t   page_index = pager_new();
	blob_node *node = GET_BLOB_NODE(page_index);

	// Initialize the node
	node->index = page_index;
	node->next = 0;
	node->size = 0;
	node->flags = 0;

	pager_mark_dirty(page_index);
	return node;
}

// ============================================================================
// Public Interface Implementation
// ============================================================================

/**
 * Create a new blob from data.
 *
 * Splits data across multiple pages as needed, creating a linked chain.
 * Each page holds up to BLOB_DATA_SIZE bytes.
 *
 * @param data Pointer to data to store (not modified)
 * @param size Size of data in bytes
 * @return First page index of blob chain, or 0 on failure
 */
uint32_t
blob_create(void *data, uint32_t size)
{
	if (!data || size == 0)
	{
		return 0;
	}

	int nodes_required = (size + BLOB_DATA_SIZE - 1) / BLOB_DATA_SIZE;

	uint32_t first_page = 0;
	uint32_t prev_page = 0;
	uint32_t remaining = size;
	uint8_t *current_data = (uint8_t *)data;

	for (int i = 0; i < nodes_required; i++)
	{
		blob_node *node = allocate_blob_node();
		node->size = remaining < (uint32_t)BLOB_DATA_SIZE ? remaining : BLOB_DATA_SIZE;

		memcpy(node->data, current_data, node->size);

		if (i == 0)
		{
			first_page = node->index;
		}
		else
		{
			blob_node *prev = GET_BLOB_NODE(prev_page);
			prev->next = node->index;
		}

		current_data += node->size;
		remaining -= node->size;
		prev_page = node->index;
	}

	return first_page;
}

/**
 * Delete an entire blob chain.
 *
 * Walks the linked list and deallocates each page.
 * Safe to call with invalid (0) page index.
 *
 * @param first_page First page index of blob chain
 */
void
blob_delete(uint32_t first_page)
{
	uint32_t current = first_page;

	while (current)
	{
		blob_node *node = GET_BLOB_NODE(current);
		if (!node)
		{
			break;
		}

		uint32_t next = node->next;
		pager_delete(current);
		current = next;
	}
}

/**
 * Read a single page from blob chain.
 *
 * Used by VM for streaming blob data without loading entire blob.
 * Returns direct pointer to page data - valid until page is evicted.
 *
 * @param page_index Index of page to read
 * @return Page descriptor with data pointer and metadata
 */
blob_page
blob_read_page(uint32_t page_index)
{
	blob_page page = {0, 0, nullptr};

	blob_node *node = GET_BLOB_NODE(page_index);
	if (node)
	{
		page.next = node->next;
		page.size = node->size;
		page.data = node->data;
	}

	return page;
}

/**
 * Calculate total size of blob.
 *
 * Walks entire chain summing page sizes.
 * Used for allocation sizing before full reads.
 *
 * @param first_page First page index of blob chain
 * @return Total size in bytes, or 0 if invalid
 */
uint32_t
blob_get_size(uint32_t first_page)
{
	uint32_t total_size = 0;
	uint32_t current = first_page;

	while (current)
	{
		blob_node *node = GET_BLOB_NODE(current);
		if (!node)
		{
			break;
		}

		total_size += node->size;
		current = node->next;
	}

	return total_size;
}

/**
 * Read entire blob into contiguous memory.
 *
 * Allocates buffer from QueryArena and copies all pages.
 * Buffer lifetime is tied to arena reset (typically end of query).
 *
 * @param first_page First page index of blob chain
 * @return Buffer with complete blob data, or {nullptr, 0} on failure
 */
void *
blob_read_full(uint32_t first_page, uint64_t *size)
{
	auto stream = arena_stream_begin<query_arena>(BLOB_DATA_SIZE);

	uint32_t current = first_page;
	while (current)
	{
		blob_node *node = GET_BLOB_NODE(current);
		if (!node)
		{
			arena_stream_abandon(&stream);
			size = 0;
			return nullptr;
		}

		arena_stream_write(&stream, node->data, node->size);
		current = node->next;
	}

	*size = arena_stream_size<query_arena>(&stream);

	auto data = arena_stream_finish(&stream);
	return data;
}
