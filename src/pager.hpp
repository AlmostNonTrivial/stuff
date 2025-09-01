#pragma once
#include <cstdint>
#include "defs.hpp"

#define PAGE_INVALID 0

#define MAX_CACHE_ENTRIES 200

/*
** BASE PAGE STRUCTURE
**
** Generic page layout used for all data pages. A page knowing it's own
** index allows us to append them in arbitrary positions in the journal,
** and rollback safely
**
** This is the "type" that free_page and other page types can be cast from,
** since they all share the index field at offset 0.
*/
struct base_page
{
	uint32_t index; /* Page's position in the file (self-identifying) */
	char	 data[PAGE_SIZE - sizeof(uint32_t)];
};

struct pager_meta
{
	uint32_t total_pages;
	uint32_t cached_pages;
	uint32_t dirty_pages;
	uint32_t free_pages;
};

bool
pager_open(const char *filename);
base_page *
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
