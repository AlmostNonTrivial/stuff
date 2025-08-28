#pragma once
#include <cstdint>
#define PAGE_INVALID 0

struct pager_meta
{
	uint32_t total_pages;
	uint32_t cached_pages;
	uint32_t dirty_pages;
	uint32_t free_pages;
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
