// btree_fuzz_test.cpp
#include "btree.hpp"
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
void fuzz() {
  for (auto tree_type : {BPLUS}) {
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
    std::uniform_int_distribution<uint32_t> op_dist(0, 2); // 0=insert, 1=delete, 2=find

    // Track inserted keys for validation and deletion
    std::set<uint32_t> inserted_keys;
    std::vector<uint32_t> keys_vector; // For random deletion selection

    const uint32_t num_operations = tree.leaf_max_keys * 200;
    uint32_t insert_count = 0;
    uint32_t delete_count = 0;
    uint32_t find_count = 0;

    for (uint32_t op = 0; op < num_operations; op++) {
      uint32_t operation = op_dist(gen);

      // Force inserts if we have too few keys, force deletes if we have too many
      if (inserted_keys.size() < 10) {
        operation = 0; // Force insert
      } else if (inserted_keys.size() > tree.leaf_max_keys * 50) {
        operation = 1; // Force delete to prevent unbounded growth
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
              exit(1);
            }
          }
          break;
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
              std::cerr << "Invariant violation after deleting key: " << key_to_delete
                        << " (operation " << op << ")" << std::endl;
              std::cerr << "Total inserts: " << insert_count
                        << ", Total deletes: " << delete_count << std::endl;
              exit(1);
            }
          }
          break;
        }

        case 2: { // Find operation
          uint32_t key = key_dist(gen);
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
            exit(1);
          }
          break;
        }
      }


    }

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
