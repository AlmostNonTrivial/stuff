// vec.hpp - Complete Vector implementation with hybrid stack/arena support

#pragma once
#include "arena.hpp"
#include <type_traits>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// Type trait helpers for operator detection
template <typename T> struct has_equality {
	template <typename U>
	static auto
	test(int) -> decltype(std::declval<U>() == std::declval<U>(),
			      std::true_type{});
	template <typename>
	static std::false_type
	test(...);
	static constexpr bool value = decltype(test<T>(0))::value;
};

template <typename T> struct has_less_than {
	template <typename U>
	static auto
	test(int) -> decltype(std::declval<U>() < std::declval<U>(),
			      std::true_type{});
	template <typename>
	static std::false_type
	test(...);
	static constexpr bool value = decltype(test<T>(0))::value;
};

// Helper to detect if type is an arena tag
template <typename T, typename = void> struct is_arena_tag : std::false_type {
};

template <typename T>
struct is_arena_tag<T, std::void_t<decltype(Arena<T>::alloc(0))>>
    : std::true_type {
};

// Tag for stack allocation with size
template <size_t N> struct stack_size_tag {
	static constexpr size_t value = N;
};

// Primary template - Arena allocation
template <typename T, typename ArenaTag, size_t InitialCapacity = 16> class Vec
{
	static_assert(is_arena_tag<ArenaTag>::value,
		      "Second parameter must be an Arena tag");

	T *data;
	size_t capacity;
	size_t count;

	// Queue state - using circular buffer
	size_t head; // First element position for queue ops
	size_t tail; // Next insert position for queue ops
	bool is_queue_mode;

      public:
	// Constructor
	Vec()
	    : data(nullptr), capacity(0), count(0), head(0), tail(0),
	      is_queue_mode(false)
	{
	}

	// ========================================================================
	// Basic Operations
	// ========================================================================

	void
	reserve(size_t new_capacity)
	{
		if (new_capacity <= capacity)
			return;

		T *new_data =
		    (T *)Arena<ArenaTag>::alloc(sizeof(T) * new_capacity);

		if (data && new_data) {
			if (is_queue_mode && head != 0) {
				// Unwrap circular buffer when reallocating
				size_t first_part = capacity - head;
				memcpy(new_data, data + head,
				       first_part * sizeof(T));
				if (tail > 0) {
					memcpy(new_data + first_part, data,
					       tail * sizeof(T));
				}
				head = 0;
				tail = count;
			} else {
				memcpy(new_data, data, sizeof(T) * count);
			}
		}

		data = new_data;
		capacity = new_capacity;
	}

	void
	clear()
	{
		data = nullptr;
		capacity = 0;
		count = 0;
		head = 0;
		tail = 0;
		is_queue_mode = false;
	}

	size_t
	size() const
	{
		return count;
	}
	bool
	empty() const
	{
		return count == 0;
	}

	T &
	operator[](size_t i)
	{
		if (is_queue_mode) {
			return data[(head + i) % capacity];
		}
		return data[i];
	}

	const T &
	operator[](size_t i) const
	{
		if (is_queue_mode) {
			return data[(head + i) % capacity];
		}
		return data[i];
	}

	// ========================================================================
	// Stack Operations (always at the back)
	// ========================================================================

	void
	push_back(const T &item)
	{
		if (is_queue_mode) {
			// Queue mode push
			if (count >= capacity) {
				reserve(capacity ? capacity * 2
						 : InitialCapacity);
			}
			data[tail] = item;
			tail = (tail + 1) % capacity;
			count++;
		} else {
			// Normal mode
			if (!data || count >= capacity) {
				reserve(capacity ? capacity * 2
						 : InitialCapacity);
			}
			data[count++] = item;
		}
	}

	void
	pop_back()
	{
		if (count == 0)
			return;

		if (is_queue_mode) {
			tail = (tail + capacity - 1) % capacity;
		}
		count--;
	}

	T &
	back()
	{
		if (is_queue_mode) {
			return data[(tail + capacity - 1) % capacity];
		}
		return data[count - 1];
	}

	const T &
	back() const
	{
		if (is_queue_mode) {
			return data[(tail + capacity - 1) % capacity];
		}
		return data[count - 1];
	}

	// ========================================================================
	// Queue Operations (circular buffer for O(1) operations)
	// ========================================================================

	void
	enable_queue_mode()
	{
		if (is_queue_mode)
			return;
		is_queue_mode = true;
		head = 0;
		tail = count;
	}

	void
	push_front(const T &item)
	{
		if (!is_queue_mode)
			enable_queue_mode();

		if (count >= capacity) {
			reserve(capacity ? capacity * 2 : InitialCapacity);
		}

		head = (head + capacity - 1) % capacity;
		data[head] = item;
		count++;
	}

	T
	pop_front()
	{
		if (count == 0)
			return T{};

		if (!is_queue_mode)
			enable_queue_mode();

		T result = data[head];
		head = (head + 1) % capacity;
		count--;

		if (count == 0) {
			head = tail = 0;
		}

		return result;
	}

	T &
	front()
	{
		if (is_queue_mode) {
			return data[head];
		}
		return data[0];
	}

	const T &
	front() const
	{
		if (is_queue_mode) {
			return data[head];
		}
		return data[0];
	}

	// ========================================================================
	// Set Operations - Auto-detect operators for primitives
	// ========================================================================

	// Find with operator== (for types that have it)
	template <typename U = T>
	typename std::enable_if<has_equality<U>::value, int>::type
	find(const T &value) const
	{
		for (size_t i = 0; i < count; i++) {
			if ((*this)[i] == value)
				return (int)i;
		}
		return -1;
	}

	// Find with custom comparator (for structs)
	template <typename EqualFn>
	int
	find_with(EqualFn eq) const
	{
		for (size_t i = 0; i < count; i++) {
			if (eq((*this)[i]))
				return (int)i;
		}
		return -1;
	}

	// Contains with operator==
	template <typename U = T>
	typename std::enable_if<has_equality<U>::value, bool>::type
	contains(const T &value) const
	{
		return find(value) != -1;
	}

	// Contains with comparator
	template <typename EqualFn>
	bool
	contains_with(EqualFn eq) const
	{
		return find_with(eq) != -1;
	}

	// Insert unique with operator==
	template <typename U = T>
	typename std::enable_if<has_equality<U>::value, void>::type
	insert_unique(const T &item)
	{
		if (!contains(item)) {
			push_back(item);
		}
	}

	// Insert unique with comparator
	template <typename EqualFn>
	void
	insert_unique_with(const T &item, EqualFn eq)
	{
		auto pred = [&item, &eq](const T &elem) {
			return eq(elem, item);
		};
		if (!contains_with(pred)) {
			push_back(item);
		}
	}

	// Erase element (unordered - O(1) using swap with last)
	template <typename U = T>
	typename std::enable_if<has_equality<U>::value, void>::type
	erase(const T &item)
	{
		int pos = find(item);
		if (pos != -1) {
			swap_remove(pos);
		}
	}

	// Erase with comparator
	template <typename EqualFn>
	void
	erase_with(EqualFn eq)
	{
		int pos = find_with(eq);
		if (pos != -1) {
			swap_remove(pos);
		}
	}

	// ========================================================================
	// Sorted Operations - Auto-detect operator< for primitives
	// ========================================================================

	// Binary search with operator
	template <typename U = T>
	typename std::enable_if<
	    has_less_than<U>::value && has_equality<U>::value, int>::type
	binary_search(const T &value) const
	{
		int left = 0, right = count - 1;
		while (left <= right) {
			int mid = left + (right - left) / 2;
			if ((*this)[mid] == value)
				return mid;
			if ((*this)[mid] < value)
				left = mid + 1;
			else
				right = mid - 1;
		}
		return -1;
	}

	// Binary search with comparator
	template <typename CompareFn>
	int
	binary_search_with(const T &value, CompareFn cmp) const
	{
		int left = 0, right = count - 1;
		while (left <= right) {
			int mid = left + (right - left) / 2;
			int result = cmp((*this)[mid], value);
			if (result == 0)
				return mid;
			if (result < 0)
				left = mid + 1;
			else
				right = mid - 1;
		}
		return -1;
	}

	// Insert sorted with operator
	template <typename U = T>
	typename std::enable_if<has_less_than<U>::value, void>::type
	insert_sorted(const T &item)
	{
		size_t pos = 0;
		while (pos < count && (*this)[pos] < item)
			pos++;
		insert(pos, item);
	}

	// Insert sorted with comparator
	template <typename CompareFn>
	void
	insert_sorted_with(const T &item, CompareFn cmp)
	{
		size_t pos = 0;
		while (pos < count && cmp((*this)[pos], item) < 0)
			pos++;
		insert(pos, item);
	}

	// Sort with operator
	template <typename U = T>
	typename std::enable_if<has_less_than<U>::value, void>::type
	sort()
	{
		if (count <= 1)
			return;
		quicksort(0, count - 1);
	}

	// Sort with comparator
	template <typename CompareFn>
	void
	sort_with(CompareFn cmp)
	{
		if (count <= 1)
			return;
		quicksort_with(0, count - 1, cmp);
	}

	// ========================================================================
	// General Utilities
	// ========================================================================

	// O(1) removal by swapping with last element
	void
	swap_remove(size_t index)
	{
		if (index >= count)
			return;
		(*this)[index] = (*this)[count - 1];
		count--;
	}

	// O(n) ordered removal
	void
	remove(size_t index)
	{
		if (index >= count)
			return;

		if (is_queue_mode) {
			// Complex for circular buffer, just swap remove
			swap_remove(index);
		} else {
			memmove(data + index, data + index + 1,
				(count - index - 1) * sizeof(T));
			count--;
		}
	}

	// O(n) ordered insert
	void
	insert(size_t index, const T &item)
	{
		if (index > count)
			index = count;

		if (is_queue_mode) {
			// For queue mode, convert to normal mode first
			normalize();
		}

		if (count >= capacity) {
			reserve(capacity ? capacity * 2 : InitialCapacity);
		}

		memmove(data + index + 1, data + index,
			(count - index) * sizeof(T));
		data[index] = item;
		count++;
	}

	// Swap two elements
	void
	swap(size_t i, size_t j)
	{
		T tmp = (*this)[i];
		(*this)[i] = (*this)[j];
		(*this)[j] = tmp;
	}

	// Reverse the vector
	void
	reverse()
	{
		for (size_t i = 0; i < count / 2; i++) {
			swap(i, count - 1 - i);
		}
	}

	// Remove duplicates with operator==
	template <typename U = T>
	typename std::enable_if<has_equality<U>::value, void>::type
	deduplicate()
	{
		if (count <= 1)
			return;

		size_t write_pos = 0;
		for (size_t i = 0; i < count; i++) {
			bool found = false;
			for (size_t j = 0; j < write_pos; j++) {
				if ((*this)[j] == (*this)[i]) {
					found = true;
					break;
				}
			}
			if (!found) {
				(*this)[write_pos++] = (*this)[i];
			}
		}
		count = write_pos;
	}

	// Remove duplicates with comparator
	template <typename EqualFn>
	void
	deduplicate_with(EqualFn eq)
	{
		if (count <= 1)
			return;

		size_t write_pos = 0;
		for (size_t i = 0; i < count; i++) {
			bool found = false;
			for (size_t j = 0; j < write_pos; j++) {
				if (eq((*this)[j], (*this)[i])) {
					found = true;
					break;
				}
			}
			if (!found) {
				(*this)[write_pos++] = (*this)[i];
			}
		}
		count = write_pos;
	}

	// Convert circular buffer back to normal array
	void
	normalize()
	{
		if (!is_queue_mode || head == 0)
			return;

		T *temp = (T *)Arena<ArenaTag>::alloc(sizeof(T) * count);
		for (size_t i = 0; i < count; i++) {
			temp[i] = (*this)[i];
		}
		memcpy(data, temp, count * sizeof(T));

		head = 0;
		tail = count;
		is_queue_mode = false;
	}

	// Introspection
	constexpr bool
	is_stack_allocated() const
	{
		return false;
	}
	constexpr size_t
	stack_capacity() const
	{
		return 0;
	}

      private:
	// Quicksort for operator
	void
	quicksort(int left, int right)
	{
		if (left >= right)
			return;

		int pivot_idx = partition_op(left, right);
		quicksort(left, pivot_idx - 1);
		quicksort(pivot_idx + 1, right);
	}

	int
	partition_op(int left, int right)
	{
		T pivot = (*this)[right];
		int i = left - 1;

		for (int j = left; j < right; j++) {
			if ((*this)[j] < pivot) {
				i++;
				swap(i, j);
			}
		}
		swap(i + 1, right);
		return i + 1;
	}

	// Quicksort with comparator
	template <typename CompareFn>
	void
	quicksort_with(int left, int right, CompareFn cmp)
	{
		if (left >= right)
			return;

		int pivot_idx = partition_with(left, right, cmp);
		quicksort_with(left, pivot_idx - 1, cmp);
		quicksort_with(pivot_idx + 1, right, cmp);
	}

	template <typename CompareFn>
	int
	partition_with(int left, int right, CompareFn cmp)
	{
		T pivot = (*this)[right];
		int i = left - 1;

		for (int j = left; j < right; j++) {
			if (cmp((*this)[j], pivot) < 0) {
				i++;
				swap(i, j);
			}
		}
		swap(i + 1, right);
		return i + 1;
	}
};

// ========================================================================
// Specialization for stack allocation
// ========================================================================

template <typename T, size_t StackSize, size_t InitialCapacity>
class Vec<T, stack_size_tag<StackSize>, InitialCapacity>
{
	T stack_buffer[StackSize];
	size_t capacity;
	size_t count;

	// Queue state
	size_t head;
	size_t tail;
	bool is_queue_mode;

      public:
	Vec()
	    : capacity(StackSize), count(0), head(0), tail(0),
	      is_queue_mode(false)
	{
	}

	// Basic operations
	void
	reserve(size_t new_capacity)
	{
		if (new_capacity > StackSize) {
			fprintf(stderr,
				"Stack vector overflow! Required: %zu, "
				"Available: %zu\n",
				new_capacity, StackSize);
			exit(1);
		}
	}

	void
	clear()
	{
		count = 0;
		head = 0;
		tail = 0;
		is_queue_mode = false;
	}

	size_t
	size() const
	{
		return count;
	}
	bool
	empty() const
	{
		return count == 0;
	}

	T &
	operator[](size_t i)
	{
		if (is_queue_mode) {
			return stack_buffer[(head + i) % capacity];
		}
		return stack_buffer[i];
	}

	const T &
	operator[](size_t i) const
	{
		if (is_queue_mode) {
			return stack_buffer[(head + i) % capacity];
		}
		return stack_buffer[i];
	}

	// Stack operations
	void
	push_back(const T &item)
	{
		if (is_queue_mode) {
			if (count >= capacity) {
				reserve(capacity + 1); // Will error
			}
			stack_buffer[tail] = item;
			tail = (tail + 1) % capacity;
			count++;
		} else {
			if (count >= capacity) {
				reserve(capacity + 1); // Will error
			}
			stack_buffer[count++] = item;
		}
	}

	void
	pop_back()
	{
		if (count == 0)
			return;
		if (is_queue_mode) {
			tail = (tail + capacity - 1) % capacity;
		}
		count--;
	}

	T &
	back()
	{
		if (is_queue_mode) {
			return stack_buffer[(tail + capacity - 1) % capacity];
		}
		return stack_buffer[count - 1];
	}

	const T &
	back() const
	{
		if (is_queue_mode) {
			return stack_buffer[(tail + capacity - 1) % capacity];
		}
		return stack_buffer[count - 1];
	}

	T &
	front()
	{
		if (is_queue_mode) {
			return stack_buffer[head];
		}
		return stack_buffer[0];
	}

	const T &
	front() const
	{
		if (is_queue_mode) {
			return stack_buffer[head];
		}
		return stack_buffer[0];
	}

	// Queue operations
	void
	enable_queue_mode()
	{
		if (is_queue_mode)
			return;
		is_queue_mode = true;
		head = 0;
		tail = count;
	}

	void
	push_front(const T &item)
	{
		if (!is_queue_mode)
			enable_queue_mode();
		if (count >= capacity) {
			reserve(capacity + 1); // Will error
		}
		head = (head + capacity - 1) % capacity;
		stack_buffer[head] = item;
		count++;
	}

	T
	pop_front()
	{
		if (count == 0)
			return T{};
		if (!is_queue_mode)
			enable_queue_mode();
		T result = stack_buffer[head];
		head = (head + 1) % capacity;
		count--;
		if (count == 0) {
			head = tail = 0;
		}
		return result;
	}

	// Utility operations
	void
	swap_remove(size_t index)
	{
		if (index >= count)
			return;
		(*this)[index] = (*this)[count - 1];
		count--;
	}

	void
	remove(size_t index)
	{
		if (index >= count)
			return;
		if (is_queue_mode) {
			swap_remove(index);
		} else {
			memmove(stack_buffer + index, stack_buffer + index + 1,
				(count - index - 1) * sizeof(T));
			count--;
		}
	}

	void
	insert(size_t index, const T &item)
	{
		if (index > count)
			index = count;
		if (is_queue_mode) {
			normalize();
		}
		if (count >= capacity) {
			reserve(capacity + 1); // Will error
		}
		memmove(stack_buffer + index + 1, stack_buffer + index,
			(count - index) * sizeof(T));
		stack_buffer[index] = item;
		count++;
	}

	void
	swap(size_t i, size_t j)
	{
		T tmp = (*this)[i];
		(*this)[i] = (*this)[j];
		(*this)[j] = tmp;
	}

	void
	reverse()
	{
		for (size_t i = 0; i < count / 2; i++) {
			swap(i, count - 1 - i);
		}
	}

	void
	normalize()
	{
		if (!is_queue_mode || head == 0)
			return;
		T temp[StackSize];
		for (size_t i = 0; i < count; i++) {
			temp[i] = (*this)[i];
		}
		memcpy(stack_buffer, temp, count * sizeof(T));
		head = 0;
		tail = count;
		is_queue_mode = false;
	}

	// Find/contains operations
	template <typename U = T>
	typename std::enable_if<has_equality<U>::value, int>::type
	find(const T &value) const
	{
		for (size_t i = 0; i < count; i++) {
			if ((*this)[i] == value)
				return (int)i;
		}
		return -1;
	}

	template <typename EqualFn>
	int
	find_with(EqualFn eq) const
	{
		for (size_t i = 0; i < count; i++) {
			if (eq((*this)[i]))
				return (int)i;
		}
		return -1;
	}

	template <typename U = T>
	typename std::enable_if<has_equality<U>::value, bool>::type
	contains(const T &value) const
	{
		return find(value) != -1;
	}

	template <typename EqualFn>
	bool
	contains_with(EqualFn eq) const
	{
		return find_with(eq) != -1;
	}

	template <typename U = T>
	typename std::enable_if<has_equality<U>::value, void>::type
	insert_unique(const T &item)
	{
		if (!contains(item)) {
			push_back(item);
		}
	}

	template <typename U = T>
	typename std::enable_if<has_equality<U>::value, void>::type
	erase(const T &item)
	{
		int pos = find(item);
		if (pos != -1) {
			swap_remove(pos);
		}
	}

	// Sort operations
	template <typename U = T>
	typename std::enable_if<has_less_than<U>::value, void>::type
	sort()
	{
		if (count <= 1)
			return;
		quicksort(0, count - 1);
	}

	template <typename CompareFn>
	void
	sort_with(CompareFn cmp)
	{
		if (count <= 1)
			return;
		quicksort_with(0, count - 1, cmp);
	}

	template <typename U = T>
	typename std::enable_if<has_less_than<U>::value, void>::type
	insert_sorted(const T &item)
	{
		size_t pos = 0;
		while (pos < count && (*this)[pos] < item)
			pos++;
		insert(pos, item);
	}

	template <typename U = T>
	typename std::enable_if<
	    has_less_than<U>::value && has_equality<U>::value, int>::type
	binary_search(const T &value) const
	{
		int left = 0, right = count - 1;
		while (left <= right) {
			int mid = left + (right - left) / 2;
			if ((*this)[mid] == value)
				return mid;
			if ((*this)[mid] < value)
				left = mid + 1;
			else
				right = mid - 1;
		}
		return -1;
	}

	// Deduplicate
	template <typename U = T>
	typename std::enable_if<has_equality<U>::value, void>::type
	deduplicate()
	{
		if (count <= 1)
			return;
		size_t write_pos = 0;
		for (size_t i = 0; i < count; i++) {
			bool found = false;
			for (size_t j = 0; j < write_pos; j++) {
				if ((*this)[j] == (*this)[i]) {
					found = true;
					break;
				}
			}
			if (!found) {
				(*this)[write_pos++] = (*this)[i];
			}
		}
		count = write_pos;
	}

	// Introspection
	constexpr bool
	is_stack_allocated() const
	{
		return true;
	}
	constexpr size_t
	stack_capacity() const
	{
		return StackSize;
	}

      private:
	// Quicksort implementation
	void
	quicksort(int left, int right)
	{
		if (left >= right)
			return;
		int pivot_idx = partition_op(left, right);
		quicksort(left, pivot_idx - 1);
		quicksort(pivot_idx + 1, right);
	}

	int
	partition_op(int left, int right)
	{
		T pivot = (*this)[right];
		int i = left - 1;
		for (int j = left; j < right; j++) {
			if ((*this)[j] < pivot) {
				i++;
				swap(i, j);
			}
		}
		swap(i + 1, right);
		return i + 1;
	}

	template <typename CompareFn>
	void
	quicksort_with(int left, int right, CompareFn cmp)
	{
		if (left >= right)
			return;
		int pivot_idx = partition_with(left, right, cmp);
		quicksort_with(left, pivot_idx - 1, cmp);
		quicksort_with(pivot_idx + 1, right, cmp);
	}

	template <typename CompareFn>
	int
	partition_with(int left, int right, CompareFn cmp)
	{
		T pivot = (*this)[right];
		int i = left - 1;
		for (int j = left; j < right; j++) {
			if (cmp((*this)[j], pivot) < 0) {
				i++;
				swap(i, j);
			}
		}
		swap(i + 1, right);
		return i + 1;
	}
};

// Convenience factory functions
template <typename T, size_t N>
/*Embedded Vec */
using EmbVec = Vec<T, stack_size_tag<N>>;

// Usage:
// EmbVec<int, 100> stack_vec;        // 100 ints on stack
// Vec<int, MyArena> arena_vec;         // Arena allocated
