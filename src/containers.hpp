#pragma once
#include "arena.hpp"
#include <cstdint>

template <typename ArenaTag, uint32_t InitialSize> struct string;

template <typename T, typename ArenaTag = global_arena, uint32_t InitialSize = 8> struct array
{
	template <typename U> struct is_string : std::false_type
	{
	};
	template <typename Tag, uint32_t Size> struct is_string<string<Tag, Size>> : std::true_type
	{
	};

	// Storage delegate
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

	template <typename OtherTag = ArenaTag, uint32_t OtherSize = InitialSize>
	uint32_t
	push(const string<OtherTag, OtherSize> &value)
	{
		static_assert(is_string<T>::value, "push(string) can only be used with string arrays");
		storage.reserve(storage.size + 1);
		T &dest = storage.data[storage.size];
		dest.set(value.c_str());
		return storage.size++;
	}
	// In array:
	T *
	push_n(const T *values, uint32_t count)
	{
		T *dest = storage.grow_by(count);
		memcpy(dest, values, count * sizeof(T));
		return dest;
	}

	template <typename OtherTag, uint32_t OtherSize>
	T *
	push_n(const string<OtherTag, OtherSize> *values, uint32_t count)
	{
		static_assert(is_string<T>::value, "push_n(string) can only be used with string arrays");
		T *dest = storage.grow_by(count);
		for (uint32_t i = 0; i < count; i++)
		{
			dest[i].set(values[i].c_str());
		}
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
	set(const array<T, OtherTag> &other)
	{
		clear();
		storage.reserve(other.size());

		if (other.size() > 0 && other.data())
		{
			memcpy(storage.data, other.data(), other.size() * sizeof(T));
			storage.size = other.size();
		}
	}

	template <typename OtherTag, uint32_t OtherSize, typename OtherArrayTag>
	void
	set(const array<string<OtherTag, OtherSize>, OtherArrayTag> &other)
	{
		static_assert(is_string<T>::value, "set(string array) can only be used with string arrays");
		clear();
		storage.reserve(other.size());

		for (uint32_t i = 0; i < other.size(); i++)
		{
			storage.data[i].set(other.data()[i].c_str());
		}
		storage.size = other.size();
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

	static array *
	create()
	{
		auto *arr = (array *)arena<ArenaTag>::alloc(sizeof(array));
		arr->reset();
		return arr;
	}
};

/*---------------------------------------------------------------------------- */

#include <algorithm>
#include <string_view>
#include <cstring>

template <typename ArenaTag = global_arena, uint32_t InitialSize = 16> struct string
{
	contiguous<char, ArenaTag, InitialSize> storage;
	mutable uint32_t						cached_hash = 0;

	// Zero-overhead view of our data
	std::string_view
	view() const
	{
		if (!storage.data || storage.size == 0)
		{
			return "";
		}

		size_t len = storage.size;
		if (len > 0 && storage.data[len - 1] == '\0')
		{
			len--;
		}

		return std::string_view(storage.data, len);
	}

	// Set from any string-like source
	void
	set(std::string_view sv)
	{
		storage.reserve(sv.size() + 1);
		memcpy(storage.data, sv.data(), sv.size());
		storage.data[sv.size()] = '\0';
		storage.size = sv.size() + 1;
		cached_hash = 0;
	}

	void
	set(const char *cstr)
	{
		if (!cstr)
		{
			clear();
			return;
		}
		set(std::string_view(cstr));
	}

	template <typename OtherTag, uint32_t OtherSize>
	void
	set(const string<OtherTag, OtherSize> &other)
	{
		set(other.view());
	}

	// Append using string_view
	void
	append(std::string_view sv)
	{
		if (sv.empty())
			return;

		if (storage.size > 0 && storage.data[storage.size - 1] == '\0')
		{
			storage.size--;
		}

		char *write_pos = storage.grow_by(sv.size() + 1);
		memcpy(write_pos, sv.data(), sv.size());
		write_pos[sv.size()] = '\0';
		cached_hash = 0;
	}
	void
	append(const char *cstr)
	{
		if (cstr)
		{
			append(std::string_view(cstr));
		}
	}

	template <typename OtherTag, uint32_t OtherSize>
	void
	append(const string<OtherTag, OtherSize> &other)
	{
		append(other.view());
	}

	// Split - simplified
	template <typename StringType, typename ArrayTag>
	void
	split(char delimiter, array<StringType, ArrayTag> *result) const
	{
		result->clear();

		std::string_view sv = view();
		if (sv.empty())
			return;

		size_t start = 0;
		size_t end = 0;

		while ((end = sv.find(delimiter, start)) != std::string_view::npos)
		{
			if (end > start)
			{
				StringType substr;
				substr.set(sv.substr(start, end - start));
				result->push(substr);
			}
			start = end + 1;
		}

		if (start < sv.size())
		{
			StringType substr;
			substr.set(sv.substr(start));
			result->push(substr);
		}
	}

	// All comparisons through string_view
	bool
	equals(std::string_view other) const
	{
		return view() == other;
	}

	bool
	equals(const char *cstr) const
	{
		return cstr ? equals(std::string_view(cstr)) : empty();
	}

	template <typename OtherTag, uint32_t OtherSize>
	bool
	equals(const string<OtherTag, OtherSize> &other) const
	{
		// Still use hash for quick rejection when comparing arena strings
		if (cached_hash && other.cached_hash && hash() != other.hash())
		{
			return false;
		}
		return view() == other.view();
	}

	// Find operations using string_view
	size_t
	find(char c) const
	{
		size_t pos = view().find(c);
		return pos == std::string_view::npos ? (size_t)-1 : pos;
	}

	size_t
	find(std::string_view needle) const
	{
		size_t pos = view().find(needle);
		return pos == std::string_view::npos ? (size_t)-1 : pos;
	}

	size_t
	rfind(char c) const
	{
		size_t pos = view().rfind(c);
		return pos == std::string_view::npos ? (size_t)-1 : pos;
	}

	size_t
	find_first_of(std::string_view chars) const
	{
		size_t pos = view().find_first_of(chars);
		return pos == std::string_view::npos ? (size_t)-1 : pos;
	}

	size_t
	find_last_of(std::string_view chars) const
	{
		size_t pos = view().find_last_of(chars);
		return pos == std::string_view::npos ? (size_t)-1 : pos;
	}

	// C++17 compatible starts_with/ends_with
	bool
	starts_with(std::string_view prefix) const
	{
		auto v = view();
		return v.size() >= prefix.size() && v.substr(0, prefix.size()) == prefix;
	}

	bool
	ends_with(std::string_view suffix) const
	{
		auto v = view();
		return v.size() >= suffix.size() && v.substr(v.size() - suffix.size()) == suffix;
	}

	// Substring extraction
	string
	substr(size_t pos, size_t len = std::string_view::npos) const
	{
		string result;
		auto   v = view();
		if (pos < v.size())
		{
			result.set(v.substr(pos, len));
		}
		return result;
	}

	// Trimming
	void
	trim()
	{
		const char *whitespace = " \t\n\r\f\v";
		trim(whitespace);
	}

	void
	trim(std::string_view chars)
	{
		auto sv = view();

		size_t start = sv.find_first_not_of(chars);
		if (start == std::string_view::npos)
		{
			clear();
			return;
		}

		size_t end = sv.find_last_not_of(chars);
		set(sv.substr(start, end - start + 1));
	}

	void
	ltrim(std::string_view chars = " \t\n\r\f\v")
	{
		auto   sv = view();
		size_t start = sv.find_first_not_of(chars);
		if (start == std::string_view::npos)
		{
			clear();
		}
		else if (start > 0)
		{
			set(sv.substr(start));
		}
	}

	void
	rtrim(std::string_view chars = " \t\n\r\f\v")
	{
		auto   sv = view();
		size_t end = sv.find_last_not_of(chars);
		if (end == std::string_view::npos)
		{
			clear();
		}
		else if (end < sv.size() - 1)
		{
			set(sv.substr(0, end + 1));
		}
	}

	// In-place transformations (these modify the buffer)
	void
	to_lower()
	{
		std::transform(storage.data, storage.data + length(), storage.data,
					   [](unsigned char c) { return std::tolower(c); });
		cached_hash = 0;
	}

	void
	to_upper()
	{
		std::transform(storage.data, storage.data + length(), storage.data,
					   [](unsigned char c) { return std::toupper(c); });
		cached_hash = 0;
	}

	void
	replace_all(char old_char, char new_char)
	{
		std::replace(storage.data, storage.data + length(), old_char, new_char);
		cached_hash = 0;
	}

	// Counting
	size_t
	count(char c) const
	{
		return std::count(view().begin(), view().end(), c);
	}

	bool
	contains(char c) const
	{
		return view().find(c) != std::string_view::npos;
	}

	bool
	contains(std::string_view needle) const
	{
		return view().find(needle) != std::string_view::npos;
	}

	// Core string operations
	uint32_t
	hash() const
	{
		if (cached_hash != 0)
			return cached_hash;

		auto sv = view();
		if (sv.empty())
		{
			cached_hash = 1;
			return cached_hash;
		}

		uint32_t h = 2166136261u;
		for (unsigned char c : sv)
		{
			h ^= c;
			h *= 16777619u;
		}

		cached_hash = h ? h : 1;
		return cached_hash;
	}

	const char *
	c_str() const
	{
		if (!storage.data || storage.size == 0)
		{
			return "";
		}

		if (storage.data[storage.size - 1] != '\0')
		{
			const_cast<string *>(this)->storage.reserve(storage.size + 1);
			const_cast<string *>(this)->storage.data[storage.size] = '\0';
			const_cast<string *>(this)->storage.size++;
		}
		return storage.data;
	}

	uint32_t
	length() const
	{
		return view().size();
	}
	bool
	empty() const
	{
		return view().empty();
	}

	void
	clear()
	{
		storage.clear();
		cached_hash = 0;
	}

	void
	reserve(uint32_t min_capacity)
	{
		storage.reserve(min_capacity);
	}

	// All comparison operators via string_view
	bool
	operator==(std::string_view other) const
	{
		return view() == other;
	}
	bool
	operator!=(std::string_view other) const
	{
		return view() != other;
	}
	bool
	operator<(std::string_view other) const
	{
		return view() < other;
	}
	bool
	operator>(std::string_view other) const
	{
		return view() > other;
	}
	bool
	operator<=(std::string_view other) const
	{
		return view() <= other;
	}
	bool
	operator>=(std::string_view other) const
	{
		return view() >= other;
	}

	bool
	operator==(const char *other) const
	{
		return other ? view() == other : empty();
	}

	template <typename OtherTag, uint32_t OtherSize>
	bool
	operator==(const string<OtherTag, OtherSize> &other) const
	{
		return equals(other);
	}

	// Assignment operators
	string &
	operator=(std::string_view sv)
	{
		set(sv);
		return *this;
	}

	string &
	operator=(const char *cstr)
	{
		set(cstr);
		return *this;
	}

	template <typename OtherTag, uint32_t OtherSize>
	string &
	operator=(const string<OtherTag, OtherSize> &other)
	{
		set(other.view());
		return *this;
	}

	// Append operators
	string &
	operator+=(std::string_view sv)
	{
		append(sv);
		return *this;
	}

	string &
	operator+=(const char *cstr)
	{
		append(cstr);
		return *this;
	}

	template <typename OtherTag, uint32_t OtherSize>
	string &
	operator+=(const string<OtherTag, OtherSize> &other)
	{
		append(other.view());
		return *this;
	}

	// Access
	char &
	operator[](uint32_t index)
	{
		assert(index < storage.size);
		return storage.data[index];
	}

	const char &
	operator[](uint32_t index) const
	{
		assert(index < storage.size);
		return storage.data[index];
	}

	operator const char *() const
	{
		return c_str();
	}
	operator std::string_view() const
	{
		return view();
	}

	// Accessors
	char *
	data()
	{
		return storage.data;
	}
	const char *
	data() const
	{
		return storage.data;
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

	// Factory methods
	static string *
	create()
	{
		auto *str = (string *)arena<ArenaTag>::alloc(sizeof(string));

		str->storage.reset();
		str->cached_hash = 0;
		return str;
	}

	static string
	make(std::string_view sv)
	{
		string s;
		s.set(sv);
		return s;
	}
};

/*------------------------------------------------------------------ */

template <typename ArenaTag, uint32_t InitialSize> struct string;
inline uint32_t
hash_32(uint32_t x)
{
	x = ((x >> 16) ^ x) * 0x45d9f3b;
	x = ((x >> 16) ^ x) * 0x45d9f3b;
	x = (x >> 16) ^ x;
	return x;
}

inline uint64_t
hash_64(uint64_t x)
{
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
	x = x ^ (x >> 31);
	return x;
}

template <typename T>
inline uint32_t
hash_int(T x)
{
	using U = typename std::make_unsigned<T>::type;
	U ux = static_cast<U>(x);

	if constexpr (sizeof(T) <= 4)
	{
		return hash_32(static_cast<uint32_t>(ux));
	}
	else
	{
		return static_cast<uint32_t>(hash_64(static_cast<uint64_t>(ux)));
	}
}

template <typename K, typename V> struct pair
{
	K key;
	V value;
};

enum hash_slot_state : uint8_t
{
	EMPTY = 0,
	OCCUPIED = 1,
	DELETED = 2
};
template <typename K, typename V, typename ArenaTag = global_arena> struct hash_map
{
	struct Entry
	{
		K				key;
		V				value;
		uint32_t		hash;
		hash_slot_state state;
	};

	contiguous<Entry, ArenaTag, 16> storage;
	uint32_t						_size = 0;		// Occupied entries
	uint32_t						tombstones = 0; // Deleted entries

	template <typename T> struct is_string : std::false_type
	{
	};
	template <typename Tag, uint32_t Size> struct is_string<string<Tag, Size>> : std::true_type
	{
	};

	// Helper to get string_view from any string-like key
	template <typename KeyType>
	static auto
	get_key_view(const KeyType &key)
	{
		if constexpr (std::is_same_v<std::decay_t<KeyType>, const char *> ||
					  std::is_same_v<std::decay_t<KeyType>, char *>)
		{
			return std::string_view(key);
		}
		else if constexpr (is_string<std::decay_t<KeyType>>::value)
		{
			return key.view();
		}
		else
		{
			return key; // For integer keys
		}
	}

	// Unified hash function
	template <typename KeyType>
	uint32_t
	hash_key(const KeyType &key) const
	{
		if constexpr (std::is_same_v<std::decay_t<KeyType>, const char *> ||
					  std::is_same_v<std::decay_t<KeyType>, char *>)
		{
			std::string_view sv(key);
			if (sv.empty())
				return 1;

			uint32_t h = 2166136261u;
			for (unsigned char c : sv)
			{
				h ^= c;
				h *= 16777619u;
			}
			return h ? h : 1;
		}
		else if constexpr (is_string<std::decay_t<KeyType>>::value)
		{
			return key.hash();
		}
		else if constexpr (std::is_integral_v<KeyType>)
		{
			return hash_int(key);
		}
		else
		{
			// static_assert(std::is_integral_v<KeyType> || is_string<std::decay_t<KeyType>>::value,
			// 			  "Key must be an integer or string type");
			return 0;
		}
	}

	// Unified key comparison
	template <typename KeyType>
	bool
	keys_equal(const K &stored_key, const KeyType &search_key) const
	{
		if constexpr (is_string<K>::value)
		{
			if constexpr (std::is_same_v<std::decay_t<KeyType>, const char *> ||
						  std::is_same_v<std::decay_t<KeyType>, char *>)
			{
				return stored_key.view() == search_key;
			}
			else if constexpr (is_string<std::decay_t<KeyType>>::value)
			{
				return stored_key.view() == search_key.view();
			}
			else
			{
				return stored_key == search_key;
			}
		}
		else
		{
			return stored_key == search_key;
		}
	}

	void
	init(uint32_t initial_capacity = 16)
	{
		if (storage.data)
			return;

		// Round up to power of 2
		initial_capacity--;
		initial_capacity |= initial_capacity >> 1;
		initial_capacity |= initial_capacity >> 2;
		initial_capacity |= initial_capacity >> 4;
		initial_capacity |= initial_capacity >> 8;
		initial_capacity |= initial_capacity >> 16;
		initial_capacity++;

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
			if (old_entries[i].state == hash_slot_state::OCCUPIED)
			{
				insert_into_storage(new_storage.data, new_storage.capacity, old_entries[i].key, old_entries[i].hash,
									old_entries[i].value);
			}
		}

		storage.swap(&new_storage);
	}

	void
	insert_into_storage(Entry *entries, uint32_t capacity, const K &key, uint32_t hash, const V &value)
	{
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state != hash_slot_state::OCCUPIED)
			{
				entry.key = key;
				entry.value = value;
				entry.hash = hash;
				entry.state = hash_slot_state::OCCUPIED;
				_size++;
				return;
			}

			if (entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.value = value;
				return;
			}

			idx = (idx + 1) & mask;
		}
	}

	// Single template for all key types
	template <typename KeyType>
	V *
	get(const KeyType &key)
	{
		if (!storage.data || _size == 0)
			return nullptr;

		uint32_t hash = hash_key(key);
		uint32_t mask = storage.capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = storage.data[idx];

			if (entry.state == hash_slot_state::EMPTY)
			{
				return nullptr;
			}

			if (entry.state == hash_slot_state::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key))
			{
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename KeyType>
	const V *
	get(const KeyType &key) const
	{
		return const_cast<hash_map *>(this)->get(key);
	}

	// Single template for insert - handles all key types
	template <typename KeyType>
	V *
	insert(const KeyType &key, const V &value)
	{
		if constexpr (std::is_same_v<std::decay_t<KeyType>, const char *> ||
					  std::is_same_v<std::decay_t<KeyType>, char *>)
		{
			if (!key)
				return nullptr;
		}

		if (!storage.data)
			init();

		if ((_size + tombstones) * 4 >= storage.capacity * 3)
		{
			grow();
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = storage.capacity - 1;
		uint32_t idx = hash & mask;
		uint32_t first_deleted = (uint32_t)-1;

		while (true)
		{
			Entry &entry = storage.data[idx];

			if (entry.state == hash_slot_state::EMPTY)
			{
				Entry *target = (first_deleted != (uint32_t)-1) ? &storage.data[first_deleted] : &entry;

				// Set the key appropriately based on type
				if constexpr (is_string<K>::value)
				{
					if constexpr (std::is_same_v<std::decay_t<KeyType>, const char *> ||
								  std::is_same_v<std::decay_t<KeyType>, char *>)
					{
						target->key.set(key);
					}
					else if constexpr (is_string<std::decay_t<KeyType>>::value)
					{
						target->key.set(key.view());
					}
					else
					{
						target->key = key;
					}
				}
				else
				{
					target->key = key;
				}

				target->value = value;
				target->hash = hash;
				target->state = hash_slot_state::OCCUPIED;

				if (first_deleted != (uint32_t)-1)
					tombstones--;
				_size++;
				return &target->value;
			}

			if (entry.state == hash_slot_state::DELETED)
			{
				if (first_deleted == (uint32_t)-1)
				{
					first_deleted = idx;
				}
			}
			else if (entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.value = value;
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename KeyType>
	bool
	remove(const KeyType &key)
	{
		if (!storage.data || _size == 0)
			return false;

		uint32_t hash = hash_key(key);
		uint32_t mask = storage.capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = storage.data[idx];

			if (entry.state == hash_slot_state::EMPTY)
			{
				return false;
			}

			if (entry.state == hash_slot_state::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.state = hash_slot_state::DELETED;
				_size--;
				tombstones++;
				return true;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename KeyType>
	bool
	contains(const KeyType &key) const
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

	template <typename ArrayTag = ArenaTag>
	void
	collect(array<pair<K, V>, ArrayTag> *out) const
	{
		out->clear();
		if (!storage.data || _size == 0)
			return;

		out->reserve(_size);

		for (uint32_t i = 0; i < storage.capacity; i++)
		{
			const Entry &entry = storage.data[i];
			if (entry.state == hash_slot_state::OCCUPIED)
			{
				pair<K, V> p = {entry.key, entry.value};
				out->push(p);
			}
		}
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

	static hash_map *
	create(uint32_t initial_capacity = 16)
	{
		auto *m = (hash_map *)arena<ArenaTag>::alloc(sizeof(hash_map));

		m->storage.reset();
		m->size = 0;
		m->tombstones = 0;
		m->init(initial_capacity);
		return m;
	}
};

//
#pragma once
#include "arena.hpp"
#include <cstdint>
#include <string_view>

template <typename T, typename ArenaTag = global_arena, uint32_t InitialSize = 8> struct array_
{
	// Storage delegate
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

	// Convenience for string_view from const char*
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
	set(const array_<T, OtherTag> &other)
	{
		clear();
		storage.reserve(other.size());

		if (other.size() > 0 && other.data())
		{
			memcpy(storage.data, other.data(), other.size() * sizeof(T));
			storage.size = other.size();
		}
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

	static array_ *
	create()
	{
		auto *arr = (array_ *)arena<ArenaTag>::alloc(sizeof(array_));
		arr->reset();
		return arr;
	}
};

/*---------------------------------------------------------------------------- */

// inline uint32_t hash_32(uint32_t x)
// {
//     x = ((x >> 16) ^ x) * 0x45d9f3b;
//     x = ((x >> 16) ^ x) * 0x45d9f3b;
//     x = (x >> 16) ^ x;
//     return x;
// }

// inline uint64_t hash_64(uint64_t x)
// {
//     x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
//     x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
//     x = x ^ (x >> 31);
//     return x;
// }

// template <typename T>
// inline uint32_t hash_int(T x)
// {
//     using U = typename std::make_unsigned<T>::type;
//     U ux = static_cast<U>(x);

//     if constexpr (sizeof(T) <= 4)
//     {
//         return hash_32(static_cast<uint32_t>(ux));
//     }
//     else
//     {
//         return static_cast<uint32_t>(hash_64(static_cast<uint64_t>(ux)));
//     }
// }

inline uint32_t
hash_string_view(std::string_view sv)
{
	if (sv.empty())
		return 1;

	uint32_t h = 2166136261u;
	for (unsigned char c : sv)
	{
		h ^= c;
		h *= 16777619u;
	}
	return h ? h : 1;
}

// template <typename K, typename V>
// struct pair
// {
//     K key;
//     V value;
// };

// enum hash_slot_state : uint8_t
// {
//     EMPTY = 0,
//     OCCUPIED = 1,
//     DELETED = 2
// };

template <typename K, typename V, typename ArenaTag = global_arena> struct hash_map_
{
	struct Entry
	{
		K				key;
		V				value;
		uint32_t		hash;
		hash_slot_state state;
	};

	contiguous<Entry, ArenaTag, 16> storage;
	uint32_t						_size = 0;		// Occupied entries
	uint32_t						tombstones = 0; // Deleted entries

	// Hash function
	template <typename KeyType>
	uint32_t
	hash_key(const KeyType &key) const
	{
		// Add this debug line to see what KeyType actually is
		if constexpr (std::is_same_v<std::decay_t<KeyType>, std::string_view>)
		{
			if (key.empty())
				return 1;

			uint32_t h = 2166136261u;
			for (unsigned char c : key)
			{
				h ^= c;
				h *= 16777619u;
			}
			return h ? h : 1;
		}
		else if constexpr (std::is_same_v<std::decay_t<KeyType>, const char *> ||
						   std::is_same_v<std::decay_t<KeyType>, char *>)
		{
			std::string_view sv(key);
			if (sv.empty())
				return 1;

			uint32_t h = 2166136261u;
			for (unsigned char c : sv)
			{
				h ^= c;
				h *= 16777619u;
			}
			return h ? h : 1;
		}
		else if constexpr (std::is_integral_v<KeyType>)
		{
			return hash_int(key);
		}
		else
		{
			// This is where you're ending up
			printf("WARNING: hash_key falling through for type: %s\n", typeid(KeyType).name());
			return 0;
		}
	}

	template <typename KeyType>
	bool
	keys_equal(const K &stored_key, const KeyType &search_key) const
	{
		if constexpr (std::is_same_v<K, std::string_view>)
		{
			if constexpr (std::is_same_v<std::decay_t<KeyType>, const char *>) // Add std::decay_t
			{
				return stored_key == search_key;
			}
			else
			{
				return stored_key == search_key;
			}
		}
		else
		{
			return stored_key == search_key;
		}
	}

	void
	init(uint32_t initial_capacity = 16)
	{
		if (storage.data)
			return;

		// Round up to power of 2
		initial_capacity--;
		initial_capacity |= initial_capacity >> 1;
		initial_capacity |= initial_capacity >> 2;
		initial_capacity |= initial_capacity >> 4;
		initial_capacity |= initial_capacity >> 8;
		initial_capacity |= initial_capacity >> 16;
		initial_capacity++;

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
			if (old_entries[i].state == hash_slot_state::OCCUPIED)
			{
				insert_into_storage(new_storage.data, new_storage.capacity, old_entries[i].key, old_entries[i].hash,
									old_entries[i].value);
			}
		}

		storage.swap(&new_storage);
	}

	void
	insert_into_storage(Entry *entries, uint32_t capacity, const K &key, uint32_t hash, const V &value)
	{
		uint32_t mask = capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = entries[idx];

			if (entry.state != hash_slot_state::OCCUPIED)
			{
				entry.key = key;
				entry.value = value;
				entry.hash = hash;
				entry.state = hash_slot_state::OCCUPIED;
				_size++;
				return;
			}

			if (entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.value = value;
				return;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename KeyType>
	V *
	get(const KeyType &key)
	{
		if (!storage.data || _size == 0)
			return nullptr;

		uint32_t hash = hash_key(key);
		uint32_t mask = storage.capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = storage.data[idx];

			if (entry.state == hash_slot_state::EMPTY)
			{
				return nullptr;
			}

			if (entry.state == hash_slot_state::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key))
			{
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename KeyType>
	const V *
	get(const KeyType &key) const
	{
		return const_cast<hash_map_ *>(this)->get(key);
	}

	template <typename KeyType>
	V *
	insert(const KeyType &key, const V &value)
	{
		if constexpr (std::is_same_v<KeyType, const char *>)
		{
			if (!key)
				return nullptr;
		}

		if (!storage.data)
			init();

		if ((_size + tombstones) * 4 >= storage.capacity * 3)
		{
			grow();
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = storage.capacity - 1;
		uint32_t idx = hash & mask;
		uint32_t first_deleted = (uint32_t)-1;

		while (true)
		{
			Entry &entry = storage.data[idx];

			if (entry.state == hash_slot_state::EMPTY)
			{
				Entry *target = (first_deleted != (uint32_t)-1) ? &storage.data[first_deleted] : &entry;

				// For string_view, just assign directly
				if constexpr (std::is_same_v<K, std::string_view> && std::is_same_v<KeyType, const char *>)
				{
					target->key = std::string_view(key);
				}
				else
				{
					target->key = key;
				}

				target->value = value;
				target->hash = hash;
				target->state = hash_slot_state::OCCUPIED;

				if (first_deleted != (uint32_t)-1)
					tombstones--;
				_size++;
				return &target->value;
			}

			if (entry.state == hash_slot_state::DELETED)
			{
				if (first_deleted == (uint32_t)-1)
				{
					first_deleted = idx;
				}
			}
			else if (entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.value = value;
				return &entry.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename KeyType>
	bool
	remove(const KeyType &key)
	{
		if (!storage.data || _size == 0)
			return false;

		uint32_t hash = hash_key(key);
		uint32_t mask = storage.capacity - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			Entry &entry = storage.data[idx];

			if (entry.state == hash_slot_state::EMPTY)
			{
				return false;
			}

			if (entry.state == hash_slot_state::OCCUPIED && entry.hash == hash && keys_equal(entry.key, key))
			{
				entry.state = hash_slot_state::DELETED;
				_size--;
				tombstones++;
				return true;
			}

			idx = (idx + 1) & mask;
		}
	}

	template <typename KeyType>
	bool
	contains(const KeyType &key) const
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

	template <typename array_Tag = ArenaTag>
	void
	collect(array_<pair<K, V>, array_Tag> *out) const
	{
		out->clear();
		if (!storage.data || _size == 0)
			return;

		out->reserve(_size);

		for (uint32_t i = 0; i < storage.capacity; i++)
		{
			const Entry &entry = storage.data[i];
			if (entry.state == hash_slot_state::OCCUPIED)
			{
				pair<K, V> p = {entry.key, entry.value};
				out->push(p);
			}
		}
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

	static hash_map_ *
	create(uint32_t initial_capacity = 16)
	{
		auto *m = (hash_map_ *)arena<ArenaTag>::alloc(sizeof(hash_map_));
		m->storage.reset();
		m->_size = 0;
		m->tombstones = 0;
		m->init(initial_capacity);
		return m;
	}
};
