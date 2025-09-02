#pragma once
#include "memtree.hpp"
#include "arena.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
#include <set>
#include <vector>

// Simple memory context for testing
struct TestContext : MemoryContext {
    TestContext() {
        alloc = [](size_t size) -> void* {
            return arena::alloc<QueryArena>(size);
        };
        emit_row = nullptr;
    }
};

inline void test_memtree_sequential_ops() {
    std::cout << "\n=== MemTree Sequential Operations ===\n";

    arena::init<QueryArena>();
    TestContext ctx;

    MemTree tree = memtree_create(TYPE_U32, sizeof(uint32_t), false);
    MemCursor cursor = {.tree = &tree, .ctx = &ctx};

    const int COUNT = 1000;

    // Sequential forward insertion
    std::cout << "Forward sequential insert..." << std::flush;
    for (int i = 0; i < COUNT; i++) {
        uint32_t key = i;
        uint32_t value = i * 100;
        assert(memtree_insert(&tree, (uint8_t*)&key, (uint8_t*)&value, &ctx));
    }
    std::cout << " OK (" << tree.node_count << " nodes)\n";

    // Verify all keys exist
    for (int i = 0; i < COUNT; i++) {
        uint32_t key = i;
        assert(memcursor_seek(&cursor, &key));
        uint32_t* val = (uint32_t*)memcursor_record(&cursor);
        assert(*val == i * 100);
    }

    // Sequential forward deletion
    std::cout << "Forward sequential delete..." << std::flush;
    for (int i = 0; i < COUNT / 2; i++) {
        uint32_t key = i;
        assert(memtree_delete(&tree, (uint8_t*)&key));
    }
    std::cout << " OK (remaining: " << tree.node_count << ")\n";

    // Verify deleted keys don't exist
    for (int i = 0; i < COUNT / 2; i++) {
        uint32_t key = i;
        assert(!memcursor_seek(&cursor, &key));
    }

    // Verify remaining keys exist
    for (int i = COUNT / 2; i < COUNT; i++) {
        uint32_t key = i;
        assert(memcursor_seek(&cursor, &key));
    }

    // Backward sequential deletion
    std::cout << "Backward sequential delete..." << std::flush;
    for (int i = COUNT - 1; i >= COUNT / 2; i--) {
        uint32_t key = i;
        assert(memtree_delete(&tree, (uint8_t*)&key));
    }
    std::cout << " OK\n";

    // Tree should be empty
    assert(memtree_is_empty(&tree));

    arena::reset<QueryArena>();
}

inline void test_memtree_random_ops() {
    std::cout << "\n=== MemTree Random Operations ===\n";

    arena::init<QueryArena>();
    TestContext ctx;

    MemTree tree = memtree_create(TYPE_U32, sizeof(uint64_t), false);
    MemCursor cursor = {.tree = &tree, .ctx = &ctx};

    const int COUNT = 1000;

    // Generate unique keys and values
    std::vector<std::pair<uint32_t, uint64_t>> data;
    for (int i = 0; i < COUNT; i++) {
        data.push_back({i, (uint64_t)i * 1000});
    }

    // Shuffle for random insertion order
    std::mt19937 rng(42); // Deterministic seed
    std::shuffle(data.begin(), data.end(), rng);

    // Random insertions
    std::cout << "Random insert..." << std::flush;
    for (auto& [key, value] : data) {
        assert(memtree_insert(&tree, (uint8_t*)&key, (uint8_t*)&value, &ctx));
    }
    std::cout << " OK (" << COUNT << " unique keys)\n";


    // Verify all entries
    for (auto& [key, value] : data) {
        assert(memcursor_seek(&cursor, &key));
        uint64_t* val = (uint64_t*)memcursor_record(&cursor);
        assert(*val == value);
    }

    // Create list of keys for deletion
    std::vector<uint32_t> keys_to_delete;
    for (auto& [key, _] : data) {
        keys_to_delete.push_back(key);
    }

    // Delete half the keys randomly
    std::shuffle(keys_to_delete.begin(), keys_to_delete.end(), rng);
    int delete_count = keys_to_delete.size() / 2;

    std::cout << "Random delete..." << std::flush;
    std::set<uint32_t> deleted_keys;
    for (int i = 0; i < delete_count; i++) {
        uint32_t key = keys_to_delete[i];
        assert(memtree_delete(&tree, (uint8_t*)&key));
        deleted_keys.insert(key);
    }
    std::cout << " OK (deleted: " << delete_count << ")\n";

    // Verify correct keys remain
    for (auto& [key, value] : data) {
        if (deleted_keys.find(key) == deleted_keys.end()) {
            assert(memcursor_seek(&cursor, &key));
            uint64_t* val = (uint64_t*)memcursor_record(&cursor);
            assert(*val == value);
        } else {
            assert(!memcursor_seek(&cursor, &key));
        }
    }

    arena::reset<QueryArena>();
}

inline void test_memtree_duplicates() {
    std::cout << "\n=== MemTree Duplicate Keys ===\n";

    arena::init<QueryArena>();
    TestContext ctx;

    // Create tree that allows duplicates
    MemTree tree = memtree_create(TYPE_U32, sizeof(uint32_t), true);
    MemCursor cursor = {.tree = &tree, .ctx = &ctx};

    // Insert multiple records with same key
    std::cout << "Insert duplicates..." << std::flush;
    uint32_t key = 42;
    for (uint32_t i = 0; i < 10; i++) {
        uint32_t record = i * 100;
        assert(memtree_insert(&tree, (uint8_t*)&key, (uint8_t*)&record, &ctx));
    }
    std::cout << " OK (10 duplicates)\n";

    // Count duplicates
    uint32_t count = memcursor_count_duplicates(&cursor, &key);
    assert(count == 10);

    // Iterate through all duplicates
    std::cout << "Iterate duplicates..." << std::flush;
    assert(memcursor_seek(&cursor, &key));
    std::set<uint32_t> found_records;
    do {
        uint32_t* curr_key = (uint32_t*)memcursor_key(&cursor);
        if (!curr_key || *curr_key != key) break;

        uint32_t* record = (uint32_t*)memcursor_record(&cursor);
        found_records.insert(*record);
    } while (memcursor_next(&cursor));

    assert(found_records.size() == 10);
    std::cout << " OK\n";

    // Delete specific duplicate
    std::cout << "Delete exact duplicate..." << std::flush;
    uint32_t target_record = 500;
    assert(memtree_delete_exact(&tree, (uint8_t*)&key, (uint8_t*)&target_record));
    count = memcursor_count_duplicates(&cursor, &key);
    assert(count == 9);
    std::cout << " OK\n";

    // Delete first occurrence
    std::cout << "Delete first occurrence..." << std::flush;
    assert(memtree_delete(&tree, (uint8_t*)&key));
    count = memcursor_count_duplicates(&cursor, &key);
    assert(count == 8);
    std::cout << " OK\n";

    arena::reset<QueryArena>();
}

inline void test_memtree_composite_keys() {
    std::cout << "\n=== MemTree Composite Keys ===\n";

    arena::init<QueryArena>();
    TestContext ctx;

    // For TYPE_U64 comparison to work correctly with composite keys,
    // we need to pack them into a uint64_t with the primary sort field
    // in the most significant bits

    auto make_composite_key = [](uint32_t user_id, uint32_t timestamp) -> uint64_t {
        // Pack user_id in high 32 bits, timestamp in low 32 bits
        return ((uint64_t)user_id << 32) | timestamp;
    };

    auto extract_user_id = [](uint64_t key) -> uint32_t {
        return (uint32_t)(key >> 32);
    };

    auto extract_timestamp = [](uint64_t key) -> uint32_t {
        return (uint32_t)(key & 0xFFFFFFFF);
    };

    MemTree tree = memtree_create(TYPE_U64, sizeof(uint64_t), false);
    MemCursor cursor = {.tree = &tree, .ctx = &ctx};

    // Insert composite keys
    std::cout << "Insert composite keys..." << std::flush;
    for (uint32_t user = 1; user <= 10; user++) {
        for (uint32_t time = 100; time <= 110; time++) {
            uint64_t key = make_composite_key(user, time);
            uint64_t value = key; // Just use key as value for simplicity
            assert(memtree_insert(&tree, (uint8_t*)&key, (uint8_t*)&value, &ctx));
        }
    }
    std::cout << " OK (110 keys)\n";

    // Range query for specific user
    std::cout << "Range query..." << std::flush;
    uint64_t start_key = make_composite_key(5, 0);
    uint64_t end_key = make_composite_key(6, 0);

    assert(memcursor_seek_ge(&cursor, &start_key));
    int count = 0;
    do {
        uint64_t* found = (uint64_t*)memcursor_key(&cursor);
        if (!found) break;

        uint32_t user_id = extract_user_id(*found);
        if (user_id >= 6) break;

        assert(user_id == 5);
        count++;
    } while (memcursor_next(&cursor));

    assert(count == 11); // 11 timestamps for user 5
    std::cout << " OK\n";

    arena::reset<QueryArena>();
}

inline void test_memtree_cursor_operations() {
    std::cout << "\n=== MemTree Cursor Operations ===\n";

    arena::init<QueryArena>();
    TestContext ctx;

    MemTree tree = memtree_create(TYPE_U32, sizeof(uint32_t), false);
    MemCursor cursor = {.tree = &tree, .ctx = &ctx};

    // Insert test data
    for (uint32_t i = 0; i < 100; i += 10) {
        uint32_t value = i;
        assert(memcursor_insert(&cursor, &i, (uint8_t*)&value));
    }

    // Test seek operations
    std::cout << "Seek operations..." << std::flush;

    uint32_t key = 25;
    assert(memcursor_seek_gt(&cursor, &key));
    assert(*(uint32_t*)memcursor_key(&cursor) == 30);

    assert(memcursor_seek_ge(&cursor, &key));
    assert(*(uint32_t*)memcursor_key(&cursor) == 30);

    key = 30;
    assert(memcursor_seek_ge(&cursor, &key));
    assert(*(uint32_t*)memcursor_key(&cursor) == 30);

    key = 35;
    assert(memcursor_seek_lt(&cursor, &key));
    assert(*(uint32_t*)memcursor_key(&cursor) == 30);

    assert(memcursor_seek_le(&cursor, &key));
    assert(*(uint32_t*)memcursor_key(&cursor) == 30);

    std::cout << " OK\n";

    // Test cursor navigation
    std::cout << "Cursor navigation..." << std::flush;

    assert(memcursor_first(&cursor));
    assert(*(uint32_t*)memcursor_key(&cursor) == 0);

    assert(memcursor_last(&cursor));
    assert(*(uint32_t*)memcursor_key(&cursor) == 90);

    assert(memcursor_previous(&cursor));
    assert(*(uint32_t*)memcursor_key(&cursor) == 80);

    assert(memcursor_next(&cursor));
    assert(*(uint32_t*)memcursor_key(&cursor) == 90);

    std::cout << " OK\n";

    // Test cursor update
    std::cout << "Cursor update..." << std::flush;
    key = 50;
    assert(memcursor_seek(&cursor, &key));
    uint32_t new_value = 5000;
    assert(memcursor_update(&cursor, (uint8_t*)&new_value));
    assert(*(uint32_t*)memcursor_record(&cursor) == 5000);
    std::cout << " OK\n";

    // Test cursor delete
    std::cout << "Cursor delete..." << std::flush;
    assert(memcursor_seek(&cursor, &key));
    assert(memcursor_delete(&cursor));
    assert(!memcursor_seek(&cursor, &key));
    std::cout << " OK\n";

    arena::reset<QueryArena>();
}

inline void test_memtree_edge_cases() {
    std::cout << "\n=== MemTree Edge Cases ===\n";

    arena::init<QueryArena>();
    TestContext ctx;

    MemTree tree = memtree_create(TYPE_U32, sizeof(uint32_t), false);
    MemCursor cursor = {.tree = &tree, .ctx = &ctx};

    // Empty tree operations
    std::cout << "Empty tree..." << std::flush;
    assert(memtree_is_empty(&tree));
    assert(!memcursor_first(&cursor));
    assert(!memcursor_last(&cursor));
    uint32_t key = 42;
    assert(!memtree_delete(&tree, (uint8_t*)&key));
    std::cout << " OK\n";

    // Single element
    std::cout << "Single element..." << std::flush;
    uint32_t value = 100;
    assert(memtree_insert(&tree, (uint8_t*)&key, (uint8_t*)&value, &ctx));
    assert(!memtree_is_empty(&tree));
    assert(memcursor_first(&cursor));
    assert(memcursor_last(&cursor));
    assert(*(uint32_t*)memcursor_key(&cursor) == 42);
    assert(memtree_delete(&tree, (uint8_t*)&key));
    assert(memtree_is_empty(&tree));
    std::cout << " OK\n";

    // Boundary values
    std::cout << "Boundary values..." << std::flush;
    uint32_t min_key = 0;
    uint32_t max_key = UINT32_MAX;

    assert(memtree_insert(&tree, (uint8_t*)&min_key, (uint8_t*)&value, &ctx));
    assert(memtree_insert(&tree, (uint8_t*)&max_key, (uint8_t*)&value, &ctx));

    assert(memcursor_first(&cursor));
    assert(*(uint32_t*)memcursor_key(&cursor) == 0);

    assert(memcursor_last(&cursor));
    assert(*(uint32_t*)memcursor_key(&cursor) == UINT32_MAX);

    std::cout << " OK\n";

    // Clear tree
    std::cout << "Clear tree..." << std::flush;
    memtree_clear(&tree);
    assert(memtree_is_empty(&tree));
    std::cout << " OK\n";

    arena::reset<QueryArena>();
}

inline void test_memtree_varchar_keys() {
    std::cout << "\n=== MemTree VARCHAR Keys ===\n";

    arena::init<QueryArena>();
    TestContext ctx;

    MemTree tree = memtree_create(TYPE_CHAR32, sizeof(uint32_t), false);
    MemCursor cursor = {.tree = &tree, .ctx = &ctx};

    const char* test_strings[] = {
        "apple", "banana", "cherry", "date", "elderberry",
        "fig", "grape", "honeydew", "ice cream", "jackfruit"
    };

    // Insert strings
    std::cout << "Insert strings..." << std::flush;
    for (int i = 0; i < 10; i++) {
        char key[32] = {0};
        strncpy(key, test_strings[i], 31);
        uint32_t value = i;
        assert(memtree_insert(&tree, (uint8_t*)key, (uint8_t*)&value, &ctx));
    }
    std::cout << " OK\n";

    // Verify sorted order
    std::cout << "Verify order..." << std::flush;
    std::vector<std::string> sorted_order;
    if (memcursor_first(&cursor)) {
        do {
            char* key = (char*)memcursor_key(&cursor);
            sorted_order.push_back(std::string(key, strnlen(key, 32)));
        } while (memcursor_next(&cursor));
    }

    // Check ordering
    for (size_t i = 1; i < sorted_order.size(); i++) {
        assert(sorted_order[i-1] < sorted_order[i]);
    }
    std::cout << " OK\n";

    arena::reset<QueryArena>();
}

inline void test_memtree() {
    std::cout << "\n========== MemTree Tests ==========\n";

    test_memtree_sequential_ops();
    test_memtree_random_ops();
    test_memtree_duplicates();
    test_memtree_composite_keys();
    test_memtree_cursor_operations();
    test_memtree_edge_cases();
    test_memtree_varchar_keys();

    std::cout << "\n========== All MemTree tests passed! ==========\n";
}
