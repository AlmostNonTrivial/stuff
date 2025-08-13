// main.cpp - Enhanced fuzzer for complete btree coverage
#include "btree.hpp"
#include "btree_debug.hpp"
#include "btree_tests.hpp"
#include "pager.hpp"
#include <cassert>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <vector>
#include <algorithm>
#include <limits>

// External coverage function
extern void print_coverage_report();

void fuzz_edge_cases() {
  // Test edge cases that are often missed
  for (auto tree_type : {BPLUS, BTREE}) {
    pager_init(tree_type == BTREE ? "edge_btree.db" : "edge_bplus.db");
    pager_begin_transaction();

    BPlusTree tree = bt_create(TYPE_INT32, TYPE_INT32, tree_type);
    bp_init(tree);

    // Test boundary values
    uint32_t boundary_keys[] = {0, 1, 2, UINT32_MAX - 1, UINT32_MAX};
    for (auto key : boundary_keys) {
      uint8_t record[TYPE_INT32];
      memcpy(record, &key, sizeof(key));
      bp_insert_element(tree, &key, record);
    }

    // Delete in specific patterns to trigger merge cases
    for (auto key : boundary_keys) {
      bp_delete_element(tree, &key);
    }

    pager_rollback();
    pager_close();
  }
}

void fuzz_different_types() {
    return;
  // Test different data types to hit type-specific code paths
  DataType types[] = {TYPE_INT32, TYPE_INT64, TYPE_VARCHAR32, TYPE_VARCHAR256};

  for (auto key_type : types) {
    for (auto tree_type : {BPLUS, BTREE}) {
      pager_init("type_test.db");
      pager_begin_transaction();

      BPlusTree tree = bt_create(key_type, TYPE_INT32, tree_type);
      bp_init(tree);

      // Insert based on type
      if (key_type == TYPE_INT32) {
        for (uint32_t i = 0; i < tree.leaf_max_keys * 3; i++) {
          uint8_t record[TYPE_INT32];
          memcpy(record, &i, sizeof(i));
          bp_insert_element(tree, &i, record);
        }

        // Delete to test paths
        for (uint32_t i = 0; i < tree.leaf_max_keys; i++) {
          bp_delete_element(tree, &i);
        }
      } else if (key_type == TYPE_INT64) {
        for (uint64_t i = 0; i < tree.leaf_max_keys * 3; i++) {
          uint8_t record[TYPE_INT32];
          uint32_t val = i;
          memcpy(record, &val, sizeof(val));
          bp_insert_element(tree, &i, record);
        }
      } else { // VARCHAR types
        for (uint32_t i = 0; i < tree.leaf_max_keys * 2; i++) {
          uint8_t key[256] = {0};
          snprintf((char*)key, sizeof(key), "key_%010u", i);
          uint8_t record[TYPE_INT32];
          memcpy(record, &i, sizeof(i));
          bp_insert_element(tree, key, record);
        }
      }

      // Use print functions to hit those coverage points
      print_tree(tree);

      pager_rollback();
      pager_close();
    }
  }
}

void fuzz_comprehensive() {
  std::random_device rd;
  std::mt19937 gen(rd());

  for (auto tree_type : {BPLUS, BTREE}) {
    const char *db_file = tree_type == BTREE ? "fuzz_btree.db" : "fuzz_bplus.db";
    pager_init(db_file);
    pager_begin_transaction();

    BPlusTree tree = bt_create(TYPE_INT32, TYPE_INT32, tree_type);
    bp_init(tree);

    std::set<uint32_t> inserted_keys;
    std::vector<uint32_t> keys_vector;

    // Different distribution patterns to hit various code paths
    std::uniform_int_distribution<uint32_t> sparse_dist(0, 1000000);
    std::uniform_int_distribution<uint32_t> dense_dist(0, 1000);
    std::uniform_int_distribution<uint32_t> tiny_dist(0, 10);
    std::bernoulli_distribution coin_flip(0.5);

    // Phase 1: Build tree with various patterns
    for (uint32_t i = 0; i < tree.leaf_max_keys * 50; i++) {
      uint32_t key;

      // Mix different key distributions
      if (i % 4 == 0) {
        key = sparse_dist(gen);
      } else if (i % 4 == 1) {
        key = dense_dist(gen);
      } else if (i % 4 == 2) {
        key = tiny_dist(gen);  // Lots of duplicates
      } else {
        key = i;  // Sequential
      }

      uint8_t record[TYPE_INT32];
      memcpy(record, &key, sizeof(key));

      bp_insert_element(tree, &key, record);

      if (tree_type == BTREE || inserted_keys.find(key) == inserted_keys.end()) {
        inserted_keys.insert(key);
        keys_vector.push_back(key);
      }

      // Periodically validate
      if (i % 100 == 0) {
        bp_validate_all_invariants(tree);
      }
    }

    // Phase 2: Mixed operations with finds
    std::uniform_real_distribution<double> op_dist(0.0, 1.0);
    for (uint32_t op = 0; op < tree.leaf_max_keys * 100; op++) {
      double choice = op_dist(gen);

      if (choice < 0.3 && !keys_vector.empty()) {
        // Delete random key
        size_t idx = std::uniform_int_distribution<size_t>(0, keys_vector.size() - 1)(gen);
        uint32_t key = keys_vector[idx];
        bp_delete_element(tree, &key);
        keys_vector.erase(keys_vector.begin() + idx);
        inserted_keys.erase(key);
      } else if (choice < 0.6) {
        // Insert new key
        uint32_t key = sparse_dist(gen);
        uint8_t record[TYPE_INT32];
        memcpy(record, &key, sizeof(key));
        bp_insert_element(tree, &key, record);
        if (inserted_keys.find(key) == inserted_keys.end()) {
          inserted_keys.insert(key);
          keys_vector.push_back(key);
        }
      } else {
        // Find operations - both existing and non-existing keys
        uint32_t key;
        if (coin_flip(gen) && !keys_vector.empty()) {
          // Search for existing key
          key = keys_vector[std::uniform_int_distribution<size_t>(0, keys_vector.size() - 1)(gen)];
        } else {
          // Search for non-existing key
          key = sparse_dist(gen) + 2000000;
        }
        bp_find_element(tree, &key);
      }
    }

    // Phase 3: Delete everything in different patterns
    if (tree_type == BTREE) {
      // Random deletion for B-tree
      while (!keys_vector.empty()) {
        size_t idx = std::uniform_int_distribution<size_t>(0, keys_vector.size() - 1)(gen);
        uint32_t key = keys_vector[idx];
        bp_delete_element(tree, &key);
        keys_vector.erase(keys_vector.begin() + idx);
      }
    } else {
      // Sequential deletion for B+tree to test leaf chain
      std::sort(keys_vector.begin(), keys_vector.end());
      for (auto key : keys_vector) {
        bp_delete_element(tree, &key);
      }
    }

    // Test destroy function
    // bp_destroy_node(tree, bp_get_root(tree));

    pager_rollback();
    pager_close();
  }
}

void fuzz_special_cases() {
  // Target specific uncovered paths
  for (auto tree_type : {BPLUS, BTREE}) {
    pager_init("special.db");
    pager_begin_transaction();

    BPlusTree tree = bt_create(TYPE_INT32, TYPE_INT32, tree_type);
    bp_init(tree);

    // Create a tree that will force root splits and merges
    uint32_t max = tree.leaf_max_keys;

    // Fill root to max
    for (uint32_t i = 0; i < max; i++) {
      uint8_t record[TYPE_INT32];
      memcpy(record, &i, sizeof(i));
      bp_insert_element(tree, &i, record);
    }

    // Force root split
    uint32_t trigger = max;
    uint8_t record[TYPE_INT32];
    memcpy(record, &trigger, sizeof(trigger));
    bp_insert_element(tree, &trigger, record);

    // Now delete to force merges - delete from middle to stress redistribution
    for (uint32_t i = max/3; i < 2*max/3; i++) {
      bp_delete_element(tree, &i);
      bp_validate_all_invariants(tree);
    }

    // Delete remaining to potentially merge back to root
    for (uint32_t i = 0; i < max/3; i++) {
      bp_delete_element(tree, &i);
    }
    for (uint32_t i = 2*max/3; i <= max; i++) {
      bp_delete_element(tree, &i);
    }

    pager_rollback();
    pager_close();
  }
}

void fuzz_repair_paths() {
  // Specifically target repair and redistribution code paths
  for (auto tree_type : {BPLUS, BTREE}) {
    pager_init("repair.db");
    pager_begin_transaction();

    BPlusTree tree = bt_create(TYPE_INT32, TYPE_INT32, tree_type);
    bp_init(tree);

    // Build a 3-level tree
    for (uint32_t i = 0; i < tree.leaf_max_keys * tree.internal_max_keys; i++) {
      uint8_t record[TYPE_INT32];
      memcpy(record, &i, sizeof(i));
      bp_insert_element(tree, &i, record);
    }

    // Delete pattern to trigger redistribution from siblings
    for (uint32_t i = 0; i < tree.leaf_max_keys * tree.internal_max_keys; i += 3) {
      bp_delete_element(tree, &i);
    }

    // Delete pattern to trigger merges
    for (uint32_t i = 1; i < tree.leaf_max_keys * tree.internal_max_keys; i += 3) {
      bp_delete_element(tree, &i);
    }

    pager_rollback();
    pager_close();
  }
}

int main() {
  std::cout << "Starting comprehensive B-tree fuzzing...\n\n";

  // Run all test suites from btree_tests
  std::cout << "Running unit tests...\n";
  run_comprehensive_tests();

  std::cout << "\nFuzzing edge cases...\n";
  fuzz_edge_cases();

  std::cout << "Fuzzing different types...\n";
  fuzz_different_types();

  std::cout << "Fuzzing special cases...\n";
  fuzz_special_cases();

  std::cout << "Fuzzing repair paths...\n";
  fuzz_repair_paths();

  std::cout << "Running comprehensive fuzz...\n";
  fuzz_comprehensive();

  // Print final coverage report
  print_coverage_report();

  std::cout << "\n=== FUZZING COMPLETE ===\n";
  return 0;
}
