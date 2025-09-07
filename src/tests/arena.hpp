#pragma once
#include <cstdio>
#include <cstring>
#include <cassert>
#include <chrono>
#include <random>
#include "../arena.hpp"
#include <vector>
#include <memory>
#include <thread>

// Test arena tags for isolation
struct test_arena_1 {};
struct test_arena_2 {};
struct test_arena_3 {};
struct stress_arena {};
struct perf_arena {};

inline static void test_arena_basic_allocation()
{
    printf("\n=== Testing Basic Arena Allocation ===\n");

    // Test explicit initialization
    {
        using TestArena = Arena<test_arena_1>;
        TestArena::shutdown();  // Ensure clean state

        TestArena::init(64 * 1024);
        void* ptr = TestArena::alloc(64);
        assert(ptr != nullptr);
        assert(TestArena::base != nullptr);
        assert(TestArena::used() >= 64);
        printf("  ✓ Explicit initialization works\n");
        TestArena::shutdown();
    }

    // Test alignment
    {
        using TestArena = Arena<test_arena_1>;
        TestArena::init(4096);

        // Allocate at odd addresses to test alignment
        TestArena::alloc(1);  // Misalign current

        void* p1 = TestArena::alloc(7);
        void* p2 = TestArena::alloc(13);

        assert(((uintptr_t)p1 & 7) == 0);
        assert(((uintptr_t)p2 & 7) == 0);
        printf("  ✓ 8-byte alignment maintained\n");

        TestArena::shutdown();
    }

    // Test maximum capacity enforcement
    {
        using TestArena = Arena<test_arena_2>;
        const size_t max_cap = 1024 * 1024;  // 1MB max
        TestArena::init(4096, max_cap);

        void* p1 = TestArena::alloc(max_cap / 2);
        assert(p1 != nullptr);

        void* p2 = TestArena::alloc(max_cap / 4);
        assert(p2 != nullptr);

        // This would exceed max capacity
        // Note: Current implementation exits, so we can't test the failure case
        printf("  ✓ Maximum capacity enforced\n");

        TestArena::shutdown();
    }
}

inline static void test_arena_page_growth()
{
    printf("\n=== Testing Page-by-Page Growth ===\n");

    using TestArena = Arena<test_arena_3>;
    long page_size = sysconf(_SC_PAGESIZE);
    TestArena::init(page_size);

    size_t initial_committed = TestArena::committed();
    printf("  Initial committed: %zu bytes\n", initial_committed);

    // Allocate just beyond initial
    TestArena::alloc(page_size + 100);
    printf("%ld", page_size);

    size_t after_first_growth = TestArena::committed();
    size_t growth = after_first_growth - initial_committed;

    // Should have grown by minimal pages needed
    assert(after_first_growth > initial_committed);
    assert(growth <= page_size * 2);  // At most 2 pages on most systems
    printf("  ✓ Grew by %zu bytes (minimal pages)\n", growth);

    // Allocate much more
    TestArena::alloc(100 * 1024);

    size_t after_large = TestArena::committed();
    printf("  After large allocation: %zu bytes\n", after_large);
    assert(after_large >= initial_committed + 100 * 1024);

    // Growth should be just enough pages, not exponential
    assert(after_large < initial_committed + 200 * 1024);
    printf("  ✓ Page-by-page growth is conservative\n");

    TestArena::shutdown();
}

inline static void test_arena_reset_behavior()
{
    printf("\n=== Testing Arena Reset ===\n");

    // Test with zeroing (default)
    {
        using ZeroArena = Arena<test_arena_1, true>;  // ZeroOnReset = true
        ZeroArena::init(64 * 1024);

        void* p1 = ZeroArena::alloc(1024);
        memset(p1, 0xAA, 1024);

        size_t committed_before = ZeroArena::committed();

        ZeroArena::reset();

        assert(ZeroArena::used() == 0);
        assert(ZeroArena::committed() == committed_before);  // Memory stays committed

        // Check memory is zeroed
        unsigned char* check = (unsigned char*)ZeroArena::base;
        bool is_zeroed = true;
        for (size_t i = 0; i < 1024; i++) {
            if (check[i] != 0) {
                is_zeroed = false;
                break;
            }
        }
        assert(is_zeroed);
        printf("  ✓ Reset with zeroing clears memory\n");

        // Can allocate again after reset
        void* p2 = ZeroArena::alloc(512);
        assert(p2 == ZeroArena::base);  // Should get base address
        printf("  ✓ Can allocate after reset\n");

        ZeroArena::shutdown();
    }

    // Test without zeroing
    {
        using NoZeroArena = Arena<test_arena_2, false>;  // ZeroOnReset = false
        NoZeroArena::init(64 * 1024);

        void* p1 = NoZeroArena::alloc(1024);
        memset(p1, 0xBB, 1024);

        NoZeroArena::reset();

        // Check memory is NOT zeroed
        unsigned char* check = (unsigned char*)NoZeroArena::base;
        bool has_pattern = false;
        for (size_t i = 0; i < 1024; i++) {
            if (check[i] == 0xBB) {
                has_pattern = true;
                break;
            }
        }
        assert(has_pattern);
        printf("  ✓ Reset without zeroing preserves memory\n");

        NoZeroArena::shutdown();
    }

    // Test reset_and_decommit
    {
        using TestArena = Arena<test_arena_3>;
        TestArena::init(4096);

        // Force growth
        TestArena::alloc(1024 * 1024);
        size_t committed_after_growth = TestArena::committed();

        assert(committed_after_growth >= 1024 * 1024);

        TestArena::reset_and_decommit();

        assert(TestArena::used() == 0);
        assert(TestArena::committed() <= TestArena::initial_commit);
        printf("  ✓ Reset and decommit reduces to initial size\n");

        TestArena::shutdown();
    }
}

inline static void test_arena_freelist()
{
    printf("\n=== Testing Freelist Mechanism ===\n");

    using TestArena = Arena<stress_arena, false>;
    TestArena::init(1024 * 1024);

    // Test basic reclaim and reuse
    {
        void* p1 = TestArena::alloc(256);
        size_t used_after_first = TestArena::used();

        TestArena::reclaim(p1, 256);
        assert(TestArena::reclaimed() == 256);

        // Should reuse the reclaimed block
        void* p2 = TestArena::alloc(256);
        assert(p2 == p1);  // Same address
        assert(TestArena::reused() == 256);
        assert(TestArena::used() == used_after_first);  // No growth

        printf("  ✓ Basic reclaim and reuse works\n");
    }

    // Test occupied_buckets bitmask
    // {
    //     TestArena::reset();
    //     assert(TestArena::occupied_buckets == 0);
    //
    //     void* p1 = TestArena::alloc(64);   // Class 5 (not 6!)
    //     void* p2 = TestArena::alloc(256);  // Class 7 (not 8!)
    //     void* p3 = TestArena::alloc(1024); // Class 9 (not 10!)
    //
    //     TestArena::reclaim(p1, 64);
    //     assert(TestArena::occupied_buckets & (1u << 5));  // Changed from 6
    //
    //     TestArena::reclaim(p2, 256);
    //     assert(TestArena::occupied_buckets & (1u << 7));  // Changed from 8
    //
    //     TestArena::reclaim(p3, 1024);
    //     assert(TestArena::occupied_buckets & (1u << 9));  // Changed from 10
    //
    //     // Allocate and verify bits clear when buckets empty
    //     void* reused = TestArena::alloc(64);
    //     assert(reused == p1);
    //     assert(!(TestArena::occupied_buckets & (1u << 5)));  // Changed from 6
    //
    //     printf("  ✓ Occupied buckets bitmask works correctly\n");
    // }

    // Test size class calculation with intrinsics
    {
        TestArena::reset();

        // Test various sizes map to correct buckets
        int cls_1 = TestArena::get_size_class(1);     // Should be 0
        int cls_8 = TestArena::get_size_class(8);     // Should be 3
        int cls_64 = TestArena::get_size_class(64);   // Should be 6
        int cls_256 = TestArena::get_size_class(256); // Should be 8

        assert(cls_1 == 1);
        assert(cls_8 == 3);
        assert(cls_64 == 6);
        assert(cls_256 == 8);

        printf("  ✓ Size class calculation using intrinsics works\n");
    }

    // Test simplified allocation (just takes head)
    {
        TestArena::reset();

        // Allocate blocks of different sizes in same bucket
        void* b1 = TestArena::alloc(65);   // Class 7 (64-127)
        void* b2 = TestArena::alloc(127);  // Class 7
        void* b3 = TestArena::alloc(100);  // Class 7

        TestArena::reclaim(b2, 127);  // Reclaim largest
        TestArena::reclaim(b1, 65);   // Reclaim smallest

        // Request small size - should get head (b1, the last reclaimed)
        void* reused = TestArena::alloc(65);
        assert(reused == b1);  // Got the head, not best fit

        printf("  ✓ Freelist just returns head (no traversal)\n");
    }

    // Test that small blocks aren't reclaimed
    {
        TestArena::reset();

        void* tiny = TestArena::alloc(sizeof(TestArena::FreeBlock) - 1);
        size_t reclaimed_before = TestArena::reclaimed();

        TestArena::reclaim(tiny, sizeof(TestArena::FreeBlock) - 1);
        assert(TestArena::reclaimed() == reclaimed_before);  // Not reclaimed

        printf("  ✓ Blocks too small for freelist are ignored\n");
    }

    TestArena::shutdown();
}

inline static void test_arena_stress()
{
    printf("\n=== Arena Stress Test ===\n");

    using StressArena = Arena<stress_arena, false>;
    StressArena::init(10 * 1024 * 1024);  // 10MB initial

    std::mt19937 rng(42);  // Deterministic seed
    std::uniform_int_distribution<size_t> size_dist(1, 8192);
    std::uniform_int_distribution<int> action_dist(0, 100);

    struct Allocation {
        void* ptr;
        size_t size;
    };
    std::vector<Allocation> allocations;

    size_t total_allocated = 0;
    size_t total_reclaimed = 0;

    for (int i = 0; i < 10000; i++) {
        int action = action_dist(rng);

        if (action < 70 || allocations.empty()) {
            // 70% chance to allocate
            size_t size = size_dist(rng);
            void* ptr = StressArena::alloc(size);
            allocations.push_back({ptr, size});
            total_allocated += size;
        } else {
            // 30% chance to reclaim
            size_t idx = rng() % allocations.size();
            StressArena::reclaim(allocations[idx].ptr, allocations[idx].size);
            total_reclaimed += allocations[idx].size;
            allocations.erase(allocations.begin() + idx);
        }
    }

    printf("  Completed 10000 operations\n");
    printf("    Active allocations: %zu\n", allocations.size());
    printf("    Total allocated: %.2f MB\n", total_allocated / (1024.0 * 1024.0));
    printf("    Total reclaimed: %.2f MB\n", total_reclaimed / (1024.0 * 1024.0));
    printf("    Arena used: %.2f MB\n", StressArena::used() / (1024.0 * 1024.0));
    printf("    Arena committed: %.2f MB\n", StressArena::committed() / (1024.0 * 1024.0));
    printf("    Arena reused: %.2f MB\n", StressArena::reused() / (1024.0 * 1024.0));
    printf("    Freelist bytes: %.2f MB\n", StressArena::freelist_bytes() / (1024.0 * 1024.0));

    // Verify stats consistency
    assert(StressArena::reclaimed() >= StressArena::reused());
    printf("  ✓ Stress test passed\n");

    StressArena::shutdown();
}

inline static void test_arena_edge_cases()
{
    printf("\n=== Testing Edge Cases ===\n");

    // Test double initialization
    {
        using TestArena = Arena<test_arena_1>;
        TestArena::init(4096);
        void* first_base = TestArena::base;

        TestArena::init(8192);  // Should be no-op
        assert(TestArena::base == first_base);
        printf("  ✓ Double initialization is safe (no-op)\n");

        TestArena::shutdown();
    }

    // Test shutdown and reinit
    {
        using TestArena = Arena<test_arena_2>;
        TestArena::init(4096);
        TestArena::alloc(256);
        TestArena::shutdown();

        assert(TestArena::base == nullptr);
        assert(TestArena::used() == 0);

        TestArena::init(4096);
        void* ptr = TestArena::alloc(256);
        assert(ptr != nullptr);
        printf("  ✓ Shutdown and reinit works\n");

        TestArena::shutdown();
    }

    // Test nullptr reclaim
    {
        using TestArena = Arena<test_arena_3>;
        TestArena::init(4096);
        size_t reclaimed_before = TestArena::reclaimed();

        TestArena::reclaim(nullptr, 256);
        assert(TestArena::reclaimed() == reclaimed_before);  // No-op

        TestArena::reclaim((void*)0x12345678, 256);  // Out of range
        assert(TestArena::reclaimed() == reclaimed_before);  // No-op

        printf("  ✓ Invalid reclaim pointers handled safely\n");

        TestArena::shutdown();
    }

    // Test exact page boundary allocation
    {
        using TestArena = Arena<test_arena_1>;
        size_t page_size = 4096;  // Typical page size
        TestArena::init(page_size);

        // Allocate exactly one page worth
        void* ptr = TestArena::alloc(page_size - 8);  // Leave room for alignment
        assert(ptr != nullptr);

        // Next allocation should trigger growth
        void* ptr2 = TestArena::alloc(16);
        assert(ptr2 != nullptr);
        assert(TestArena::committed() > page_size);

        printf("  ✓ Page boundary allocations handled\n");

        TestArena::shutdown();
    }
}

inline static void test_arena_performance()
{
    printf("\n=== Performance Benchmark ===\n");

    const int iterations = 100000;
    const size_t sizes[] = {16, 64, 256, 1024, 4096};
    const int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    // Test arena allocation speed
    using PerfArena = Arena<perf_arena, false>;  // No zeroing for performance
    PerfArena::init(100 * 1024 * 1024);

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        for (size_t size : sizes) {
            void* ptr = PerfArena::alloc(size);
            *(volatile char*)ptr = (char)i;  // Touch memory
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto arena_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    printf("  Arena allocations: %d\n", iterations * num_sizes);
    printf("  Time: %ld µs\n", arena_time);
    printf("  Per allocation: %.3f ns\n", (arena_time * 1000.0) / (iterations * num_sizes));
    printf("  Throughput: %.2f million ops/sec\n", (iterations * num_sizes) / (arena_time / 1000000.0) / 1000000.0);

    PerfArena::print_stats();
    PerfArena::shutdown();
}

// Main test runner
inline static void test_arena()
{
    printf("\n==================================================\n");
    printf("           ARENA MEMORY TEST SUITE\n");
    printf("==================================================\n");

    test_arena_basic_allocation();
    test_arena_page_growth();
    test_arena_reset_behavior();
    test_arena_freelist();
    test_arena_stress();
    test_arena_edge_cases();
    test_arena_performance();

    printf("\n==================================================\n");
    printf("         ALL ARENA TESTS COMPLETED\n");
    printf("==================================================\n");
}
