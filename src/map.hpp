// map.hpp - Map implementation using two Vecs for keys and values

#pragma once
#include "vec.hpp"

// Primary template - Arena allocated Map
template <typename K, typename V, typename ArenaTag, size_t InitialCapacity = 16>
class Map {
    static_assert(is_arena_tag<ArenaTag>::value,
                  "Third parameter must be an Arena tag");

    Vec<K, ArenaTag, InitialCapacity>* keys;
    Vec<V, ArenaTag, InitialCapacity>* values;

public:
    // Constructor
    Map() : keys(nullptr), values(nullptr) {}

    // Factory method for arena allocation
    static Map* create(size_t initial_capacity = 16) {
        // Allocate the Map object itself
        Map* map = (Map*)Arena<ArenaTag>::alloc(sizeof(Map));
        new (map) Map();

        // Create the key and value vectors
        map->keys = Vec<K, ArenaTag, InitialCapacity>::create(initial_capacity);
        map->values = Vec<V, ArenaTag, InitialCapacity>::create(initial_capacity);

        return map;
    }

    // Insert or update a key-value pair
    void insert(const K& key, const V& value) {
        // Check if key already exists
        int index = find_key_index(key);

        if (index >= 0) {
            // Update existing value
            (*values)[index] = value;
        } else {
            // Add new key-value pair
            keys->push_back(key);
            values->push_back(value);
        }
    }

    // Get value by key (returns nullptr if not found)
    V* get(const K& key) {
        int index = find_key_index(key);
        if (index >= 0) {
            return &(*values)[index];
        }
        return nullptr;
    }

    // Const version of get
    const V* get(const K& key) const {
        int index = find_key_index(key);
        if (index >= 0) {
            return &(*values)[index];
        }
        return nullptr;
    }

    // Get key by index
    K* get_key(size_t index) {
        if (index < keys->size()) {
            return &(*keys)[index];
        }
        return nullptr;
    }

    // Const version of get_key
    const K* get_key(size_t index) const {
        if (index < keys->size()) {
            return &(*keys)[index];
        }
        return nullptr;
    }

    // Get value by index
    V* get_value(size_t index) {
        if (index < values->size()) {
            return &(*values)[index];
        }
        return nullptr;
    }

    // Const version of get_value
    const V* get_value(size_t index) const {
        if (index < values->size()) {
            return &(*values)[index];
        }
        return nullptr;
    }

    // Operator[] for convenient access (inserts if key doesn't exist)
    V& operator[](const K& key) {
        int index = find_key_index(key);

        if (index < 0) {
            // Key doesn't exist, insert with default value
            keys->push_back(key);
            values->push_back(V{});
            return values->back();
        }

        return (*values)[index];
    }

    // Check if key exists
    bool contains(const K& key) const {
        return find_key_index(key) >= 0;
    }

    // Remove a key-value pair
    bool remove(const K& key) {
        int index = find_key_index(key);
        if (index >= 0) {
            // Use swap_remove for O(1) removal
            keys->swap_remove(index);
            values->swap_remove(index);
            return true;
        }
        return false;
    }

    // Clear all entries
    void clear() {
        keys->clear();
        values->clear();
    }

    // Get number of entries
    size_t size() const {
        return keys->size();
    }

    // Check if empty
    bool empty() const {
        return keys->empty();
    }

    // Reserve capacity
    void reserve(size_t new_capacity) {
        keys->reserve(new_capacity);
        values->reserve(new_capacity);
    }

    // Get all keys (returns the Vec directly for iteration)
    Vec<K, ArenaTag, InitialCapacity>* get_keys() {
        return keys;
    }

    const Vec<K, ArenaTag, InitialCapacity>* get_keys() const {
        return keys;
    }

    // Get all values (returns the Vec directly for iteration)
    Vec<V, ArenaTag, InitialCapacity>* get_values() {
        return values;
    }

    const Vec<V, ArenaTag, InitialCapacity>* get_values() const {
        return values;
    }

    // Iterator support - iterate over key-value pairs by index
    struct Entry {
        K* key;
        V* value;
    };

    Entry get_entry(size_t index) {
        if (index < keys->size()) {
            return { &(*keys)[index], &(*values)[index] };
        }
        return { nullptr, nullptr };
    }


private:
    // Linear search for key (returns -1 if not found)
    template <typename U = K>
    typename std::enable_if<has_equality<U>::value, int>::type
    find_key_index(const K& key) const {
        if (!keys) return -1;
        return keys->find(key);
    }

    // Version for types without operator== (use custom comparator)
    template <typename U = K>
    typename std::enable_if<!has_equality<U>::value, int>::type
    find_key_index(const K& key) const {
        // For types without operator==, we can't do automatic comparison
        // User should provide a specialized Map with custom comparator
        return -1;
    }
};

// ========================================================================
// Specialization for stack allocation
// ========================================================================

template <typename K, typename V, size_t StackSize, size_t InitialCapacity>
class Map<K, V, stack_size_tag<StackSize>, InitialCapacity> {
    Vec<K, stack_size_tag<StackSize>, InitialCapacity> keys;
    Vec<V, stack_size_tag<StackSize>, InitialCapacity> values;

public:
    Map() {}

    // Insert or update
    void insert(const K& key, const V& value) {
        int index = find_key_index(key);

        if (index >= 0) {
            values[index] = value;
        } else {
            keys.push_back(key);
            values.push_back(value);
        }
    }

    // Get value by key
    V* get(const K& key) {
        int index = find_key_index(key);
        if (index >= 0) {
            return &values[index];
        }
        return nullptr;
    }

    const V* get(const K& key) const {
        int index = find_key_index(key);
        if (index >= 0) {
            return &values[index];
        }
        return nullptr;
    }

    // Get key by index
    K* get_key(size_t index) {
        if (index < keys.size()) {
            return &keys[index];
        }
        return nullptr;
    }

    const K* get_key(size_t index) const {
        if (index < keys.size()) {
            return &keys[index];
        }
        return nullptr;
    }

    // Get value by index
    V* get_value(size_t index) {
        if (index < values.size()) {
            return &values[index];
        }
        return nullptr;
    }

    const V* get_value(size_t index) const {
        if (index < values.size()) {
            return &values[index];
        }
        return nullptr;
    }

    // Operator[] for convenient access
    V& operator[](const K& key) {
        int index = find_key_index(key);

        if (index < 0) {
            keys.push_back(key);
            values.push_back(V{});
            return values.back();
        }

        return values[index];
    }

    // Check if key exists
    bool contains(const K& key) const {
        return find_key_index(key) >= 0;
    }

    // Remove a key-value pair
    bool remove(const K& key) {
        int index = find_key_index(key);
        if (index >= 0) {
            keys.swap_remove(index);
            values.swap_remove(index);
            return true;
        }
        return false;
    }

    // Clear all entries
    void clear() {
        keys.clear();
        values.clear();
    }

    // Get number of entries
    size_t size() const {
        return keys.size();
    }

    // Check if empty
    bool empty() const {
        return keys.empty();
    }

    // Reserve capacity (for stack version, just checks bounds)
    void reserve(size_t new_capacity) {
        keys.reserve(new_capacity);
        values.reserve(new_capacity);
    }

    // Get all keys
    Vec<K, stack_size_tag<StackSize>, InitialCapacity>* get_keys() {
        return &keys;
    }

    const Vec<K, stack_size_tag<StackSize>, InitialCapacity>* get_keys() const {
        return &keys;
    }

    // Get all values
    Vec<V, stack_size_tag<StackSize>, InitialCapacity>* get_values() {
        return &values;
    }

    const Vec<V, stack_size_tag<StackSize>, InitialCapacity>* get_values() const {
        return &values;
    }

    // Iterator support
    struct Entry {
        K* key;
        V* value;
    };

    Entry get_entry(size_t index) {
        if (index < keys.size()) {
            return { &keys[index], &values[index] };
        }
        return { nullptr, nullptr };
    }

private:
    template <typename U = K>
    typename std::enable_if<has_equality<U>::value, int>::type
    find_key_index(const K& key) const {
        return keys.find(key);
    }

    template <typename U = K>
    typename std::enable_if<!has_equality<U>::value, int>::type
    find_key_index(const K& key) const {
        return -1;
    }
};

// ========================================================================
// Map with custom key comparator
// ========================================================================

template <typename K, typename V, typename ArenaTag, typename KeyEqual,
          size_t InitialCapacity = 16>
class MapWithComparator {
    Vec<K, ArenaTag, InitialCapacity>* keys;
    Vec<V, ArenaTag, InitialCapacity>* values;
    KeyEqual eq;

public:
    MapWithComparator(KeyEqual eq_fn = KeyEqual{}) : keys(nullptr), values(nullptr), eq(eq_fn) {}

    static MapWithComparator* create(size_t initial_capacity = 16,
                                     KeyEqual eq_fn = KeyEqual{}) {
        auto* map = (MapWithComparator*)Arena<ArenaTag>::alloc(sizeof(MapWithComparator));
        new (map) MapWithComparator(eq_fn);

        map->keys = Vec<K, ArenaTag, InitialCapacity>::create(initial_capacity);
        map->values = Vec<V, ArenaTag, InitialCapacity>::create(initial_capacity);

        return map;
    }

    void insert(const K& key, const V& value) {
        int index = find_key_index(key);

        if (index >= 0) {
            (*values)[index] = value;
        } else {
            keys->push_back(key);
            values->push_back(value);
        }
    }

    V* get(const K& key) {
        int index = find_key_index(key);
        if (index >= 0) {
            return &(*values)[index];
        }
        return nullptr;
    }

    V& operator[](const K& key) {
        int index = find_key_index(key);

        if (index < 0) {
            keys->push_back(key);
            values->push_back(V{});
            return values->back();
        }

        return (*values)[index];
    }

    bool contains(const K& key) const {
        return find_key_index(key) >= 0;
    }

    bool remove(const K& key) {
        int index = find_key_index(key);
        if (index >= 0) {
            keys->swap_remove(index);
            values->swap_remove(index);
            return true;
        }
        return false;
    }

    size_t size() const { return keys->size(); }
    bool empty() const { return keys->empty(); }
    void clear() { keys->clear(); values->clear(); }

    // Get all keys (returns the Vec directly for iteration)
    Vec<K, ArenaTag, InitialCapacity>* get_keys() {
        return keys;
    }

    const Vec<K, ArenaTag, InitialCapacity>* get_keys() const {
        return keys;
    }

    // Get all values (returns the Vec directly for iteration)
    Vec<V, ArenaTag, InitialCapacity>* get_values() {
        return values;
    }

    const Vec<V, ArenaTag, InitialCapacity>* get_values() const {
        return values;
    }

private:
    int find_key_index(const K& key) const {
        if (!keys) return -1;

        auto pred = [this, &key](const K& elem) {
            return eq(elem, key);
        };
        return keys->find_with(pred);
    }
};

// String comparison functor for const char* keys
struct StringKeyEqual {
    bool operator()(const char* a, const char* b) const {
        if (a == b) return true;  // Same pointer
        if (!a || !b) return false;  // One is null
        return strcmp(a, b) == 0;
    }
};

// Specialization for const char* keys - uses string comparison
template <typename V, typename ArenaTag, size_t InitialCapacity>
class Map<const char*, V, ArenaTag, InitialCapacity> {
    Vec<const char*, ArenaTag, InitialCapacity>* keys;
    Vec<V, ArenaTag, InitialCapacity>* values;

public:
    Map() : keys(nullptr), values(nullptr) {}

    static Map* create(size_t initial_capacity = 16) {
        Map* map = (Map*)Arena<ArenaTag>::alloc(sizeof(Map));
        new (map) Map();

        map->keys = Vec<const char*, ArenaTag, InitialCapacity>::create(initial_capacity);
        map->values = Vec<V, ArenaTag, InitialCapacity>::create(initial_capacity);

        return map;
    }

    void insert(const char* key, const V& value) {
        int index = find_key_index(key);

        if (index >= 0) {
            (*values)[index] = value;
        } else {
            // Note: We store the pointer directly - caller must ensure string lifetime!
            keys->push_back(key);
            values->push_back(value);
        }
    }

    V* get(const char* key) {
        int index = find_key_index(key);
        if (index >= 0) {
            return &(*values)[index];
        }
        return nullptr;
    }

    const V* get(const char* key) const {
        int index = find_key_index(key);
        if (index >= 0) {
            return &(*values)[index];
        }
        return nullptr;
    }

    const char* get_key(size_t index) const {
        if (index < keys->size()) {
            return (*keys)[index];
        }
        return nullptr;
    }

    V* get_value(size_t index) {
        if (index < values->size()) {
            return &(*values)[index];
        }
        return nullptr;
    }

    V& operator[](const char* key) {
        int index = find_key_index(key);

        if (index < 0) {
            keys->push_back(key);
            values->push_back(V{});
            return values->back();
        }

        return (*values)[index];
    }

    bool contains(const char* key) const {
        return find_key_index(key) >= 0;
    }

    bool remove(const char* key) {
        int index = find_key_index(key);
        if (index >= 0) {
            keys->swap_remove(index);
            values->swap_remove(index);
            return true;
        }
        return false;
    }

    void clear() {
        keys->clear();
        values->clear();
    }

    size_t size() const { return keys->size(); }
    bool empty() const { return keys->empty(); }

    void reserve(size_t new_capacity) {
        keys->reserve(new_capacity);
        values->reserve(new_capacity);
    }

    Vec<const char*, ArenaTag, InitialCapacity>* get_keys() { return keys; }
    const Vec<const char*, ArenaTag, InitialCapacity>* get_keys() const { return keys; }

    Vec<V, ArenaTag, InitialCapacity>* get_values() { return values; }
    const Vec<V, ArenaTag, InitialCapacity>* get_values() const { return values; }

private:
    int find_key_index(const char* key) const {
        if (!keys || !key) return -1;

        for (size_t i = 0; i < keys->size(); i++) {
            const char* stored_key = (*keys)[i];
            if (stored_key == key) return i;  // Same pointer
            if (stored_key && strcmp(stored_key, key) == 0) return i;  // Same string
        }
        return -1;
    }
};

// Convenience factory functions
template <typename K, typename V, size_t N>
using EmbMap = Map<K, V, stack_size_tag<N>>;

// Convenience for string-keyed maps using the comparator version
template <typename V, typename ArenaTag>
using StringMap = MapWithComparator<const char*, V, ArenaTag, StringKeyEqual>;

// Usage examples:
// EmbMap<int, float, 100> stack_map;           // 100 entries on stack
// Map<int, std::string, MyArena> arena_map;    // Arena allocated
//
// For const char* keys (automatic string comparison):
// Map<const char*, int, MyArena>* string_map = Map<const char*, int, MyArena>::create();
// string_map->insert("hello", 42);
//
// Alternative using StringMap alias:
// StringMap<int, MyArena>* str_map = StringMap<int, MyArena>::create();
//
// For custom key types without operator==:
// struct MyKeyEqual {
//     bool operator()(const MyKey& a, const MyKey& b) const {
//         return a.id == b.id;
//     }
// };
// MapWithComparator<MyKey, int, MyArena, MyKeyEqual> custom_map;
