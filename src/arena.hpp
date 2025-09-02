
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
struct VirtualMemory {
  static void *reserve(size_t size) {
#ifdef _WIN32
    return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE);
#else
    void *ptr = mmap(nullptr, size, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return (ptr == MAP_FAILED) ? nullptr : ptr;
#endif
  }

  static bool commit(void *addr, size_t size) {
#ifdef _WIN32
    return VirtualAlloc(addr, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#else
    return mprotect(addr, size, PROT_READ | PROT_WRITE) == 0;
#endif
  }

  static void decommit(void *addr, size_t size) {
#ifdef _WIN32
    VirtualFree(addr, size, MEM_DECOMMIT);
#else
    madvise(addr, size, MADV_DONTNEED);
    mprotect(addr, size, PROT_NONE);
#endif
  }

  static void release(void *addr, size_t size) {
#ifdef _WIN32
    (void)size; // Windows doesn't need size for MEM_RELEASE
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


struct global_arena {};

// Arena with virtual memory backing and automatic reclamation
template <typename Tag> struct Arena {
  static inline uint8_t *base = nullptr;
  static inline uint8_t *current = nullptr;
  static inline size_t reserved_capacity = 0;
  static inline size_t committed_capacity = 0;
  static inline size_t max_capacity = 0;
  static inline size_t initial_commit = 0;

  // Freelist management
  struct FreeBlock {
    FreeBlock *next;
    size_t size;
  };

  // Freelists for different size classes (powers of 2)
  // freelists[i] holds blocks of size [2^i, 2^(i+1))
  static inline FreeBlock *freelists[32] = {};
  static inline size_t reclaimed_bytes = 0;
  static inline size_t reused_bytes = 0;

  // Get size class for a given size (which freelist bucket to use)
  static int get_size_class(size_t size) {
    if (size == 0)
      return 0;

    // Find highest bit set (essentially log2)
    int cls = 0;
    size_t s = size - 1;
    if (s >= (1ULL << 16)) {
      cls += 16;
      s >>= 16;
    }
    if (s >= (1ULL << 8)) {
      cls += 8;
      s >>= 8;
    }
    if (s >= (1ULL << 4)) {
      cls += 4;
      s >>= 4;
    }
    if (s >= (1ULL << 2)) {
      cls += 2;
      s >>= 2;
    }
    if (s >= (1ULL << 1)) {
      cls += 1;
    }

    return cls < 31 ? cls : 31;
  }

  // Initialize with initial size and optional maximum
  static void init(size_t initial = 4 * 1024 * 1024, size_t maximum = 0) {
    if (base)
      return;

    initial_commit = VirtualMemory::round_to_pages(initial);
    max_capacity = maximum;

    reserved_capacity = max_capacity ? max_capacity : (1ULL << 38);

    base = (uint8_t *)VirtualMemory::reserve(reserved_capacity);
    if (!base) {
      reserved_capacity = 1ULL << 33;
      base = (uint8_t *)VirtualMemory::reserve(reserved_capacity);
      if (!base) {
        fprintf(stderr, "Failed to reserve virtual memory\n");
        exit(1);
      }
    }

    current = base;
    committed_capacity = 0;

    if (initial_commit > 0) {
      if (!VirtualMemory::commit(base, initial_commit)) {
        fprintf(stderr, "Failed to commit initial memory: %zu bytes\n",
                initial_commit);
        VirtualMemory::release(base, reserved_capacity);
        base = nullptr;
        exit(1);
      }
      committed_capacity = initial_commit;
    }

    // Clear freelists
    for (int i = 0; i < 32; i++) {
      freelists[i] = nullptr;
    }
    reclaimed_bytes = 0;
    reused_bytes = 0;
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

      for (int i = 0; i < 32; i++) {
        freelists[i] = nullptr;
      }
      reclaimed_bytes = 0;
      reused_bytes = 0;
    }
  }

  // Reclaim memory for reuse (called automatically by containers)
  static void reclaim(void *ptr, size_t size) {
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
  static void *try_alloc_from_freelist(size_t size) {
    int size_class = get_size_class(size);

    // Search in this size class and larger ones
    for (int cls = size_class; cls < 32; cls++) {
      FreeBlock **prev_ptr = &freelists[cls];
      FreeBlock *block = freelists[cls];

      while (block) {
        if (block->size >= size) {
          // Remove from freelist
          *prev_ptr = block->next;

          // If block is significantly larger, split it
          size_t remaining = block->size - size;
          if (remaining >= sizeof(FreeBlock) && remaining >= 64) {
            // Put remainder back in appropriate freelist
            uint8_t *remainder_addr = ((uint8_t *)block) + size;
            reclaim(remainder_addr, remaining);
          } else {
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
  static void *alloc(size_t size) {
    if (!base) {
      init();
    }

    // Try freelists first
    void *recycled = try_alloc_from_freelist(size);
    if (recycled) {
      return recycled;
    }

    // Normal allocation path
    size_t align = 8;
    uintptr_t current_addr = (uintptr_t)current;
    uintptr_t aligned_addr = (current_addr + align - 1) & ~(align - 1);

    uint8_t *aligned = (uint8_t *)aligned_addr;
    uint8_t *next = aligned + size;

    // Check if we need to commit more memory
    if (next > base + committed_capacity) {
      size_t needed = next - base;

      if (max_capacity > 0 && needed > max_capacity) {
        fprintf(stderr, "Arena exhausted: requested %zu, max %zu\n", needed,
                max_capacity);
        exit(1);
      }

      if (needed > reserved_capacity) {
        fprintf(stderr, "Arena exhausted: requested %zu, reserved %zu\n",
                needed, reserved_capacity);
        exit(1);
      }

      size_t new_committed = VirtualMemory::round_to_pages(needed);
      size_t min_growth = committed_capacity > 0
                              ? committed_capacity + committed_capacity / 2
                              : initial_commit;
      if (new_committed < min_growth) {
        new_committed = min_growth;
      }

      if (max_capacity > 0 && new_committed > max_capacity) {
        new_committed = max_capacity;
      }

      if (new_committed > reserved_capacity) {
        new_committed = reserved_capacity;
      }

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

  // Add to arena.hpp after the Arena struct definition
  // Reset arena (keep memory committed)
  static void reset() {
    current = base;
    if (base && committed_capacity > 0) {
      memset(base, 0, committed_capacity);
    }

    // Clear freelists
    for (int i = 0; i < 32; i++) {
      freelists[i] = nullptr;
    }
    reclaimed_bytes = 0;
    reused_bytes = 0;
  }

  // Reset and decommit memory back to initial size
  static void reset_and_decommit() {
    current = base;

    if (committed_capacity > initial_commit) {
      VirtualMemory::decommit(base + initial_commit,
                              committed_capacity - initial_commit);
      committed_capacity = initial_commit;
    }

    if (base && committed_capacity > 0) {
      memset(base, 0, committed_capacity);
    }

    // Clear freelists
    for (int i = 0; i < 32; i++) {
      freelists[i] = nullptr;
    }
    reclaimed_bytes = 0;
    reused_bytes = 0;
  }

  // Query functions
  static size_t used() { return current - base; }
  static size_t committed() { return committed_capacity; }
  static size_t reserved() { return reserved_capacity; }
  static size_t reclaimed() { return reclaimed_bytes; }
  static size_t reused() { return reused_bytes; }

  // Get freelist stats
  static size_t freelist_bytes() {
    size_t total = 0;
    for (int i = 0; i < 32; i++) {
      FreeBlock *block = freelists[i];
      while (block) {
        total += block->size;
        block = block->next;
      }
    }
    return total;
  }

  // Print detailed stats
  static void print_stats() {
    printf("Arena<%s>:\n", typeid(Tag).name());
    printf("  Used:      %zu bytes (%.2f MB)\n", used(),
           used() / (1024.0 * 1024.0));
    printf("  Committed: %zu bytes (%.2f MB)\n", committed_capacity,
           committed_capacity / (1024.0 * 1024.0));
    printf("  Reserved:  %zu bytes (%.2f MB)\n", reserved_capacity,
           reserved_capacity / (1024.0 * 1024.0));
    if (max_capacity > 0) {
      printf("  Maximum:   %zu bytes (%.2f MB)\n", max_capacity,
             max_capacity / (1024.0 * 1024.0));
    }
    printf("  Reclaimed: %zu bytes (%.2f MB)\n", reclaimed_bytes,
           reclaimed_bytes / (1024.0 * 1024.0));
    printf("  Reused:    %zu bytes (%.2f MB)\n", reused_bytes,
           reused_bytes / (1024.0 * 1024.0));
    printf("  In freelists: %zu bytes (%.2f MB)\n", freelist_bytes(),
           freelist_bytes() / (1024.0 * 1024.0));
  }
};

// Convenience namespace
namespace arena {
template <typename Tag>
void init(size_t initial = 4 * 1024 * 1024, size_t maximum = 0) {
  Arena<Tag>::init(initial, maximum);
}

template <typename Tag> void shutdown() { Arena<Tag>::shutdown(); }
template <typename Tag> void reset() { Arena<Tag>::reset(); }
template <typename Tag> void reset_and_decommit() {
  Arena<Tag>::reset_and_decommit();
}
template <typename Tag> void *alloc(size_t size) {
  return Arena<Tag>::alloc(size);
}
template <typename Tag> void reclaim(void *ptr, size_t size) {
  Arena<Tag>::reclaim(ptr, size);
}
template <typename Tag> size_t used() { return Arena<Tag>::used(); }
template <typename Tag> size_t committed() { return Arena<Tag>::committed(); }
template <typename Tag> size_t reclaimed() { return Arena<Tag>::reclaimed(); }
template <typename Tag> size_t reused() { return Arena<Tag>::reused(); }
template <typename Tag> void print_stats() { Arena<Tag>::print_stats(); }

// Streaming allocation support
template <typename Tag>
struct StreamAlloc {
    uint8_t* start;      // Where this allocation began
    uint8_t* write_pos;  // Current write position
    size_t reserved;     // How much we've reserved so far
};

template <typename Tag>
StreamAlloc<Tag> stream_begin(size_t initial_reserve = 1024) {
    if (!Arena<Tag>::base) {
        Arena<Tag>::init();
    }

    // Ensure we have space for initial reservation
    uint8_t* start = Arena<Tag>::current;
    size_t available = Arena<Tag>::committed_capacity - (Arena<Tag>::current - Arena<Tag>::base);

    if (initial_reserve > available) {
        // Need to commit more memory
        size_t needed = (Arena<Tag>::current - Arena<Tag>::base) + initial_reserve;
        if (needed > Arena<Tag>::reserved_capacity) {
            fprintf(stderr, "Stream allocation too large\n");
            exit(1);
        }

        size_t new_committed = VirtualMemory::round_to_pages(needed);
        if (!VirtualMemory::commit(Arena<Tag>::base + Arena<Tag>::committed_capacity,
                                  new_committed - Arena<Tag>::committed_capacity)) {
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
void stream_write(StreamAlloc<Tag>* stream, const void* data, size_t size) {
    size_t used = stream->write_pos - stream->start;
    size_t remaining = stream->reserved - used;

    if (size > remaining) {
        // Need to grow the reservation
        size_t new_reserved = stream->reserved * 2;
        while (new_reserved - used < size) {
            new_reserved *= 2;
        }

        // Ensure we have space
        size_t available = Arena<Tag>::committed_capacity - (stream->start - Arena<Tag>::base);
        if (new_reserved > available) {
            size_t needed = (stream->start - Arena<Tag>::base) + new_reserved;
            if (needed > Arena<Tag>::reserved_capacity) {
                fprintf(stderr, "Stream allocation too large\n");
                exit(1);
            }

            size_t new_committed = VirtualMemory::round_to_pages(needed);
            if (!VirtualMemory::commit(Arena<Tag>::base + Arena<Tag>::committed_capacity,
                                      new_committed - Arena<Tag>::committed_capacity)) {
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
uint8_t* stream_finish(StreamAlloc<Tag>* stream) {
    // Shrink allocation to actual size (give back unused reservation)
    Arena<Tag>::current = stream->write_pos;
    return stream->start;
}

template <typename Tag>
void stream_abandon(StreamAlloc<Tag>* stream) {
    // Reset current to where we started
    Arena<Tag>::current = stream->start;
}

template <typename Tag>
size_t stream_size(const StreamAlloc<Tag>* stream) {
    return stream->write_pos - stream->start;
}


} // namespace arena







template <typename T, typename ArenaTag = global_arena, uint32_t InitialSize = 8>
struct array {
  T *data = nullptr;
  uint32_t size = 0;
  uint32_t capacity = 0;
};


// Set array contents from another array (potentially from a different arena)
template <typename T, typename Tag, uint32_t InitSize, typename OtherTag>
void array_set(array<T, Tag, InitSize>* arr, const array<T, OtherTag>& other) {
    // Clear existing contents
    array_clear(arr);

    // Reserve space for the new contents
    array_reserve(arr, other.size);

    // Copy the data
    if (other.size > 0) {
        memcpy(arr->data, other.data, other.size * sizeof(T));
        arr->size = other.size;
    }
}


// Ensure array has at least 'min_capacity' slots
template <typename T, typename Tag, uint32_t InitSize>
inline void array_reserve(array<T, Tag, InitSize> *arr, uint32_t min_capacity) {
  // Already have enough capacity
  if (arr->capacity >= min_capacity) {
    return;
  }

  // Lazy init if needed
  if (!arr->data) {
    arr->capacity = min_capacity > InitSize ? min_capacity : InitSize;
    arr->data = (T *)Arena<Tag>::alloc(arr->capacity * sizeof(T));
    return;
  }

  // Save old data for reclamation
  T* old_data = arr->data;
  uint32_t old_capacity = arr->capacity;

  // Calculate new capacity (at least double, but ensure we meet min_capacity)
  uint32_t new_cap = arr->capacity * 2;
  if (new_cap < min_capacity) {
    new_cap = min_capacity;
  }

  // Allocate and copy
  T *new_data = (T *)Arena<Tag>::alloc(new_cap * sizeof(T));
  memcpy(new_data, arr->data, arr->size * sizeof(T));

  arr->data = new_data;
  arr->capacity = new_cap;

  // Automatically reclaim old memory
  Arena<Tag>::reclaim(old_data, old_capacity * sizeof(T));
}

template <typename T, typename Tag, uint32_t InitSize>
uint32_t array_push(array<T, Tag, InitSize> *arr, const T &value) {
  // Ensure we have room for one more element
  array_reserve(arr, arr->size + 1);

  arr->data[arr->size] = value;
  // return index
  return arr->size++;
}

template <typename T, typename Tag, uint32_t InitSize>
void array_clear(array<T, Tag, InitSize> *arr) {
  if (arr->data) {
    memset(arr->data, 0, sizeof(T) * arr->size);
  }
  arr->size = 0;
}

template <typename T, typename Tag = global_arena>
array<T, Tag> *array_create() {
  auto *arr = (array<T, Tag> *)Arena<Tag>::alloc(
      sizeof(array<T, Tag>));
  arr->data = nullptr;
  arr->size = 0;
  arr->capacity = 0;
  return arr;
}

// Resize array to exact size (useful for pre-allocating known sizes)
template <typename T, typename Tag, uint32_t InitSize>
void array_resize(array<T, Tag, InitSize> *arr, uint32_t new_size) {
  array_reserve(arr, new_size);

  // Zero out new elements if growing
  if (new_size > arr->size) {
    memset(arr->data + arr->size, 0, (new_size - arr->size) * sizeof(T));
  }

  arr->size = new_size;
}

// Push multiple elements at once
template <typename T, typename Tag, uint32_t InitSize>
T *array_push_n(array<T, Tag, InitSize> *arr, const T *values, uint32_t count) {
  array_reserve(arr, arr->size + count);

  T *dest = arr->data + arr->size;
  memcpy(dest, values, count * sizeof(T));
  arr->size += count;

  return dest;
}

// Shrink array to fit (reclaim excess capacity)
template <typename T, typename Tag, uint32_t InitSize>
void array_shrink_to_fit(array<T, Tag, InitSize> *arr) {
  if (arr->size == arr->capacity || arr->size == 0) {
    return;
  }

  T* old_data = arr->data;
  uint32_t old_capacity = arr->capacity;

  // Allocate exact size needed
  arr->data = (T *)Arena<Tag>::alloc(arr->size * sizeof(T));
  memcpy(arr->data, old_data, arr->size * sizeof(T));
  arr->capacity = arr->size;

  // Reclaim old memory
  Arena<Tag>::reclaim(old_data, old_capacity * sizeof(T));
}






// Generic hash functions for different integer sizes
inline uint32_t hash_32(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

inline uint64_t hash_64(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    return x;
}

// Template hash that picks the right function based on size
template<typename T>
inline uint32_t hash_int(T x) {
    // Convert all types to unsigned for hashing
    using U = typename std::make_unsigned<T>::type;
    U ux = static_cast<U>(x);

    if constexpr (sizeof(T) <= 4) {
        return hash_32(static_cast<uint32_t>(ux));
    } else {
        // For 64-bit, hash and then reduce to 32-bit
        return static_cast<uint32_t>(hash_64(static_cast<uint64_t>(ux)));
    }
}

// Generic HashMap for any integer key type
template <typename K, typename V, typename Tag = global_arena>
struct hash_map {
    static_assert(std::is_integral<K>::value, "Key must be an integer type");

    struct Entry {
        K key;
        V value;
        enum State : uint8_t { EMPTY = 0, OCCUPIED = 1, DELETED = 2 };
        State state;
    };

    Entry* entries = nullptr;
    uint32_t capacity = 0;
    uint32_t size = 0;
    uint32_t tombstones = 0;
};

template <typename K, typename V, typename Tag>
static void hashmap_init(hash_map<K, V, Tag>* m, uint32_t initial_capacity = 16) {
    initial_capacity--;
    initial_capacity |= initial_capacity >> 1;
    initial_capacity |= initial_capacity >> 2;
    initial_capacity |= initial_capacity >> 4;
    initial_capacity |= initial_capacity >> 8;
    initial_capacity |= initial_capacity >> 16;
    initial_capacity++;

    m->capacity = initial_capacity;
    m->entries = (typename hash_map<K, V, Tag>::Entry*)
        Arena<Tag>::alloc(initial_capacity * sizeof(typename hash_map<K, V, Tag>::Entry));
    memset(m->entries, 0, initial_capacity * sizeof(typename hash_map<K, V, Tag>::Entry));
    m->size = 0;
    m->tombstones = 0;
}

template <typename K, typename V, typename Tag>
static V* hashmap_insert_internal(hash_map<K, V, Tag>* m, K key, V value) {
    uint32_t mask = m->capacity - 1;
    uint32_t idx = hash_int(key) & mask;

    while (true) {
        auto& entry = m->entries[idx];

        if (entry.state != hash_map<K, V, Tag>::Entry::OCCUPIED) {
            entry.key = key;
            entry.value = value;
            entry.state = hash_map<K, V, Tag>::Entry::OCCUPIED;
            m->size++;
            return &entry.value;
        }

        if (entry.key == key) {
            entry.value = value;
            return &entry.value;
        }

        idx = (idx + 1) & mask;
    }
}

template <typename K, typename V, typename Tag>
static void hashmap_grow(hash_map<K, V, Tag>* m) {
    uint32_t old_capacity = m->capacity;
    auto* old_entries = m->entries;

    m->capacity = old_capacity * 2;
    m->entries = (typename hash_map<K, V, Tag>::Entry*)
        Arena<Tag>::alloc(m->capacity * sizeof(typename hash_map<K, V, Tag>::Entry));
    memset(m->entries, 0, m->capacity * sizeof(typename hash_map<K, V, Tag>::Entry));

    uint32_t old_size = m->size;
    m->size = 0;
    m->tombstones = 0;

    for (uint32_t i = 0; i < old_capacity; i++) {
        if (old_entries[i].state == hash_map<K, V, Tag>::Entry::OCCUPIED) {
            hashmap_insert_internal(m, old_entries[i].key, old_entries[i].value);
        }
    }
}

template <typename K, typename V, typename Tag>
V* hashmap_insert(hash_map<K, V, Tag>* m, K key, V value) {
    if (!m->entries) {
        hashmap_init(m);
    }

    if ((m->size + m->tombstones) * 4 >= m->capacity * 3) {
        hashmap_grow(m);
    }

    uint32_t mask = m->capacity - 1;
    uint32_t idx = hash_int(key) & mask;
    uint32_t first_deleted = (uint32_t)-1;

    while (true) {
        auto& entry = m->entries[idx];

        if (entry.state == hash_map<K, V, Tag>::Entry::EMPTY) {
            if (first_deleted != (uint32_t)-1) {
                auto& deleted_entry = m->entries[first_deleted];
                deleted_entry.key = key;
                deleted_entry.value = value;
                deleted_entry.state = hash_map<K, V, Tag>::Entry::OCCUPIED;
                m->tombstones--;
                m->size++;
                return &deleted_entry.value;
            } else {
                entry.key = key;
                entry.value = value;
                entry.state = hash_map<K, V, Tag>::Entry::OCCUPIED;
                m->size++;
                return &entry.value;
            }
        }

        if (entry.state == hash_map<K, V, Tag>::Entry::DELETED) {
            if (first_deleted == (uint32_t)-1) {
                first_deleted = idx;
            }
        } else if (entry.key == key) {
            entry.value = value;
            return &entry.value;
        }

        idx = (idx + 1) & mask;
    }
}

template <typename K, typename V, typename Tag>
V* hashmap_get(hash_map<K, V, Tag>* m, K key) {
    if (!m->entries || m->size == 0) return nullptr;

    uint32_t mask = m->capacity - 1;
    uint32_t idx = hash_int(key) & mask;

    while (true) {
        auto& entry = m->entries[idx];

        if (entry.state == hash_map<K, V, Tag>::Entry::EMPTY) {
            return nullptr;
        }

        if (entry.state == hash_map<K, V, Tag>::Entry::OCCUPIED && entry.key == key) {
            return &entry.value;
        }

        idx = (idx + 1) & mask;
    }
}

template <typename K, typename V, typename Tag>
bool hashmap_delete(hash_map<K, V, Tag>* m, K key) {
    if (!m->entries || m->size == 0) return false;

    uint32_t mask = m->capacity - 1;
    uint32_t idx = hash_int(key) & mask;

    while (true) {
        auto& entry = m->entries[idx];

        if (entry.state == hash_map<K, V, Tag>::Entry::EMPTY) {
            return false;
        }

        if (entry.state == hash_map<K, V, Tag>::Entry::OCCUPIED && entry.key == key) {
            entry.state = hash_map<K, V, Tag>::Entry::DELETED;
            m->size--;
            m->tombstones++;
            return true;
        }

        idx = (idx + 1) & mask;
    }
}

template <typename K, typename V, typename Tag>
void hashmap_clear(hash_map<K, V, Tag>* m) {
    if (m->entries) {
        memset(m->entries, 0, m->capacity * sizeof(typename hash_map<K, V, Tag>::Entry));
    }
    m->size = 0;
    m->tombstones = 0;
}






// Red-Black Tree based Map implementation with node reclamation
template <typename K, typename V, typename Tag = global_arena> struct map {
  enum Color { RED, BLACK };

  struct Node {
    K key;
    V value;
    Node *left;
    Node *right;
    Node *parent;
    Color color;
  };

  Node *root = nullptr;
  uint32_t size = 0;
};

// Helper functions for Red-Black tree
template <typename K, typename V, typename Tag>
static void rotate_left(map<K, V, Tag> *m, typename map<K, V, Tag>::Node *x) {
  auto *y = x->right;
  x->right = y->left;

  if (y->left)
    y->left->parent = x;

  y->parent = x->parent;

  if (!x->parent)
    m->root = y;
  else if (x == x->parent->left)
    x->parent->left = y;
  else
    x->parent->right = y;

  y->left = x;
  x->parent = y;
}

template <typename K, typename V, typename Tag>
static void rotate_right(map<K, V, Tag> *m,
                         typename map<K, V, Tag>::Node *x) {
  auto *y = x->left;
  x->left = y->right;

  if (y->right)
    y->right->parent = x;

  y->parent = x->parent;

  if (!x->parent)
    m->root = y;
  else if (x == x->parent->right)
    x->parent->right = y;
  else
    x->parent->left = y;

  y->right = x;
  x->parent = y;
}

template <typename K, typename V, typename Tag>
static void insert_fixup(map<K, V, Tag> *m,
                         typename map<K, V, Tag>::Node *z) {
  while (z->parent && z->parent->color == map<K, V, Tag>::RED) {
    if (z->parent == z->parent->parent->left) {
      auto *y = z->parent->parent->right;
      if (y && y->color == map<K, V, Tag>::RED) {
        z->parent->color = map<K, V, Tag>::BLACK;
        y->color = map<K, V, Tag>::BLACK;
        z->parent->parent->color = map<K, V, Tag>::RED;
        z = z->parent->parent;
      } else {
        if (z == z->parent->right) {
          z = z->parent;
          rotate_left(m, z);
        }
        z->parent->color = map<K, V, Tag>::BLACK;
        z->parent->parent->color = map<K, V, Tag>::RED;
        rotate_right(m, z->parent->parent);
      }
    } else {
      auto *y = z->parent->parent->left;
      if (y && y->color == map<K, V, Tag>::RED) {
        z->parent->color = map<K, V, Tag>::BLACK;
        y->color = map<K, V, Tag>::BLACK;
        z->parent->parent->color = map<K, V, Tag>::RED;
        z = z->parent->parent;
      } else {
        if (z == z->parent->left) {
          z = z->parent;
          rotate_right(m, z);
        }
        z->parent->color = map<K, V, Tag>::BLACK;
        z->parent->parent->color = map<K, V, Tag>::RED;
        rotate_left(m, z->parent->parent);
      }
    }
  }
  m->root->color = map<K, V, Tag>::BLACK;
}

template <typename K, typename V, typename Tag>
V *m_get(map<K, V, Tag> *m, K key) {
  auto *current = m->root;
  while (current) {
    if (key < current->key)
      current = current->left;
    else if (key > current->key)
      current = current->right;
    else
      return &current->value;
  }
  return nullptr;
}

template <typename K, typename V, typename Tag>
V *m_insert(map<K, V, Tag> *m, K key, V value) {
  // Create new node - will try to use reclaimed memory first
  auto *node = (typename map<K, V, Tag>::Node *)Arena<Tag>::alloc(
      sizeof(typename map<K, V, Tag>::Node));
  node->key = key;
  node->value = value;
  node->left = nullptr;
  node->right = nullptr;
  node->color = map<K, V, Tag>::RED;

  // BST insert
  typename map<K, V, Tag>::Node *parent = nullptr;
  typename map<K, V, Tag>::Node *current = m->root;

  while (current) {
    parent = current;
    if (key < current->key)
      current = current->left;
    else if (key > current->key)
      current = current->right;
    else {
      // Key exists, update value and reclaim the unused new node
      current->value = value;
      Arena<Tag>::reclaim(node, sizeof(typename map<K, V, Tag>::Node));
      return &current->value;
    }
  }

  node->parent = parent;

  if (!parent) {
    m->root = node;
  } else if (key < parent->key) {
    parent->left = node;
  } else {
    parent->right = node;
  }

  m->size++;

  // Fix Red-Black properties
  insert_fixup(m, node);

  return &node->value;
}

template <typename K, typename V, typename Tag>
static typename map<K, V, Tag>::Node *
tree_minimum(typename map<K, V, Tag>::Node *node) {
  while (node->left)
    node = node->left;
  return node;
}

template <typename K, typename V, typename Tag>
static void transplant(map<K, V, Tag> *m, typename map<K, V, Tag>::Node *u,
                       typename map<K, V, Tag>::Node *v) {
  if (!u->parent)
    m->root = v;
  else if (u == u->parent->left)
    u->parent->left = v;
  else
    u->parent->right = v;

  if (v)
    v->parent = u->parent;
}

template <typename K, typename V, typename Tag>
static void delete_fixup(map<K, V, Tag> *m, typename map<K, V, Tag>::Node *x,
                         typename map<K, V, Tag>::Node *x_parent) {
  while (x != m->root && (!x || x->color == map<K, V, Tag>::BLACK)) {
    if (x == x_parent->left) {
      auto *w = x_parent->right;
      if (w->color == map<K, V, Tag>::RED) {
        w->color = map<K, V, Tag>::BLACK;
        x_parent->color = map<K, V, Tag>::RED;
        rotate_left(m, x_parent);
        w = x_parent->right;
      }
      if ((!w->left || w->left->color == map<K, V, Tag>::BLACK) &&
          (!w->right || w->right->color == map<K, V, Tag>::BLACK)) {
        w->color = map<K, V, Tag>::RED;
        x = x_parent;
        x_parent = x->parent;
      } else {
        if (!w->right || w->right->color == map<K, V, Tag>::BLACK) {
          if (w->left)
            w->left->color = map<K, V, Tag>::BLACK;
          w->color = map<K, V, Tag>::RED;
          rotate_right(m, w);
          w = x_parent->right;
        }
        w->color = x_parent->color;
        x_parent->color = map<K, V, Tag>::BLACK;
        if (w->right)
          w->right->color = map<K, V, Tag>::BLACK;
        rotate_left(m, x_parent);
        x = m->root;
      }
    } else {
      auto *w = x_parent->left;
      if (w->color == map<K, V, Tag>::RED) {
        w->color = map<K, V, Tag>::BLACK;
        x_parent->color = map<K, V, Tag>::RED;
        rotate_right(m, x_parent);
        w = x_parent->left;
      }
      if ((!w->right || w->right->color == map<K, V, Tag>::BLACK) &&
          (!w->left || w->left->color == map<K, V, Tag>::BLACK)) {
        w->color = map<K, V, Tag>::RED;
        x = x_parent;
        x_parent = x->parent;
      } else {
        if (!w->left || w->left->color == map<K, V, Tag>::BLACK) {
          if (w->right)
            w->right->color = map<K, V, Tag>::BLACK;
          w->color = map<K, V, Tag>::RED;
          rotate_left(m, w);
          w = x_parent->left;
        }
        w->color = x_parent->color;
        x_parent->color = map<K, V, Tag>::BLACK;
        if (w->left)
          w->left->color = map<K, V, Tag>::BLACK;
        rotate_right(m, x_parent);
        x = m->root;
      }
    }
  }
  if (x)
    x->color = map<K, V, Tag>::BLACK;
}

template <typename K, typename V, typename Tag>
bool map_delete(map<K, V, Tag> *m, K key) {
  // Find node
  auto *z = m->root;
  while (z) {
    if (key < z->key)
      z = z->left;
    else if (key > z->key)
      z = z->right;
    else
      break;
  }

  if (!z)
    return false;

  // Remember the node to reclaim
  typename map<K, V, Tag>::Node *node_to_reclaim = nullptr;

  auto *y = z;
  auto y_original_color = y->color;
  typename map<K, V, Tag>::Node *x = nullptr;
  typename map<K, V, Tag>::Node *x_parent = nullptr;

  if (!z->left) {
    x = z->right;
    x_parent = z->parent;
    transplant(m, z, z->right);
    node_to_reclaim = z;
  } else if (!z->right) {
    x = z->left;
    x_parent = z->parent;
    transplant(m, z, z->left);
    node_to_reclaim = z;
  } else {
    y = tree_minimumap<K, V, Tag>(z->right);
    y_original_color = y->color;
    x = y->right;

    if (y->parent == z) {
      x_parent = y;
    } else {
      x_parent = y->parent;
      transplant(m, y, y->right);
      y->right = z->right;
      y->right->parent = y;
    }

    transplant(m, z, y);
    y->left = z->left;
    y->left->parent = y;
    y->color = z->color;
    node_to_reclaim = z;
  }

  m->size--;

  if (y_original_color == map<K, V, Tag>::BLACK)
    delete_fixup(m, x, x_parent);

  // Reclaim the deleted node
  if (node_to_reclaim) {
    Arena<Tag>::reclaim(node_to_reclaim, sizeof(typename map<K, V, Tag>::Node));
  }

  return true;
}

template <typename K, typename V, typename Tag>
void map_clear(map<K, V, Tag> *m) {
  // Could optionally walk the tree and reclaim all nodes,
  // but usually map_clear is called before arena reset anyway
  m->root = nullptr;
  m->size = 0;
}

// Rebuild map to reclaim all deleted nodes at once (defragmentation)
template <typename K, typename V, typename Tag>
void map_rebuild(map<K, V, Tag> *m) {
  if (!m->root)
    return;

  // Collect all nodes in sorted order
  struct NodePair {
    K key;
    V value;
  };

  // Count nodes first
  uint32_t count = m->size;
  if (count == 0)
    return;

  // Allocate temporary storage
  NodePair pairs[count];

  // In-order traversal to collect all key-value pairs
  struct StackFrame {
    typename map<K, V, Tag>::Node *node;
    bool processed;
  };

  StackFrame stack[count];
  int stack_top = 0;
  uint32_t pair_idx = 0;

  stack[stack_top++] = {m->root, false};

  while (stack_top > 0) {
    StackFrame &frame = stack[stack_top - 1];

    if (!frame.processed) {
      frame.processed = true;
      if (frame.node->left) {
        stack[stack_top++] = {frame.node->left, false};
      }
    } else {
      pairs[pair_idx].key = frame.node->key;
      pairs[pair_idx].value = frame.node->value;
      pair_idx++;

      stack_top--;

      if (frame.node->right) {
        stack[stack_top++] = {frame.node->right, false};
      }
    }
  }

  // Reclaim all old nodes by clearing the map
  map_clear(m);

  // Re-insert all pairs (will allocate fresh, contiguous nodes)
  for (uint32_t i = 0; i < count; i++) {
    map_insert(m, pairs[i].key, pairs[i].value);
  }
}

// HashSet reuses HashMap
template <typename K, typename Tag = global_arena> using hash_set= hash_map<K, uint8_t, Tag>;

// HashSet implementation
template <typename K, typename Tag>
bool
hashset_insert(hash_set<K, Tag>* set, K key)
{
    auto* existing = hashmap_get(set, key);
    if (existing)
        return false;

    hashmap_insert(set, key, uint8_t(1));
    return true;
}


template <typename K, typename Tag>
bool hashset_contains(hash_set<K, Tag>* set, K key)
{
    return hashmap_get(set, key) != nullptr;
}

template <typename K, typename Tag>
bool hashset_delete(hash_set<K, Tag>* set, K key)
{
    return hashmap_delete(set, key);
}

template <typename K, typename Tag>
void hashset_clear(hash_set<K, Tag>* set)
{
    hashmap_clear(set);
}




// String is just an array of chars
template <typename Tag = global_arena, uint32_t InitSize = 0>
using string = array<char, Tag, InitSize>;

// Copy a C string
template <typename Tag, uint32_t InitSize>
void string_set(string<Tag, InitSize> *s, const char *cstr) {
  int length = strlen(cstr) + 1;
  if(length > s->capacity) {
     array_reserve(s, length);
  }
  strcpy(s->data, cstr);
  s->size= length;
  s->capacity = length;
}

template <typename Tag = global_arena, uint32_t InitSize = 0>
void string_split(const char * str, char delimiter, array<string<Tag>, Tag>* result) {
    // Clear the result array
    array_clear(result);



    const char* start = str;
    const char* end = str;

    while (*end) {
        if (*end == delimiter) {
            // Create substring from start to current position
            string<Tag> substr;
            size_t len = end - start + 1;
            array_reserve(&substr, len);
            memcpy(substr.data, start, len - 1);
            substr.data[len - 1] = '\0';
            substr.size = len;
            array_push(result, substr);
            start = end + 1;
        }
        end++;
    }

    // Add the last substring (if any)
    if (start < end) {
        string<Tag> substr;
        size_t len = end - start + 1;
        array_reserve(&substr, len);
        memcpy(substr.data, start, len - 1);
        substr.data[len - 1] = '\0';
        substr.size = len;
        array_push(result, substr);
    }
}



// Append to string
template <typename Tag, uint32_t InitSize>
void string_append(string<Tag, InitSize> *s, const char *cstr) {
  if (s->size > 0 && s->data[s->size - 1] == '\0') {
    s->size--; // Remove old null terminator
  }
  while (*cstr) {
    array_push(s, *cstr++);
  }
  array_push(s, '\0');
}



// String hashing and string_map implementation
// Add this to your header file after the existing hash_map implementation

// FNV-1a hash for strings
inline uint32_t hash_string(const char* str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619u;
    }
    return hash;
}

// Helper to allocate and copy string
template <typename Tag>
inline char* string_dup(const char* str) {
    size_t len = strlen(str) + 1;
    char* copy = (char*)Arena<Tag>::alloc(len);
    memcpy(copy, str, len);
    return copy;
}

// String map - similar structure to hash_map but with string keys
template <typename V, typename Tag = global_arena>
struct string_map {
    struct Entry {
        char* key;           // nullptr means empty
        V value;
        uint32_t hash;       // cached hash value
        enum State : uint8_t { EMPTY = 0, OCCUPIED = 1, DELETED = 2 };
        State state;
    };

    Entry* entries = nullptr;
    uint32_t capacity = 0;
    uint32_t size = 0;
    uint32_t tombstones = 0;
};

template <typename V, typename Tag>
static void stringmap_init(string_map<V, Tag>* m, uint32_t initial_capacity = 16) {
    initial_capacity--;
    initial_capacity |= initial_capacity >> 1;
    initial_capacity |= initial_capacity >> 2;
    initial_capacity |= initial_capacity >> 4;
    initial_capacity |= initial_capacity >> 8;
    initial_capacity |= initial_capacity >> 16;
    initial_capacity++;

    m->capacity = initial_capacity;
    m->entries = (typename string_map<V, Tag>::Entry*)
        Arena<Tag>::alloc(initial_capacity * sizeof(typename string_map<V, Tag>::Entry));
    memset(m->entries, 0, initial_capacity * sizeof(typename string_map<V, Tag>::Entry));
    m->size = 0;
    m->tombstones = 0;
}

template <typename V, typename Tag>
static V* stringmap_insert_internal(string_map<V, Tag>* m, const char* key, uint32_t hash, V value) {
    uint32_t mask = m->capacity - 1;
    uint32_t idx = hash & mask;

    while (true) {
        auto& entry = m->entries[idx];

        if (entry.state != string_map<V, Tag>::Entry::OCCUPIED) {
            // Found empty or deleted slot
            if (entry.state == string_map<V, Tag>::Entry::DELETED) {
                m->tombstones--;
            }

            // Reclaim old string if it exists (from deleted entry)
            if (entry.key) {
                size_t old_len = strlen(entry.key) + 1;
                Arena<Tag>::reclaim(entry.key, old_len);
            }

            entry.key = string_dup<Tag>(key);
            entry.hash = hash;
            entry.value = value;
            entry.state = string_map<V, Tag>::Entry::OCCUPIED;
            m->size++;
            return &entry.value;
        }

        // Check if key matches
        if (entry.hash == hash && strcmp(entry.key, key) == 0) {
            entry.value = value;
            return &entry.value;
        }

        idx = (idx + 1) & mask;
    }
}

template <typename V, typename Tag>
static void stringmap_grow(string_map<V, Tag>* m) {
    uint32_t old_capacity = m->capacity;
    auto* old_entries = m->entries;

    m->capacity = old_capacity * 2;
    m->entries = (typename string_map<V, Tag>::Entry*)
        Arena<Tag>::alloc(m->capacity * sizeof(typename string_map<V, Tag>::Entry));
    memset(m->entries, 0, m->capacity * sizeof(typename string_map<V, Tag>::Entry));

    uint32_t old_size = m->size;
    m->size = 0;
    m->tombstones = 0;

    for (uint32_t i = 0; i < old_capacity; i++) {
        if (old_entries[i].state == string_map<V, Tag>::Entry::OCCUPIED) {
            // Reuse existing string allocation - no need to duplicate again
            uint32_t mask = m->capacity - 1;
            uint32_t idx = old_entries[i].hash & mask;

            while (m->entries[idx].state == string_map<V, Tag>::Entry::OCCUPIED) {
                idx = (idx + 1) & mask;
            }

            m->entries[idx] = old_entries[i];
            m->size++;
        } else if (old_entries[i].state == string_map<V, Tag>::Entry::DELETED && old_entries[i].key) {
            // Reclaim deleted entry's string
            size_t len = strlen(old_entries[i].key) + 1;
            Arena<Tag>::reclaim(old_entries[i].key, len);
        }
    }

    // Reclaim old backing array
    Arena<Tag>::reclaim(old_entries, old_capacity * sizeof(typename string_map<V, Tag>::Entry));
}

template <typename V, typename Tag>
V* stringmap_insert(string_map<V, Tag>* m, const char* key, V value) {
    if (!m->entries) {
        stringmap_init(m);
    }

    if ((m->size + m->tombstones) * 4 >= m->capacity * 3) {
        stringmap_grow(m);
    }

    uint32_t hash = hash_string(key);
    uint32_t mask = m->capacity - 1;
    uint32_t idx = hash & mask;
    uint32_t first_deleted = (uint32_t)-1;

    while (true) {
        auto& entry = m->entries[idx];

        if (entry.state == string_map<V, Tag>::Entry::EMPTY) {
            if (first_deleted != (uint32_t)-1) {
                auto& deleted_entry = m->entries[first_deleted];

                // Reclaim old string if exists
                if (deleted_entry.key) {
                    size_t old_len = strlen(deleted_entry.key) + 1;
                    Arena<Tag>::reclaim(deleted_entry.key, old_len);
                }

                deleted_entry.key = string_dup<Tag>(key);
                deleted_entry.hash = hash;
                deleted_entry.value = value;
                deleted_entry.state = string_map<V, Tag>::Entry::OCCUPIED;
                m->tombstones--;
                m->size++;
                return &deleted_entry.value;
            } else {
                entry.key = string_dup<Tag>(key);
                entry.hash = hash;
                entry.value = value;
                entry.state = string_map<V, Tag>::Entry::OCCUPIED;
                m->size++;
                return &entry.value;
            }
        }

        if (entry.state == string_map<V, Tag>::Entry::DELETED) {
            if (first_deleted == (uint32_t)-1) {
                first_deleted = idx;
            }
        } else if (entry.hash == hash && strcmp(entry.key, key) == 0) {
            entry.value = value;
            return &entry.value;
        }

        idx = (idx + 1) & mask;
    }
}

template <typename V, typename Tag>
V* stringmap_get(string_map<V, Tag>* m, const char* key) {
    if (!m->entries || m->size == 0) return nullptr;

    uint32_t hash = hash_string(key);
    uint32_t mask = m->capacity - 1;
    uint32_t idx = hash & mask;

    while (true) {
        auto& entry = m->entries[idx];

        if (entry.state == string_map<V, Tag>::Entry::EMPTY) {
            return nullptr;
        }

        if (entry.state == string_map<V, Tag>::Entry::OCCUPIED &&
            entry.hash == hash &&
            strcmp(entry.key, key) == 0) {
            return &entry.value;
        }

        idx = (idx + 1) & mask;
    }
}

template <typename V, typename Tag>
bool stringmap_delete(string_map<V, Tag>* m, const char* key) {
    if (!m->entries || m->size == 0) return false;

    uint32_t hash = hash_string(key);
    uint32_t mask = m->capacity - 1;
    uint32_t idx = hash & mask;

    while (true) {
        auto& entry = m->entries[idx];

        if (entry.state == string_map<V, Tag>::Entry::EMPTY) {
            return false;
        }

        if (entry.state == string_map<V, Tag>::Entry::OCCUPIED &&
            entry.hash == hash &&
            strcmp(entry.key, key) == 0) {
            // Reclaim string memory
            size_t len = strlen(entry.key) + 1;
            Arena<Tag>::reclaim(entry.key, len);

            entry.key = nullptr;
            entry.state = string_map<V, Tag>::Entry::DELETED;
            m->size--;
            m->tombstones++;
            return true;
        }

        idx = (idx + 1) & mask;
    }
}

template <typename V, typename Tag>
void stringmap_clear(string_map<V, Tag>* m) {
    if (m->entries) {
        // Reclaim all string memory
        for (uint32_t i = 0; i < m->capacity; i++) {
            if (m->entries[i].state == string_map<V, Tag>::Entry::OCCUPIED && m->entries[i].key) {
                size_t len = strlen(m->entries[i].key) + 1;
                Arena<Tag>::reclaim(m->entries[i].key, len);
            } else if (m->entries[i].state == string_map<V, Tag>::Entry::DELETED && m->entries[i].key) {
                // Also reclaim deleted entries that still have strings
                size_t len = strlen(m->entries[i].key) + 1;
                Arena<Tag>::reclaim(m->entries[i].key, len);
            }
        }

        memset(m->entries, 0, m->capacity * sizeof(typename string_map<V, Tag>::Entry));
    }
    m->size = 0;
    m->tombstones = 0;
}
// Pair structure
template <typename K, typename V>
struct pair {
    K key;
    V value;
};

// Collect all entries from string_map
template <typename V, typename TagIn, typename TagOut>
void stringmap_collect(string_map<V, TagIn>* m, array<pair<const char*, V>, TagOut>* out) {
    array_clear(out);
    if (!m || !m->entries || m->size == 0) return;

    array_reserve(out, m->size);

    for (uint32_t i = 0; i < m->capacity; i++) {
        auto& entry = m->entries[i];
        if (entry.state == string_map<V, TagIn>::Entry::OCCUPIED) {
            pair<const char*, V> p = { entry.key, entry.value };
            array_push(out, p);
        }
    }
}

// Collect all entries from hash_map
template <typename K, typename V, typename TagIn, typename TagOut>
void hashmap_collect(hash_map<K, V, TagIn>* m, array<pair<K, V>, TagOut>* out) {
    array_clear(out);
    if (!m || !m->entries || m->size == 0) return;

    array_reserve(out, m->size);

    for (uint32_t i = 0; i < m->capacity; i++) {
        auto& entry = m->entries[i];
        if (entry.state == hash_map<K, V, TagIn>::Entry::OCCUPIED) {
            pair<K, V> p = { entry.key, entry.value };
            array_push(out, p);
        }
    }
}

// Helper for map collection
template <typename K, typename V, typename TagIn, typename TagOut>
void map_collect_node(typename map<K, V, TagIn>::Node* node, array<pair<K, V>, TagOut>* out) {
    if (!node) return;

    map_collect_node<K, V, TagIn, TagOut>(node->left, out);
    pair<K, V> p = { node->key, node->value };
    array_push(out, p);
    map_collect_node<K, V, TagIn, TagOut>(node->right, out);
}

// Collect all entries from map (in sorted order)
template <typename K, typename V, typename TagIn, typename TagOut>
void map_collect(map<K, V, TagIn>* m, array<pair<K, V>, TagOut>* out) {
    array_clear(out);
    if (!m || !m->root || m->size == 0) return;

    array_reserve(out, m->size);
    map_collect_node<K, V, TagIn, TagOut>(m->root, out);
}
