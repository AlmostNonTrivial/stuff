#pragma once
#include "btree.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <future>
#include <iostream>
#include <iterator>
#include <random>
#include <thread>
#include <vector>
#include <algorithm>
#include "defs.hpp"
#include "os_layer.hpp"
#include "pager.hpp"
#include "test_utils.hpp"
#include "types.hpp"
#include "vm.hpp"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <ios>
#include <set>
#include <random>
#include <unordered_map>
#include <vector>
#include <iostream>

#define TEST_DB "test_btree.db"

#include <random>
#include <algorithm>

inline void
test_btree_sequential_ops()
{
	std::cout << "\n=== Sequential Operations ===\n";

	pager_open(TEST_DB);
	pager_begin_transaction();

	btree tree = btree_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor cursor = {.tree = &tree};

	const int COUNT = 5000;

	// Sequential forward insertion
	std::cout << "Forward sequential insert..." << std::flush;
	for (int i = 0; i < COUNT; i++)
	{
		uint32_t key = i;
		uint32_t value = i * 100;
		btree_cursor_insert(&cursor, &key, (void *)&value);
		btree_validate(&tree);
	}
	std::cout << " OK\n";

	// Verify all keys exist
	for (int i = 0; i < COUNT; i++)
	{
		uint32_t key = i;
		assert(btree_cursor_seek(&cursor, &key));
		uint32_t *val = (uint32_t *)btree_cursor_record(&cursor);
		assert(*val == i * 100);
	}

	// Sequential forward deletion
	std::cout << "Forward sequential delete..." << std::flush;
	for (int i = 0; i < COUNT / 2; i++)
	{
		uint32_t key = i;
		assert(btree_cursor_seek(&cursor, &key));
		btree_cursor_delete(&cursor);
		btree_validate(&tree);
	}
	std::cout << " OK\n";

	// Verify deleted keys don't exist
	for (int i = 0; i < COUNT / 2; i++)
	{
		uint32_t key = i;
		assert(!btree_cursor_seek(&cursor, (uint8_t*)&key));
	}

	// Verify remaining keys exist
	for (int i = COUNT / 2; i < COUNT; i++)
	{
		uint32_t key = i;
		assert(btree_cursor_seek(&cursor, &key));
	}

	// Backward sequential deletion
	std::cout << "Backward sequential delete..." << std::flush;
	for (int i = COUNT - 1; i >= COUNT / 2; i--)
	{
		uint32_t key = i;
		assert(btree_cursor_seek(&cursor, &key));
		btree_cursor_delete(&cursor);
		btree_validate(&tree);
	}
	std::cout << " OK\n";

	// Tree should be empty
	assert(!btree_cursor_first(&cursor));

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

inline void
test_btree_random_ops()
{
	std::cout << "\n=== Random Operations ===\n";

	pager_open(TEST_DB);
	pager_begin_transaction();

	btree tree = btree_create(TYPE_U32, sizeof(uint64_t), true);
	bt_cursor cursor = {.tree = &tree};

	const int COUNT = 5000;

	// Generate unique keys and values
	std::vector<std::pair<uint32_t, uint64_t>> data;
	for (int i = 0; i < COUNT; i++)
	{
		data.push_back({i, (uint64_t)i * 1000});
	}

	// Shuffle for random insertion order
	std::mt19937 rng(42); // Deterministic seed
	std::shuffle(data.begin(), data.end(), rng);

	// Random insertions
	std::cout << "Random insert..." << std::flush;
	for (auto &[key, value] : data)
	{
		btree_cursor_insert(&cursor, &key, (void *)&value);
		btree_validate(&tree);
	}
	std::cout << " OK (" << COUNT << " unique keys)\n";

	// Verify all entries
	for (auto &[key, value] : data)
	{
		assert(btree_cursor_seek(&cursor, &key));
		uint64_t *val = (uint64_t *)btree_cursor_record(&cursor);
		assert(*val == value);
	}

	// Create list of keys for deletion
	std::vector<uint32_t> keys_to_delete;
	for (auto &[key, _] : data)
	{
		keys_to_delete.push_back(key);
	}

	// Delete half the keys randomly
	std::shuffle(keys_to_delete.begin(), keys_to_delete.end(), rng);
	int delete_count = keys_to_delete.size() / 2;

	std::cout << "Random delete..." << std::flush;
	std::set<uint32_t> deleted_keys;
	for (int i = 0; i < delete_count; i++)
	{
		uint32_t key = keys_to_delete[i];
		assert(btree_cursor_seek(&cursor, &key));
		btree_cursor_delete(&cursor);
		btree_validate(&tree);
		deleted_keys.insert(key);
	}
	std::cout << " OK (deleted keys: " << delete_count << ")\n";

	// Verify correct keys remain
	for (auto &[key, value] : data)
	{
		if (deleted_keys.find(key) == deleted_keys.end())
		{
			// Should exist
			assert(btree_cursor_seek(&cursor, &key));
			uint64_t *val = (uint64_t *)btree_cursor_record(&cursor);
			assert(*val == value);
		}
		else
		{
			// Should not exist
			assert(!btree_cursor_seek(&cursor, &key));
		}
	}

	// Delete remaining keys
	std::cout << "Delete remaining..." << std::flush;
	for (int i = delete_count; i < keys_to_delete.size(); i++)
	{
		uint32_t key = keys_to_delete[i];
		assert(btree_cursor_seek(&cursor, &key));
		btree_cursor_delete(&cursor);
		btree_validate(&tree);
	}
	std::cout << " OK\n";

	// Tree should be empty
	assert(!btree_cursor_first(&cursor));

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

inline void
test_btree_mixed_ops()
{
	std::cout << "\n=== Mixed Operations ===\n";

	std::srand(123);

	pager_open(TEST_DB);
	pager_begin_transaction();

	btree tree = btree_create(TYPE_U64, sizeof(uint32_t), true);
	bt_cursor cursor = {.tree = &tree};

	std::set<uint64_t> keys_in_tree;
	const int		   ITERATIONS = 1000;
	const uint64_t	   KEY_RANGE = 1000;

	std::cout << "Mixed insert/delete pattern..." << std::flush;

	for (int i = 0; i < ITERATIONS; i++)
	{
		// Weighted operations: 60% insert, 40% delete
		int op = std::rand() % 100;

		if (op < 60 || keys_in_tree.empty())
		{ // Insert
			uint64_t key = std::rand() % KEY_RANGE;
			uint32_t value = key * 1000;

			btree_cursor_insert(&cursor, &key, (void *)&value);
			keys_in_tree.insert(key);
			btree_validate(&tree);
		}
		else
		{ // Delete
			// Pick random existing key
			auto it = keys_in_tree.begin();
			std::advance(it, std::rand() % keys_in_tree.size());
			uint64_t key = *it;

			assert(btree_cursor_seek(&cursor, &key));
			btree_cursor_delete(&cursor);
			keys_in_tree.erase(key);
			btree_validate(&tree);
		}

		// Periodically verify tree contents
		if (i % 50 == 0)
		{
			for (uint64_t key : keys_in_tree)
			{
				assert(btree_cursor_seek(&cursor, &key));
				uint32_t *val = (uint32_t *)btree_cursor_record(&cursor);
				assert(*val == key * 1000);
			}
		}
	}

	std::cout << " OK (final size: " << keys_in_tree.size() << ")\n";

	// Clean up remaining keys
	std::cout << "Cleanup..." << std::flush;
	for (uint64_t key : keys_in_tree)
	{
		assert(btree_cursor_seek(&cursor, &key));
		btree_cursor_delete(&cursor);
		btree_validate(&tree);
	}
	std::cout << " OK\n";

	assert(!btree_cursor_first(&cursor));

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

inline void
test_btree_edge_cases()
{
	std::cout << "\n=== Edge Cases ===\n";

	pager_open(TEST_DB);
	pager_begin_transaction();

	btree tree = btree_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor cursor = {.tree = &tree};

	// Delete from empty tree
	std::cout << "Delete from empty..." << std::flush;
	uint32_t key = 42;
	assert(!btree_cursor_seek(&cursor, &key));
	assert(!btree_cursor_delete(&cursor));
	btree_validate(&tree);
	std::cout << " OK\n";

	// Single element operations
	std::cout << "Single element..." << std::flush;
	uint32_t value = 100;
	btree_cursor_insert(&cursor, &key, (void *)&value);
	btree_validate(&tree);
	assert(btree_cursor_seek(&cursor, &key));
	btree_cursor_delete(&cursor);
	btree_validate(&tree);
	assert(!btree_cursor_first(&cursor));
	std::cout << " OK\n";

	// Boundary key values
	std::cout << "Boundary values..." << std::flush;
	uint32_t min_key = 0;
	uint32_t max_key = UINT32_MAX;

	btree_cursor_insert(&cursor, &min_key, (void *)&value);
	btree_validate(&tree);
	btree_cursor_insert(&cursor, &max_key, (void *)&value);
	btree_validate(&tree);

	assert(btree_cursor_seek(&cursor, &min_key));
	assert(btree_cursor_seek(&cursor, &max_key));

	btree_cursor_delete(&cursor);
	btree_validate(&tree);
	assert(btree_cursor_seek(&cursor, &min_key));
	btree_cursor_delete(&cursor);
	btree_validate(&tree);

	std::cout << " OK\n";

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

inline void
test_btree_stress()
{
	std::cout << "\n========== B+Tree Stress Test ==========\n";

	// Test sequential operations
	test_btree_sequential_ops();

	// Test random operations
	test_btree_random_ops();

	// Test mixed operations
	test_btree_mixed_ops();

	// Test edge cases
	test_btree_edge_cases();

	std::cout << "\n========== All B+Tree stress tests passed! ==========\n";
}

// ============================================================================
// B+Tree Integration Tests - Four focused tests
// ============================================================================

// // Test 1: U32+U64 composite (main use case)
inline void test_btree_u32_u64() {
    printf("Test 1: U32+U64 composite keys\n");
    pager_open(TEST_DB);
    pager_begin_transaction();

    DataType key_type = make_dual(TYPE_U32, TYPE_U64);
    btree tree = btree_create(key_type, 0, true);
    bt_cursor cursor = {.tree = &tree};

    uint8_t key_data[12];
    uint8_t empty_value = 0;

    // Insert user+timestamp pairs
    for (uint32_t user = 1; user <= 5; user++) {
        for (uint64_t time = 100; time <= 103; time++) {
            pack_dual(key_data, TYPE_U32, &user, TYPE_U64, &time);
            assert(btree_cursor_insert(&cursor, key_data, &empty_value));
        }
    }

    // Range query for user 3

    uint32_t a = 3;
    uint64_t  b = 0;
    pack_dual(key_data, TYPE_U32, &a, TYPE_U64, &b);

    assert(btree_cursor_seek(&cursor, key_data, GE));

    int count = 0;
    do {
        void* found = btree_cursor_key(&cursor);
        uint32_t a;
        uint64_t b;
        unpack_dual(key_type, found, &a, &b);
        if (a != 3) break;
        count++;
    } while (btree_cursor_next(&cursor));

    assert(count == 4);
    printf("  Found %d entries for user 3\n", count);

    pager_rollback();
    pager_close();
    os_file_delete(TEST_DB);
}

// // Test 2: U16+U16 composite
// inline void test_btree_u16_u16() {
//     printf("Test 2: U16+U16 composite keys\n");
//     pager_open(TEST_DB);
//     pager_begin_transaction();

//     DataType key_type = TYPE_MULTI_U16_U16;
//     BPlusTree tree = btree_create(key_type, 0, true);
//     BPtCursor cursor = {.tree = &tree};

//     uint8_t key_data[4];
//     uint8_t empty_value = 0;

//     for (uint16_t dept = 10; dept <= 12; dept++) {
//         for (uint16_t emp = 1000; emp <= 1002; emp++) {
//             pack_u16_u16(key_data, dept, emp);
//             assert(btree_cursor_insert(&cursor, key_data, &empty_value));
//         }
//     }

//     pack_u16_u16(key_data, 11, 0);
//     assert(btree_cursor_seek(&cursor, key_data, GE));

//     int count = 0;
//     do {
//         uint8_t* found = btree_cursor_key(&cursor);
//         uint16_t found_dept = extract_u16_at(found, 0);
//         if (found_dept != 11) break;
//         count++;
//     } while (btree_cursor_next(&cursor));

//     assert(count == 3);
//     printf("  Found %d entries for dept 11\n", count);

//     pager_rollback();
//     pager_close();
//     os_file_delete(TEST_DB);
// }

// // Test 3: U8+U8 composite
// inline void test_btree_u8_u8() {
//     printf("Test 3: U8+U8 composite keys\n");
//     pager_open(TEST_DB);
//     pager_begin_transaction();

//     DataType key_type = TYPE_MULTI_U8_U8;
//     BPlusTree tree = btree_create(key_type, 0, true);
//     BPtCursor cursor = {.tree = &tree};

//     uint8_t key_data[2];
//     uint8_t empty_value = 0;

//     for (uint8_t cat = 1; cat <= 3; cat++) {
//         for (uint8_t pri = 10; pri <= 12; pri++) {
//             pack_u8_u8(key_data, cat, pri);
//             assert(btree_cursor_insert(&cursor, key_data, &empty_value));
//         }
//     }

//     // Verify lexicographic ordering
//     assert(btree_cursor_first(&cursor));

//     uint8_t expected[][2] = {{1,10}, {1,11}, {1,12}, {2,10}, {2,11}, {2,12}, {3,10}, {3,11}, {3,12}};

//     for (int i = 0; i < 9; i++) {
//         uint8_t* found = btree_cursor_key(&cursor);
//         assert(extract_u8_at(found, 0) == expected[i][0]);
//         assert(extract_u8_at(found, 1) == expected[i][1]);
//         if (i < 8) assert(btree_cursor_next(&cursor));
//     }

//     printf("  Verified lexicographic ordering for 9 entries\n");

//     pager_rollback();
//     pager_close();
//     os_file_delete(TEST_DB);
// }

// // Test 4: Mixed size U32+U64
// inline void test_btree_u32_u64() {
//     printf("Test 4: U32+U64 composite keys (mixed sizes)\n");
//     pager_open(TEST_DB);
//     pager_begin_transaction();

//     DataType key_type = make_dual(TYPE_U32, TYPE_U64);
//     BTree tree = btree_create(key_type, 0, true);
//     BtCursor cursor = {.tree = &tree};

//     uint8_t key_data[12];
//     uint8_t empty_value = 0;

//     for (uint32_t order = 100; order <= 102; order++) {
//         for (uint64_t ts = 1000000000000ULL; ts <= 1000000000002ULL; ts++) {
//             pack_dual(key_data, TYPE_U32, &order, TYPE_U64, &ts);
//             assert(btree_cursor_insert(&cursor, key_data, &empty_value));
//         }
//     }

//     // Test that first component dominates
//     uint8_t key_small[12], key_large[12];
//     // pack_u32_u64(key_small, 101, 0ULL);
//     // pack_u32_u64(key_large, 100, 0xFFFFFFFFFFFFFFFFULL);

//     assert(type_compare(key_type, key_large, key_small) < 0);  // (100,MAX) < (101,0)
//     printf("  Verified first component dominates: (100,MAX) < (101,0)\n");

//     pager_rollback();
//     pager_close();
//     os_file_delete(TEST_DB);
// }


// Test with maximum-size records
inline void
test_btree_large_records()
{
	std::cout << "\n=== Large Record Tests ===\n";

	pager_open(TEST_DB);
	pager_begin_transaction();

	// Create tree with very large records (forces MIN_ENTRY_COUNT)
	const uint32_t LARGE_RECORD = PAGE_SIZE / 4; // Close to page size limit
	btree	   tree = btree_create(TYPE_U32, LARGE_RECORD, true);
	bt_cursor	   cursor = {.tree = &tree};

	// Should have minimum keys per node


	uint8_t large_data[LARGE_RECORD];

	// Insert enough to force multiple levels
	for (uint32_t i = 0; i < 30; i++)
	{
		memset(large_data, i, LARGE_RECORD);
		assert(btree_cursor_insert(&cursor, &i, large_data));
		btree_validate(&tree);
	}

	// Verify data integrity
	for (uint32_t i = 0; i < 30; i++)
	{
		assert(btree_cursor_seek(&cursor, &i));
		uint8_t*data = (uint8_t*)btree_cursor_record(&cursor);
		assert(data[0] == i && data[LARGE_RECORD - 1] == i);
	}

	std::cout << "Large records OK\n";

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

// Test multiple cursors on same tree
inline void
test_btree_multiple_cursors()
{
	std::cout << "\n=== Multiple Cursor Tests ===\n";

	pager_open(TEST_DB);
	pager_begin_transaction();

	btree tree = btree_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor cursor1 = {.tree = &tree};
	bt_cursor cursor2 = {.tree = &tree};
	bt_cursor cursor3 = {.tree = &tree};

	// Insert data
	for (uint32_t i = 0; i < 100; i++)
	{
		uint32_t value = i * 100;
		assert(btree_cursor_insert(&cursor1, &i, (void *)&value));
	}

	// Position cursors at different locations
	assert(btree_cursor_first(&cursor1));

	uint32_t key = 50;
	assert(btree_cursor_seek(&cursor2, &key));

	assert(btree_cursor_last(&cursor3));

	// Verify each cursor maintains independent position
	uint32_t *key1 = (uint32_t *)btree_cursor_key(&cursor1);
	uint32_t *key2 = (uint32_t *)btree_cursor_key(&cursor2);
	uint32_t *key3 = (uint32_t *)btree_cursor_key(&cursor3);

	assert(*key1 == 0);
	assert(*key2 == 50);
	assert(*key3 == 99);

	// Navigate cursors independently
	assert(btree_cursor_next(&cursor1));
	assert(btree_cursor_previous(&cursor3));

	key1 = (uint32_t *)btree_cursor_key(&cursor1);
	key3 = (uint32_t *)btree_cursor_key(&cursor3);

	assert(*key1 == 1);
	assert(*key3 == 98);

	std::cout << "Multiple cursors OK\n";

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

// Test page eviction scenarios (requires small cache)
inline void
test_btree_page_eviction()
{
	std::cout << "\n=== Page Eviction Tests ===\n";

	if (MAX_CACHE_ENTRIES > 10)
	{
		std::cout << "Skipping (cache too large)\n";
		return;
	}

	pager_open(TEST_DB);
	pager_begin_transaction();

	btree tree = btree_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor cursor = {.tree = &tree};

	// Insert enough data to create many pages
	for (uint32_t i = 0; i < 1000; i++)
	{
		uint32_t value = i;
		assert(btree_cursor_insert(&cursor, &i, (void *)&value));
	}

	// Force cache thrashing by accessing in pattern
	for (int iter = 0; iter < 3; iter++)
	{
		// Forward scan
		assert(btree_cursor_first(&cursor));
		int count = 0;
		do
		{
			count++;
		} while (btree_cursor_next(&cursor) && count < 100);

		// Backward scan
		assert(btree_cursor_last(&cursor));
		count = 0;
		do
		{
			count++;
		} while (btree_cursor_previous(&cursor) && count < 100);

		// Random access
		for (int i = 0; i < 50; i++)
		{
			uint32_t key = (i * 37) % 1000;
			assert(btree_cursor_seek(&cursor, &key));
		}
	}

	btree_validate(&tree);
	std::cout << "Page eviction OK\n";

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

// Test VARCHAR keys with collation-like behavior
inline void
test_btree_varchar_collation()
{
	std::cout << "\n=== VARCHAR Collation Tests ===\n";

	pager_open(TEST_DB);
	pager_begin_transaction();

	btree tree = btree_create(TYPE_CHAR32, sizeof(uint32_t), true);
	bt_cursor cursor = {.tree = &tree};

	// Test strings that expose comparison edge cases
	const char *test_strings[] = {
		"",		// Empty
		" ",	// Space
		"  ",	// Multiple spaces
		"A",	// Upper
		"a",	// Lower
		"AA",	// Double upper
		"Aa",	// Mixed case
		"aA",	// Mixed case reverse
		"aa",	// Double lower
		"a b",	// With space
		"a  b", // Double space
		"a\tb", // With tab
		"1",	// Digit
		"10",	// Multi-digit
		"2",	// Another digit
		"abc",	// Lowercase word
		"ABC",	// Uppercase word
		"aBc",	// Mixed case word
		"\x01", // Control char
		"\xFF", // High byte
	};

	// Insert all strings
	for (int i = 0; i < sizeof(test_strings) / sizeof(test_strings[0]); i++)
	{
		char key[32] = {0};
		strncpy(key, test_strings[i], 31);
		uint32_t value = i;
		btree_cursor_insert(&cursor, key, (void *)&value);
	}

	// Collect sorted order from tree
	std::vector<std::string> tree_order;
	if (btree_cursor_first(&cursor))
	{
		do
		{
			char *key = (char *)btree_cursor_key(&cursor);
			tree_order.push_back(std::string(key, strnlen(key, 32)));
		} while (btree_cursor_next(&cursor));
	}

	// Verify ordering is consistent
	for (size_t i = 1; i < tree_order.size(); i++)
	{
		assert(memcmp(tree_order[i - 1].c_str(), tree_order[i].c_str(), 32) < 0);
	}

	std::cout << "VARCHAR collation OK (" << tree_order.size() << " unique keys)\n";

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

// Add to test_btree()
inline void
test_btree_extended()
{
	std::cout << "\n========== Extended B+Tree Tests ==========\n";

	test_btree_large_records();

	test_btree_multiple_cursors();
	test_btree_page_eviction();
	test_btree_varchar_collation();

	std::cout << "\n========== All extended tests passed! ==========\n";
}

inline void
test_update_parent_keys_condition()
{
	std::cout << "\n=== Sequential Operations ===\n";

	pager_open(TEST_DB);
	pager_begin_transaction();

	btree tree = btree_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor cursor = {.tree = &tree};

	const int COUNT = tree.leaf_max_keys * 3;

	std::vector<uint32_t> keys;
	std::cout << "Forward sequential insert..." << std::flush;
	for (int i = 0; i < COUNT; i++)
	{
		keys.push_back(i);
		btree_cursor_insert(&cursor, &i, (void *)&i);
	}

	uint32_t key = 150;
	btree_cursor_seek(&cursor, &key);
	for (int i = 0; i < 182 - 150; i++)
	{
		btree_cursor_delete(&cursor);
	}

	// btree_print(&tree);
	std::cout << " OK\n";

	;

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}
inline void
test_merge_empty_root()
{
	std::cout << "\n=== Sequential Operations ===\n";

	pager_open(TEST_DB);
	pager_begin_transaction();

	btree tree = btree_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor cursor = {.tree = &tree};

	const int COUNT = tree.leaf_max_keys + 1;

	std::cout << "Forward sequential insert..." << std::flush;
	for (int i = 0; i < COUNT; i++)
	{
		btree_cursor_insert(&cursor, &i, (void *)&i);
	}
	uint32_t key = 30;
	btree_print(&tree);
	btree_cursor_seek(&cursor, &key);
	btree_cursor_delete(&cursor);

	// uint32_t key = 150;
	// btree_cursor_seek(&cursor, &key);
	// for (int i = 0; i < 182 - 150; i++)
	// {
	// 	btree_cursor_delete(&cursor);
	// }

	// // btree_print(&tree);
	// std::cout << " OK\n";

	;

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

inline void
test_btree_single_key_leaf_delete()
{
	std::cout << "\n=== Single Key Leaf Delete Test ===\n";

	pager_open(TEST_DB);
	pager_begin_transaction();

	btree tree = btree_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor cursor = {.tree = &tree};

	// Insert enough keys to create internal nodes and multiple leaves
	// We need a tree structure where we can isolate a leaf with 1 key

	// First, fill up the root to force a split
	for (uint32_t i = 0; i <= tree.leaf_max_keys; i++)
	{
		uint32_t value = i;
		btree_cursor_insert(&cursor, &i, (void *)&value);
	}

	// Now we have an internal root with 2 leaf children
	// Delete all but one key from the left leaf
	for (uint32_t i = 1; i < tree.leaf_min_keys; i++)
	{
		assert(btree_cursor_seek(&cursor, &i));
		btree_cursor_delete(&cursor);
	}

	// The left leaf should now have exactly min_keys
	// Delete one more to trigger underflow, but not the first key yet
	uint32_t key_to_delete = tree.leaf_min_keys - 1;
	if (key_to_delete > 0)
	{
		assert(btree_cursor_seek(&cursor, &key_to_delete));
		btree_cursor_delete(&cursor);
	}

	// Now delete the first key (index 0) from a leaf with 1 key
	// This should trigger if_43 in update_parent_keys
	uint32_t first_key = 0;
	assert(btree_cursor_seek(&cursor, &first_key));
	btree_cursor_delete(&cursor);

	btree_validate(&tree);

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

inline void
test_btree_collapse_root()
{
	std::cout << "\n=== Collapse Root Test ===\n";

	pager_open(TEST_DB);
	pager_begin_transaction();

	btree tree = btree_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor cursor = {.tree = &tree};

	// Insert just enough to create a 2-level tree
	for (uint32_t i = 0; i <= tree.leaf_max_keys; i++)
	{
		btree_cursor_insert(&cursor, &i, (void *)&i);
	}

	// Now delete everything to collapse the tree
	for (uint32_t i = 0; i <= tree.leaf_max_keys; i++)
	{
		assert(btree_cursor_seek(&cursor, &i));
		btree_cursor_delete(&cursor);
		btree_validate(&tree);
	}

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

inline void
test_btree_deep_tree_coverage()
{
	std::cout << "\n=== Deep Tree Coverage Test ===\n";

	pager_open(TEST_DB);
	pager_begin_transaction();

	// Create tree with 64-byte records to force smaller node capacity
	const uint32_t RECORD_SIZE = 64;
	btree	   tree = btree_create(TYPE_U32, RECORD_SIZE, true);
	bt_cursor	   cursor = {.tree = &tree};

	std::cout << "Tree config: leaf_max=" << tree.leaf_max_keys << ", internal_max=" << tree.internal_max_keys << "\n";

	// Insert enough keys to create a deep tree (at least 3 levels)
	const int KEY_COUNT = 500;
	uint8_t	  record_data[RECORD_SIZE];

	std::cout << "Building deep tree..." << std::flush;
	for (int i = 0; i < KEY_COUNT; i++)
	{
		uint32_t key = i;
		memset(record_data, i % 256, RECORD_SIZE);
		assert(btree_cursor_insert(&cursor, &key, record_data));
	}
	std::cout << " OK\n";

	// Test cursor has_next and has_previous (if_100, if_101)
	std::cout << "Testing cursor helpers..." << std::flush;
	assert(btree_cursor_first(&cursor));
	assert(btree_cursor_has_next(&cursor));		 // Should hit if_100
	assert(!btree_cursor_has_previous(&cursor)); // Should test if_101

	assert(btree_cursor_last(&cursor));
	assert(!btree_cursor_has_next(&cursor));
	assert(btree_cursor_has_previous(&cursor)); // Should hit if_101
	std::cout << " OK\n";

	// Navigate to trigger previous leaf movement (if_98, if_99)
	std::cout << "Testing leaf navigation..." << std::flush;
	// Find a key that's at the start of a non-first leaf
	uint32_t target_key = tree.leaf_max_keys; // Should be first key of second leaf
	assert(btree_cursor_seek(&cursor, &target_key));
	assert(btree_cursor_previous(&cursor)); // Should move to previous leaf (if_98, if_99)
	std::cout << " OK\n";

	// Trigger if_37: Delete first key of a non-leftmost leaf
	// The key should be used as a separator in parent
	std::cout << "Testing parent key update (if_37)..." << std::flush;

	// Navigate to second leaf's first key
	target_key = tree.leaf_max_keys;
	assert(btree_cursor_seek(&cursor, &target_key));

	// This key should be a separator in the parent
	// Delete it to trigger parent key update
	assert(btree_cursor_delete(&cursor));
	btree_validate(&tree);
	std::cout << " OK\n";

	// Test cursor operations on invalid cursor (various if_7x, if_8x, if_9x)
	std::cout << "Testing invalid cursor operations..." << std::flush;
	bt_cursor invalid_cursor = {.tree = &tree};
	invalid_cursor.state = BT_CURSOR_INVALID;

	assert(btree_cursor_key(&invalid_cursor) == nullptr);		// if_75
	assert(btree_cursor_record(&invalid_cursor) == nullptr);	// if_77
	assert(!btree_cursor_delete(&invalid_cursor));				// if_82
	assert(!btree_cursor_update(&invalid_cursor, record_data)); // if_89
	assert(!btree_cursor_next(&invalid_cursor));				// if_90
	assert(!btree_cursor_previous(&invalid_cursor));			// if_95
	std::cout << " OK\n";

	// Test cursor on empty tree (if_79)
	std::cout << "Testing empty tree seek..." << std::flush;
	btree empty_tree = btree_create(TYPE_U32, sizeof(uint32_t), false); // Don't init
	bt_cursor empty_cursor = {.tree = &empty_tree};
	uint32_t  test_key = 42;
	assert(!btree_cursor_seek(&empty_cursor, &test_key)); // if_79
	std::cout << " OK\n";

	// Test seek_cmp for coverage (if_72, if_73)
	std::cout << "Testing seek_cmp..." << std::flush;
	uint32_t cmp_key = 250;
	assert(btree_cursor_seek(&cursor, &cmp_key, GE)); // if_72 for exact match case

	// Test with key that doesn't exist
	uint32_t missing_key = KEY_COUNT + 100;
	assert(btree_cursor_seek(&cursor, &missing_key, LE)); // Should iterate and hit if_73
	std::cout << " OK\n";

	// Test node fault conditions (if_91, if_96)
	std::cout << "Testing fault conditions..." << std::flush;
	bt_cursor fault_cursor = {.tree = &tree};
	fault_cursor.state = BT_CURSOR_VALID;
	fault_cursor.leaf_page = 999999; // Invalid page
	fault_cursor.leaf_index = 0;

	assert(!btree_cursor_next(&fault_cursor));	   // if_91
	fault_cursor.state = BT_CURSOR_VALID;			   // Reset state
	assert(!btree_cursor_previous(&fault_cursor)); // if_96
	std::cout << " OK\n";

	// Test cursor with out-of-bounds index (if_76, if_78)
	std::cout << "Testing out-of-bounds cursor..." << std::flush;
	assert(btree_cursor_first(&cursor));
	cursor.leaf_index = 999;							 // Way out of bounds
	assert(btree_cursor_key(&cursor) == nullptr);	 // if_76
	assert(btree_cursor_record(&cursor) == nullptr); // if_78
	std::cout << " OK\n";

	// Test node changes after delete (if_84)
	std::cout << "Testing node change after delete..." << std::flush;
	// This is tricky - need to delete such that the node itself changes
	// Usually happens during merges where the node gets deallocated
	// Set up a scenario with minimal keys
	btree small_tree = btree_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor small_cursor = {.tree = &small_tree};

	// Insert just enough to split once
	for (uint32_t i = 0; i <= small_tree.leaf_max_keys; i++)
	{
		uint32_t val = i;
		btree_cursor_insert(&small_cursor, &i, (void *)&val);
	}

	// Delete to cause merge that changes node structure
	for (uint32_t i = 1; i < small_tree.leaf_min_keys; i++)
	{
		assert(btree_cursor_seek(&small_cursor, &i));
		btree_cursor_delete(&small_cursor);
	}
	std::cout << " OK\n";

	// Clear the trees (if_64, if_65)
	std::cout << "Testing tree clear..." << std::flush;
	assert(btree_clear(&tree));		  // if_64 (recursive clear)
	assert(btree_clear(&empty_tree)); // if_65 (empty tree clear)
	assert(btree_clear(&small_tree));
	std::cout << " OK\n";

	std::cout << "All coverage paths tested!\n";

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}
inline void
test_btree_remaining_coverage()
{
	std::cout << "\n=== Remaining Coverage Tests ===\n";

	// Test 1: if_55, if_56 - Collapse internal root with single child
	{
		std::cout << "Test collapse internal root..." << std::flush;
		pager_open(TEST_DB);
		pager_begin_transaction();

		btree tree = btree_create(TYPE_U32, sizeof(uint32_t), true);
		bt_cursor cursor = {.tree = &tree};

		// Build a 3-level tree
		for (uint32_t i = 0; i < 200; i++)
		{
			uint32_t val = i;
			btree_cursor_insert(&cursor, &i, (void *)&val);
		}

		// Delete everything except keys that keep one subtree
		// This should eventually collapse an internal root
		for (uint32_t i = 0; i < 199; i++)
		{
			if (btree_cursor_seek(&cursor, &i))
			{
				btree_cursor_delete(&cursor);
				btree_validate(&tree);
			}
		}

		pager_rollback();
		pager_close();
		os_file_delete(TEST_DB);
		std::cout << " OK\n";
	}

	// Test 2: if_37 - Update parent keys when deleted key matches separator
	{
		// std::cout << "Test parent key update..." << std::flush;
		// pager_open(TEST_DB);
		// pager_begin_transaction();

		// BPlusTree tree = btree_create(TYPE_U32, sizeof(uint32_t), true);
		// BPtCursor cursor = {.tree = &tree};

		// // Insert enough to create internal nodes
		// for (uint32_t i = 0; i < 100; i++)
		// {
		// 	uint32_t val = i * 10;
		// 	btree_cursor_insert(&cursor, &i, (void *)&val);
		// }

		// // Find the first key of the second leaf - this should be a separator
		// uint32_t separator_key = tree.leaf_max_keys;

		// // Delete this key to trigger parent update
		// assert(btree_cursor_seek(&cursor, &separator_key));
		// btree_cursor_delete(&cursor);

		// pager_rollback();
		// pager_close();
		// os_file_delete(TEST_DB);
		// std::cout << " OK\n";
	}

	// Test 3: if_98, if_99 - Previous navigation across leaf boundary
	{
		std::cout << "Test previous leaf navigation..." << std::flush;
		pager_open(TEST_DB);
		pager_begin_transaction();

		btree tree = btree_create(TYPE_U32, sizeof(uint32_t), true);
		bt_cursor cursor = {.tree = &tree};

		// Insert enough for multiple leaves
		for (uint32_t i = 0; i < tree.leaf_max_keys * 3; i++)
		{
			uint32_t val = i;
			btree_cursor_insert(&cursor, &i, (void *)&val);
		}

		while (btree_cursor_previous(&cursor))
			;

		// Position at start of second leaf
		uint32_t key = tree.leaf_max_keys;
		assert(btree_cursor_seek(&cursor, &key));

		// Move to previous should cross leaf boundary
		assert(btree_cursor_previous(&cursor)); // Should trigger if_98, if_99

		// Verify we're at the end of first leaf
		uint32_t *current = (uint32_t *)btree_cursor_key(&cursor);
		assert(*current == tree.leaf_max_keys - 1);

		pager_rollback();
		pager_close();
		os_file_delete(TEST_DB);
		std::cout << " OK\n";
	}

	// Test 4: if_73 - Null key in seek_cmp loop
	{
		std::cout << "Test seek_cmp with gaps..." << std::flush;
		pager_open(TEST_DB);
		pager_begin_transaction();

		btree tree = btree_create(TYPE_U32, sizeof(uint32_t), true);
		bt_cursor cursor = {.tree = &tree};

		// Insert sparse keys
		uint32_t keys[] = {10, 20, 30, 40, 50};
		for (int i = 0; i < 5; i++)
		{
			uint32_t val = keys[i];
			btree_cursor_insert(&cursor, &keys[i], (void *)&val);
		}

		// Seek with comparison to non-existent key
		uint32_t target = 25;

		// Position cursor in invalid state first
		cursor.state = BT_CURSOR_INVALID;

		// This should iterate and hit if_73 when cursor key returns null
		assert(btree_cursor_seek(&cursor, &target, GE));

		pager_rollback();
		pager_close();
		os_file_delete(TEST_DB);
		std::cout << " OK\n";
	}

	// Test 5: if_68 - Fault in cursor_move_in_subtree
	{
		std::cout << "Test cursor subtree fault..." << std::flush;
		pager_open(TEST_DB);
		pager_begin_transaction();

		btree tree = btree_create(TYPE_U32, sizeof(uint32_t), true);

		// Create a tree with internal nodes
		bt_cursor cursor = {.tree = &tree};
		uint32_t  i = 0;
		btree_cursor_seek(&cursor, &i, GE);

		pager_rollback();
		pager_close();
		os_file_delete(TEST_DB);
		std::cout << " OK\n";
	}

	// Test 6: if_84 - Node changes after delete
	{
		std::cout << "Test node change on delete..." << std::flush;
		pager_open(TEST_DB);
		pager_begin_transaction();

		btree tree = btree_create(TYPE_U32, sizeof(uint32_t), true);
		bt_cursor cursor = {.tree = &tree};

		// Create minimal tree that will merge on delete
		for (uint32_t i = 0; i <= tree.leaf_max_keys + 1; i++)
		{
			uint32_t val = i;
			btree_cursor_insert(&cursor, &i, (void *)&val);
		}

		// Position cursor on a key in the right leaf
		uint32_t target = tree.leaf_max_keys + 1;
		assert(btree_cursor_seek(&cursor, &target));

		// Delete keys to force merge that will deallocate the node cursor is on
		for (uint32_t i = 1; i < tree.leaf_max_keys; i++)
		{
			bt_cursor temp_cursor = {.tree = &tree};
			if (btree_cursor_seek(&temp_cursor, &i))
			{
				btree_cursor_delete(&temp_cursor);
			}
		}

		// Now delete with our positioned cursor - node should change
		btree_cursor_delete(&cursor);
		// assert(cursor.state == BPT_CURSOR_INVALID); // Should be invalid after node change

		pager_rollback();
		pager_close();
		os_file_delete(TEST_DB);
		std::cout << " OK\n";
	}

	std::cout << "All remaining coverage tests complete!\n";
}



// Update the main test_btree function to call stress test
inline void
test_btree()
{

	test_btree_stress();
	std::this_thread::sleep_for(std::chrono::seconds(2));
	// test_btree_single_key_leaf_delete();
	test_merge_empty_root();
	test_btree_extended();

	test_update_parent_keys_condition();
	test_btree_collapse_root();
	test_btree_deep_tree_coverage();
	test_btree_remaining_coverage();



    printf("\n=== Composite Type B+Tree Integration Tests ===\n");
    test_btree_u32_u64();
    // test_btree_u16_u16();
    // test_btree_u8_u8();
    // test_btree_u32_u64();

}
