#include "../arena.hpp"
#include "../containers.hpp"
#include <cstdio>
#include <cassert>

struct test_arena {};

/* Some basic tests, by no-means bullet-proof */

inline void
test_array()
{
	arena<test_arena>::init(1024 * 1024);

	{
		array<int, test_arena> arr;
		assert(arr.size() == 0);
		assert(arr.empty());

		for (int i = 0; i < 100; i++)
		{
			arr.push(i * 2);
		}
		assert(arr.size() == 100);
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
		assert(arr.size() == 150);

		arr.reserve(500);
		assert(arr.capacity() >= 500);
		assert(arr.size() == 150);

		arr.resize(200);
		assert(arr.size() == 200);

		arr.clear();
		assert(arr.size() == 0);
		assert(arr.capacity() >= 500);
	}

	{
		array<int, test_arena> arr1;
		for (int i = 0; i < 1000; i++)
		{
			arr1.push(i);
		}

		array<int, test_arena> arr2;
		arr2.set(arr1);

		assert(arr2.size() == arr1.size());
		for (uint32_t i = 0; i < arr1.size(); i++)
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
		assert(arr.capacity() >= 1000);

		arr.shrink_to_fit();
		assert(arr.capacity() == 10);
		assert(arr.size() == 10);
	}

	{
		auto *heap_arr = array<int, test_arena>::create();
		heap_arr->push(42);
		heap_arr->push(84);
		assert(heap_arr->size() == 2);
		assert((*heap_arr)[0] == 42);
	}

	printf("  Array memory stats:\n");
	printf("    Reclaimed: %zu bytes\n", arena<test_arena>::reclaimed());
	printf("    Reused: %zu bytes\n", arena<test_arena>::reused());

	arena<test_arena>::reset();
}

inline void
test_string()
{
	arena<test_arena>::reset();

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

		assert(parts.size() == 5);
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

	// Test new string_view based operations
	{
		string<test_arena> str;
		str.set("Test string for find operations");

		assert(str.find('s') == 2);
		assert(str.find("string") == 5);
		assert(str.starts_with("Test"));
		assert(str.ends_with("operations"));
		assert(!str.starts_with("test")); // case sensitive

		auto substr = str.substr(5, 6);
		assert(substr.equals("string"));
	}

	{
		string<test_arena> str;
		str.set("  trim test  ");
		str.trim();
		assert(str.equals("trim test"));

		str.set("  left trim");
		str.ltrim();
		assert(str.equals("left trim"));

		str.set("right trim  ");
		str.rtrim();
		assert(str.equals("right trim"));
	}

	{
		string<test_arena> str;
		str.set("UPPERCASE");
		str.to_lower();
		assert(str.equals("uppercase"));

		str.set("lowercase");
		str.to_upper();
		assert(str.equals("LOWERCASE"));

		str.set("a-b-c-d");
		str.replace_all('-', '_');
		assert(str.equals("a_b_c_d"));

		assert(str.count('_') == 3);
		assert(str.contains("b_c"));
		assert(!str.contains("xyz"));
	}

	printf("  String memory stats:\n");
	printf("    Reclaimed: %zu bytes\n", arena<test_arena>::reclaimed());
	printf("    Reused: %zu bytes\n", arena<test_arena>::reused());

	arena<test_arena>::reset();
}

inline void
test_hash_map()
{
	arena<test_arena>::reset();

	{
		hash_map<int, int, test_arena> map;
		map.init();

		for (int i = 0; i < 100; i++)
		{
			map.insert(i, i * 10);
		}
		assert(map.size() == 100);

		for (int i = 0; i < 100; i++)
		{
			int *val = map.get(i);
			assert(val != nullptr);
			assert(*val == i * 10);
		}

		map.insert(50, 999);
		assert(*map.get(50) == 999);
		assert(map.size() == 100);

		assert(map.contains(75));
		assert(!map.contains(200));

		assert(map.remove(25));
		assert(!map.contains(25));
		assert(map.size() == 99);
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

		assert(map.size() == 3);
		assert(*map.get(key1) == 100);
		assert(*map.get(key2) == 200);
		assert(*map.get(key3) == 300);

		// Test const char* key access
		assert(*map.get("first") == 100);
		assert(*map.get("second") == 200);
		assert(map.contains("third"));
		assert(!map.contains("fourth"));

		map.insert("fourth", 400);
		assert(map.size() == 4);
		assert(*map.get("fourth") == 400);

		assert(map.remove("second"));
		assert(map.size() == 3);
		assert(!map.contains("second"));
	}




	{
		hash_map<int, int, test_arena> map;
		map.init(4);

		for (int i = 0; i < 1000; i++)
		{
			map.insert(i, i * 2);
		}

		assert(map.size() == 1000);
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

		assert(pairs.size() == 10);

		int sum = 0;
		for (uint32_t i = 0; i < pairs.size(); i++)
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
		assert(map.size() == 50);

		map.clear();
		assert(map.size() == 0);
		assert(map.empty());
		assert(!map.contains(25));
	}

	arena<test_arena>::reset();
}

inline void
test_cross_arena_operations()
{
	struct arena1 {};
	struct arena2 {};

	arena<arena1>::init(1024 * 1024);
	arena<arena2>::init(1024 * 1024);

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
			char buf[32];
			snprintf(buf, sizeof(buf), "String %d", i);
			s.set(buf);
			arr1.push(s);
		}

		array<string<arena2>, arena2> arr2;
		arr2.set(arr1);

		assert(arr2.size() == arr1.size());
		for (uint32_t i = 0; i < arr1.size(); i++)
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

	arena<arena1>::shutdown();
	arena<arena2>::shutdown();
}

inline void
test_stream_allocation()
{
	arena<test_arena>::init(1024 * 1024);
	arena<test_arena>::reset();

	{
		auto stream = arena_stream_begin<test_arena>(256);

		const char *data1 = "Hello ";
		arena_stream_write(&stream, data1, strlen(data1));

		const char *data2 = "World!";
		arena_stream_write(&stream, data2, strlen(data2) + 1);

		char *result = (char *)arena_stream_finish(&stream);
		assert(strcmp(result, "Hello World!") == 0);
		assert(arena_stream_size(&stream) == strlen("Hello World!") + 1);
	}

	{
		auto stream = arena_stream_begin<test_arena>(16);

		char buffer[1024];
		memset(buffer, 'A', sizeof(buffer));
		arena_stream_write(&stream, buffer, sizeof(buffer));

		char more[512];
		memset(more, 'B', sizeof(more));
		arena_stream_write(&stream, more, sizeof(more));

		uint8_t *result = arena_stream_finish(&stream);
		assert(arena_stream_size(&stream) == 1024 + 512);

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
		size_t before = arena<test_arena>::used();

		auto stream = arena_stream_begin<test_arena>(1024);
		arena_stream_write(&stream, "test", 4);
		arena_stream_abandon(&stream);

		size_t after = arena<test_arena>::used();
		assert(after == before);
	}

	printf("  Stream allocation memory stats:\n");
	printf("    Used: %zu bytes\n", arena<test_arena>::used());
	printf("    Committed: %zu bytes\n", arena<test_arena>::committed());

	arena<test_arena>::reset();
}

inline void
test_memory_reuse_patterns()
{
	printf("\n=== Testing memory reuse patterns ===\n");

	arena<test_arena>::reset();

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
	printf("    Total reclaimed: %zu bytes\n", arena<test_arena>::reclaimed());
	printf("    Total reused: %zu bytes\n", arena<test_arena>::reused());
	printf("    Currently in freelists: %zu bytes\n", arena<test_arena>::freelist_bytes());
	printf("    Reuse efficiency: %.2f%%\n",
		   arena<test_arena>::reclaimed() > 0 ? (100.0 * arena<test_arena>::reused() / arena<test_arena>::reclaimed())
											  : 0.0);

	arena<test_arena>::print_stats();
}

inline void
test_string_view_interning()
{
    printf("\n=== Testing string_view interning ===\n");

    arena<test_arena>::reset();

    // Test basic interning
    {
        std::string_view sv1 = arena_intern<test_arena>("Hello, World!");
        std::string_view sv2 = arena_intern<test_arena>("Hello, World!");

        assert(sv1 == "Hello, World!");
        assert(sv2 == "Hello, World!");
        // Different pointers since we don't deduplicate
        assert(sv1.data() != sv2.data());
    }

    // Test array with interned string_views
    {
        array<std::string_view, test_arena> arr;

        // Intern some strings
        arr.push(arena_intern<test_arena>("first"));
        arr.push(arena_intern<test_arena>("second"));
        arr.push(arena_intern<test_arena>("third"));

        // Also test with temporary strings that would be unsafe without interning
        for (int i = 0; i < 10; i++) {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "item_%d", i);
            arr.push(arena_intern<test_arena>(buffer));
        }

        assert(arr.size() == 13);
        assert(arr[0] == "first");
        assert(arr[1] == "second");
        assert(arr[2] == "third");
        assert(arr[3] == "item_0");
        assert(arr[12] == "item_9");

        // Test that the strings survive even after clearing other things
        arena<test_arena>::print_stats();
    }

    // Test hash_map with interned string_view keys
    {
        hash_map_<std::string_view, int, test_arena> map;
        map.init();

        // Use interned strings as keys
        map.insert(arena_intern<test_arena>("apple"), 100);
        map.insert(arena_intern<test_arena>("banana"), 200);
        map.insert(arena_intern<test_arena>("cherry"), 300);



    	int x = *(int*)map.get("apple");
    	(*map.get("banana") == 200);
    	(*map.get("cherry") == 300);
        // Can still look up with literals (they compare equal)
        assert(*map.get("apple") == 100);
        assert(*map.get("banana") == 200);
        assert(*map.get("cherry") == 300);

        // Test with dynamic strings
        for (int i = 0; i < 50; i++) {
            char key[32];
            snprintf(key, sizeof(key), "key_%d", i);
            map.insert(arena_intern<test_arena>(key), i * 10);
        }

        assert(map.size() == 53);
        assert(*map.get("key_25") == 250);
        assert(*map.get("key_49") == 490);
    }

    // Test interning from existing strings
    {
        string<test_arena> source;
        source.set("This is a longer string that we want to intern");

        std::string_view interned = arena_intern<test_arena>(source.view());

        array_<std::string_view, test_arena> arr;
        arr.push(interned);

        // Original can be modified without affecting the interned copy
        source.set("Changed!");
        assert(arr[0] == "This is a longer string that we want to intern");
    }

    // Test hash_map with string_view values
    {
        hash_map_<int, std::string_view, test_arena> map;
        map.init();

        for (int i = 0; i < 20; i++) {
            char value[64];
            snprintf(value, sizeof(value), "Value for key %d", i);
            map.insert(i, arena_intern<test_arena>(value));
        }

        assert(map.size() == 20);
        assert(*map.get(0) == "Value for key 0");
        assert(*map.get(19) == "Value for key 19");
    }


    // Test collection of pairs with string_view
    {
        hash_map_<std::string_view, std::string_view, test_arena> map;
        map.init();

        map.insert(arena_intern<test_arena>("name"),
                  arena_intern<test_arena>("Alice"));
        map.insert(arena_intern<test_arena>("city"),
                  arena_intern<test_arena>("New York"));
        map.insert(arena_intern<test_arena>("country"),
                  arena_intern<test_arena>("USA"));

        array_<pair<std::string_view, std::string_view>, test_arena> pairs;
        map.collect(&pairs);

        assert(pairs.size() == 3);
        bool found_name = false;
        for (uint32_t i = 0; i < pairs.size(); i++) {
            if (pairs[i].key == "name") {
                assert(pairs[i].value == "Alice");
                found_name = true;
            }
        }
        assert(found_name);
    }

    printf("  String interning memory stats:\n");
    printf("    Total used: %zu bytes\n", arena<test_arena>::used());
    printf("    Total committed: %zu bytes\n", arena<test_arena>::committed());

    arena<test_arena>::reset();
}

inline int
test_containers()
{
    arena<test_arena>::init();
	// test_array();
	// test_string();
	// test_hash_map();
	// test_cross_arena_operations();
	// test_stream_allocation();
	// test_memory_reuse_patterns();
	test_string_view_interning();

	arena<test_arena>::shutdown();

	return 0;
}
