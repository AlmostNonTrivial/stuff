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


/// Simple arena-based vector - self-initializing
template<typename T>
struct ArenaVec {
    T* data;
    size_t size;
    size_t capacity;

    void push_back(T value) {
        // Self-initialize on first use if needed
        // arena_alloc gives zeroed memory, so capacity will be 0
        if (size >= capacity) {
            // Grow by doubling (or initial size of 4)
            size_t new_capacity = capacity == 0 ? 4 : capacity * 2;
            T* new_data = ARENA_ALLOC_ARRAY(T, new_capacity);

            // Copy old data if any
            if (data && size > 0) {
                memcpy(new_data, data, size * sizeof(T));
            }

            data = new_data;
            capacity = new_capacity;
        }

        data[size++] = value;
    }

    T& operator[](size_t i) { return data[i]; }
    const T& operator[](size_t i) const { return data[i]; }

    bool empty() const { return size == 0; }
};
