#pragma once
#include <cstdint>

#define PAGE_INVALID	  0
/*
 * Although we only need at least 3 cache entries for the lru algorithm,
 * having too small a cache puts us at risk of getting stale pointers
 * when doing operations on the b+tree, which was the cause of some
 * hilariously frustrating bugs, for example, when I got all my b+tree
 * tests passing, and absent-mindedly reduced the cache size, then the next
 * day ran the tests again after change a variable name, for them to fail.
 */
#define MAX_CACHE_ENTRIES 64
#define PAGE_SIZE		  1024

/*
**
** Generic page layout used for all data pages. A page knowing it's own
** index allows us to append them in arbitrary positions in the journal,
** and rollback
**
** This is the "type" that free_page and other page types can be cast from,
** since they all share the index field at offset 0.
*/
struct base_page
{
	uint32_t index; /* Page's position in the database file (self-identifying) */
	char	 data[PAGE_SIZE - sizeof(uint32_t)];
};

struct pager_meta
{
	uint32_t total_pages, cached_pages, dirty_pages, free_pages;
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
uint32_t
pager_get_next();
pager_meta
pager_get_stats();
void
pager_close();
