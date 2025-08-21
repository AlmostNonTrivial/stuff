#pragma once
#include <typeinfo>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

// Cross-platform virtual memory operations
struct VirtualMemory {
    static void* reserve(size_t size) {
        #ifdef _WIN32
            return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE);
        #else
            void* ptr = mmap(nullptr, size, PROT_NONE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            return (ptr == MAP_FAILED) ? nullptr : ptr;
        #endif
    }

    static bool commit(void* addr, size_t size) {
        #ifdef _WIN32
            return VirtualAlloc(addr, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
        #else
            return mprotect(addr, size, PROT_READ | PROT_WRITE) == 0;
        #endif
    }

    static void decommit(void* addr, size_t size) {
        #ifdef _WIN32
            VirtualFree(addr, size, MEM_DECOMMIT);
        #else
            madvise(addr, size, MADV_DONTNEED);
            mprotect(addr, size, PROT_NONE);
        #endif
    }

    static void release(void* addr, size_t size) {
        #ifdef _WIN32
            (void)size;  // Windows doesn't need size for MEM_RELEASE
            VirtualFree(addr, 0, MEM_RELEASE);
        #else
            munmap(addr, size);
        #endif
    }

    static size_t page_size() {
        static size_t cached_size = 0;
        if (cached_size == 0) {
            #ifdef _WIN32
                SYSTEM_INFO si;
                GetSystemInfo(&si);
                cached_size = si.dwPageSize;
            #else
                cached_size = sysconf(_SC_PAGESIZE);
            #endif
        }
        return cached_size;
    }

    static size_t round_to_pages(size_t size) {
        size_t page_sz = page_size();
        return ((size + page_sz - 1) / page_sz) * page_sz;
    }
};

// Arena with virtual memory backing
template <typename Tag> struct Arena {
    static inline uint8_t *base = nullptr;
    static inline uint8_t *current = nullptr;
    static inline size_t reserved_capacity = 0;  // Virtual address space reserved
    static inline size_t committed_capacity = 0; // Physical memory committed
    static inline size_t max_capacity = 0;       // Optional limit
    static inline size_t initial_commit = 0;     // Initial commit size

    // Initialize with initial size and optional maximum
    // initial: how much memory to commit right away (0 = on demand)
    // maximum: maximum memory this arena can use (0 = unlimited up to system limits)
    static void init(size_t initial = 4 * 1024 * 1024,  // 4MB default initial
                     size_t maximum = 0) {
        if (base) return;  // Already initialized

        initial_commit = VirtualMemory::round_to_pages(initial);
        max_capacity = maximum;

        // Reserve virtual address space
        // If there's a max, reserve that. Otherwise reserve something huge.
        reserved_capacity = max_capacity ? max_capacity : (1ULL << 38);  // 256GB if no limit

        base = (uint8_t*)VirtualMemory::reserve(reserved_capacity);
        if (!base) {
            // Fallback: try smaller reservation
            reserved_capacity = 1ULL << 33;  // 8GB
            base = (uint8_t*)VirtualMemory::reserve(reserved_capacity);

            if (!base) {
                fprintf(stderr, "Failed to reserve virtual memory\n");
                exit(1);
            }
        }

        current = base;
        committed_capacity = 0;

        // Commit initial memory if requested
        if (initial_commit > 0) {
            if (!VirtualMemory::commit(base, initial_commit)) {
                fprintf(stderr, "Failed to commit initial memory: %zu bytes\n", initial_commit);
                VirtualMemory::release(base, reserved_capacity);
                base = nullptr;
                exit(1);
            }
            committed_capacity = initial_commit;
        }
    }

    // Shutdown and free all memory
    static void shutdown() {
        if (base) {
            VirtualMemory::release(base, reserved_capacity);
            base = nullptr;
            current = nullptr;
            reserved_capacity = 0;
            committed_capacity = 0;
            max_capacity = 0;
        }
    }

    // Allocate memory from arena
    static void *alloc(size_t size) {
        // Auto-initialize if needed
        if (!base) {
            init();
        }

        // Align to 16 bytes
        size_t align = 16;
        uintptr_t current_addr = (uintptr_t)current;
        uintptr_t aligned_addr = (current_addr + align - 1) & ~(align - 1);

        uint8_t *aligned = (uint8_t *)aligned_addr;
        uint8_t *next = aligned + size;

        // Check if we need to commit more memory
        if (next > base + committed_capacity) {
            size_t needed = next - base;

            // Check against maximum capacity if set
            if (max_capacity > 0 && needed > max_capacity) {
                fprintf(stderr, "Arena exhausted: requested %zu, max %zu\n",
                        needed, max_capacity);
                exit(1);
            }

            // Check against reserved capacity
            if (needed > reserved_capacity) {
                fprintf(stderr, "Arena exhausted: requested %zu, reserved %zu\n",
                        needed, reserved_capacity);
                exit(1);
            }

            // Calculate how much to commit
            size_t new_committed = VirtualMemory::round_to_pages(needed);

            // Grow by at least 50% of current committed or initial size
            size_t min_growth = committed_capacity > 0
                ? committed_capacity + committed_capacity / 2
                : initial_commit;
            if (new_committed < min_growth) {
                new_committed = min_growth;
            }

            // Respect maximum if set
            if (max_capacity > 0 && new_committed > max_capacity) {
                new_committed = max_capacity;
            }

            // Don't exceed reservation
            if (new_committed > reserved_capacity) {
                new_committed = reserved_capacity;
            }

            // Commit the new range
            size_t commit_size = new_committed - committed_capacity;
            if (!VirtualMemory::commit(base + committed_capacity, commit_size)) {
                fprintf(stderr, "Failed to commit memory: %zu bytes\n", commit_size);
                exit(1);
            }

            committed_capacity = new_committed;
        }

        current = next;
        return aligned;
    }

    // Reset arena (keep memory committed)
    static void reset() {
        current = base;
        if (base && committed_capacity > 0) {
            memset(base, 0, committed_capacity);
        }
    }

    // Reset and decommit memory back to initial size
    static void reset_and_decommit() {
        current = base;

        if (committed_capacity > initial_commit) {
            // Decommit everything beyond initial
            VirtualMemory::decommit(base + initial_commit,
                                  committed_capacity - initial_commit);
            committed_capacity = initial_commit;
        }

        if (base && committed_capacity > 0) {
            memset(base, 0, committed_capacity);
        }
    }

    // Query functions
    static size_t used() { return current - base; }
    static size_t committed() { return committed_capacity; }
    static size_t reserved() { return reserved_capacity; }

    // Get stats
    static void print_stats() {
        printf("Arena<%s>:\n", typeid(Tag).name());
        printf("  Used:      %zu bytes (%.2f MB)\n",
               used(), used() / (1024.0 * 1024.0));
        printf("  Committed: %zu bytes (%.2f MB)\n",
               committed_capacity, committed_capacity / (1024.0 * 1024.0));
        printf("  Reserved:  %zu bytes (%.2f MB)\n",
               reserved_capacity, reserved_capacity / (1024.0 * 1024.0));
        if (max_capacity > 0) {
            printf("  Maximum:   %zu bytes (%.2f MB)\n",
                   max_capacity, max_capacity / (1024.0 * 1024.0));
        }
    }
};

// Convenience namespace
namespace arena {
    template <typename Tag>
    void init(size_t initial = 4 * 1024 * 1024, size_t maximum = 0) {
        Arena<Tag>::init(initial, maximum);
    }

    template <typename Tag> void shutdown() {
        Arena<Tag>::shutdown();
    }

    template <typename Tag> void reset() {
        Arena<Tag>::reset();
    }

    template <typename Tag> void reset_and_decommit() {
        Arena<Tag>::reset_and_decommit();
    }

    template <typename Tag> void *alloc(size_t size) {
        return Arena<Tag>::alloc(size);
    }

    template <typename Tag> size_t used() {
        return Arena<Tag>::used();
    }

    template <typename Tag> size_t committed() {
        return Arena<Tag>::committed();
    }

    template <typename Tag> void print_stats() {
        Arena<Tag>::print_stats();
    }
}
