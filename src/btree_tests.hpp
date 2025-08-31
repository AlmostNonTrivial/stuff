#pragma once
#include "bplustree.hpp"
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <iterator>
#include <random>
#include <vector>
#include <algorithm>
#include "defs.hpp"
#include "pager.hpp"
#include "test_utils.hpp"
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

	BPlusTree tree = bplustree_create(TYPE_4, sizeof(uint32_t), true);
	BPtCursor cursor = {.tree = &tree};

	const int COUNT = 5000;

	// Sequential forward insertion
	std::cout << "Forward sequential insert..." << std::flush;
	for (int i = 0; i < COUNT; i++)
	{
		uint32_t key = i;
		uint32_t value = i * 100;
		bplustree_cursor_insert(&cursor, &key, (uint8_t *)&value);
		bplustree_validate(&tree);
	}
	std::cout << " OK\n";

	// Verify all keys exist
	for (int i = 0; i < COUNT; i++)
	{
		uint32_t key = i;
		assert(bplustree_cursor_seek(&cursor, &key));
		uint32_t *val = (uint32_t *)bplustree_cursor_record(&cursor);
		assert(*val == i * 100);
	}

	// Sequential forward deletion
	std::cout << "Forward sequential delete..." << std::flush;
	for (int i = 0; i < COUNT / 2; i++)
	{
		uint32_t key = i;
		assert(bplustree_cursor_seek(&cursor, &key));
		bplustree_cursor_delete(&cursor);
		bplustree_validate(&tree);
	}
	std::cout << " OK\n";

	// Verify deleted keys don't exist
	for (int i = 0; i < COUNT / 2; i++)
	{
		uint32_t key = i;
		assert(!bplustree_cursor_seek(&cursor, &key));
	}

	// Verify remaining keys exist
	for (int i = COUNT / 2; i < COUNT; i++)
	{
		uint32_t key = i;
		assert(bplustree_cursor_seek(&cursor, &key));
	}

	// Backward sequential deletion
	std::cout << "Backward sequential delete..." << std::flush;
	for (int i = COUNT - 1; i >= COUNT / 2; i--)
	{
		uint32_t key = i;
		assert(bplustree_cursor_seek(&cursor, &key));
		bplustree_cursor_delete(&cursor);
		bplustree_validate(&tree);
	}
	std::cout << " OK\n";

	// Tree should be empty
	assert(!bplustree_cursor_first(&cursor));

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

	BPlusTree tree = bplustree_create(TYPE_4, sizeof(uint64_t), true);
	BPtCursor cursor = {.tree = &tree};

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
		bplustree_cursor_insert(&cursor, &key, (uint8_t *)&value);
		bplustree_validate(&tree);
	}
	std::cout << " OK (" << COUNT << " unique keys)\n";

	// Verify all entries
	for (auto &[key, value] : data)
	{
		assert(bplustree_cursor_seek(&cursor, &key));
		uint64_t *val = (uint64_t *)bplustree_cursor_record(&cursor);
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
		assert(bplustree_cursor_seek(&cursor, &key));
		bplustree_cursor_delete(&cursor);
		bplustree_validate(&tree);
		deleted_keys.insert(key);
	}
	std::cout << " OK (deleted keys: " << delete_count << ")\n";

	// Verify correct keys remain
	for (auto &[key, value] : data)
	{
		if (deleted_keys.find(key) == deleted_keys.end())
		{
			// Should exist
			assert(bplustree_cursor_seek(&cursor, &key));
			uint64_t *val = (uint64_t *)bplustree_cursor_record(&cursor);
			assert(*val == value);
		}
		else
		{
			// Should not exist
			assert(!bplustree_cursor_seek(&cursor, &key));
		}
	}

	// Delete remaining keys
	std::cout << "Delete remaining..." << std::flush;
	for (int i = delete_count; i < keys_to_delete.size(); i++)
	{
		uint32_t key = keys_to_delete[i];
		assert(bplustree_cursor_seek(&cursor, &key));
		bplustree_cursor_delete(&cursor);
		bplustree_validate(&tree);
	}
	std::cout << " OK\n";

	// Tree should be empty
	assert(!bplustree_cursor_first(&cursor));

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

	BPlusTree tree = bplustree_create(TYPE_8, sizeof(uint32_t), true);
	BPtCursor cursor = {.tree = &tree};

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

			bplustree_cursor_insert(&cursor, &key, (uint8_t *)&value);
			keys_in_tree.insert(key);
			bplustree_validate(&tree);
		}
		else
		{ // Delete
			// Pick random existing key
			auto it = keys_in_tree.begin();
			std::advance(it, std::rand() % keys_in_tree.size());
			uint64_t key = *it;

			assert(bplustree_cursor_seek(&cursor, &key));
			bplustree_cursor_delete(&cursor);
			keys_in_tree.erase(key);
			bplustree_validate(&tree);
		}

		// Periodically verify tree contents
		if (i % 50 == 0)
		{
			for (uint64_t key : keys_in_tree)
			{
				assert(bplustree_cursor_seek(&cursor, &key));
				uint32_t *val = (uint32_t *)bplustree_cursor_record(&cursor);
				assert(*val == key * 1000);
			}
		}
	}

	std::cout << " OK (final size: " << keys_in_tree.size() << ")\n";

	// Clean up remaining keys
	std::cout << "Cleanup..." << std::flush;
	for (uint64_t key : keys_in_tree)
	{
		assert(bplustree_cursor_seek(&cursor, &key));
		bplustree_cursor_delete(&cursor);
		bplustree_validate(&tree);
	}
	std::cout << " OK\n";

	assert(!bplustree_cursor_first(&cursor));

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

	BPlusTree tree = bplustree_create(TYPE_4, sizeof(uint32_t), true);
	BPtCursor cursor = {.tree = &tree};

	// Delete from empty tree
	std::cout << "Delete from empty..." << std::flush;
	uint32_t key = 42;
	assert(!bplustree_cursor_seek(&cursor, &key));
	assert(!bplustree_cursor_delete(&cursor));
	bplustree_validate(&tree);
	std::cout << " OK\n";

	// Single element operations
	std::cout << "Single element..." << std::flush;
	uint32_t value = 100;
	bplustree_cursor_insert(&cursor, &key, (uint8_t *)&value);
	bplustree_validate(&tree);
	assert(bplustree_cursor_seek(&cursor, &key));
	bplustree_cursor_delete(&cursor);
	bplustree_validate(&tree);
	assert(!bplustree_cursor_first(&cursor));
	std::cout << " OK\n";

	// Boundary key values
	std::cout << "Boundary values..." << std::flush;
	uint32_t min_key = 0;
	uint32_t max_key = UINT32_MAX;

	bplustree_cursor_insert(&cursor, &min_key, (uint8_t *)&value);
	bplustree_validate(&tree);
	bplustree_cursor_insert(&cursor, &max_key, (uint8_t *)&value);
	bplustree_validate(&tree);

	assert(bplustree_cursor_seek(&cursor, &min_key));
	assert(bplustree_cursor_seek(&cursor, &max_key));

	bplustree_cursor_delete(&cursor);
	bplustree_validate(&tree);
	assert(bplustree_cursor_seek(&cursor, &min_key));
	bplustree_cursor_delete(&cursor);
	bplustree_validate(&tree);

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

// Update the main test_btree function to call stress test
inline void
test_btree()
{
	// Run comprehensive stress test
	test_btree_stress();
}
