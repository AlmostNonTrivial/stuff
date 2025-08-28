
#include "pager.hpp"
#include "arena.hpp"
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
#define PAGE_ROOT			  0

static_assert(MAX_CACHE_ENTRIES >= 3, "Cache size must be atleast 3");

// Internal structures

struct FreePage
{
	uint32_t index;
	uint32_t prev_free_page;
	uint32_t free_pointer;
	uint32_t free_pages[FREE_PAGES_PER_FREE_PAGE];
};

struct RootPage
{
	uint32_t page_counter;
	uint32_t free_page;
	char	 padding[PAGE_SIZE - (sizeof(uint32_t) * 2)];
};

static_assert(PAGE_SIZE == sizeof(Page));
static_assert(PAGE_SIZE == sizeof(RootPage));
static_assert(PAGE_SIZE == sizeof(FreePage));

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
	bool	 root_dirty;

	CacheMetadata cache_meta[MAX_CACHE_ENTRIES];
	Page		  cache_data[MAX_CACHE_ENTRIES];

	int32_t lru_head;
	int32_t lru_tail;

	bool		in_transaction;
	const char *data_file;
	char		journal_file[JOURNAL_FILENAME_SIZE];

	HashMap<uint32_t, uint32_t, PagerArena> page_to_cache;
	HashSet<uint32_t, PagerArena>			free_pages_set;
	HashSet<uint32_t, PagerArena>			journaled_pages;
	HashSet<uint32_t, PagerArena>			new_pages_in_transaction;

} PAGER = {.data_fd = OS_INVALID_HANDLE,
		   .journal_fd = OS_INVALID_HANDLE,
		   .root = {},
		   .root_dirty = false,
		   .cache_meta = {},
		   .cache_data = {},
		   .lru_head = INVALID_SLOT,
		   .lru_tail = INVALID_SLOT,
		   .in_transaction = false,
		   .data_file = nullptr,
		   .journal_file = NULL,
		   .page_to_cache = {},
		   .free_pages_set = {},
		   .journaled_pages = {},
		   .new_pages_in_transaction = {}};

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
journal_page(uint32_t page_index, const void *data)
{
	if (!PAGER.in_transaction || PAGER.journal_fd == OS_INVALID_HANDLE)
	{
		return;
	}

	if (hashset_contains(&PAGER.journaled_pages, page_index))
	{
		return;
	}

	if (page_index == PAGE_ROOT)
	{
		os_file_seek(PAGER.journal_fd, PAGE_ROOT);
	}
	else
	{
		int64_t pos_before = os_file_size(PAGER.journal_fd);
		if (pos_before < PAGE_SIZE)
		{
			pos_before = PAGE_SIZE;
		}
		os_file_seek(PAGER.journal_fd, pos_before);
	}

	os_file_write(PAGER.journal_fd, data, PAGE_SIZE);
	os_file_sync(PAGER.journal_fd);
}

static void
lru_remove(int32_t slot)
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
lru_add_head(int32_t slot)
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

	lru_remove(slot);
	lru_add_head(slot);
}

static int32_t
cache_evict_lru()
{
	int32_t		   slot = PAGER.lru_tail;
	CacheMetadata *entry = &PAGER.cache_meta[slot];

	if (entry->is_dirty)
	{
		write_page_to_disk(entry->page_index, &PAGER.cache_data[slot]);
	}

	hashmap_delete(&PAGER.page_to_cache, entry->page_index);

	lru_remove(slot);

	entry->is_valid = false;
	entry->is_dirty = false;
	entry->page_index = PAGE_ROOT;

	return slot;
}

static uint32_t
cache_find_slot()
{
	for (uint32_t i = 0; i < MAX_CACHE_ENTRIES; i++)
	{
		if (!PAGER.cache_meta[i].is_valid)
		{
			return i;
		}
	}

	return cache_evict_lru();
}

static void
cache_init()
{
	for (uint32_t i = 0; i < MAX_CACHE_ENTRIES; i++)
	{
		PAGER.cache_meta[i].page_index = PAGE_ROOT;
		PAGER.cache_meta[i].is_dirty = false;
		PAGER.cache_meta[i].is_valid = false;
		PAGER.cache_meta[i].lru_next = INVALID_SLOT;
		PAGER.cache_meta[i].lru_prev = INVALID_SLOT;
	}

	hashset_clear(&PAGER.free_pages_set);
	hashset_clear(&PAGER.journaled_pages);
	hashset_clear(&PAGER.new_pages_in_transaction);
	hashmap_clear(&PAGER.page_to_cache);
	PAGER.lru_head = INVALID_SLOT;
	PAGER.lru_tail = INVALID_SLOT;
	arena::reset<PagerArena>();
}

static void
save_root()
{
	journal_page(PAGE_ROOT, &PAGER.root);

	if (PAGER.root_dirty)
	{
		write_page_to_disk(PAGE_ROOT, &PAGER.root);
		PAGER.root_dirty = false;
	}
}

void *
get_internal(uint32_t page_index)
{

	if (hashmap_get(&PAGER.page_to_cache, page_index))
	{
		uint32_t slot = *hashmap_get(&PAGER.page_to_cache, page_index);

		cache_move_to_head(slot);

		return &PAGER.cache_data[slot];
	}

	uint32_t	   slot = cache_find_slot();
	CacheMetadata *entry = &PAGER.cache_meta[slot];

	if (!read_page_from_disk(page_index, &PAGER.cache_data[slot]))
	{
		return nullptr;
	}

	entry->page_index = page_index;
	entry->is_valid = true;
	entry->is_dirty = false;

	hashmap_insert(&PAGER.page_to_cache, page_index, slot);
	lru_add_head(slot);

	return &PAGER.cache_data[slot];
}
static uint32_t
create_free_page(uint32_t prev_free_page, uint32_t page_to_free)
{

	get_internal(page_to_free);

	PAGER.root_dirty = true;

	uint32_t slot = *hashmap_get(&PAGER.page_to_cache, page_to_free);

	memset(&PAGER.cache_data[slot], 0, PAGE_SIZE);

	FreePage *free = reinterpret_cast<FreePage *>(&PAGER.cache_data[slot]);

	free->free_pointer = PAGE_ROOT;
	free->prev_free_page = prev_free_page;
	free->index = page_to_free;

	return page_to_free;
}

static void
add_to_free_list(uint32_t page_index)
{
	if (PAGER.root.free_page == PAGE_ROOT)
	{
		PAGER.root.free_page = create_free_page(PAGE_ROOT, page_index);
		hashset_insert(&PAGER.free_pages_set, page_index);
		PAGER.root_dirty = true;
		return;
	}

	FreePage *page = static_cast<FreePage *>(get_internal(PAGER.root.free_page));
	if (page->free_pointer >= FREE_PAGES_PER_FREE_PAGE)
	{
		PAGER.root.free_page = create_free_page(PAGER.root.free_page, page_index);
		hashset_insert(&PAGER.free_pages_set, PAGER.root.free_page);
		hashset_insert(&PAGER.new_pages_in_transaction, PAGER.root.free_page);
		PAGER.root_dirty = true;
		return;
	}

	pager_mark_dirty(PAGER.root.free_page);
	hashset_insert(&PAGER.free_pages_set, page_index);
	page = static_cast<FreePage *>(get_internal(PAGER.root.free_page));
	page->free_pages[page->free_pointer++] = page_index;
}

static uint32_t
take_from_free_list()
{
	if (PAGER.free_pages_set.size == 0)
	{
		return PAGE_ROOT;
	}

	FreePage *page = static_cast<FreePage *>(get_internal(PAGER.root.free_page));

	// current page, has no more
	if (page->free_pointer == 0)
	{
		uint32_t empty_free_page_index = PAGER.root.free_page;
		PAGER.root.free_page = page->prev_free_page;
		PAGER.root_dirty = true;

		hashset_delete(&PAGER.free_pages_set, empty_free_page_index);
		return empty_free_page_index;
	}

	pager_mark_dirty(PAGER.root.free_page);
	page->free_pointer--;
	uint32_t free_page_index = page->free_pages[page->free_pointer];
	page->free_pages[page->free_pointer] = PAGE_ROOT;

	hashset_delete(&PAGER.free_pages_set, free_page_index);

	return free_page_index;
}

static void
build_free_pages_set()
{
	uint32_t current_free_page = PAGER.root.free_page;
	hashset_clear(&PAGER.free_pages_set);
	while (current_free_page != PAGE_ROOT)
	{
		void	 *data = get_internal(current_free_page);
		FreePage *page = static_cast<FreePage *>(data);

		hashset_insert(&PAGER.free_pages_set, current_free_page);

		for (uint32_t i = 0; i < page->free_pointer; i++)
		{
			hashset_insert(&PAGER.free_pages_set, page->free_pages[i]);
		}

		current_free_page = page->prev_free_page;
	}
}

// Public API implementation
bool
pager_init(const char *filename)
{
	arena::init<PagerArena>();

	PAGER.data_file = filename;

	snprintf(PAGER.journal_file, sizeof(PAGER.journal_file), "%s-journal", filename);

	bool exists = os_file_exists(filename);
	PAGER.data_fd = os_file_open(filename, true, true);

	bool journal_exists = os_file_exists(PAGER.journal_file);
	if (journal_exists)
	{
		PAGER.in_transaction = true;
		PAGER.journal_fd = os_file_open(PAGER.journal_file, true, false);
		pager_rollback();
	}

	cache_init();

	if (exists)
	{

		if (read_page_from_disk(PAGE_ROOT, &PAGER.root))
		{
			PAGER.root_dirty = false;
		}
		build_free_pages_set();
	}
	else
	{
		PAGER.root.page_counter = 1;
		PAGER.root.free_page = PAGE_ROOT;
		PAGER.root_dirty = true;
		save_root();
	}

	return exists;
}

void *
pager_get(uint32_t page_index)
{
	if (page_index >= PAGER.root.page_counter || page_index == PAGE_ROOT)
	{
		return nullptr;
	}

	if (hashset_contains(&PAGER.free_pages_set, page_index))
	{
		return nullptr;
	}

	return get_internal(page_index);
}

uint32_t
pager_new()
{

	if (!PAGER.in_transaction)
	{
		return PAGE_ROOT;
	}

	uint32_t page_index = take_from_free_list();

	if (page_index == 0)
	{
		page_index = PAGER.root.page_counter++;
		PAGER.root_dirty = true;
	}

	hashset_insert(&PAGER.new_pages_in_transaction, page_index);

	uint32_t	   slot = cache_find_slot();
	CacheMetadata *entry = &PAGER.cache_meta[slot];

	memset(&PAGER.cache_data[slot], 0, sizeof(Page));
	PAGER.cache_data[slot].index = page_index;
	entry->page_index = page_index;
	entry->is_valid = true;
	entry->is_dirty = true;

	hashmap_insert(&PAGER.page_to_cache, page_index, slot);
	lru_add_head(slot);

	return page_index;
}

void
pager_mark_dirty(uint32_t page_index)
{
	if (page_index >= PAGER.root.page_counter || !PAGER.in_transaction)
	{
		return;
	}

	if (!(hashset_contains(&PAGER.journaled_pages, page_index)) &&
		!(hashset_contains(&PAGER.new_pages_in_transaction, page_index)))
	{
		void *data = get_internal(page_index);
		journal_page(page_index, data);
		hashset_insert(&PAGER.journaled_pages, page_index);
	}

	if (hashmap_get(&PAGER.page_to_cache, page_index))
	{
		PAGER.cache_meta[*hashmap_get(&PAGER.page_to_cache, page_index)].is_dirty = true;
	}
}

void
pager_delete(uint32_t page_index)
{
	if (page_index == PAGE_ROOT || page_index >= PAGER.root.page_counter || !PAGER.in_transaction)
	{
		return;
	}

	get_internal(page_index);
	if (0 != PAGER.root.free_page)
	{
		get_internal(PAGER.root.free_page);
	}

	pager_mark_dirty(page_index);

	add_to_free_list(page_index);
}

void
pager_begin_transaction()
{
	if (PAGER.in_transaction)
	{
		return;
	}

	PAGER.in_transaction = true;
	PAGER.journal_fd = os_file_open(PAGER.journal_file, true, true);

	journal_page(PAGE_ROOT, &PAGER.root);
	hashset_insert(&PAGER.journaled_pages, (uint32_t)PAGE_ROOT);
}

void
pager_commit()
{
	if (!PAGER.in_transaction)
	{
		return;
	}

	PAGER.root_dirty = true;

	pager_sync();

	os_file_close(PAGER.journal_fd);
	os_file_delete(PAGER.journal_file);

	PAGER.journal_fd = OS_INVALID_HANDLE;
	PAGER.in_transaction = false;

	hashset_clear(&PAGER.journaled_pages);
	hashset_clear(&PAGER.new_pages_in_transaction);
}

void
pager_rollback()
{
	if (!PAGER.in_transaction)
	{
		return;
	}

	int64_t journal_size = os_file_size(PAGER.journal_fd);

	if (journal_size >= PAGE_SIZE)
	{
		os_file_seek(PAGER.journal_fd, PAGE_ROOT);
		if (os_file_read(PAGER.journal_fd, &PAGER.root, PAGE_SIZE) == PAGE_SIZE)
		{
			PAGER.root_dirty = true;
			save_root();
		}

		for (int64_t pos = PAGE_SIZE; pos < journal_size; pos += PAGE_SIZE)
		{
			uint8_t page_data[PAGE_SIZE];
			os_file_seek(PAGER.journal_fd, pos);
			if (os_file_read(PAGER.journal_fd, page_data, PAGE_SIZE) != PAGE_SIZE)
			{
				break;
			}

			Page	*page = reinterpret_cast<Page *>(page_data);
			uint32_t page_index = page->index;
			write_page_to_disk(page_index, page_data);
		}

		os_file_truncate(PAGER.data_fd, PAGER.root.page_counter * PAGE_SIZE);
	}
	os_file_close(PAGER.journal_fd);
	os_file_delete(PAGER.journal_file);
	PAGER.journal_fd = OS_INVALID_HANDLE;

	cache_init();
	build_free_pages_set();
	PAGER.in_transaction = false;
}

void
pager_sync()
{
	for (int32_t i = 0; i < MAX_CACHE_ENTRIES; i++)
	{
		if (PAGER.cache_meta[i].is_valid && PAGER.cache_meta[i].is_dirty)
		{
			write_page_to_disk(PAGER.cache_meta[i].page_index, &PAGER.cache_data[i]);
			PAGER.cache_meta[i].is_dirty = false;
		}
	}

	save_root();
	os_file_sync(PAGER.data_fd);
}

void
pager_close()
{
	os_file_close(PAGER.data_fd);

	PAGER.in_transaction = false;
	arena::shutdown<PagerArena>();

	PAGER.data_fd = OS_INVALID_HANDLE;
}

PagerMeta
pager_get_stats()
{
	PagerMeta data;
	data.total_pages = PAGER.root.page_counter - 1;
	data.free_pages = PAGER.free_pages_set.size;

	data.cached_pages = 0;
	data.dirty_pages = 0;

	for (int32_t i = 0; i < MAX_CACHE_ENTRIES; i++)
	{
		if (PAGER.cache_meta[i].is_valid)
		{
			(data.cached_pages)++;
			if (PAGER.cache_meta[i].is_dirty)
			{
				(data.dirty_pages)++;
			}
		}
	}

	return data;
}
