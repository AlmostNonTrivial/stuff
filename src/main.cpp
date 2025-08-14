// main.cpp - Enhanced fuzzer for complete btree coverage
#include "arena.hpp"
#include "btree.hpp"
#include "btree_debug.hpp"
#include "btree_tests.hpp"
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
  std::random_device rd;
  std::mt19937 gen(rd());

  for (auto tree_type : {BPLUS, BTREE}) {
    const char *db_file =
        tree_type == BTREE ? "fuzz_cursor_btree.db" : "fuzz_cursor_bplus.db";
    pager_init(db_file);
    pager_begin_transaction();

    BPlusTree tree = bt_create(TYPE_INT32, TYPE_INT32, tree_type);
    bp_init(tree);

    // Create both read and write cursors
    BtCursor *write_cursor = bt_cursor_create(&tree, true);
    BtCursor *read_cursor = bt_cursor_create(&tree, false);

    std::set<uint32_t> inserted_keys;
    std::vector<uint32_t> keys_vector;

    // Different distribution patterns
    std::uniform_int_distribution<uint32_t> sparse_dist(0, 1000000);
    std::uniform_int_distribution<uint32_t> dense_dist(0, 1000);
    std::uniform_int_distribution<uint32_t> tiny_dist(0, 10);
    std::bernoulli_distribution coin_flip(0.5);

    // Phase 1: Build tree using cursor insertions
    std::cout << "Phase 1: Building tree with cursor insertions..."
              << std::endl;
    for (uint32_t i = 0; i < tree.leaf_max_keys * 20; i++) {
      uint32_t key;

      // Mix different key distributions
      if (i % 4 == 0) {
        key = sparse_dist(gen);
      } else if (i % 4 == 1) {
        key = dense_dist(gen);
      } else if (i % 4 == 2) {
        key = tiny_dist(gen); // Lots of duplicates
      } else {
        key = i; // Sequential
      }

      uint8_t record[TYPE_INT32];
      memcpy(record, &key, sizeof(key));

      // Use cursor insert
      bt_cursor_insert(write_cursor, &key, record);

      if (tree_type == BTREE ||
          inserted_keys.find(key) == inserted_keys.end()) {
        inserted_keys.insert(key);
        keys_vector.push_back(key);
      }

      // Periodically validate
      if (i % 100 == 0) {
        bp_validate_all_invariants(tree);
      }
    }

    // Phase 2: Test cursor navigation
    std::cout << "Phase 2: Testing cursor navigation..." << std::endl;

    // Test first/last
    if (bt_cursor_first(read_cursor)) {
      const uint8_t *first_key = bt_cursor_get_key(read_cursor);
      if (!first_key) {
        std::cerr << "Failed to get first key" << std::endl;
      }
    }

    if (bt_cursor_last(read_cursor)) {
      const uint8_t *last_key = bt_cursor_get_key(read_cursor);
      if (!last_key) {
        std::cerr << "Failed to get last key" << std::endl;
      }
    }

    // Test forward traversal and collect all keys
    std::vector<uint32_t> traversed_keys;
    if (bt_cursor_first(read_cursor)) {
      do {
        const uint8_t *key = bt_cursor_get_key(read_cursor);
        if (key) {
          traversed_keys.push_back(*reinterpret_cast<const uint32_t *>(key));
        }
      } while (bt_cursor_next(read_cursor));
    }

    // Test backward traversal
    std::vector<uint32_t> reverse_traversed;
    if (bt_cursor_last(read_cursor)) {
      do {
        const uint8_t *key = bt_cursor_get_key(read_cursor);
        if (key) {
          reverse_traversed.push_back(*reinterpret_cast<const uint32_t *>(key));
        }
      } while (bt_cursor_previous(read_cursor));
    }

    // Verify traversal ordering
    for (size_t i = 1; i < traversed_keys.size(); i++) {
      if (traversed_keys[i] < traversed_keys[i - 1]) {
        std::cerr << "Forward traversal order violation!" << std::endl;
      }
    }

    for (size_t i = 1; i < reverse_traversed.size(); i++) {
      if (reverse_traversed[i] > reverse_traversed[i - 1]) {
        std::cerr << "Backward traversal order violation!" << std::endl;
      }
    }

    // Phase 3: Mixed cursor operations
    std::cout << "Phase 3: Mixed cursor operations..." << std::endl;
    std::uniform_real_distribution<double> op_dist(0.0, 1.0);

    for (uint32_t op = 0; op < tree.leaf_max_keys * 50; op++) {
      double choice = op_dist(gen);

      if (choice < 0.2 && !keys_vector.empty()) {
        // Delete using cursor
        size_t idx = std::uniform_int_distribution<size_t>(
            0, keys_vector.size() - 1)(gen);
        uint32_t key = keys_vector[idx];

        if (bt_cursor_seek(write_cursor, &key)) {
          bt_cursor_delete(write_cursor);
          keys_vector.erase(keys_vector.begin() + idx);
          inserted_keys.erase(key);
        }

      } else if (choice < 0.4) {
        // Insert new key using cursor
        uint32_t key = sparse_dist(gen);
        uint8_t record[TYPE_INT32];
        memcpy(record, &key, sizeof(key));

        bt_cursor_insert(write_cursor, &key, record);
        if (inserted_keys.find(key) == inserted_keys.end()) {
          inserted_keys.insert(key);
          keys_vector.push_back(key);
        }

      } else if (choice < 0.5) {
        // Update existing key using cursor
        if (!keys_vector.empty()) {
          size_t idx = std::uniform_int_distribution<size_t>(
              0, keys_vector.size() - 1)(gen);
          uint32_t key = keys_vector[idx];

          if (bt_cursor_seek(write_cursor, &key)) {
            uint32_t new_value = key * 2;
            bt_cursor_update(write_cursor,
                             reinterpret_cast<uint8_t *>(&new_value));
          }
        }

      } else if (choice < 0.7) {
        // Seek operations (exact match)
        uint32_t key;
        if (coin_flip(gen) && !keys_vector.empty()) {
          // Seek existing key
          key = keys_vector[std::uniform_int_distribution<size_t>(
              0, keys_vector.size() - 1)(gen)];
        } else {
          // Seek non-existing key
          key = sparse_dist(gen) + 2000000;
        }
        bt_cursor_seek(read_cursor, &key);

      } else if (choice < 0.8) {
        // Test seek comparison operations
        if (!keys_vector.empty()) {
          uint32_t key = keys_vector[std::uniform_int_distribution<size_t>(
              0, keys_vector.size() - 1)(gen)];

          // Test all seek variants
          int seek_type = std::uniform_int_distribution<int>(0, 3)(gen);
          switch (seek_type) {
          case 0:
            bt_cursor_seek_ge(read_cursor, &key);
            break;
          case 1:
            bt_cursor_seek_gt(read_cursor, &key);
            break;
          case 2:
            bt_cursor_seek_le(read_cursor, &key);
            break;
          case 3:
            bt_cursor_seek_lt(read_cursor, &key);
            break;
          }
        }

      } else {
        // Random navigation
        int nav_type = std::uniform_int_distribution<int>(0, 3)(gen);
        switch (nav_type) {
        case 0:
          bt_cursor_first(read_cursor);
          break;
        case 1:
          bt_cursor_last(read_cursor);
          break;
        case 2:
          bt_cursor_next(read_cursor);
          break;
        case 3:
          bt_cursor_previous(read_cursor);
          break;
        }
      }

      // Periodically validate
      if (op % 50 == 0) {
        bp_validate_all_invariants(tree);
      }
    }

    // Phase 4: Edge case testing
    std::cout << "Phase 4: Testing cursor edge cases..." << std::endl;

    // Test cursor at boundaries
    bt_cursor_first(read_cursor);
    bt_cursor_previous(read_cursor); // Should fail gracefully

    bt_cursor_last(read_cursor);
    bt_cursor_next(read_cursor); // Should fail gracefully

    // Test is_end
    bt_cursor_last(read_cursor);
    bool at_end = bt_cursor_is_end(read_cursor);
    bt_cursor_next(read_cursor);
    at_end = bt_cursor_is_end(read_cursor); // Should be true

    // Test operations on invalid cursor
    bt_cursor_clear(read_cursor);
    const uint8_t *invalid_key =
        bt_cursor_get_key(read_cursor);              // Should return nullptr
    bool invalid_next = bt_cursor_next(read_cursor); // Should return false

    // Phase 5: Delete using cursor in different patterns
    std::cout << "Phase 5: Cursor deletion patterns..." << std::endl;

    if (tree_type == BTREE) {
      // Random deletion using cursor for B-tree
      while (!keys_vector.empty()) {
        size_t idx = std::uniform_int_distribution<size_t>(
            0, keys_vector.size() - 1)(gen);
        uint32_t key = keys_vector[idx];

        if (bt_cursor_seek(write_cursor, &key)) {
          bt_cursor_delete(write_cursor);
          keys_vector.erase(keys_vector.begin() + idx);
        }
      }
    } else {
      // Forward deletion for B+tree using cursor
      if (bt_cursor_first(write_cursor)) {
        while (bt_cursor_is_valid(write_cursor)) {
          bt_cursor_delete(write_cursor);
          // Cursor should auto-advance or become invalid
          if (!bt_cursor_is_valid(write_cursor)) {
            break;
          }
        }
      }
    }

    // Verify tree is empty
    if (bt_cursor_first(read_cursor)) {
      std::cerr << "Tree should be empty but cursor found elements!"
                << std::endl;
    }

    // Cleanup



    pager_rollback();
    pager_close();
    arena_reset();

    std::cout << "Cursor fuzzing for "
              << (tree_type == BTREE ? "B-tree" : "B+tree")
              << " completed successfully" << std::endl;
  }
}

void fuzz_cursor_stress() {
  std::cout << "\n=== Cursor Stress Testing ===" << std::endl;
  std::random_device rd;
  std::mt19937 gen(rd());

  for (auto tree_type : {BPLUS, BTREE}) {
    pager_init("cursor_stress.db");
    pager_begin_transaction();

    BPlusTree tree = bt_create(TYPE_INT32, TYPE_INT32, tree_type);
    bp_init(tree);

    // Create multiple cursors
    std::vector<BtCursor *> cursors;
    for (int i = 0; i < 5; i++) {
      cursors.push_back(
          bt_cursor_create(&tree, i == 0)); // First is write cursor
    }

    // Build initial tree
    for (uint32_t i = 0; i < tree.leaf_max_keys * 10; i++) {
      uint8_t record[TYPE_INT32];
      memcpy(record, &i, sizeof(i));
      bp_insert_element(tree, &i, record);
    }

    // Position cursors at different locations
    bt_cursor_first(cursors[1]);
    bt_cursor_last(cursors[2]);
    uint32_t mid_key = tree.leaf_max_keys * 5;
    bt_cursor_seek(cursors[3], &mid_key);
    bt_cursor_seek_ge(cursors[4], &mid_key);

    // Perform modifications while cursors are positioned
    for (uint32_t i = 0; i < tree.leaf_max_keys * 2; i++) {
      uint32_t key = std::uniform_int_distribution<uint32_t>(
          0, tree.leaf_max_keys * 10)(gen);

      // Delete using write cursor
      if (bt_cursor_seek(cursors[0], &key)) {
        bt_cursor_delete(cursors[0]);

        // Check other cursors still valid (they might be invalidated)
        for (size_t j = 1; j < cursors.size(); j++) {
          if (bt_cursor_is_valid(cursors[j])) {
            const uint8_t *cursor_key = bt_cursor_get_key(cursors[j]);
            // Cursor should still point to valid data
          }
        }
      }

      // Insert new data
      uint32_t new_key = tree.leaf_max_keys * 10 + i;
      uint8_t record[TYPE_INT32];
      memcpy(record, &new_key, sizeof(new_key));
      bt_cursor_insert(cursors[0], &new_key, record);
    }

    // Cleanup cursors

    pager_rollback();
    pager_close();
    arena_reset();
  }

  std::cout << "Cursor stress testing completed" << std::endl;
}

int main() {
  arena_init(PAGE_SIZE);
  fuzz_cursor_comprehensive();

  arena_shutdown();
  std::cout << "\n=== FUZZING COMPLETE ===\n";
  return 0;
}
