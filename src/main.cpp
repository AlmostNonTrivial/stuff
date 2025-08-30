#include "arena.hpp"
#include <cstdint>
#include <random>
#include <chrono>
#include <unordered_map>
#include <cassert>
#include <vector>

// Structure to hold operation data for replay
struct Operation {
    enum Type { INSERT, LOOKUP, DELETE, UPDATE };
    Type type;
    int key_idx;
    uint32_t value;
};

void performance_comparison_test() {
    printf("=== STRING MAP vs STD::UNORDERED_MAP PERFORMANCE COMPARISON ===\n\n");

    // Generate test strings
    const int POOL_SIZE = 10000;
    std::vector<std::string> string_pool;
    string_pool.reserve(POOL_SIZE);

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> key_len_dist(1, 100);
    std::uniform_int_distribution<int> char_dist(33, 126);

    printf("Generating %d test strings...\n", POOL_SIZE);
    for (int i = 0; i < POOL_SIZE; i++) {
        int len = key_len_dist(rng);
        std::string str;
        str.reserve(len);

        int pattern = i % 5;
        switch (pattern) {
            case 0: // Random
                for (int j = 0; j < len; j++) {
                    str += char(char_dist(rng));
                }
                break;
            case 1: // Common prefix
                str = std::string(len, 'A');
                for (int j = len - 5; j < len && j >= 0; j++) {
                    str[j] = char_dist(rng);
                }
                break;
            case 2: // Common suffix
                str = std::string(len, 'Z');
                for (int j = 0; j < 5 && j < len; j++) {
                    str[j] = char_dist(rng);
                }
                break;
            case 3: // Repetitive
                for (int j = 0; j < len; j++) {
                    str += char('A' + (j % 3));
                }
                if (len > 0) str[len - 1] = '0' + (i % 10);
                break;
            case 4: // Near-duplicates
                str = std::string(len, 'X');
                if (len > 0) str[len / 2] = '0' + (i % 62);
                break;
        }
        string_pool.push_back(str);
    }

    // Generate operations sequence
    const int ITERATIONS = 1000000;
    std::vector<Operation> operations;
    operations.reserve(ITERATIONS);

    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<uint32_t> value_dist(0, 0xFFFFFFFF);

    printf("Generating %d operations...\n", ITERATIONS);
    for (int i = 0; i < ITERATIONS; i++) {
        Operation op;
        int op_type = op_dist(rng);
        op.key_idx = rng() % POOL_SIZE;
        op.value = value_dist(rng);

        if (op_type < 40) {
            op.type = Operation::INSERT;
        } else if (op_type < 70) {
            op.type = Operation::LOOKUP;
        } else if (op_type < 85) {
            op.type = Operation::DELETE;
        } else {
            op.type = Operation::UPDATE;
        }

        operations.push_back(op);
    }

    printf("\n");

    // Test std::unordered_map
    {
        printf("Testing std::unordered_map...\n");
        std::unordered_map<std::string, uint32_t> std_map;
        std_map.reserve(POOL_SIZE);

        size_t insert_count = 0, lookup_count = 0, delete_count = 0, update_count = 0;
        size_t lookup_hits = 0, delete_hits = 0;

        auto start = std::chrono::high_resolution_clock::now();

        for (const auto& op : operations) {
            const std::string& key = string_pool[op.key_idx];

            switch (op.type) {
                case Operation::INSERT:
                    std_map[key] = op.value;
                    insert_count++;
                    break;

                case Operation::LOOKUP: {
                    auto it = std_map.find(key);
                    if (it != std_map.end()) {
                        volatile uint32_t v = it->second; // Prevent optimization
                        (void)v;
                        lookup_hits++;
                    }
                    lookup_count++;
                    break;
                }

                case Operation::DELETE:
                    if (std_map.erase(key) > 0) {
                        delete_hits++;
                    }
                    delete_count++;
                    break;

                case Operation::UPDATE:
                    if (std_map.find(key) != std_map.end()) {
                        std_map[key] = op.value;
                        update_count++;
                    }
                    break;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        printf("  Time:              %lld μs (%.3f ms)\n", duration.count(), duration.count() / 1000.0);
        printf("  Ops/second:        %.0f\n", (ITERATIONS * 1000000.0) / duration.count());
        printf("  Final size:        %zu\n", std_map.size());
        printf("  Inserts:           %zu\n", insert_count);
        printf("  Lookups:           %zu (hits: %zu)\n", lookup_count, lookup_hits);
        printf("  Deletes:           %zu (hits: %zu)\n", delete_count, delete_hits);
        printf("  Updates:           %zu\n", update_count);
        printf("\n");
    }

    // Test string_map
    {
        printf("Testing string_map...\n");

        struct TestArena {};
        Arena<TestArena>::init(64 * 1024 * 1024, 256 * 1024 * 1024);

        string_map<uint32_t, TestArena> m;
        stringmap_init(&m, POOL_SIZE);

        size_t insert_count = 0, lookup_count = 0, delete_count = 0, update_count = 0;
        size_t lookup_hits = 0, delete_hits = 0;

        auto start = std::chrono::high_resolution_clock::now();

        for (const auto& op : operations) {
            const char* key = string_pool[op.key_idx].c_str();

            switch (op.type) {
                case Operation::INSERT:
                    stringmap_insert(&m, key, op.value);
                    insert_count++;
                    break;

                case Operation::LOOKUP: {
                    uint32_t* result = stringmap_get(&m, key);
                    if (result) {
                        volatile uint32_t v = *result; // Prevent optimization
                        (void)v;
                        lookup_hits++;
                    }
                    lookup_count++;
                    break;
                }

                case Operation::DELETE:
                    if (stringmap_delete(&m, key)) {
                        delete_hits++;
                    }
                    delete_count++;
                    break;

                case Operation::UPDATE:
                    if (stringmap_get(&m, key)) {
                        stringmap_insert(&m, key, op.value);
                        update_count++;
                    }
                    break;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        printf("  Time:              %lld μs (%.3f ms)\n", duration.count(), duration.count() / 1000.0);
        printf("  Ops/second:        %.0f\n", (ITERATIONS * 1000000.0) / duration.count());
        printf("  Final size:        %u\n", m.size);
        printf("  Capacity:          %u\n", m.capacity);
        printf("  Tombstones:        %u\n", m.tombstones);
        printf("  Inserts:           %zu\n", insert_count);
        printf("  Lookups:           %zu (hits: %zu)\n", lookup_count, lookup_hits);
        printf("  Deletes:           %zu (hits: %zu)\n", delete_count, delete_hits);
        printf("  Updates:           %zu\n", update_count);
        printf("\n");
        printf("  Arena Statistics:\n");
        printf("    Used:            %.2f MB\n", Arena<TestArena>::used() / (1024.0 * 1024.0));
        printf("    Committed:       %.2f MB\n", Arena<TestArena>::committed() / (1024.0 * 1024.0));
        printf("    Reclaimed:       %.2f MB\n", Arena<TestArena>::reclaimed() / (1024.0 * 1024.0));
        printf("    Reused:          %.2f MB\n", Arena<TestArena>::reused() / (1024.0 * 1024.0));

        Arena<TestArena>::shutdown();
    }

    // Additional focused benchmarks
    printf("\n=== FOCUSED BENCHMARKS ===\n\n");

    // Benchmark: Sequential inserts
    {
        printf("Sequential Insert Test (10,000 unique keys):\n");

        // std::unordered_map
        {
            std::unordered_map<std::string, uint32_t> std_map;
            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < 10000; i++) {
                std_map[string_pool[i]] = i;
            }
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            printf("  std::unordered_map: %lld μs\n", duration.count());
        }

        // string_map
        {
            struct BenchArena {};
            Arena<BenchArena>::init(8 * 1024 * 1024);
            string_map<uint32_t, BenchArena> m;
            stringmap_init(&m);

            auto start = std::chrono::high_resolution_clock::now();
            for (uint32_t i = 0; i < 10000; i++) {
                stringmap_insert(&m, string_pool[i].c_str(), i);
            }
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            printf("  string_map:         %lld μs\n", duration.count());

            Arena<BenchArena>::shutdown();
        }
    }

    // Benchmark: Lookup-heavy workload
    {
        printf("\nLookup Test (100,000 lookups on 5,000 keys):\n");

        // Prepare both maps with same data
        std::unordered_map<std::string, uint32_t> std_map;
        for (int i = 0; i < 5000; i++) {
            std_map[string_pool[i]] = i;
        }

        struct LookupArena {};
        Arena<LookupArena>::init(8 * 1024 * 1024);
        string_map<uint32_t, LookupArena> m;
        stringmap_init(&m);
        for (uint32_t i = 0; i < 5000; i++) {
            stringmap_insert(&m, string_pool[i].c_str(), i);
        }

        // std::unordered_map lookups
        {
            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < 100000; i++) {
                auto it = std_map.find(string_pool[i % 5000]);
                volatile uint32_t v = it->second;
                (void)v;
            }
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            printf("  std::unordered_map: %lld μs\n", duration.count());
        }

        // string_map lookups
        {
            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < 100000; i++) {
                uint32_t* result = stringmap_get(&m, string_pool[i % 5000].c_str());
                volatile uint32_t v = *result;
                (void)v;
            }
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            printf("  string_map:         %lld μs\n", duration.count());
        }

        Arena<LookupArena>::shutdown();
    }

    printf("\n✓ COMPARISON TEST COMPLETE\n");
}

int main() {
    try {
        performance_comparison_test();
    } catch (const std::exception& e) {
        fprintf(stderr, "Test failed with exception: %s\n", e.what());
        return 1;
    }

    return 0;
}
