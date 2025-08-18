// arena.h
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

void arena_init(size_t capacity);
void arena_shutdown(void);
void* arena_alloc(size_t size);
void arena_reset(void);
size_t arena_used(void);

// Convenience macro for typed allocation
#define ARENA_ALLOC(type) ((type*)arena_alloc(sizeof(type)))
#define ARENA_ALLOC_ARRAY(type, count) ((type*)arena_alloc(sizeof(type) * (count)))


// arena_containers.hpp
#pragma once
#include "arena.hpp"
#include <cstring>

// Simple arena-based map using linear search
// Just use it directly - allocates on first use
template<typename K, typename V>
struct ArenaMap {
    struct Entry {
        K key;
        V value;
        bool used;
    };

    Entry* entries = nullptr;
    size_t capacity = 0;
    size_t count = 0;

    V* find(const K& key) {
        if (!entries) return nullptr;

        for (size_t i = 0; i < capacity; i++) {
            if (entries[i].used && entries[i].key == key) {
                return &entries[i].value;
            }
        }
        return nullptr;
    }

    void insert(const K& key, const V& value) {
        // Lazy allocation
        if (!entries) {
            capacity = 64;
            entries = (Entry*)arena_alloc(sizeof(Entry) * capacity);
            memset(entries, 0, sizeof(Entry) * capacity);
        }

        // Check if exists
        for (size_t i = 0; i < capacity; i++) {
            if (entries[i].used && entries[i].key == key) {
                entries[i].value = value;
                return;
            }
        }

        // Find empty slot
        for (size_t i = 0; i < capacity; i++) {
            if (!entries[i].used) {
                entries[i].key = key;
                entries[i].value = value;
                entries[i].used = true;
                count++;
                return;
            }
        }
    }

    V& operator[](const K& key) {
        // Lazy allocation
        if (!entries) {
            capacity = 64;
            entries = (Entry*)arena_alloc(sizeof(Entry) * capacity);
            memset(entries, 0, sizeof(Entry) * capacity);
        }

        // Find existing
        for (size_t i = 0; i < capacity; i++) {
            if (entries[i].used && entries[i].key == key) {
                return entries[i].value;
            }
        }

        // Create new
        for (size_t i = 0; i < capacity; i++) {
            if (!entries[i].used) {
                entries[i].key = key;
                entries[i].used = true;
                count++;
                return entries[i].value;
            }
        }

        // Should not reach here if capacity is sufficient
        return entries[0].value;
    }

    void erase(const K& key) {
        if (!entries) return;

        for (size_t i = 0; i < capacity; i++) {
            if (entries[i].used && entries[i].key == key) {
                entries[i].used = false;
                count--;
                return;
            }
        }
    }

    void clear() {
        if (entries) {
            memset(entries, 0, sizeof(Entry) * capacity);
            count = 0;
        }
    }

    bool empty() const { return count == 0; }
    size_t size() const { return count; }
};

// Simple arena-based queue - circular buffer
template<typename T>
struct ArenaQueue {
    T* data = nullptr;
    size_t capacity = 0;
    size_t head = 0;
    size_t tail = 0;
    size_t count = 0;

    void push(const T& item) {
        // Lazy allocation
        if (!data) {
            capacity = 256;
            data = (T*)arena_alloc(sizeof(T) * capacity);
        }

        if (count >= capacity) return; // Full

        data[tail] = item;
        tail = (tail + 1) % capacity;
        count++;
    }

    T front() {
        if (count == 0) return T{};
        return data[head];
    }

    void pop() {
        if (count == 0) return;
        head = (head + 1) % capacity;
        count--;
    }

    bool empty() const { return count == 0; }
    size_t size() const { return count; }

    void clear() {
        head = 0;
        tail = 0;
        count = 0;
    }
};

// Simple arena vector - just for completeness
template<typename T>
struct ArenaVector{
    T* data = nullptr;
    size_t capacity = 0;
    size_t count = 0;

    void push_back(const T& item) {
        if (!data || count >= capacity) {
            size_t new_capacity = capacity ? capacity * 2 : 16;
            T* new_data = (T*)arena_alloc(sizeof(T) * new_capacity);
            if (data) {
                memcpy(new_data, data, sizeof(T) * count);
            }
            data = new_data;
            capacity = new_capacity;
        }
        data[count++] = item;
    }

    T& operator[](size_t i) { return data[i]; }
    const T& operator[](size_t i) const { return data[i]; }

    size_t size() const { return count; }
    bool empty() const { return count == 0; }
    void clear() { count = 0; }

    T* begin() { return data; }
    T* end() { return data + count; }
};



// Simple arena string
struct ArenaString {
    char* data = nullptr;
    size_t len = 0;

    // Default constructor
    ArenaString() = default;

    // From C string
    ArenaString(const char* str) {
        if (str) {
            len = strlen(str);
            data = (char*)arena_alloc(len + 1);
            memcpy(data, str, len + 1);
        }
    }

    // From std::string (for migration)
    ArenaString(const std::string& str) {
        len = str.length();
        data = (char*)arena_alloc(len + 1);
        memcpy(data, str.c_str(), len + 1);
    }

    // Copy constructor
    ArenaString(const ArenaString& other) {
        if (other.data) {
            len = other.len;
            data = (char*)arena_alloc(len + 1);
            memcpy(data, other.data, len + 1);
        }
    }

    // Assignment
    ArenaString& operator=(const char* str) {
        if (str) {
            len = strlen(str);
            data = (char*)arena_alloc(len + 1);
            memcpy(data, str, len + 1);
        } else {
            data = nullptr;
            len = 0;
        }
        return *this;
    }

    ArenaString& operator=(const ArenaString& other) {
        if (other.data) {
            len = other.len;
            data = (char*)arena_alloc(len + 1);
            memcpy(data, other.data, len + 1);
        } else {
            data = nullptr;
            len = 0;
        }
        return *this;
    }

    // Concatenation
    ArenaString operator+(const ArenaString& other) const {
        ArenaString result;
        result.len = len + other.len;
        result.data = (char*)arena_alloc(result.len + 1);
        if (data) memcpy(result.data, data, len);
        if (other.data) memcpy(result.data + len, other.data, other.len);
        result.data[result.len] = '\0';
        return result;
    }

    ArenaString operator+(const char* str) const {
        size_t str_len = str ? strlen(str) : 0;
        ArenaString result;
        result.len = len + str_len;
        result.data = (char*)arena_alloc(result.len + 1);
        if (data) memcpy(result.data, data, len);
        if (str) memcpy(result.data + len, str, str_len);
        result.data[result.len] = '\0';
        return result;
    }

    // Comparison
    bool operator==(const ArenaString& other) const {
        if (len != other.len) return false;
        if (!data || !other.data) return data == other.data;
        return memcmp(data, other.data, len) == 0;
    }

    bool operator==(const char* str) const {
        if (!data) return !str || *str == '\0';
        if (!str) return false;
        return strcmp(data, str) == 0;
    }

    bool operator!=(const ArenaString& other) const { return !(*this == other); }
    bool operator!=(const char* str) const { return !(*this == str); }

    // For use in map
    bool operator<(const ArenaString& other) const {
        if (!data || !other.data) return data < other.data;
        return strcmp(data, other.data) < 0;
    }

    // Access
    const char* c_str() const { return data ? data : ""; }
    size_t length() const { return len; }
    size_t size() const { return len; }
    bool empty() const { return len == 0; }

    char& operator[](size_t i) { return data[i]; }
    const char& operator[](size_t i) const { return data[i]; }
};
