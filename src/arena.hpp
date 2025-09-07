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
		assert(!(!ptr || size < sizeof(free_block)));

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

template <typename ArenaTag, uint32_t InitialSize> struct string;

template <typename T, typename ArenaTag = global_arena, uint32_t InitialSize = 8> struct array
{
	template <typename U> struct is_string : std::false_type
	{
	};
	template <typename Tag, uint32_t Size> struct is_string<string<Tag, Size>> : std::true_type
	{
	};

	T		*data = nullptr;
	uint32_t size = 0;
	uint32_t capacity = 0;

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
			data = (T *)arena<ArenaTag>::alloc(capacity * sizeof(T));
			return;
		}

		T		*old_data = data;
		uint32_t old_capacity = capacity;

		uint32_t new_cap = capacity * 2;
		if (new_cap < min_capacity)
		{
			new_cap = min_capacity;
		}

		T *new_data = (T *)arena<ArenaTag>::alloc(new_cap * sizeof(T));
		memcpy(new_data, data, size * sizeof(T));

		data = new_data;
		capacity = new_cap;

		arena<ArenaTag>::reclaim(old_data, old_capacity * sizeof(T));
	}

	uint32_t
	push(const T &value)
	{
		reserve(size + 1);
		data[size] = value;
		return size++;
	}

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
	pop_back()
	{
		assert(size > 0);
		--size;
	}

	T
	pop_value()
	{
		assert(size > 0);
		return data[--size];
	}

	void
	clear()
	{
		size = 0;
	}

	void
	resize(uint32_t new_size)
	{
		uint32_t old_size = size;
		reserve(new_size);

		if (new_size > old_size && data)
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

		T		*old_data = data;
		uint32_t old_capacity = capacity;

		data = (T *)arena<ArenaTag>::alloc(size * sizeof(T));
		memcpy(data, old_data, size * sizeof(T));
		capacity = size;

		arena<ArenaTag>::reclaim(old_data, old_capacity * sizeof(T));
	}

	template <typename OtherTag>
	void
	set(const array<T, OtherTag> &other)
	{
		clear();
		reserve(other.size);

		if (other.size > 0 && other.data)
		{
			memcpy(data, other.data, other.size * sizeof(T));
			size = other.size;
		}
	}

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

	T &
	operator[](uint32_t index)
	{
		assert(index < size);
		return data[index];
	}

	const T &
	operator[](uint32_t index) const
	{
		assert(index < size);
		return data[index];
	}

	T *
	back()
	{
		return size > 0 ? &data[size - 1] : nullptr;
	}

	const T *
	back() const
	{
		return size > 0 ? &data[size - 1] : nullptr;
	}

	T *
	front()
	{
		return size > 0 ? &data[0] : nullptr;
	}

	const T *
	front() const
	{
		return size > 0 ? &data[0] : nullptr;
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

	static array *
	create()
	{
		auto *arr = (array *)arena<ArenaTag>::alloc(sizeof(array));
		arr->data = nullptr;
		arr->size = 0;
		arr->capacity = 0;
		return arr;
	}
};

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

template <typename T>
inline uint32_t
hash_int(T x)
{
	using U = typename std::make_unsigned<T>::type;
	U ux = static_cast<U>(x);

	if constexpr (sizeof(T) <= 4)
	{
		return hash_32(static_cast<uint32_t>(ux));
	}
	else
	{
		return static_cast<uint32_t>(hash_64(static_cast<uint64_t>(ux)));
	}
}

template <typename ArenaTag = global_arena, uint32_t InitialSize = 16> struct string
{
	char			*data = nullptr;
	uint32_t		 size = 0;
	uint32_t		 capacity = 0;
	mutable uint32_t cached_hash = 0;

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
		{
			return;
		}

		if (!data)
		{
			capacity = min_capacity > InitialSize ? min_capacity : InitialSize;
			data = (char *)arena<ArenaTag>::alloc(capacity);
			return;
		}

		char	*old_data = data;
		uint32_t old_capacity = capacity;

		uint32_t new_cap = capacity * 2;
		if (new_cap < min_capacity)
		{
			new_cap = min_capacity;
		}

		char *new_data = (char *)arena<ArenaTag>::alloc(new_cap);
		memcpy(new_data, data, size);

		data = new_data;
		capacity = new_cap;
		cached_hash = 0;

		arena<ArenaTag>::reclaim(old_data, old_capacity);
	}

	void
	set(const char *cstr, size_t len = 0)
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
		size = actual_len;
		cached_hash = 0;
	}

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
	append(const char *cstr, size_t s = 0)
	{
		if (size > 0 && data[size - 1] == '\0')
		{
			size--;
		}

		size_t len = s != 0 ? s : strlen(cstr);
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
		{
			return;
		}

		if (size > 0 && data[size - 1] == '\0')
		{
			size--;
		}

		size_t copy_len = other.size;
		if (other.data[other.size - 1] == '\0')
		{
			copy_len--;
		}

		reserve(size + copy_len + 1);
		memcpy(data + size, other.data, copy_len);
		size += copy_len;
		data[size++] = '\0';
		cached_hash = 0;
	}

	template <typename OtherTag, uint32_t OtherSize>
	void
	append(const string<OtherTag, OtherSize> &other)
	{
		if (other.empty())
		{
			return;
		}

		if (size > 0 && data[size - 1] == '\0')
		{
			size--;
		}

		size_t copy_len = other.size;
		if (other.data[other.size - 1] == '\0')
		{
			copy_len--;
		}

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
		{
			memset(data, 0, size);
		}
		size = 0;
		cached_hash = 0;
	}

	const char *
	c_str() const
	{
		if (!data || size == 0)
		{
			return "";
		}

		// hacky
		if (data[size - 1] != '\0')
		{
			const_cast<string *>(this)->reserve(size + 1);
			const_cast<string *>(this)->data[size] = '\0';
			const_cast<string *>(this)->size++;
		}
		return data;
	}

	/* For use in hash maps  */
	uint32_t
	hash() const
	{
		if (cached_hash != 0)
		{
			return cached_hash;
		}

		if (!data || size == 0)
		{
			cached_hash = 1;
			return cached_hash;
		}

		uint32_t h = 2166136261u;
		for (uint32_t i = 0; i < size && data[i] != '\0'; i++)
		{
			h ^= (uint8_t)data[i];
			h *= 16777619u;
		}

		cached_hash = h ? h : 1;
		return cached_hash;
	}

	uint32_t
	length() const
	{
		if (!data || size == 0)
		{
			return 0;
		}

		if (data[size - 1] == '\0')
		{
			return size - 1;
		}
		return size;
	}

	bool
	equals(const char *cstr) const
	{
		if (!data || !cstr)
		{
			return !data && !cstr;
		}
		return strcmp(c_str(), cstr) == 0;
	}

	bool
	equals(const string &other) const
	{
		if (!data || !other.data)
		{
			return !data && !other.data;
		}

		if (hash() != other.hash())
		{
			return false;
		}

		return strcmp(c_str(), other.c_str()) == 0;
	}

	bool
	operator==(const string &other) const
	{
		return equals(other);
	}
	bool
	operator==(const char *cstr) const
	{
		return equals(cstr);
	}
	bool
	operator!=(const string &other) const
	{
		return !equals(other);
	}
	bool
	operator!=(const char *cstr) const
	{
		return !equals(cstr);
	}

	string &
	operator+=(const char *cstr)
	{
		append(cstr);
		return *this;
	}

	string &
	operator+=(const string &other)
	{
		append(other);
		return *this;
	}

	bool
	operator<(const string &other) const
	{
		return strcmp(c_str(), other.c_str()) < 0;
	}

	template <typename OtherTag, uint32_t OtherSize>
	bool
	equals(const string<OtherTag, OtherSize> &other) const
	{
		if (!data || !other.data)
		{
			return !data && !other.data;
		}

		if (hash() != other.hash())
		{
			return false;
		}

		return strcmp(c_str(), other.c_str()) == 0;
	}

	template <typename ArrayTag = ArenaTag>
	void
	split(char delimiter, array<string<ArenaTag>, ArrayTag> *result) const
	{
		result->clear();

		if (!data || size == 0)
		{
			return;
		}

		const char *start = data;
		const char *end = data;
		const char *limit = data + length();

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

	template <typename OtherTag, typename ArrayTag>
	void
	split(char delimiter, array<string<OtherTag>, ArrayTag> *result) const
	{
		result->clear();

		if (!data || size == 0)
		{
			return;
		}

		const char *start = data;
		const char *end = data;
		const char *limit = data + length();

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

	char &
	operator[](uint32_t index)
	{
		assert(index < size);
		return data[index];
	}

	const char &
	operator[](uint32_t index) const
	{
		assert(index < size);
		return data[index];
	}

	operator const char *() const
	{
		return c_str();
	}

	static string *
	create()
	{
		auto *str = (string *)arena<ArenaTag>::alloc(sizeof(string));
		str->data = nullptr;
		str->size = 0;
		str->capacity = 0;
		str->cached_hash = 0;
		return str;
	}

	static string
	make(const char *cstr)
	{
		string s;
		s.set(cstr);
		return s;
	}

	template <typename OtherTag, uint32_t OtherSize>
	static string
	make(const string<OtherTag, OtherSize> &other)
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
 * Open-addressed hash map
 * For keys, only supports primtivites and and arena string/cstrings as special cases
 *
 */

// Add this type alias at global scope:

enum hash_slot_state : uint8_t
{
	EMPTY = 0,
	OCCUPIED = 1,
	DELETED = 2
};

template <typename K, typename V, typename ArenaTag = global_arena> struct hash_map
{
	struct Entry
	{

		K		   key;
		V		   value;
		uint32_t   hash;
		hash_slot_state state;
	};

	Entry	*entries = nullptr;
	uint32_t capacity = 0;
	uint32_t size = 0;
	uint32_t tombstones = 0;

	template <typename T> struct is_string : std::false_type
	{
	};
	template <typename Tag, uint32_t Size> struct is_string<string<Tag, Size>> : std::true_type
	{
	};

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

	template <typename OtherTag, uint32_t OtherSize>
	uint32_t
	hash_key(const string<OtherTag, OtherSize> &key) const
	{
		return key.hash();
	}

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

	uint32_t
	hash_cstr(const char *cstr) const
	{
		if (!cstr || !*cstr)
		{
			return 1;
		}

		uint32_t h = 2166136261u;
		while (*cstr)
		{
			h ^= (uint8_t)*cstr++;
			h *= 16777619u;
		}

		return h ? h : 1;
	}

	template <typename U = K>
	typename std::enable_if<is_string<U>::value, V *>::type
	get(const char *key)
	{
		if (!entries || size == 0 || !key)
		{
			return nullptr;
		}

		uint32_t hash = hash_cstr(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state == hash_slot_state::EMPTY)
			{
				return nullptr;
			}

			if (entry.state == hash_slot_state::OCCUPIED && entry.hash == hash && entry.key.equals(key))
			{
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}
	/* To allow map.get("cstring") */
	template <typename U = K>
	typename std::enable_if<is_string<U>::value, const V *>::type
	get(const char *key) const
	{
		return const_cast<hash_map *>(this)->template get<U>(key);
	}

	template <typename U = K>
	typename std::enable_if<is_string<U>::value, V *>::type
	insert(const char *key, const V &value)
	{
		if (!key)
		{
			return nullptr;
		}

		if (!entries)
		{
			init();
		}

		if ((size + tombstones) * 4 >= capacity * 3)
		{
			grow();
		}

		uint32_t hash = hash_cstr(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;
		uint32_t first_deleted = (uint32_t)-1;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state == hash_slot_state::EMPTY)
			{
				if (first_deleted != (uint32_t)-1)
				{
					Entry &deleted_entry = entries[first_deleted];
					deleted_entry.key.set(key);
					deleted_entry.value = value;
					deleted_entry.hash = hash;
					deleted_entry.state = hash_slot_state::OCCUPIED;
					tombstones--;
					size++;
					return &deleted_entry.value;
				}
				else
				{
					entry.key.set(key);
					entry.value = value;
					entry.hash = hash;
					entry.state = hash_slot_state::OCCUPIED;
					size++;
					return &entry.value;
				}
			}

			if (entry.state == hash_slot_state::DELETED)
			{
				if (first_deleted == (uint32_t)-1)
				{
					first_deleted = idx;
				}
			}
			else if (entry.hash == hash && entry.key.equals(key))
			{
				entry.value = value;
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename U = K>
	typename std::enable_if<is_string<U>::value, bool>::type
	contains(const char *key) const
	{
		return const_cast<hash_map *>(this)->get(key) != nullptr;
	}

	template <typename U = K>
	typename std::enable_if<is_string<U>::value, bool>::type
	remove(const char *key)
	{
		if (!entries || size == 0 || !key)
		{
			return false;
		}

		uint32_t hash = hash_cstr(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state == hash_slot_state::EMPTY)
			{
				return false;
			}

			if (entry.state == hash_slot_state::OCCUPIED && entry.hash == hash && entry.key.equals(key))
			{
				entry.state = hash_slot_state::DELETED;
				size--;
				tombstones++;
				return true;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename U = K>
	typename std::enable_if<is_string<U>::value, V &>::type
	operator[](const char *key)
	{
		V *val = get(key);
		if (val)
		{
			return *val;
		}

		V default_val = {};
		return *insert(key, default_val);
	}

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
			return false;
		}
	}

	void
	init(uint32_t initial_capacity = 16)
	{
		if (entries)
		{
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
		entries = (Entry *)arena<ArenaTag>::alloc(capacity * sizeof(Entry));
		memset(entries, 0, capacity * sizeof(Entry));
		size = 0;
		tombstones = 0;
	}

	V *
	insert_internal(const K &key, uint32_t hash, const V &value)
	{
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state != hash_slot_state::OCCUPIED)
			{
				entry.key = key;
				entry.value = value;
				entry.hash = hash;
				entry.state = hash_slot_state::OCCUPIED;
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

	void
	grow()
	{
		uint32_t old_capacity = capacity;
		Entry	*old_entries = entries;

		capacity = old_capacity * 2;
		entries = (Entry *)arena<ArenaTag>::alloc(capacity * sizeof(Entry));
		memset(entries, 0, capacity * sizeof(Entry));

		uint32_t old_size = size;
		size = 0;
		tombstones = 0;

		for (uint32_t i = 0; i < old_capacity; i++)
		{
			if (old_entries[i].state == hash_slot_state::OCCUPIED)
			{
				insert_internal(old_entries[i].key, old_entries[i].hash, old_entries[i].value);
			}
		}

		arena<ArenaTag>::reclaim(old_entries, old_capacity * sizeof(Entry));
	}

	V *
	insert(const K &key, const V &value)
	{
		if (!entries)
		{
			init();
		}

		if ((size + tombstones) * 4 >= capacity * 3)
		{
			grow();
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;
		uint32_t first_deleted = (uint32_t)-1;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state == hash_slot_state::EMPTY)
			{
				if (first_deleted != (uint32_t)-1)
				{
					Entry &deleted_entry = entries[first_deleted];
					deleted_entry.key = key;
					deleted_entry.value = value;
					deleted_entry.hash = hash;
					deleted_entry.state = hash_slot_state::OCCUPIED;
					tombstones--;
					size++;
					return &deleted_entry.value;
				}
				else
				{
					entry.key = key;
					entry.value = value;
					entry.hash = hash;
					entry.state = hash_slot_state::OCCUPIED;
					size++;
					return &entry.value;
				}
			}

			if (entry.state == hash_slot_state::DELETED)
			{
				if (first_deleted == (uint32_t)-1)
				{
					first_deleted = idx;
				}
			}
			else if (entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.value = value;
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename OtherTag, uint32_t OtherSize>
	V *
	insert(const string<OtherTag, OtherSize> &key, const V &value)
	{
		static_assert(is_string<K>::value, "insert(string) can only be used with string keys");
		if (!entries)
		{
			init();
		}

		if ((size + tombstones) * 4 >= capacity * 3)
		{
			grow();
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;
		uint32_t first_deleted = (uint32_t)-1;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state == hash_slot_state::EMPTY)
			{
				if (first_deleted != (uint32_t)-1)
				{
					Entry &deleted_entry = entries[first_deleted];
					deleted_entry.key.set(key.c_str());
					deleted_entry.value = value;
					deleted_entry.hash = hash;
					deleted_entry.state = hash_slot_state::OCCUPIED;
					tombstones--;
					size++;
					return &deleted_entry.value;
				}
				else
				{
					entry.key.set(key.c_str());
					entry.value = value;
					entry.hash = hash;
					entry.state = hash_slot_state::OCCUPIED;
					size++;
					return &entry.value;
				}
			}

			if (entry.state == hash_slot_state::DELETED)
			{
				if (first_deleted == (uint32_t)-1)
				{
					first_deleted = idx;
				}
			}
			else if (entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.value = value;
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	V *
	get(const K &key)
	{
		if (!entries || size == 0)
		{
			return nullptr;
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state == hash_slot_state::EMPTY)
			{
				return nullptr;
			}

			if (entry.state == hash_slot_state::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key))
			{
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename OtherTag, uint32_t OtherSize>
	V *
	get(const string<OtherTag, OtherSize> &key)
	{
		static_assert(is_string<K>::value, "get(string) can only be used with string keys");
		if (!entries || size == 0)
		{
			return nullptr;
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state == hash_slot_state::EMPTY)
			{
				return nullptr;
			}

			if (entry.state == hash_slot_state::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key))
			{
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	const V *
	get(const K &key) const
	{
		return const_cast<hash_map *>(this)->get(key);
	}

	template <typename OtherTag, uint32_t OtherSize>
	const V *
	get(const string<OtherTag, OtherSize> &key) const
	{
		return const_cast<hash_map *>(this)->get(key);
	}

	bool
	contains(const K &key) const
	{
		return const_cast<hash_map *>(this)->get(key) != nullptr;
	}

	template <typename OtherTag, uint32_t OtherSize>
	bool
	contains(const string<OtherTag, OtherSize> &key) const
	{
		return const_cast<hash_map *>(this)->get(key) != nullptr;
	}

	bool
	remove(const K &key)
	{
		if (!entries || size == 0)
		{
			return false;
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state == hash_slot_state::EMPTY)
			{
				return false;
			}

			if (entry.state == hash_slot_state::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.state = hash_slot_state::DELETED;
				size--;
				tombstones++;
				return true;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename OtherTag, uint32_t OtherSize>
	bool
	remove(const string<OtherTag, OtherSize> &key)
	{
		static_assert(is_string<K>::value, "remove(string) can only be used with string keys");
		if (!entries || size == 0)
		{
			return false;
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state == hash_slot_state::EMPTY)
			{
				return false;
			}

			if (entry.state == hash_slot_state::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.state = hash_slot_state::DELETED;
				size--;
				tombstones++;
				return true;
			}

			idx = (idx + 1) & mask;
		}
	}

	void
	clear()
	{
		if (entries)
		{
			memset(entries, 0, capacity * sizeof(Entry));
		}
		size = 0;
		tombstones = 0;
	}

	template <typename ArrayTag = ArenaTag>
	void
	collect(array<pair<K, V>, ArrayTag> *out) const
	{
		out->clear();
		if (!entries || size == 0)
		{
			return;
		}

		out->reserve(size);

		for (uint32_t i = 0; i < capacity; i++)
		{
			const Entry &entry = entries[i];
			if (entry.state == hash_slot_state::OCCUPIED)
			{
				pair<K, V> p = {entry.key, entry.value};
				out->push(p);
			}
		}
	}

	V &
	operator[](const K &key)
	{
		V *val = get(key);
		if (val)
		{
			return *val;
		}

		V default_val = {};
		return *insert(key, default_val);
	}

	template <typename OtherTag, uint32_t OtherSize>
	V &
	operator[](const string<OtherTag, OtherSize> &key)
	{
		static_assert(is_string<K>::value, "operator[](string) can only be used with string keys");
		V *val = get(key);
		if (val)
		{
			return *val;
		}

		V default_val = {};
		return *insert(key, default_val);
	}

	bool
	empty() const
	{
		return size == 0;
	}

	static hash_map *
	create(uint32_t initial_capacity = 16)
	{
		auto *m = (hash_map *)arena<ArenaTag>::alloc(sizeof(hash_map));
		m->entries = nullptr;
		m->capacity = 0;
		m->size = 0;
		m->tombstones = 0;
		m->init(initial_capacity);
		return m;
	}
};

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
