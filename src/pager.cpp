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
#define INVALID_SLOT			 -1
#define JOURNAL_FILENAME_SIZE	 256


// Internal structures

struct FreePage
{
	uint32_t index;
	uint32_t next_free_page;
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
	Page cache_data[MAX_CACHE_ENTRIES];

	int32_t lru_head;
	int32_t lru_tail;

	bool in_transaction;

	HashMap<uint32_t, uint32_t, PagerArena> page_to_cache;
	HashSet<uint32_t, PagerArena>			free_pages_set;
	HashSet<uint32_t, PagerArena>			journaled_pages;
	HashSet<uint32_t, PagerArena>			new_pages_in_transaction;

	const char *data_file;
	char		journal_file[JOURNAL_FILENAME_SIZE];
} pager = {.data_fd = -1,
		   .journal_fd = -1,
		   .root = {},
		   .root_dirty = false,
		   .cache_meta = {},
		   .cache_data = {},
		   .lru_head = INVALID_SLOT,
		   .lru_tail = INVALID_SLOT,
		   .in_transaction = false,
		   .data_file = nullptr};

// Internal functions implementation
static void
write_page_to_disk(uint32_t page_index, const void *data)
{
	os_file_seek(pager.data_fd, page_index * PAGE_SIZE);
	os_file_write(pager.data_fd, data, PAGE_SIZE);
}

static bool
read_page_from_disk(uint32_t page_index, void *data)
{
	os_file_seek(pager.data_fd, page_index * PAGE_SIZE);
	return os_file_read(pager.data_fd, data, PAGE_SIZE) == PAGE_SIZE;
}

static void
journal_page(uint32_t page_index, const void *data)
{
	if (!pager.in_transaction || pager.journal_fd == -1 ||
		hashset_contains(&pager.new_pages_in_transaction, page_index) ||
		hashset_contains(&pager.journaled_pages, page_index))
	{
		return;
	}

	if (page_index == PAGE_ROOT)
	{
		os_file_seek(pager.journal_fd, PAGE_INVALID);
	}
	else
	{
		int64_t pos_before = os_file_size(pager.journal_fd);
		if (pos_before < PAGE_SIZE)
		{
			pos_before = PAGE_SIZE;
		}
		os_file_seek(pager.journal_fd, pos_before);
	}

	os_file_write(pager.journal_fd, data, PAGE_SIZE);
	os_file_sync(pager.journal_fd);
}

static void
lru_remove(int32_t slot)
{
	CacheMetadata *entry = &pager.cache_meta[slot];

	if (entry->lru_prev != INVALID_SLOT)
	{
		pager.cache_meta[entry->lru_prev].lru_next = entry->lru_next;
	}
	else
	{
		pager.lru_head = entry->lru_next;
	}

	if (entry->lru_next != INVALID_SLOT)
	{
		pager.cache_meta[entry->lru_next].lru_prev = entry->lru_prev;
	}
	else
	{
		pager.lru_tail = entry->lru_prev;
	}

	entry->lru_next = INVALID_SLOT;
	entry->lru_prev = INVALID_SLOT;
}

static void
lru_add_head(int32_t slot)
{
	CacheMetadata *entry = &pager.cache_meta[slot];

	entry->lru_next = pager.lru_head;
	entry->lru_prev = INVALID_SLOT;

	if (pager.lru_head != INVALID_SLOT)
	{
		pager.cache_meta[pager.lru_head].lru_prev = slot;
	}

	pager.lru_head = slot;

	if (pager.lru_tail == INVALID_SLOT)
	{
		pager.lru_tail = slot;
	}
}

static void
cache_move_to_head(int32_t slot)
{
	if (pager.lru_head == slot)
	{
		return;
	}

	lru_remove(slot);
	lru_add_head(slot);
}

static int32_t
cache_evict_lru()
{
	if (pager.lru_tail == INVALID_SLOT)
	{
		return INVALID_SLOT;
	}

	int32_t		   slot = pager.lru_tail;
	CacheMetadata *entry = &pager.cache_meta[slot];

	if (entry->is_dirty)
	{
		write_page_to_disk(entry->page_index,& pager.cache_data[slot]);
	}

	hashmap_delete(&pager.page_to_cache, entry->page_index);

	lru_remove(slot);

	entry->is_valid = false;
	entry->is_dirty = false;
	entry->page_index = PAGE_INVALID;

	return slot;
}

static uint32_t
cache_find_slot()
{
	for (uint32_t i = 0; i < MAX_CACHE_ENTRIES; i++)
	{
		if (!pager.cache_meta[i].is_valid)
		{
			return i;
		}
	}

	return cache_evict_lru();
}

static void
cache_write_dirty()
{
	for (uint32_t i = 0; i < MAX_CACHE_ENTRIES; i++)
	{
		if (pager.cache_meta[i].is_valid && pager.cache_meta[i].is_dirty)
		{
			write_page_to_disk(pager.cache_meta[i].page_index, &pager.cache_data[i]);
			pager.cache_meta[i].is_dirty = false;
		}
	}
}

static void
cache_init()
{
	for (uint32_t i = 0; i < MAX_CACHE_ENTRIES; i++)
	{
		pager.cache_meta[i].page_index = PAGE_INVALID;
		pager.cache_meta[i].is_dirty = false;
		pager.cache_meta[i].is_valid = false;
		pager.cache_meta[i].lru_next = INVALID_SLOT;
		pager.cache_meta[i].lru_prev = INVALID_SLOT;
	}

	hashset_clear(&pager.free_pages_set);
	hashset_clear(&pager.journaled_pages);
	hashset_clear(&pager.new_pages_in_transaction);
	hashmap_clear(&pager.page_to_cache);
	pager.lru_head = INVALID_SLOT;
	pager.lru_tail = INVALID_SLOT;
	arena::reset<PagerArena>();
}

static void
load_root()
{
	if (read_page_from_disk(PAGE_ROOT, &pager.root))
	{
		pager.root_dirty = false;
	}
	else
	{
		pager.root.page_counter = 1;
		pager.root.free_page = PAGE_INVALID;
		pager.root_dirty = true;
	}
}

static void
save_root()
{
	journal_page(PAGE_ROOT, &pager.root);

	if (pager.root_dirty)
	{
		write_page_to_disk(PAGE_ROOT, &pager.root);
		pager.root_dirty = false;
	}
}

static uint32_t
create_free_page(uint32_t prev_free_page)
{
	uint32_t index = pager.root.page_counter++;
	pager.root_dirty = true;

	uint32_t	   slot = cache_find_slot();
	CacheMetadata *entry = &pager.cache_meta[slot];

	memset(&pager.cache_data[slot], 0, PAGE_SIZE);
	entry->page_index = index;
	entry->is_valid = true;
	entry->is_dirty = true;

	hashmap_insert(&pager.page_to_cache, index, slot);
	lru_add_head(slot);

	FreePage *free = reinterpret_cast<FreePage *>(&pager.cache_data[slot]);

	free->free_pointer = PAGE_INVALID;
	free->prev_free_page = prev_free_page;
	free->next_free_page = PAGE_INVALID;
	free->index = index;

	if (prev_free_page != PAGE_INVALID)
	{
		void *prev_data = pager_get(prev_free_page);
		if (prev_data)
		{
			FreePage *prev_free = static_cast<FreePage *>(prev_data);
			prev_free->next_free_page = index;
			pager_mark_dirty(prev_free_page);
		}
	}

	hashset_insert(&pager.new_pages_in_transaction, index);

	return index;
}

static void
add_to_free_list(uint32_t page_index)
{
	if (page_index == PAGE_INVALID || page_index >= pager.root.page_counter)
	{
		return;
	}

	hashset_insert(&pager.free_pages_set, page_index);

	if (pager.root.free_page == PAGE_INVALID)
	{
		pager.root.free_page = create_free_page(PAGE_INVALID);
		hashset_insert(&pager.new_pages_in_transaction, pager.root.free_page);
		pager.root_dirty = true;
	}

	void *data = pager_get(pager.root.free_page);
	if (!data)
	{
		return;
	}

	FreePage *page = static_cast<FreePage *>(data);

	if (page->free_pointer >= FREE_PAGES_PER_FREE_PAGE)
	{
		uint32_t new_free_page = create_free_page(pager.root.free_page);
		hashset_insert(&pager.new_pages_in_transaction, pager.root.free_page);
		pager.root.free_page = new_free_page;
		pager.root_dirty = true;

		page = static_cast<FreePage *>(pager_get(new_free_page));
	}

	pager_mark_dirty(pager.root.free_page);
	page->free_pages[page->free_pointer++] = page_index;
}

static uint32_t
take_from_free_list()
{
	if (pager.root.free_page == PAGE_INVALID || pager.free_pages_set.count == 0)
	{
		return PAGE_INVALID;
	}

	FreePage *page = static_cast<FreePage *>(pager_get(pager.root.free_page));

	if (page->free_pointer == PAGE_INVALID)
	{
		uint32_t empty_free_page_index = pager.root.free_page;

		if (page->prev_free_page != PAGE_INVALID)
		{
			pager.root.free_page = page->prev_free_page;
			pager.root_dirty = true;

			FreePage *prev_data = static_cast<FreePage *>(pager_get(page->prev_free_page));
			prev_data->next_free_page = PAGE_INVALID;
			pager_mark_dirty(prev_data->prev_free_page);

			return empty_free_page_index;
		}
		else
		{
			pager.root.free_page = PAGE_INVALID;
			pager.root_dirty = true;
			return PAGE_INVALID;
		}
	}

	pager_mark_dirty(pager.root.free_page);
	page->free_pointer--;
	uint32_t free_page_index = page->free_pages[page->free_pointer];
	page->free_pages[page->free_pointer] = PAGE_INVALID;

	hashset_delete(&pager.free_pages_set, free_page_index);

	return free_page_index;
}

static void
build_free_pages_set()
{
	uint32_t current_free_page = pager.root.free_page;
	while (current_free_page != PAGE_INVALID)
	{
		void	 *data = pager_get(current_free_page);
		FreePage *page = static_cast<FreePage *>(data);

		for (uint32_t i = 0; i < page->free_pointer; i++)
		{
			hashset_insert(&pager.free_pages_set, page->free_pages[i]);
		}

		current_free_page = page->prev_free_page;
	}
}

// Public API implementation
bool
pager_init(const char *filename)
{
	arena::init<PagerArena>();

	pager.data_file = filename;

	snprintf(pager.journal_file, sizeof(pager.journal_file), "%s-journal", filename);

	bool exists = os_file_exists(filename);
	pager.data_fd = os_file_open(filename, true, true);

	bool journal_exists = os_file_exists(pager.journal_file);
	if (journal_exists)
	{
		pager.in_transaction = true;
		pager_rollback();
	}

	cache_init();

	if (exists)
	{
		load_root();
		build_free_pages_set();
	}
	else
	{
		pager.root.page_counter = 1;
		pager.root.free_page = PAGE_INVALID;
		pager.root_dirty = true;
		save_root();
	}

	return exists;
}

void *
pager_get(uint32_t page_index)
{
	if (page_index >= pager.root.page_counter || page_index == PAGE_ROOT)
	{
		return nullptr;
	}

	if (hashset_contains(&pager.free_pages_set, page_index))
	{
		return nullptr;
	}

	if (hashmap_get(&pager.page_to_cache, page_index))
	{
		uint32_t slot = *hashmap_get(&pager.page_to_cache, page_index);

		cache_move_to_head(slot);
		return &pager.cache_data[slot];
	}

	uint32_t	   slot = cache_find_slot();
	CacheMetadata *entry = &pager.cache_meta[slot];

	if (!read_page_from_disk(page_index, &pager.cache_data[slot]))
	{
		return nullptr;
	}

	entry->page_index = page_index;
	entry->is_valid = true;
	entry->is_dirty = false;

	hashmap_insert(&pager.page_to_cache, page_index, slot);
	lru_add_head(slot);

	return &pager.cache_data[slot];
}

uint32_t
pager_new()
{
	if (!pager.in_transaction)
	{
		return PAGE_INVALID;
	}

	uint32_t page_index = take_from_free_list();

	if (page_index == PAGE_INVALID)
	{
		page_index = pager.root.page_counter++;
		pager.root_dirty = true;
	}

	hashset_insert(&pager.new_pages_in_transaction, page_index);

	uint32_t	   slot = cache_find_slot();
	CacheMetadata *entry = &pager.cache_meta[slot];

	memset(&pager.cache_data[slot], 0, sizeof(Page));
	pager.cache_data[slot].index = page_index;
	entry->page_index = page_index;
	entry->is_valid = true;
	entry->is_dirty = true;

	hashmap_insert(&pager.page_to_cache, page_index, slot);
	lru_add_head(slot);

	return page_index;
}

void
pager_mark_dirty(uint32_t page_index)
{
	if (page_index > pager.root.page_counter || !pager.in_transaction)
	{
		return;
	}

	if (!(hashset_contains(&pager.journaled_pages, page_index)) &&
		!(hashset_contains(&pager.new_pages_in_transaction, page_index)))
	{
		void *data = pager_get(page_index);
		journal_page(page_index, data);
		hashset_insert(&pager.journaled_pages, page_index);
	}

	if (hashmap_get(&pager.page_to_cache, page_index))
	{
		pager.cache_meta[*hashmap_get(&pager.page_to_cache, page_index)].is_dirty = true;
	}
}

void
pager_delete(uint32_t page_index)
{
	if (page_index == PAGE_INVALID || page_index >= pager.root.page_counter || !pager.in_transaction)
	{
		return;
	}

	pager_get(page_index);
	pager_mark_dirty(page_index);

	add_to_free_list(page_index);
}

void
pager_begin_transaction()
{
	if (pager.in_transaction)
	{
		return;
	}

	cache_write_dirty();

	pager.in_transaction = true;

	pager.journal_fd = os_file_open(pager.journal_file, true, true);

	journal_page(PAGE_ROOT, &pager.root);
	hashset_insert(&pager.journaled_pages, (uint32_t)PAGE_ROOT);
}

void
pager_commit()
{
	if (!pager.in_transaction)
	{
		return;
	}

	pager.root_dirty = true;

	cache_write_dirty();
	save_root();

	os_file_sync(pager.data_fd);

	os_file_close(pager.journal_fd);
	os_file_delete(pager.journal_file);

	pager.journal_fd = -1;
	pager.in_transaction = false;

	hashset_clear(&pager.journaled_pages);
	hashset_clear(&pager.new_pages_in_transaction);
}

void
pager_rollback()
{
	if (!pager.in_transaction)
	{
		return;
	}

	int64_t journal_size = os_file_size(pager.journal_fd);

	if (journal_size >= PAGE_SIZE)
	{
		os_file_seek(pager.journal_fd, PAGE_INVALID);
		if (os_file_read(pager.journal_fd, &pager.root, PAGE_SIZE) == PAGE_SIZE)
		{
			pager.root_dirty = true;
			save_root();
		}

		for (int64_t pos = PAGE_SIZE; pos < journal_size; pos += PAGE_SIZE)
		{
			uint8_t page_data[PAGE_SIZE];
			os_file_seek(pager.journal_fd, pos);
			if (os_file_read(pager.journal_fd, page_data, PAGE_SIZE) != PAGE_SIZE)
			{
				break;
			}

			Page	*page = reinterpret_cast<Page *>(page_data);
			uint32_t page_index = page->index;
			write_page_to_disk(page_index, page_data);
		}

		os_file_truncate(pager.data_fd, pager.root.page_counter * PAGE_SIZE);
	}
		os_file_close(pager.journal_fd);
		os_file_delete(pager.journal_file);
		pager.journal_fd = -1;

	cache_init();
	build_free_pages_set();
	pager.in_transaction = false;
}

void
pager_sync()
{
	for (int32_t i = 0; i < MAX_CACHE_ENTRIES; i++)
	{
		if (pager.cache_meta[i].is_valid && pager.cache_meta[i].is_dirty)
		{
			write_page_to_disk(pager.cache_meta[i].page_index, &pager.cache_data[i]);
			pager.cache_meta[i].is_dirty = false;
		}
	}

	save_root();
	os_file_sync(pager.data_fd);
}

void
pager_close()
{
	os_file_close(pager.data_fd);

	pager.in_transaction = false;
	arena::shutdown<PagerArena>();

	pager.data_fd = -1;
}

PagerMeta
pager_get_stats()
{
	PagerMeta data;
	data.total_pages = pager.root.page_counter - 1;
	data.free_pages = pager.free_pages_set.count;

	data.cached_pages = 0;
	data.dirty_pages = 0;

	for (int32_t i = 0; i < MAX_CACHE_ENTRIES; i++)
	{
		if (pager.cache_meta[i].is_valid)
		{
			(data.cached_pages)++;
			if (pager.cache_meta[i].is_dirty)
			{
				(data.dirty_pages)++;
			}
		}
	}

	return data;
}
