#pragma once
#include "../arena.hpp"
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <vector>
#include <cstdio>

struct test_arena {};
using TestArena = Arena<test_arena>;

struct Allocation {
    void* ptr;
    size_t size;
};

inline void
fuzz_arena(int reclaim_ratio)
{
    TestArena::reset_and_decommit();  // Clean slate for each run

    std::vector<Allocation> active;

    /* Stats */
    size_t total_allocations = 0;
    size_t total_bytes_allocated = 0;
    size_t total_reclaims = 0;
    size_t total_bytes_reclaimed = 0;

    const int iterations = 50000;

    /* Size distribution - small to medium allocations */
    const size_t size_distribution[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 28, 32,           // Tiny
        40, 48, 56, 64, 80, 96, 112, 128,                         // Small
        160, 192, 224, 256, 320, 384, 448, 512,                   // Medium-small
        640, 768, 896, 1024, 1280, 1536, 1792, 2048,              // Medium
        2560, 3072, 3584, 4096, 5120, 6144, 7168, 8192,           // Medium-large
    };
    const int size_count = sizeof(size_distribution) / sizeof(size_distribution[0]);

    for (int i = 0; i < iterations; i++)
    {
        bool should_reclaim = !active.empty() && (std::rand() % 100 < reclaim_ratio);

        if (should_reclaim)
        {
            /* Pick random allocation to reclaim */
            size_t idx = std::rand() % active.size();
            Allocation& alloc = active[idx];

            TestArena::reclaim(alloc.ptr, alloc.size);
            total_reclaims++;
            total_bytes_reclaimed += alloc.size;

            /* Remove from active list */
            active[idx] = active.back();
            active.pop_back();
        }
        else
        {
            /* Pick random size and allocate */
            size_t size = size_distribution[std::rand() % size_count];
            size = size + (std::rand() % (size / 4 + 1));  // Add variance

            void* ptr = TestArena::alloc(size);
            if (ptr)
            {
                memset(ptr, (uint8_t)(i & 0xFF), size);
                active.push_back({ptr, size});
                total_allocations++;
                total_bytes_allocated += size;
            }
        }
    }

    /* Print results for this ratio */
    printf("\n========================================\n");
    printf("   RECLAIM RATIO: %d%%\n", reclaim_ratio);
    printf("========================================\n");

    printf("Operations:\n");
    printf("  Total allocations:     %zu\n", total_allocations);
    printf("  Total reclaims:        %zu\n", total_reclaims);
    printf("  Currently active:      %zu\n\n", active.size());

    printf("Memory:\n");
    printf("  Total allocated:       %zu bytes (%.2f MB)\n",
           total_bytes_allocated, total_bytes_allocated / (1024.0 * 1024.0));
    printf("  Total reclaimed:       %zu bytes (%.2f MB)\n",
           total_bytes_reclaimed, total_bytes_reclaimed / (1024.0 * 1024.0));
    printf("  Net allocated:         %zu bytes (%.2f MB)\n",
           total_bytes_allocated - total_bytes_reclaimed,
           (total_bytes_allocated - total_bytes_reclaimed) / (1024.0 * 1024.0));

    printf("\nArena internals:\n");
    printf("  Arena used:            %zu bytes (%.2f MB)\n",
           TestArena::used(), TestArena::used() / (1024.0 * 1024.0));
    printf("  Arena committed:       %zu bytes (%.2f MB)\n",
           TestArena::committed(), TestArena::committed() / (1024.0 * 1024.0));
    printf("  Bytes in freelists:    %zu bytes (%.2f MB)\n",
           TestArena::freelist_bytes(), TestArena::freelist_bytes() / (1024.0 * 1024.0));

    double reuse_rate = (total_bytes_allocated > 0)
        ? (100.0 * TestArena::reused() / total_bytes_allocated)
        : 0;
    printf("\nEfficiency:\n");
    printf("  Bytes reused:          %zu (%.2f MB)\n",
           TestArena::reused(), TestArena::reused() / (1024.0 * 1024.0));
    printf("  Reuse rate:            %.2f%% of total allocated\n", reuse_rate);
}

inline void
test_arena()
{
    std::srand(42);  // Deterministic results

    /* Initialize once with enough space */
    TestArena::init();

    printf("Arena Fuzzing Test - 50,000 operations per run\n");
    printf("Testing different reclaim ratios to observe freelist behavior\n\n");

    /*
     * Test with different reclaim ratios to see how freelists perform.
     *
     * Expected behavior:
     * - 10% reclaim: Arena grows continuously, minimal reuse
     * - 30% reclaim: Some reuse, but arena still grows
     * - 50% reclaim: Balanced, significant reuse, arena stabilizes
     * - 70% reclaim: High reuse, arena reaches steady state quickly
     * - 90% reclaim: Very high reuse, minimal new allocations needed
     */

    int ratios[] = {10, 30, 50, 70, 90};
    for (int i = 0; i < 5; i++)
    {
        fuzz_arena(ratios[i]);
    }


    TestArena::shutdown();
}
