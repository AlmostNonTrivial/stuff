#ifndef FIXED_CONTAINERS_HPP
#define FIXED_CONTAINERS_HPP

#include <cstddef>
#include <stdexcept>

// Fixed-size Map class (key-value pairs, linear search)
template <typename K, typename V, size_t MaxSize>
class FixedMap {
private:
    struct Pair {
        K key{};
        V value{};
        bool occupied = false;
    };
    Pair data[MaxSize]{};
    size_t count = 0;

public:
    // Insert or update key-value pair
    bool insert(const K& key, const V& value) {
        // Check if key already exists
        for (size_t i = 0; i < count; ++i) {
            if (data[i].occupied && data[i].key == key) {
                data[i].value = value;
                return true;
            }
        }
        // Add new pair if there's space
        if (count < MaxSize) {
            data[count].key = key;
            data[count].value = value;
            data[count].occupied = true;
            ++count;
            return true;
        }
        return false; // Map is full
    }

    // Access value by key, throws if not found
    V& operator[](const K& key) {
        for (size_t i = 0; i < count; ++i) {
            if (data[i].occupied && data[i].key == key) {
                return data[i].value;
            }
        }
        throw std::out_of_range("Key not found");
    }

    // Check if key exists
    bool contains(const K& key) const {
        for (size_t i = 0; i < count; ++i) {
            if (data[i].occupied && data[i].key == key) {
                return true;
            }
        }
        return false;
    }

    // Remove key-value pair
    bool remove(const K& key) {
        for (size_t i = 0; i < count; ++i) {
            if (data[i].occupied && data[i].key == key) {
                data[i].occupied = false;
                // Shift elements to maintain contiguous occupied pairs
                for (size_t j = i; j < count - 1; ++j) {
                    data[j] = data[j + 1];
                }
                data[count - 1].occupied = false;
                --count;
                return true;
            }
        }
        return false;
    }

    size_t size() const { return count; }
    bool empty() const { return count == 0; }
    constexpr size_t max_size() const { return MaxSize; }
};

// Fixed-size Vector class (dynamic array)
template <typename T, size_t MaxSize>
class FixedVector {
private:
    T data[MaxSize]{};
    size_t count = 0;

public:
    // Add element to the end
    bool push_back(const T& value) {
        if (count < MaxSize) {
            data[count++] = value;
            return true;
        }
        return false; // Vector is full
    }

    // Access element by index, throws if out of bounds
    T& operator[](size_t index) {
        if (index >= count) {
            throw std::out_of_range("Index out of bounds");
        }
        return data[index];
    }

    const T& operator[](size_t index) const {
        if (index >= count) {
            throw std::out_of_range("Index out of bounds");
        }
        return data[index];
    }

    // Remove last element
    bool pop_back() {
        if (count > 0) {
            --count;
            return true;
        }
        return false;
    }

    size_t size() const { return count; }
    bool empty() const { return count == 0; }
    constexpr size_t max_size() const { return MaxSize; }
};

// Fixed-size Queue class (circular buffer)
template <typename T, size_t MaxSize>
class FixedQueue {
private:
    T data[MaxSize]{};
    size_t head = 0;
    size_t tail = 0;
    size_t count = 0;

public:
    // Add element to the back
    bool enqueue(const T& value) {
        if (count < MaxSize) {
            data[tail] = value;
            tail = (tail + 1) % MaxSize;
            ++count;
            return true;
        }
        return false; // Queue is full
    }

    // Remove element from the front
    bool dequeue(T& value) {
        if (count > 0) {
            value = data[head];
            head = (head + 1) % MaxSize;
            --count;
            return true;
        }
        return false; // Queue is empty
    }

    // Access front element, throws if empty
    T& front() {
        if (count == 0) {
            throw std::out_of_range("Queue is empty");
        }
        return data[head];
    }

    size_t size() const { return count; }
    bool empty() const { return count == 0; }
    constexpr size_t max_size() const { return MaxSize; }
};

#endif // FIXED_CONTAINERS_HPP
