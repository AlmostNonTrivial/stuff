
#include "btree.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <random>
#include <thread>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <set>

#include "../common.hpp"
#include "../os_layer.hpp"
#include "../pager.hpp"
#include "../types.hpp"
#include "../btree.hpp"

#define TEST_DB "test_btree.db"

void
test_btree_sequential_ops()
{
	pager_open(TEST_DB);
	pager_begin_transaction();

	btree	  tree = bt_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor cursor = {.tree = &tree};

	const int COUNT = 5000;

	// Sequential forward insertion
	for (int i = 0; i < COUNT; i++)
	{
		uint32_t key = i;
		uint32_t value = i * 100;

		if (i == 1831)
		{
			int k = 0;
		}
		bt_cursorinsert(&cursor, &key, (void *)&value);
		bt_validate(&tree);
	}

	btree_print(&tree);

	// Verify all keys exist
	for (int i = 0; i < COUNT; i++)
	{
		uint32_t key = i;
		assert(bt_cursorseek(&cursor, &key));
		uint32_t *val = (uint32_t *)bt_cursorrecord(&cursor);
		assert(*val == i * 100);
	}

	// Sequential forward deletion
	for (int i = 0; i < COUNT / 2; i++)
	{
		uint32_t key = i;
		assert(bt_cursorseek(&cursor, &key));
		bt_cursordelete(&cursor);
		bt_validate(&tree);
	}

	// Verify deleted keys don't exist
	for (int i = 0; i < COUNT / 2; i++)
	{
		uint32_t key = i;
		assert(!bt_cursorseek(&cursor, (uint8_t *)&key));
	}

	// Verify remaining keys exist
	for (int i = COUNT / 2; i < COUNT; i++)
	{
		uint32_t key = i;
		assert(bt_cursorseek(&cursor, &key));
	}

	// Backward sequential deletion
	for (int i = COUNT - 1; i >= COUNT / 2; i--)
	{
		uint32_t key = i;
		assert(bt_cursorseek(&cursor, &key));
		bt_cursordelete(&cursor);
		bt_validate(&tree);
	}

	// Tree should be empty
	assert(!bt_cursorfirst(&cursor));

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

void
test_btree_random_ops()
{
	pager_open(TEST_DB);
	pager_begin_transaction();

	btree	  tree = bt_create(TYPE_U32, sizeof(uint64_t), true);
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
	for (auto &[key, value] : data)
	{
		bt_cursorinsert(&cursor, &key, (void *)&value);
		bt_validate(&tree);
	}

	// Verify all entries
	for (auto &[key, value] : data)
	{
		assert(bt_cursorseek(&cursor, &key));
		uint64_t *val = (uint64_t *)bt_cursorrecord(&cursor);
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

	std::set<uint32_t> deleted_keys;
	for (int i = 0; i < delete_count; i++)
	{
		uint32_t key = keys_to_delete[i];
		assert(bt_cursorseek(&cursor, &key));
		bt_cursordelete(&cursor);
		bt_validate(&tree);
		deleted_keys.insert(key);
	}

	// Verify correct keys remain
	for (auto &[key, value] : data)
	{
		if (deleted_keys.find(key) == deleted_keys.end())
		{
			// Should exist
			assert(bt_cursorseek(&cursor, &key));
			uint64_t *val = (uint64_t *)bt_cursorrecord(&cursor);
			assert(*val == value);
		}
		else
		{
			// Should not exist
			assert(!bt_cursorseek(&cursor, &key));
		}
	}

	// Delete remaining keys
	for (int i = delete_count; i < keys_to_delete.size(); i++)
	{
		uint32_t key = keys_to_delete[i];
		assert(bt_cursorseek(&cursor, &key));
		bt_cursordelete(&cursor);
		bt_validate(&tree);
	}

	// Tree should be empty
	assert(!bt_cursorfirst(&cursor));

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

void
test_btree_mixed_ops()
{
	std::srand(123);

	pager_open(TEST_DB);
	pager_begin_transaction();

	btree	  tree = bt_create(TYPE_U64, sizeof(uint32_t), true);
	bt_cursor cursor = {.tree = &tree};

	std::set<uint64_t> keys_in_tree;
	const int		   ITERATIONS = 1000;
	const uint64_t	   KEY_RANGE = 1000;

	for (int i = 0; i < ITERATIONS; i++)
	{
		// Weighted operations: 60% insert, 40% delete
		int op = std::rand() % 100;

		if (op < 60 || keys_in_tree.empty())
		{ // Insert
			uint64_t key = std::rand() % KEY_RANGE;
			uint32_t value = key * 1000;

			bt_cursorinsert(&cursor, &key, (void *)&value);
			keys_in_tree.insert(key);
			bt_validate(&tree);
		}
		else
		{ // Delete
			// Pick random existing key
			auto it = keys_in_tree.begin();
			std::advance(it, std::rand() % keys_in_tree.size());
			uint64_t key = *it;

			assert(bt_cursorseek(&cursor, &key));
			bt_cursordelete(&cursor);
			keys_in_tree.erase(key);
			bt_validate(&tree);
		}

		// Periodically verify tree contents
		if (i % 50 == 0)
		{
			for (uint64_t key : keys_in_tree)
			{
				assert(bt_cursorseek(&cursor, &key));
				uint32_t *val = (uint32_t *)bt_cursorrecord(&cursor);
				assert(*val == key * 1000);
			}
		}
	}

	// Clean up remaining keys
	for (uint64_t key : keys_in_tree)
	{
		assert(bt_cursorseek(&cursor, &key));
		bt_cursordelete(&cursor);
		bt_validate(&tree);
	}

	assert(!bt_cursorfirst(&cursor));

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

void
test_btree_edge_cases()
{
	pager_open(TEST_DB);
	pager_begin_transaction();

	btree	  tree = bt_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor cursor = {.tree = &tree};

	// Delete from empty tree
	uint32_t key = 42;
	assert(!bt_cursorseek(&cursor, &key));
	assert(!bt_cursordelete(&cursor));
	bt_validate(&tree);

	// Single element operations
	uint32_t value = 100;
	bt_cursorinsert(&cursor, &key, (void *)&value);
	bt_validate(&tree);
	assert(bt_cursorseek(&cursor, &key));
	bt_cursordelete(&cursor);
	bt_validate(&tree);
	assert(!bt_cursorfirst(&cursor));

	// Boundary key values
	uint32_t min_key = 0;
	uint32_t max_key = UINT32_MAX;

	bt_cursorinsert(&cursor, &min_key, (void *)&value);
	bt_validate(&tree);
	bt_cursorinsert(&cursor, &max_key, (void *)&value);
	bt_validate(&tree);

	assert(bt_cursorseek(&cursor, &min_key));
	assert(bt_cursorseek(&cursor, &max_key));

	bt_cursordelete(&cursor);
	bt_validate(&tree);
	assert(bt_cursorseek(&cursor, &min_key));
	bt_cursordelete(&cursor);
	bt_validate(&tree);

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

void
test_btree_stress()
{
	// Test sequential operations
	test_btree_sequential_ops();

	// Test random operations
	test_btree_random_ops();

	// Test mixed operations
	test_btree_mixed_ops();

	// Test edge cases
	test_btree_edge_cases();
}

// ============================================================================
// B+Tree Integration Tests - Four focused tests
// ============================================================================

// // Test 1: U32+U64 composite (main use case)
void
test_btree_u32_u64()
{
	pager_open(TEST_DB);
	pager_begin_transaction();

	DataType  key_type = make_dual(TYPE_U32, TYPE_U64);
	btree	  tree = bt_create(key_type, 0, true);
	bt_cursor cursor = {.tree = &tree};

	uint8_t key_data[12];
	uint8_t empty_value = 0;

	// Insert user+timestamp pairs
	for (uint32_t user = 1; user <= 5; user++)
	{
		for (uint64_t time = 100; time <= 103; time++)
		{
			pack_dual(key_data, TYPE_U32, &user, TYPE_U64, &time);
			assert(bt_cursorinsert(&cursor, key_data, &empty_value));
		}
	}

	// Range query for user 3

	uint32_t a = 3;
	uint64_t b = 0;
	pack_dual(key_data, TYPE_U32, &a, TYPE_U64, &b);

	assert(bt_cursorseek(&cursor, key_data, GE));

	int count = 0;
	do
	{
		void	*found = bt_cursorkey(&cursor);
		uint32_t a;
		uint64_t b;
		unpack_dual(key_type, found, &a, &b);
		if (a != 3)
			break;
		count++;
	} while (bt_cursornext(&cursor));

	assert(count == 4);

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

// Test with maximum-size records
void
test_btree_large_records()
{
	pager_open(TEST_DB);
	pager_begin_transaction();

	// Create tree with very large records (forces MIN_ENTRY_COUNT)
	const uint32_t LARGE_RECORD = PAGE_SIZE / 4; // Close to page size limit
	btree		   tree = bt_create(TYPE_U32, LARGE_RECORD, true);
	bt_cursor	   cursor = {.tree = &tree};

	// Should have minimum keys per node

	uint8_t large_data[LARGE_RECORD];

	// Insert enough to force multiple levels
	for (uint32_t i = 0; i < 30; i++)
	{
		memset(large_data, i, LARGE_RECORD);
		assert(bt_cursorinsert(&cursor, &i, large_data));
		bt_validate(&tree);
	}

	// Verify data integrity
	for (uint32_t i = 0; i < 30; i++)
	{
		assert(bt_cursorseek(&cursor, &i));
		uint8_t *data = (uint8_t *)bt_cursorrecord(&cursor);
		assert(data[0] == i && data[LARGE_RECORD - 1] == i);
	}

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

// Test multiple cursors on same tree
void
test_btree_multiple_cursors()
{
	pager_open(TEST_DB);
	pager_begin_transaction();

	btree	  tree = bt_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor cursor1 = {.tree = &tree};
	bt_cursor cursor2 = {.tree = &tree};
	bt_cursor cursor3 = {.tree = &tree};

	// Insert data
	for (uint32_t i = 0; i < 100; i++)
	{
		uint32_t value = i * 100;
		assert(bt_cursorinsert(&cursor1, &i, (void *)&value));
	}

	// Position cursors at different locations
	assert(bt_cursorfirst(&cursor1));

	uint32_t key = 50;
	assert(bt_cursorseek(&cursor2, &key));

	assert(bt_cursorlast(&cursor3));

	// Verify each cursor maintains independent position
	uint32_t *key1 = (uint32_t *)bt_cursorkey(&cursor1);
	uint32_t *key2 = (uint32_t *)bt_cursorkey(&cursor2);
	uint32_t *key3 = (uint32_t *)bt_cursorkey(&cursor3);

	assert(*key1 == 0);
	assert(*key2 == 50);
	assert(*key3 == 99);

	// Navigate cursors independently
	assert(bt_cursornext(&cursor1));
	assert(bt_cursorprevious(&cursor3));

	key1 = (uint32_t *)bt_cursorkey(&cursor1);
	key3 = (uint32_t *)bt_cursorkey(&cursor3);

	assert(*key1 == 1);
	assert(*key3 == 98);

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

// Test page eviction scenarios (requires small cache)
void
test_btree_page_eviction()
{
	if (MAX_CACHE_ENTRIES > 10)
	{
		return;
	}

	pager_open(TEST_DB);
	pager_begin_transaction();

	btree	  tree = bt_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor cursor = {.tree = &tree};

	// Insert enough data to create many pages
	for (uint32_t i = 0; i < 1000; i++)
	{
		uint32_t value = i;
		assert(bt_cursorinsert(&cursor, &i, (void *)&value));
	}

	// Force cache thrashing by accessing in pattern
	for (int iter = 0; iter < 3; iter++)
	{
		// Forward scan
		assert(bt_cursorfirst(&cursor));
		int count = 0;
		do
		{
			count++;
		} while (bt_cursornext(&cursor) && count < 100);

		// Backward scan
		assert(bt_cursorlast(&cursor));
		count = 0;
		do
		{
			count++;
		} while (bt_cursorprevious(&cursor) && count < 100);

		// Random access
		for (int i = 0; i < 50; i++)
		{
			uint32_t key = (i * 37) % 1000;
			assert(bt_cursorseek(&cursor, &key));
		}
	}

	bt_validate(&tree);

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

// Test VARCHAR keys with collation-like behavior
void
test_btree_varchar_collation()
{
	pager_open(TEST_DB);
	pager_begin_transaction();

	btree	  tree = bt_create(TYPE_CHAR32, sizeof(uint32_t), true);
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
		bt_cursorinsert(&cursor, key, (void *)&value);
	}

	// Collect sorted order from tree
	std::vector<std::string> tree_order;
	if (bt_cursorfirst(&cursor))
	{
		do
		{
			char *key = (char *)bt_cursorkey(&cursor);
			tree_order.push_back(std::string(key, strnlen(key, 32)));
		} while (bt_cursornext(&cursor));
	}

	// Verify ordering is consistent
	for (size_t i = 1; i < tree_order.size(); i++)
	{
		assert(memcmp(tree_order[i - 1].c_str(), tree_order[i].c_str(), 32) < 0);
	}

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

// Add to test_btree()
void
test_btree_extended()
{
	test_btree_large_records();

	test_btree_multiple_cursors();
	test_btree_page_eviction();
	test_btree_varchar_collation();
}

void
test_update_parent_keys_condition()
{
	pager_open(TEST_DB);
	pager_begin_transaction();

	btree	  tree = bt_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor cursor = {.tree = &tree};

	const int COUNT = tree.leaf_max_keys * 3;

	std::vector<uint32_t> keys;
	for (int i = 0; i < COUNT; i++)
	{
		keys.push_back(i);
		bt_cursorinsert(&cursor, &i, (void *)&i);
	}

	uint32_t key = 150;
	bt_cursorseek(&cursor, &key);
	for (int i = 0; i < 182 - 150; i++)
	{
		bt_cursordelete(&cursor);
	}

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}
void
test_merge_empty_root()
{
	pager_open(TEST_DB);
	pager_begin_transaction();

	btree	  tree = bt_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor cursor = {.tree = &tree};

	const int COUNT = tree.leaf_max_keys + 1;

	for (int i = 0; i < COUNT; i++)
	{
		bt_cursorinsert(&cursor, &i, (void *)&i);
	}
	uint32_t key = 30;
	btree_print(&tree);
	bt_cursorseek(&cursor, &key);
	bt_cursordelete(&cursor);

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

void
test_btree_collapse_root()
{
	pager_open(TEST_DB);
	pager_begin_transaction();

	btree	  tree = bt_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor cursor = {.tree = &tree};

	// Insert just enough to create a 2-level tree
	for (uint32_t i = 0; i <= tree.leaf_max_keys; i++)
	{
		bt_cursorinsert(&cursor, &i, (void *)&i);
	}

	// Now delete everything to collapse the tree
	for (uint32_t i = 0; i <= tree.leaf_max_keys; i++)
	{
		assert(bt_cursorseek(&cursor, &i));
		bt_cursordelete(&cursor);
		bt_validate(&tree);
	}

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

void
test_btree_deep_tree_coverage()
{
	pager_open(TEST_DB);
	pager_begin_transaction();

	// Create tree with 64-byte records to force smaller node capacity
	const uint32_t RECORD_SIZE = 64;
	btree		   tree = bt_create(TYPE_U32, RECORD_SIZE, true);
	bt_cursor	   cursor = {.tree = &tree};

	// Insert enough keys to create a deep tree (at least 3 levels)
	const int KEY_COUNT = 500;
	uint8_t	  record_data[RECORD_SIZE];

	for (int i = 0; i < KEY_COUNT; i++)
	{
		uint32_t key = i;
		memset(record_data, i % 256, RECORD_SIZE);
		assert(bt_cursorinsert(&cursor, &key, record_data));
	}

	// Test cursor has_next and has_previous (if_100, if_101)
	assert(bt_cursorfirst(&cursor));
	assert(bt_cursorhas_next(&cursor));		 // Should hit if_100
	assert(!bt_cursorhas_previous(&cursor)); // Should test if_101

	assert(bt_cursorlast(&cursor));
	assert(!bt_cursorhas_next(&cursor));
	assert(bt_cursorhas_previous(&cursor)); // Should hit if_101

	// Navigate to trigger previous leaf movement (if_98, if_99)
	// Find a key that's at the start of a non-first leaf
	uint32_t target_key = tree.leaf_max_keys; // Should be first key of second leaf
	assert(bt_cursorseek(&cursor, &target_key));
	assert(bt_cursorprevious(&cursor)); // Should move to previous leaf (if_98, if_99)

	// Trigger if_37: Delete first key of a non-leftmost leaf
	// The key should be used as a separator in parent

	// Navigate to second leaf's first key
	target_key = tree.leaf_max_keys;
	assert(bt_cursorseek(&cursor, &target_key));

	// This key should be a separator in the parent
	// Delete it to trigger parent key update
	assert(bt_cursordelete(&cursor));
	bt_validate(&tree);

	// Test cursor operations on invalid cursor (various if_7x, if_8x, if_9x)
	bt_cursor invalid_cursor = {.tree = &tree};
	invalid_cursor.state = BT_CURSOR_INVALID;

	assert(bt_cursorkey(&invalid_cursor) == nullptr);		// if_75
	assert(bt_cursorrecord(&invalid_cursor) == nullptr);	// if_77
	assert(!bt_cursordelete(&invalid_cursor));				// if_82
	assert(!bt_cursorupdate(&invalid_cursor, record_data)); // if_89
	assert(!bt_cursornext(&invalid_cursor));				// if_90
	assert(!bt_cursorprevious(&invalid_cursor));			// if_95

	// Test cursor on empty tree (if_79)
	btree	  empty_tree = bt_create(TYPE_U32, sizeof(uint32_t), false); // Don't init
	bt_cursor empty_cursor = {.tree = &empty_tree};
	uint32_t  test_key = 42;
	assert(!bt_cursorseek(&empty_cursor, &test_key)); // if_79

	// Test seek_cmp for coverage (if_72, if_73)
	uint32_t cmp_key = 250;
	assert(bt_cursorseek(&cursor, &cmp_key, GE)); // if_72 for exact match case

	// Test with key that doesn't exist
	uint32_t missing_key = KEY_COUNT + 100;
	assert(bt_cursorseek(&cursor, &missing_key, LE)); // Should iterate and hit if_73

	// Test node fault conditions (if_91, if_96)
	bt_cursor fault_cursor = {.tree = &tree};
	fault_cursor.state = BT_CURSOR_VALID;
	fault_cursor.leaf_page = 999999; // Invalid page
	fault_cursor.leaf_index = 0;

	assert(!bt_cursornext(&fault_cursor));	   // if_91
	fault_cursor.state = BT_CURSOR_VALID;	   // Reset state
	assert(!bt_cursorprevious(&fault_cursor)); // if_96

	// Test cursor with out-of-bounds index (if_76, if_78)
	assert(bt_cursorfirst(&cursor));
	cursor.leaf_index = 999;					 // Way out of bounds
	assert(bt_cursorkey(&cursor) == nullptr);	 // if_76
	assert(bt_cursorrecord(&cursor) == nullptr); // if_78

	// Test node changes after delete (if_84)
	// This is tricky - need to delete such that the node itself changes
	// Usually happens during merges where the node gets deallocated
	// Set up a scenario with minimal keys
	btree	  small_tree = bt_create(TYPE_U32, sizeof(uint32_t), true);
	bt_cursor small_cursor = {.tree = &small_tree};

	// Insert just enough to split once
	for (uint32_t i = 0; i <= small_tree.leaf_max_keys; i++)
	{
		uint32_t val = i;
		bt_cursorinsert(&small_cursor, &i, (void *)&val);
	}

	// Delete to cause merge that changes node structure
	for (uint32_t i = 1; i < small_tree.leaf_min_keys; i++)
	{
		assert(bt_cursorseek(&small_cursor, &i));
		bt_cursordelete(&small_cursor);
	}

	// Clear the trees (if_64, if_65)
	assert(bt_clear(&tree));	   // if_64 (recursive clear)
	assert(bt_clear(&empty_tree)); // if_65 (empty tree clear)
	assert(bt_clear(&small_tree));

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}
void
test_btree_remaining_coverage()
{
	// Test 1: if_55, if_56 - Collapse internal root with single child
	{
		pager_open(TEST_DB);
		pager_begin_transaction();

		btree	  tree = bt_create(TYPE_U32, sizeof(uint32_t), true);
		bt_cursor cursor = {.tree = &tree};

		// Build a 3-level tree
		for (uint32_t i = 0; i < 200; i++)
		{
			uint32_t val = i;
			bt_cursorinsert(&cursor, &i, (void *)&val);
		}

		// Delete everything except keys that keep one subtree
		// This should eventually collapse an internal root
		for (uint32_t i = 0; i < 199; i++)
		{
			if (bt_cursorseek(&cursor, &i))
			{
				bt_cursordelete(&cursor);
				bt_validate(&tree);
			}
		}

		pager_rollback();
		pager_close();
		os_file_delete(TEST_DB);
	}

	// Test 2: if_37 - Update parent keys when deleted key matches separator
	{
		// Commented out in original
	}

	// Test 3: if_98, if_99 - Previous navigation across leaf boundary
	{
		pager_open(TEST_DB);
		pager_begin_transaction();

		btree	  tree = bt_create(TYPE_U32, sizeof(uint32_t), true);
		bt_cursor cursor = {.tree = &tree};

		// Insert enough for multiple leaves
		for (uint32_t i = 0; i < tree.leaf_max_keys * 3; i++)
		{
			uint32_t val = i;
			bt_cursorinsert(&cursor, &i, (void *)&val);
		}

		while (bt_cursorprevious(&cursor))
			;

		// Position at start of second leaf
		uint32_t key = tree.leaf_max_keys;
		assert(bt_cursorseek(&cursor, &key));

		// Move to previous should cross leaf boundary
		assert(bt_cursorprevious(&cursor)); // Should trigger if_98, if_99

		// Verify we're at the end of first leaf
		uint32_t *current = (uint32_t *)bt_cursorkey(&cursor);
		assert(*current == tree.leaf_max_keys - 1);

		pager_rollback();
		pager_close();
		os_file_delete(TEST_DB);
	}

	// Test 4: if_73 - Null key in seek_cmp loop
	{
		pager_open(TEST_DB);
		pager_begin_transaction();

		btree	  tree = bt_create(TYPE_U32, sizeof(uint32_t), true);
		bt_cursor cursor = {.tree = &tree};

		// Insert sparse keys
		uint32_t keys[] = {10, 20, 30, 40, 50};
		for (int i = 0; i < 5; i++)
		{
			uint32_t val = keys[i];
			bt_cursorinsert(&cursor, &keys[i], (void *)&val);
		}

		// Seek with comparison to non-existent key
		uint32_t target = 25;

		// Position cursor in invalid state first
		cursor.state = BT_CURSOR_INVALID;

		// This should iterate and hit if_73 when cursor key returns null
		assert(bt_cursorseek(&cursor, &target, GE));

		pager_rollback();
		pager_close();
		os_file_delete(TEST_DB);
	}

	// Test 5: if_68 - Fault in cursor_move_in_subtree
	{
		pager_open(TEST_DB);
		pager_begin_transaction();

		btree tree = bt_create(TYPE_U32, sizeof(uint32_t), true);

		// Create a tree with internal nodes
		bt_cursor cursor = {.tree = &tree};
		uint32_t  i = 0;
		bt_cursorseek(&cursor, &i, GE);

		pager_rollback();
		pager_close();
		os_file_delete(TEST_DB);
	}

	// Test 6: if_84 - Node changes after delete
	{
		pager_open(TEST_DB);
		pager_begin_transaction();

		btree	  tree = bt_create(TYPE_U32, sizeof(uint32_t), true);
		bt_cursor cursor = {.tree = &tree};

		// Create minimal tree that will merge on delete
		for (uint32_t i = 0; i <= tree.leaf_max_keys + 1; i++)
		{
			uint32_t val = i;
			bt_cursorinsert(&cursor, &i, (void *)&val);
		}

		// Position cursor on a key in the right leaf
		uint32_t target = tree.leaf_max_keys + 1;
		assert(bt_cursorseek(&cursor, &target));

		// Delete keys to force merge that will deallocate the node cursor is on
		for (uint32_t i = 1; i < tree.leaf_max_keys; i++)
		{
			bt_cursor temp_cursor = {.tree = &tree};
			if (bt_cursorseek(&temp_cursor, &i))
			{
				bt_cursordelete(&temp_cursor);
			}
		}

		// Now delete with our positioned cursor - node should change
		bt_cursordelete(&cursor);

		pager_rollback();
		pager_close();
		os_file_delete(TEST_DB);
	}
}
struct BTreeTestConfig
{
	DataType	key_type;
	uint32_t	record_size;
	const char *name;
};

void
test_btree_sequential_ops_parameterized(const BTreeTestConfig &config)
{
	pager_open(TEST_DB);
	pager_begin_transaction();

	btree	  tree = bt_create(config.key_type, config.record_size, true);
	bt_cursor cursor = {.tree = &tree};

	const int COUNT = 5000;
	uint32_t  key_size = type_size(config.key_type);

	// Allocate storage for keys and records
	std::vector<uint8_t> key_storage(COUNT * key_size);
	std::vector<uint8_t> record_storage(COUNT * config.record_size);

	// Generate keys based on type
	for (int i = 0; i < COUNT; i++)
	{
		uint8_t *key_ptr = key_storage.data() + (i * key_size);
		uint8_t *record_ptr = record_storage.data() + (i * config.record_size);

		// Generate key based on type
		if (type_is_dual(config.key_type))
		{
			// Handle dual types like U32+U64
			DataType first_type = dual_component_type(config.key_type, 0);
			DataType second_type = dual_component_type(config.key_type, 1);

			if (first_type == TYPE_U32 && second_type == TYPE_U64)
			{
				uint32_t first = i;
				uint64_t second = i * 100;
				pack_dual(key_ptr, TYPE_U32, &first, TYPE_U64, &second);
			}
			else if (first_type == TYPE_U16 && second_type == TYPE_U16)
			{
				uint16_t first = i % 65536;
				uint16_t second = (i * 10) % 65536;
				pack_dual(key_ptr, TYPE_U16, &first, TYPE_U16, &second);
			}
			else if (first_type == TYPE_U8 && second_type == TYPE_U8)
			{
				uint8_t first = i % 256;
				uint8_t second = (i * 10) % 256;
				pack_dual(key_ptr, TYPE_U8, &first, TYPE_U8, &second);
			}
		}
		else if (config.key_type == TYPE_CHAR32)
		{
			// Handle VARCHAR
			char str[32] = {0};
			snprintf(str, 31, "key_%010d", i);
			memcpy(key_ptr, str, 32);
		}
		else
		{
			// Handle simple numeric types
			switch (config.key_type)
			{
			case TYPE_U8:
				*(uint8_t *)key_ptr = i % 256;
				break;
			case TYPE_U16:
				*(uint16_t *)key_ptr = i % 65536;
				break;
			case TYPE_U32:
				*(uint32_t *)key_ptr = i;
				break;
			case TYPE_U64:
				*(uint64_t *)key_ptr = i;
				break;
			case TYPE_F64:
				*(float *)key_ptr = (float)i;
				break;
			default:
				assert(false && "Unsupported key type");
			}
		}

		// Generate record data - just use a pattern based on index
		if (config.record_size > 0)
		{
			// Fill with pattern: [i % 256, (i+1) % 256, ...]
			for (uint32_t j = 0; j < config.record_size; j++)
			{
				record_ptr[j] = (i + j) % 256;
			}
		}
	}

	// Sequential forward insertion
	for (int i = 0; i < COUNT; i++)
	{
		uint8_t *key = key_storage.data() + (i * key_size);
		uint8_t *record = record_storage.data() + (i * config.record_size);

		bool inserted = bt_cursorinsert(&cursor, key, record);
		assert(inserted);
		bt_validate(&tree);
	}

	// Verify all keys exist
	for (int i = 0; i < COUNT; i++)
	{
		uint8_t *key = key_storage.data() + (i * key_size);
		assert(bt_cursorseek(&cursor, key));

		// Verify record data matches
		if (config.record_size > 0)
		{
			uint8_t *expected_record = record_storage.data() + (i * config.record_size);
			uint8_t *actual_record = (uint8_t *)bt_cursorrecord(&cursor);
			assert(memcmp(actual_record, expected_record, config.record_size) == 0);
		}
	}

	// Sequential forward deletion (first half)
	for (int i = 0; i < COUNT / 2; i++)
	{
		uint8_t *key = key_storage.data() + (i * key_size);
		assert(bt_cursorseek(&cursor, key));
		assert(bt_cursordelete(&cursor));
		bt_validate(&tree);
	}

	// Verify deleted keys don't exist
	for (int i = 0; i < COUNT / 2; i++)
	{
		uint8_t *key = key_storage.data() + (i * key_size);
		assert(!bt_cursorseek(&cursor, key));
	}

	// Verify remaining keys still exist
	for (int i = COUNT / 2; i < COUNT; i++)
	{
		uint8_t *key = key_storage.data() + (i * key_size);
		assert(bt_cursorseek(&cursor, key));
	}

	// Backward sequential deletion (remaining half)
	for (int i = COUNT - 1; i >= COUNT / 2; i--)
	{
		uint8_t *key = key_storage.data() + (i * key_size);
		assert(bt_cursorseek(&cursor, key));
		assert(bt_cursordelete(&cursor));
		bt_validate(&tree);
	}

	// Tree should be empty
	assert(!bt_cursorfirst(&cursor));

	pager_rollback();
	pager_close();
	os_file_delete(TEST_DB);
}

// Usage example:
void
test_btree_sequential_all_types()
{
	const BTreeTestConfig configs[] = {
		{TYPE_U32, sizeof(uint32_t), "U32 key, U32 record"},
		{TYPE_U32, sizeof(uint64_t), "U32 key, U64 record"},
		{TYPE_U64, sizeof(uint32_t), "U64 key, U32 record"},
		{TYPE_CHAR32, sizeof(uint32_t), "VARCHAR key, U32 record"},
		{make_dual(TYPE_U32, TYPE_U64), sizeof(uint16_t), "U32+U64 key, U16 record"},
		{make_dual(TYPE_U16, TYPE_U16), 0, "U16+U16 key, no record"},
	};

	for (const auto &config : configs)
	{
		test_btree_sequential_ops_parameterized(config);
	}
}

// Update the main test_btree function to call stress test
void
test_btree()
{
	test_btree_sequential_all_types();
	test_btree_stress();
	std::this_thread::sleep_for(std::chrono::seconds(2));
	test_merge_empty_root();
	test_btree_extended();

	test_update_parent_keys_condition();
	test_btree_collapse_root();
	test_btree_deep_tree_coverage();
	test_btree_remaining_coverage();
	test_btree_u32_u64();

	printf("btree tests passed\n");
}
