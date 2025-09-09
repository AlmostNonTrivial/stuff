// test_blob.cpp
#include "../blob.hpp"
#include "../common.hpp"
#include "../pager.hpp"
#include "../arena.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

// Test data generators
static const char *
generate_text(size_t target_size, char fill_char = 'A')
{
	static char buffer[8192];
	memset(buffer, fill_char, target_size);
	buffer[target_size] = '\0';
	return buffer;
}

static const char *LOREM_IPSUM_1K = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod "
									"tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, "
									"quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo "
									"consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse "
									"cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non "
									"proident, sunt in culpa qui officia deserunt mollit anim id est laborum. "
									"Sed ut perspiciatis unde omnis iste natus error sit voluptatem accusantium "
									"doloremque laudantium, totam rem aperiam, eaque ipsa quae ab illo inventore "
									"veritatis et quasi architecto beatae vitae dicta sunt explicabo. Nemo enim "
									"ipsam voluptatem quia voluptas sit aspernatur aut odit aut fugit, sed quia "
									"consequuntur magni dolores eos qui ratione voluptatem sequi nesciunt. Neque "
									"porro quisquam est, qui dolorem ipsum quia dolor sit amet, consectetur, "
									"adipisci velit, sed quia non numquam eius modi tempora incidunt ut labore "
									"et dolore magnam aliquam quaerat voluptatem.";

inline static void
test_single_page_blob()
{
	printf("Testing single-page blob...\n");

	// Single page can hold PAGE_SIZE - 12 bytes of data
	const char *small_text = "This is a small blob that fits in a single page.";
	size_t		text_len = strlen(small_text);

	// Create blob
	uint32_t blob_id = blob_create((void *)small_text, text_len);
	assert(blob_id != 0);
	printf("  Created blob with ID: %u\n", blob_id);

	// Get size
	uint32_t size = blob_get_size(blob_id);
	assert(size == text_len);

	// Read back full blob
	uint64_t read_size;
	auto result = blob_read_full(blob_id);
	assert(result != nullptr);
	assert(read_size == text_len);
	assert(memcmp(result.data(), small_text, text_len) == 0);
	printf("  Successfully read back %lu bytes\n", read_size);

	// Test page-by-page read
	blob_page page = blob_read_page(blob_id);
	assert(page.data != nullptr);
	assert(page.size == text_len);
	assert(page.next == 0); // Single page, no next
	assert(memcmp(page.data, small_text, text_len) == 0);
	printf("  Page-by-page read successful\n");

	// Delete
	blob_delete(blob_id);
	printf("  Blob deleted successfully\n");
}

inline static void
test_multi_page_blob()
{
	printf("\nTesting multi-page blob...\n");

	// Create text that spans exactly 3 pages
	// Assuming PAGE_SIZE - 12 bytes per page
	const size_t page_capacity = PAGE_SIZE - 12;
	const size_t three_pages_size = page_capacity * 3;
	const char	*text_3pages = generate_text(three_pages_size, 'B');

	// Insert
	uint32_t blob_id = blob_create((void *)text_3pages, three_pages_size);
	assert(blob_id != 0);
	printf("  Created 3-page blob with ID: %u\n", blob_id);

	// Verify size
	assert(blob_get_size(blob_id) == three_pages_size);

	// Read back full blob
	uint64_t read_size;
	auto result = blob_read_full(blob_id);
	assert(read_size == three_pages_size);
	assert(memcmp(result.data(), text_3pages, three_pages_size) == 0);
	printf("  Successfully read back %lu bytes across 3 pages\n", read_size);

	// Test page chain navigation
	uint32_t current = blob_id;
	int		 page_count = 0;
	size_t	 total_read = 0;

	while (current)
	{
		blob_page page = blob_read_page(current);
		page_count++;
		total_read += page.size;
		current = page.next;
	}

	assert(page_count == 3);
	assert(total_read == three_pages_size);
	printf("  Page chain navigation verified: %d pages\n", page_count);

	// Clean up
	blob_delete(blob_id);
}

inline static void
test_boundary_cases()
{
	printf("\nTesting boundary cases...\n");

	const size_t page_capacity = PAGE_SIZE - 12;

	// Test exact page boundary
	const char *text_exact = generate_text(page_capacity, 'C');
	uint32_t	id1 = blob_create((void *)text_exact, page_capacity);
	assert(blob_get_size(id1) == page_capacity);


	auto result1 = blob_read_full(id1);
	assert(result1.size() == page_capacity);
	printf("  %zu bytes (exact page) - OK\n", page_capacity);
	blob_delete(id1);

	// Test one byte over page boundary
	const char *text_over = generate_text(page_capacity + 1, 'D');
	uint32_t	id2 = blob_create((void *)text_over, page_capacity + 1);
	assert(blob_get_size(id2) == page_capacity + 1);


	auto result2 = blob_read_full(id2);
	assert(result2.size() == page_capacity + 1);

	// Verify it spans 2 pages
	blob_page page1 = blob_read_page(id2);
	assert(page1.size == page_capacity);
	assert(page1.next != 0);
	blob_page page2 = blob_read_page(page1.next);
	assert(page2.size == 1);
	assert(page2.next == 0);
	printf("  %zu bytes (spans 2 pages) - OK\n", page_capacity + 1);
	blob_delete(id2);

	// Test one byte under page boundary
	const char *text_under = generate_text(page_capacity - 1, 'E');
	uint32_t	id3 = blob_create((void *)text_under, page_capacity - 1);
	assert(blob_get_size(id3) == page_capacity - 1);

	uint64_t size3;
	void *result3 = blob_read_full(id3, &size3);
	assert(size3 == page_capacity - 1);
	printf("  %zu bytes (fits in 1 page) - OK\n", page_capacity - 1);
	blob_delete(id3);
}

inline static void
test_large_blob()
{
	printf("\nTesting large blob (10KB)...\n");

	// 10KB blob
	const size_t large_size = 10240;
	char		*large_text = (char *)arena<query_arena>::alloc(large_size + 1);
	memset(large_text, 'L', large_size);
	large_text[large_size] = '\0';

	uint32_t blob_id = blob_create((void *)large_text, large_size);
	assert(blob_id != 0);

	const size_t page_capacity = PAGE_SIZE - 12;
	int			 pages_expected = (large_size + page_capacity - 1) / page_capacity;
	printf("  Created %zu byte blob using ~%d pages\n", large_size, pages_expected);

	// Verify size
	assert(blob_get_size(blob_id) == large_size);

	// Verify content
	uint64_t read_size;
	auto result = blob_read_full(blob_id, &read_size);
	assert(read_size == large_size);

	// Spot check some bytes
	uint8_t *data = (uint8_t*)result;
	assert(data[0] == 'L');
	assert(data[large_size / 2] == 'L');
	assert(data[large_size - 1] == 'L');
	printf("  Content verification passed\n");

	// Count actual pages
	uint32_t current = blob_id;
	int		 page_count = 0;
	while (current)
	{
		blob_page page = blob_read_page(current);
		page_count++;
		current = page.next;
	}
	assert(page_count == pages_expected);
	printf("  Page count verified: %d pages\n", page_count);

	blob_delete(blob_id);
}

inline static void
test_multiple_blobs()
{
	printf("\nTesting multiple concurrent blobs...\n");

	// Create three different blobs
	const char *text1 = "First blob with unique content AAA";
	const char *text2 = generate_text(750, 'X');
	const char *text3 = LOREM_IPSUM_1K;

	uint32_t id1 = blob_create((void *)text1, strlen(text1));
	uint32_t id2 = blob_create((void *)text2, 750);
	uint32_t id3 = blob_create((void *)text3, strlen(text3));

	printf("  Created 3 blobs: %u, %u, %u\n", id1, id2, id3);

	// Verify each can be read independently
	uint64_t size1;
	void *r1 = blob_read_full(id1, &size1);
	assert(size1 == strlen(text1));
	assert(memcmp(r1, text1, size1) == 0);

	uint64_t size2;
	void *r2 = blob_read_full(id2, &size2);
	assert(size2 == 750);
	assert(memcmp(r2, text2, size2) == 0);

	uint64_t size3;
	void *r3 = blob_read_full(id3, &size3);
	assert(size3 == strlen(text3));
	assert(memcmp(r3, text3, size3) == 0);

	printf("  All blobs verified independently\n");

	// Delete middle blob and verify others still work
	blob_delete(id2);

	r1 = blob_read_full(id1, &size1);
	assert(size1 == strlen(text1));
	assert(memcmp(r1, text1, size1) == 0);

	r3 = blob_read_full(id3, &size3);
	assert(size3 == strlen(text3));
	assert(memcmp(r3, text3, size3) == 0);

	printf("  After deleting blob 2, blobs 1 and 3 still accessible\n");

	// Clean up
	blob_delete(id1);
	blob_delete(id3);
}

inline static void
test_empty_blob()
{
	printf("\nTesting edge case: empty blob...\n");

	// Empty blob should return 0
	uint32_t id = blob_create((void *)nullptr, 0);
	assert(id == 0);
	printf("  Empty blob correctly rejected\n");

	// Zero-length data
	const char *empty = "";
	id = blob_create((void *)empty, 0);
	assert(id == 0);
	printf("  Zero-length blob correctly rejected\n");
}

inline static void
test_binary_data()
{
	printf("\nTesting binary data with null bytes...\n");

	// Create binary data with nulls and all byte values
	uint8_t binary_data[512];
	for (int i = 0; i < 512; i++)
	{
		binary_data[i] = i % 256;
	}

	uint32_t id = blob_create((void *)binary_data, 512);
	assert(id != 0);

	uint64_t read_size;
	auto result = blob_read_full(id, &read_size);
	assert(read_size == 512);
	assert(memcmp(result, binary_data, 512) == 0);

	// Verify some specific bytes including nulls
	uint8_t *data = (uint8_t *)result;
	assert(data[0] == 0);
	assert(data[255] == 255);
	assert(data[256] == 0);
	assert(data[511] == 255);

	printf("  Binary data with null bytes handled correctly\n");

	// Also test page-by-page reading for binary data
	blob_page page = blob_read_page(id);
	assert(page.data[0] == 0);

	blob_delete(id);
}

int
test_blob()
{
	// Initialize systems
	arena<query_arena>::init(16 * 1024 * 1024);
	pager_open("test_blob.db");

	printf("=== BLOB STORAGE TESTS ===\n");

	pager_begin_transaction();

	test_single_page_blob();
	test_multi_page_blob();
	test_boundary_cases();
	test_large_blob();
	test_multiple_blobs();
	test_empty_blob();
	test_binary_data();

	pager_commit();

	printf("\n=== ALL TESTS PASSED ===\n");

	// Cleanup
	pager_close();
	arena<query_arena>::shutdown();

	return 0;
}
