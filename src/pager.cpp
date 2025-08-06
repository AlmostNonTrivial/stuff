#include "pager.hpp"
#include "os_layer.hpp"
#include <cstddef>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unordered_map>
#include <unordered_set>

#define HAS(map, val) (pager.map.find(val) != pager.map.end())
#define GET(map, val) (pager.map.find(val)->second)

struct Page {
  unsigned int index;
  char padding[PAGE_SIZE - sizeof(unsigned int)];
};

#define FREE_PAGES_PER_FREE_PAGE ((PAGE_SIZE - (sizeof(unsigned int) * 4)) / 4)
struct FreePage {
  unsigned int index;
  unsigned int next_free_page;
  unsigned int prev_free_page;
  unsigned int free_pointer;
  unsigned int free_pages[FREE_PAGES_PER_FREE_PAGE];
};

struct RootPage {
  unsigned int page_counter;
  unsigned int free_page;
  char padding[PAGE_SIZE - (sizeof(unsigned int) * 2)];
};

struct CacheEntry {
  unsigned int page_index;
  bool is_dirty;
  bool is_valid;
  int lru_next;
  int lru_prev;
  char data[PAGE_SIZE];
};

static struct {
  os_file_handle_t data_fd;
  os_file_handle_t journal_fd;

  RootPage root;
  bool root_dirty;

  CacheEntry cache[MAX_CACHE_ENTRIES];
  std::unordered_map<unsigned int, int> page_to_cache;
  int lru_head;
  int lru_tail;
  std::unordered_set<unsigned int> free_pages_set;

  bool in_transaction;

  std::unordered_set<unsigned int> journaled_pages;
  std::unordered_set<unsigned int> new_pages_in_transaction;

  const char *data_file;
  char journal_file[256];
} pager = {.data_fd = -1,
           .journal_fd = -1,
           .root = {},
           .root_dirty = false,
           .cache = {},
           .lru_head = -1,
           .lru_tail = -1,
           .in_transaction = false,
           .data_file = nullptr};

static int cache_evict_lru();
static void cache_move_to_head(int slot);
static void write_page_to_disk(unsigned int page_index, const void *data);
static bool read_page_from_disk(unsigned int page_index, void *data);
static void journal_page(unsigned int page_index, const void *data);


static void build_free_pages_set() {
  pager.free_pages_set.clear();

  unsigned int current_free_page = pager.root.free_page;
  while (current_free_page != PAGE_INVALID) {

    void *data = pager_get(current_free_page);

    if (!data)
      break;

    FreePage *page = (FreePage *)data;

    for (unsigned int i = 0; i < page->free_pointer; i++) {
      pager.free_pages_set.insert(page->free_pages[i]);
    }

    current_free_page = page->prev_free_page;
  }
}

static void cache_init() {

  for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
    pager.cache[i].page_index = PAGE_INVALID;
    pager.cache[i].is_dirty = false;
    pager.cache[i].is_valid = false;
    pager.cache[i].lru_next = -1;
    pager.cache[i].lru_prev = -1;
  }

  pager.page_to_cache.clear();
  pager.journaled_pages.clear();
  pager.new_pages_in_transaction.clear();

  pager.lru_head = -1;
  pager.lru_tail = -1;
}

static void lru_remove(int slot) {
  CacheEntry *entry = &pager.cache[slot];

  if (entry->lru_prev != -1) {
    pager.cache[entry->lru_prev].lru_next = entry->lru_next;
  } else {
    pager.lru_head = entry->lru_next;
  }

  if (entry->lru_next != -1) {
    pager.cache[entry->lru_next].lru_prev = entry->lru_prev;
  } else {
    pager.lru_tail = entry->lru_prev;
  }

  entry->lru_next = -1;
  entry->lru_prev = -1;
}

static void lru_add_head(int slot) {
  CacheEntry *entry = &pager.cache[slot];

  entry->lru_next = pager.lru_head;
  entry->lru_prev = -1;

  if (pager.lru_head != -1) {
    pager.cache[pager.lru_head].lru_prev = slot;
  }

  pager.lru_head = slot;

  if (pager.lru_tail == -1) {
    pager.lru_tail = slot;
  }
}

static void cache_move_to_head(int slot) {
  if (pager.lru_head == slot)
    return;

  lru_remove(slot);
  lru_add_head(slot);
}

static int cache_find_slot() {
  for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
    if (!pager.cache[i].is_valid) {
      return i;
    }
  }

  return cache_evict_lru();
}

static void cache_write_dirty() {
  for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
    if (pager.cache[i].is_valid && pager.cache[i].is_dirty) {
      write_page_to_disk(pager.cache[i].page_index, pager.cache[i].data);
      pager.cache[i].is_dirty = false;
    }
  }
}

static int cache_evict_lru() {
  if (pager.lru_tail == -1)
    return -1;

  int slot = pager.lru_tail;

  CacheEntry *entry = &pager.cache[slot];

  if (entry->is_dirty) {
    write_page_to_disk(entry->page_index, entry->data);
  }

  pager.page_to_cache.erase(entry->page_index);

  lru_remove(slot);

  entry->is_valid = false;
  entry->is_dirty = false;
  entry->page_index = PAGE_INVALID;
  return slot;
}

static void load_root() {
  if (read_page_from_disk(PAGE_ROOT, &pager.root)) {
    pager.root_dirty = false;
  } else {
    pager.root.page_counter = 1;
    pager.root.free_page = PAGE_INVALID;
    pager.root_dirty = true;
  }
}

static void save_root() {
  journal_page(PAGE_ROOT, &pager.root);

  if (pager.root_dirty) {
    write_page_to_disk(PAGE_ROOT, &pager.root);
    pager.root_dirty = false;
  }
}

static void write_page_to_disk(unsigned int page_index, const void *data) {
  os_file_seek(pager.data_fd, page_index * PAGE_SIZE);
  os_file_write(pager.data_fd, data, PAGE_SIZE);
}

static bool read_page_from_disk(unsigned int page_index, void *data) {
  os_file_seek(pager.data_fd, page_index * PAGE_SIZE);
  return os_file_read(pager.data_fd, data, PAGE_SIZE) == PAGE_SIZE;
}

static void journal_page(unsigned int page_index, const void *data) {
  if (!pager.in_transaction || pager.journal_fd == -1 ||
      HAS(new_pages_in_transaction, page_index) ||
      HAS(journaled_pages, page_index)) {
    return;
  }

  if (page_index == PAGE_ROOT) {
    os_file_seek(pager.journal_fd, PAGE_INVALID);
  } else {
    long pos_before = os_file_size(pager.journal_fd);
    if (pos_before < PAGE_SIZE) {
      pos_before = PAGE_SIZE;
    }
    os_file_seek(pager.journal_fd, pos_before);
  }

  os_file_write(pager.journal_fd, data, PAGE_SIZE);
  os_file_sync(pager.journal_fd);
}

static unsigned int create_free_page(unsigned int prev_free_page) {
  unsigned int index = pager.root.page_counter++;
  pager.root_dirty = true;

  int slot = cache_find_slot();
  CacheEntry *entry = &pager.cache[slot];

  memset(entry->data, PAGE_INVALID, PAGE_SIZE);
  entry->page_index = index;
  entry->is_valid = true;
  entry->is_dirty = true;

  pager.page_to_cache[index] = slot;
  lru_add_head(slot);

  FreePage *free = (FreePage *)entry->data;

  free->free_pointer = PAGE_INVALID;
  free->prev_free_page = prev_free_page;
  free->next_free_page = PAGE_INVALID;
  free->index = index;

  if (prev_free_page != PAGE_INVALID) {
    void *prev_data = pager_get(prev_free_page);
    if (prev_data) {
      FreePage *prev_free = (FreePage *)prev_data;
      prev_free->next_free_page = index;
      pager_mark_dirty(prev_free_page);
    }
  }

  pager.new_pages_in_transaction.insert(index);

  return index;
}

static void add_to_free_list(unsigned int page_index) {
  if (page_index == PAGE_INVALID || page_index >= pager.root.page_counter) {
    return;
  }
  pager.free_pages_set.insert(page_index);

  if (pager.root.free_page == PAGE_INVALID) {
    pager.root.free_page = create_free_page(PAGE_INVALID);
    pager.new_pages_in_transaction.insert(pager.root.free_page);
    pager.root_dirty = true;
  }

  void *data = pager_get(pager.root.free_page);
  if (!data) {
    return;
  }

  FreePage *page = (FreePage *)data;

  if (page->free_pointer >= FREE_PAGES_PER_FREE_PAGE) {
    unsigned int new_free_page = create_free_page(pager.root.free_page);
    pager.new_pages_in_transaction.insert(new_free_page);
    pager.root.free_page = new_free_page;
    pager.root_dirty = true;

    page = (FreePage *)pager_get(new_free_page);
  }

  pager_mark_dirty(pager.root.free_page);
  page->free_pages[page->free_pointer++] = page_index;
}

static unsigned int take_from_free_list() {
  if (pager.root.free_page == PAGE_INVALID || pager.free_pages_set.empty()) {
    return PAGE_INVALID;
  }

  FreePage *page = (FreePage *)pager_get(pager.root.free_page);

  if (page->free_pointer == PAGE_INVALID) {
    unsigned int empty_free_page_index = pager.root.free_page;

    if (page->prev_free_page != PAGE_INVALID) {
      pager.root.free_page = page->prev_free_page;
      pager.root_dirty = true;

      auto prev_data = (FreePage *)pager_get(page->prev_free_page);

      prev_data->next_free_page = PAGE_INVALID;
      pager_mark_dirty(prev_data->prev_free_page);

      return empty_free_page_index;

    } else {
      pager.root.free_page = PAGE_INVALID;
      pager.root_dirty = true;
      return PAGE_INVALID;
    }
  }

  pager_mark_dirty(pager.root.free_page);
  page->free_pointer--;
  unsigned int free_page_index = page->free_pages[page->free_pointer];
  page->free_pages[page->free_pointer] = PAGE_INVALID;

  pager.free_pages_set.erase(free_page_index);

  return free_page_index;
}

void pager_init(const char *filename) {
  pager.data_file = filename;

  snprintf(pager.journal_file, sizeof(pager.journal_file), "%s-journal",
           filename);

  bool exists = os_file_exists(filename);

  pager.data_fd =
      os_file_open(filename, true, true);

  bool journal_exists = os_file_exists(pager.journal_file);
  if (journal_exists) {
    pager.in_transaction = true;
    pager_rollback();
  }

  cache_init();

  if (exists) {
    load_root();
    build_free_pages_set();
  } else {
    pager.root.page_counter = 1;
    pager.root.free_page = PAGE_INVALID;
    pager.root_dirty = true;
    save_root();
  }
}

void *pager_get(unsigned int page_index) {
  if (page_index >= pager.root.page_counter || page_index == PAGE_ROOT) {
    return nullptr;
  }

  if (HAS(free_pages_set, page_index)) {
    return nullptr;
  }

  auto it = pager.page_to_cache.find(page_index);
  if (it != pager.page_to_cache.end()) {
    int slot = it->second;
    cache_move_to_head(slot);
    return pager.cache[slot].data;
  }

  int slot = cache_find_slot();
  CacheEntry *entry = &pager.cache[slot];

  if (!read_page_from_disk(page_index, entry->data)) {
    return nullptr;
  }

  entry->page_index = page_index;
  entry->is_valid = true;
  entry->is_dirty = false;

  pager.page_to_cache[page_index] = slot;
  lru_add_head(slot);

  return entry->data;
}

unsigned int pager_new() {
  if (!pager.in_transaction) {
    return PAGE_INVALID;
  }

  unsigned int page_index = take_from_free_list();

  if (page_index == PAGE_INVALID) {
    page_index = pager.root.page_counter++;
    pager.root_dirty = true;
  }

  pager.new_pages_in_transaction.insert(page_index);

  int slot = cache_find_slot();
  CacheEntry *entry = &pager.cache[slot];

  memset(entry->data, PAGE_INVALID, PAGE_SIZE);
  entry->page_index = page_index;
  entry->is_valid = true;
  entry->is_dirty = true;

  pager.page_to_cache[page_index] = slot;
  lru_add_head(slot);

  return page_index;
}

void pager_mark_dirty(unsigned int page_index) {
  if (page_index > pager.root.page_counter && !pager.in_transaction) {
    return;
  }

  if (!HAS(journaled_pages, page_index) &&
      !HAS(new_pages_in_transaction, page_index)) {
    void *data = pager_get(page_index);
    journal_page(page_index, data);
    pager.journaled_pages.insert(page_index);
  }

  if (HAS(page_to_cache, page_index)) {
    pager.cache[GET(page_to_cache, page_index)].is_dirty = true;
  }
}

void pager_delete(unsigned int page_index) {
  if (page_index == PAGE_INVALID ||
      page_index >= pager.root.page_counter && !pager.in_transaction) {
    return;
  }

  auto data = pager_get(page_index);
  pager_mark_dirty(page_index);

  add_to_free_list(page_index);
}

void pager_begin_transaction() {
  if (pager.in_transaction) {
    return;
  }

  cache_write_dirty();

  pager.in_transaction = true;

  pager.journaled_pages.clear();
  pager.new_pages_in_transaction.clear();

  pager.journal_fd = os_file_open(pager.journal_file, true, true);

  journal_page(PAGE_ROOT, &pager.root);

  pager.journaled_pages.insert(PAGE_ROOT);
}

void pager_commit() {
  if (!pager.in_transaction) {
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
  pager.journaled_pages.clear();
  pager.new_pages_in_transaction.clear();
}

void pager_rollback() {
  if (!pager.in_transaction)
    return;

  long journal_size = os_file_size(pager.journal_fd);

  if (journal_size >= PAGE_SIZE) {
    os_file_seek(pager.journal_fd, PAGE_INVALID);
    if (os_file_read(pager.journal_fd, &pager.root, PAGE_SIZE) == PAGE_SIZE) {
      pager.root_dirty = true;
      save_root();
    }

    for (long pos = PAGE_SIZE; pos < journal_size; pos += PAGE_SIZE) {
      char page_data[PAGE_SIZE];
      os_file_seek(pager.journal_fd, pos);
      if (os_file_read(pager.journal_fd, page_data, PAGE_SIZE) != PAGE_SIZE) {
        break;
      }

      Page *page = (Page *)page_data;

      unsigned int page_index = page->index;
      write_page_to_disk(page_index, page_data);
    }

    os_file_truncate(pager.data_fd, pager.root.page_counter * PAGE_SIZE);
    os_file_close(pager.journal_fd);
    os_file_delete(pager.journal_file);
    pager.journal_fd = -1;
    pager.new_pages_in_transaction.clear();
  }

  cache_init();
  build_free_pages_set();
}

void pager_sync() {
  for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
    if (pager.cache[i].is_valid && pager.cache[i].is_dirty) {
      write_page_to_disk(pager.cache[i].page_index, pager.cache[i].data);
      pager.cache[i].is_dirty = false;
    }
  }

  save_root();

  os_file_sync(pager.data_fd);
}

void pager_get_stats(unsigned int *total_pages, unsigned int *free_pages,
                     unsigned int *cached_pages, unsigned int *dirty_pages) {

  *total_pages = pager.root.page_counter - 1;

  *free_pages = pager.free_pages_set.size();

  *cached_pages = PAGE_INVALID;
  *dirty_pages = PAGE_INVALID;

  for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
    if (pager.cache[i].is_valid) {
      (*cached_pages)++;
      if (pager.cache[i].is_dirty) {
        (*dirty_pages)++;
      }
    }
  }
}

void pager_close() {
  pager_sync();

  os_file_close(pager.data_fd);

  pager.data_fd = -1;
}
