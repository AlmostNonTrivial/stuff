
#pragma once
#include <cstring>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <typeinfo>
#include <type_traits>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

// Cross-platform virtual memory operations
struct VirtualMemory
{
	static void *
	reserve(size_t size)
	{
#ifdef _WIN32
		return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE);
#else
		void *ptr = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
		return (ptr == MAP_FAILED) ? nullptr : ptr;
#endif
	}

	static bool
	commit(void *addr, size_t size)
	{
#ifdef _WIN32
		return VirtualAlloc(addr, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#else
		return mprotect(addr, size, PROT_READ | PROT_WRITE) == 0;
#endif
	}

	static void
	decommit(void *addr, size_t size)
	{
#ifdef _WIN32
		VirtualFree(addr, size, MEM_DECOMMIT);
#else
		madvise(addr, size, MADV_DONTNEED);
		mprotect(addr, size, PROT_NONE);
#endif
	}

	static void
	release(void *addr, size_t size)
	{
#ifdef _WIN32
		(void)size; // Windows doesn't need size for MEM_RELEASE
		VirtualFree(addr, 0, MEM_RELEASE);
#else
		munmap(addr, size);
#endif
	}

	static size_t
	page_size()
	{
		static size_t cached_size = 0;
		if (cached_size == 0)
		{
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

	static size_t
	round_to_pages(size_t size)
	{
		size_t page_sz = page_size();
		return ((size + page_sz - 1) / page_sz) * page_sz;
	}
};

struct global_arena
{
};

// Arena with virtual memory backing and automatic reclamation
template <typename Tag> struct Arena
{
	static inline uint8_t *base = nullptr;
	static inline uint8_t *current = nullptr;
	static inline size_t   reserved_capacity = 0;
	static inline size_t   committed_capacity = 0;
	static inline size_t   max_capacity = 0;
	static inline size_t   initial_commit = 0;

	// Freelist management
	struct FreeBlock
	{
		FreeBlock *next;
		size_t	   size;
	};

	// Freelists for different size classes (powers of 2)
	// freelists[i] holds blocks of size [2^i, 2^(i+1))
	static inline FreeBlock *freelists[32] = {};
	static inline size_t	 reclaimed_bytes = 0;
	static inline size_t	 reused_bytes = 0;

	// Get size class for a given size (which freelist bucket to use)
	static int
	get_size_class(size_t size)
	{
		if (size == 0)
			return 0;

		// Find highest bit set (essentially log2)
		int	   cls = 0;
		size_t s = size - 1;
		if (s >= (1ULL << 16))
		{
			cls += 16;
			s >>= 16;
		}
		if (s >= (1ULL << 8))
		{
			cls += 8;
			s >>= 8;
		}
		if (s >= (1ULL << 4))
		{
			cls += 4;
			s >>= 4;
		}
		if (s >= (1ULL << 2))
		{
			cls += 2;
			s >>= 2;
		}
		if (s >= (1ULL << 1))
		{
			cls += 1;
		}

		return cls < 31 ? cls : 31;
	}

	// Initialize with initial size and optional maximum
	static void
	init(size_t initial = 4 * 1024 * 1024, size_t maximum = 0)
	{
		if (base)
			return;

		initial_commit = VirtualMemory::round_to_pages(initial);
		max_capacity = maximum;

		reserved_capacity = max_capacity ? max_capacity : (1ULL << 38);

		base = (uint8_t *)VirtualMemory::reserve(reserved_capacity);
		if (!base)
		{
			reserved_capacity = 1ULL << 33;
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

		// Clear freelists
		for (int i = 0; i < 32; i++)
		{
			freelists[i] = nullptr;
		}
		reclaimed_bytes = 0;
		reused_bytes = 0;
	}

	// Shutdown and free all memory
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
			reclaimed_bytes = 0;
			reused_bytes = 0;
		}
	}

	// Reclaim memory for reuse (called automatically by containers)
	static void
	reclaim(void *ptr, size_t size)
	{
		if (!ptr || size < sizeof(FreeBlock))
			return;

		// Only reclaim if the block is within our arena
		uint8_t *addr = (uint8_t *)ptr;
		if (addr < base || addr >= base + reserved_capacity)
			return;

		int size_class = get_size_class(size);

		FreeBlock *block = (FreeBlock *)ptr;
		block->size = size;
		block->next = freelists[size_class];
		freelists[size_class] = block;

		reclaimed_bytes += size;
	}

	// Try to allocate from freelists
	static void *
	try_alloc_from_freelist(size_t size)
	{
		int size_class = get_size_class(size);

		// Search in this size class and larger ones
		for (int cls = size_class; cls < 32; cls++)
		{
			FreeBlock **prev_ptr = &freelists[cls];
			FreeBlock  *block = freelists[cls];

			while (block)
			{
				if (block->size >= size)
				{
					// Remove from freelist
					*prev_ptr = block->next;

					// If block is significantly larger, split it
					size_t remaining = block->size - size;
					if (remaining >= sizeof(FreeBlock) && remaining >= 64)
					{
						// Put remainder back in appropriate freelist
						uint8_t *remainder_addr = ((uint8_t *)block) + size;
						reclaim(remainder_addr, remaining);
					}
					else
					{
						// Use whole block to avoid tiny fragments
						size = block->size;
					}

					reused_bytes += size;
					return block;
				}
				prev_ptr = &block->next;
				block = block->next;
			}
		}

		return nullptr;
	}

	// Allocate memory from arena
	static void *
	alloc(size_t size)
	{
		if (!base)
		{
			init();
		}

		// Try freelists first
		void *recycled = try_alloc_from_freelist(size);
		if (recycled)
		{
			return recycled;
		}

		// Normal allocation path
		size_t	  align = 8;
		uintptr_t current_addr = (uintptr_t)current;
		uintptr_t aligned_addr = (current_addr + align - 1) & ~(align - 1);

		uint8_t *aligned = (uint8_t *)aligned_addr;
		uint8_t *next = aligned + size;

		// Check if we need to commit more memory
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

			size_t new_committed = VirtualMemory::round_to_pages(needed);
			size_t min_growth = committed_capacity > 0 ? committed_capacity + committed_capacity / 2 : initial_commit;
			if (new_committed < min_growth)
			{
				new_committed = min_growth;
			}

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

	// Reset arena (keep memory committed)
	static void
	reset()
	{
		current = base;
		if (base && committed_capacity > 0)
		{
			memset(base, 0, committed_capacity);
		}

		// Clear freelists
		for (int i = 0; i < 32; i++)
		{
			freelists[i] = nullptr;
		}
		reclaimed_bytes = 0;
		reused_bytes = 0;
	}

	// Reset and decommit memory back to initial size
	static void
	reset_and_decommit()
	{
		current = base;

		if (committed_capacity > initial_commit)
		{
			VirtualMemory::decommit(base + initial_commit, committed_capacity - initial_commit);
			committed_capacity = initial_commit;
		}

		if (base && committed_capacity > 0)
		{
			memset(base, 0, committed_capacity);
		}

		// Clear freelists
		for (int i = 0; i < 32; i++)
		{
			freelists[i] = nullptr;
		}
		reclaimed_bytes = 0;
		reused_bytes = 0;
	}

	// Query functions
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

	// Get freelist stats
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

	// Print detailed stats
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
	}
};

// Convenience namespace
namespace arena
{
template <typename Tag>
void
init(size_t initial = 4 * 1024 * 1024, size_t maximum = 0)
{
	Arena<Tag>::init(initial, maximum);
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

template <typename Tag>
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

template <typename Tag>
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

template <typename Tag>
uint8_t *
stream_finish(StreamAlloc<Tag> *stream)
{
	// Shrink allocation to actual size (give back unused reservation)
	Arena<Tag>::current = stream->write_pos;
	return stream->start;
}

template <typename Tag>
void
stream_abandon(StreamAlloc<Tag> *stream)
{
	// Reset current to where we started
	Arena<Tag>::current = stream->start;
}

template <typename Tag>
size_t
stream_size(const StreamAlloc<Tag> *stream)
{
	return stream->write_pos - stream->start;
}

} // namespace arena

// Forward declaration of string
template <typename ArenaTag, uint32_t InitialSize> struct string;

// Forward declaration of array
template <typename T, typename ArenaTag, uint32_t InitialSize> struct array;

// Array class - data-oriented style without constructors/destructors
template <typename T, typename ArenaTag = global_arena, uint32_t InitialSize = 8> struct array
{
	// Public data members - direct access
	T		*data = nullptr;
	uint32_t size = 0;
	uint32_t capacity = 0;

	// Type trait to check if T is a string type
	template <typename U> struct is_string : std::false_type
	{
	};

	template <typename Tag, uint32_t Size> struct is_string<string<Tag, Size>> : std::true_type
	{
	};

	// Member functions instead of free functions

	void
	reserve(uint32_t min_capacity)
	{
		if (capacity >= min_capacity)
			return;

		// Lazy init if needed
		if (!data)
		{
			capacity = min_capacity > InitialSize ? min_capacity : InitialSize;
			data = (T *)Arena<ArenaTag>::alloc(capacity * sizeof(T));
			return;
		}

		// Save old data for reclamation
		T		*old_data = data;
		uint32_t old_capacity = capacity;

		// Calculate new capacity (at least double, but ensure we meet min_capacity)
		uint32_t new_cap = capacity * 2;
		if (new_cap < min_capacity)
			new_cap = min_capacity;

		// Allocate and copy
		T *new_data = (T *)Arena<ArenaTag>::alloc(new_cap * sizeof(T));
		memcpy(new_data, data, size * sizeof(T));

		data = new_data;
		capacity = new_cap;

		// Automatically reclaim old memory
		Arena<ArenaTag>::reclaim(old_data, old_capacity * sizeof(T));
	}

	uint32_t
	push(const T &value)
	{
		reserve(size + 1);
		data[size] = value;
		return size++;
	}

	// Push for string from different arena
	template <typename OtherTag = ArenaTag, uint32_t OtherSize = InitialSize>
	uint32_t
	push(const string<OtherTag, OtherSize> &value)
	{
		static_assert(is_string<T>::value, "push(string) can only be used with string arrays");
		reserve(size + 1);
		T &dest = data[size];
		dest.set(value.c_str());
		return size++;
	}

	T *
	push_n(const T *values, uint32_t count)
	{
		reserve(size + count);
		T *dest = data + size;
		memcpy(dest, values, count * sizeof(T));
		size += count;
		return dest;
	}

	// Push_n for string array from different arena
	template <typename OtherTag, uint32_t OtherSize>
	T *
	push_n(const string<OtherTag, OtherSize> *values, uint32_t count)
	{
		static_assert(is_string<T>::value, "push_n(string) can only be used with string arrays");
		reserve(size + count);
		T *dest = data + size;
		for (uint32_t i = 0; i < count; i++)
		{
			dest[i].set(values[i].c_str());
		}
		size += count;
		return dest;
	}

	void
	clear()
	{
		if (data)
			memset(data, 0, sizeof(T) * size);
		size = 0;
	}

	void
	resize(uint32_t new_size)
	{
		reserve(new_size);

		// Zero out new elements if growing
		if (new_size > size)
			memset(data + size, 0, (new_size - size) * sizeof(T));

		size = new_size;
	}

	void
	shrink_to_fit()
	{
		if (size == capacity || size == 0)
			return;

		T		*old_data = data;
		uint32_t old_capacity = capacity;

		// Allocate exact size needed
		data = (T *)Arena<ArenaTag>::alloc(size * sizeof(T));
		memcpy(data, old_data, size * sizeof(T));
		capacity = size;

		// Reclaim old memory
		Arena<ArenaTag>::reclaim(old_data, old_capacity * sizeof(T));
	}

	// Set contents from another array
	template <typename OtherTag>
	void
	set(const array<T, OtherTag> &other)
	{
		clear();
		reserve(other.size);

		if (other.size > 0)
		{
			memcpy(data, other.data, other.size * sizeof(T));
			size = other.size;
		}
	}

	// Set string array from array of strings in different arena
	template <typename OtherTag, uint32_t OtherSize, typename OtherArrayTag>
	void
	set(const array<string<OtherTag, OtherSize>, OtherArrayTag> &other)
	{
		static_assert(is_string<T>::value, "set(string array) can only be used with string arrays");
		clear();
		reserve(other.size);

		for (uint32_t i = 0; i < other.size; i++)
		{
			data[i].set(other.data[i].c_str());
		}
		size = other.size;
	}

	// Element access
	T &
	operator[](uint32_t index)
	{
		return data[index];
	}
	const T &
	operator[](uint32_t index) const
	{
		return data[index];
	}

	T *
	begin()
	{
		return data;
	}
	T *
	end()
	{
		return data + size;
	}
	const T *
	begin() const
	{
		return data;
	}
	const T *
	end() const
	{
		return data + size;
	}

	bool
	empty() const
	{
		return size == 0;
	}

	// Static factory function for heap allocation (replaces array_create)
	static array *
	create()
	{
		auto *arr = (array *)Arena<ArenaTag>::alloc(sizeof(array));
		arr->data = nullptr;
		arr->size = 0;
		arr->capacity = 0;
		return arr;
	}
};

// Generic hash functions for different integer sizes
inline uint32_t
hash_32(uint32_t x)
{
	x = ((x >> 16) ^ x) * 0x45d9f3b;
	x = ((x >> 16) ^ x) * 0x45d9f3b;
	x = (x >> 16) ^ x;
	return x;
}

inline uint64_t
hash_64(uint64_t x)
{
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
	x = x ^ (x >> 31);
	return x;
}

// Template hash that picks the right function based on size
template <typename T>
inline uint32_t
hash_int(T x)
{
	// Convert all types to unsigned for hashing
	using U = typename std::make_unsigned<T>::type;
	U ux = static_cast<U>(x);

	if constexpr (sizeof(T) <= 4)
	{
		return hash_32(static_cast<uint32_t>(ux));
	}
	else
	{
		// For 64-bit, hash and then reduce to 32-bit
		return static_cast<uint32_t>(hash_64(static_cast<uint64_t>(ux)));
	}
}

// String class with hash support
template <typename ArenaTag = global_arena, uint32_t InitialSize = 32> struct string
{
	// String is backed by char array
	char	*data = nullptr;
	uint32_t size = 0; // Includes null terminator when present
	uint32_t capacity = 0;

	// Cached hash - 0 means not computed, actual hash is never 0 (we'll ensure that)
	mutable uint32_t cached_hash = 0;

	// Assignment from C string
	string &
	operator=(const char *cstr)
	{
		if (cstr)
		{
			set(cstr);
		}
		else
		{
			clear();
		}
		return *this;
	}

	// Assignment from another string (same arena tag)
	string &
	operator=(const string &other)
	{
		if (this != &other)
		{
			if (other.empty())
			{
				clear();
			}
			else
			{
				set(other.c_str());
			}
		}
		return *this;
	}

	// Assignment from string with different arena tag
	template <typename OtherTag, uint32_t OtherSize>
	string &
	operator=(const string<OtherTag, OtherSize> &other)
	{
		if (other.empty())
		{
			clear();
		}
		else
		{
			set(other.c_str());
		}
		return *this;
	}

	void
	reserve(uint32_t min_capacity)
	{
		if (capacity >= min_capacity)
			return;

		if (!data)
		{
			capacity = min_capacity > InitialSize ? min_capacity : InitialSize;
			data = (char *)Arena<ArenaTag>::alloc(capacity);
			return;
		}

		char	*old_data = data;
		uint32_t old_capacity = capacity;

		uint32_t new_cap = capacity * 2;
		if (new_cap < min_capacity)
			new_cap = min_capacity;

		char *new_data = (char *)Arena<ArenaTag>::alloc(new_cap);
		memcpy(new_data, data, size);

		data = new_data;
		capacity = new_cap;
		cached_hash = 0; // Invalidate hash cache

		Arena<ArenaTag>::reclaim(old_data, old_capacity);
	}

	void
set(const char *cstr, size_t len = 0)  // Renamed parameter
{
		size_t actual_len;
		if (len != 0)
		{
			actual_len = len;
		}
		else
		{
			actual_len = strlen(cstr) + 1;
		}
		reserve(actual_len);
		memcpy(data, cstr, actual_len);
		size = actual_len;  // Now assigns to member variable
		cached_hash = 0;
}

	// Set from string with different arena tag
	template <typename OtherTag, uint32_t OtherSize>
	void
	set(const string<OtherTag, OtherSize> &other)
	{
		if (other.empty())
		{
			clear();
		}
		else
		{
			set(other.c_str());
		}
	}

	void
	append(const char *cstr)
	{
		if (size > 0 && data[size - 1] == '\0')
			size--; // Remove old null terminator

		size_t len = strlen(cstr);
		reserve(size + len + 1);

		memcpy(data + size, cstr, len);
		size += len;
		data[size++] = '\0';
		cached_hash = 0;
	}

	void
	append(const string &other)
	{
		if (other.empty())
			return;

		if (size > 0 && data[size - 1] == '\0')
			size--;

		size_t copy_len = other.size;
		if (other.data[other.size - 1] == '\0')
			copy_len--; // Don't copy their null terminator

		reserve(size + copy_len + 1);
		memcpy(data + size, other.data, copy_len);
		size += copy_len;
		data[size++] = '\0';
		cached_hash = 0;
	}

	// Append string from different arena
	template <typename OtherTag, uint32_t OtherSize>
	void
	append(const string<OtherTag, OtherSize> &other)
	{
		if (other.empty())
			return;

		if (size > 0 && data[size - 1] == '\0')
			size--;

		size_t copy_len = other.size;
		if (other.data[other.size - 1] == '\0')
			copy_len--; // Don't copy their null terminator

		reserve(size + copy_len + 1);
		memcpy(data + size, other.data, copy_len);
		size += copy_len;
		data[size++] = '\0';
		cached_hash = 0;
	}

	void
	clear()
	{
		if (data)
			memset(data, 0, size);
		size = 0;
		cached_hash = 0;
	}

	// Get C string (ensures null termination)
	const char *
	c_str() const
	{
		if (!data || size == 0)
			return "";

		// Ensure null terminated
		if (data[size - 1] != '\0')
		{
			// This is a bit hacky but maintains const-ness for the user
			const_cast<string *>(this)->reserve(size + 1);
			const_cast<string *>(this)->data[size] = '\0';
			const_cast<string *>(this)->size++;
		}
		return data;
	}

	// Compute hash - cached for efficiency
	uint32_t
	hash() const
	{
		if (cached_hash != 0)
			return cached_hash;

		if (!data || size == 0)
		{
			cached_hash = 1; // Use 1 for empty string so 0 means "not computed"
			return cached_hash;
		}

		// FNV-1a hash
		uint32_t h = 2166136261u;
		for (uint32_t i = 0; i < size && data[i] != '\0'; i++)
		{
			h ^= (uint8_t)data[i];
			h *= 16777619u;
		}

		// Ensure hash is never 0 (so we can use 0 as "not computed")
		cached_hash = h ? h : 1;
		return cached_hash;
	}

	// Length without null terminator
	uint32_t
	length() const
	{
		if (!data || size == 0)
			return 0;

		// If we have a null terminator, don't count it
		if (data[size - 1] == '\0')
			return size - 1;
		return size;
	}

	bool
	equals(const char *cstr) const
	{
		if (!data || !cstr)
			return !data && !cstr;
		return strcmp(c_str(), cstr) == 0;
	}

	bool
	equals(const string &other) const
	{
		if (!data || !other.data)
			return !data && !other.data;

		// Quick hash check first
		if (hash() != other.hash())
			return false;

		return strcmp(c_str(), other.c_str()) == 0;
	}

	// Equals for string with different arena
	template <typename OtherTag, uint32_t OtherSize>
	bool
	equals(const string<OtherTag, OtherSize> &other) const
	{
		if (!data || !other.data)
			return !data && !other.data;

		// Quick hash check first
		if (hash() != other.hash())
			return false;

		return strcmp(c_str(), other.c_str()) == 0;
	}

	// Split string by delimiter
	template <typename ArrayTag = ArenaTag>
	void
	split(char delimiter, array<string<ArenaTag>, ArrayTag> *result) const
	{
		result->clear();

		if (!data || size == 0)
			return;

		const char *start = data;
		const char *end = data;
		const char *limit = data + length(); // Don't include null terminator

		while (end < limit)
		{
			if (*end == delimiter)
			{
				string<ArenaTag> substr;
				size_t			 len = end - start;
				if (len > 0)
				{
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

		// Add the last substring
		if (start < end)
		{
			string<ArenaTag> substr;
			size_t			 len = end - start;
			substr.reserve(len + 1);
			memcpy(substr.data, start, len);
			substr.data[len] = '\0';
			substr.size = len + 1;
			result->push(substr);
		}
	}

	// Split string by delimiter into array with different arena
	template <typename OtherTag, typename ArrayTag>
	void
	split(char delimiter, array<string<OtherTag>, ArrayTag> *result) const
	{
		result->clear();

		if (!data || size == 0)
			return;

		const char *start = data;
		const char *end = data;
		const char *limit = data + length(); // Don't include null terminator

		while (end < limit)
		{
			if (*end == delimiter)
			{
				string<OtherTag> substr;
				size_t			 len = end - start;
				if (len > 0)
				{
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

		// Add the last substring
		if (start < end)
		{
			string<OtherTag> substr;
			size_t			 len = end - start;
			substr.reserve(len + 1);
			memcpy(substr.data, start, len);
			substr.data[len] = '\0';
			substr.size = len + 1;
			result->push(substr);
		}
	}

	bool
	empty() const
	{
		return size == 0 || (size == 1 && data[0] == '\0');
	}

	// Operator overloads for convenience
	char &
	operator[](uint32_t index)
	{
		return data[index];
	}
	const char &
	operator[](uint32_t index) const
	{
		return data[index];
	}

	// Implicit conversion to C string for convenience
	operator const char *() const
	{
		return c_str();
	}

	// Static factory for heap allocation
	static string *
	create()
	{
		auto *str = (string *)Arena<ArenaTag>::alloc(sizeof(string));
		str->data = nullptr;
		str->size = 0;
		str->capacity = 0;
		str->cached_hash = 0;
		return str;
	}

	// Static helper to duplicate a C string
	static string
	make(const char *cstr)
	{
		string s;
		s.set(cstr);
		return s;
	}

	// Static helper to duplicate a string from different arena
	template <typename OtherTag, uint32_t OtherSize>
	static string
	make(const string<OtherTag, OtherSize> &other)
	{
		string s;
		s.set(other.c_str());
		return s;
	}
};

// Pair structure
template <typename K, typename V> struct pair
{
	K key;
	V value;
};

// HashMap class - supports primitives and string keys
template <typename K, typename V, typename ArenaTag = global_arena> struct hash_map
{
	struct Entry
	{
		K		 key;
		V		 value;
		uint32_t hash; // Store hash for all types (for strings, avoids recomputing)
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

	// Type trait to check if K is a string type
	template <typename T> struct is_string : std::false_type
	{
	};

	template <typename Tag, uint32_t Size> struct is_string<string<Tag, Size>> : std::true_type
	{
	};

	// Hash function - picks the right implementation
	uint32_t
	hash_key(const K &key) const
	{
		if constexpr (is_string<K>::value)
		{
			return key.hash();
		}
		else if constexpr (std::is_integral<K>::value)
		{
			return hash_int(key);
		}
		else
		{
			static_assert(std::is_integral<K>::value || is_string<K>::value, "Key must be an integer or string type");
			return 0;
		}
	}

	// Hash function for string from different arena
	template <typename OtherTag, uint32_t OtherSize>
	uint32_t
	hash_key(const string<OtherTag, OtherSize> &key) const
	{
		return key.hash();
	}

	// Key comparison
	bool
	keys_equal(const K &a, const K &b) const
	{
		if constexpr (is_string<K>::value)
		{
			return a.equals(b);
		}
		else
		{
			return a == b;
		}
	}

	// Key comparison for string from different arena
	template <typename OtherTag, uint32_t OtherSize>
	bool
	keys_equal(const K &a, const string<OtherTag, OtherSize> &b) const
	{
		if constexpr (is_string<K>::value)
		{
			return a.equals(b);
		}
		else
		{
			static_assert(std::is_integral<K>::value, "Key must be an integer or string type");
			return false; // Cannot compare non-string K with string
		}
	}

	// Initialize with initial capacity
	void
	init(uint32_t initial_capacity = 16)
	{
		if (entries)
			return;

		// Round up to power of 2
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

	// Internal insert (used during growth)
	V *
	insert_internal(const K &key, uint32_t hash, const V &value)
	{
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state != Entry::OCCUPIED)
			{
				entry.key = key;
				entry.value = value;
				entry.hash = hash;
				entry.state = Entry::OCCUPIED;
				size++;
				return &entry.value;
			}

			if (entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.value = value;
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	// Grow the table
	void
	grow()
	{
		uint32_t old_capacity = capacity;
		Entry	*old_entries = entries;

		capacity = old_capacity * 2;
		entries = (Entry *)Arena<ArenaTag>::alloc(capacity * sizeof(Entry));
		memset(entries, 0, capacity * sizeof(Entry));

		uint32_t old_size = size;
		size = 0;
		tombstones = 0;

		for (uint32_t i = 0; i < old_capacity; i++)
		{
			if (old_entries[i].state == Entry::OCCUPIED)
			{
				insert_internal(old_entries[i].key, old_entries[i].hash, old_entries[i].value);
			}
		}

		// Reclaim old backing array
		Arena<ArenaTag>::reclaim(old_entries, old_capacity * sizeof(Entry));
	}

	// Insert or update
	V *
	insert(const K &key, const V &value)
	{
		if (!entries)
			init();

		if ((size + tombstones) * 4 >= capacity * 3)
			grow();

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;
		uint32_t first_deleted = (uint32_t)-1;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state == Entry::EMPTY)
			{
				if (first_deleted != (uint32_t)-1)
				{
					Entry &deleted_entry = entries[first_deleted];
					deleted_entry.key = key;
					deleted_entry.value = value;
					deleted_entry.hash = hash;
					deleted_entry.state = Entry::OCCUPIED;
					tombstones--;
					size++;
					return &deleted_entry.value;
				}
				else
				{
					entry.key = key;
					entry.value = value;
					entry.hash = hash;
					entry.state = Entry::OCCUPIED;
					size++;
					return &entry.value;
				}
			}

			if (entry.state == Entry::DELETED)
			{
				if (first_deleted == (uint32_t)-1)
					first_deleted = idx;
			}
			else if (entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.value = value;
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	// Insert with string key from different arena
	template <typename OtherTag, uint32_t OtherSize>
	V *
	insert(const string<OtherTag, OtherSize> &key, const V &value)
	{
		static_assert(is_string<K>::value, "insert(string) can only be used with string keys");
		if (!entries)
			init();

		if ((size + tombstones) * 4 >= capacity * 3)
			grow();

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;
		uint32_t first_deleted = (uint32_t)-1;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state == Entry::EMPTY)
			{
				if (first_deleted != (uint32_t)-1)
				{
					Entry &deleted_entry = entries[first_deleted];
					deleted_entry.key.set(key.c_str());
					deleted_entry.value = value;
					deleted_entry.hash = hash;
					deleted_entry.state = Entry::OCCUPIED;
					tombstones--;
					size++;
					return &deleted_entry.value;
				}
				else
				{
					entry.key.set(key.c_str());
					entry.value = value;
					entry.hash = hash;
					entry.state = Entry::OCCUPIED;
					size++;
					return &entry.value;
				}
			}

			if (entry.state == Entry::DELETED)
			{
				if (first_deleted == (uint32_t)-1)
					first_deleted = idx;
			}
			else if (entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.value = value;
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	// Lookup
	V *
	get(const K &key)
	{
		if (!entries || size == 0)
			return nullptr;

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state == Entry::EMPTY)
				return nullptr;

			if (entry.state == Entry::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key))
			{
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	// Lookup with string key from different arena
	template <typename OtherTag, uint32_t OtherSize>
	V *
	get(const string<OtherTag, OtherSize> &key)
	{
		static_assert(is_string<K>::value, "get(string) can only be used with string keys");
		if (!entries || size == 0)
			return nullptr;

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state == Entry::EMPTY)
				return nullptr;

			if (entry.state == Entry::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key))
			{
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	// Const lookup
	const V *
	get(const K &key) const
	{
		return const_cast<hash_map *>(this)->get(key);
	}

	// Const lookup with string key from different arena
	template <typename OtherTag, uint32_t OtherSize>
	const V *
	get(const string<OtherTag, OtherSize> &key) const
	{
		return const_cast<hash_map *>(this)->get(key);
	}

	// Check if key exists
	bool
	contains(const K &key) const
	{
		return const_cast<hash_map *>(this)->get(key) != nullptr;
	}

	// Check if string key from different arena exists
	template <typename OtherTag, uint32_t OtherSize>
	bool
	contains(const string<OtherTag, OtherSize> &key) const
	{
		return const_cast<hash_map *>(this)->get(key) != nullptr;
	}

	// Delete entry
	bool
	remove(const K &key)
	{
		if (!entries || size == 0)
			return false;

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state == Entry::EMPTY)
				return false;

			if (entry.state == Entry::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.state = Entry::DELETED;
				size--;
				tombstones++;
				return true;
			}

			idx = (idx + 1) & mask;
		}
	}

	// Delete entry with string key from different arena
	template <typename OtherTag, uint32_t OtherSize>
	bool
	remove(const string<OtherTag, OtherSize> &key)
	{
		static_assert(is_string<K>::value, "remove(string) can only be used with string keys");
		if (!entries || size == 0)
			return false;

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state == Entry::EMPTY)
				return false;

			if (entry.state == Entry::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.state = Entry::DELETED;
				size--;
				tombstones++;
				return true;
			}

			idx = (idx + 1) & mask;
		}
	}

	// Clear all entries
	void
	clear()
	{
		if (entries)
			memset(entries, 0, capacity * sizeof(Entry));
		size = 0;
		tombstones = 0;
	}

	// Collect all key-value pairs into an array
	template <typename ArrayTag = ArenaTag>
	void
	collect(array<pair<K, V>, ArrayTag> *out) const
	{
		out->clear();
		if (!entries || size == 0)
			return;

		out->reserve(size);

		for (uint32_t i = 0; i < capacity; i++)
		{
			const Entry &entry = entries[i];
			if (entry.state == Entry::OCCUPIED)
			{
				pair<K, V> p = {entry.key, entry.value};
				out->push(p);
			}
		}
	}

	// Iterator support for range-based for loops
	struct iterator
	{
		Entry	*entries;
		uint32_t capacity;
		uint32_t idx;

		iterator(Entry *e, uint32_t cap, uint32_t i) : entries(e), capacity(cap), idx(i)
		{
			// Skip to first occupied entry
			while (idx < capacity && entries[idx].state != Entry::OCCUPIED)
				idx++;
		}

		pair<K &, V &>
		operator*()
		{
			return {entries[idx].key, entries[idx].value};
		}

		iterator &
		operator++()
		{
			idx++;
			while (idx < capacity && entries[idx].state != Entry::OCCUPIED)
				idx++;
			return *this;
		}

		bool
		operator!=(const iterator &other) const
		{
			return idx != other.idx;
		}
	};

	iterator
	begin()
	{
		return iterator(entries, capacity, 0);
	}
	iterator
	end()
	{
		return iterator(entries, capacity, capacity);
	}

	// Convenience operator[] for insertion/access
	V &
	operator[](const K &key)
	{
		V *val = get(key);
		if (val)
			return *val;

		// Insert default value
		V default_val = {};
		return *insert(key, default_val);
	}

	// Operator[] for string key from different arena
	template <typename OtherTag, uint32_t OtherSize>
	V &
	operator[](const string<OtherTag, OtherSize> &key)
	{
		static_assert(is_string<K>::value, "operator[](string) can only be used with string keys");
		V *val = get(key);
		if (val)
			return *val;

		// Insert default value
		V default_val = {};
		return *insert(key, default_val);
	}

	bool
	empty() const
	{
		return size == 0;
	}

	// Static factory for heap allocation
	static hash_map *
	create(uint32_t initial_capacity = 16)
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

// HashSet using HashMap
template <typename K, typename ArenaTag = global_arena> struct hash_set
{
	hash_map<K, uint8_t, ArenaTag> map;

	void
	init(uint32_t initial_capacity = 16)
	{
		map.init(initial_capacity);
	}

	bool
	insert(const K &key)
	{
		if (map.contains(key))
			return false;
		map.insert(key, 1);
		return true;
	}

	// Insert with string key from different arena
	template <typename OtherTag, uint32_t OtherSize>
	bool
	insert(const string<OtherTag, OtherSize> &key)
	{
		static_assert(hash_map<K, uint8_t, ArenaTag>::template is_string<K>::value,
					  "insert(string) can only be used with string keys");
		if (map.contains(key))
			return false;
		map.insert(key, 1);
		return true;
	}

	bool
	contains(const K &key) const
	{
		return map.contains(key);
	}

	// Contains with string key from different arena
	template <typename OtherTag, uint32_t OtherSize>
	bool
	contains(const string<OtherTag, OtherSize> &key) const
	{
		static_assert(hash_map<K, uint8_t, ArenaTag>::template is_string<K>::value,
					  "contains(string) can only be used with string keys");
		return map.contains(key);
	}

	bool
	remove(const K &key)
	{
		return map.remove(key);
	}

	// Remove with string key from different arena
	template <typename OtherTag, uint32_t OtherSize>
	bool
	remove(const string<OtherTag, OtherSize> &key)
	{
		static_assert(hash_map<K, uint8_t, ArenaTag>::template is_string<K>::value,
					  "remove(string) can only be used with string keys");
		return map.remove(key);
	}

	void
	clear()
	{
		map.clear();
	}

	uint32_t
	size() const
	{
		return map.size;
	}
	bool
	empty() const
	{
		return map.empty();
	}

	// Static factory
	static hash_set *
	create(uint32_t initial_capacity = 16)
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
