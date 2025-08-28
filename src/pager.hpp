#pragma once
#include <cstdint>
#include "defs.hpp"
#define PAGE_INVALID 0


#define MAX_CACHE_ENTRIES 3
#define FREE_PAGES_PER_FREE_PAGE ((PAGE_SIZE - (sizeof(uint32_t) * 3)) / sizeof(uint32_t))
struct PagerMeta {
	uint32_t total_pages, cached_pages, dirty_pages, free_pages;
};


struct Page
{
	uint32_t index;
	char data[PAGE_SIZE - sizeof(uint32_t)];
};

bool
pager_init(const char *filename);

void *
pager_get(uint32_t page_index);

uint32_t
pager_new();

void
pager_mark_dirty(uint32_t page_index);

void
pager_delete(uint32_t page_index);
void
pager_begin_transaction();
void
pager_commit();
void
pager_rollback();

void
pager_sync();

PagerMeta
pager_get_stats();

void
pager_close();
