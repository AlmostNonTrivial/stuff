
#include "blob.hpp"
#include "../blob.hpp"
#include "../common.hpp"
#include "../pager.hpp"
#include "../arena.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>

#define ASSERT_PRINT(cond, ...)                                                                                        \
	do                                                                                                                 \
	{                                                                                                                  \
		if (!(cond))                                                                                                   \
		{                                                                                                              \
			fprintf(stderr, "Assertion failed: %s\n", #cond);                                                          \
			fprintf(stderr, "  at %s:%d\n", __FILE__, __LINE__);                                                       \
			fprintf(stderr, __VA_ARGS__);                                                                              \
			abort();                                                                                                   \
		}                                                                                                              \
	} while (0)

static void
dump_bytes(const uint8_t *data, size_t len, const char *label)
{
	fprintf(stderr, "%s (%zu bytes): ", label, len);
	for (size_t i = 0; i < len && i < 32; i++)
	{
		fprintf(stderr, "%02x ", data[i]);
	}
	if (len > 32)
	{
		fprintf(stderr, "...");
	}
	fprintf(stderr, "\n");
}

static void
test_empty_blob()
{
	// Empty blob should return 0
	uint32_t id = blob_create(nullptr, 0);
	ASSERT_PRINT(id == 0, "Empty blob should return ID 0, got %u\n", id);

	// Zero-length data
	const char *empty = "";
	id = blob_create((void *)empty, 0);
	ASSERT_PRINT(id == 0, "Zero-length blob should return ID 0, got %u\n", id);
}

static void
test_single_page_blob()
{
	const char *text = "Single page test data - fits comfortably in one page";
	size_t		text_len = strlen(text);

	// Create blob
	uint32_t blob_id = blob_create((void *)text, text_len);
	ASSERT_PRINT(blob_id != 0, "Failed to create blob\n");

	// Verify size
	uint32_t size = blob_get_size(blob_id);
	ASSERT_PRINT(size == text_len, "Size mismatch: expected %zu, got %u\n", text_len, size);

	// Read back and verify
	size_t	 read_size;
	uint8_t *result = blob_read_full(blob_id, &read_size);
	ASSERT_PRINT(result != nullptr, "Failed to read blob\n");
	ASSERT_PRINT(read_size == text_len, "Read size mismatch: expected %zu, got %zu\n", text_len, read_size);

	if (memcmp(result, text, text_len) != 0)
	{
		dump_bytes((const uint8_t *)text, text_len, "Expected");
		dump_bytes(result, read_size, "Got");
		ASSERT_PRINT(false, "Content mismatch\n");
	}

	// Verify single page (no chain)
	blob_page page = blob_read_page(blob_id);
	ASSERT_PRINT(page.data != nullptr, "Page data is null\n");
	ASSERT_PRINT(page.size == text_len, "Page size mismatch: expected %zu, got %u\n", text_len, page.size);
	ASSERT_PRINT(page.next == 0, "Single page should have next=0, got %u\n", page.next);

	blob_delete(blob_id);
}

static void
test_page_boundary()
{
	// Test exact page capacity
	const size_t page_capacity = PAGE_SIZE - 12;
	uint8_t		*data = (uint8_t *)arena<query_arena>::alloc(page_capacity);
	memset(data, 'B', page_capacity);

	uint32_t blob_id = blob_create(data, page_capacity);
	ASSERT_PRINT(blob_id != 0, "Failed to create boundary blob\n");

	// Should fit exactly in one page
	blob_page page = blob_read_page(blob_id);
	ASSERT_PRINT(page.size == page_capacity, "Boundary size mismatch: expected %zu, got %u\n", page_capacity,
				 page.size);
	ASSERT_PRINT(page.next == 0, "Boundary blob should fit in one page, but has next=%u\n", page.next);

	// Verify content
	size_t	 read_size;
	uint8_t *result = blob_read_full(blob_id, &read_size);
	ASSERT_PRINT(read_size == page_capacity, "Boundary read size mismatch: expected %zu, got %zu\n", page_capacity,
				 read_size);

	if (memcmp(result, data, page_capacity) != 0)
	{
		dump_bytes(data, 32, "Expected");
		dump_bytes(result, 32, "Got");
		ASSERT_PRINT(false, "Boundary content mismatch\n");
	}

	blob_delete(blob_id);
}

static void
test_multi_page_blob()
{
	// Create blob that spans exactly 3 pages
	const size_t page_capacity = PAGE_SIZE - 12;
	const size_t total_size = page_capacity * 3;
	uint8_t		*data = (uint8_t *)arena<query_arena>::alloc(total_size);

	// Fill with pattern to detect corruption
	for (size_t i = 0; i < total_size; i++)
	{
		data[i] = (uint8_t)(i % 251); // Prime number for better pattern
	}

	uint32_t blob_id = blob_create(data, total_size);
	ASSERT_PRINT(blob_id != 0, "Failed to create multi-page blob\n");

	// Verify size
	uint32_t size = blob_get_size(blob_id);
	ASSERT_PRINT(size == total_size, "Multi-page size mismatch: expected %zu, got %u\n", total_size, size);

	// Count pages and verify chain
	uint32_t current = blob_id;
	int		 page_count = 0;
	size_t	 bytes_read = 0;

	while (current)
	{
		blob_page page = blob_read_page(current);
		page_count++;
		bytes_read += page.size;

		// Verify each page except last is full
		if (page.next != 0)
		{
			ASSERT_PRINT(page.size == page_capacity, "Non-final page should be full: expected %zu, got %u\n",
						 page_capacity, page.size);
		}

		current = page.next;
	}

	ASSERT_PRINT(page_count == 3, "Expected 3 pages, got %d\n", page_count);
	ASSERT_PRINT(bytes_read == total_size, "Total bytes mismatch: expected %zu, got %zu\n", total_size, bytes_read);

	// Read full and verify content
	size_t	 read_size;
	uint8_t *result = blob_read_full(blob_id, &read_size);
	ASSERT_PRINT(read_size == total_size, "Multi-page read size mismatch: expected %zu, got %zu\n", total_size,
				 read_size);

	if (memcmp(result, data, total_size) != 0)
	{
		// Find first difference
		for (size_t i = 0; i < total_size; i++)
		{
			if (result[i] != data[i])
			{
				fprintf(stderr, "First difference at byte %zu: expected %02x, got %02x\n", i, data[i], result[i]);
				break;
			}
		}
		ASSERT_PRINT(false, "Multi-page content mismatch\n");
	}

	blob_delete(blob_id);
}

static void
test_binary_data()
{
	// Test with binary data including nulls
	uint8_t binary[512];
	for (int i = 0; i < 512; i++)
	{
		binary[i] = (uint8_t)(i % 256);
	}

	uint32_t blob_id = blob_create(binary, 512);
	ASSERT_PRINT(blob_id != 0, "Failed to create binary blob\n");

	size_t	 read_size;
	uint8_t *result = blob_read_full(blob_id, &read_size);
	ASSERT_PRINT(read_size == 512, "Binary size mismatch: expected 512, got %zu\n", read_size);

	// Check specific bytes including nulls
	ASSERT_PRINT(result[0] == 0, "Binary[0] should be 0, got %u\n", result[0]);
	ASSERT_PRINT(result[255] == 255, "Binary[255] should be 255, got %u\n", result[255]);
	ASSERT_PRINT(result[256] == 0, "Binary[256] should be 0, got %u\n", result[256]);
	ASSERT_PRINT(result[511] == 255, "Binary[511] should be 255, got %u\n", result[511]);

	if (memcmp(result, binary, 512) != 0)
	{
		dump_bytes(binary, 32, "Expected binary");
		dump_bytes(result, 32, "Got binary");
		ASSERT_PRINT(false, "Binary content mismatch\n");
	}

	blob_delete(blob_id);
}

int
test_blob()
{
	// Initialize systems
	arena<query_arena>::init(16 * 1024 * 1024);
	pager_open("test_blob.db");

	pager_begin_transaction();

	test_empty_blob();
	test_single_page_blob();
	test_page_boundary();
	test_multi_page_blob();
	test_binary_data();

	pager_commit();

	printf("blob_tests_passed\n");

	// Cleanup
	pager_close();
	arena<query_arena>::shutdown();

	return 0;
}
