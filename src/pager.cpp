#include "pager.hpp"
#include "arena.hpp"
#include "defs.hpp"
#include "os_layer.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct PagerArena
{
};

// Constants
#define INVALID_SLOT		  -1
#define JOURNAL_FILENAME_SIZE 256
#define ROOT_PAGE_INDEX		  0U // used as both test for the root page and null, return, as callers can't use to this page

static_assert(MAX_CACHE_ENTRIES >= 3, "Cache size must be at least 3");

struct FreePage
{
	uint32_t index;
	uint32_t previous_free_page; // it's predeccessor, when it is 0, it is the last free page
	uint32_t free_page_pointer;	 // marks the position in the free page array
	uint32_t free_pages[FREE_PAGES_PER_FREE_PAGE];
};

struct RootPage
{
	uint32_t page_counter;	 // page_counter - 1 (for the root) * PAGE_SIZE should be the size of the file
	uint32_t free_page_head; // points to current free page that is not full
	char	 padding[PAGE_SIZE - (sizeof(uint32_t) * 2)]; // where metadata would go
};

static_assert(PAGE_SIZE == sizeof(Page), "Page size mismatch");
static_assert(PAGE_SIZE == sizeof(RootPage), "RootPage size mismatch");
static_assert(PAGE_SIZE == sizeof(FreePage), "FreePage size mismatch");

// kept seperate from data to reduce copying and better cache locality
struct CacheMetadata
{
	uint32_t page_index;
	bool	 is_dirty;
	bool	 is_valid;
	int32_t	 lru_next;
	int32_t	 lru_prev;
};

// Global pager state
static struct
{
	os_file_handle_t data_fd;
	os_file_handle_t journal_fd;

	RootPage root;

	CacheMetadata cache_meta[MAX_CACHE_ENTRIES];
	Page		  cache_data[MAX_CACHE_ENTRIES];

	int32_t lru_head;
	int32_t lru_tail;

	bool		in_transaction;
	const char *data_file;
	char		journal_file[JOURNAL_FILENAME_SIZE];

	HashMap<uint32_t, uint32_t, PagerArena> page_to_cache;
	HashSet<uint32_t, PagerArena>			free_pages_set; // to quickly make sure user doens't ask for delted page
	HashSet<uint32_t, PagerArena>			journaled_pages;
	// pages journaled once within a transaction, and check so so we don't journal already dirtied or new pages

} PAGER = {.data_fd = OS_INVALID_HANDLE,
		   .journal_fd = OS_INVALID_HANDLE,
		   .root = {},
		   .cache_meta = {},
		   .cache_data = {},
		   .lru_head = INVALID_SLOT,
		   .lru_tail = INVALID_SLOT,
		   .in_transaction = false,
		   .data_file = nullptr,
		   .journal_file = {},
		   .page_to_cache = {},
		   .free_pages_set = {},
		   .journaled_pages = {}};



bool mark_dirty_internal(uint32_t page_index);
// Internal functions implementation
static void
write_page_to_disk(uint32_t page_index, const void *data)
{
	os_file_seek(PAGER.data_fd, page_index * PAGE_SIZE);
	os_file_write(PAGER.data_fd, data, PAGE_SIZE);
}

static bool
read_page_from_disk(uint32_t page_index, void *data)
{
	os_file_seek(PAGER.data_fd, page_index * PAGE_SIZE);
	return os_file_read(PAGER.data_fd, data, PAGE_SIZE) == PAGE_SIZE;
}

static void
journal_write_page(uint32_t page_index, const void *data)
{
	hashset_insert(&PAGER.journaled_pages, page_index);

	if (page_index == ROOT_PAGE_INDEX)
	{
		os_file_seek(PAGER.journal_fd, 0);
	}
	else
	{
		int64_t journal_size = os_file_size(PAGER.journal_fd);
		if (journal_size < PAGE_SIZE)
		{
			journal_size = PAGE_SIZE;
		}
		os_file_seek(PAGER.journal_fd, journal_size);
	}

	os_file_write(PAGER.journal_fd, data, PAGE_SIZE);
	os_file_sync(PAGER.journal_fd);
}

static void
lru_remove_from_list(int32_t slot)
{
	CacheMetadata *entry = &PAGER.cache_meta[slot];

	if (entry->lru_prev != INVALID_SLOT)
	{
		PAGER.cache_meta[entry->lru_prev].lru_next = entry->lru_next;
	}
	else
	{
		PAGER.lru_head = entry->lru_next;
	}

	if (entry->lru_next != INVALID_SLOT)
	{
		PAGER.cache_meta[entry->lru_next].lru_prev = entry->lru_prev;
	}
	else
	{
		PAGER.lru_tail = entry->lru_prev;
	}

	entry->lru_next = INVALID_SLOT;
	entry->lru_prev = INVALID_SLOT;
}

static void
lru_add_to_head(int32_t slot)
{
	CacheMetadata *entry = &PAGER.cache_meta[slot];

	entry->lru_next = PAGER.lru_head;
	entry->lru_prev = INVALID_SLOT;

	if (PAGER.lru_head != INVALID_SLOT)
	{
		PAGER.cache_meta[PAGER.lru_head].lru_prev = slot;
	}

	PAGER.lru_head = slot;

	if (PAGER.lru_tail == INVALID_SLOT)
	{
		PAGER.lru_tail = slot;
	}
}

static void
cache_move_to_head(int32_t slot)
{
	if (PAGER.lru_head == slot)
	{
		return;
	}

	lru_remove_from_list(slot);
	lru_add_to_head(slot);
}

static int32_t
cache_evict_lru_entry()
{
	int32_t		   slot = PAGER.lru_tail;
	CacheMetadata *entry = &PAGER.cache_meta[slot];

	if (entry->is_dirty)
	{
		write_page_to_disk(entry->page_index, &PAGER.cache_data[slot]);
	}

	hashmap_delete(&PAGER.page_to_cache, entry->page_index);

	lru_remove_from_list(slot);

	entry->is_valid = false;
	entry->is_dirty = false;
	entry->page_index = ROOT_PAGE_INDEX;

	return slot;
}

static uint32_t
cache_find_free_slot()
{
	for (uint32_t i = 0; i < MAX_CACHE_ENTRIES; i++)
	{
		if (!PAGER.cache_meta[i].is_valid)
		{
			return i;
		}
	}

	return cache_evict_lru_entry();
}

static void
cache_reset()
{
	for (uint32_t i = 0; i < MAX_CACHE_ENTRIES; i++)
	{
		PAGER.cache_meta[i].page_index = ROOT_PAGE_INDEX;
		PAGER.cache_meta[i].is_dirty = false;
		PAGER.cache_meta[i].is_valid = false;
		PAGER.cache_meta[i].lru_next = INVALID_SLOT;
		PAGER.cache_meta[i].lru_prev = INVALID_SLOT;
	}

	hashset_clear(&PAGER.free_pages_set);
	hashset_clear(&PAGER.journaled_pages);
	hashmap_clear(&PAGER.page_to_cache);
	PAGER.lru_head = INVALID_SLOT;
	PAGER.lru_tail = INVALID_SLOT;
}

static void *
cache_get_or_load(uint32_t page_index)
{
	if (hashmap_get(&PAGER.page_to_cache, page_index))
	{
		uint32_t slot = *hashmap_get(&PAGER.page_to_cache, page_index);
		cache_move_to_head(slot);
		return &PAGER.cache_data[slot];
	}

	uint32_t	   slot = cache_find_free_slot();
	CacheMetadata *entry = &PAGER.cache_meta[slot];

	read_page_from_disk(page_index, &PAGER.cache_data[slot]);

	entry->page_index = page_index;
	entry->is_valid = true;
	entry->is_dirty = false;

	hashmap_insert(&PAGER.page_to_cache, page_index, slot);
	lru_add_to_head(slot);

	return &PAGER.cache_data[slot];
}

static uint32_t
convert_to_free_page(uint32_t prev_free_page, uint32_t page_to_free)
{
	cache_get_or_load(page_to_free);

	uint32_t slot = *hashmap_get(&PAGER.page_to_cache, page_to_free);
	memset(&PAGER.cache_data[slot], 0, PAGE_SIZE);

	FreePage *free_page = reinterpret_cast<FreePage *>(&PAGER.cache_data[slot]);

	free_page->free_page_pointer = 0;
	free_page->previous_free_page = prev_free_page;
	free_page->index = page_to_free;

	hashset_insert(&PAGER.free_pages_set, page_to_free);

	return page_to_free;
}

static void
add_page_to_free_list(uint32_t page_index)
{
	mark_dirty_internal(page_index);

	if (PAGER.root.free_page_head == ROOT_PAGE_INDEX)
	{
		PAGER.root.free_page_head = convert_to_free_page(ROOT_PAGE_INDEX, page_index);
		return;
	}

	FreePage *current_free_page = static_cast<FreePage *>(cache_get_or_load(PAGER.root.free_page_head));

	if (current_free_page->free_page_pointer >= FREE_PAGES_PER_FREE_PAGE)
	{
		PAGER.root.free_page_head = convert_to_free_page(PAGER.root.free_page_head, page_index);
		return;
	}

	mark_dirty_internal(PAGER.root.free_page_head);
	hashset_insert(&PAGER.free_pages_set, page_index);
	current_free_page->free_pages[current_free_page->free_page_pointer++] = page_index;
}

static uint32_t
take_page_from_free_list()
{
	if (PAGER.free_pages_set.size == 0)
	{
		return ROOT_PAGE_INDEX;
	}

	FreePage *current_free_page = static_cast<FreePage *>(cache_get_or_load(PAGER.root.free_page_head));

	// Current free page header is now empty
	if (current_free_page->free_page_pointer == 0)
	{
		uint32_t empty_free_page_index = PAGER.root.free_page_head;
		PAGER.root.free_page_head = current_free_page->previous_free_page;

		hashset_delete(&PAGER.free_pages_set, empty_free_page_index);
		return empty_free_page_index;
	}

	mark_dirty_internal(PAGER.root.free_page_head);
	current_free_page->free_page_pointer--;
	uint32_t recycled_page_index = current_free_page->free_pages[current_free_page->free_page_pointer];
	current_free_page->free_pages[current_free_page->free_page_pointer] = ROOT_PAGE_INDEX;

	hashset_delete(&PAGER.free_pages_set, recycled_page_index);

	return recycled_page_index;
}

static void
rebuild_free_pages_set()
{
	uint32_t current_free_page_index = PAGER.root.free_page_head;
	hashset_clear(&PAGER.free_pages_set);

	while (current_free_page_index != ROOT_PAGE_INDEX)
	{
		void	 *data = cache_get_or_load(current_free_page_index);
		FreePage *free_page = static_cast<FreePage *>(data);

		hashset_insert(&PAGER.free_pages_set, current_free_page_index);

		for (uint32_t i = 0; i < free_page->free_page_pointer; i++)
		{
			hashset_insert(&PAGER.free_pages_set, free_page->free_pages[i]);
		}

		current_free_page_index = free_page->previous_free_page;
	}
}

// Public API implementation
bool
pager_open(const char *filename)
{
	arena::init<PagerArena>();


	PAGER.data_file = filename;

	snprintf(PAGER.journal_file, sizeof(PAGER.journal_file), "%s-journal", filename);

	bool exists = os_file_exists(filename);
	PAGER.data_fd = os_file_open(filename, true, true);
	if (OS_INVALID_HANDLE == PAGER.data_fd)
	{
		return false;
	}

	bool journal_exists = os_file_exists(PAGER.journal_file);
	if (journal_exists)
	{
		PAGER.in_transaction = true;
		PAGER.journal_fd = os_file_open(PAGER.journal_file, true, false);
		if (OS_INVALID_HANDLE == PAGER.journal_fd)
		{
			pager_close();
			return false;
		}
		pager_rollback();
	}
	else
	{
		cache_reset();
	}

	if (exists)
	{
		read_page_from_disk(ROOT_PAGE_INDEX, &PAGER.root);
		rebuild_free_pages_set();
	}
	else
	{
		PAGER.root.page_counter = 1;
		PAGER.root.free_page_head = ROOT_PAGE_INDEX;
		write_page_to_disk(ROOT_PAGE_INDEX, &PAGER.root);
	}

	return exists;
}

void *
pager_get(uint32_t page_index)
{
	if (page_index >= PAGER.root.page_counter || page_index == ROOT_PAGE_INDEX)
	{
		return nullptr;
	}

	if (hashset_contains(&PAGER.free_pages_set, page_index))
	{
		return nullptr;
	}

	return cache_get_or_load(page_index);
}

uint32_t
pager_new()
{
	if (!PAGER.in_transaction)
	{
		return ROOT_PAGE_INDEX;
	}

	uint32_t page_index = take_page_from_free_list();

	if (page_index == ROOT_PAGE_INDEX)
	{
		page_index = PAGER.root.page_counter++;
	}

	// unless it came from the free list, we don't actually journal it, but add it to the journaled pages
	// such that it won't be added if modified
	hashset_insert(&PAGER.journaled_pages, page_index);

	uint32_t	   slot = cache_find_free_slot();
	CacheMetadata *entry = &PAGER.cache_meta[slot];

	memset(&PAGER.cache_data[slot], 0, sizeof(Page));
	PAGER.cache_data[slot].index = page_index;
	entry->page_index = page_index;
	entry->is_valid = true;
	entry->is_dirty = true;

	hashmap_insert(&PAGER.page_to_cache, page_index, slot);
	lru_add_to_head(slot);

	return page_index;
}


bool
mark_dirty_internal(uint32_t page_index)
{
	if (!hashset_contains(&PAGER.journaled_pages, page_index))
	{
		void *data = cache_get_or_load(page_index);
		journal_write_page(page_index, data);
	}

	if (hashmap_get(&PAGER.page_to_cache, page_index))
	{
		PAGER.cache_meta[*hashmap_get(&PAGER.page_to_cache, page_index)].is_dirty = true;
	}

	return true;
}

bool
pager_mark_dirty(uint32_t page_index)
{
	if (page_index >= PAGER.root.page_counter || !PAGER.in_transaction)
	{
		return false;
	}

	if(hashset_contains(&PAGER.free_pages_set, page_index)){
	    return false;
	}

	return mark_dirty_internal(page_index);
}

bool
pager_delete(uint32_t page_index)
{
	if (page_index == ROOT_PAGE_INDEX || page_index >= PAGER.root.page_counter || !PAGER.in_transaction)
	{
		return false;
	}

	add_page_to_free_list(page_index);
	return true;
}

bool
pager_begin_transaction()
{
	if (PAGER.in_transaction)
	{
		return false;
	}

	PAGER.journal_fd = os_file_open(PAGER.journal_file, true, true);
	if (OS_INVALID_HANDLE == PAGER.journal_fd)
	{
		return false;
	}

	PAGER.in_transaction = true;
	journal_write_page(ROOT_PAGE_INDEX, &PAGER.root);

	return true;
}

bool
pager_commit()
{
	if (!PAGER.in_transaction)
	{
		return false;
	}

	for (int32_t i = 0; i < MAX_CACHE_ENTRIES; i++)
	{
		if (PAGER.cache_meta[i].is_valid && PAGER.cache_meta[i].is_dirty)
		{
			write_page_to_disk(PAGER.cache_meta[i].page_index, &PAGER.cache_data[i]);
			PAGER.cache_meta[i].is_dirty = false;
		}
	}

	write_page_to_disk(ROOT_PAGE_INDEX, &PAGER.root);
	os_file_sync(PAGER.data_fd);

	os_file_close(PAGER.journal_fd);
	os_file_delete(PAGER.journal_file);

	PAGER.journal_fd = OS_INVALID_HANDLE;
	PAGER.in_transaction = false;

	hashset_clear(&PAGER.journaled_pages);

	return true;
}

bool
pager_rollback()
{
	if (!PAGER.in_transaction)
	{
		return false;
	}

	int64_t journal_size = os_file_size(PAGER.journal_fd);

	if (journal_size >= PAGE_SIZE)
	{
		// page root always at 0 in the journal file, others have their indexes
		// and can be positioned as such
		os_file_seek(PAGER.journal_fd, 0);
		if (os_file_read(PAGER.journal_fd, &PAGER.root, PAGE_SIZE) == PAGE_SIZE)
		{
			write_page_to_disk(ROOT_PAGE_INDEX, &PAGER.root);
		}

		for (int64_t offset = PAGE_SIZE; offset < journal_size; offset += PAGE_SIZE)
		{
			uint8_t page_buffer[PAGE_SIZE];
			os_file_seek(PAGER.journal_fd, offset);
			if (os_file_read(PAGER.journal_fd, page_buffer, PAGE_SIZE) != PAGE_SIZE)
			{
				break;
			}

			Page	*page = reinterpret_cast<Page *>(page_buffer);
			uint32_t page_index = page->index;
			write_page_to_disk(page_index, page_buffer);
		}

		os_file_truncate(PAGER.data_fd, PAGER.root.page_counter * PAGE_SIZE);
	}

	os_file_close(PAGER.journal_fd);
	os_file_delete(PAGER.journal_file);
	PAGER.journal_fd = OS_INVALID_HANDLE;

	cache_reset();
	arena::reset_and_decommit<PagerArena>();
	rebuild_free_pages_set();
	PAGER.in_transaction = false;



	return true;
}

void
pager_close()
{
	os_file_close(PAGER.data_fd);

	PAGER.in_transaction = false;
	PAGER.data_fd = OS_INVALID_HANDLE;

	arena::shutdown<PagerArena>();
}

PagerMeta
pager_get_stats()
{
	PagerMeta stats;
	stats.total_pages = PAGER.root.page_counter - 1;
	stats.free_pages = PAGER.free_pages_set.size;

	stats.cached_pages = 0;
	stats.dirty_pages = 0;

	for (int32_t i = 0; i < MAX_CACHE_ENTRIES; i++)
	{
		if (PAGER.cache_meta[i].is_valid)
		{
			stats.cached_pages++;
			if (PAGER.cache_meta[i].is_dirty)
			{
				stats.dirty_pages++;
			}
		}
	}

	return stats;
}
