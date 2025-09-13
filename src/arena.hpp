#pragma once
#include <cassert>
#include <cstring>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>
#include <algorithm>
#include "os_layer.hpp"
#include "pager.hpp"
#include <cstring>
#include <cstdint>
#include <string_view>
#include <typeinfo>
#include <type_traits>

struct global_arena
{
};

/*
 * Memory arena with virtual memory backing and freelist-based reclamation.
 *
 * Key design decisions:
 * - Uses virtual memory reserve/commit pattern to avoid fragmentation
 * - Static storage per Tag type
 * - Power-of-2 freelists for O(1) reclamation and reuse
 *
 * Usage pattern:
 * 1. Arena<MyTag>::init() reserves virtual address space
 * 2. Allocations commit pages as needed
 * 3. Containers can reclaim() memory when growing
 * 4. reset() nukes everything but keeps pages committed
 * 4. reset_and_decommit() nukes everything and give's back pages
 */
template <typename Tag = global_arena, bool zero_on_reset = true, size_t Align = 8> struct arena
{
	static_assert((Align & (Align - 1)) == 0, "Alignment must be power of 2");

	static inline uint8_t *base = nullptr;
	static inline uint8_t *current = nullptr;
	static inline size_t   reserved_capacity = 0;
	static inline size_t   committed_capacity = 0;
	static inline size_t   max_capacity = 0;
	static inline size_t   initial_commit = 0;

	struct free_block
	{
		free_block *next;
		size_t		size;
	};

	/*
	 * Freelist buckets organized by power-of-2 size classes.
	 * freelists[4] = blocks of size [16, 32)
	 * freelists[5] = blocks of size [32, 64)
	 * etc.
	 * This gives us O(1) bucket selection via bit manipulation.
	 */
	static inline free_block *freelists[32] = {};
	static inline uint32_t	  occupied_buckets = 0; // Bitmask: which buckets have blocks

	// Diagnostic counters
	static inline size_t total_allocations = 0;
	static inline size_t total_reclamations = 0;
	static inline size_t total_bytes_allocated = 0;
	static inline size_t peak_usage = 0;
	static inline size_t reclaimed_bytes = 0;
	static inline size_t reused_bytes = 0;
	static inline size_t allocation_histogram[32] = {};
	static inline std::chrono::steady_clock::time_point init_time;

	// Arena registry for cross-arena diagnostics
	struct arena_info
	{
		const char *tag_name;
		uint8_t    *base;
		size_t      reserved;
		void       *arena_ptr;
	};
	static inline std::vector<arena_info> *arena_registry = nullptr;

	static void
	register_arena()
	{
		if (!arena_registry)
		{
			arena_registry = new std::vector<arena_info>();
		}
		arena_info info;
		info.tag_name = typeid(Tag).name();
		info.base = base;
		info.reserved = reserved_capacity;
		info.arena_ptr = nullptr;
		arena_registry->push_back(info);
	}

	static void
	init(size_t initial = PAGE_SIZE, size_t maximum = 0)
	{
		if (base)
		{
			return;
		}

		initial_commit = virtual_memory::round_to_pages(initial);
		max_capacity = maximum;

		/*
		 * Reserve a huge virtual address range upfront.
		 * This costs nothing on 64-bit systems
		 * We'll commit physical pages lazily as needed.
		 *
		 * This means that each arena can have it's own address space giving it a
		 * contiguous view of memory
		 */
		reserved_capacity = max_capacity ? max_capacity : (1ULL << 33); // 8GB

		base = (uint8_t *)virtual_memory::reserve(reserved_capacity);
		if (!base)
		{
			fprintf(stderr, "Failed to reserve virtual memory\n");
			exit(1);
		}

		current = base;
		committed_capacity = 0;

		if (initial_commit > 0)
		{
			if (!virtual_memory::commit(base, initial_commit))
			{
				fprintf(stderr, "Failed to commit initial memory: %zu bytes\n", initial_commit);
				virtual_memory::release(base, reserved_capacity);
				base = nullptr;
				exit(1);
			}
			committed_capacity = initial_commit;
		}

		for (int i = 0; i < 32; i++)
		{
			freelists[i] = nullptr;
			allocation_histogram[i] = 0;
		}
		occupied_buckets = 0;

		// Reset diagnostic counters
		total_allocations = 0;
		total_reclamations = 0;
		total_bytes_allocated = 0;
		peak_usage = 0;
		reclaimed_bytes = 0;
		reused_bytes = 0;
		init_time = std::chrono::steady_clock::now();

		register_arena();

		printf("Arena<%s> initialized at %p (size: %zu)\n", typeid(Tag).name(), base, reserved_capacity);
	}

	static void
	shutdown()
	{
		if (base)
		{
			virtual_memory::release(base, reserved_capacity);
			base = nullptr;
			current = nullptr;
			reserved_capacity = 0;
			committed_capacity = 0;
			max_capacity = 0;

			for (int i = 0; i < 32; i++)
			{
				freelists[i] = nullptr;
			}
			occupied_buckets = 0;

			total_allocations = 0;
			total_reclamations = 0;
			total_bytes_allocated = 0;
			peak_usage = 0;
			reclaimed_bytes = 0;
			reused_bytes = 0;
		}
	}

	/*
	 * Maps allocation size to freelist bucket index.
	 *
	 * We need the index i such that 2^i <= size < 2^(i+1)
	 * This is just finding the position of the highest set bit.
	 *
	 * The XOR dance at the end clamps the result to [0, 31] branchlessly.
	 */
	static inline int
	get_size_class(size_t size)
	{
		size = size | 0x2;
		/*
		 * The OR with 0x2 handles edge cases:
		 * - size=0 becomes 2, avoiding undefined behavior in clz(-1)
		 * - size=1 becomes 3, mapping to bucket 1 instead of 0
		 */

#ifdef _MSC_VER
		unsigned long index;
		_BitScanReverse64(&index, size - 1);
		return (int)index ^ ((index ^ 31) & -((int)index > 31));
#else
		int cls = 63 - __builtin_clzll(size - 1);
		return cls ^ ((cls ^ 31) & -(cls > 31));
#endif
	}

	/*
	 * Called by containers when they grow and abandon their old buffer.
	 * We add it to the appropriate freelist for future reuse.
	 */
	static void
	reclaim(void *ptr, size_t size)
	{
		if (!ptr || size < sizeof(free_block))
		{
			return;
		}

		uint8_t *addr = (uint8_t *)ptr;
		assert(!(addr < base || addr >= base + reserved_capacity));

		assert(addr < current);
		assert(size > 0);

		int size_class = get_size_class(size);

		free_block *block = (free_block *)ptr;
		block->size = size;
		block->next = freelists[size_class];
		freelists[size_class] = block;

		occupied_buckets |= (1u << size_class);

		reclaimed_bytes += size;
		total_reclamations++;
	}

	/*
	 * Check freelists for a suitable reclaimed block but
	 * fall back to bump allocation from current pointer
	 *
	 * We use the occupied_buckets bitmask to quickly find the smallest
	 * bucket that can satisfy our request.
	 */
	static void *
	try_alloc_from_freelist(size_t size)
	{
		int size_class = get_size_class(size);

		/*
		 * If size is exactly 2^n, it fits in bucket n.
		 * If size is 2^n + 1, we need bucket n+1.
		 */
		if (size > (1u << size_class))
		{
			size_class++;
		}

		/*
		 * Create a mask of all buckets >= size_class.
		 * Then AND with occupied_buckets to find available buckets.
		 */
		uint32_t mask = ~((1u << size_class) - 1);
		uint32_t candidates = occupied_buckets & mask;

		if (!candidates)
		{
			return nullptr;
		}

		/* Find the lowest set bit = smallest suitable bucket */
#ifdef _MSC_VER
		unsigned long cls;
		_BitScanForward(&cls, candidates);
#else
		int cls = __builtin_ctz(candidates);
#endif

		free_block *block = freelists[cls];
		freelists[cls] = block->next;

		if (!freelists[cls])
		{
			occupied_buckets &= ~(1u << cls); // Bucket now empty
		}

		reused_bytes += block->size;

		return block;
	}

	static void *
	alloc(size_t size)
	{
		assert(size > 0);
		assert(size < reserved_capacity);

		// Update diagnostic counters
		total_allocations++;
		total_bytes_allocated += size;
		allocation_histogram[get_size_class(size)]++;

		void *recycled = try_alloc_from_freelist(size);
		if (recycled)
		{
			return recycled;
		}
		return alloc_internal(size);
	}

	static void *
	alloc_internal(size_t size)
	{
		/* Bump allocator path - align the current pointer */
		uint8_t *aligned = (uint8_t *)(((uintptr_t)current + (Align - 1)) & ~(Align - 1));
		uint8_t *next = aligned + size;

		/*
		 * Check if we need more committed pages.
		 * We only commit what we need, not the full reserved range.
		 */
		if (next > base + committed_capacity)
		{
			size_t needed = next - base;

			if (max_capacity > 0 && needed > max_capacity)
			{
				fprintf(stderr, "Arena exhausted: requested %zu, max %zu\n", needed, max_capacity);
				exit(1);
			}

			if (needed > reserved_capacity)
			{
				fprintf(stderr, "Arena exhausted: requested %zu, reserved %zu\n", needed, reserved_capacity);
				exit(1);
			}

			size_t new_committed = virtual_memory::round_to_pages(needed);

			if (max_capacity > 0 && new_committed > max_capacity)
			{
				new_committed = max_capacity;
			}

			if (new_committed > reserved_capacity)
			{
				new_committed = reserved_capacity;
			}

			size_t commit_size = new_committed - committed_capacity;
			if (!virtual_memory::commit(base + committed_capacity, commit_size))
			{
				fprintf(stderr, "Failed to commit memory: %zu bytes\n", commit_size);
				exit(1);
			}

			committed_capacity = new_committed;
		}

		current = next;

		// Update peak usage
		size_t current_usage = used();
		if (current_usage > peak_usage)
		{
			peak_usage = current_usage;
		}

		return aligned;
	}

	/*
	 * Instead of zeroing all pages, tell the OS
	 * to discard the page contents. Next access will get zero pages.
	 */
	static void
	zero_pages_lazy(void *addr, size_t size)
	{
#ifdef _WIN32
		VirtualAlloc(addr, size, MEM_RESET, PAGE_READWRITE);
#elif defined(__linux__)
		madvise(addr, size, MADV_DONTNEED);
#else
		memset(addr, 0, size);
#endif
	}

	/* Reset the arena but keep pages committed */
	static void
	reset()
	{
		current = base;

		if constexpr (zero_on_reset)
		{
			if (base && committed_capacity > 0)
			{
				zero_pages_lazy(base, committed_capacity);
			}
		}

		/* Clear all freelists */
		for (int i = 0; i < 32; i++)
		{
			freelists[i] = nullptr;
		}
		occupied_buckets = 0;

		reclaimed_bytes = 0;
		reused_bytes = 0;
	}

	static void
	reset_and_decommit()
	{
		current = base;

		if (committed_capacity > initial_commit)
		{
			virtual_memory::decommit(base + initial_commit, committed_capacity - initial_commit);
			committed_capacity = initial_commit;
		}

		if constexpr (zero_on_reset)
		{
			if (base && committed_capacity > 0)
			{
				zero_pages_lazy(base, committed_capacity);
			}
		}

		for (int i = 0; i < 32; i++)
		{
			freelists[i] = nullptr;
		}
		occupied_buckets = 0;

		reclaimed_bytes = 0;
		reused_bytes = 0;
	}

	static size_t
	used()
	{
		return current - base;
	}
	static size_t
	committed()
	{
		return committed_capacity;
	}
	static size_t
	reserved()
	{
		return reserved_capacity;
	}
	static size_t
	reclaimed()
	{
		return reclaimed_bytes;
	}

	static size_t
	reused()
	{
		return reused_bytes;
	}

	static size_t
	freelist_bytes()
	{
		size_t total = 0;
		for (int i = 0; i < 32; i++)
		{
			free_block *block = freelists[i];
			while (block)
			{
				total += block->size;
				block = block->next;
			}
		}
		return total;
	}

	// ============================================================
	// DIAGNOSTIC FUNCTIONS
	// ============================================================

	/*
	 * Print basic statistics about the arena
	 */
	static void
	print_stats()
	{
		printf("Arena<%s>:\n", typeid(Tag).name());
		printf("  Used:      %zu bytes (%.2f MB)\n", used(), used() / (1024.0 * 1024.0));
		printf("  Committed: %zu bytes (%.2f MB)\n", committed_capacity, committed_capacity / (1024.0 * 1024.0));
		printf("  Reserved:  %zu bytes (%.2f MB)\n", reserved_capacity, reserved_capacity / (1024.0 * 1024.0));
		if (max_capacity > 0)
		{
			printf("  Maximum:   %zu bytes (%.2f MB)\n", max_capacity, max_capacity / (1024.0 * 1024.0));
		}
		printf("  Peak usage: %zu bytes (%.2f MB)\n", peak_usage, peak_usage / (1024.0 * 1024.0));
		printf("  Total allocations: %zu\n", total_allocations);
		printf("  Total bytes allocated: %zu (%.2f MB)\n", total_bytes_allocated, total_bytes_allocated / (1024.0 * 1024.0));
		printf("  Reclaimed: %zu bytes (%.2f MB) in %zu operations\n",
		       reclaimed_bytes, reclaimed_bytes / (1024.0 * 1024.0), total_reclamations);
		printf("  Reused:    %zu bytes (%.2f MB)\n", reused_bytes, reused_bytes / (1024.0 * 1024.0));
		printf("  In freelists: %zu bytes (%.2f MB)\n", freelist_bytes(), freelist_bytes() / (1024.0 * 1024.0));

		if (occupied_buckets)
		{
			printf("  Occupied buckets: ");
			for (int i = 0; i < 32; i++)
			{
				if (occupied_buckets & (1u << i))
				{
					printf("%d ", i);
				}
			}
			printf("\n");
		}
	}

	/*
	 * Print memory range information
	 */
	static void
	print_memory_ranges()
	{
		printf("\n=== Memory Ranges for Arena<%s> ===\n", typeid(Tag).name());
		printf("Reserved range: [%p - %p] (%zu bytes)\n",
		       base, base + reserved_capacity, reserved_capacity);
		printf("Committed range: [%p - %p] (%zu bytes)\n",
		       base, base + committed_capacity, committed_capacity);
		printf("Used range: [%p - %p] (%zu bytes)\n",
		       base, current, used());
		printf("Available in committed: %zu bytes\n",
		       committed_capacity - used());
		printf("Available in reserved: %zu bytes\n",
		       reserved_capacity - used());
	}

	/*
	 * Visual memory layout representation
	 */
	static void
	print_memory_layout()
	{
		printf("\n=== Memory Layout for Arena<%s> ===\n", typeid(Tag).name());
		printf("Address: [%p - %p]\n", base, base + reserved_capacity);
		printf("Visual:  [");

		const int bar_width = 50;
		size_t used_blocks = (used() * bar_width) / reserved_capacity;
		size_t committed_blocks = (committed_capacity * bar_width) / reserved_capacity;

		for (int i = 0; i < bar_width; i++)
		{
			if (i < used_blocks) printf("#");
			else if (i < committed_blocks) printf("-");
			else printf(".");
		}
		printf("]\n");
		printf("Legend:  # = used (%.1f%%), - = committed (%.1f%%), . = reserved\n",
		       (100.0 * used()) / reserved_capacity,
		       (100.0 * committed_capacity) / reserved_capacity);
	}

	/*
	 * Print allocation size histogram
	 */
	static void
	print_allocation_histogram()
	{
		printf("\n=== Allocation Size Distribution ===\n");
		printf("Size Class | Range          | Count      | Percentage\n");
		printf("-----------|----------------|------------|------------\n");

		size_t total = 0;
		for (int i = 0; i < 32; i++)
		{
			total += allocation_histogram[i];
		}

		if (total == 0)
		{
			printf("No allocations recorded.\n");
			return;
		}

		for (int i = 0; i < 32; i++)
		{
			if (allocation_histogram[i] > 0)
			{
				size_t min_size = 1u << i;
				size_t max_size = (1u << (i + 1)) - 1;
				double percentage = (100.0 * allocation_histogram[i]) / total;

				printf("%10d | %6zu - %6zu | %10zu | %10.2f%%\n",
				       i, min_size, max_size, allocation_histogram[i], percentage);
			}
		}
		printf("Total allocations: %zu\n", total);
	}

	/*
	 * Print detailed freelist information
	 */
	static void
	print_freelist_details()
	{
		printf("\n=== Freelist Details ===\n");
		printf("Bucket | Size Range     | Blocks | Total Bytes | Max Block | Avg Chain\n");
		printf("-------|----------------|--------|-------------|-----------|----------\n");

		size_t total_blocks = 0;
		size_t total_freelist_bytes = 0;

		for (int i = 0; i < 32; i++)
		{
			if (freelists[i])
			{
				size_t count = 0;
				size_t total_size = 0;
				size_t max_size = 0;

				free_block *block = freelists[i];
				while (block)
				{
					count++;
					total_size += block->size;
					if (block->size > max_size)
					{
						max_size = block->size;
					}
					block = block->next;
				}

				size_t min_range = 1u << i;
				size_t max_range = (1u << (i + 1)) - 1;
				double avg_size = (double)total_size / count;

				printf("%6d | %6zu - %6zu | %6zu | %11zu | %9zu | %9.1f\n",
				       i, min_range, max_range, count, total_size, max_size, avg_size);

				total_blocks += count;
				total_freelist_bytes += total_size;
			}
		}

		printf("\nTotal: %zu blocks, %zu bytes (%.2f MB)\n",
		       total_blocks, total_freelist_bytes,
		       total_freelist_bytes / (1024.0 * 1024.0));

		// Calculate fragmentation
		if (reclaimed_bytes > 0)
		{
			double efficiency = (100.0 * reused_bytes) / reclaimed_bytes;
			printf("Reuse efficiency: %.2f%% (reused/reclaimed)\n", efficiency);
		}
	}

	/*
	 * Check if a pointer belongs to this arena
	 */
	static bool
	owns_pointer(void *ptr)
	{
		uint8_t *addr = (uint8_t *)ptr;
		return addr >= base && addr < base + reserved_capacity;
	}

	/*
	 * Get largest available block from freelists
	 */
	static size_t
	largest_available_block()
	{
		if (!occupied_buckets)
		{
			return 0;
		}

		// Find highest set bit = largest bucket with blocks
#ifdef _MSC_VER
		unsigned long highest;
		_BitScanReverse(&highest, occupied_buckets);
#else
		int highest = 31 - __builtin_clz(occupied_buckets);
#endif

		// The actual largest block might be bigger than the minimum for this bucket
		size_t max_size = 0;
		for (int i = highest; i >= 0; i--)
		{
			if (occupied_buckets & (1u << i))
			{
				free_block *block = freelists[i];
				while (block)
				{
					if (block->size > max_size)
					{
						max_size = block->size;
					}
					block = block->next;
				}
			}
		}

		return max_size;
	}

	/*
	 * Verify freelist integrity
	 */
	static bool
	verify_freelist_integrity()
	{
		printf("\n=== Verifying Freelist Integrity ===\n");
		bool valid = true;

		for (int i = 0; i < 32; i++)
		{
			if (freelists[i])
			{
				free_block *slow = freelists[i];
				free_block *fast = freelists[i];
				size_t count = 0;

				// Floyd's cycle detection
				while (fast && fast->next)
				{
					slow = slow->next;
					fast = fast->next->next;
					count++;

					if (slow == fast)
					{
						printf("ERROR: Cycle detected in bucket %d\n", i);
						valid = false;
						break;
					}

					// Sanity check - no freelist should be this long
					if (count > 100000)
					{
						printf("WARNING: Suspiciously long chain in bucket %d (>100k nodes)\n", i);
						break;
					}
				}

				// Verify all blocks are within arena bounds
				free_block *block = freelists[i];
				while (block)
				{
					if (!owns_pointer(block))
					{
						printf("ERROR: Block %p in bucket %d is outside arena bounds\n", block, i);
						valid = false;
					}

					// Verify size is reasonable for this bucket
					int expected_class = get_size_class(block->size);
					if (expected_class != i && block->size < (1u << i))
					{
						printf("WARNING: Block size %zu in wrong bucket %d (expected %d)\n",
						       block->size, i, expected_class);
					}

					block = block->next;
				}
			}
		}

		if (valid)
		{
			printf("All freelists are valid.\n");
		}

		return valid;
	}

	/*
	 * Print temporal statistics
	 */
	static void
	print_temporal_stats()
	{
		auto now = std::chrono::steady_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - init_time);

		printf("\n=== Temporal Statistics ===\n");
		printf("Arena active for: %lld seconds\n", duration.count());

		if (duration.count() > 0)
		{
			double alloc_rate = (double)total_allocations / duration.count();
			double byte_rate = (double)total_bytes_allocated / duration.count();

			printf("Allocation rate: %.2f ops/sec\n", alloc_rate);
			printf("Byte allocation rate: %.2f MB/sec\n", byte_rate / (1024.0 * 1024.0));

			// Growth rate
			double growth_rate = (double)used() / duration.count();
			printf("Net growth rate: %.2f KB/sec\n", growth_rate / 1024.0);
		}
	}

	/*
	 * Dump memory at specific address (if it belongs to arena)
	 */
	static void
	dump_memory(void *addr, size_t size)
	{
		if (!owns_pointer(addr))
		{
			printf("Address %p does not belong to Arena<%s>\n", addr, typeid(Tag).name());
			return;
		}

		printf("\n=== Memory Dump at %p ===\n", addr);
		uint8_t *bytes = (uint8_t *)addr;

		for (size_t i = 0; i < size; i += 16)
		{
			printf("%p: ", bytes + i);

			// Hex bytes
			for (size_t j = 0; j < 16 && i + j < size; j++)
			{
				printf("%02x ", bytes[i + j]);
			}

			// Padding if last line is short
			for (size_t j = size - i; j < 16 && i + 16 > size; j++)
			{
				printf("   ");
			}

			printf(" |");

			// ASCII representation
			for (size_t j = 0; j < 16 && i + j < size; j++)
			{
				uint8_t c = bytes[i + j];
				printf("%c", (c >= 32 && c < 127) ? c : '.');
			}

			printf("|\n");
		}
	}

	/*
	 * Print all arena diagnostics
	 */
	static void
	print_all_diagnostics()
	{
		printf("\n" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=\n");
		printf("COMPLETE ARENA DIAGNOSTICS: %s\n", typeid(Tag).name());
		printf("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=\n");

		print_stats();
		print_memory_ranges();
		print_memory_layout();
		print_allocation_histogram();
		print_freelist_details();
		print_temporal_stats();
		verify_freelist_integrity();

		printf("\n" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=\n");
	}

	/*
	 * Print cross-arena statistics (static function callable without template)
	 */
	static void
	print_global_arena_stats()
	{
		if (!arena_registry || arena_registry->empty())
		{
			printf("No arenas registered.\n");
			return;
		}

		printf("\n=== Global Arena Statistics ===\n");
		printf("Active arenas: %zu\n", arena_registry->size());

		size_t total_reserved = 0;
		size_t total_committed = 0;

		for (const auto &info : *arena_registry)
		{
			printf("  %s: base=%p, reserved=%zu MB\n",
			       info.tag_name, info.base, info.reserved / (1024 * 1024));
			total_reserved += info.reserved;

			// Check for overlaps
			for (const auto &other : *arena_registry)
			{
				if (&info != &other)
				{
					uint8_t *end1 = info.base + info.reserved;
					uint8_t *end2 = other.base + other.reserved;

					if ((info.base >= other.base && info.base < end2) ||
					    (end1 > other.base && end1 <= end2))
					{
						printf("  WARNING: Overlap detected with %s!\n", other.tag_name);
					}
				}
			}
		}

		printf("Total reserved across all arenas: %.2f GB\n",
		       total_reserved / (1024.0 * 1024.0 * 1024.0));
	}
};
/**
 * Arena-based containers
 *
 * I considered whether to use STL with allocators, but felt there was
 * a fundermental mismatch between STL's RAII lifetime management, and my nuke-from-orbit
 * arena approach.
 *
 * Implented is array, string, and map, with stack/inlined metadata with a a dynamic
 * space that pulls from, and releases into their arena. Each container should specify
 * which arena it is bound to, but cross-arena copying is supported.
 *
 * When resizing, the old data is put back to the respective arena into the free list but
 * when the array falls out of scope the allocations won't automatically be freed.
 * The reclaimation mechanism is really just about crawling back some wasted space from the array/string/map
 * resizing, so it's quite crude comparitively. The expectation is that
 *
 *
 */
/**
 * Open-addressed hash map
 * For keys, only supports primtivites and and arena string/cstrings as special cases
 *
 */

template <typename T, typename arena_tag = global_arena, uint32_t InitialSize = 8> struct contiguous
{
	T		*data = nullptr;
	uint32_t size = 0;
	uint32_t capacity = 0;

	T *
	alloc_raw(uint32_t count)
	{
		return (T *)arena<arena_tag>::alloc(count * sizeof(T));
	}

	void
	realloc_internal(uint32_t new_capacity, bool copy_existing)
	{
		T		*old_data = data;
		uint32_t old_capacity = capacity;
		uint32_t old_size = size;

		data = alloc_raw(new_capacity);
		capacity = new_capacity;

		if (old_data && copy_existing && old_size > 0)
		{
			memcpy(data, old_data, old_size * sizeof(T));
			size = old_size;
		}
		else
		{
			size = 0;
		}

		if (old_data)
		{
			arena<arena_tag>::reclaim(old_data, old_capacity * sizeof(T));
		}
	}

	void
	reclaim_if_exists()
	{
		if (data)
		{
			arena<arena_tag>::reclaim(data, capacity * sizeof(T));
			data = nullptr;
			capacity = 0;
			size = 0;
		}
	}

	T *
	grow_by(uint32_t count)
	{
		reserve(size + count);
		T *write_pos = data + size;
		size += count;
		return write_pos;
	}

	void
	reserve(uint32_t min_capacity)
	{
		if (capacity >= min_capacity)
		{
			return;
		}

		if (!data)
		{
			capacity = min_capacity > InitialSize ? min_capacity : InitialSize;
			data = alloc_raw(capacity);
			return;
		}

		uint32_t new_cap = capacity * 2;
		if (new_cap < min_capacity)
		{
			new_cap = min_capacity;
		}

		realloc_internal(new_cap, true);
	}

	void
	allocate(uint32_t new_capacity)
	{
		realloc_internal(new_capacity, false);
	}

	void
	allocate_full(uint32_t new_capacity)
	{
		allocate(new_capacity);
		size = new_capacity;
	}

	void
	resize(uint32_t new_size, bool zero_new = true)
	{
		uint32_t old_size = size;
		reserve(new_size);

		if (new_size > old_size && data && zero_new)
		{
			memset(data + old_size, 0, (new_size - old_size) * sizeof(T));
		}

		size = new_size;
	}

	void
	shrink_to_fit()
	{
		if (!data || size == capacity || size == 0)
		{
			return;
		}

		arena<arena_tag>::reclaim(data + size, (capacity - size) * sizeof(T));
		capacity = size;
	}

	void
	adopt(T *new_data, uint32_t new_size, uint32_t new_capacity)
	{
		reclaim_if_exists();
		data = new_data;
		size = new_size;
		capacity = new_capacity;
	}

	void
	swap(contiguous *other)
	{
		T		*tmp_data = data;
		uint32_t tmp_size = size;
		uint32_t tmp_capacity = capacity;

		data = other->data;
		size = other->size;
		capacity = other->capacity;

		other->data = tmp_data;
		other->size = tmp_size;
		other->capacity = tmp_capacity;
	}
	template <typename OtherTag>
	void
	copy_from(const contiguous<T, OtherTag> &other)
	{
		clear();
		if (other.size > 0 && other.data)
		{
			reserve(other.size);
			memcpy(data, other.data, other.size * sizeof(T));
			size = other.size;
		}
	}

	void
	copy_from(const T *src_data, uint32_t src_size)
	{
		clear();
		if (src_size > 0 && src_data)
		{
			reserve(src_size);
			memcpy(data, src_data, src_size * sizeof(T));
			size = src_size;
		}
	}
	template <typename OtherTag>
	void
	move_from(contiguous<T, OtherTag> &other)
	{
		this->copy_from(other);
		other.release();
	}

	// Maybe also add append operations while we're at it
	template <typename OtherTag>
	void
	append_from(const contiguous<T, OtherTag> &other)
	{
		if (other.size > 0 && other.data)
		{
			T *dest = grow_by(other.size);
			memcpy(dest, other.data, other.size * sizeof(T));
		}
	}

	void
	release()
	{
		reclaim_if_exists();
	}

	void
	clear()
	{
		size = 0;
	}

	void
	zero()
	{
		if (data && capacity > 0)
		{
			memset(data, 0, capacity * sizeof(T));
		}
	}

	void
	set_size_unsafe(uint32_t new_size)
	{
		assert(new_size <= capacity);
		size = new_size;
	}

	void
	reset()
	{
		data = nullptr;
		size = 0;
		capacity = 0;
	}
};

// Utility functions at global/namespace level
template <typename T>
inline T
round_up_power_of_2(T n)
{
	static_assert(std::is_unsigned_v<T>);
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	if constexpr (sizeof(T) > 1)
		n |= n >> 16;
	if constexpr (sizeof(T) > 4)
		n |= n >> 32;
	return n + 1;
}

template <typename Tag = global_arena> struct stream_writer
{
	uint8_t *start;
	uint8_t *write_ptr;

	static stream_writer
	begin()
	{
		if (!arena<Tag>::base)
		{
			arena<Tag>::init();
		}
		return {arena<Tag>::current, arena<Tag>::current};
	}

	void
	write(const void *data, size_t size)
	{

		if (write_ptr + size > arena<Tag>::base + arena<Tag>::committed_capacity)
		{
			size_t needed = (write_ptr - arena<Tag>::base) + size;
			size_t new_committed = virtual_memory::round_to_pages(needed);
			if (new_committed > arena<Tag>::reserved_capacity)
			{
				fprintf(stderr, "Arena exhausted\n");
				exit(1);
			}
			size_t commit_size = new_committed - arena<Tag>::committed_capacity;
			if (!virtual_memory::commit(arena<Tag>::base + arena<Tag>::committed_capacity, commit_size))
			{
				fprintf(stderr, "Failed to commit memory\n");
				exit(1);
			}
			arena<Tag>::committed_capacity = new_committed;
		}

		memcpy(write_ptr, data, size);
		write_ptr += size;
	}

	void
	write(std::string_view sv)
	{
		write(sv.data(), sv.size());
	}

	void
	write(const char *str)
	{
		write(str, strlen(str));
	}

	size_t
	size() const
	{
		return write_ptr - start;
	}

	std::string_view
	finish()
	{
		// Null terminate
		*write_ptr = '\0';
		size_t len = write_ptr - start;

		arena<Tag>::current = write_ptr + 1;

		return std::string_view((char *)start, len);
	}

	void
	abandon()
	{
		arena<Tag>::current = start;
	}
};

template <typename Tag = global_arena>
std::string_view
arena_intern(std::string_view sv)
{
	char *memory = (char *)arena<Tag>::alloc(sv.size());
	memcpy(memory, sv.data(), sv.size());
	return std::string_view(memory, sv.size());
}

template <typename Tag = global_arena>
std::string_view
arena_intern(const char *str, size_t len = 0, bool null_terminate = false)
{
	size_t l = len != 0 ? len : std::strlen(str);
	size_t alloc_size = null_terminate ? l + 1 : l;
	char  *memory = (char *)arena<Tag>::alloc(alloc_size);
	memcpy(memory, str, l);
	if (null_terminate)
	{
		memory[l] = '\0';
	}
	return std::string_view(memory, l);
}

template <typename Tag = global_arena>
void
arena_reclaim_string(std::string_view str)
{
	void *memory = (void *)(str.begin());
	arena<Tag>::reclaim(memory, str.size());
}
