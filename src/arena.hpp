#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Tagged arena system - each arena type gets its own memory pool
template <typename Tag> struct Arena {
  static inline uint8_t *base = nullptr;
  static inline uint8_t *current = nullptr;
  static inline size_t capacity = 0;

  // Initialize arena with given capacity
  static void init(size_t cap) {
    if (base)
      return; // Already initialized

    base = (uint8_t *)malloc(cap);
    if (!base) {
      fprintf(stderr, "Failed to allocate arena: %zu bytes\n", cap);
      exit(1);
    }
    current = base;
    capacity = cap;
  }

  // Shutdown and free memory
  static void shutdown() {
    free(base);
    base = nullptr;
    current = nullptr;
    capacity = 0;
  }

  // Allocate memory from this arena
  static void *alloc(size_t size) {
    // Lazy init with default size if needed
    if (!base) {
      init(64 * 1024 * 1024); // 64MB default
    }

    // Align to 16 bytes
    size_t align = 16;
    uintptr_t current_addr = (uintptr_t)current;
    uintptr_t aligned_addr = (current_addr + align - 1) & ~(align - 1);

    uint8_t *aligned = (uint8_t *)aligned_addr;
    uint8_t *next = aligned + size;

    if (next > base + capacity) {
      fprintf(stderr, "Arena exhausted: requested %zu, used %zu/%zu\n", size,
              used(), capacity);

      exit(1);
      return nullptr;
    }

    current = next;
    return aligned;
  }

  // Reset arena (move pointer back to start)
  static void reset() {
      current = base;
      memset(base, 0, capacity);
  }

  // Get used memory
  static size_t used() { return current - base; }
};

// Convenience namespace for arena operations
namespace arena {
template <typename Tag> void init(size_t capacity) {
  Arena<Tag>::init(capacity);
}

template <typename Tag> void shutdown() { Arena<Tag>::shutdown(); }

template <typename Tag> void reset() { Arena<Tag>::reset(); }

template <typename Tag> void *alloc(size_t size) {
  return Arena<Tag>::alloc(size);
}

template <typename Tag> size_t used() { return Arena<Tag>::used(); }
} // namespace arena

// Arena-based vector
template <typename T, typename ArenaTag, size_t InitialCapacity = 16>
struct Vector {
  T *data;
  size_t capacity;
  size_t count;

  // Constructor - automatically initializes
  Vector() : data(nullptr), capacity(0), count(0) {}

  Vector& operator=(const Vector& other) {
        if (this != &other) {
            clear();
            set(other);
        }
        return *this;
    }

  void set(const Vector<T, ArenaTag> & to_copy) {
    if (count != 0) {
      // needs to be empty;
      return;
    }

    this->reserve(to_copy.capacity);

    for (int i = 0; i < to_copy.count; i++) {
      this->push_back(to_copy[i]);
    }
  }

  template<typename OtherArenaTag>
  void set(const Vector<T, OtherArenaTag>& to_copy) {
      if(count != 0) {
          return; // or clear() first
      }

      this->reserve(to_copy.count); // or to_copy.capacity
      for(size_t i = 0; i < to_copy.count; i++) {
          this->push_back(to_copy[i]);
      }
  }

  void push_back(const T &item) {
    if (!data || count >= capacity) {
      size_t new_capacity = capacity ? capacity * 2 : InitialCapacity;
      T *new_data = (T *)Arena<ArenaTag>::alloc(sizeof(T) * new_capacity);
      if (data && new_data) {
        memcpy(new_data, data, sizeof(T) * count);
      }
      data = new_data;
      capacity = new_capacity;
    }
    if (data) {
      data[count++] = item;
    }
  }

  void erase(const T &item) {
    int position = this->find(item);
    if (position == -1) {
      return;
    }

    this->data[position] = this->data[this->count - 1];
    this->count--;
  }

  void insert_unique(const T &item) {
    if (this->find(item) == -1) {
      this->push_back(item);
    }
  }

  void pop_back() {
    if (count > 0)
      count--;
  }

  T &back() { return data[count - 1]; }

  const T &back() const { return data[count - 1]; }

  T &operator[](size_t i) { return data[i]; }

  const T &operator[](size_t i) const { return data[i]; }

  size_t size() const { return count; }

  bool empty() const { return count == 0; }

  void clear() { count = 0; data = nullptr; capacity = 0; }

  void reserve(size_t new_capacity) {
    if (new_capacity > capacity) {
      T *new_data = (T *)Arena<ArenaTag>::alloc(sizeof(T) * new_capacity);
      if (data && new_data) {
        memcpy(new_data, data, sizeof(T) * count);
      }
      data = new_data;
      capacity = new_capacity;
    }
  }

  void resize(size_t new_size) {
    if (new_size > capacity) {
      reserve(new_size);
    }
    count = new_size;
  }

  T *begin() { return data; }
  T *end() { return data + count; }
  const T *begin() const { return data; }
  const T *end() const { return data + count; }

  // Find element and return index (-1 if not found)
  int find(const T &value) const {
    for (size_t i = 0; i < count; i++) {
      if (data[i] == value) {
        return (int)i;
      }
    }
    return -1;
  }

  // Find element with custom predicate and return index (-1 if not found)
  template <typename Predicate> int find_if(Predicate pred) const {
    for (size_t i = 0; i < count; i++) {
      if (pred(data[i])) {
        return (int)i;
      }
    }
    return -1;
  }
};

// Arena-based string
template <typename ArenaTag, size_t InitialCapacity = 32> struct Str {
  char *data;
  size_t len;
  size_t capacity;

  // Default constructor
  Str() : data(nullptr), len(0), capacity(0) {}

  // From C string
  Str(const char *str) : data(nullptr), len(0), capacity(0) {
    if (str) {
      assign(str);
    }
  }

  // Copy constructor
  Str(const Str &other) : data(nullptr), len(0), capacity(0) {
    if (other.data) {
      assign(other.data);
    }
  }

  void assign(const char *str) {
    if (!str) {
      data = nullptr;
      len = 0;
      capacity = 0;
      return;
    }

    len = strlen(str);
    if (!data || len + 1 > capacity) {
      capacity = len + 1 > InitialCapacity ? len + 1 : InitialCapacity;
      data = (char *)Arena<ArenaTag>::alloc(capacity);
    }
    if (data) {
      memcpy(data, str, len + 1);
    }
  }

  // Assignment operators
  Str &operator=(const char *str) {
    assign(str);
    return *this;
  }

  Str &operator=(const Str &other) {
    if (this != &other && other.data) {
      assign(other.data);
    }
    return *this;
  }

  // Append
  void append(const char *str) {
    if (!str)
      return;

    size_t str_len = strlen(str);
    size_t new_len = len + str_len;

    if (!data || new_len + 1 > capacity) {
      size_t new_capacity = capacity ? capacity * 2 : InitialCapacity;
      while (new_capacity < new_len + 1) {
        new_capacity *= 2;
      }

      char *new_data = (char *)Arena<ArenaTag>::alloc(new_capacity);
      if (data && new_data) {
        memcpy(new_data, data, len);
      }
      data = new_data;
      capacity = new_capacity;
    }

    if (data) {
      memcpy(data + len, str, str_len + 1);
      len = new_len;
    }
  }

  void append(char c) {
    char buf[2] = {c, '\0'};
    append(buf);
  }

  void append(const Str &other) {
    if (other.data) {
      append(other.data);
    }
  }

  // Concatenation operators
  Str operator+(const char *str) const {
    Str result(*this);
    result.append(str);
    return result;
  }

  Str operator+(const Str &other) const {
    Str result(*this);
    result.append(other);
    return result;
  }

  Str &operator+=(const char *str) {
    append(str);
    return *this;
  }

  Str &operator+=(const Str &other) {
    append(other);
    return *this;
  }

  Str &operator+=(char c) {
    append(c);
    return *this;
  }

  // Access
  char &operator[](size_t i) { return data[i]; }

  const char &operator[](size_t i) const { return data[i]; }

  const char *c_str() const { return data ? data : ""; }

  size_t length() const { return len; }

  size_t size() const { return len; }

  bool empty() const { return len == 0; }

  void clear() {
    len = 0;
    if (data) {
      data[0] = '\0';
    }
  }

  // Comparison operators
  bool operator==(const char *str) const {
    if (!data && !str)
      return true;
    if (!data || !str)
      return false;
    return strcmp(data, str) == 0;
  }

  bool operator==(const Str &other) const {
    if (!data && !other.data)
      return true;
    if (!data || !other.data)
      return false;
    return strcmp(data, other.data) == 0;
  }

  bool operator!=(const char *str) const { return !(*this == str); }

  bool operator!=(const Str &other) const { return !(*this == other); }

  bool operator<(const Str &other) const {
    if (!data && !other.data)
      return false;
    if (!data)
      return true;
    if (!other.data)
      return false;
    return strcmp(data, other.data) < 0;
  }

  // Check if string contains substring
  bool contains(const char *substr) const {
    if (!data || !substr)
      return false;
    return strstr(data, substr) != nullptr;
  }

  bool contains(const Str &substr) const {
    if (!data || !substr.data)
      return false;
    return strstr(data, substr.data) != nullptr;
  }

  // Find substring and return position (-1 if not found)
  int find(const char *substr) const {
    if (!data || !substr)
      return -1;
    const char *pos = strstr(data, substr);
    if (pos) {
      return (int)(pos - data);
    }
    return -1;
  }

  int find(const Str &substr) const { return find(substr.c_str()); }

  // Check if string starts with prefix
  bool starts_with(const char *prefix) const {
    if (!data || !prefix)
      return false;
    size_t prefix_len = strlen(prefix);
    if (prefix_len > len)
      return false;
    return strncmp(data, prefix, prefix_len) == 0;
  }

  bool equals(const char *prefix) const {
     size_t prefix_len = strlen(prefix);
     if(len != prefix_len) {
         return false;
     }

     return strncmp(data, prefix, prefix_len) == 0;
  }

  bool equals(const Str &str) const {
     if(len != str.length()) {
         return false;
     }

     return starts_with(str);
  }


  bool starts_with(const Str &prefix) const {
    return starts_with(prefix.c_str());
  }

  // Check if string ends with suffix
  bool ends_with(const char *suffix) const {
    if (!data || !suffix)
      return false;
    size_t suffix_len = strlen(suffix);
    if (suffix_len > len)
      return false;
    return strcmp(data + len - suffix_len, suffix) == 0;
  }

  bool ends_with(const Str &suffix) const {
    return ends_with(suffix.c_str());
  }
};

// Arena-based map using linear search with contiguous storage
template <typename K, typename V, typename ArenaTag,
          size_t InitialCapacity = 64>
struct Map {
  struct Entry {
    K key;
    V value;
  };

  Entry *entries;
  size_t capacity;
  size_t count;

  // Cached lookup - mutable to allow const methods to update cache
  mutable V *cached_value;
  mutable K cached_key;
  mutable bool has_cached;

  // Constructor - automatically initializes
  Map()
      : entries(nullptr), capacity(0), count(0), cached_value(nullptr),
        has_cached(false) {}

  // Find value by key, returns nullptr if not found
  V *find(const K &key) {
    if (!entries)
      return nullptr;

    // Check cache first
    if (has_cached && cached_key == key) {
      return cached_value;
    }

    // Linear search through contiguous entries
    for (size_t i = 0; i < count; i++) {
      if (entries[i].key == key) {
        // Update cache
        cached_key = key;
        cached_value = &entries[i].value;
        has_cached = true;
        return &entries[i].value;
      }
    }

    // Not found - clear cache
    cached_value = nullptr;
    has_cached = false;
    return nullptr;
  }

  const V *find(const K &key) const {
    if (!entries)
      return nullptr;

    // Check cache first
    if (has_cached && cached_key == key) {
      return cached_value;
    }

    // Linear search through contiguous entries
    for (size_t i = 0; i < count; i++) {
      if (entries[i].key == key) {
        // Update cache
        cached_key = key;
        cached_value = const_cast<V *>(&entries[i].value);
        has_cached = true;
        return &entries[i].value;
      }
    }

    // Not found - clear cache
    cached_value = nullptr;
    has_cached = false;
    return nullptr;
  }

  // Check if key exists (updates cache)
  bool contains(const K &key) const { return find(key) != nullptr; }

  // Insert or update key-value pair
  void insert(const K &key, const V &value) {
    // Lazy allocation
    if (!entries) {
      capacity = InitialCapacity;
      entries = (Entry *)Arena<ArenaTag>::alloc(sizeof(Entry) * capacity);
    }

    // Check if exists and update
    for (size_t i = 0; i < count; i++) {
      if (entries[i].key == key) {
        entries[i].value = value;
        // Update cache if this was the cached entry
        if (has_cached && cached_key == key) {
          cached_value = &entries[i].value;
        }
        return;
      }
    }

    // Need to grow?
    if (count >= capacity) {
      size_t new_capacity = capacity * 2;
      Entry *new_entries =
          (Entry *)Arena<ArenaTag>::alloc(sizeof(Entry) * new_capacity);
      memcpy(new_entries, entries, sizeof(Entry) * count);
      entries = new_entries;
      capacity = new_capacity;

      // Fix cache pointer if it was pointing to old array
      if (has_cached) {
        for (size_t i = 0; i < count; i++) {
          if (entries[i].key == cached_key) {
            cached_value = &entries[i].value;
            break;
          }
        }
      }
    }

    // Add new entry at the end
    entries[count].key = key;
    entries[count].value = value;
    count++;
  }

  // Get entry at index (for iteration)
  Entry *at(size_t index) {
    if (index < count) {
      return &entries[index];
    }
    return nullptr;
  }

  const Entry *at(size_t index) const {
    if (index < count) {
      return &entries[index];
    }
    return nullptr;
  }

  // Get key at index
  K *key_at(size_t index) {
    if (index < count) {
      return &entries[index].key;
    }
    return nullptr;
  }

  const K *key_at(size_t index) const {
    if (index < count) {
      return &entries[index].key;
    }
    return nullptr;
  }

  // Get value at index
  V *value_at(size_t index) {
    if (index < count) {
      return &entries[index].value;
    }
    return nullptr;
  }

  const V *value_at(size_t index) const {
    if (index < count) {
      return &entries[index].value;
    }
    return nullptr;
  }

  // Access or create element
  V &operator[](const K &key) {
    // Try to find existing
    V *existing = find(key);
    if (existing) {
      return *existing;
    }

    // Need to create new entry
    // Lazy allocation
    if (!entries) {
      capacity = InitialCapacity;
      entries = (Entry *)Arena<ArenaTag>::alloc(sizeof(Entry) * capacity);
    }

    // Need to grow?
    if (count >= capacity) {
      size_t new_capacity = capacity * 2;
      Entry *new_entries =
          (Entry *)Arena<ArenaTag>::alloc(sizeof(Entry) * new_capacity);
      memcpy(new_entries, entries, sizeof(Entry) * count);
      entries = new_entries;
      capacity = new_capacity;

      // Fix cache pointer if it was pointing to old array
      if (has_cached) {
        for (size_t i = 0; i < count; i++) {
          if (entries[i].key == cached_key) {
            cached_value = &entries[i].value;
            break;
          }
        }
      }
    }

    // Add new entry at the end
    entries[count].key = key;
    entries[count].value = V{}; // Default construct
    V *result = &entries[count].value;
    count++;

    // Update cache
    cached_key = key;
    cached_value = result;
    has_cached = true;

    return *result;
  }

  // Remove key - swap with last element to maintain contiguous storage
  void erase(const K &key) {
    if (!entries)
      return;

    for (size_t i = 0; i < count; i++) {
      if (entries[i].key == key) {
        // Clear cache if we're removing the cached entry
        if (has_cached && cached_key == key) {
          cached_value = nullptr;
          has_cached = false;
        }

        // Swap with last element (if not already last)
        if (i < count - 1) {
          entries[i] = entries[count - 1];

          // Update cache if we moved the cached entry
          if (has_cached && cached_key == entries[i].key) {
            cached_value = &entries[i].value;
          }
        }

        count--;
        return;
      }
    }
  }

  // Clear all entries
  void clear() {
    count = 0;
    cached_value = nullptr;
    has_cached = false;
    entries = nullptr;
    capacity = 0;
  }

  // Get all keys
  Vector<K, ArenaTag> keys() const {
    Vector<K, ArenaTag> result;
    result.reserve(count);
    for (size_t i = 0; i < count; i++) {
      result.push_back(entries[i].key);
    }
    return result;
  }

  // Get all values
  Vector<V, ArenaTag> values() const {
    Vector<V, ArenaTag> result;
    result.reserve(count);
    for (size_t i = 0; i < count; i++) {
      result.push_back(entries[i].value);
    }
    return result;
  }

  bool empty() const { return count == 0; }
  size_t size() const { return count; }
};

template <typename T, typename ArenaTag, size_t InitialCapacity = 16>
struct Set {
  Vector<T, ArenaTag, InitialCapacity> vector;

  // Constructor
  Set() {}

  // Insert an element (only if it doesn't exist)
  void insert(const T &item) { vector.insert_unique(item); }

  // Remove an element
  void erase(const T &item) { vector.erase(item); }

  // Check if element exists
  bool contains(const T &item) const { return vector.find(item) != -1; }

  // Get size of set
  size_t size() const { return vector.size(); }

  // Check if set is empty
  bool empty() const { return vector.empty(); }

  // Clear all elements
  void clear() { vector.clear(); }

  // Find element (returns index or -1 if not found)
  int find(const T &item) const { return vector.find(item); }

  // Find element with predicate (returns index or -1 if not found)
  template <typename Predicate> int find_if(Predicate pred) const {
    return vector.find_if(pred);
  }

  // Iterator support
  T *begin() { return vector.begin(); }
  T *end() { return vector.end(); }
  const T *begin() const { return vector.begin(); }
  const T *end() const { return vector.end(); }

  // Reserve space
  void reserve(size_t new_capacity) { vector.reserve(new_capacity); }
};

template <typename T, typename ArenaTag, size_t InitialCapacity = 16>
struct Queue {
  Vector<T, ArenaTag, InitialCapacity> vector;

  // Constructor
  Queue() {}

  // Add element to the back of the queue
  void push(const T &item) { vector.push_back(item); }

  // Remove and return element from the front of the queue
  T pop() {
    if (vector.empty()) {
      return T{}; // Default-constructed value if empty
    }
    T result = vector[0];
    // Shift elements left
    for (size_t i = 0; i < vector.size() - 1; ++i) {
      vector[i] = vector[i + 1];
    }
    vector.pop_back();
    return result;
  }

  // Get reference to front element
  T &front() { return vector[0]; }

  const T &front() const { return vector[0]; }

  // Get reference to back element
  T &back() { return vector.back(); }

  const T &back() const { return vector.back(); }

  // Check if queue is empty
  bool empty() const { return vector.empty(); }

  // Get size of queue
  size_t size() const { return vector.size(); }

  // Clear all elements
  void clear() { vector.clear(); }

  // Reserve space
  void reserve(size_t new_capacity) { vector.reserve(new_capacity); }
};
