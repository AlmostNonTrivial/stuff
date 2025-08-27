#pragma once
#include <typeinfo>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct GlobalArena
{
};

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

// Arena with virtual memory backing
template <typename Tag> struct Arena
{
	static inline uint8_t *base = nullptr;
	static inline uint8_t *current = nullptr;
	static inline size_t   reserved_capacity = 0;  // Virtual address space reserved
	static inline size_t   committed_capacity = 0; // Physical memory committed
	static inline size_t   max_capacity = 0;	   // Optional limit
	static inline size_t   initial_commit = 0;	   // Initial commit size

	// Initialize with initial size and optional maximum
	// initial: how much memory to commit right away (0 = on demand)
	// maximum: maximum memory this arena can use (0 = unlimited up to system limits)
	static void
	init(size_t initial = 4 * 1024 * 1024, // 4MB default initial
		 size_t maximum = 0)
	{
		if (base)
			return; // Already initialized

		initial_commit = VirtualMemory::round_to_pages(initial);
		max_capacity = maximum;

		// Reserve virtual address space
		// If there's a max, reserve that. Otherwise reserve something huge.
		reserved_capacity = max_capacity ? max_capacity : (1ULL << 38); // 256GB if no limit

		base = (uint8_t *)VirtualMemory::reserve(reserved_capacity);
		if (!base)
		{
			// Fallback: try smaller reservation
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

		// Commit initial memory if requested
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
		}
	}

	// Allocate memory from arena
	static void *
	alloc(size_t size)
	{
		// Auto-initialize if needed
		if (!base)
		{
			init();
		}

		// Align to 16 bytes
		size_t	  align = 16;
		uintptr_t current_addr = (uintptr_t)current;
		uintptr_t aligned_addr = (current_addr + align - 1) & ~(align - 1);

		uint8_t *aligned = (uint8_t *)aligned_addr;
		uint8_t *next = aligned + size;

		// Check if we need to commit more memory
		if (next > base + committed_capacity)
		{
			size_t needed = next - base;

			// Check against maximum capacity if set
			if (max_capacity > 0 && needed > max_capacity)
			{
				fprintf(stderr, "Arena exhausted: requested %zu, max %zu\n", needed, max_capacity);
				exit(1);
			}

			// Check against reserved capacity
			if (needed > reserved_capacity)
			{
				fprintf(stderr, "Arena exhausted: requested %zu, reserved %zu\n", needed, reserved_capacity);
				exit(1);
			}

			// Calculate how much to commit
			size_t new_committed = VirtualMemory::round_to_pages(needed);

			// Grow by at least 50% of current committed or initial size
			size_t min_growth = committed_capacity > 0 ? committed_capacity + committed_capacity / 2 : initial_commit;
			if (new_committed < min_growth)
			{
				new_committed = min_growth;
			}

			// Respect maximum if set
			if (max_capacity > 0 && new_committed > max_capacity)
			{
				new_committed = max_capacity;
			}

			// Don't exceed reservation
			if (new_committed > reserved_capacity)
			{
				new_committed = reserved_capacity;
			}

			// Commit the new range
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
	}

	// Reset and decommit memory back to initial size
	static void
	reset_and_decommit()
	{
		current = base;

		if (committed_capacity > initial_commit)
		{
			// Decommit everything beyond initial
			VirtualMemory::decommit(base + initial_commit, committed_capacity - initial_commit);
			committed_capacity = initial_commit;
		}

		if (base && committed_capacity > 0)
		{
			memset(base, 0, committed_capacity);
		}
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

	// Get stats
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
void
print_stats()
{
	Arena<Tag>::print_stats();
}
} // namespace arena

template <typename T, typename ArenaTag = GlobalArena, size_t InitialSize = 8> struct Array
{
	T	  *data = nullptr;
	size_t size = 0;
	size_t capacity = 0;
};

template <typename T, typename Tag, size_t InitSize>
T *
array_push(Array<T, Tag, InitSize> *arr, const T &value)
{
	// Lazy init on first push
	if (!arr->data)
	{
		arr->capacity = InitSize;
		arr->data = (T *)Arena<Tag>::alloc(arr->capacity * sizeof(T));
	}

	// Grow if needed
	if (arr->size >= arr->capacity)
	{
		size_t new_cap = arr->capacity * 2;
		T	  *new_data = (T *)Arena<Tag>::alloc(new_cap * sizeof(T));
		memcpy(new_data, arr->data, arr->size * sizeof(T));

		arr->data = new_data;
		arr->capacity = new_cap;
	}

	arr->data[arr->size] = value;
	return &arr->data[arr->size++];
}
template <typename T, typename Tag>
void
array_clear(Array<T, Tag> *arr)
{
	memset(arr->data, 0, sizeof(T) * arr->size);
	arr->size = 0;
}

template <typename T, typename Tag = GlobalArena, size_t InitSize = 0>
Array<T, Tag, InitSize> *
array_create()
{
	auto *arr = (Array<T, Tag, InitSize> *)Arena<Tag>::alloc(sizeof(Array<T, Tag, InitSize>));
	arr->data = nullptr;
	arr->size = 0;
	arr->capacity = 0;
	return arr;
}
// Hash any integer type
template <typename T>
inline size_t
hash_int(T key)
{
	size_t h = (size_t)key;
	h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
	h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
	return h ^ (h >> 31);
}

// Simple entry with state
template <typename K, typename V> struct HashEntry
{
	K		key;
	V		value;
	uint8_t state = 0; // 0=empty, 1=occupied, 2=deleted
};

template <typename K, typename V, typename Tag = GlobalArena> struct HashMap
{
	HashEntry<K, V> *data = nullptr;
	size_t			 capacity = 0;
	size_t			 count = 0;
	size_t			 cursor = 0;
};

// HashSet just reuses HashMap with dummy value
template <typename K, typename Tag = GlobalArena> using HashSet = HashMap<K, uint8_t, Tag>;

// Get value by key
template <typename K, typename V, typename Tag>
V *
hashmap_get(HashMap<K, V, Tag> *map, K key)
{
	if (map->capacity == 0)
		return nullptr;

	size_t idx = hash_int(key) & (map->capacity - 1);
	while (map->data[idx].state != 0)
	{ // Keep going until empty slot
		if (map->data[idx].state == 1 && map->data[idx].key == key)
		{
			return &map->data[idx].value;
		}
		idx = (idx + 1) & (map->capacity - 1);
	}
	return nullptr;
}

// Delete by key
template <typename K, typename V, typename Tag>
bool
hashmap_delete(HashMap<K, V, Tag> *map, K key)
{
	if (map->capacity == 0)
		return false;

	size_t idx = hash_int(key) & (map->capacity - 1);
	while (map->data[idx].state != 0)
	{
		if (map->data[idx].state == 1 && map->data[idx].key == key)
		{
			map->data[idx].state = 2; // Mark as deleted (tombstone)
			map->count--;
			return true;
		}
		idx = (idx + 1) & (map->capacity - 1);
	}
	return false;
}

// Fixed insert with rehashing
template <typename K, typename V, typename Tag>
V *
hashmap_insert(HashMap<K, V, Tag> *map, K key, V value)
{
	// Initialize or grow at 70% load
	if (map->capacity == 0 || map->count >= map->capacity * 0.7)
	{
		size_t new_cap = map->capacity ? map->capacity * 2 : 16;

		// Keep old data
		HashEntry<K, V> *old_data = map->data;
		size_t			 old_cap = map->capacity;

		// Create new array
		map->data = (HashEntry<K, V> *)Arena<Tag>::alloc(new_cap * sizeof(HashEntry<K, V>));
		map->capacity = new_cap;
		memset(map->data, 0, new_cap * sizeof(HashEntry<K, V>));

		// Rehash old entries
		map->count = 0;
		for (size_t i = 0; i < old_cap; i++)
		{
			if (old_data[i].state == 1)
			{
				hashmap_insert(map, old_data[i].key, old_data[i].value);
			}
		}
	}

	size_t idx = hash_int(key) & (map->capacity - 1);
	while (map->data[idx].state == 1)
	{
		if (map->data[idx].key == key)
		{
			map->data[idx].value = value;
			return &map->data[idx].value;
		}
		idx = (idx + 1) & (map->capacity - 1);
	}

	map->data[idx] = {key, value, 1};
	map->count++;
	return &map->data[idx].value;
}

template <typename K, typename V, typename Tag>
HashEntry<K, V> *
hashmap_next(HashMap<K, V, Tag> *map)
{
	if (!map->data)
		return nullptr;

	while (map->cursor < map->capacity)
	{
		HashEntry<K, V> *entry = &map->data[map->cursor++];
		if (entry->state == 1)
		{ // Found occupied entry
			return entry;
		}
	}
	return nullptr;
}

template <typename K, typename V, typename Tag>
void
hashmap_clear(HashMap<K, V, Tag> *map)
{
	memset(map->data, 0, sizeof(HashEntry<K, V>) * map->count);
	map->count = 0;
}

// HashSet helpers
template <typename K, typename Tag>
bool
hashset_insert(HashSet<K, Tag> *set, K key)
{
	void *existing = hashmap_get(set, key);
	if (existing != nullptr)
		return false;
	hashmap_insert(set, key, uint8_t(0));
	return true;
}

template <typename K, typename Tag>
bool
hashset_contains(HashSet<K, Tag> *set, K key)
{
	return hashmap_get(set, key) != nullptr;
}

template <typename K, typename Tag>
bool
hashset_delete(HashSet<K, Tag> *set, K key)
{
	return hashmap_delete(set, key);
}

// HashSet versions just delegate
template <typename K, typename Tag>
void
hashset_clear(HashSet<K, Tag> *set)
{
	hashmap_clear(set);
}

// String is just an array of chars
template <typename Tag = GlobalArena, size_t InitSize = 64> using String = Array<char, Tag, InitSize>;

// Copy a C string
template <typename Tag, size_t InitSize>
void
string_set(String<Tag, InitSize> *s, const char *cstr)
{
	s->size = 0;
	while (*cstr)
	{
		array_push(s, *cstr++);
	}
	array_push(s, '\0');
}

// Append to string
template <typename Tag, size_t InitSize>
void
string_append(String<Tag, InitSize> *s, const char *cstr)
{
	if (s->size > 0 && s->data[s->size - 1] == '\0')
	{
		s->size--; // Remove old null terminator
	}
	while (*cstr)
	{
		array_push(s, *cstr++);
	}
	array_push(s, '\0');
}

// Hash a string (for use with HashMap)
inline size_t
hash_string(const char *str)
{
	size_t h = 0;
	while (*str)
	{
		h = h * 31 + (unsigned char)*str++;
	}
	return h;
}
