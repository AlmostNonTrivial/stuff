// arena.h
#pragma once
#include <stddef.h>
#include <stdint.h>

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
