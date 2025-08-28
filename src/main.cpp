#include "arena.hpp"
#include "os_layer.hpp"
#include "pager.hpp"
#include "test_utils.hpp"
#include <cassert>
#include <cstdint>
#include <functional>

#include "arena.hpp"
#include "os_layer.hpp"
#include "pager.hpp"
#include "test_utils.hpp"
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <random>

#define DB "db"

void
test_free_list()
{

	uint32_t size = FREE_PAGES_PER_FREE_PAGE * 5 - 1;
	pager_begin_transaction();
	for (uint32_t i = 1; i <= size; i++)
	{
		pager_new();
	}

	PagerMeta stats1 = pager_get_stats();

	std::cout << stats1.free_pages << ", " << stats1.total_pages << "\n";
	assert(0 == stats1.free_pages && size == stats1.total_pages);

	for (uint32_t i = 1; i <= size; i++)
	{
		pager_delete(i);
	}

	PagerMeta stats3 = pager_get_stats();
	std::cout << stats3.free_pages << ", " << stats3.total_pages << "\n";

	for (uint32_t i = 1; i <= size * 2; i++)
	{
		pager_new();
	}

	stats3 = pager_get_stats();
	std::cout << stats3.free_pages << ", " << stats3.total_pages << "\n";
	assert(0 == stats3.free_pages && size * 2== stats3.total_pages);
}

void
test_rollback()
{
	auto start = hash_file(DB);
	pager_begin_transaction();
	auto  p1 = pager_new();
	auto  p2 = pager_new();
	Page *ptr = (Page *)pager_get(p1);
	ptr->data[0] = 'a';
	pager_commit();
	pager_close();
	pager_init(DB);
	assert(nullptr != pager_get(p1));
	assert(nullptr != pager_get(p2));
	assert('a' == ((Page *)pager_get(p1))->data[0]);
	auto before = hash_file(DB);
	assert(before != start);
	pager_begin_transaction();
	auto p3 = pager_new();
	assert(nullptr != pager_get(p3));
	pager_delete(p2);
	assert(nullptr == pager_get(p2));
	ptr = (Page *)pager_get(p1);
	ptr->data[0] = 'b';
	// for sync without commit
	pager_sync();
	pager_close();
	auto after_sync = hash_file(DB);
	assert(after_sync != before);
	pager_init(DB);
	// rollback applied
	auto after_rollback = hash_file(DB);
	assert(after_rollback == before);
	pager_begin_transaction();
	ptr = (Page *)pager_get(p1);
	ptr->data[0] = 'c';
	pager_rollback();
	ptr = (Page *)pager_get(p1);
	assert('a' == ptr->data[0]);
}

void
test_transaction_semanticts()
{
	auto should_be_zero = pager_new();
	assert(0 == should_be_zero);
	pager_begin_transaction();
	auto should_not_be_zero = pager_new();
	assert(0 != should_not_be_zero);
	auto *valid_ptr = pager_get(should_not_be_zero);

	assert(nullptr != valid_ptr);
	pager_rollback();
	// warning, ptr still points to valid memory
	auto *invalid_ptr = pager_get(should_not_be_zero);
	assert(nullptr == invalid_ptr);
}

void
test_lru()
{
	uint32_t size = MAX_CACHE_ENTRIES; // 3
	pager_begin_transaction();
	auto p1 = pager_new();
	auto p2 = pager_new();
	auto p3 = pager_new();
	auto p4 = pager_new();

	auto *ptr1 = (Page *)pager_get(p1);
	pager_mark_dirty(p1);
	ptr1->data[0] = 'a';
	auto *ptr2 = (Page *)pager_get(p2);
	pager_mark_dirty(p2);
	ptr2->data[0] = 'b';
	auto *ptr3 = (Page *)pager_get(p3);
	pager_mark_dirty(p3);
	ptr3->data[0] = 'c';
	std::cout << ptr1->data[0] << ", " << ptr2->data[0] << ", " << ptr3->data[0] << '\n';
	auto *ptr4 = (Page *)pager_get(p4);
	pager_mark_dirty(p4);
	ptr4->data[0] = 'd'; // now first, c written
	std::cout << ptr1->data[0] << ", " << ptr2->data[0] << ", " << ptr3->data[0] << '\n';
	assert('d' == ptr1->data[0]);
	pager_get(p1);
	std::cout << ptr1->data[0] << ", " << ptr2->data[0] << ", " << ptr3->data[0] << '\n';
	assert('a' == ptr2->data[0]);
	assert('a' == ptr2->data[0]);
	pager_rollback();
}

void
test_getd_delete_patterns()
{
	Array<uint32_t> vals;
	pager_begin_transaction();
	for (uint32_t i = 0; i < 100; i++)
	{
		uint32_t page = pager_new();
		assert(page != 0);
		array_push(&vals, page);
	}
}

void test_on_off() {
   pager_init(DB);
      auto one = hash_file(DB);
   pager_init(DB);
      auto two = hash_file(DB);
   pager_commit();
   auto three = hash_file(DB);
   pager_rollback();
   auto four = hash_file(DB);
   assert(one == two && two == three && three == four);
}



void test_pager_stress()
{
    // Initialize random number generator
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    os_file_delete(DB);
    pager_init(DB);


    // Track created pages
    Array<uint32_t> created_pages;

    const int iterations = 100; // Number of operations to perform
    const char chars[] = "abcdefghijklmnopqrstuvwxyz"; // Characters to write
    const int char_count = sizeof(chars) - 1; // Exclude null terminator
    bool in_transaction = false;
    PagerMeta stats = pager_get_stats();

        uint64_t before_hash = hash_file(DB);

    for (int i = 0; i < iterations; ++i) {
        // Randomly choose an operation: 0=create, 1=write, 2=delete, 3=rollback
        int operation = std::rand() % 4;

        if (operation == 0) { // Create new page
            if(!in_transaction) {
               stats = pager_get_stats();
               before_hash = hash_file(DB);
                pager_begin_transaction();
                in_transaction = true;
            }
            uint32_t page_id = pager_new();
            assert(page_id != 0 && "Failed to create new page");
            array_push(&created_pages, page_id);
            std::cout << "Created page " << page_id << "\n";
        }
        else if (operation == 1 && created_pages.size > 0) { // Write to random page
            if(!in_transaction) {
                stats = pager_get_stats();
                before_hash = hash_file(DB);
                pager_begin_transaction();

                in_transaction = true;


            }
            uint32_t index = std::rand() % created_pages.size;
            uint32_t page_id = created_pages.data[index];
            Page* page = (Page*)pager_get(page_id);
            assert(page != nullptr && "Failed to get page for writing");
            char random_char = chars[std::rand() % char_count];
            pager_mark_dirty(page_id);
            page->data[0] = random_char;
            std::cout << "Wrote '" << random_char << "' to page " << page_id << "\n";
            // Verify write
            Page* verify_page = (Page*)pager_get(page_id);
            assert(verify_page->data[0] == random_char && "Write verification failed");
        }
        else if (operation == 2 && created_pages.size > 0) { // Delete random page
            if(!in_transaction) {
                stats = pager_get_stats();
                before_hash = hash_file(DB);
                pager_begin_transaction();

                in_transaction = true;

            }
            uint32_t index = std::rand() % created_pages.size;
            uint32_t page_id = created_pages.data[index];
            pager_delete(page_id);
            std::cout << "Deleted page " << page_id << "\n";
            // Verify deletion
            assert(pager_get(page_id) == nullptr && "Page still exists after deletion");
            // Remove from tracking array

            created_pages.data[index] = created_pages.data[created_pages.size - 1];

            created_pages.size--;
        }
        else if (operation == 3) { // Rollback transaction

            pager_rollback();
            in_transaction = false;
            std::cout << "Rolled back transaction\n";

            // Verify rollback by checking pages are gone
            for (uint32_t j = 0; j < created_pages.size; ++j) {
                assert(pager_get(created_pages.data[j]) == nullptr && "Page exists after rollback");
            }
            array_clear(&created_pages);
            // Verify file state
            auto after_hash = hash_file(DB);

            PagerMeta stats2 = pager_get_stats();
            std::cout << stats.free_pages << ", " << stats.total_pages << '\n';
            std::cout << stats2.free_pages << ", " << stats2.total_pages << '\n';
            assert(before_hash == after_hash && "File hash changed after rollback");

        }
    }

    // Cleanup
    pager_rollback();
    pager_close();
    os_file_delete(DB);

    std::cout << "Stress test passed!\n";
}



int
main()
{
	pager_init(DB);
	test_transaction_semanticts();
	pager_close();
	os_file_delete(DB);
	pager_init(DB);
	test_rollback();
	os_file_delete(DB);
	pager_init(DB);
	test_lru();
	os_file_delete(DB);
	pager_init(DB);
	test_free_list();
	test_on_off();
	os_file_delete(DB);
	test_pager_stress();

	std::cout << "tests passed!\n";
}
