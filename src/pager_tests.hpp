#pragma once
#include "arena.hpp"
#include "os_layer.hpp"
#include "pager.hpp"
#include "test_utils.hpp"
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <iostream>

#define DB "db"


inline void
test_free_list()
{

    uint32_t size = 1000;
	pager_begin_transaction();
	for (uint32_t i = 1; i <= size; i++)
	{
		pager_new();
	}

	pager_meta stats1 = pager_get_stats();

	std::cout << stats1.free_pages << ", " << stats1.total_pages << "\n";
	assert(0 == stats1.free_pages && size == stats1.total_pages);

	for (uint32_t i = 1; i <= size; i++)
	{
		pager_delete(i);
	}
	pager_meta stats3 = pager_get_stats();
	assert(size == stats3.free_pages && size == stats3.total_pages);

	std::cout << stats3.free_pages << ", " << stats3.total_pages << "\n";

	for (uint32_t i = 1; i <= size * 2; i++)
	{

		pager_new();
	}

	stats3 = pager_get_stats();
	std::cout << stats3.free_pages << ", " << stats3.total_pages << "\n";
	assert(0 == stats3.free_pages && size * 2 == stats3.total_pages);
}

inline void
test_rollback()
{
	auto start = hash_file(DB);
	pager_begin_transaction();
	auto	   p1 = pager_new();
	auto	   p2 = pager_new();
	base_page *ptr = (base_page *)pager_get(p1);
	ptr->data[0] = 'a';
	pager_commit();
	pager_close();
	pager_open(DB);
	assert(nullptr != pager_get(p1));
	assert(nullptr != pager_get(p2));
	assert('a' == ((base_page *)pager_get(p1))->data[0]);
	auto before = hash_file(DB);
	assert(before != start);
	pager_begin_transaction();
	auto p3 = pager_new();
	assert(nullptr != pager_get(p3));
	pager_delete(p2);
	assert(nullptr == pager_get(p2));
	ptr = (base_page *)pager_get(p1);
	ptr->data[0] = 'b';
	// for sync without commit
	// pager_sync();
	pager_close();
	auto after_sync = hash_file(DB);
	assert(after_sync != before);
	pager_open(DB);
	// rollback applied
	auto after_rollback = hash_file(DB);
	assert(after_rollback == before);
	pager_begin_transaction();
	ptr = (base_page *)pager_get(p1);
	ptr->data[0] = 'c';
	pager_rollback();
	ptr = (base_page *)pager_get(p1);
	assert('a' == ptr->data[0]);
}

inline void
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

inline void
test_lru()
{
	pager_begin_transaction();
	auto p1 = pager_new();
	auto p2 = pager_new();
	auto p3 = pager_new();
	auto p4 = pager_new();

	auto *ptr1 = (base_page *)pager_get(p1);
	pager_mark_dirty(p1);
	ptr1->data[0] = 'a';
	auto *ptr2 = (base_page *)pager_get(p2);
	pager_mark_dirty(p2);
	ptr2->data[0] = 'b';
	auto *ptr3 = (base_page *)pager_get(p3);
	pager_mark_dirty(p3);
	ptr3->data[0] = 'c';
	std::cout << ptr1->data[0] << ", " << ptr2->data[0] << ", " << ptr3->data[0] << '\n';
	auto *ptr4 = (base_page *)pager_get(p4);
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

inline void
test_on_off()
{
	pager_open(DB);
	auto one = hash_file(DB);
	pager_open(DB);
	auto two = hash_file(DB);
	pager_commit();
	auto three = hash_file(DB);
	pager_rollback();
	auto four = hash_file(DB);
	assert(one == two && two == three && three == four);
}



// Helper function to generate weighted random operation
inline uint32_t
weighted_rand_op()
{
	// Weights: 30% create, 30% write, 30% delete, 5% rollback, 5% commit
	int r = std::rand() % 100;
	if (r < 30)
		return 0; // Create
	if (r < 60)
		return 1; // Write
	if (r < 90)
		return 2; // Delete
	if (r < 95)
		return 3; // Rollback
	return 4;	  // Commit
}

inline void
test_pager_stress()
{
	std::srand(42); // Fixed seed for reproducibility
	os_file_delete(DB);
	pager_open(DB);

	// Track pages: committed (persisted) and transaction (uncommitted)
	Array<uint32_t> committed_pages;
	Array<uint32_t> transaction_pages;
	const int		iterations = 100; // Increased for more stress
	const char		chars[] = "abcdefghijklmnopqrstuvwxyz";
	const int		char_count = sizeof(chars) - 1;
	bool			in_transaction = false;
	bool			made_changes = false;
	pager_meta		stats = pager_get_stats();
	uint64_t		before_hash = hash_file(DB);

	std::cout << "Initial stats: free_pages=" << stats.free_pages << ", total_pages=" << stats.total_pages << "\n";
	std::cout << "Initial hash: " << before_hash << "\n";

	for (int i = 0; i < iterations; ++i)
	{
		uint32_t operation;
		// Force create if no pages exist to ensure write/delete can proceed
		if (committed_pages.size + transaction_pages.size == 0)
		{
			operation = 0;
		}
		else
		{
			operation = weighted_rand_op();
		}
		std::cout << "Operation " << i << ": " << operation << "\n";

		if (operation == 0)
		{ // Create new page
			if (!in_transaction)
			{
				stats = pager_get_stats();
				before_hash = hash_file(DB);
				pager_begin_transaction();
				in_transaction = true;
				std::cout << "Began transaction\n";
			}
			uint32_t page_id = pager_new();
			assert(page_id != 0 && "Failed to create new page");
			array_push(&transaction_pages, page_id);
			made_changes = true;
			std::cout << "Created page " << page_id << "\n";
			pager_meta new_stats = pager_get_stats();
			std::cout << "Stats: free_pages=" << new_stats.free_pages << ", total_pages=" << new_stats.total_pages
					  << "\n";
		}
		else if (operation == 1 && (committed_pages.size + transaction_pages.size > 0))
		{ // Write to random page
			if (!in_transaction)
			{
				stats = pager_get_stats();
				before_hash = hash_file(DB);
				pager_begin_transaction();
				in_transaction = true;
				std::cout << "Began transaction\n";
			}
			uint32_t   total_size = committed_pages.size + transaction_pages.size;
			uint32_t   index = std::rand() % total_size;
			uint32_t   page_id = index < committed_pages.size ? committed_pages.data[index]
															  : transaction_pages.data[index - committed_pages.size];
			base_page *page = (base_page *)pager_get(page_id);
			assert(page != nullptr && "Failed to get page for writing");
			char random_char = chars[std::rand() % char_count];
			pager_mark_dirty(page_id);
			page->data[0] = random_char;
			made_changes = true;
			std::cout << "Wrote '" << random_char << "' to page " << page_id << "\n";
			base_page *verify_page = (base_page *)pager_get(page_id);
			assert(verify_page->data[0] == random_char && "Write verification failed");
			pager_meta new_stats = pager_get_stats();
			std::cout << "Stats: free_pages=" << new_stats.free_pages << ", total_pages=" << new_stats.total_pages
					  << "\n";
		}
		else if (operation == 2 && (committed_pages.size + transaction_pages.size > 0))
		{ // Delete random page
			if (!in_transaction)
			{
				stats = pager_get_stats();
				before_hash = hash_file(DB);
				pager_begin_transaction();
				in_transaction = true;
				std::cout << "Began transaction\n";
			}
			uint32_t total_size = committed_pages.size + transaction_pages.size;
			uint32_t index = std::rand() % total_size;
			uint32_t page_id = index < committed_pages.size ? committed_pages.data[index]
															: transaction_pages.data[index - committed_pages.size];
			pager_delete(page_id);
			made_changes = true;
			std::cout << "Deleted page " << page_id << "\n";
			assert(pager_get(page_id) == nullptr && "Page still exists after deletion");
			if (index < committed_pages.size)
			{
				committed_pages.data[index] = committed_pages.data[committed_pages.size - 1];
				committed_pages.size--;
			}
			else
			{
				index -= committed_pages.size;
				transaction_pages.data[index] = transaction_pages.data[transaction_pages.size - 1];
				transaction_pages.size--;
			}
			pager_meta new_stats = pager_get_stats();
			std::cout << "Stats: free_pages=" << new_stats.free_pages << ", total_pages=" << new_stats.total_pages
					  << "\n";
		}
		else if (operation == 3 && in_transaction)
		{ // Rollback transaction
			pager_rollback();
			in_transaction = false;
			made_changes = false;
			std::cout << "Rolled back transaction\n";

			array_clear(&transaction_pages);
			pager_meta new_stats = pager_get_stats();
			std::cout << "Stats after rollback: free_pages=" << new_stats.free_pages
					  << ", total_pages=" << new_stats.total_pages << "\n";
			uint64_t after_hash = hash_file(DB);
			std::cout << "Hash after rollback: " << after_hash << "\n";
			assert(before_hash == after_hash && "File hash changed after rollback");
			assert(new_stats.free_pages == stats.free_pages && new_stats.total_pages == stats.total_pages &&
				   "Stats not restored after rollback");
		}
		else if (operation == 4 && in_transaction && made_changes)
		{ // Commit transaction
			pager_commit();
			in_transaction = false;
			made_changes = false;
			std::cout << "Committed transaction\n";
			for (uint32_t j = 0; j < transaction_pages.size; ++j)
			{
				array_push(&committed_pages, transaction_pages.data[j]);
			}
			array_clear(&transaction_pages);
			pager_meta new_stats = pager_get_stats();
			std::cout << "Stats after commit: free_pages=" << new_stats.free_pages
					  << ", total_pages=" << new_stats.total_pages << "\n";
			uint64_t after_hash = hash_file(DB);
			std::cout << "Hash after commit: " << after_hash << "\n";
			assert(before_hash != after_hash && "File hash unchanged after commit");
			stats = new_stats;
			before_hash = after_hash;
		}
	}

	// Cleanup
	if (in_transaction)
	{
		if (made_changes)
		{
			pager_commit();
			std::cout << "Committed final transaction\n";
			for (uint32_t j = 0; j < transaction_pages.size; ++j)
			{
				array_push(&committed_pages, transaction_pages.data[j]);
			}
			array_clear(&transaction_pages);
		}
		else
		{
			pager_rollback();
			std::cout << "Rolled back final transaction\n";
			array_clear(&transaction_pages);
		}
	}
	pager_meta final_stats = pager_get_stats();
	std::cout << "Final stats: free_pages=" << final_stats.free_pages << ", total_pages=" << final_stats.total_pages
			  << "\n";
	uint64_t final_hash = hash_file(DB);
	std::cout << "Final hash: " << final_hash << "\n";
	pager_close();
	os_file_delete(DB);

	std::cout << "Stress test passed!\n";
}

inline void
pager_tests()
{
	pager_open(DB);
	test_transaction_semanticts();
	pager_close();
	os_file_delete(DB);
	pager_open(DB);
	// test_rollback();
	os_file_delete(DB);
	pager_open(DB);
	// test_lru();
	os_file_delete(DB);
	pager_open(DB);
	test_free_list();
	test_on_off();
	os_file_delete(DB);
	test_pager_stress();
}
