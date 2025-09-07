#pragma once
#include <cassert>
#include <cstring>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include "os_layer.hpp"
#include <cstring>
#include <cstdint>
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
 * - Static storage per Tag type (no instance overhead)
 * - Power-of-2 freelists for O(1) reclamation and reuse
 * - Lazy zeroing via OS facilities when available
 *
 * Usage pattern:
 * 1. Arena<MyTag>::init() reserves virtual address space
 * 2. Allocations commit pages as needed
 * 3. Containers can reclaim() memory when growing
 * 4. reset() nukes everything but keeps pages committed
 */
template <typename Tag = global_arena, bool ZeroOnReset = true, size_t Align = 8> struct Arena
{
	static_assert((Align & (Align - 1)) == 0, "Alignment must be power of 2");

	static inline uint8_t *base = nullptr;
	static inline uint8_t *current = nullptr;
	static inline size_t   reserved_capacity = 0;
	static inline size_t   committed_capacity = 0;
	static inline size_t   max_capacity = 0;
	static inline size_t   initial_commit = 0;

	struct FreeBlock
	{
		FreeBlock *next;
		size_t	   size;
	};

	/*
	 * Freelist buckets organized by power-of-2 size classes.
	 * freelists[4] = blocks of size [16, 32)
	 * freelists[5] = blocks of size [32, 64)
	 * etc.
	 * This gives us O(1) bucket selection via bit manipulation.
	 */
	static inline FreeBlock *freelists[32] = {};
	static inline uint32_t	 occupied_buckets = 0; // Bitmask: which buckets have blocks
	static inline size_t	 reclaimed_bytes = 0;
	static inline size_t	 reused_bytes = 0;

	static void
	init(size_t initial = 4 * 1024 * 1024, size_t maximum = 0)
	{
		if (base)
		{
			return;
		}

		initial_commit = VirtualMemory::round_to_pages(initial);
		max_capacity = maximum;

		/*
		 * Reserve a huge virtual address range upfront.
		 * This costs nothing on 64-bit systems - it's just address space.
		 * We'll commit physical pages lazily as needed.
		 */
		reserved_capacity = max_capacity ? max_capacity : (1ULL << 38); // 256GB default

		base = (uint8_t *)VirtualMemory::reserve(reserved_capacity);
		if (!base)
		{
			// Fallback to smaller reservation
			reserved_capacity = 1ULL << 33; // 8GB
			base = (uint8_t *)VirtualMemory::reserve(reserved_capacity);
			if (!base)
			{
				fprintf(stderr, "Failed to reserve virtual memory\n");
				exit(1);
			}
		}

		current = base;
		committed_capacity = 0;

		if (initial_commit > 0)
		{
			if (!VirtualMemory::commit(base, initial_commit))
			{
				fprintf(stderr, "Failed to commit initial memory: %zu bytes\n", initial_commit);
				VirtualMemory::release(base, reserved_capacity);
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
			VirtualMemory::release(base, reserved_capacity);
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
	 * The trick: we need the index i such that 2^i <= size < 2^(i+1)
	 * This is just finding the position of the highest set bit.
	 *
	 * The OR with 0x2 handles edge cases:
	 * - size=0 becomes 2, avoiding undefined behavior in clz(-1)
	 * - size=1 becomes 3, mapping to bucket 1 instead of 0
	 *
	 * The XOR dance at the end clamps the result to [0, 31] branchlessly.
	 */
	static inline int
	get_size_class(size_t size)
	{
		size = size | 0x2;

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
		if (!ptr || size < sizeof(FreeBlock))
		{
			return;
		}

		uint8_t *addr = (uint8_t *)ptr;
		if (addr < base || addr >= base + reserved_capacity)
		{
			return; // Not from this arena
		}

		assert(addr < current); // Can't reclaim unallocated memory
		assert(size > 0);

		int size_class = get_size_class(size);

		FreeBlock *block = (FreeBlock *)ptr;
		block->size = size;
		block->next = freelists[size_class];
		freelists[size_class] = block;

		occupied_buckets |= (1u << size_class);
		reclaimed_bytes += size;
	}

	/*
	 * Allocation strategy:
	 * 1. Check freelists for a suitable reclaimed block
	 * 2. Fall back to bump allocation from current pointer
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

		FreeBlock *block = freelists[cls];
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
		assert(size < reserved_capacity); // Sanity check

		/* First try to reuse a reclaimed block */
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

			/* Round up to page boundary for efficiency */
			size_t new_committed = VirtualMemory::round_to_pages(needed);

			if (max_capacity > 0 && new_committed > max_capacity)
			{
				new_committed = max_capacity;
			}

			if (new_committed > reserved_capacity)
			{
				new_committed = reserved_capacity;
			}

			size_t commit_size = new_committed - committed_capacity;
			if (!VirtualMemory::commit(base + committed_capacity, commit_size))
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
	 * Platform-specific optimization: Instead of memset, we tell the OS
	 * to discard the page contents. Next access will get zero pages.
	 * This turns O(n) zeroing into O(pages actually touched).
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

	/* Reset the arena but keep pages committed for fast reuse */
	static void
	reset()
	{
		current = base;

		if constexpr (ZeroOnReset)
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

	/* Reset and give memory back to OS (keeps initial_commit for fast restart) */
	static void
	reset_and_decommit()
	{
		current = base;

		if (committed_capacity > initial_commit)
		{
			VirtualMemory::decommit(base + initial_commit, committed_capacity - initial_commit);
			committed_capacity = initial_commit;
		}

		if constexpr (ZeroOnReset)
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

	/* Query functions */
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
			FreeBlock *block = freelists[i];
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

namespace arena
{
template <typename Tag>
void
init(size_t initial = 4 * 1024 * 1024, size_t maximum = 0)
{
	Arena<Tag>::init(initial);
}

template <typename Tag>
void
shutdown()
{
	Arena<Tag>::shutdown();
}
template <typename Tag>
void
reset()
{
	Arena<Tag>::reset();
}
template <typename Tag>
void
reset_and_decommit()
{
	Arena<Tag>::reset_and_decommit();
}
template <typename Tag>
void *
alloc(size_t size)
{
	return Arena<Tag>::alloc(size);
}
template <typename Tag>
void
reclaim(void *ptr, size_t size)
{
	Arena<Tag>::reclaim(ptr, size);
}
template <typename Tag>
size_t
used()
{
	return Arena<Tag>::used();
}
template <typename Tag>
size_t
committed()
{
	return Arena<Tag>::committed();
}
template <typename Tag>
size_t
reclaimed()
{
	return Arena<Tag>::reclaimed();
}
template <typename Tag>
size_t
reused()
{
	return Arena<Tag>::reused();
}
template <typename Tag>
void
print_stats()
{
	Arena<Tag>::print_stats();
}

// Streaming allocation support
template <typename Tag> struct StreamAlloc
{
	uint8_t *start;		// Where this allocation began
	uint8_t *write_pos; // Current write position
	size_t	 reserved;	// How much we've reserved so far
};

template <typename Tag = global_arena>
StreamAlloc<Tag>
stream_begin(size_t initial_reserve = 1024)
{
	if (!Arena<Tag>::base)
	{
		Arena<Tag>::init();
	}

	// Ensure we have space for initial reservation
	uint8_t *start = Arena<Tag>::current;
	size_t	 available = Arena<Tag>::committed_capacity - (Arena<Tag>::current - Arena<Tag>::base);

	if (initial_reserve > available)
	{
		// Need to commit more memory
		size_t needed = (Arena<Tag>::current - Arena<Tag>::base) + initial_reserve;
		if (needed > Arena<Tag>::reserved_capacity)
		{
			fprintf(stderr, "Stream allocation too large\n");
			exit(1);
		}

		size_t new_committed = VirtualMemory::round_to_pages(needed);
		if (!VirtualMemory::commit(Arena<Tag>::base + Arena<Tag>::committed_capacity,
								   new_committed - Arena<Tag>::committed_capacity))
		{
			fprintf(stderr, "Failed to commit memory for stream\n");
			exit(1);
		}
		Arena<Tag>::committed_capacity = new_committed;
	}

	// Reserve initial space by moving current
	Arena<Tag>::current = start + initial_reserve;

	return StreamAlloc<Tag>{start, start, initial_reserve};
}

template <typename Tag = global_arena>
void
stream_write(StreamAlloc<Tag> *stream, const void *data, size_t size)
{
	size_t used = stream->write_pos - stream->start;
	size_t remaining = stream->reserved - used;

	if (size > remaining)
	{
		// Need to grow the reservation
		size_t new_reserved = stream->reserved * 2;
		while (new_reserved - used < size)
		{
			new_reserved *= 2;
		}

		// Ensure we have space
		size_t available = Arena<Tag>::committed_capacity - (stream->start - Arena<Tag>::base);
		if (new_reserved > available)
		{
			size_t needed = (stream->start - Arena<Tag>::base) + new_reserved;
			if (needed > Arena<Tag>::reserved_capacity)
			{
				fprintf(stderr, "Stream allocation too large\n");
				exit(1);
			}

			size_t new_committed = VirtualMemory::round_to_pages(needed);
			if (!VirtualMemory::commit(Arena<Tag>::base + Arena<Tag>::committed_capacity,
									   new_committed - Arena<Tag>::committed_capacity))
			{
				fprintf(stderr, "Failed to commit memory for stream\n");
				exit(1);
			}
			Arena<Tag>::committed_capacity = new_committed;
		}

		// Update arena's current pointer to new end
		Arena<Tag>::current = stream->start + new_reserved;
		stream->reserved = new_reserved;
	}

	// Copy the data
	memcpy(stream->write_pos, data, size);
	stream->write_pos += size;
}

template <typename Tag = global_arena>
uint8_t *
stream_finish(StreamAlloc<Tag> *stream)
{
	// Shrink allocation to actual size (give back unused reservation)
	Arena<Tag>::current = stream->write_pos;
	return stream->start;
}

template <typename Tag = global_arena>
void
stream_abandon(StreamAlloc<Tag> *stream)
{
	// Reset current to where we started
	Arena<Tag>::current = stream->start;
}

template <typename Tag = global_arena>
size_t
stream_size(const StreamAlloc<Tag> *stream)
{
	return stream->write_pos - stream->start;
}

} // namespace arena

/**
 * Arena-based containers for C++ with data-oriented design.
 * All containers allocate from arena memory pools and support O(1) growth via reclamation.
 */

template <typename ArenaTag, uint32_t InitialSize> struct string;

/**
 * Arena-based containers for C++ with data-oriented design.
 * All containers allocate from arena memory pools and support O(1) growth via reclamation.
 */

template <typename ArenaTag, uint32_t InitialSize> struct string;

/**
 * Dynamic array with arena allocation.
 * - Grows by doubling capacity
 * - Reclaims old buffers back to arena when resizing
 * - Specialized support for string element types
 */
template <typename T, typename ArenaTag = global_arena, uint32_t InitialSize = 8> struct array
{
	template <typename U> struct is_string : std::false_type {};
	template <typename Tag, uint32_t Size> struct is_string<string<Tag, Size>> : std::true_type {};

	T		*data = nullptr;
	uint32_t size = 0;
	uint32_t capacity = 0;

	void reserve(uint32_t min_capacity)
	{
		if (capacity >= min_capacity) {
			return;
		}

		if (!data) {
			capacity = min_capacity > InitialSize ? min_capacity : InitialSize;
			data = (T *)Arena<ArenaTag>::alloc(capacity * sizeof(T));
			return;
		}

		T		*old_data = data;
		uint32_t old_capacity = capacity;

		uint32_t new_cap = capacity * 2;
		if (new_cap < min_capacity) {
			new_cap = min_capacity;
		}

		T *new_data = (T *)Arena<ArenaTag>::alloc(new_cap * sizeof(T));
		memcpy(new_data, data, size * sizeof(T));

		data = new_data;
		capacity = new_cap;

		Arena<ArenaTag>::reclaim(old_data, old_capacity * sizeof(T));
	}

	uint32_t push(const T &value)
	{
		reserve(size + 1);
		data[size] = value;
		return size++;
	}

	template <typename OtherTag = ArenaTag, uint32_t OtherSize = InitialSize>
	uint32_t push(const string<OtherTag, OtherSize> &value)
	{
		static_assert(is_string<T>::value, "push(string) can only be used with string arrays");
		reserve(size + 1);
		T &dest = data[size];
		dest.set(value.c_str());
		return size++;
	}

	T *push_n(const T *values, uint32_t count)
	{
		reserve(size + count);
		T *dest = data + size;
		memcpy(dest, values, count * sizeof(T));
		size += count;
		return dest;
	}

	template <typename OtherTag, uint32_t OtherSize>
	T *push_n(const string<OtherTag, OtherSize> *values, uint32_t count)
	{
		static_assert(is_string<T>::value, "push_n(string) can only be used with string arrays");
		reserve(size + count);
		T *dest = data + size;
		for (uint32_t i = 0; i < count; i++) {
			dest[i].set(values[i].c_str());
		}
		size += count;
		return dest;
	}

	void pop_back()
	{
		assert(size > 0);
		--size;
	}

	T pop_value()
	{
		assert(size > 0);
		return data[--size];
	}

	void clear()
	{
		size = 0;
	}

	void resize(uint32_t new_size)
	{
		uint32_t old_size = size;
		reserve(new_size);

		if (new_size > old_size && data) {
			memset(data + old_size, 0, (new_size - old_size) * sizeof(T));
		}

		size = new_size;
	}

	void shrink_to_fit()
	{
		if (!data || size == capacity || size == 0) {
			return;
		}

		T		*old_data = data;
		uint32_t old_capacity = capacity;

		data = (T *)Arena<ArenaTag>::alloc(size * sizeof(T));
		memcpy(data, old_data, size * sizeof(T));
		capacity = size;

		Arena<ArenaTag>::reclaim(old_data, old_capacity * sizeof(T));
	}

	template <typename OtherTag>
	void set(const array<T, OtherTag> &other)
	{
		clear();
		reserve(other.size);

		if (other.size > 0 && other.data) {
			memcpy(data, other.data, other.size * sizeof(T));
			size = other.size;
		}
	}

	template <typename OtherTag, uint32_t OtherSize, typename OtherArrayTag>
	void set(const array<string<OtherTag, OtherSize>, OtherArrayTag> &other)
	{
		static_assert(is_string<T>::value, "set(string array) can only be used with string arrays");
		clear();
		reserve(other.size);

		for (uint32_t i = 0; i < other.size; i++) {
			data[i].set(other.data[i].c_str());
		}
		size = other.size;
	}

	T &operator[](uint32_t index)
	{
		assert(index < size);
		return data[index];
	}

	const T &operator[](uint32_t index) const
	{
		assert(index < size);
		return data[index];
	}

	T *back()
	{
		return size > 0 ? &data[size - 1] : nullptr;
	}

	const T *back() const
	{
		return size > 0 ? &data[size - 1] : nullptr;
	}

	T *front()
	{
		return size > 0 ? &data[0] : nullptr;
	}

	const T *front() const
	{
		return size > 0 ? &data[0] : nullptr;
	}

	T *begin() { return data; }
	T *end() { return data + size; }
	const T *begin() const { return data; }
	const T *end() const { return data + size; }

	bool empty() const { return size == 0; }

	static array *create()
	{
		auto *arr = (array *)Arena<ArenaTag>::alloc(sizeof(array));
		arr->data = nullptr;
		arr->size = 0;
		arr->capacity = 0;
		return arr;
	}
};

inline uint32_t hash_32(uint32_t x)
{
	x = ((x >> 16) ^ x) * 0x45d9f3b;
	x = ((x >> 16) ^ x) * 0x45d9f3b;
	x = (x >> 16) ^ x;
	return x;
}

inline uint64_t hash_64(uint64_t x)
{
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
	x = x ^ (x >> 31);
	return x;
}

template <typename T>
inline uint32_t hash_int(T x)
{
	using U = typename std::make_unsigned<T>::type;
	U ux = static_cast<U>(x);

	if constexpr (sizeof(T) <= 4) {
		return hash_32(static_cast<uint32_t>(ux));
	}
	else {
		return static_cast<uint32_t>(hash_64(static_cast<uint64_t>(ux)));
	}
}

/**
 * Arena-allocated string with small string optimization.
 * - Caches hash value for efficient comparison
 * - Supports cross-arena string operations
 * - Automatically null-terminates for C compatibility
 */
template <typename ArenaTag = global_arena, uint32_t InitialSize = 32> struct string
{
	char	*data = nullptr;
	uint32_t size = 0;
	uint32_t capacity = 0;
	mutable uint32_t cached_hash = 0;

	string &operator=(const char *cstr)
	{
		if (cstr) {
			set(cstr);
		}
		else {
			clear();
		}
		return *this;
	}

	string &operator=(const string &other)
	{
		if (this != &other) {
			if (other.empty()) {
				clear();
			}
			else {
				set(other.c_str());
			}
		}
		return *this;
	}

	template <typename OtherTag, uint32_t OtherSize>
	string &operator=(const string<OtherTag, OtherSize> &other)
	{
		if (other.empty()) {
			clear();
		}
		else {
			set(other.c_str());
		}
		return *this;
	}

	void reserve(uint32_t min_capacity)
	{
		if (capacity >= min_capacity) {
			return;
		}

		if (!data) {
			capacity = min_capacity > InitialSize ? min_capacity : InitialSize;
			data = (char *)Arena<ArenaTag>::alloc(capacity);
			return;
		}

		char	*old_data = data;
		uint32_t old_capacity = capacity;

		uint32_t new_cap = capacity * 2;
		if (new_cap < min_capacity) {
			new_cap = min_capacity;
		}

		char *new_data = (char *)Arena<ArenaTag>::alloc(new_cap);
		memcpy(new_data, data, size);

		data = new_data;
		capacity = new_cap;
		cached_hash = 0;

		Arena<ArenaTag>::reclaim(old_data, old_capacity);
	}

	void set(const char *cstr, size_t len = 0)
	{
		size_t actual_len;
		if (len != 0) {
			actual_len = len;
		}
		else {
			actual_len = strlen(cstr) + 1;
		}
		reserve(actual_len);
		memcpy(data, cstr, actual_len);
		size = actual_len;
		cached_hash = 0;
	}

	template <typename OtherTag, uint32_t OtherSize>
	void set(const string<OtherTag, OtherSize> &other)
	{
		if (other.empty()) {
			clear();
		}
		else {
			set(other.c_str());
		}
	}

	void append(const char *cstr, size_t s = 0)
	{
		if (size > 0 && data[size - 1] == '\0') {
			size--;
		}

		size_t len = s != 0 ? s : strlen(cstr);
		reserve(size + len + 1);

		memcpy(data + size, cstr, len);
		size += len;
		data[size++] = '\0';
		cached_hash = 0;
	}

	void append(const string &other)
	{
		if (other.empty()) {
			return;
		}

		if (size > 0 && data[size - 1] == '\0') {
			size--;
		}

		size_t copy_len = other.size;
		if (other.data[other.size - 1] == '\0') {
			copy_len--;
		}

		reserve(size + copy_len + 1);
		memcpy(data + size, other.data, copy_len);
		size += copy_len;
		data[size++] = '\0';
		cached_hash = 0;
	}

	template <typename OtherTag, uint32_t OtherSize>
	void append(const string<OtherTag, OtherSize> &other)
	{
		if (other.empty()) {
			return;
		}

		if (size > 0 && data[size - 1] == '\0') {
			size--;
		}

		size_t copy_len = other.size;
		if (other.data[other.size - 1] == '\0') {
			copy_len--;
		}

		reserve(size + copy_len + 1);
		memcpy(data + size, other.data, copy_len);
		size += copy_len;
		data[size++] = '\0';
		cached_hash = 0;
	}

	void clear()
	{
		if (data) {
			memset(data, 0, size);
		}
		size = 0;
		cached_hash = 0;
	}

	const char *c_str() const
	{
		if (!data || size == 0) {
			return "";
		}

		if (data[size - 1] != '\0') {
			const_cast<string *>(this)->reserve(size + 1);
			const_cast<string *>(this)->data[size] = '\0';
			const_cast<string *>(this)->size++;
		}
		return data;
	}

	uint32_t hash() const
	{
		if (cached_hash != 0) {
			return cached_hash;
		}

		if (!data || size == 0) {
			cached_hash = 1;
			return cached_hash;
		}

		uint32_t h = 2166136261u;
		for (uint32_t i = 0; i < size && data[i] != '\0'; i++) {
			h ^= (uint8_t)data[i];
			h *= 16777619u;
		}

		cached_hash = h ? h : 1;
		return cached_hash;
	}

	uint32_t length() const
	{
		if (!data || size == 0) {
			return 0;
		}

		if (data[size - 1] == '\0') {
			return size - 1;
		}
		return size;
	}

	bool equals(const char *cstr) const
	{
		if (!data || !cstr) {
			return !data && !cstr;
		}
		return strcmp(c_str(), cstr) == 0;
	}

	bool equals(const string &other) const
	{
		if (!data || !other.data) {
			return !data && !other.data;
		}

		if (hash() != other.hash()) {
			return false;
		}

		return strcmp(c_str(), other.c_str()) == 0;
	}

	bool operator==(const string &other) const { return equals(other); }
	bool operator==(const char *cstr) const { return equals(cstr); }
	bool operator!=(const string &other) const { return !equals(other); }
	bool operator!=(const char *cstr) const { return !equals(cstr); }

	string &operator+=(const char *cstr)
	{
		append(cstr);
		return *this;
	}

	string &operator+=(const string &other)
	{
		append(other);
		return *this;
	}

	bool operator<(const string &other) const
	{
		return strcmp(c_str(), other.c_str()) < 0;
	}

	template <typename OtherTag, uint32_t OtherSize>
	bool equals(const string<OtherTag, OtherSize> &other) const
	{
		if (!data || !other.data) {
			return !data && !other.data;
		}

		if (hash() != other.hash()) {
			return false;
		}

		return strcmp(c_str(), other.c_str()) == 0;
	}

	template <typename ArrayTag = ArenaTag>
	void split(char delimiter, array<string<ArenaTag>, ArrayTag> *result) const
	{
		result->clear();

		if (!data || size == 0) {
			return;
		}

		const char *start = data;
		const char *end = data;
		const char *limit = data + length();

		while (end < limit) {
			if (*end == delimiter) {
				string<ArenaTag> substr;
				size_t len = end - start;
				if (len > 0) {
					substr.reserve(len + 1);
					memcpy(substr.data, start, len);
					substr.data[len] = '\0';
					substr.size = len + 1;
				}
				result->push(substr);
				start = end + 1;
			}
			end++;
		}

		if (start < end) {
			string<ArenaTag> substr;
			size_t len = end - start;
			substr.reserve(len + 1);
			memcpy(substr.data, start, len);
			substr.data[len] = '\0';
			substr.size = len + 1;
			result->push(substr);
		}
	}

	template <typename OtherTag, typename ArrayTag>
	void split(char delimiter, array<string<OtherTag>, ArrayTag> *result) const
	{
		result->clear();

		if (!data || size == 0) {
			return;
		}

		const char *start = data;
		const char *end = data;
		const char *limit = data + length();

		while (end < limit) {
			if (*end == delimiter) {
				string<OtherTag> substr;
				size_t len = end - start;
				if (len > 0) {
					substr.reserve(len + 1);
					memcpy(substr.data, start, len);
					substr.data[len] = '\0';
					substr.size = len + 1;
				}
				result->push(substr);
				start = end + 1;
			}
			end++;
		}

		if (start < end) {
			string<OtherTag> substr;
			size_t len = end - start;
			substr.reserve(len + 1);
			memcpy(substr.data, start, len);
			substr.data[len] = '\0';
			substr.size = len + 1;
			result->push(substr);
		}
	}

	bool empty() const
	{
		return size == 0 || (size == 1 && data[0] == '\0');
	}

	char &operator[](uint32_t index)
	{
		assert(index < size);
		return data[index];
	}

	const char &operator[](uint32_t index) const
	{
		assert(index < size);
		return data[index];
	}

	operator const char *() const { return c_str(); }

	static string *create()
	{
		auto *str = (string *)Arena<ArenaTag>::alloc(sizeof(string));
		str->data = nullptr;
		str->size = 0;
		str->capacity = 0;
		str->cached_hash = 0;
		return str;
	}

	static string make(const char *cstr)
	{
		string s;
		s.set(cstr);
		return s;
	}

	template <typename OtherTag, uint32_t OtherSize>
	static string make(const string<OtherTag, OtherSize> &other)
	{
		string s;
		s.set(other.c_str());
		return s;
	}
};

template <typename K, typename V> struct pair
{
	K key;
	V value;
};

/**
 * Open-addressed hash map with quadratic probing.
 * - Power-of-2 sizing for fast modulo via bit masking
 * - Tombstone deletion for stable iteration
 * - Supports both integer and string keys
 * - Automatic growth at 75% load factor
 */
template <typename K, typename V, typename ArenaTag = global_arena> struct hash_map
{
	struct Entry
	{
		K		 key;
		V		 value;
		uint32_t hash;
		enum State : uint8_t
		{
			EMPTY = 0,
			OCCUPIED = 1,
			DELETED = 2
		};
		State state;
	};

	Entry	*entries = nullptr;
	uint32_t capacity = 0;
	uint32_t size = 0;
	uint32_t tombstones = 0;

	template <typename T> struct is_string : std::false_type {};
	template <typename Tag, uint32_t Size> struct is_string<string<Tag, Size>> : std::true_type {};

	uint32_t hash_key(const K &key) const
	{
		if constexpr (is_string<K>::value) {
			return key.hash();
		}
		else if constexpr (std::is_integral<K>::value) {
			return hash_int(key);
		}
		else {
			static_assert(std::is_integral<K>::value || is_string<K>::value,
						  "Key must be an integer or string type");
			return 0;
		}
	}

	template <typename OtherTag, uint32_t OtherSize>
	uint32_t hash_key(const string<OtherTag, OtherSize> &key) const
	{
		return key.hash();
	}

	bool keys_equal(const K &a, const K &b) const
	{
		if constexpr (is_string<K>::value) {
			return a.equals(b);
		}
		else {
			return a == b;
		}
	}

	uint32_t hash_cstr(const char *cstr) const
	{
		if (!cstr || !*cstr) {
			return 1;
		}

		uint32_t h = 2166136261u;
		while (*cstr) {
			h ^= (uint8_t)*cstr++;
			h *= 16777619u;
		}

		return h ? h : 1;
	}

	template <typename U = K>
	typename std::enable_if<is_string<U>::value, V *>::type get(const char *key)
	{
		if (!entries || size == 0 || !key) {
			return nullptr;
		}

		uint32_t hash = hash_cstr(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true) {
			Entry &entry = entries[idx];

			if (entry.state == Entry::EMPTY) {
				return nullptr;
			}

			if (entry.state == Entry::OCCUPIED && entry.hash == hash && entry.key.equals(key)) {
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename U = K>
	typename std::enable_if<is_string<U>::value, const V *>::type get(const char *key) const
	{
		return const_cast<hash_map *>(this)->template get<U>(key);
	}

	template <typename U = K>
	typename std::enable_if<is_string<U>::value, V *>::type insert(const char *key, const V &value)
	{
		if (!key) {
			return nullptr;
		}

		if (!entries) {
			init();
		}

		if ((size + tombstones) * 4 >= capacity * 3) {
			grow();
		}

		uint32_t hash = hash_cstr(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;
		uint32_t first_deleted = (uint32_t)-1;

		while (true) {
			Entry &entry = entries[idx];

			if (entry.state == Entry::EMPTY) {
				if (first_deleted != (uint32_t)-1) {
					Entry &deleted_entry = entries[first_deleted];
					deleted_entry.key.set(key);
					deleted_entry.value = value;
					deleted_entry.hash = hash;
					deleted_entry.state = Entry::OCCUPIED;
					tombstones--;
					size++;
					return &deleted_entry.value;
				}
				else {
					entry.key.set(key);
					entry.value = value;
					entry.hash = hash;
					entry.state = Entry::OCCUPIED;
					size++;
					return &entry.value;
				}
			}

			if (entry.state == Entry::DELETED) {
				if (first_deleted == (uint32_t)-1) {
					first_deleted = idx;
				}
			}
			else if (entry.hash == hash && entry.key.equals(key)) {
				entry.value = value;
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename U = K>
	typename std::enable_if<is_string<U>::value, bool>::type contains(const char *key) const
	{
		return const_cast<hash_map *>(this)->get(key) != nullptr;
	}

	template <typename U = K>
	typename std::enable_if<is_string<U>::value, bool>::type remove(const char *key)
	{
		if (!entries || size == 0 || !key) {
			return false;
		}

		uint32_t hash = hash_cstr(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true) {
			Entry &entry = entries[idx];

			if (entry.state == Entry::EMPTY) {
				return false;
			}

			if (entry.state == Entry::OCCUPIED && entry.hash == hash && entry.key.equals(key)) {
				entry.state = Entry::DELETED;
				size--;
				tombstones++;
				return true;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename U = K>
	typename std::enable_if<is_string<U>::value, V &>::type operator[](const char *key)
	{
		V *val = get(key);
		if (val) {
			return *val;
		}

		V default_val = {};
		return *insert(key, default_val);
	}



	template <typename OtherTag, uint32_t OtherSize>
	bool keys_equal(const K &a, const string<OtherTag, OtherSize> &b) const
	{
		if constexpr (is_string<K>::value) {
			return a.equals(b);
		}
		else {
			static_assert(std::is_integral<K>::value, "Key must be an integer or string type");
			return false;
		}
	}

	void init(uint32_t initial_capacity = 16)
	{
		if (entries) {
			return;
		}

		initial_capacity--;
		initial_capacity |= initial_capacity >> 1;
		initial_capacity |= initial_capacity >> 2;
		initial_capacity |= initial_capacity >> 4;
		initial_capacity |= initial_capacity >> 8;
		initial_capacity |= initial_capacity >> 16;
		initial_capacity++;

		capacity = initial_capacity;
		entries = (Entry *)Arena<ArenaTag>::alloc(capacity * sizeof(Entry));
		memset(entries, 0, capacity * sizeof(Entry));
		size = 0;
		tombstones = 0;
	}

	V *insert_internal(const K &key, uint32_t hash, const V &value)
	{
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true) {
			Entry &entry = entries[idx];

			if (entry.state != Entry::OCCUPIED) {
				entry.key = key;
				entry.value = value;
				entry.hash = hash;
				entry.state = Entry::OCCUPIED;
				size++;
				return &entry.value;
			}

			if (entry.hash == hash && keys_equal(entry.key, key)) {
				entry.value = value;
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	void grow()
	{
		uint32_t old_capacity = capacity;
		Entry	*old_entries = entries;

		capacity = old_capacity * 2;
		entries = (Entry *)Arena<ArenaTag>::alloc(capacity * sizeof(Entry));
		memset(entries, 0, capacity * sizeof(Entry));

		uint32_t old_size = size;
		size = 0;
		tombstones = 0;

		for (uint32_t i = 0; i < old_capacity; i++) {
			if (old_entries[i].state == Entry::OCCUPIED) {
				insert_internal(old_entries[i].key, old_entries[i].hash, old_entries[i].value);
			}
		}

		Arena<ArenaTag>::reclaim(old_entries, old_capacity * sizeof(Entry));
	}

	V *insert(const K &key, const V &value)
	{
		if (!entries) {
			init();
		}

		if ((size + tombstones) * 4 >= capacity * 3) {
			grow();
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;
		uint32_t first_deleted = (uint32_t)-1;

		while (true) {
			Entry &entry = entries[idx];

			if (entry.state == Entry::EMPTY) {
				if (first_deleted != (uint32_t)-1) {
					Entry &deleted_entry = entries[first_deleted];
					deleted_entry.key = key;
					deleted_entry.value = value;
					deleted_entry.hash = hash;
					deleted_entry.state = Entry::OCCUPIED;
					tombstones--;
					size++;
					return &deleted_entry.value;
				}
				else {
					entry.key = key;
					entry.value = value;
					entry.hash = hash;
					entry.state = Entry::OCCUPIED;
					size++;
					return &entry.value;
				}
			}

			if (entry.state == Entry::DELETED) {
				if (first_deleted == (uint32_t)-1) {
					first_deleted = idx;
				}
			}
			else if (entry.hash == hash && keys_equal(entry.key, key)) {
				entry.value = value;
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename OtherTag, uint32_t OtherSize>
	V *insert(const string<OtherTag, OtherSize> &key, const V &value)
	{
		static_assert(is_string<K>::value, "insert(string) can only be used with string keys");
		if (!entries) {
			init();
		}

		if ((size + tombstones) * 4 >= capacity * 3) {
			grow();
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;
		uint32_t first_deleted = (uint32_t)-1;

		while (true) {
			Entry &entry = entries[idx];

			if (entry.state == Entry::EMPTY) {
				if (first_deleted != (uint32_t)-1) {
					Entry &deleted_entry = entries[first_deleted];
					deleted_entry.key.set(key.c_str());
					deleted_entry.value = value;
					deleted_entry.hash = hash;
					deleted_entry.state = Entry::OCCUPIED;
					tombstones--;
					size++;
					return &deleted_entry.value;
				}
				else {
					entry.key.set(key.c_str());
					entry.value = value;
					entry.hash = hash;
					entry.state = Entry::OCCUPIED;
					size++;
					return &entry.value;
				}
			}

			if (entry.state == Entry::DELETED) {
				if (first_deleted == (uint32_t)-1) {
					first_deleted = idx;
				}
			}
			else if (entry.hash == hash && keys_equal(entry.key, key)) {
				entry.value = value;
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	V *get(const K &key)
	{
		if (!entries || size == 0) {
			return nullptr;
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true) {
			Entry &entry = entries[idx];

			if (entry.state == Entry::EMPTY) {
				return nullptr;
			}

			if (entry.state == Entry::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key)) {
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename OtherTag, uint32_t OtherSize>
	V *get(const string<OtherTag, OtherSize> &key)
	{
		static_assert(is_string<K>::value, "get(string) can only be used with string keys");
		if (!entries || size == 0) {
			return nullptr;
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true) {
			Entry &entry = entries[idx];

			if (entry.state == Entry::EMPTY) {
				return nullptr;
			}

			if (entry.state == Entry::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key)) {
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	const V *get(const K &key) const
	{
		return const_cast<hash_map *>(this)->get(key);
	}

	template <typename OtherTag, uint32_t OtherSize>
	const V *get(const string<OtherTag, OtherSize> &key) const
	{
		return const_cast<hash_map *>(this)->get(key);
	}

	bool contains(const K &key) const
	{
		return const_cast<hash_map *>(this)->get(key) != nullptr;
	}

	template <typename OtherTag, uint32_t OtherSize>
	bool contains(const string<OtherTag, OtherSize> &key) const
	{
		return const_cast<hash_map *>(this)->get(key) != nullptr;
	}

	bool remove(const K &key)
	{
		if (!entries || size == 0) {
			return false;
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true) {
			Entry &entry = entries[idx];

			if (entry.state == Entry::EMPTY) {
				return false;
			}

			if (entry.state == Entry::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key)) {
				entry.state = Entry::DELETED;
				size--;
				tombstones++;
				return true;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename OtherTag, uint32_t OtherSize>
	bool remove(const string<OtherTag, OtherSize> &key)
	{
		static_assert(is_string<K>::value, "remove(string) can only be used with string keys");
		if (!entries || size == 0) {
			return false;
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true) {
			Entry &entry = entries[idx];

			if (entry.state == Entry::EMPTY) {
				return false;
			}

			if (entry.state == Entry::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key)) {
				entry.state = Entry::DELETED;
				size--;
				tombstones++;
				return true;
			}

			idx = (idx + 1) & mask;
		}
	}

	void clear()
	{
		if (entries) {
			memset(entries, 0, capacity * sizeof(Entry));
		}
		size = 0;
		tombstones = 0;
	}

	template <typename ArrayTag = ArenaTag>
	void collect(array<pair<K, V>, ArrayTag> *out) const
	{
		out->clear();
		if (!entries || size == 0) {
			return;
		}

		out->reserve(size);

		for (uint32_t i = 0; i < capacity; i++) {
			const Entry &entry = entries[i];
			if (entry.state == Entry::OCCUPIED) {
				pair<K, V> p = {entry.key, entry.value};
				out->push(p);
			}
		}
	}

	struct iterator
	{
		Entry	*entries;
		uint32_t capacity;
		uint32_t idx;

		iterator(Entry *e, uint32_t cap, uint32_t i) : entries(e), capacity(cap), idx(i)
		{
			while (idx < capacity && entries[idx].state != Entry::OCCUPIED) {
				idx++;
			}
		}

		pair<K &, V &> operator*()
		{
			return {entries[idx].key, entries[idx].value};
		}

		iterator &operator++()
		{
			idx++;
			while (idx < capacity && entries[idx].state != Entry::OCCUPIED) {
				idx++;
			}
			return *this;
		}

		bool operator!=(const iterator &other) const
		{
			return idx != other.idx;
		}
	};

	iterator begin() { return iterator(entries, capacity, 0); }
	iterator end() { return iterator(entries, capacity, capacity); }

	V &operator[](const K &key)
	{
		V *val = get(key);
		if (val) {
			return *val;
		}

		V default_val = {};
		return *insert(key, default_val);
	}

	template <typename OtherTag, uint32_t OtherSize>
	V &operator[](const string<OtherTag, OtherSize> &key)
	{
		static_assert(is_string<K>::value, "operator[](string) can only be used with string keys");
		V *val = get(key);
		if (val) {
			return *val;
		}

		V default_val = {};
		return *insert(key, default_val);
	}

	bool empty() const { return size == 0; }

	static hash_map *create(uint32_t initial_capacity = 16)
	{
		auto *m = (hash_map *)Arena<ArenaTag>::alloc(sizeof(hash_map));
		m->entries = nullptr;
		m->capacity = 0;
		m->size = 0;
		m->tombstones = 0;
		m->init(initial_capacity);
		return m;
	}
};

/**
 * Set implementation using hash_map with dummy values.
 * - Inherits all performance characteristics from hash_map
 * - Provides set-specific interface (insert returns bool)
 */
template <typename K, typename ArenaTag = global_arena> struct hash_set
{
	hash_map<K, uint8_t, ArenaTag> map;

	void init(uint32_t initial_capacity = 16)
	{
		map.init(initial_capacity);
	}

	bool insert(const K &key)
	{
		if (map.contains(key)) {
			return false;
		}
		map.insert(key, 1);
		return true;
	}

	template <typename OtherTag, uint32_t OtherSize>
	bool insert(const string<OtherTag, OtherSize> &key)
	{
		static_assert(hash_map<K, uint8_t, ArenaTag>::template is_string<K>::value,
					  "insert(string) can only be used with string keys");
		if (map.contains(key)) {
			return false;
		}
		map.insert(key, 1);
		return true;
	}

	template <typename U = K>
	typename std::enable_if<hash_map<K, uint8_t, ArenaTag>::template is_string<U>::value, bool>::type
	insert(const char *key)
	{
		if (map.contains(key)) {
			return false;
		}
		map.insert(key, 1);
		return true;
	}

	bool contains(const K &key) const
	{
		return map.contains(key);
	}

	template <typename OtherTag, uint32_t OtherSize>
	bool contains(const string<OtherTag, OtherSize> &key) const
	{
		static_assert(hash_map<K, uint8_t, ArenaTag>::template is_string<K>::value,
					  "contains(string) can only be used with string keys");
		return map.contains(key);
	}

	template <typename U = K>
	typename std::enable_if<hash_map<K, uint8_t, ArenaTag>::template is_string<U>::value, bool>::type
	contains(const char *key) const
	{
		return map.contains(key);
	}

	bool remove(const K &key)
	{
		return map.remove(key);
	}

	template <typename OtherTag, uint32_t OtherSize>
	bool remove(const string<OtherTag, OtherSize> &key)
	{
		static_assert(hash_map<K, uint8_t, ArenaTag>::template is_string<K>::value,
					  "remove(string) can only be used with string keys");
		return map.remove(key);
	}

	template <typename U = K>
	typename std::enable_if<hash_map<K, uint8_t, ArenaTag>::template is_string<U>::value, bool>::type
	remove(const char *key)
	{
		return map.remove(key);
	}

	void clear()
	{
		map.clear();
	}

	uint32_t size() const { return map.size; }
	bool empty() const { return map.empty(); }

	static hash_set *create(uint32_t initial_capacity = 16)
	{
		auto *s = (hash_set *)Arena<ArenaTag>::alloc(sizeof(hash_set));
		s->map.entries = nullptr;
		s->map.capacity = 0;
		s->map.size = 0;
		s->map.tombstones = 0;
		s->map.init(initial_capacity);
		return s;
	}
};
