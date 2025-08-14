// main.cpp - Enhanced fuzzer for complete btree coverage
#include "arena.hpp"
#include "btree.hpp"
#include "btree_debug.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <vector>
void fuzz_cursor_comprehensive() {

  for (auto tree_type : {BPLUS, BTREE}) {
    const char *db_file =
        tree_type == BTREE ? "fuzz_cursor_btree.db" : "fuzz_cursor_bplus.db";
    pager_init(db_file);
    pager_begin_transaction();

    int mode = 0;

    BPlusTree tree = bt_create(TYPE_INT32, TYPE_INT32, tree_type);
    bp_init(tree);

    BtCursor *cursor = bt_cursor_create(&tree, true);

  start:
    for (uint32_t i = 0; i < tree.leaf_max_keys * 10; i++) {
      if (!bt_cursor_insert(cursor, (void *)&i, (uint8_t *)&i)) {
        PRINT "Couldn't insert" END;
        exit(0);
      }
    }

    if (mode == 0) {

      bt_cursor_first(cursor);

      auto current = bt_cursor_get_key(cursor);
      do {
        auto key = bt_cursor_get_key(cursor);

        if (cmp(TYPE_INT32, current, key) > 0) {
          PRINT "WRONG ORDER" END;
          exit(0);
        }

        current = key;
      } while (bt_cursor_next(cursor));
    }
    if (mode == 0) {
      bt_cursor_last(cursor);

      auto current = bt_cursor_get_key(cursor);
      do {
        auto key = bt_cursor_get_key(cursor);
        if (cmp(TYPE_INT32, current, key) < 0) {
          PRINT "WRONG ORDER" END;
          exit(0);
        }

        current = key;
      } while (bt_cursor_previous(cursor));
    }
    if (mode == 0) {
      bt_cursor_first(cursor);

      do {
        if (!bt_cursor_delete(cursor)) {
          PRINT "Couldnt delete" END;
          exit(0);
        }
      } while (bt_cursor_has_next(cursor));

      mode = 1;
      goto start;
    }

    if (mode == 1) {
      std::set<uint32_t> to_delete;
      uint32_t i;

      for (int i = 1; i < tree.internal_max_keys; i += 4) {
        if (!bt_cursor_seek(cursor, &i)) {
          PRINT "should have found it" END;

          exit(0);
        }

        bt_cursor_delete(cursor);

        if (bt_cursor_seek(cursor, &i)) {
          PRINT "should not have found it" END;
          exit(0);
        }

        if (!bt_cursor_seek_lt(cursor, &i)) {
          PRINT "should have found it" END;
          exit(0);
        }
      }

      mode = 2;
      goto start;
    }

    if (mode == 2) {
      bt_cursor_first(cursor);
      auto record = (uint32_t *)bt_cursor_read(cursor);
      uint32_t rec_cpy;
      memcpy(&rec_cpy, record, TYPE_INT32);
      uint32_t new_record = 80;
      bt_cursor_update(cursor, (uint8_t *)&new_record);
      bt_cursor_first(cursor);
      if (cmp(TYPE_INT32, bt_cursor_read(cursor), (uint8_t *)&rec_cpy) == 0) {
        PRINT "SHOULD BE UPDATED" END;
        exit(0);
      }
    }

    pager_commit();
    pager_close();
    arena_reset();

    std::cout << "Cursor fuzzing for "
              << (tree_type == BTREE ? "B-tree" : "B+tree")
              << " completed successfully" << std::endl;
  }
}

int main() {
  arena_init(PAGE_SIZE);
  fuzz_cursor_comprehensive();

  arena_shutdown();
  std::cout << "\n=== FUZZING COMPLETE ===\n";
  return 0;
}
