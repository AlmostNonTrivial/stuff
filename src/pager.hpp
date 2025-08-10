#ifndef PAGER_HPP
#define PAGER_HPP

#define PAGE_INVALID 0
#define PAGE_ROOT 0


#define MAX_CACHE_ENTRIES 128
#define FREE_PAGES_PER_FREE_PAGE ((PAGE_SIZE - (sizeof(unsigned int) * 4)) / 4)
#include "defs.hpp"

void pager_init(const char *filename);

void *pager_get(unsigned int page_index);

unsigned int pager_new();

void pager_mark_dirty(unsigned int page_index);

void pager_delete(unsigned int page_index);

void pager_begin_transaction();
void pager_commit();
void pager_rollback();

void pager_sync();

void pager_get_stats(unsigned int *total_pages, unsigned int *free_pages,
                     unsigned int *cached_pages, unsigned int *dirty_pages);

void pager_close();

#endif
