#pragma once
#include "bplustree.hpp"
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

// Test composite keys
inline void test_btree_composite_keys() {
    std::cout << "\n=== Composite Key Tests ===\n";

    pager_open(TEST_DB);
    pager_begin_transaction();

    // Composite key: 8 bytes (user_id:4, timestamp:4)
    struct CompositeKey {
        uint32_t timestamp;
        uint32_t user_id;

        bool operator<(const CompositeKey& other) const {
            if (user_id != other.user_id) return user_id < other.user_id;
            return timestamp < other.timestamp;
        }
    };

    static_assert(sizeof(CompositeKey) == 8);

    // Tree with composite key, no value data (just key existence)
    BPlusTree tree = bplustree_create(TYPE_8, 0, true);  // 0-byte records
    BPtCursor cursor = {.tree = &tree};

    // Insert composite keys
    for (uint32_t user = 1; user <= 10; user++) {
        for (uint32_t time = 100; time <= 110; time++) {
            CompositeKey key = {time, user};
            uint8_t empty_value = 0;
            assert(bplustree_cursor_insert(&cursor, &key, &empty_value));
        }
    }

    // Test range queries on composite key
 CompositeKey seek_key = {0, 5};
	// uint64_t z = *reinterpret_cast<uint64_t*>(&seek_key);
 //    assert(bplustree_cursor_seek(&cursor, &seek_key));
	// bplustree_print(cursor.tree);

    // Find all entries for user 5
    assert(bplustree_cursor_seek_ge(&cursor, &seek_key));

	int count = 0;
    do {
        CompositeKey* found = (CompositeKey*)bplustree_cursor_key(&cursor);
        if (!found || found->user_id != 5) break;
        count++;
    } while (bplustree_cursor_next(&cursor));

    assert(count == 11);  // Should find 11 timestamps for user 5

    std::cout << "Composite keys OK\n";

    pager_rollback();
    pager_close();
    os_file_delete(TEST_DB);
}

// Test with maximum-size records
inline void test_btree_large_records() {
    std::cout << "\n=== Large Record Tests ===\n";

    pager_open(TEST_DB);
    pager_begin_transaction();

    // Create tree with very large records (forces MIN_ENTRY_COUNT)
    const uint32_t LARGE_RECORD = PAGE_SIZE / 4;  // Close to page size limit
    BPlusTree tree = bplustree_create(TYPE_4, LARGE_RECORD, true);
    BPtCursor cursor = {.tree = &tree};

    // Should have minimum keys per node
    assert(tree.leaf_max_keys == MIN_ENTRY_COUNT);

    uint8_t large_data[LARGE_RECORD];

    // Insert enough to force multiple levels
    for (uint32_t i = 0; i < 30; i++) {
        memset(large_data, i, LARGE_RECORD);
        assert(bplustree_cursor_insert(&cursor, &i, large_data));
        bplustree_validate(&tree);
    }

    // Verify data integrity
    for (uint32_t i = 0; i < 30; i++) {
        assert(bplustree_cursor_seek(&cursor, &i));
        uint8_t* data = bplustree_cursor_record(&cursor);
        assert(data[0] == i && data[LARGE_RECORD-1] == i);
    }

    std::cout << "Large records OK\n";

    pager_rollback();
    pager_close();
    os_file_delete(TEST_DB);
}

// Test multiple cursors on same tree
inline void test_btree_multiple_cursors() {
    std::cout << "\n=== Multiple Cursor Tests ===\n";

    pager_open(TEST_DB);
    pager_begin_transaction();

    BPlusTree tree = bplustree_create(TYPE_4, sizeof(uint32_t), true);
    BPtCursor cursor1 = {.tree = &tree};
    BPtCursor cursor2 = {.tree = &tree};
    BPtCursor cursor3 = {.tree = &tree};

    // Insert data
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t value = i * 100;
        assert(bplustree_cursor_insert(&cursor1, &i, (uint8_t*)&value));
    }

    // Position cursors at different locations
    assert(bplustree_cursor_first(&cursor1));

    uint32_t key = 50;
    assert(bplustree_cursor_seek(&cursor2, &key));

    assert(bplustree_cursor_last(&cursor3));

    // Verify each cursor maintains independent position
    uint32_t* key1 = (uint32_t*)bplustree_cursor_key(&cursor1);
    uint32_t* key2 = (uint32_t*)bplustree_cursor_key(&cursor2);
    uint32_t* key3 = (uint32_t*)bplustree_cursor_key(&cursor3);

    assert(*key1 == 0);
    assert(*key2 == 50);
    assert(*key3 == 99);

    // Navigate cursors independently
    assert(bplustree_cursor_next(&cursor1));
    assert(bplustree_cursor_previous(&cursor3));

    key1 = (uint32_t*)bplustree_cursor_key(&cursor1);
    key3 = (uint32_t*)bplustree_cursor_key(&cursor3);

    assert(*key1 == 1);
    assert(*key3 == 98);

    std::cout << "Multiple cursors OK\n";

    pager_rollback();
    pager_close();
    os_file_delete(TEST_DB);
}

// Test page eviction scenarios (requires small cache)
inline void test_btree_page_eviction() {
    std::cout << "\n=== Page Eviction Tests ===\n";

    if (MAX_CACHE_ENTRIES > 10) {
        std::cout << "Skipping (cache too large)\n";
        return;
    }

    pager_open(TEST_DB);
    pager_begin_transaction();

    BPlusTree tree = bplustree_create(TYPE_4, sizeof(uint32_t), true);
    BPtCursor cursor = {.tree = &tree};

    // Insert enough data to create many pages
    for (uint32_t i = 0; i < 1000; i++) {
        uint32_t value = i;
        assert(bplustree_cursor_insert(&cursor, &i, (uint8_t*)&value));
    }

    // Force cache thrashing by accessing in pattern
    for (int iter = 0; iter < 3; iter++) {
        // Forward scan
        assert(bplustree_cursor_first(&cursor));
        int count = 0;
        do {
            count++;
        } while (bplustree_cursor_next(&cursor) && count < 100);

        // Backward scan
        assert(bplustree_cursor_last(&cursor));
        count = 0;
        do {
            count++;
        } while (bplustree_cursor_previous(&cursor) && count < 100);

        // Random access
        for (int i = 0; i < 50; i++) {
            uint32_t key = (i * 37) % 1000;
            assert(bplustree_cursor_seek(&cursor, &key));
        }
    }

    bplustree_validate(&tree);
    std::cout << "Page eviction OK\n";

    pager_rollback();
    pager_close();
    os_file_delete(TEST_DB);
}

// Test VARCHAR keys with collation-like behavior
inline void test_btree_varchar_collation() {
    std::cout << "\n=== VARCHAR Collation Tests ===\n";

    pager_open(TEST_DB);
    pager_begin_transaction();

    BPlusTree tree = bplustree_create(TYPE_32, sizeof(uint32_t), true);
    BPtCursor cursor = {.tree = &tree};

    // Test strings that expose comparison edge cases
    const char* test_strings[] = {
        "",           // Empty
        " ",          // Space
        "  ",         // Multiple spaces
        "A",          // Upper
        "a",          // Lower
        "AA",         // Double upper
        "Aa",         // Mixed case
        "aA",         // Mixed case reverse
        "aa",         // Double lower
        "a b",        // With space
        "a  b",       // Double space
        "a\tb",       // With tab
        "1",          // Digit
        "10",         // Multi-digit
        "2",          // Another digit
        "abc",        // Lowercase word
        "ABC",        // Uppercase word
        "aBc",        // Mixed case word
        "\x01",       // Control char
        "\xFF",       // High byte
    };

    // Insert all strings
    for (int i = 0; i < sizeof(test_strings)/sizeof(test_strings[0]); i++) {
        char key[32] = {0};
        strncpy(key, test_strings[i], 31);
        uint32_t value = i;
        bplustree_cursor_insert(&cursor, key, (uint8_t*)&value);
    }

    // Collect sorted order from tree
    std::vector<std::string> tree_order;
    if (bplustree_cursor_first(&cursor)) {
        do {
            char* key = (char*)bplustree_cursor_key(&cursor);
            tree_order.push_back(std::string(key, strnlen(key, 32)));
        } while (bplustree_cursor_next(&cursor));
    }

    // Verify ordering is consistent
    for (size_t i = 1; i < tree_order.size(); i++) {
        assert(memcmp(tree_order[i-1].c_str(), tree_order[i].c_str(), 32) < 0);
    }

    std::cout << "VARCHAR collation OK (" << tree_order.size() << " unique keys)\n";

    pager_rollback();
    pager_close();
    os_file_delete(TEST_DB);
}

// Add to test_btree()
inline void test_btree_extended() {
    std::cout << "\n========== Extended B+Tree Tests ==========\n";

    test_btree_large_records();
    test_btree_composite_keys();
    test_btree_multiple_cursors();
    test_btree_page_eviction();
    test_btree_varchar_collation();


    std::cout << "\n========== All extended tests passed! ==========\n";
}

// Add arena stats printing to see memory growth
inline void print_memory_diagnostics(const char* test_name) {
    std::cout << "\nMemory after " << test_name << ":\n";

    Arena<global_arena>::print_stats();
}

// Update the main test_btree function to call stress test
inline void
test_btree()
{
	test_btree_stress();
	std::this_thread::sleep_for(std::chrono::seconds(2));
    test_btree_extended();
}
