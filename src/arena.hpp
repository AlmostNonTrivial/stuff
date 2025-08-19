#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// Tagged arena system - each arena type gets its own memory pool
template<typename Tag>
struct Arena {
    static inline uint8_t* base = nullptr;
    static inline uint8_t* current = nullptr;
    static inline size_t capacity = 0;

    // Initialize arena with given capacity
    static void init(size_t cap) {
        if (base) return; // Already initialized

        base = (uint8_t*)malloc(cap);
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
    static void* alloc(size_t size) {
        // Lazy init with default size if needed
        if (!base) {
            init(64 * 1024 * 1024); // 64MB default
        }

        // Align to 16 bytes
        size_t align = 16;
        uintptr_t current_addr = (uintptr_t)current;
        uintptr_t aligned_addr = (current_addr + align - 1) & ~(align - 1);

        uint8_t* aligned = (uint8_t*)aligned_addr;
        uint8_t* next = aligned + size;

        if (next > base + capacity) {
            fprintf(stderr, "Arena exhausted: requested %zu, used %zu/%zu\n",
                    size, used(), capacity);
            return nullptr;
        }

        current = next;
        return aligned;
    }

    // Reset arena (move pointer back to start)
    static void reset() {
        current = base;
    }

    // Get used memory
    static size_t used() {
        return current - base;
    }
};

// Convenience namespace for arena operations
namespace arena {
    template<typename Tag>
    void init(size_t capacity) {
        Arena<Tag>::init(capacity);
    }

    template<typename Tag>
    void shutdown() {
        Arena<Tag>::shutdown();
    }

    template<typename Tag>
    void reset() {
        Arena<Tag>::reset();
    }

    template<typename Tag>
    void* alloc(size_t size) {
        return Arena<Tag>::alloc(size);
    }

    template<typename Tag>
    size_t used() {
        return Arena<Tag>::used();
    }
}

// Arena-based vector
template<typename T, typename ArenaTag, size_t InitialCapacity = 16>
struct ArenaVector {
    T* data;
    size_t capacity;
    size_t count;

    // Constructor - automatically initializes
    ArenaVector() : data(nullptr), capacity(0), count(0) {}

    void push_back(const T& item) {
        if (!data || count >= capacity) {
            size_t new_capacity = capacity ? capacity * 2 : InitialCapacity;
            T* new_data = (T*)Arena<ArenaTag>::alloc(sizeof(T) * new_capacity);
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


    void erase(const T&item) {
       int position = this->find(item);
       if(position == -1) {
           return;
       }

       this->data[position] = this->data[this->count - 1];
       this->count--;
    }

    void insert_unique(const T& item) {
        if(this->find(item) == -1) {
            this->push_back(item);
        }
    }

    void pop_back() {
        if (count > 0) count--;
    }

    T& back() {
        return data[count - 1];
    }

    const T& back() const {
        return data[count - 1];
    }

    T& operator[](size_t i) {
        return data[i];
    }

    const T& operator[](size_t i) const {
        return data[i];
    }

    size_t size() const {
        return count;
    }

    bool empty() const {
        return count == 0;
    }

    void clear() {
        count = 0;
    }

    void reserve(size_t new_capacity) {
        if (new_capacity > capacity) {
            T* new_data = (T*)Arena<ArenaTag>::alloc(sizeof(T) * new_capacity);
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

    T* begin() { return data; }
    T* end() { return data + count; }
    const T* begin() const { return data; }
    const T* end() const { return data + count; }

    // Find element and return index (-1 if not found)
    int find(const T& value) const {
        for (size_t i = 0; i < count; i++) {
            if (data[i] == value) {
                return (int)i;
            }
        }
        return -1;
    }

    // Find element with custom predicate and return index (-1 if not found)
    template<typename Predicate>
    int find_if(Predicate pred) const {
        for (size_t i = 0; i < count; i++) {
            if (pred(data[i])) {
                return (int)i;
            }
        }
        return -1;
    }
};

// Arena-based string
template<typename ArenaTag, size_t InitialCapacity = 32>
struct ArenaString {
    char* data;
    size_t len;
    size_t capacity;

    // Default constructor
    ArenaString() : data(nullptr), len(0), capacity(0) {}

    // From C string
    ArenaString(const char* str) : data(nullptr), len(0), capacity(0) {
        if (str) {
            assign(str);
        }
    }

    // Copy constructor
    ArenaString(const ArenaString& other) : data(nullptr), len(0), capacity(0) {
        if (other.data) {
            assign(other.data);
        }
    }

    void assign(const char* str) {
        if (!str) {
            data = nullptr;
            len = 0;
            capacity = 0;
            return;
        }

        len = strlen(str);
        if (!data || len + 1 > capacity) {
            capacity = len + 1 > InitialCapacity ? len + 1 : InitialCapacity;
            data = (char*)Arena<ArenaTag>::alloc(capacity);
        }
        if (data) {
            memcpy(data, str, len + 1);
        }
    }

    // Assignment operators
    ArenaString& operator=(const char* str) {
        assign(str);
        return *this;
    }

    ArenaString& operator=(const ArenaString& other) {
        if (this != &other && other.data) {
            assign(other.data);
        }
        return *this;
    }

    // Append
    void append(const char* str) {
        if (!str) return;

        size_t str_len = strlen(str);
        size_t new_len = len + str_len;

        if (!data || new_len + 1 > capacity) {
            size_t new_capacity = capacity ? capacity * 2 : InitialCapacity;
            while (new_capacity < new_len + 1) {
                new_capacity *= 2;
            }

            char* new_data = (char*)Arena<ArenaTag>::alloc(new_capacity);
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

    void append(const ArenaString& other) {
        if (other.data) {
            append(other.data);
        }
    }

    // Concatenation operators
    ArenaString operator+(const char* str) const {
        ArenaString result(*this);
        result.append(str);
        return result;
    }

    ArenaString operator+(const ArenaString& other) const {
        ArenaString result(*this);
        result.append(other);
        return result;
    }

    ArenaString& operator+=(const char* str) {
        append(str);
        return *this;
    }

    ArenaString& operator+=(const ArenaString& other) {
        append(other);
        return *this;
    }

    ArenaString& operator+=(char c) {
        append(c);
        return *this;
    }

    // Access
    char& operator[](size_t i) {
        return data[i];
    }

    const char& operator[](size_t i) const {
        return data[i];
    }

    const char* c_str() const {
        return data ? data : "";
    }

    size_t length() const {
        return len;
    }

    size_t size() const {
        return len;
    }

    bool empty() const {
        return len == 0;
    }

    void clear() {
        len = 0;
        if (data) {
            data[0] = '\0';
        }
    }

    // Comparison operators
    bool operator==(const char* str) const {
        if (!data && !str) return true;
        if (!data || !str) return false;
        return strcmp(data, str) == 0;
    }

    bool operator==(const ArenaString& other) const {
        if (!data && !other.data) return true;
        if (!data || !other.data) return false;
        return strcmp(data, other.data) == 0;
    }

    bool operator!=(const char* str) const {
        return !(*this == str);
    }

    bool operator!=(const ArenaString& other) const {
        return !(*this == other);
    }

    bool operator<(const ArenaString& other) const {
        if (!data && !other.data) return false;
        if (!data) return true;
        if (!other.data) return false;
        return strcmp(data, other.data) < 0;
    }

    // Check if string contains substring
    bool contains(const char* substr) const {
        if (!data || !substr) return false;
        return strstr(data, substr) != nullptr;
    }

    bool contains(const ArenaString& substr) const {
        if (!data || !substr.data) return false;
        return strstr(data, substr.data) != nullptr;
    }

    // Find substring and return position (-1 if not found)
    int find(const char* substr) const {
        if (!data || !substr) return -1;
        const char* pos = strstr(data, substr);
        if (pos) {
            return (int)(pos - data);
        }
        return -1;
    }

    int find(const ArenaString& substr) const {
        return find(substr.c_str());
    }

    // Check if string starts with prefix
    bool starts_with(const char* prefix) const {
        if (!data || !prefix) return false;
        size_t prefix_len = strlen(prefix);
        if (prefix_len > len) return false;
        return strncmp(data, prefix, prefix_len) == 0;
    }

    bool starts_with(const ArenaString& prefix) const {
        return starts_with(prefix.c_str());
    }

    // Check if string ends with suffix
    bool ends_with(const char* suffix) const {
        if (!data || !suffix) return false;
        size_t suffix_len = strlen(suffix);
        if (suffix_len > len) return false;
        return strcmp(data + len - suffix_len, suffix) == 0;
    }

    bool ends_with(const ArenaString& suffix) const {
        return ends_with(suffix.c_str());
    }
};
