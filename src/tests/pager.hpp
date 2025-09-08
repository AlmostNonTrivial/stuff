#pragma once
#include "../arena.hpp"
#include "../containers.hpp"
#include "../pager.hpp"
// #include "test_utils.hpp"
#include <cassert>
#include "../os_layer.hpp"
#include <cstdlib>
#include <ctime>
#include <iostream>

#define DB "db"



inline uint64_t
hash_file(const char *filename)
{
	os_file_handle_t handle = os_file_open(filename, false, false);
	if (handle == OS_INVALID_HANDLE)
		return 0;

	uint64_t	   hash = 0xcbf29ce484222325ULL;
	const uint64_t prime = 0x100000001b3ULL;

	uint8_t		   buffer[PAGE_SIZE];
	os_file_size_t bytes_read;
	while ((bytes_read = os_file_read(handle, buffer, sizeof(buffer))) > 0)
	{
		for (os_file_size_t i = 0; i < bytes_read; i++)
		{
			hash ^= buffer[i];
			hash *= prime;
		}
	}

	os_file_close(handle);
	return hash;
}

inline void
test_free_list()
{
	pager_open(DB);
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
	pager_close();
	os_file_delete(DB);
}

inline void
test_rollback()
{

	if (MAX_CACHE_ENTRIES > 3)
	{
		return;
	}

	pager_open(DB);
	auto start = hash_file(DB);
	pager_begin_transaction();
	auto	   p1 = pager_new();
	auto	   p2 = pager_new();
	base_page *ptr = pager_get(p1);
	pager_mark_dirty(p1);
	ptr->data[0] = 'a';

	pager_commit();
	pager_close();
	pager_open(DB);

	assert(nullptr != pager_get(p1));
	assert(nullptr != pager_get(p2));
	assert('a' == (pager_get(p1))->data[0]);

	auto before = hash_file(DB);
	assert(before != start);

	pager_begin_transaction();

	auto p3 = pager_new();
	assert(nullptr != pager_get(p3));
	pager_delete(p2);

	ptr = pager_get(p1);
	pager_mark_dirty(p1);
	ptr->data[0] = 'b';
	pager_new();
	pager_new();
	pager_new();   // force p1 to be evicted and written to data file
	pager_close(); // didn't commit
	auto after_sync = hash_file(DB);
	assert(after_sync != before);
	pager_open(DB); // rollback applied

	auto after_rollback = hash_file(DB);
	assert(after_rollback == before);
	pager_begin_transaction();

	ptr = pager_get(p1);
	pager_mark_dirty(p1);
	ptr->data[0] = 'c';

	pager_rollback();

	ptr = pager_get(p1);
	assert('a' == ptr->data[0]);

	pager_close();
	os_file_delete(DB);
}

inline void
test_lru()
{

	if (MAX_CACHE_ENTRIES != 3)
	{
		return;
	}

	pager_open(DB);
	pager_begin_transaction();

	auto p1 = pager_new();
	auto p2 = pager_new();
	auto p3 = pager_new();
	auto p4 = pager_new();

	base_page *ptr1 = pager_get(p1);
	pager_mark_dirty(p1);
	ptr1->data[0] = 'a';
	/* [a],[],[] */
	base_page *ptr2 = pager_get(p2);
	pager_mark_dirty(p2);
	ptr2->data[0] = 'b';
	/* [a],[b],[] */
	base_page *ptr3 = pager_get(p3);
	pager_mark_dirty(p3);
	ptr3->data[0] = 'c';
	/* [a],[b],[c] */
	base_page *ptr4 = pager_get(p4); // p1 at end, evict p1
	pager_mark_dirty(p4);
	ptr4->data[0] = 'd';
	/* [d],[b],[c] */
	assert('d' == ptr1->data[0] && ptr4 == ptr1);
	pager_get(p1); // p2 at end, evict p2
	assert('a' == ptr2->data[0]);
	/* [d],[a],[c] */

	pager_rollback();
	pager_close();
	os_file_delete(DB);
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
	std::srand(42);
	os_file_delete(DB);
	pager_open(DB);

	array<uint32_t> committed_pages;
	array<uint32_t> transaction_pages;
	const int		iterations = 100;
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
		if (committed_pages.size() + transaction_pages.size == 0)
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
			transaction_pages.push(page_id);
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
			base_page *page = pager_get(page_id);
			assert(page != nullptr && "Failed to get page for writing");
			char random_char = chars[std::rand() % char_count];
			pager_mark_dirty(page_id);
			page->data[0] = random_char;
			made_changes = true;
			std::cout << "Wrote '" << random_char << "' to page " << page_id << "\n";
			base_page *verify_page = pager_get(page_id);
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

			transaction_pages.clear();
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
			{committed_pages.push( transaction_pages.data[j]);
			}
			transaction_pages.clear();
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

	if (in_transaction)
	{
		if (made_changes)
		{
			pager_commit();
			std::cout << "Committed final transaction\n";
			for (uint32_t j = 0; j < transaction_pages.size; ++j)
			{
				committed_pages.push( transaction_pages.data[j]);
			}
			transaction_pages.clear();
		}
		else
		{
			pager_rollback();
			std::cout << "Rolled back final transaction\n";
			transaction_pages.clear();
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
test_pager()
{

	test_lru();
	test_rollback();
	test_free_list();
	test_pager_stress();
}
