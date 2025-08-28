#pragma once
#include <cstdint>
#include "defs.hpp"
#define PAGE_INVALID 0

#define MAX_CACHE_ENTRIES		 3
#define FREE_PAGES_PER_FREE_PAGE ((PAGE_SIZE - (sizeof(uint32_t) * 3)) / sizeof(uint32_t))
// struct pager_meta
// {
// 	uint32_t total_pages, cached_pages, dirty_pages, free_pages;
// };

struct base_page
{
	uint32_t index;
	char	 data[PAGE_SIZE - sizeof(uint32_t)];
};
/*
** Pager Statistics Structure
** --------------------------
** Provides runtime metrics about pager state for monitoring and debugging.
*/
struct pager_meta
{
	uint32_t total_pages;  /* Total pages in database file */
	uint32_t cached_pages; /* Pages currently in cache */
	uint32_t dirty_pages;  /* Modified pages awaiting flush */
	uint32_t free_pages;   /* Pages available for reuse */
};

bool
pager_open(const char *filename);

void *
pager_get(uint32_t page_index);

uint32_t
pager_new();

bool
pager_mark_dirty(uint32_t page_index);

bool
pager_delete(uint32_t page_index);
bool
pager_begin_transaction();
bool
pager_commit();
bool
pager_rollback();

pager_meta
pager_get_stats();

void
pager_close();
