/**
 * Binary Large Object (BLOB) Storage Implementation
 *
 * This module implements a loose equivilent to sqlite's overflow page storage for large data that cannot fit
 * within standard btree nodes. BLOBs are stored as single-linked-list of pages,
 * where each page contains a portion of the data and a pointer to the next page.
 *
 * A way to properly intergrate this might be to have a BLOB column,
 * that would store the the actual index to the start of a blob chain.
 *
 * Note that both B+tree and blob's can use the pager, because the page is completely agnostic to the
 * actual format of the data.


  1. SINGLE PAGE BLOB (fits in one page)
  ----------------------------------------

	 btree column
	 ┌──────────┐
	 │ page: 42 │ ──────┐
	 └──────────┘       │
						▼
				   Page #42 (4096 bytes)
				   ┌──────────────────────────────────────┐
				   │ index: 42   (4 bytes)                │
				   │ next:  0    (4 bytes) [terminates]   │
				   │ size:  1500 (2 bytes)                │
				   │ flags: 0    (2 bytes)                │
				   ├──────────────────────────────────────┤
				   │ data: [1500 bytes of actual content] │
				   │       [............................] │
				   │       [2584 bytes unused]            │
				   └──────────────────────────────────────┘
						  12 byte header + 4084 data area


  2. MULTI-PAGE BLOB (chained across 3 pages)
  ---------------------------------------------

	 btree column
	 ┌──────────┐
	 │ page: 42 │ ──────┐
	 └──────────┘       │
						▼
				   Page #42                    Page #57                    Page #89
	 ┌─────────────────────────┐  ┌─────────────────────────┐  ┌─────────────────────────┐
	 │ index: 42               │  │ index: 57               │  │ index: 89               │
	 │ next:  57 ──────────────┼─▶  next:  89   ────────────┼──▶ next:  0  [end]         │
	 │ size:  4084             │  │ size:  4084             │  │ size:  2000             │
	 │ flags: 0                │  │ flags: 0                │  │ flags: 0                │
	 ├─────────────────────────┤  ├─────────────────────────┤  ├─────────────────────────┤
	 │ data: [4084 bytes full] │  │ data: [4084 bytes full] │  │ data: [2000 bytes]      │
	 │       [████████████████]│  │       [████████████████]│  │       [████████]        │
	 │       [████████████████]│  │       [████████████████]│  │       [        ]        │
	 └─────────────────────────┘  └─────────────────────────┘  └─────────────────────────┘
		  Total: 10,168 bytes of user data across 3 pages

 */

#include "blob.hpp"
#include "common.hpp"
#include "pager.hpp"
#include "arena.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/_types/_ssize_t.h>

#define BLOB_HEADER_SIZE 12
#define BLOB_DATA_SIZE	 (PAGE_SIZE - BLOB_HEADER_SIZE)

struct blob_node
{
	uint32_t index; // Page index of this node
	uint32_t next;	// Next page in chain (0 if last)
	uint32_t size;	// Size of data in this node
	uint8_t	 data[BLOB_DATA_SIZE];
};
static_assert(sizeof(blob_node) == PAGE_SIZE);

#define GET_BLOB_NODE(index) (reinterpret_cast<blob_node *>(pager_get(index)))

inline static blob_node *
allocate_blob_node()
{
	uint32_t   page_index = pager_new();
	blob_node *node = GET_BLOB_NODE(page_index);

	node->index = page_index;
	node->next = 0;
	node->size = 0;

	pager_mark_dirty(page_index);
	return node;
}

/**
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

	// how many pages do we need?
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
 * Walks entire chain summing page sizes.
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
 * Allocates buffer from query_arena and copies all pages.
 */
uint8_t *
blob_read_full(uint32_t first_page, size_t *size)
{
	auto stream = stream_writer<query_arena>::begin();

	uint32_t current = first_page;
	*size = 0;
	while (current)
	{
		blob_node *node = GET_BLOB_NODE(current);
		if (!node)
		{
			stream.abandon();
			return nullptr;
		}

		*size += node->size;
		stream.write(node->data, node->size);
		current = node->next;
	}

	return (uint8_t *)stream.start;
}
