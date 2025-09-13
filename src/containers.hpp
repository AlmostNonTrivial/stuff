#pragma once
#include "arena.hpp"
#include <cstdint>
#include <string_view>



template <typename T, typename arena_tag = global_arena, uint32_t InitialSize = 8> struct array
{

	array() = default;

	// Initializer list constructor
	array(std::initializer_list<T> init)
	{
		if (init.size() > 0)
		{
			storage.reserve(init.size());
			for (const T &value : init)
			{
				push(value);
			}
		}
	}

	contiguous<T, arena_tag, InitialSize> storage;

	void
	reserve(uint32_t min_capacity)
	{
		storage.reserve(min_capacity);
	}

	uint32_t
	push(const T &value)
	{
		uint32_t idx = storage.size;
		*storage.grow_by(1) = value;
		return idx;
	}

	template <typename U = T>
	typename std::enable_if_t<std::is_same_v<U, std::string_view>, uint32_t>
	push(const char *str)
	{
		return push(std::string_view(str));
	}

	T *
	push_n(const T *values, uint32_t count)
	{
		T *dest = storage.grow_by(count);
		memcpy(dest, values, count * sizeof(T));
		return dest;
	}

	void
	pop_back()
	{
		assert(storage.size > 0);
		--storage.size;
	}

	T
	pop_value()
	{
		assert(storage.size > 0);
		return storage.data[--storage.size];
	}

	void
	clear()
	{
		storage.clear();
	}
	void
	reset()
	{
		storage.reset();
	}
	void
	resize(uint32_t new_size)
	{
		storage.resize(new_size, true);
	}
	void
	shrink_to_fit()
	{
		storage.shrink_to_fit();
	}

	template <typename OtherTag>
	void
	copy_from(const array<T, OtherTag> &other)
	{
		storage.copy_from(other.storage);
	}

	T &
	operator[](uint32_t index)
	{
		assert(index < storage.size);
		return storage.data[index];
	}

	const T &
	operator[](uint32_t index) const
	{
		assert(index < storage.size);
		return storage.data[index];
	}

	T *
	back()
	{
		return storage.size > 0 ? &storage.data[storage.size - 1] : nullptr;
	}
	const T *
	back() const
	{
		return storage.size > 0 ? &storage.data[storage.size - 1] : nullptr;
	}
	T *
	front()
	{
		return storage.size > 0 ? &storage.data[0] : nullptr;
	}
	const T *
	front() const
	{
		return storage.size > 0 ? &storage.data[0] : nullptr;
	}

	T *
	begin()
	{
		return storage.data;
	}
	T *
	end()
	{
		return storage.data + storage.size;
	}
	const T *
	begin() const
	{
		return storage.data;
	}
	const T *
	end() const
	{
		return storage.data + storage.size;
	}

	bool
	empty() const
	{
		return storage.size == 0;
	}
	uint32_t
	size() const
	{
		return storage.size;
	}
	uint32_t
	capacity() const
	{
		return storage.capacity;
	}
	T *
	data()
	{
		return storage.data;
	}
	const T *
	data() const
	{
		return storage.data;
	}
};

template <typename T, typename arena_tag = global_arena> struct queue
{
	contiguous<T, arena_tag, 16> storage;
	uint32_t					 head = 0;
	uint32_t					 tail = 0;
	uint32_t					 count = 0;

	void
	reserve(uint32_t min_capacity)
	{
		if (storage.capacity >= min_capacity)
		{
			return;
		}

		min_capacity = round_up_power_of_2(min_capacity);
		grow_to(min_capacity);
	}

	void
	grow_to(uint32_t new_capacity)
	{
		T		*old_data = storage.data;
		uint32_t old_cap = storage.capacity;

		// Allocate new buffer
		T *new_data = storage.alloc_raw(new_capacity);

		// Copy data
		uint32_t j = 0;
		if (count > 0 && old_data)
		{
			for (uint32_t i = 0; i < count; i++)
			{
				new_data[j++] = old_data[(head + i) % old_cap];
			}
		}

		if (old_data)
		{
			arena<arena_tag>::reclaim(old_data, old_cap * sizeof(T));
		}

		// Update metadata
		storage.data = new_data;
		storage.capacity = new_capacity;
		storage.size = 0; // Queue doesn't use this
		head = 0;
		tail = count;
	}

	void
	push(const T &value)
	{
		if (count == storage.capacity)
		{
			uint32_t new_cap = storage.capacity ? storage.capacity * 2 : 16;
			grow_to(new_cap);
		}

		storage.data[tail] = value;
		tail = (tail + 1) % storage.capacity;
		count++;
	}

	T
	pop()
	{
		assert(count > 0);
		T value = storage.data[head];
		head = (head + 1) % storage.capacity;
		count--;
		return value;
	}

	T *
	front()
	{
		return count > 0 ? &storage.data[head] : nullptr;
	}

	const T *
	front() const
	{
		return count > 0 ? &storage.data[head] : nullptr;
	}

	T *
	back()
	{
		if (count == 0)
		{
			return nullptr;
		}

		uint32_t back_idx = (tail == 0) ? storage.capacity - 1 : tail - 1;
		return &storage.data[back_idx];
	}

	const T *
	back() const
	{
		if (count == 0)
		{
			return nullptr;
		}

		uint32_t back_idx = (tail == 0) ? storage.capacity - 1 : tail - 1;
		return &storage.data[back_idx];
	}

	void
	clear()
	{
		head = 0;
		tail = 0;
		count = 0;
	}

	void
	reset()
	{
		storage.reset();
		head = 0;
		tail = 0;
		count = 0;
	}

	bool
	empty() const
	{
		return count == 0;
	}
	uint32_t
	size() const
	{
		return count;
	}
	uint32_t
	capacity() const
	{
		return storage.capacity;
	}

	T *
	data()
	{
		return storage.data;
	}
	const T *
	data() const
	{
		return storage.data;
	}
};

// Simple fixed-size string for use as hash_map keys
template<size_t N>
struct fixed_string
{
	char data[N];

	// Default constructor - zero initialize
	fixed_string()
	{
		memset(data, 0, N);
	}

	// Construct from C string
	fixed_string(const char* str)
	{
		if (str)
		{
			size_t len = strlen(str);
			if (len >= N)
			{
				len = N - 1;
			}
			memcpy(data, str, len);
			data[len] = '\0';
			// Zero the rest for consistent hashing/comparison
			if (len + 1 < N)
			{
				memset(data + len + 1, 0, N - len - 1);
			}
		}
		else
		{
			memset(data, 0, N);
		}
	}

	// Construct from string_view
	fixed_string(std::string_view sv)
	{
		size_t len = sv.size();
		if (len >= N)
		{
			len = N - 1;
		}
		memcpy(data, sv.data(), len);
		data[len] = '\0';
		// Zero the rest for consistent hashing/comparison
		if (len + 1 < N)
		{
			memset(data + len + 1, 0, N - len - 1);
		}
	}

	// Assignment from C string
	fixed_string& operator=(const char* str)
	{
		if (str)
		{
			size_t len = strlen(str);
			if (len >= N)
			{
				len = N - 1;
			}
			memcpy(data, str, len);
			data[len] = '\0';
			if (len + 1 < N)
			{
				memset(data + len + 1, 0, N - len - 1);
			}
		}
		else
		{
			memset(data, 0, N);
		}
		return *this;
	}

	// Equality comparisons
	bool operator==(const fixed_string& other) const
	{
		return strcmp(data, other.data) == 0;
	}

	bool operator==(const char* str) const
	{
		return str && strcmp(data, str) == 0;
	}

	bool operator==(std::string_view sv) const
	{
		size_t my_len = strlen(data);
		return my_len == sv.size() && memcmp(data, sv.data(), my_len) == 0;
	}

	bool operator!=(const fixed_string& other) const
	{
		return !(*this == other);
	}

	// Get length
	size_t length() const
	{
		return strlen(data);
	}

	// Check if empty
	bool empty() const
	{
		return data[0] == '\0';
	}

	// Access as C string
	const char* c_str() const
	{
		return data;
	}

	char* c_str()
	{
		return data;
	}
};

// Type aliases for common sizes
using string32 = fixed_string<32>;
using string64 = fixed_string<64>;
using string128 = fixed_string<128>;
using string256 = fixed_string<256>;

// Complete hash_map implementation with all key types
using std::pair;

template <typename K, typename V, typename arena_tag = global_arena> struct hash_map
{
	struct entry
	{
		K		 key;
		V		 value;
		uint32_t hash;
		enum uint8_t
		{
			EMPTY = 0,
			OCCUPIED = 1,
			DELETED = 2
		} state;
	};

	contiguous<entry, arena_tag, 16> storage;
	uint32_t						 _size = 0;
	uint32_t						 tombstones = 0;

	// Helper to detect if type has c_str() method
	template<typename T, typename = void>
	struct has_c_str : std::false_type {};

	template<typename T>
	struct has_c_str<T, std::void_t<decltype(std::declval<T>().c_str())>> : std::true_type {};

	// Generic string hashing function
	static uint32_t hash_string_data(const char* s, size_t len)
	{
		if (!s || len == 0) return 1;
		uint32_t h = 2166136261u;
		for (size_t i = 0; i < len; i++)
		{
			h ^= static_cast<unsigned char>(s[i]);
			h *= 16777619u;
		}
		return h ? h : 1;
	}

	uint32_t
	hash_key(const K &key) const
	{
		// Handle types with c_str() method (like fixed_string)
		if constexpr (has_c_str<K>::value)
		{
			const char* s = key.c_str();
			return hash_string_data(s, strlen(s));
		}
		else if constexpr (std::is_same_v<K, std::string_view>)
		{
			return hash_string_data(key.data(), key.size());
		}
		else if constexpr (std::is_pointer_v<K>)
		{
			// Hash pointer address
			uintptr_t addr = reinterpret_cast<uintptr_t>(key);

			// Mix bits using same algorithm as integers
			if constexpr (sizeof(uintptr_t) <= 4)
			{
				uint32_t ux = static_cast<uint32_t>(addr);
				ux = ((ux >> 16) ^ ux) * 0x45d9f3b;
				ux = ((ux >> 16) ^ ux) * 0x45d9f3b;
				ux = (ux >> 16) ^ ux;
				return ux ? ux : 1; // Ensure non-zero
			}
			else
			{
				uint64_t ux = static_cast<uint64_t>(addr);
				ux = (ux ^ (ux >> 30)) * 0xbf58476d1ce4e5b9ULL;
				ux = (ux ^ (ux >> 27)) * 0x94d049bb133111ebULL;
				ux = ux ^ (ux >> 31);
				return static_cast<uint32_t>(ux) ? static_cast<uint32_t>(ux) : 1; // Ensure non-zero
			}
		}
		else if constexpr (std::is_integral_v<K>)
		{
			using U = typename std::make_unsigned<K>::type;
			U ux = static_cast<U>(key);

			if constexpr (sizeof(K) <= 4)
			{
				ux = ((ux >> 16) ^ ux) * 0x45d9f3b;
				ux = ((ux >> 16) ^ ux) * 0x45d9f3b;
				ux = (ux >> 16) ^ ux;
				return static_cast<uint32_t>(ux);
			}
			else
			{
				ux = (ux ^ (ux >> 30)) * 0xbf58476d1ce4e5b9ULL;
				ux = (ux ^ (ux >> 27)) * 0x94d049bb133111ebULL;
				ux = ux ^ (ux >> 31);
				return static_cast<uint32_t>(static_cast<uint64_t>(ux));
			}
		}
		else
		{
			static_assert(sizeof(K) == 0, "Unsupported key type");
			return 0;
		}
	}

	// Hash for lookup keys (might be different type than K)
	template<typename LookupKey>
	uint32_t hash_lookup_key(const LookupKey& key) const
	{
		if constexpr (std::is_same_v<LookupKey, K>)
		{
			return hash_key(key);
		}
		else if constexpr (std::is_same_v<LookupKey, std::string_view>)
		{
			return hash_string_data(key.data(), key.size());
		}
		else if constexpr (std::is_same_v<LookupKey, const char*> || std::is_same_v<LookupKey, char*>)
		{
			return hash_string_data(key, key ? strlen(key) : 0);
		}
		else if constexpr (has_c_str<LookupKey>::value)
		{
			const char* s = key.c_str();
			return hash_string_data(s, strlen(s));
		}
		else
		{
			return hash_key(key);
		}
	}

	bool
	keys_equal(const K &stored_key, const K &search_key) const
	{
		return stored_key == search_key;
	}

	// Compare stored key with lookup key (might be different type)
	template<typename LookupKey>
	bool keys_match(const K& stored_key, const LookupKey& lookup_key) const
	{
		if constexpr (std::is_same_v<LookupKey, K>)
		{
			return stored_key == lookup_key;
		}
		else if constexpr (has_c_str<K>::value)
		{
			// K is fixed_string, use its operator== overloads
			return stored_key == lookup_key;
		}
		else
		{
			return false;
		}
	}

	void
	init(uint32_t initial_capacity = 16)
	{
		if (storage.data)
		{
			return;
		}

		initial_capacity = round_up_power_of_2(initial_capacity);

		storage.allocate_full(initial_capacity);
		storage.zero();
		_size = 0;
		tombstones = 0;
	}

	void
	grow()
	{
		uint32_t old_capacity = storage.capacity;
		entry	*old_entries = storage.data;

		contiguous<entry, arena_tag, 16> new_storage;
		new_storage.allocate_full(old_capacity * 2);
		new_storage.zero();

		uint32_t old_size = _size;
		_size = 0;
		tombstones = 0;

		for (uint32_t i = 0; i < old_capacity; i++)
		{
			if (old_entries[i].state == entry::OCCUPIED)
			{

				insert_into(new_storage.data, new_storage.capacity, old_entries[i].key, old_entries[i].hash,
							old_entries[i].value);
			}
		}

		storage.swap(&new_storage);
	}

	// Template get that works with any compatible lookup type
	template<typename LookupKey>
	V* get_impl(const LookupKey& key)
	{
		if (!storage.data || _size == 0)
		{
			return nullptr;
		}

		uint32_t hash = hash_lookup_key(key);
		uint32_t mask = storage.capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			entry &e = storage.data[idx];

			if (e.state == entry::EMPTY)
			{
				return nullptr;
			}

			if (e.state == entry::OCCUPIED && e.hash == hash && keys_match(e.key, key))
			{
				return &e.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	V *
	get(const K &key)
	{
		return get_impl(key);
	}

	// Enable lookups with string_view when K is fixed_string
	template<typename T = K>
	typename std::enable_if<has_c_str<T>::value, V*>::type
	get(std::string_view key)
	{
		return get_impl(key);
	}

	// Enable lookups with const char* when K is fixed_string
	template<typename T = K>
	typename std::enable_if<has_c_str<T>::value, V*>::type
	get(const char* key)
	{
		return get_impl(key);
	}

	const V *
	get(const K &key) const
	{
		return const_cast<hash_map *>(this)->get(key);
	}

	V *
	insert(const K &key, const V &value)
	{
		if (!storage.data)
		{
			init();
		}

		if ((_size + tombstones) * 4 >= storage.capacity * 3)
		{
			grow();
		}

		uint32_t hash = hash_key(key);
		return insert_into(storage.data, storage.capacity, key, hash, value, true);
	}

	bool
	remove(const K &key)
	{
		if (!storage.data || _size == 0)
		{
			return false;
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = storage.capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			entry &entry = storage.data[idx];

			if (entry.state == entry::EMPTY)
			{
				return false;
			}

			if (entry.state == entry::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.state = entry::DELETED;
				_size--;
				tombstones++;
				return true;
			}

			idx = (idx + 1) & mask;
		}
	}

	bool
	contains(const K &key) const
	{
		return get(key) != nullptr;
	}

	void
	clear()
	{
		if (storage.data)
		{
			storage.zero();
		}
		_size = 0;
		tombstones = 0;
	}

	void
	reset()
	{
		storage.reset();
		_size = 0;
		tombstones = 0;
	}

	bool
	empty() const
	{
		return _size == 0;
	}
	uint32_t
	capacity() const
	{
		return storage.capacity;
	}
	entry *
	entries() const
	{
		return storage.data;
	}
	uint32_t
	size() const
	{
		return _size;
	}
	uint32_t
	tombstone_count() const
	{
		return tombstones;
	}

	entry *
	data()
	{
		return storage.data;
	}
	const entry *
	data() const
	{
		return storage.data;
	}

  private:
	// Core insertion logic - no growth checks, works on any buffer
	V *
	insert_into(entry *entries, uint32_t capacity, const K &key, uint32_t hash, const V &value,
				bool handle_tombstones = false)
	{
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;
		uint32_t first_deleted = -1;

		while (true)
		{
			entry &e = entries[idx];

			if (e.state == entry::EMPTY || (!handle_tombstones && e.state != entry::OCCUPIED))
			{
				entry *target = (handle_tombstones && first_deleted != -1) ? &entries[first_deleted] : &e;

				target->key = key;
				target->value = value;
				target->hash = hash;
				target->state = entry::OCCUPIED;

				if (handle_tombstones && first_deleted != -1)
				{
					tombstones--;
				}

				_size++;
				return &target->value;
			}

			if (handle_tombstones && e.state == entry::DELETED && first_deleted == -1)
			{
				first_deleted = idx;
			}
			else if (e.state == entry::OCCUPIED && e.hash == hash && keys_equal(e.key, key))
			{
				e.value = value;
				return &e.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	struct iterator
	{
		entry	*entries;
		uint32_t capacity;
		uint32_t index;

		// Constructor
		iterator(entry *e, uint32_t cap, uint32_t idx) : entries(e), capacity(cap), index(idx)
		{
			// Skip to first valid entry
			while (index < capacity && entries[index].state != entry::OCCUPIED)
			{
				++index;
			}
		}

		// Dereference - returns pair reference (no copy)
		pair<K &, V &>
		operator*()
		{
			return {entries[index].key, entries[index].value};
		}

		// Arrow operator for direct member access
		struct arrow_proxy
		{
			pair<K &, V &> p;
			pair<K &, V &> *
			operator->()
			{
				return &p;
			}
		};
		arrow_proxy
		operator->()
		{
			return {{entries[index].key, entries[index].value}};
		}

		// Pre-increment
		iterator &
		operator++()
		{
			++index;
			while (index < capacity && entries[index].state != entry::OCCUPIED)
			{
				++index;
			}
			return *this;
		}

		// Post-increment
		iterator
		operator++(int)
		{
			iterator tmp = *this;
			++(*this);
			return tmp;
		}

		// Comparison
		bool
		operator==(const iterator &other) const
		{
			return index == other.index;
		}
		bool
		operator!=(const iterator &other) const
		{
			return index != other.index;
		}
	};

  public:
	// Begin/end for range-based for loops
	iterator
	begin()
	{
		if (!storage.data)
			return end();
		return iterator(storage.data, storage.capacity, 0);
	}

	iterator
	end()
	{
		return iterator(storage.data, storage.capacity, storage.capacity);
	}
};

template <typename K, typename arena_tag = global_arena> using hash_set = hash_map<K, char, arena_tag>;


// ============================================================
// CONTAINER DIAGNOSTIC FUNCTIONS
// ============================================================

/*
 * Print diagnostic info for a contiguous container
 */
template <typename T, typename Tag>
void
print_contiguous_info(const contiguous<T, Tag> &cont, const char * name = "")
{
	printf("\n=== Contiguous<%s> Info: %s ===\n", typeid(T).name(), name);
	printf("  Data ptr:  %p\n", cont.data);
	printf("  Size:      %u / %u (%.1f%% used)\n", cont.size, cont.capacity,
		   cont.capacity > 0 ? (100.0 * cont.size / cont.capacity) : 0);
	printf("  Bytes:     %u used, %u allocated, %u wasted\n", cont.size * sizeof(T), cont.capacity * sizeof(T),
		   (cont.capacity - cont.size) * sizeof(T));

	if (arena<Tag>::owns_pointer(cont.data))
	{
		printf("  Arena:     %s (verified ownership)\n", typeid(Tag).name());
	}
	else if (cont.data)
	{
		printf("  Arena:     WARNING - pointer not owned by expected arena!\n");
	}
}

/*
 * Print diagnostic info for an array
 */
template <typename T, typename Tag>
void
print_array_info(const char *name, const array<T, Tag> &arr)
{
	printf("\n=== Array<%s> Info: %s ===\n", typeid(T).name(), name);
	printf("  Size:      %u elements\n", arr.size());
	printf("  Capacity:  %u elements\n", arr.capacity());
	printf("  Empty:     %s\n", arr.empty() ? "yes" : "no");

	if (arr.capacity() > 0)
	{
		printf("  Utilization: %.1f%%\n", (100.0 * arr.size()) / arr.capacity());
		printf("  Memory:    %u bytes used, %u bytes allocated\n", arr.size() * sizeof(T), arr.capacity() * sizeof(T));
	}

	if (arr.data() && arena<Tag>::owns_pointer(arr.data()))
	{
		printf("  Arena:     %s\n", typeid(Tag).name());
		printf("  Address:   [%p - %p]\n", arr.data(), arr.data() + arr.capacity());
	}

	// Sample first/last elements for non-empty arrays
	if (!arr.empty() && arr.size() <= 10)
	{
		printf("  Elements:  ");
		if constexpr (std::is_arithmetic_v<T>)
		{
			for (uint32_t i = 0; i < arr.size(); i++)
			{
				if constexpr (std::is_floating_point_v<T>)
				{
					printf("%.2f ", static_cast<double>(arr[i]));
				}
				else
				{
					printf("%lld ", static_cast<long long>(arr[i]));
				}
			}
			printf("\n");
		}
		else
		{
			printf("[%u elements of type %s]\n", arr.size(), typeid(T).name());
		}
	}
	else if (!arr.empty())
	{
		printf("  Elements:  [%u elements, showing first/last]\n", arr.size());
		if constexpr (std::is_arithmetic_v<T>)
		{
			printf("    First 3: ");
			for (uint32_t i = 0; i < std::min(3u, arr.size()); i++)
			{
				if constexpr (std::is_floating_point_v<T>)
				{
					printf("%.2f ", static_cast<double>(arr[i]));
				}
				else
				{
					printf("%lld ", static_cast<long long>(arr[i]));
				}
			}
			printf("\n    Last 3:  ");
			uint32_t start = arr.size() > 3 ? arr.size() - 3 : 0;
			for (uint32_t i = start; i < arr.size(); i++)
			{
				if constexpr (std::is_floating_point_v<T>)
				{
					printf("%.2f ", static_cast<double>(arr[i]));
				}
				else
				{
					printf("%lld ", static_cast<long long>(arr[i]));
				}
			}
			printf("\n");
		}
	}
}

/*
 * Print diagnostic info for a queue
 */
template <typename T, typename Tag>
void
print_queue_info(const char *name, const queue<T, Tag> &q)
{
	printf("\n=== Queue<%s> Info: %s ===\n", typeid(T).name(), name);
	printf("  Count:     %u elements\n", q.count);
	printf("  Capacity:  %u elements\n", q.capacity());
	printf("  Head/Tail: %u / %u\n", q.head, q.tail);
	printf("  Empty:     %s\n", q.empty() ? "yes" : "no");

	if (q.capacity() > 0)
	{
		printf("  Utilization: %.1f%%\n", (100.0 * q.count) / q.capacity());
		printf("  Memory:    %u bytes used, %u bytes allocated\n", q.count * sizeof(T), q.capacity() * sizeof(T));
	}

	if (q.data() && arena<Tag>::owns_pointer(q.data()))
	{
		printf("  Arena:     %s\n", typeid(Tag).name());
		printf("  Address:   [%p - %p]\n", q.data(), q.data() + q.capacity());
	}

	// Visual representation of circular buffer
	if (q.capacity() > 0 && q.capacity() <= 32)
	{
		printf("  Layout:    [");
		for (uint32_t i = 0; i < q.capacity(); i++)
		{
			if (q.count == 0)
			{
				printf(".");
			}
			else if (q.head <= q.tail)
			{
				printf("%c", (i >= q.head && i < q.tail) ? '#' : '.');
			}
			else
			{
				printf("%c", (i >= q.head || i < q.tail) ? '#' : '.');
			}
		}
		printf("] (# = data, . = empty)\n");
		if (q.count > 0)
		{
			printf("             ");
			for (uint32_t i = 0; i < q.capacity(); i++)
			{
				if (i == q.head && i == ((q.tail - 1 + q.capacity()) % q.capacity()))
				{
					printf("B"); // Both head and tail
				}
				else if (i == q.head)
				{
					printf("H");
				}
				else if (i == ((q.tail - 1 + q.capacity()) % q.capacity()))
				{
					printf("T");
				}
				else
				{
					printf(" ");
				}
			}
			printf("  (H = head, T = tail)\n");
		}
	}
}

/*
 * Print diagnostic info for a hash_map
 */
template <typename K, typename V, typename Tag>
void
print_hash_map_info(const hash_map<K, V, Tag> &map, const char *name = "")
{
	printf("\n=== HashMap<%s, %s> Info: %s ===\n", typeid(K).name(), typeid(V).name(), name);
	printf("  Size:       %u entries\n", map.size());
	printf("  Capacity:   %u buckets\n", map.capacity());
	printf("  Tombstones: %u\n", map.tombstone_count());
	printf("  Empty:      %s\n", map.empty() ? "yes" : "no");

	if (map.capacity() > 0)
	{
		double load_factor = (double)(map.size() + map.tombstone_count()) / map.capacity();
		printf("  Load factor: %.2f (%.1f%% with tombstones)\n", (double)map.size() / map.capacity(),
			   load_factor * 100);
		printf("  Memory:     %zu bytes for table\n", map.capacity() * sizeof(typename hash_map<K, V, Tag>::entry));
	}

	if (map.data() && arena<Tag>::owns_pointer(const_cast<void *>(static_cast<const void *>(map.data()))))
	{
		printf("  Arena:      %s\n", typeid(Tag).name());
		printf("  Address:    %p\n", map.data());
	}

	// Analyze distribution
	if (map.capacity() > 0 && map.size() > 0)
	{
		uint32_t max_probe = 0;
		uint32_t total_probe = 0;
		uint32_t occupied = 0;

		for (uint32_t i = 0; i < map.capacity(); i++)
		{
			auto &entry = map.entries()[i];
			if (entry.state == hash_map<K, V, Tag>::entry::OCCUPIED)
			{
				occupied++;
				uint32_t ideal_pos = entry.hash & (map.capacity() - 1);
				uint32_t probe_dist = (i >= ideal_pos) ? (i - ideal_pos) : (map.capacity() - ideal_pos + i);
				total_probe += probe_dist;
				if (probe_dist > max_probe)
				{
					max_probe = probe_dist;
				}
			}
		}

		printf("  Probe stats:\n");
		printf("    Max distance:  %u\n", max_probe);
		printf("    Avg distance:  %.2f\n", occupied > 0 ? (double)total_probe / occupied : 0.0);

		// Distribution visualization for small maps
		if (map.capacity() <= 64)
		{
			printf("  Bucket map: [");
			for (uint32_t i = 0; i < map.capacity(); i++)
			{
				auto &entry = map.entries()[i];
				if (entry.state == hash_map<K, V, Tag>::entry::OCCUPIED)
				{
					printf("#");
				}
				else if (entry.state == hash_map<K, V, Tag>::entry::DELETED)
				{
					printf("x");
				}
				else
				{
					printf(".");
				}
			}
			printf("] (# = occupied, x = deleted, . = empty)\n");
		}

		// Clustering analysis
		uint32_t clusters = 0;
		uint32_t max_cluster = 0;
		uint32_t current_cluster = 0;
		bool	 in_cluster = false;

		for (uint32_t i = 0; i < map.capacity(); i++)
		{
			auto &entry = map.entries()[i];
			if (entry.state != hash_map<K, V, Tag>::entry::EMPTY)
			{
				if (!in_cluster)
				{
					clusters++;
					in_cluster = true;
					current_cluster = 1;
				}
				else
				{
					current_cluster++;
				}
				if (current_cluster > max_cluster)
				{
					max_cluster = current_cluster;
				}
			}
			else
			{
				in_cluster = false;
				current_cluster = 0;
			}
		}

		printf("  Clustering:\n");
		printf("    Clusters:      %u\n", clusters);
		printf("    Max cluster:   %u entries\n", max_cluster);
		printf("    Avg cluster:   %.2f entries\n",
			   clusters > 0 ? (double)(map.size() + map.tombstone_count()) / clusters : 0.0);
	}

	// Sample entries for small maps
	if (map.size() > 0 && map.size() <= 5)
	{
		printf("  Entries:\n");
		uint32_t shown = 0;
		for (uint32_t i = 0; i < map.capacity() && shown < map.size(); i++)
		{
			auto &entry = map.entries()[i];
			if (entry.state == hash_map<K, V, Tag>::entry::OCCUPIED)
			{
				printf("    [%u]: ", i);
				if constexpr (std::is_arithmetic_v<K>)
				{
					if constexpr (std::is_floating_point_v<K>)
					{
						printf("key=%.2f", static_cast<double>(entry.key));
					}
					else
					{
						printf("key=%lld", static_cast<long long>(entry.key));
					}
				}
				else if constexpr (std::is_same_v<K, std::string_view>)
				{
					printf("key=\"%.*s\"", (int)std::min(size_t(20), entry.key.size()), entry.key.data());
				}
				else
				{
					printf("key=[%s]", typeid(K).name());
				}

				printf(", hash=%08x\n", entry.hash);
				shown++;
			}
		}
	}
}

/*
 * Global function to print all container stats in an arena
 */
template <typename Tag>
void
print_arena_container_stats()
{
	printf("\n=== Container Statistics for Arena<%s> ===\n", typeid(Tag).name());

	size_t arena_used = arena<Tag>::used();
	size_t arena_committed = arena<Tag>::committed();
	size_t arena_freelist = arena<Tag>::freelist_bytes();

	printf("Arena memory breakdown:\n");
	printf("  Total used:     %zu bytes\n", arena_used);
	printf("  In freelists:   %zu bytes (%.1f%%)\n", arena_freelist,
		   arena_used > 0 ? (100.0 * arena_freelist / arena_used) : 0);
	printf("  Net allocated:  %zu bytes\n", arena_used - arena_freelist);

	double fragmentation = 0;
	if (arena_used > arena_freelist)
	{
		fragmentation = (100.0 * arena_freelist) / (arena_used - arena_freelist);
	}
	printf("  Fragmentation:  %.1f%% (freelist/allocated)\n", fragmentation);
}
