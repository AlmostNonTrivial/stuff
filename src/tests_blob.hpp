// test_blob.cpp
#include "blob.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include "arena.hpp"
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

// Helper to create memory context
static MemoryContext
create_test_context()
{
	static MemoryContext ctx;
	ctx.alloc = [](size_t size) -> void * { return arena::alloc<QueryArena>(size); };
	ctx.emit_row = nullptr;
	return ctx;
}

inline static void
test_single_page_blob()
{
	printf("Testing single-page blob...\n");

	// Single page can hold 500 bytes of data (512 - 12 header)
	const char *small_text = "This is a small blob that fits in a single page.";
	size_t		text_len = strlen(small_text);

	BlobCursor cursor;
	cursor.ctx = new MemoryContext(create_test_context());

	// Insert blob
	uint32_t blob_id = blob_cursor_insert(&cursor, (const uint8_t *)small_text, text_len);
	assert(blob_id != 0);
	printf("  Created blob with ID: %u\n", blob_id);

	// Seek to blob
	assert(blob_cursor_seek(&cursor, blob_id));

	// Read back
	Buffer result = blob_cursor_record(&cursor);
	assert(result.ptr != nullptr);
	assert(result.size == text_len);
	assert(memcmp(result.ptr, small_text, text_len) == 0);
	printf("  Successfully read back %zu bytes\n", result.size);

	// Delete
	assert(blob_cursor_delete(&cursor));
	assert(cursor.blob_id == 0);
	printf("  Blob deleted successfully\n");
}

inline static void
test_multi_page_blob()
{
	printf("\nTesting multi-page blob...\n");

	// Create text that spans exactly 3 pages
	// Page capacity: 500 bytes, so 1500 bytes = 3 pages exactly
	const char *text_1500 = generate_text(1500, 'B');

	BlobCursor cursor;
	cursor.ctx = new MemoryContext(create_test_context());

	// Insert
	uint32_t blob_id = blob_cursor_insert(&cursor, (const uint8_t *)text_1500, 1500);
	assert(blob_id != 0);
	printf("  Created 3-page blob with ID: %u\n", blob_id);

	// Read back
	assert(blob_cursor_seek(&cursor, blob_id));
	Buffer result = blob_cursor_record(&cursor);
	assert(result.size == 1500);
	assert(memcmp(result.ptr, text_1500, 1500) == 0);
	printf("  Successfully read back %zu bytes across 3 pages\n", result.size);

	// Clean up
	blob_cursor_delete(&cursor);
}

inline static void
test_boundary_cases()
{
	printf("\nTesting boundary cases...\n");

	BlobCursor cursor;
	cursor.ctx = new MemoryContext(create_test_context());

	// Test exact page boundary (500 bytes)
	const char *text_500 = generate_text(500, 'C');
	uint32_t	id1 = blob_cursor_insert(&cursor, (const uint8_t *)text_500, 500);
	assert(blob_cursor_seek(&cursor, id1));
	Buffer result1 = blob_cursor_record(&cursor);
	assert(result1.size == 500);
	printf("  500 bytes (exact page) - OK\n");

	// Test one byte over page boundary (501 bytes)
	const char *text_501 = generate_text(501, 'D');
	uint32_t	id2 = blob_cursor_insert(&cursor, (const uint8_t *)text_501, 501);
	assert(blob_cursor_seek(&cursor, id2));
	Buffer result2 = blob_cursor_record(&cursor);
	assert(result2.size == 501);
	printf("  501 bytes (spans 2 pages) - OK\n");

	// Test one byte under page boundary (499 bytes)
	const char *text_499 = generate_text(499, 'E');
	uint32_t	id3 = blob_cursor_insert(&cursor, (const uint8_t *)text_499, 499);
	assert(blob_cursor_seek(&cursor, id3));
	Buffer result3 = blob_cursor_record(&cursor);
	assert(result3.size == 499);
	printf("  499 bytes (fits in 1 page) - OK\n");
}

inline static void
test_large_blob()
{
	printf("\nTesting large blob (10KB)...\n");

	// 10KB blob = ~20 pages
	const size_t large_size = 10240;
	char	*large_text = (char*)arena::alloc<QueryArena>(large_size);//generate_text(large_size, 'L');
	memset(large_text, 'L', large_size);

	BlobCursor cursor;
	cursor.ctx = new MemoryContext(create_test_context());

	uint32_t blob_id = blob_cursor_insert(&cursor, (const uint8_t *)large_text, large_size);
	assert(blob_id != 0);

	int pages_used = (large_size + 499) / 500; // Round up
	printf("  Created %zu byte blob using ~%d pages\n", large_size, pages_used);

	// Verify content
	assert(blob_cursor_seek(&cursor, blob_id));
	Buffer result = blob_cursor_record(&cursor);
	assert(result.size == large_size);

	// Spot check some bytes
	uint8_t *data = (uint8_t *)result.ptr;
	assert(data[0] == 'L');
	assert(data[large_size / 2] == 'L');
	assert(data[large_size - 1] == 'L');
	printf("  Content verification passed\n");

	blob_cursor_delete(&cursor);
}

inline static void
test_multiple_blobs()
{
	printf("\nTesting multiple concurrent blobs...\n");

	BlobCursor cursor1, cursor2, cursor3;
	cursor1.ctx = new MemoryContext(create_test_context());
	cursor2.ctx = new MemoryContext(create_test_context());
	cursor3.ctx = new MemoryContext(create_test_context());

	// Create three different blobs
	const char *text1 = "First blob with unique content AAA";
	const char *text2 = generate_text(750, 'X'); // Spans 2 pages
	const char *text3 = LOREM_IPSUM_1K;

	uint32_t id1 = blob_cursor_insert(&cursor1, (const uint8_t *)text1, strlen(text1));
	uint32_t id2 = blob_cursor_insert(&cursor2, (const uint8_t *)text2, 750);
	uint32_t id3 = blob_cursor_insert(&cursor3, (const uint8_t *)text3, strlen(text3));

	printf("  Created 3 blobs: %u, %u, %u\n", id1, id2, id3);

	// Verify each can be read independently
	assert(blob_cursor_seek(&cursor1, id1));
	Buffer r1 = blob_cursor_record(&cursor1);
	assert(r1.size == strlen(text1));
	assert(memcmp(r1.ptr, text1, r1.size) == 0);

	assert(blob_cursor_seek(&cursor2, id2));
	Buffer r2 = blob_cursor_record(&cursor2);
	assert(r2.size == 750);

	assert(blob_cursor_seek(&cursor3, id3));
	Buffer r3 = blob_cursor_record(&cursor3);
	assert(r3.size == strlen(text3));

	printf("  All blobs verified independently\n");

	// Delete middle blob and verify others still work
	blob_cursor_delete(&cursor2);

	assert(blob_cursor_seek(&cursor1, id1));
	r1 = blob_cursor_record(&cursor1);
	assert(r1.size == strlen(text1));

	assert(blob_cursor_seek(&cursor3, id3));
	r3 = blob_cursor_record(&cursor3);
	assert(r3.size == strlen(text3));

	printf("  After deleting blob 2, blobs 1 and 3 still accessible\n");
}

inline static void
test_empty_blob()
{
	printf("\nTesting edge case: empty blob...\n");

	BlobCursor cursor;
	cursor.ctx = new MemoryContext(create_test_context());

	// Empty blob should fail or return 0
	uint32_t id = blob_cursor_insert(&cursor, nullptr, 0);
	assert(id == 0);
	printf("  Empty blob correctly rejected\n");

	// Zero-length data
	const char *empty = "";
	id = blob_cursor_insert(&cursor, (const uint8_t *)empty, 0);
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

	BlobCursor cursor;
	cursor.ctx = new MemoryContext(create_test_context());

	uint32_t id = blob_cursor_insert(&cursor, binary_data, 512);
	assert(id != 0);

	assert(blob_cursor_seek(&cursor, id));
	Buffer result = blob_cursor_record(&cursor);
	assert(result.size == 512);
	assert(memcmp(result.ptr, binary_data, 512) == 0);

	// Verify some specific bytes including nulls
	uint8_t *data = (uint8_t *)result.ptr;
	assert(data[0] == 0);
	assert(data[255] == 255);
	assert(data[256] == 0);
	assert(data[511] == 255);

	printf("  Binary data with null bytes handled correctly\n");
}

int
test_blob()
{
	// Initialize systems
	arena::init<QueryArena>(16 * 1024 * 1024);

	pager_open("test_blob.db");


	printf("=== BLOB STORAGE TESTS ===\n");

	pager_begin_transaction();

	test_single_page_blob();

	test_multi_page_blob();

	test_large_blob();

	test_multiple_blobs();

	test_empty_blob();

	test_binary_data();

	printf("\n=== ALL TESTS PASSED ===\n");

	// Cleanup
	pager_close();
	arena::shutdown<QueryArena>();
}
