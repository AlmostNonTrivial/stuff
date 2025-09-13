#pragma once
#include <cstddef>
#include <cstdint>

// Single page in a blob chain
struct blob_page
{
	uint32_t next; // Next page index (0 if last)
	uint16_t size; // Size of data in this page
	uint8_t *data; // Pointer to page data
};

uint32_t
blob_create(void *data, uint32_t size);

void
blob_delete(uint32_t first_page);

blob_page
blob_read_page(uint32_t page_index);

uint32_t
blob_get_size(uint32_t first_page);

uint8_t *
blob_read_full(uint32_t first_page, size_t *size);
