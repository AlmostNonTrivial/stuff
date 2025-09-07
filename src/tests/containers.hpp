#include "../arena.hpp"
#include <cstdio>
#include <cassert>

struct test_arena
{
};


/* Some basic tests, by no-means bullet-proof */


inline void
test_array()
{

	Arena<test_arena>::init(1024 * 1024);

	{
		array<int, test_arena> arr;
		assert(arr.size == 0);
		assert(arr.empty());

		for (int i = 0; i < 100; i++)
		{
			arr.push(i * 2);
		}
		assert(arr.size == 100);
		assert(!arr.empty());

		for (int i = 0; i < 100; i++)
		{
			assert(arr[i] == i * 2);
		}

		int batch[50];
		for (int i = 0; i < 50; i++)
		{
			batch[i] = i + 1000;
		}
		arr.push_n(batch, 50);
		assert(arr.size == 150);

		arr.reserve(500);
		assert(arr.capacity >= 500);
		assert(arr.size == 150);

		arr.resize(200);
		assert(arr.size == 200);

		arr.clear();
		assert(arr.size == 0);
		assert(arr.capacity >= 500);
	}

	{
		array<int, test_arena> arr1;
		for (int i = 0; i < 1000; i++)
		{
			arr1.push(i);
		}

		array<int, test_arena> arr2;

		arr2.set(arr1);

		assert(arr2.size == arr1.size);
		for (uint32_t i = 0; i < arr1.size; i++)
		{
			assert(arr2[i] == arr1[i]);
		}

		arr1.clear();
		for (int i = 0; i < 2000; i++)
		{
			arr1.push(i * 3);
		}
	}

	{
		array<int, test_arena> arr;
		for (int i = 0; i < 10; i++)
		{
			arr.push(i);
		}

		int sum = 0;
		for (int val : arr)
		{
			sum += val;
		}
		assert(sum == 45);
	}

	{
		array<int, test_arena> arr;
		arr.reserve(1000);
		for (int i = 0; i < 10; i++)
		{
			arr.push(i);
		}
		assert(arr.capacity >= 1000);

		arr.shrink_to_fit();
		assert(arr.capacity == 10);
		assert(arr.size == 10);
	}

	{
		auto *heap_arr = array<int, test_arena>::create();
		heap_arr->push(42);
		heap_arr->push(84);
		assert(heap_arr->size == 2);
		assert((*heap_arr)[0] == 42);
	}

	printf("  Array memory stats:\n");
	printf("    Reclaimed: %zu bytes\n", Arena<test_arena>::reclaimed());
	printf("    Reused: %zu bytes\n", Arena<test_arena>::reused());

	Arena<test_arena>::reset();
}

inline void
test_string()
{

	Arena<test_arena>::reset();

	{
		string<test_arena> str;
		assert(str.empty());
		assert(str.length() == 0);

		str.set("Hello, World!");
		assert(!str.empty());
		assert(str.length() == 13);
		assert(str.equals("Hello, World!"));

		const char *cstr = str.c_str();
		assert(strcmp(cstr, "Hello, World!") == 0);
	}

	{
		string<test_arena> str;
		str.set("Hello");
		str.append(", ");
		str.append("World");
		str.append("!");
		assert(str.equals("Hello, World!"));

		string<test_arena> str2;
		str2.set(" More text");
		str.append(str2);
		assert(str.equals("Hello, World! More text"));
	}

	{
		string<test_arena> str1;
		str1.set("test string");
		uint32_t hash1 = str1.hash();

		string<test_arena> str2;
		str2.set("test string");
		uint32_t hash2 = str2.hash();

		assert(hash1 == hash2);
		assert(str1.equals(str2));

		str2.set("different");
		assert(str1.hash() != str2.hash());
		assert(!str1.equals(str2));
	}

	{
		string<test_arena> str;
		str.set("one,two,three,four,five");

		array<string<test_arena>, test_arena> parts;
		str.split(',', &parts);

		assert(parts.size == 5);
		assert(parts[0].equals("one"));
		assert(parts[1].equals("two"));
		assert(parts[2].equals("three"));
		assert(parts[3].equals("four"));
		assert(parts[4].equals("five"));
	}

	{
		string<test_arena> str1;
		str1 = "Assignment test";
		assert(str1.equals("Assignment test"));

		string<test_arena> str2;
		str2 = str1;
		assert(str2.equals("Assignment test"));

		const char *literal = "Literal assignment";
		str1 = literal;
		assert(str1.equals("Literal assignment"));
	}

	{
		string<test_arena> str;
		str.reserve(1000);
		str.set("Small");

		str.reserve(2000);
		str.append(" text that causes reallocation");
	}

	{
		auto str = string<test_arena>::make("Factory string");
		assert(str.equals("Factory string"));

		auto *heap_str = string<test_arena>::create();
		heap_str->set("Heap string");
		assert(heap_str->equals("Heap string"));
	}

	printf("  String memory stats:\n");
	printf("    Reclaimed: %zu bytes\n", Arena<test_arena>::reclaimed());
	printf("    Reused: %zu bytes\n", Arena<test_arena>::reused());

	Arena<test_arena>::reset();
}

inline void
test_hash_map()
{

	Arena<test_arena>::reset();

	{
		hash_map<int, int, test_arena> map;
		map.init();

		for (int i = 0; i < 100; i++)
		{
			map.insert(i, i * 10);
		}
		assert(map.size == 100);

		for (int i = 0; i < 100; i++)
		{
			int *val = map.get(i);
			assert(val != nullptr);
			assert(*val == i * 10);
		}

		map.insert(50, 999);
		assert(*map.get(50) == 999);
		assert(map.size == 100);

		assert(map.contains(75));
		assert(!map.contains(200));

		assert(map.remove(25));
		assert(!map.contains(25));
		assert(map.size == 99);
		assert(!map.remove(25));
	}

	{
		hash_map<string<test_arena>, int, test_arena> map;
		map.init();

		string<test_arena> key1;
		key1.set("first");
		map.insert(key1, 100);

		string<test_arena> key2;
		key2.set("second");
		map.insert(key2, 200);

		string<test_arena> key3;
		key3.set("third");
		map.insert(key3, 300);

		assert(map.size == 3);
		assert(*map.get(key1) == 100);
		assert(*map.get(key2) == 200);
		assert(*map.get(key3) == 300);

		assert(*map.get("first") == 100);
		assert(*map.get("second") == 200);
		assert(map.contains("third"));
		assert(!map.contains("fourth"));

		map.insert("fourth", 400);
		assert(map.size == 4);
		assert(*map.get("fourth") == 400);

		assert(map.remove("second"));
		assert(map.size == 3);
		assert(!map.contains("second"));
	}

	{
		hash_map<int, int, test_arena> map;
		map.init();

		map[10] = 100;
		map[20] = 200;
		assert(map[10] == 100);
		assert(map[20] == 200);

		assert(map[30] == 0);
		assert(map.contains(30));
	}

	{
		hash_map<int, int, test_arena> map;
		map.init(4);

		for (int i = 0; i < 1000; i++)
		{
			map.insert(i, i * 2);
		}

		assert(map.size == 1000);
		for (int i = 0; i < 1000; i++)
		{
			assert(*map.get(i) == i * 2);
		}
	}

	{
		hash_map<int, int, test_arena> map;
		map.init();

		for (int i = 0; i < 10; i++)
		{
			map.insert(i, i * 100);
		}

		array<pair<int, int>, test_arena> pairs;
		map.collect(&pairs);

		assert(pairs.size == 10);

		int sum = 0;
		for (uint32_t i = 0; i < pairs.size; i++)
		{
			sum += pairs[i].value;
		}
		assert(sum == 4500);
	}

	{
		hash_map<int, int, test_arena> map;
		map.init();

		for (int i = 0; i < 50; i++)
		{
			map.insert(i, i);
		}
		assert(map.size == 50);

		map.clear();
		assert(map.size == 0);
		assert(map.empty());
		assert(!map.contains(25));
	}

	Arena<test_arena>::reset();
}

inline void
test_cross_arena_operations()
{

	struct arena1
	{
	};
	struct arena2
	{
	};

	Arena<arena1>::init(1024 * 1024);
	Arena<arena2>::init(1024 * 1024);

	{
		string<arena1> str1;
		str1.set("Cross-arena string");

		string<arena2> str2;
		str2.set(str1);

		assert(str2.equals(str1));
		assert(str2.equals("Cross-arena string"));
	}

	{
		array<string<arena1>, arena1> arr1;
		for (int i = 0; i < 5; i++)
		{
			string<arena1> s;
			char		   buf[32];
			snprintf(buf, sizeof(buf), "String %d", i);
			s.set(buf);
			arr1.push(s);
		}

		array<string<arena2>, arena2> arr2;
		arr2.set(arr1);

		assert(arr2.size == arr1.size);
		for (uint32_t i = 0; i < arr1.size; i++)
		{
			assert(arr2[i].equals(arr1[i]));
		}
	}

	{
		hash_map<string<arena1>, int, arena1> map;
		map.init();

		string<arena2> key_from_arena2;
		key_from_arena2.set("key from arena 2");

		map.insert(key_from_arena2, 42);
		assert(map.contains(key_from_arena2));
		assert(*map.get(key_from_arena2) == 42);
	}

	Arena<arena1>::shutdown();
	Arena<arena2>::shutdown();
}

inline void
test_stream_allocation()
{

	Arena<test_arena>::init(1024 * 1024);
	Arena<test_arena>::reset();

	{
		auto stream = arena::stream_begin<test_arena>(256);

		const char *data1 = "Hello ";
		arena::stream_write(&stream, data1, strlen(data1));

		const char *data2 = "World!";
		arena::stream_write(&stream, data2, strlen(data2) + 1);

		char *result = (char *)arena::stream_finish(&stream);
		assert(strcmp(result, "Hello World!") == 0);
		assert(arena::stream_size(&stream) == strlen("Hello World!") + 1);
	}

	{
		auto stream = arena::stream_begin<test_arena>(16);

		char buffer[1024];
		memset(buffer, 'A', sizeof(buffer));
		arena::stream_write(&stream, buffer, sizeof(buffer));

		char more[512];
		memset(more, 'B', sizeof(more));
		arena::stream_write(&stream, more, sizeof(more));

		uint8_t *result = arena::stream_finish(&stream);
		assert(arena::stream_size(&stream) == 1024 + 512);

		for (int i = 0; i < 1024; i++)
		{
			assert(result[i] == 'A');
		}
		for (int i = 0; i < 512; i++)
		{
			assert(result[1024 + i] == 'B');
		}
	}

	{
		size_t before = Arena<test_arena>::used();

		auto stream = arena::stream_begin<test_arena>(1024);
		arena::stream_write(&stream, "test", 4);
		arena::stream_abandon(&stream);

		size_t after = Arena<test_arena>::used();
		assert(after == before);
	}

	printf("  Stream allocation memory stats:\n");
	printf("    Used: %zu bytes\n", Arena<test_arena>::used());
	printf("    Committed: %zu bytes\n", Arena<test_arena>::committed());

	Arena<test_arena>::reset();
}

inline void
test_memory_reuse_patterns()
{
	printf("\n=== Testing memory reuse patterns ===\n");

	Arena<test_arena>::reset();

	for (int iteration = 0; iteration < 10; iteration++)
	{
		for (int size = 10; size <= 1000; size *= 10)
		{
			array<int, test_arena> arr;
			arr.reserve(size);
			for (int i = 0; i < size; i++)
			{
				arr.push(i);
			}
			arr.clear();
			arr.shrink_to_fit();
		}

		for (int i = 0; i < 100; i++)
		{
			string<test_arena> str;
			str.reserve(64);
			str.set("Initial string");
			str.reserve(256);
			str.append(" - appended text that makes it longer");
		}

		hash_map<int, int, test_arena> map;
		map.init(8);
		for (int i = 0; i < 100; i++)
		{
			map.insert(i, i * 2);
		}
		map.clear();
	}

	printf("  Final memory reuse stats:\n");
	printf("    Total reclaimed: %zu bytes\n", Arena<test_arena>::reclaimed());
	printf("    Total reused: %zu bytes\n", Arena<test_arena>::reused());
	printf("    Currently in freelists: %zu bytes\n", Arena<test_arena>::freelist_bytes());
	printf("    Reuse efficiency: %.2f%%\n",
		   Arena<test_arena>::reclaimed() > 0 ? (100.0 * Arena<test_arena>::reused() / Arena<test_arena>::reclaimed())
											  : 0.0);

	Arena<test_arena>::print_stats();
}

inline int
test_containers()
{

	test_array();
	test_string();
	test_hash_map();
	test_cross_arena_operations();
	test_stream_allocation();
	test_memory_reuse_patterns();

	Arena<test_arena>::shutdown();

	return 0;
}
