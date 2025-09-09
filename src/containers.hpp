#pragma once
#include "arena.hpp"
#include <cstdint>
#include <string_view>

template <typename T, typename ArenaTag = global_arena, uint32_t InitialSize = 8> struct array
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

	contiguous<T, ArenaTag, InitialSize> storage;

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

template <typename T, typename ArenaTag = global_arena> struct queue
{
	contiguous<T, ArenaTag, 16> storage;
	uint32_t					head = 0;
	uint32_t					tail = 0;
	uint32_t					count = 0;

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
			arena<ArenaTag>::reclaim(old_data, old_cap * sizeof(T));
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
/*----------------------------------------------------------------------------
 */

using std::pair;

template <typename K, typename V, typename ArenaTag = global_arena> struct hash_map
{
	struct Entry
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

	contiguous<Entry, ArenaTag, 16> storage;
	uint32_t						_size = 0;
	uint32_t						tombstones = 0;

	uint32_t
	hash_key(const K &key) const
	{
		if constexpr (std::is_same_v<K, std::string_view>)
		{
			if (key.empty())
			{
				return 1;
			}
			uint32_t h = 2166136261u;
			for (unsigned char c : key)
			{
				h ^= c;
				h *= 16777619u;
			}
			return h ? h : 1;
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

	bool
	keys_equal(const K &stored_key, const K &search_key) const
	{
		return stored_key == search_key;
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
		Entry	*old_entries = storage.data;

		contiguous<Entry, ArenaTag, 16> new_storage;
		new_storage.allocate_full(old_capacity * 2);
		new_storage.zero();

		uint32_t old_size = _size;
		_size = 0;
		tombstones = 0;

		for (uint32_t i = 0; i < old_capacity; i++)
		{
			if (old_entries[i].state == Entry::OCCUPIED)
			{

				insert_into(new_storage.data, new_storage.capacity, old_entries[i].key, old_entries[i].hash,
							old_entries[i].value);
			}
		}

		storage.swap(&new_storage);
	}

	V *
	get(const K &key)
	{
		if (nullptr == entry(key)) {
			return nullptr;
		}
		return &entry(key)->value;
	}

	Entry *
	entry(const K &key)
	{
		if (!storage.data || _size == 0)
		{
			return nullptr;
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = storage.capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = storage.data[idx];

			if (entry.state == Entry::EMPTY)
			{
				return nullptr;
			}

			if (entry.state == Entry::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key))
			{
				return &entry;
			}

			idx = (idx + 1) & mask;
		}
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
			Entry &entry = storage.data[idx];

			if (entry.state == Entry::EMPTY)
			{
				return false;
			}

			if (entry.state == Entry::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.state = Entry::DELETED;
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
	Entry *
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

	Entry *
	data()
	{
		return storage.data;
	}
	const Entry *
	data() const
	{
		return storage.data;
	}

  private:
	// Core insertion logic - no growth checks, works on any buffer
	V *
	insert_into(Entry *entries, uint32_t capacity, const K &key, uint32_t hash, const V &value,
				bool handle_tombstones = false)
	{
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;
		uint32_t first_deleted = -1;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state == Entry::EMPTY || (!handle_tombstones && entry.state != Entry::OCCUPIED))
			{
				Entry *target = (handle_tombstones && first_deleted != -1) ? &entries[first_deleted] : &entry;

				target->key = key;
				target->value = value;
				target->hash = hash;
				target->state = Entry::OCCUPIED;

				if (handle_tombstones && first_deleted != -1)
				{
					tombstones--;
				}

				_size++;
				return &target->value;
			}

			if (handle_tombstones && entry.state == Entry::DELETED && first_deleted == -1)
			{
				first_deleted = idx;
			}
			else if (entry.state == Entry::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.value = value;
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	struct iterator
	{
		Entry	*entries;
		uint32_t capacity;
		uint32_t index;

		// Constructor
		iterator(Entry *e, uint32_t cap, uint32_t idx) : entries(e), capacity(cap), index(idx)
		{
			// Skip to first valid entry
			while (index < capacity && entries[index].state != Entry::OCCUPIED)
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
			while (index < capacity && entries[index].state != Entry::OCCUPIED)
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
template <typename K, typename ArenaTag = global_arena> using hash_set = hash_map<K, char, ArenaTag>;
