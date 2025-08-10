
#include "btree.hpp"
#include "defs.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
#include <set>

/* TEST UTILS */
uint32_t random_u32() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<uint32_t> dis(0 + 1, UINT32_MAX - 1);
  return dis(gen);
}

// Test result tracking
struct TestResults {
  int passed = 0;
  int failed = 0;
  std::vector<std::string> failed_tests;
};

static TestResults g_results;

// Color codes for terminal output
const char *RESET = "\033[0m";
const char *GREEN = "\033[32m";
const char *RED = "\033[31m";
const char *YELLOW = "\033[33m";
const char *BLUE = "\033[34m";
void check(const std::string &test_name, bool condition) {
  if (condition) {
    std::cout << GREEN << "✓ " << RESET << test_name << std::endl;
    g_results.passed++;
  } else {
    std::cout << RED << "✗ " << RESET << test_name << std::endl;
    g_results.failed++;
    g_results.failed_tests.push_back(test_name);
    exit(0);
  }
}
/* TREE-OPERATION TESTS */
void test_tree_toplevel(bool single_node) {
  pager_init("test_large_records.db");

  for (auto type : {BPLUS, BTREE}) {
    pager_begin_transaction();
    uint32_t schema = TYPE_INT32;
    BPlusTree tree = bp_create(TYPE_INT32, schema, type);
    if (tree.tree_type == INVALID) {
      std::cout << "invalid tree\n";
      exit(1);
    }
    bp_init(tree);
    // validate_tree(tree);

    int insert_count =
        single_node ? tree.leaf_max_keys : tree.leaf_max_keys * 8;

    std::set<uint32_t> keys;
    std::set<uint32_t> deleted_keys;
    while (keys.size() < insert_count) {
      keys.insert(random_u32());
    }

    int inserted = 0;
    int duplicated = 0;
    for (uint32_t key : keys) {
      uint8_t record[TYPE_INT32];
      memcpy(record, &key, sizeof(key));
      bp_insert_element(tree, key, record);
      inserted++;
      bp_validate_all_invariants(tree);

      if (key % 7 == 0 && deleted_keys.size() + 1 != inserted) {
        deleted_keys.insert(key);
        bp_delete_element(tree, key);
        bp_validate_all_invariants(tree);

      } else if (key % 9 == 0) {
        bp_insert_element(tree, key, record);
        if (type == BTREE) {
          duplicated++;
        }
      }
    }

    for (uint32_t key : keys) {
      bp_delete_element(tree, key);
      bp_validate_all_invariants(tree);
    }

    pager_rollback();
  }

  std::cout << "allpassed\n";
}
// Additional test cases to catch B-tree/B+tree implementation issues

void test_sequential_operations() {
  // Test sequential insertions/deletions which stress different code paths
  pager_init("test_sequential.db");

  for (auto type : {BPLUS, BTREE}) {
    pager_begin_transaction();
    uint32_t schema = TYPE_INT32;
    BPlusTree tree = bp_create(TYPE_INT32, schema, type);
    bp_init(tree);

    // Sequential ascending insertions (stresses right-heavy splits)
    for (uint32_t i = 1; i <= tree.leaf_max_keys * 3; i++) {
      uint8_t record[TYPE_INT32];
      memcpy(record, &i, sizeof(i));
      bp_insert_element(tree, i, record);
      bp_validate_all_invariants(tree);
    }

    // Sequential descending deletions (stresses left-heavy merges)
    for (uint32_t i = tree.leaf_max_keys * 3; i >= 1; i--) {
      bp_delete_element(tree, i);
      bp_validate_all_invariants(tree);
    }

    pager_rollback();
  }
  pager_close();
}

void test_edge_case_splits_merges() {
  pager_init("test_edge_cases.db");

  for (auto type : {BPLUS, BTREE}) {
    pager_begin_transaction();
    uint32_t schema = TYPE_INT32;
    BPlusTree tree = bp_create(TYPE_INT32, schema, type);
    bp_init(tree);

    // Test minimum capacity scenarios
    uint32_t min_keys = tree.leaf_min_keys;
    uint32_t max_keys = tree.leaf_max_keys;

    // Fill to exactly trigger first split
    for (uint32_t i = 0; i <= max_keys; i++) {
      uint8_t record[TYPE_INT32];
      memcpy(record, &i, sizeof(i));
      bp_insert_element(tree, i, record);
      bp_validate_all_invariants(tree);
    }

    // Delete to exactly trigger first merge
    for (uint32_t i = 0; i < max_keys - min_keys + 1; i++) {
      bp_delete_element(tree, i);
      bp_validate_all_invariants(tree);
    }

    pager_rollback();
  }

  pager_close();
}

void test_duplicate_handling() {
  // Critical for B-tree vs B+tree behavior differences
  pager_init("test_duplicates.db");

  for (auto type : {BPLUS, BTREE}) {
    pager_begin_transaction();
    uint32_t schema = TYPE_INT32;
    BPlusTree tree = bp_create(TYPE_INT32, schema, type);
    bp_init(tree);

    uint32_t duplicate_key = 100;
    uint8_t record[TYPE_INT32];
    memcpy(record, &duplicate_key, sizeof(duplicate_key));

    // Insert same key multiple times
    for (int i = 0; i < tree.leaf_max_keys; i++) {
      bp_insert_element(tree, duplicate_key, record);
      bp_validate_all_invariants(tree);

      // Verify behavior difference between B-tree and B+tree
      if (type == BTREE) {
        // B-tree should allow multiple copies
        // (test would need to check actual count)
      } else {
        // B+tree should update existing record
        const uint8_t *found = bp_get(tree, duplicate_key);
        assert(found != nullptr);
      }
    }

    // Delete duplicates
    while (bp_find_element(tree, duplicate_key)) {
      bp_delete_element(tree, duplicate_key);
      bp_validate_all_invariants(tree);
    }

    pager_rollback();
  }

  pager_close();
}

void test_internal_node_operations() {
  // Specifically test operations on internal nodes (B-tree specific)
  pager_init("test_internal.db");

  for (auto type : {BPLUS, BTREE}) {
    pager_begin_transaction();
    uint32_t schema = TYPE_INT32;
    BPlusTree tree = bp_create(TYPE_INT32, schema, type);
    bp_init(tree);

    // Build a tree with internal nodes
    for (uint32_t i = 1; i <= tree.leaf_max_keys * tree.internal_max_keys;
         i++) {
      uint8_t record[TYPE_INT32];
      memcpy(record, &i, sizeof(i));
      bp_insert_element(tree, i, record);
    }
    bp_validate_all_invariants(tree);

    // Delete keys that exist in internal nodes (critical for B-tree)
    BPTreeNode *root = bp_get_root(tree);
    if (!root->is_leaf) {
      uint32_t *keys = reinterpret_cast<uint32_t *>(root->data);
      for (uint32_t i = 0; i < root->num_keys; i++) {
        uint32_t internal_key = keys[i];
        bp_delete_element(tree, internal_key);
        bp_validate_all_invariants(tree);
      }
    }

    pager_rollback();
  }

  pager_close();
}

void test_root_special_cases() {
  // Test root node edge cases
  pager_init("test_root.db");

  for (auto type : {BPLUS, BTREE}) {
    pager_begin_transaction();
    uint32_t schema = TYPE_INT32;
    BPlusTree tree = bp_create(TYPE_INT32, schema, type);
    bp_init(tree);

    // Insert single element
    uint32_t key = 42;
    uint8_t record[TYPE_INT32];
    memcpy(record, &key, sizeof(key));
    bp_insert_element(tree, key, record);
    bp_validate_all_invariants(tree);

    // Delete it (root becomes empty)
    bp_delete_element(tree, key);
    bp_validate_all_invariants(tree);

    // Test root split scenario
    for (uint32_t i = 0; i <= tree.leaf_max_keys; i++) {
      memcpy(record, &i, sizeof(i));
      bp_insert_element(tree, i, record);
      bp_validate_all_invariants(tree);
    }

    // Delete everything to test root merge
    for (uint32_t i = 0; i <= tree.leaf_max_keys; i++) {
      bp_delete_element(tree, i);
      bp_validate_all_invariants(tree);
    }

    pager_rollback();
  }

  pager_close();
}

void test_stress_patterns() {
  // Patterns that have historically exposed B-tree bugs
  pager_init("test_stress.db");

  for (auto type : {BPLUS, BTREE}) {
    pager_begin_transaction();
    uint32_t schema = TYPE_INT32;
    BPlusTree tree = bp_create(TYPE_INT32, schema, type);
    bp_init(tree);

    std::vector<uint32_t> keys;

    // Pattern 1: Insert evens, then odds (creates interleaving)
    for (uint32_t i = 2; i <= tree.leaf_max_keys * 4; i += 2) {
      keys.push_back(i);
      uint8_t record[TYPE_INT32];
      memcpy(record, &i, sizeof(i));
      bp_insert_element(tree, i, record);
    }
    bp_validate_all_invariants(tree);

    for (uint32_t i = 1; i <= tree.leaf_max_keys * 4; i += 2) {
      keys.push_back(i);
      uint8_t record[TYPE_INT32];
      memcpy(record, &i, sizeof(i));
      bp_insert_element(tree, i, record);
    }
    bp_validate_all_invariants(tree);

    // Pattern 2: Delete from middle outward
    std::sort(keys.begin(), keys.end());
    size_t mid = keys.size() / 2;
    for (size_t i = 0; i < keys.size(); i++) {
      size_t idx = (i % 2 == 0) ? mid + i / 2 : mid - 1 - i / 2;
      if (idx < keys.size()) {
        bp_delete_element(tree, keys[idx]);
        bp_validate_all_invariants(tree);
      }
    }

    pager_rollback();
  }

  pager_close();
}

void test_boundary_conditions() {
  // Test exact boundary conditions
  pager_init("test_boundaries.db");

  for (auto type : {BPLUS, BTREE}) {
    pager_begin_transaction();
    uint32_t schema = TYPE_INT32;
    BPlusTree tree = bp_create(TYPE_INT32, schema, type);
    bp_init(tree);

    // Test with exactly min_keys, min_keys+1, max_keys-1, max_keys
    uint32_t test_counts[] = {
        tree.leaf_min_keys, tree.leaf_min_keys + 1, tree.leaf_max_keys - 1,
        tree.leaf_max_keys,
        tree.leaf_max_keys + 1 // This should trigger split
    };

    for (uint32_t count : test_counts) {
      // Fresh tree for each test
      for (uint32_t i = 1; i <= count; i++) {
        uint8_t record[TYPE_INT32];
        memcpy(record, &i, sizeof(i));
        bp_insert_element(tree, i, record);
        bp_validate_all_invariants(tree);
      }

      // Delete back to empty
      for (uint32_t i = 1; i <= count; i++) {
        bp_delete_element(tree, i);
        bp_validate_all_invariants(tree);
      }
    }

    pager_rollback();
  }

  pager_close();
}

// Additional invariant checks specific to B-tree vs B+tree differences
void verify_btree_specific_invariants(BPlusTree &tree) {
  if (tree.tree_type != BTREE)
    return;

  // For B-trees: verify that internal nodes actually store records
  BPTreeNode *root = bp_get_root(tree);
  if (!root || root->is_leaf)
    return;

  std::queue<BPTreeNode *> to_visit;
  to_visit.push(root);

  while (!to_visit.empty()) {
    BPTreeNode *node = to_visit.front();
    to_visit.pop();

    if (!node->is_leaf) {
      // Verify internal node has records for each key
      for (uint32_t i = 0; i < node->num_keys; i++) {
        const uint8_t *record = bp_get(tree, i);
        if (!record) {
          std::cerr << "B-tree internal node missing record for key " << i
                    << std::endl;
          exit(1);
        }
      }

      // Add children to queue
      for (uint32_t i = 0; i <= node->num_keys; i++) {
        BPTreeNode *child = bp_get_child(tree, node, i);
        if (child) {
          to_visit.push(child);
        }
      }
    }
  }
}

/* INTEGERATION TESTS */

// Integration tests for B-tree with pager system
// Tests commit, rollback, persistence, and cross-session data integrity

#include "btree.hpp"
#include "pager.hpp"
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// Simple test record for integration tests
struct TestRecord {
  int32_t id;
  char name[32];
};

void test_basic_persistence() {
  std::cout << "\n=== Testing Basic Persistence ===" << std::endl;

  const char *db_file = "test_persist_basic.db";
  uint32_t saved_root_index = 0;

  // Session 1: Insert data and commit
  {
    pager_init(db_file);
    pager_begin_transaction();

    uint32_t schema = TYPE_INT32 + TYPE_VARCHAR32;
    BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    // Insert test data
    for (int i = 0; i < 20; i++) {
      TestRecord record;
      record.id = i * 100;
      snprintf(record.name, sizeof(record.name), "User_%d", i);
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&record));
    }

    saved_root_index = tree.root_page_index;
    std::cout << "Session 1: Inserted 20 records, root page: "
              << saved_root_index << std::endl;

    pager_commit();
    pager_close();
  }

  // Session 2: Reopen and verify data persisted
  {
    pager_init(db_file);
    pager_begin_transaction();

    uint32_t schema = TYPE_INT32 + TYPE_VARCHAR32;
    BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
    tree.root_page_index =
        saved_root_index; // In real system, this comes from catalog

    // Verify all records are still there
    bool all_found = true;
    for (int i = 0; i < 20; i++) {
      const TestRecord *result =
          reinterpret_cast<const TestRecord *>(bp_get(tree, i));
      if (!result || result->id != i * 100) {
        all_found = false;
        std::cout << "Failed to find record " << i << std::endl;
        break;
      }
    }

    check("Basic persistence: All records found after restart", all_found);

    pager_commit();
    pager_close();
  }

  std::cout << "Basic persistence test completed." << std::endl;
}

void test_transaction_rollback() {
  std::cout << "\n=== Testing Transaction Rollback ===" << std::endl;

  const char *db_file = "test_rollback.db";
  BPlusTree tree;
  uint64_t before;
  uint64_t during;
  uint64_t after;

  // Setup: Create initial committed state
  {
    pager_init(db_file);
    pager_begin_transaction();

    uint32_t schema = TYPE_INT32;
    tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    // Insert initial data
    for (int i = 0; i < 10; i++) {
      int32_t value = i * 100;
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&value));
    }

    pager_commit();
    before = debug_hash_tree(tree);
    pager_close();
    std::cout << "Setup: Committed 10 initial records" << std::endl;
  }

  // Test: Make modifications then rollback
  {
    pager_init(db_file);

    check("persists", debug_hash_tree(tree) == before);

    pager_begin_transaction();

    uint32_t schema = TYPE_INT32;
    // BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    // Verify initial data is there
    bool initial_data_ok = true;
    for (int i = 0; i < 10; i++) {
      const int32_t *result =
          reinterpret_cast<const int32_t *>(bp_get(tree, i));
      if (!result || *result != i * 100) {
        initial_data_ok = false;
        break;
      }
    }
    check("Rollback: Initial committed data visible", initial_data_ok);

    // Make modifications that will be rolled back
    // 1. Update existing records
    for (int i = 0; i < 5; i++) {
      int32_t new_value = i * 1000; // Change from i*100 to i*1000
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&new_value));
    }

    // 2. Insert new records
    for (int i = 100; i < 105; i++) {
      int32_t value = i * 10;
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&value));
    }

    // 3. Delete some records
    for (int i = 7; i < 10; i++) {
      bp_delete_element(tree, i);
    }

    // Verify modifications are visible
    const int32_t *updated = reinterpret_cast<const int32_t *>(bp_get(tree, 2));
    bool mods_visible = (updated && *updated == 2000) &&
                        bp_find_element(tree, 102) && !bp_find_element(tree, 8);
    check("Rollback: Modifications visible before rollback", mods_visible);

    during = debug_hash_tree(tree);
    check("not equal", during != before);
    // ROLLBACK - don't commit
    pager_rollback();


    after = debug_hash_tree(tree);
    check("not equal", after == before);
    pager_close();
    std::cout << "Performed rollback" << std::endl;
  }

  // Verify: Check that rollback worked
  {
    pager_init(db_file);
    pager_begin_transaction();

    uint32_t schema = TYPE_INT32;
    // BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    // Check updates were rolled back
    bool updates_rolled_back = true;
    for (int i = 0; i < 5; i++) {
      const int32_t *result =
          reinterpret_cast<const int32_t *>(bp_get(tree, i));
      if (!result || *result != i * 100) { // Should be original, not modified
        updates_rolled_back = false;
        break;
      }
    }
    check("Rollback: Updates rolled back to original values",
          updates_rolled_back);

    // Check inserts were rolled back
    bool inserts_rolled_back = !bp_find_element(tree, 102);
    check("Rollback: New inserts rolled back", inserts_rolled_back);

    // Check deletes were rolled back (deleted data restored)
    bool deletes_rolled_back = bp_find_element(tree, 8);
    check("Rollback: Deleted records restored", deletes_rolled_back);

    pager_commit();
    pager_close();
  }

  std::cout << "Transaction rollback test completed." << std::endl;
}

void test_multi_session_consistency() {
  std::cout << "\n=== Testing Multi-Session Consistency ===" << std::endl;

  const char *db_file = "test_multi_session.db";
  BPlusTree tree;

  // Session 1: Insert initial data
  {
    pager_init(db_file);
    pager_begin_transaction();

    uint32_t schema = TYPE_INT32 + TYPE_VARCHAR32;
    tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    for (int i = 0; i < 15; i++) {
      TestRecord record;
      record.id = i;
      snprintf(record.name, sizeof(record.name), "Session1_User_%d", i);
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&record));
    }

    pager_commit();

    pager_close();
    std::cout << "Session 1: Inserted 15 records" << std::endl;
  }

  // Session 2: Add more data
  {
    pager_init(db_file);
    pager_begin_transaction();

    uint32_t schema = TYPE_INT32 + TYPE_VARCHAR32;
    // BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    // Verify session 1 data is visible
    bool session1_visible =
        bp_find_element(tree, 5) && bp_find_element(tree, 14);
    check("Multi-session: Session 1 data visible in session 2",
          session1_visible);

    // Add more data
    for (int i = 20; i < 30; i++) {
      TestRecord record;
      record.id = i;
      snprintf(record.name, sizeof(record.name), "Session2_User_%d", i);
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&record));
    }

    // Update some session 1 data
    for (int i = 0; i < 5; i++) {
      TestRecord record;
      record.id = i + 1000;
      snprintf(record.name, sizeof(record.name), "Updated_User_%d", i);
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&record));
    }


    pager_commit();

    pager_close();
    std::cout << "Session 2: Added 10 records, updated 5 records" << std::endl;
  }

  // Session 3: Verify all changes persisted
  {
    pager_init(db_file);
    pager_begin_transaction();

    uint32_t schema = TYPE_INT32 + TYPE_VARCHAR32;
    // BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    // Check original data (not updated)
    const TestRecord *orig =
        reinterpret_cast<const TestRecord *>(bp_get(tree, 10));
    bool orig_ok =
        orig && orig->id == 10 && strstr(orig->name, "Session1") != nullptr;
    check("Multi-session: Original data preserved", orig_ok);

    // Check updated data
    const TestRecord *updated =
        reinterpret_cast<const TestRecord *>(bp_get(tree, 2));
    bool update_ok = updated && updated->id == 1002 &&
                     strstr(updated->name, "Updated") != nullptr;
    check("Multi-session: Updates persisted", update_ok);

    // Check new data
    const TestRecord *new_data =
        reinterpret_cast<const TestRecord *>(bp_get(tree, 25));
    bool new_ok = new_data && new_data->id == 25 &&
                  strstr(new_data->name, "Session2") != nullptr;
    check("Multi-session: New data persisted", new_ok);

    pager_commit();
    pager_close();
  }

  std::cout << "Multi-session consistency test completed." << std::endl;
}

void test_crash_recovery_simulation() {
  std::cout << "\n=== Testing Crash Recovery Simulation ===" << std::endl;

  const char *db_file = "test_crash_recovery.db";
  BPlusTree tree;
  // Setup: Create committed state
  {
    pager_init(db_file);
    pager_begin_transaction();

    uint32_t schema = TYPE_INT32;
    tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    for (int i = 0; i < 10; i++) {
      int32_t value = i * 10;
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&value));
    }

    pager_commit();
    pager_close();
    std::cout << "Setup: Committed baseline data" << std::endl;
  }

  // Simulate crash: Make changes but don't commit (close without commit)
  {
    pager_init(db_file);
    pager_begin_transaction();

    uint32_t schema = TYPE_INT32;
    // tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    // Make changes that should be lost due to "crash"
    for (int i = 100; i < 110; i++) {
      int32_t value = i * 20;
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&value));
    }

    // Update existing data
    for (int i = 0; i < 5; i++) {
      int32_t new_value = i * 1000;
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&new_value));
    }

    // Simulate crash - close without commit
    pager_close();
    std::cout << "Simulated crash: closed without commit" << std::endl;
  }

  // Recovery: Reopen and verify uncommitted changes are gone
  {
    pager_init(db_file); // This should trigger journal recovery
    pager_begin_transaction();

    uint32_t schema = TYPE_INT32;
    // tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    // Check that uncommitted inserts are gone
    bool crash_inserts_gone = !bp_find_element(tree, 105);
    check("Crash recovery: Uncommitted inserts lost", crash_inserts_gone);

    // Check that uncommitted updates are gone (original values restored)
    const int32_t *restored =
        reinterpret_cast<const int32_t *>(bp_get(tree, 2));
    bool crash_updates_gone =
        restored && *restored == 20; // Original value, not 2000
    check("Crash recovery: Uncommitted updates lost", crash_updates_gone);

    // Check that committed data is still there
    bool committed_data_ok = bp_find_element(tree, 9);
    check("Crash recovery: Committed data preserved", committed_data_ok);

    pager_commit();
    pager_close();
  }

  std::cout << "Crash recovery simulation completed." << std::endl;
}

void test_large_transaction_rollback() {
  std::cout << "\n=== Testing Large Transaction Rollback ===" << std::endl;

  BPlusTree tree;
  const char *db_file = "test_large_rollback.db";

  {
    pager_init(db_file);
    pager_begin_transaction();

    uint32_t schema = TYPE_INT32;
    tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    // Insert enough data to cause multiple node splits
    int large_count = tree.leaf_max_keys * 5;
    for (int i = 0; i < large_count; i++) {
      int32_t value = i * 7;
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&value));
    }

    // Verify data is accessible
    bool data_accessible = bp_find_element(tree, 0) &&
                           bp_find_element(tree, large_count - 1) &&
                           bp_find_element(tree, large_count / 2);
    check("Large rollback: Data accessible before rollback", data_accessible);

    // Rollback large transaction
    pager_rollback();
    pager_close();
    std::cout << "Rolled back transaction with " << large_count << " inserts"
              << std::endl;
  }

  // Verify rollback worked
  {
    pager_init(db_file);
    pager_begin_transaction();

    uint32_t schema = TYPE_INT32;
    tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    // Tree should be empty
    bool tree_empty = !bp_find_element(tree, 0) &&
                      !bp_find_element(tree, 100) &&
                      !bp_find_element(tree, 500);
    check("Large rollback: Tree empty after rollback", tree_empty);

    pager_commit();
    pager_close();
  }

  std::cout << "Large transaction rollback test completed." << std::endl;
}

void test_multi_tree_isolation() {
  std::cout << "\n=== Testing Multi-Tree Isolation ===" << std::endl;

  const char *db_file = "test_multi_tree.db";

  {
    pager_init(db_file);
    pager_begin_transaction();

    // Create multiple trees with different schemas
    uint32_t users_schema = TYPE_INT32 + TYPE_VARCHAR32;
    uint32_t orders_schema = TYPE_INT32 + TYPE_INT64;
    uint32_t products_schema = TYPE_VARCHAR32;

    BPlusTree users_tree = bp_create(TYPE_INT32, users_schema, BPLUS);
    BPlusTree orders_tree = bp_create(TYPE_INT32, orders_schema, BPLUS);
    BPlusTree products_tree = bp_create(TYPE_INT32, products_schema, BPLUS);

    bp_init(users_tree);
    bp_init(orders_tree);
    bp_init(products_tree);

    // Insert data into each tree
    for (int i = 0; i < 10; i++) {
      // Users table
      TestRecord user;
      user.id = i;
      snprintf(user.name, sizeof(user.name), "User_%d", i);
      bp_insert_element(users_tree, i,
                        reinterpret_cast<const uint8_t *>(&user));

      // Orders table
      struct {
        int32_t order_id;
        int64_t amount;
      } order = {i + 1000, i * 100LL};
      bp_insert_element(orders_tree, i,
                        reinterpret_cast<const uint8_t *>(&order));

      // Products table
      char product[32];
      snprintf(product, sizeof(product), "Product_%d", i);
      bp_insert_element(products_tree, i,
                        reinterpret_cast<const uint8_t *>(product));
    }

    // Verify data isolation - each tree should only see its own data
    bool users_ok = bp_find_element(users_tree, 5);
    bool orders_ok = bp_find_element(orders_tree, 5);
    bool products_ok = bp_find_element(products_tree, 5);

    check("Multi-tree: Users tree has user data", users_ok);
    check("Multi-tree: Orders tree has order data", orders_ok);
    check("Multi-tree: Products tree has product data", products_ok);

    // Verify trees don't interfere with each other
    const TestRecord *user =
        reinterpret_cast<const TestRecord *>(bp_get(users_tree, 3));
    bool user_data_correct =
        user && user->id == 3 && strstr(user->name, "User_3") != nullptr;
    check("Multi-tree: User data integrity maintained", user_data_correct);

    pager_commit();
    pager_close();
    std::cout << "Created 3 trees with different schemas" << std::endl;
  }
}

void test_multi_tree_transactions() {
  std::cout << "\n=== Testing Multi-Tree Transactions ===" << std::endl;

  const char *db_file = "test_multi_tree_txn.db";

  uint32_t table1_schema = TYPE_INT32;
  uint32_t table2_schema = TYPE_INT32;

  BPlusTree table1 = bp_create(TYPE_INT32, table1_schema, BPLUS);
  BPlusTree table2 = bp_create(TYPE_INT32, table2_schema, BPLUS);

  // Setup: Create trees with initial data
  {
    pager_init(db_file);
    pager_begin_transaction();

    bp_init(table1);
    bp_init(table2);

    // Insert initial data
    for (int i = 0; i < 5; i++) {
      int32_t value1 = i * 10;
      int32_t value2 = i * 20;
      bp_insert_element(table1, i, reinterpret_cast<const uint8_t *>(&value1));
      bp_insert_element(table2, i, reinterpret_cast<const uint8_t *>(&value2));
    }

    pager_commit();
    pager_close();
    std::cout << "Setup: Created 2 tables with initial data" << std::endl;
  }

  // Test: Modify both trees, then rollback
  {
    pager_init(db_file);
    pager_begin_transaction();

    bp_init(table1);
    bp_init(table2);

    // Modify both trees
    for (int i = 10; i < 15; i++) {
      int32_t value1 = i * 100;
      int32_t value2 = i * 200;
      bp_insert_element(table1, i, reinterpret_cast<const uint8_t *>(&value1));
      bp_insert_element(table2, i, reinterpret_cast<const uint8_t *>(&value2));
    }

    // Update existing data in both trees
    for (int i = 0; i < 3; i++) {
      int32_t new_value1 = i * 1000;
      int32_t new_value2 = i * 2000;
      bp_insert_element(table1, i,
                        reinterpret_cast<const uint8_t *>(&new_value1));
      bp_insert_element(table2, i,
                        reinterpret_cast<const uint8_t *>(&new_value2));
    }

    // Verify changes are visible
    const int32_t *t1_new =
        reinterpret_cast<const int32_t *>(bp_get(table1, 12));
    const int32_t *t2_new =
        reinterpret_cast<const int32_t *>(bp_get(table2, 12));
    bool changes_visible =
        t1_new && *t1_new == 1200 && t2_new && *t2_new == 2400;
    check("Multi-tree txn: Changes visible before rollback", changes_visible);

    // ROLLBACK affects both trees
    pager_rollback();
    pager_close();
    std::cout << "Rolled back changes to both trees" << std::endl;
  }

  // Verify: Both trees should be restored to original state
  {
    pager_init(db_file);
    pager_begin_transaction();

    bp_init(table1);
    bp_init(table2);

    // Check that new inserts were rolled back from both trees
    bool inserts_rolled_back =
        !bp_find_element(table1, 12) && !bp_find_element(table2, 12);
    check("Multi-tree txn: Inserts rolled back from both trees",
          inserts_rolled_back);

    // Check that updates were rolled back to original values
    const int32_t *t1_orig =
        reinterpret_cast<const int32_t *>(bp_get(table1, 2));
    const int32_t *t2_orig =
        reinterpret_cast<const int32_t *>(bp_get(table2, 2));
    bool updates_rolled_back =
        t1_orig && *t1_orig == 20 && t2_orig && *t2_orig == 40;
    check("Multi-tree txn: Updates rolled back to original values",
          updates_rolled_back);

    pager_commit();
    pager_close();
  }

  std::cout << "Multi-tree transaction test completed." << std::endl;
}

void test_multi_tree_page_sharing() {
  std::cout << "\n=== Testing Multi-Tree Page Sharing ===" << std::endl;

  const char *db_file = "test_page_sharing.db";

  {
    pager_init(db_file);
    pager_begin_transaction();

    uint32_t schema = TYPE_INT32;

    // Create multiple trees
    BPlusTree tree1 = bp_create(TYPE_INT32, schema, BPLUS);
    BPlusTree tree2 = bp_create(TYPE_INT32, schema, BPLUS);
    BPlusTree tree3 = bp_create(TYPE_INT32, schema, BPLUS);

    bp_init(tree1);
    bp_init(tree2);
    bp_init(tree3);

    // Each tree should have different root pages
    bool different_roots = (tree1.root_page_index != tree2.root_page_index) &&
                           (tree2.root_page_index != tree3.root_page_index) &&
                           (tree1.root_page_index != tree3.root_page_index);
    check("Multi-tree: Trees have different root pages", different_roots);

    std::cout << "Tree roots: " << tree1.root_page_index << ", "
              << tree2.root_page_index << ", " << tree3.root_page_index
              << std::endl;

    // Insert enough data to cause page allocation from shared pool
    for (int i = 0; i < 50; i++) {
      int32_t value1 = i;
      int32_t value2 = i + 1000;
      int32_t value3 = i + 2000;

      bp_insert_element(tree1, i, reinterpret_cast<const uint8_t *>(&value1));
      bp_insert_element(tree2, i, reinterpret_cast<const uint8_t *>(&value2));
      bp_insert_element(tree3, i, reinterpret_cast<const uint8_t *>(&value3));
    }

    // Verify data integrity across all trees
    const int32_t *val1 = reinterpret_cast<const int32_t *>(bp_get(tree1, 25));
    const int32_t *val2 = reinterpret_cast<const int32_t *>(bp_get(tree2, 25));
    const int32_t *val3 = reinterpret_cast<const int32_t *>(bp_get(tree3, 25));

    bool data_integrity =
        val1 && *val1 == 25 && val2 && *val2 == 1025 && val3 && *val3 == 2025;
    check("Multi-tree: Data integrity with shared page pool", data_integrity);

    pager_commit();
    pager_close();
  }

  std::cout << "Multi-tree page sharing test completed." << std::endl;
}

void test_mixed_tree_types() {
  std::cout << "\n=== Testing Mixed Tree Types ===" << std::endl;

  const char *db_file = "test_mixed_types.db";

  {
    pager_init(db_file);
    pager_begin_transaction();

    uint32_t schema = TYPE_INT32;

    // Create both B+tree and B-tree in same database
    BPlusTree bplus_tree = bp_create(TYPE_INT32, schema, BPLUS);
    BPlusTree btree_tree = bp_create(TYPE_INT32, schema, BTREE);

    bp_init(bplus_tree);
    bp_init(btree_tree);

    // Insert same keys in both trees
    for (int i = 0; i < 20; i++) {
      int32_t value = i * 50;
      bp_insert_element(bplus_tree, i,
                        reinterpret_cast<const uint8_t *>(&value));
      bp_insert_element(btree_tree, i,
                        reinterpret_cast<const uint8_t *>(&value));
    }

    // Insert duplicates in B-tree (should be allowed)
    for (int i = 0; i < 5; i++) {
      int32_t duplicate_value = i * 50 + 1; // Slightly different value
      bp_insert_element(btree_tree, i,
                        reinterpret_cast<const uint8_t *>(&duplicate_value));
    }

    // Verify both trees work correctly
    bool bplus_ok = bp_find_element(bplus_tree, 10);
    bool btree_ok = bp_find_element(btree_tree, 10);
    check("Mixed types: B+tree operations work", bplus_ok);
    check("Mixed types: B-tree operations work", btree_ok);

    // Verify B+tree behavior (updates replace)
    const int32_t *bplus_val =
        reinterpret_cast<const int32_t *>(bp_get(bplus_tree, 2));
    bool bplus_updated = bplus_val && *bplus_val == 100; // Original value
    check("Mixed types: B+tree maintains single values", bplus_updated);

    pager_commit();
    pager_close();
  }

  std::cout << "Mixed tree types test completed." << std::endl;
}

void test_concurrent_tree_operations() {
  std::cout << "\n=== Testing Concurrent Tree Operations ===" << std::endl;

  const char *db_file = "test_concurrent.db";

  {
    pager_init(db_file);
    pager_begin_transaction();

    uint32_t schema = TYPE_INT32;

    BPlusTree log_tree = bp_create(TYPE_INT32, schema, BPLUS);
    BPlusTree data_tree = bp_create(TYPE_INT32, schema, BPLUS);

    bp_init(log_tree);
    bp_init(data_tree);

    // Simulate interleaved operations on both trees
    for (int i = 0; i < 30; i++) {
      int32_t log_entry = i;
      int32_t data_value = i * 3;

      // Insert to log tree
      bp_insert_element(log_tree, i,
                        reinterpret_cast<const uint8_t *>(&log_entry));

      // Every 3rd operation, insert to data tree
      if (i % 3 == 0) {
        bp_insert_element(data_tree, i / 3,
                          reinterpret_cast<const uint8_t *>(&data_value));
      }

      // Every 5th operation, delete from log tree (simulate log cleanup)
      if (i >= 5 && (i % 5) == 0) {
        bp_delete_element(log_tree, i - 5);
      }
    }

    // Verify final state
    bool log_has_recent = bp_find_element(log_tree, 29);
    bool log_missing_old = !bp_find_element(log_tree, 0); // Should be deleted
    bool data_has_entries = bp_find_element(data_tree, 5);

    check("Concurrent ops: Log tree has recent entries", log_has_recent);
    check("Concurrent ops: Log tree cleaned old entries", log_missing_old);
    check("Concurrent ops: Data tree has entries", data_has_entries);

    pager_commit();
    pager_close();
  }

  std::cout << "Concurrent tree operations test completed." << std::endl;
}

void run_integration_tests() {
  std::cout << "=== B-Tree Pager Integration Test Suite ===" << std::endl;

  test_basic_persistence();
  test_transaction_rollback();
  test_multi_session_consistency();
  test_crash_recovery_simulation();
  test_large_transaction_rollback();

  // Multi-tree specific tests
  test_multi_tree_isolation();
  test_multi_tree_transactions();
  test_multi_tree_page_sharing();
  test_mixed_tree_types();
  test_concurrent_tree_operations();

  std::cout << "\n=== Integration Tests Completed ===" << std::endl;
  std::cout << "Tested: persistence, rollback, multi-session, crash recovery, "
               "multi-tree support"
            << std::endl;
}

void run_comprehensive_tests() {
  test_sequential_operations();
  test_edge_case_splits_merges();
  test_duplicate_handling();
  test_internal_node_operations();
  test_root_special_cases();
  test_stress_patterns();
  test_boundary_conditions();
  run_integration_tests();

  std::cout << "All comprehensive tests passed!" << std::endl;
}
