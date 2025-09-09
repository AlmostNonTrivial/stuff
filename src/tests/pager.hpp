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
		if (committed_pages.size() + transaction_pages.size() == 0)
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
		else if (operation == 1 && (committed_pages.size() + transaction_pages.size() > 0))
		{ // Write to random page
			if (!in_transaction)
			{
				stats = pager_get_stats();
				before_hash = hash_file(DB);
				pager_begin_transaction();
				in_transaction = true;
				std::cout << "Began transaction\n";
			}
			uint32_t   total_size = committed_pages.size() + transaction_pages.size();
			uint32_t   index = std::rand() % total_size;
			uint32_t   page_id = index < committed_pages.size() ? committed_pages[index]
																: transaction_pages[index - committed_pages.size()];
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
		else if (operation == 2 && (committed_pages.size() + transaction_pages.size() > 0))
		{ // Delete random page
			if (!in_transaction)
			{
				stats = pager_get_stats();
				before_hash = hash_file(DB);
				pager_begin_transaction();
				in_transaction = true;
				std::cout << "Began transaction\n";
			}
			uint32_t total_size = committed_pages.size() + transaction_pages.size();
			uint32_t index = std::rand() % total_size;
			uint32_t page_id = index < committed_pages.size() ? committed_pages[index]
															  : transaction_pages[index - committed_pages.size()];
			pager_delete(page_id);
			made_changes = true;
			std::cout << "Deleted page " << page_id << "\n";
			if (index < committed_pages.size())
			{
				committed_pages[index] = committed_pages[committed_pages.size() - 1];
				committed_pages.pop_back();
			}
			else
			{
				index -= committed_pages.size();
				transaction_pages[index] = transaction_pages[transaction_pages.size() - 1];
				transaction_pages.pop_back();
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
			for (uint32_t j = 0; j < transaction_pages.size(); ++j)
			{
				committed_pages.push(transaction_pages[j]);
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
			for (uint32_t j = 0; j < transaction_pages.size(); ++j)
			{
				committed_pages.push(transaction_pages[j]);
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
	test_pager_stress();
}
