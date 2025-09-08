#pragma once
#include <cassert>
#include <cstring>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include "os_layer.hpp"
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
	static inline size_t	  reclaimed_bytes = 0;
	static inline size_t	  reused_bytes = 0;

	static void
	init(size_t initial = 4 * 1024 * 1024, size_t maximum = 0)
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
		}
		occupied_buckets = 0;
		reclaimed_bytes = 0;
		reused_bytes = 0;
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

	/* faster path, make sure you have enough allocated */
	static void *
	alloc_fast(size_t size)
	{
		uint8_t *aligned = (uint8_t *)(((uintptr_t)current + (Align - 1)) & ~(Align - 1));
		uint8_t *next = aligned + size;
		current = next;
		return aligned;
	}

	static void *
	alloc(size_t size)
	{
		assert(size > 0);
		assert(size < reserved_capacity);

		void *recycled = try_alloc_from_freelist(size);
		if (recycled)
		{
			return recycled;
		}

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
		printf("  Reclaimed: %zu bytes (%.2f MB)\n", reclaimed_bytes, reclaimed_bytes / (1024.0 * 1024.0));
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

// Add this type alias at global scope:

template <typename T, typename ArenaTag = global_arena, uint32_t InitialSize = 8> struct contiguous
{
	T		*data = nullptr;
	uint32_t size = 0;
	uint32_t capacity = 0;

	// Core allocation primitive
	T *
	alloc_raw(uint32_t count)
	{
		return (T *)arena<ArenaTag>::alloc(count * sizeof(T));
	}

	// Core reallocation - FIXED VERSION
	void
	realloc_internal(uint32_t new_capacity, bool copy_existing)
	{
		T		*old_data = data;
		uint32_t old_capacity = capacity;
		uint32_t old_size = size; // Save size BEFORE any modifications

		// Allocate new buffer
		data = alloc_raw(new_capacity);
		capacity = new_capacity;

		// Copy if requested and there was old data
		if (old_data && copy_existing && old_size > 0)
		{
			memcpy(data, old_data, old_size * sizeof(T));
			size = old_size; // Preserve size when copying
		}
		else
		{
			size = 0; // Reset size when not copying
		}

		// Reclaim old buffer
		if (old_data)
		{
			arena<ArenaTag>::reclaim(old_data, old_capacity * sizeof(T));
		}
	}

	// Core reclaim primitive - unchanged
	void
	reclaim_if_exists()
	{
		if (data)
		{
			arena<ArenaTag>::reclaim(data, capacity * sizeof(T));
			data = nullptr;
			capacity = 0;
			size = 0;
		}
	}

	// The rest remains the same...
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

		arena<ArenaTag>::reclaim(data + size, (capacity - size) * sizeof(T));
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

/*
 *
 * --Stream--
 */

/* Streams to support contiguous allocation without knowing the size ahead of time */

template <typename Tag> struct stream_alloc
{
	uint8_t *start;		// Where this allocation began
	uint8_t *write_pos; // Current write position
	size_t	 reserved;	// How much we've reserved so far
};

template <typename Tag = global_arena>
stream_alloc<Tag>
arena_stream_begin(size_t initial_reserve = 1024)
{
	if (!arena<Tag>::base)
	{
		arena<Tag>::init();
	}

	// Ensure we have space for initial reservation
	uint8_t *start = arena<Tag>::current;
	size_t	 available = arena<Tag>::committed_capacity - (arena<Tag>::current - arena<Tag>::base);

	if (initial_reserve > available)
	{
		// Need to commit more memory
		size_t needed = (arena<Tag>::current - arena<Tag>::base) + initial_reserve;
		if (needed > arena<Tag>::reserved_capacity)
		{
			fprintf(stderr, "Stream allocation too large\n");
			exit(1);
		}

		size_t new_committed = virtual_memory::round_to_pages(needed);
		if (!virtual_memory::commit(arena<Tag>::base + arena<Tag>::committed_capacity,
									new_committed - arena<Tag>::committed_capacity))
		{
			fprintf(stderr, "Failed to commit memory for stream\n");
			exit(1);
		}
		arena<Tag>::committed_capacity = new_committed;
	}

	arena<Tag>::current = start + initial_reserve;

	return stream_alloc<Tag>{start, start, initial_reserve};
}

template <typename Tag = global_arena>
void
arena_stream_write(stream_alloc<Tag> *stream, const void *data, size_t size)
{
	size_t used = stream->write_pos - stream->start;
	size_t remaining = stream->reserved - used;

	if (size > remaining)
	{

		size_t new_reserved = stream->reserved * 2;
		while (new_reserved - used < size)
		{
			new_reserved *= 2;
		}

		size_t available = arena<Tag>::committed_capacity - (stream->start - arena<Tag>::base);
		if (new_reserved > available)
		{
			size_t needed = (stream->start - arena<Tag>::base) + new_reserved;
			if (needed > arena<Tag>::reserved_capacity)
			{
				fprintf(stderr, "Stream allocation too large\n");
				exit(1);
			}

			size_t new_committed = virtual_memory::round_to_pages(needed);
			if (!virtual_memory::commit(arena<Tag>::base + arena<Tag>::committed_capacity,
										new_committed - arena<Tag>::committed_capacity))
			{
				fprintf(stderr, "Failed to commit memory for stream\n");
				exit(1);
			}
			arena<Tag>::committed_capacity = new_committed;
		}

		arena<Tag>::current = stream->start + new_reserved;
		stream->reserved = new_reserved;
	}

	memcpy(stream->write_pos, data, size);
	stream->write_pos += size;
}

template <typename Tag = global_arena>
uint8_t *
arena_stream_finish(stream_alloc<Tag> *stream)
{

	arena<Tag>::current = stream->write_pos;
	return stream->start;
}

template <typename Tag = global_arena>
void
arena_stream_abandon(stream_alloc<Tag> *stream)
{

	arena<Tag>::current = stream->start;
}

template <typename Tag = global_arena>
size_t
arena_stream_size(const stream_alloc<Tag> *stream)
{
	return stream->write_pos - stream->start;
}

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
	if (null_terminate) {
			memory[l] = '\0';
	}
	return std::string_view(memory, l);
}
