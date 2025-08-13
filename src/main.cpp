// btree_fuzz_test.cpp
#include "btree.hpp"
#include "btree_debug.hpp"
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

void try_insert() {

}

void fuzz() {
  for (auto tree_type : {BPLUS,BTREE}) {
    std::cout << "\nTesting " << (tree_type == BTREE ? "B-tree" : "B+tree")
              << " variant...\n";

    // Initialize
    const char *db_file =
        tree_type == BTREE ? "fuzz_btree.db" : "fuzz_bplus.db";
    pager_init(db_file);
    uint32_t schema = TYPE_VARCHAR32;
    BPlusTree tree = bt_create(TYPE_INT32, schema, tree_type);
    bp_init(tree);
    pager_begin_transaction();

    // Random number generation
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> key_dist(0, 100000);
    std::uniform_real_distribution<double> prob_dist(0.0, 1.0);

    // Track inserted keys for validation and deletion
    std::set<uint32_t> inserted_keys;
    std::vector<uint32_t> keys_vector; // For random deletion selection

    // Phase control parameters
    const uint32_t target_size = tree.leaf_max_keys * 100;  // Target size for build-up
    const uint32_t max_size = tree.leaf_max_keys * 200;     // Maximum tree size
    const uint32_t num_operations = tree.leaf_max_keys * 1000;

    uint32_t insert_count = 0;
    uint32_t delete_count = 0;
    uint32_t find_count = 0;

    for (uint32_t op = 0; op < num_operations; op++) {
      // Determine operation probabilities based on current tree size
      double insert_prob, delete_prob, find_prob;
      size_t current_size = inserted_keys.size();

      if (current_size < target_size) {
        // Build-up phase: heavy insertion
        insert_prob = 0.70;
        delete_prob = 0.10;
        find_prob = 0.20;
      } else if (current_size >= target_size && current_size < max_size) {
        // Steady state: balanced operations with some churn
        insert_prob = 0.35;
        delete_prob = 0.35;
        find_prob = 0.30;
      } else {
        // Over max size: favor deletion to bring size down
        insert_prob = 0.10;
        delete_prob = 0.60;
        find_prob = 0.30;
      }

      // Tear-down phase: when we're past 80% of operations, start withering
      if (op > num_operations * 0.8) {
        insert_prob = 0.05;
        delete_prob = 0.75;
        find_prob = 0.20;
      }

      // Final phase: ensure complete teardown in last 5% of operations
      if (op > num_operations * 0.95 && !inserted_keys.empty()) {
        insert_prob = 0.0;
        delete_prob = 0.90;
        find_prob = 0.10;
      }

      // Select operation based on probabilities
      double rand_val = prob_dist(gen);
      uint32_t operation;
      if (rand_val < insert_prob) {
        operation = 0; // Insert
      } else if (rand_val < insert_prob + delete_prob) {
        operation = 1; // Delete
      } else {
        operation = 2; // Find
      }

      // Force delete if we have no keys and selected delete
      if (inserted_keys.empty() && operation == 1) {
        operation = 0; // Switch to insert
      }

      switch (operation) {
        case 0: { // Insert operation
          uint32_t key = key_dist(gen);
          if (inserted_keys.find(key) == inserted_keys.end()) {
            uint8_t record[TYPE_VARCHAR32] = {0};
            memcpy(record, &key, sizeof(key));

            bp_insert_element(tree, &key, record);
            inserted_keys.insert(key);
            keys_vector.push_back(key);
            insert_count++;

            if (!bp_validate_all_invariants(tree)) {
              std::cerr << "Invariant violation after inserting key: " << key
                        << " (operation " << op << ")" << std::endl;
              std::cerr << "Total inserts: " << insert_count
                        << ", Total deletes: " << delete_count << std::endl;
              std::cerr << "Current tree size: " << inserted_keys.size() << std::endl;
              print_tree(tree);
              exit(1);
            }
          }
          continue;;
        }

        case 1: { // Delete operation

          if (!keys_vector.empty()) {
            std::uniform_int_distribution<size_t> idx_dist(0, keys_vector.size() - 1);
            size_t idx = idx_dist(gen);
            uint32_t key_to_delete = keys_vector[idx];

            bp_delete_element(tree, &key_to_delete);
            inserted_keys.erase(key_to_delete);
            keys_vector.erase(keys_vector.begin() + idx);
            delete_count++;

            if (!bp_validate_all_invariants(tree)) {
              print_tree(tree);
              std::cerr << "Invariant violation after deleting key: " << key_to_delete
                        << " (operation " << op << ")" << std::endl;
              std::cerr << "Total inserts: " << insert_count
                        << ", Total deletes: " << delete_count << std::endl;
              std::cerr << "Current tree size: " << inserted_keys.size() << std::endl;
              exit(1);
            }
          }
          continue;;
        }

        case 2: { // Find operation

          uint32_t key;
          // Mix of finding existing and non-existing keys
          if (!inserted_keys.empty() && prob_dist(gen) < 0.7) {
            // 70% chance to search for an existing key
            std::uniform_int_distribution<size_t> idx_dist(0, keys_vector.size() - 1);
            key = keys_vector[idx_dist(gen)];
          } else {
            // 30% chance to search for a random key
            key = key_dist(gen);
          }

          bool found = bp_find_element(tree, &key);
          bool should_exist = (inserted_keys.find(key) != inserted_keys.end());
          find_count++;

          if (found != should_exist) {
            std::cerr << "Find operation failed for key " << key
                      << " (operation " << op << ")" << std::endl;
            std::cerr << "Found: " << found << ", Should exist: " << should_exist << std::endl;
            std::cerr << "Total operations - Inserts: " << insert_count
                      << ", Deletes: " << delete_count
                      << ", Finds: " << find_count << std::endl;
            std::cerr << "Current tree size: " << inserted_keys.size() << std::endl;
            exit(1);
          }
          continue;;
        }
      }

      // Progress reporting every 10% of operations
      if (op % (num_operations / 10) == 0 && op > 0) {
        std::cout << "Progress: " << (op * 100 / num_operations) << "% "
                  << "- Tree size: " << inserted_keys.size()
                  << " (I:" << insert_count << " D:" << delete_count
                  << " F:" << find_count << ")\n";
      }
    }

    // Final teardown: delete all remaining keys
    std::cout << "Final teardown phase - removing " << keys_vector.size() << " keys\n";
    while (!keys_vector.empty()) {
      uint32_t key_to_delete = keys_vector.back();
      bp_delete_element(tree, &key_to_delete);
      inserted_keys.erase(key_to_delete);
      keys_vector.pop_back();
      delete_count++;

      if (!bp_validate_all_invariants(tree)) {
        print_tree(tree);
        std::cerr << "Invariant violation during final teardown, key: " << key_to_delete << std::endl;
        std::cerr << "Remaining keys: " << keys_vector.size() << std::endl;
        exit(1);
      }
    }

    // Verify tree is empty
    if (!inserted_keys.empty()) {
      std::cerr << "Error: Tree should be empty but still has "
                << inserted_keys.size() << " keys\n";
      exit(1);
    }

    std::cout << "Test completed - Total operations: "
              << "Inserts: " << insert_count
              << ", Deletes: " << delete_count
              << ", Finds: " << find_count << "\n";

    bp_validate_all_invariants(tree);

    pager_rollback();
    pager_close();
  }
}

// Entry point
int main() {
  fuzz();
  std::cout << "\n=== ALL TESTS PASSED ===\n";
  return 0;
}
