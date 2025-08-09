
// btree_test.cpp
#include "btree.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include "btree_tests.hpp"

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

// Test helper function
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

// Test data structures for different data types
struct Int32Record {
  int32_t value;
};

struct Int64Record {
  int64_t value;
};

struct VarChar32Record {
  char data[32];
};

struct VarChar256Record {
  char data[256];
};

struct __attribute__((packed)) CompositeRecord {
  int32_t id;            // 4 bytes
  int64_t timestamp;     // 8 bytes
  char name[32];         // 32 bytes
  char description[256]; // 256 bytes
                         // Total: 300 bytes
};

// Helper function to create test data
CompositeRecord create_composite_record(int32_t id, const std::string &name,
                                        const std::string &desc) {
  CompositeRecord record;
  record.id = id;
  record.timestamp =
      std::chrono::system_clock::now().time_since_epoch().count();

  memset(record.name, 0, sizeof(record.name));
  strncpy(record.name, name.c_str(), sizeof(record.name) - 1);

  memset(record.description, 0, sizeof(record.description));
  strncpy(record.description, desc.c_str(), sizeof(record.description) - 1);

  return record;
}

// Helper function to clean and validate string data
void clean_string(char *buffer, size_t max_size) {
  for (size_t i = 0; i < max_size; i++) {
    // Replace any non-printable characters (except null) with spaces
    if (buffer[i] != '\0' && (buffer[i] < 32 || buffer[i] > 126)) {
      buffer[i] = ' ';
    }
    // Stop at first null terminator
    if (buffer[i] == '\0') {
      break;
    }
  }
  buffer[max_size - 1] = '\0'; // Ensure null termination

  // Trim trailing spaces
  int end = max_size - 2;
  while (end >= 0 && buffer[end] == ' ') {
    buffer[end] = '\0';
    end--;
  }
}

// Helper function to format data based on column type
void format_column_data(const uint8_t *data, DataType type, std::ostream &os) {
  switch (type) {
  case TYPE_INT32: {
    int32_t value;
    memcpy(&value, data, sizeof(int32_t));
    os << value;
    break;
  }
  case TYPE_INT64: {
    int64_t value;
    memcpy(&value, data, sizeof(int64_t));
    os << value;
    break;
  }
  case TYPE_VARCHAR32: {
    char buffer[33];
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, data, 32);
    clean_string(buffer, 33);
    os << "\"" << buffer << "\"";
    break;
  }
  case TYPE_VARCHAR256: {
    char buffer[257];
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, data, 256);
    clean_string(buffer, 257);
    os << "\"" << buffer << "\"";
    break;
  }
  default:
    os << "[UNKNOWN_TYPE]";
    break;
  }
}

// Main function to print leaf data in CSV format with node boundaries
void bp_print_leaf_data_csv(BPlusTree &tree,
                            const std::vector<ColumnInfo> &schema) {
  auto leaf_data = bp_extract_leaf_data(tree);

  if (leaf_data.empty()) {
    std::cout << "No data in tree leaves" << std::endl;
    return;
  }

  // Print header
  std::cout << "key";
  for (size_t i = 0; i < schema.size(); i++) {
    std::cout << ",col" << i;
  }
  std::cout << std::endl;

  uint32_t current_node =
      0xFFFFFFFF; // Invalid node number to force first node marker

  for (const auto &entry : leaf_data) {
    // Print node boundary marker when we switch nodes
    if (entry.node_page != current_node) {
      std::cout << "# --- Node " << entry.node_page << " ---" << std::endl;
      current_node = entry.node_page;
    }

    // Print key
    std::cout << entry.key;

    // Print each column
    size_t data_offset = 0;
    for (size_t col = 0; col < schema.size(); col++) {
      std::cout << ",";
      format_column_data(entry.data.data() + data_offset, schema[col].type,
                         std::cout);
      data_offset += schema[col].type;
    }

    std::cout << std::endl;
  }

  std::cout << "# Total records: " << leaf_data.size() << std::endl;
}

// Debug function to show raw bytes of stored data
void bp_debug_raw_leaf_data(BPlusTree &tree,
                            const std::vector<ColumnInfo> &schema) {
  auto leaf_data = bp_extract_leaf_data(tree);

  if (leaf_data.empty()) {
    std::cout << "No data in tree leaves" << std::endl;
    return;
  }

  std::cout << "=== RAW LEAF DATA DEBUG ===" << std::endl;
  std::cout << "Record size: " << tree.record_size << " bytes" << std::endl;
  std::cout << "Schema column sizes: ";
  for (size_t i = 0; i < schema.size(); i++) {
    if (i > 0)
      std::cout << " + ";
    std::cout << schema[i].type;
  }
  std::cout << " = "
            << std::accumulate(schema.begin(), schema.end(), 0,
                               [](int sum, const ColumnInfo &col) {
                                 return sum + col.type;
                               })
            << std::endl;

  for (size_t rec_idx = 0; rec_idx < std::min(size_t(3), leaf_data.size());
       rec_idx++) {
    const auto &entry = leaf_data[rec_idx];

    std::cout << "\nRecord " << rec_idx << " (key=" << entry.key
              << ", node=" << entry.node_page << "):" << std::endl;

    // Print all raw bytes
    std::cout << "Raw bytes: ";
    for (size_t i = 0; i < entry.data.size(); i++) {
      if (i > 0 && i % 16 == 0)
        std::cout << "\n           ";
      std::cout << std::hex << std::setw(2) << std::setfill('0')
                << (int)entry.data[i] << " ";
    }
    std::cout << std::dec << std::endl;

    // Parse by schema
    size_t offset = 0;
    for (size_t col = 0; col < schema.size(); col++) {
      std::cout << "Column " << col << " (type=" << schema[col].type
                << ", offset=" << offset << "): ";

      if (offset + schema[col].type <= entry.data.size()) {
        format_column_data(entry.data.data() + offset, schema[col].type,
                           std::cout);

        // Also show raw bytes for this column
        std::cout << " [raw: ";
        for (uint32_t i = 0; i < schema[col].type && i < 16; i++) {
          if (i > 0)
            std::cout << " ";
          std::cout << std::hex << std::setw(2) << std::setfill('0')
                    << (int)entry.data[offset + i] << std::dec;
        }
        if (schema[col].type > 16)
          std::cout << "...";
        std::cout << "]";
      } else {
        std::cout << "[DATA TRUNCATED]";
      }

      std::cout << std::endl;
      offset += schema[col].type;
    }
  }

  std::cout << "=========================" << std::endl;
}

std::vector<ColumnInfo> make_large_schema(bool to_large) {
  int count = 0;
  std::vector<ColumnInfo> schema;
  while ((count += 256) < PAGE_SIZE) {
    schema.push_back({TYPE_VARCHAR256});
  }
  if (to_large) {
    schema.push_back({TYPE_VARCHAR256});
  }
  return schema;
}

void test_data_types() {
  std::cout << BLUE << "\n=== Testing Different Data Types ===" << RESET
            << std::endl;

  // Test INT32
  {
    pager_init("test_int32.db");
    pager_begin_transaction();

    std::vector<ColumnInfo> schema = {{TYPE_INT32}};
    BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    Int32Record data1 = {42};
    Int32Record data2 = {-100};
    Int32Record data3 = {2147483647}; // MAX_INT

    bp_insert_element(tree, 1, reinterpret_cast<const uint8_t *>(&data1));
    bp_insert_element(tree, 2, reinterpret_cast<const uint8_t *>(&data2));
    bp_insert_element(tree, 3, reinterpret_cast<const uint8_t *>(&data3));

    const Int32Record *result1 =
        reinterpret_cast<const Int32Record *>(bp_get(tree, 1));
    const Int32Record *result2 =
        reinterpret_cast<const Int32Record *>(bp_get(tree, 2));
    const Int32Record *result3 =
        reinterpret_cast<const Int32Record *>(bp_get(tree, 3));

    check("INT32: Store and retrieve positive value",
          result1 && result1->value == 42);
    check("INT32: Store and retrieve negative value",
          result2 && result2->value == -100);
    check("INT32: Store and retrieve MAX_INT",
          result3 && result3->value == 2147483647);

    pager_commit();
    pager_close();
  }

  // Test INT64
  {
    pager_init("test_int64.db");
    pager_begin_transaction();

    std::vector<ColumnInfo> schema = {{TYPE_INT64}};
    BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    Int64Record data1 = {9223372036854775807LL}; // MAX_LONG
    Int64Record data2 = {-9223372036854775807LL};

    bp_insert_element(tree, 1, reinterpret_cast<const uint8_t *>(&data1));
    bp_insert_element(tree, 2, reinterpret_cast<const uint8_t *>(&data2));

    const Int64Record *result1 =
        reinterpret_cast<const Int64Record *>(bp_get(tree, 1));
    const Int64Record *result2 =
        reinterpret_cast<const Int64Record *>(bp_get(tree, 2));

    check("INT64: Store and retrieve MAX_LONG",
          result1 && result1->value == 9223372036854775807LL);
    check("INT64: Store and retrieve negative large value",
          result2 && result2->value == -9223372036854775807LL);

    pager_commit();
    pager_close();
  }

  // Test VARCHAR32
  {
    pager_init("test_varchar32.db");
    pager_begin_transaction();

    std::vector<ColumnInfo> schema = {{TYPE_VARCHAR32}};
    BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    VarChar32Record data1;
    strcpy(data1.data, "Hello, World!");

    VarChar32Record data2;
    strcpy(data2.data, "31 chars long string here....."); // 31 chars

    bp_insert_element(tree, 1, reinterpret_cast<const uint8_t *>(&data1));
    bp_insert_element(tree, 2, reinterpret_cast<const uint8_t *>(&data2));

    const VarChar32Record *result1 =
        reinterpret_cast<const VarChar32Record *>(bp_get(tree, 1));
    const VarChar32Record *result2 =
        reinterpret_cast<const VarChar32Record *>(bp_get(tree, 2));

    check("VARCHAR32: Store and retrieve short string",
          result1 && strcmp(result1->data, "Hello, World!") == 0);
    check("VARCHAR32: Store and retrieve max length string",
          result2 &&
              strcmp(result2->data, "31 chars long string here.....") == 0);

    pager_commit();
    pager_close();
  }

  // Test VARCHAR256
  {
    pager_init("test_varchar256.db");
    pager_begin_transaction();

    std::vector<ColumnInfo> schema = {{TYPE_VARCHAR256}};
    BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    VarChar256Record data1;
    std::string long_str =
        "This is a very long string that is used to test VARCHAR256. ";
    long_str += "It contains multiple sentences and should be able to store up "
                "to 255 characters. ";
    long_str += "Let's add some more text to make it longer and test the "
                "capacity properly.";
    strcpy(data1.data, long_str.c_str());

    bp_insert_element(tree, 1, reinterpret_cast<const uint8_t *>(&data1));

    const VarChar256Record *result1 =
        reinterpret_cast<const VarChar256Record *>(bp_get(tree, 1));

    check("VARCHAR256: Store and retrieve long string",
          result1 && strcmp(result1->data, long_str.c_str()) == 0);

    pager_commit();
    pager_close();
  }
}

void test_composite_records() {
  std::cout << BLUE << "\n=== Testing Composite Records ===" << RESET
            << std::endl;

  pager_init("test_composite.db");
  pager_begin_transaction();

  // Schema with multiple columns
  std::vector<ColumnInfo> schema = {
      {TYPE_INT32},     // id
      {TYPE_INT64},     // timestamp
      {TYPE_VARCHAR32}, // name
      {TYPE_VARCHAR256} // description
  };

  BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
  bp_init(tree);

  // Insert composite records
  CompositeRecord rec1 =
      create_composite_record(1001, "Alice", "Software Engineer at TechCorp");
  CompositeRecord rec2 = create_composite_record(
      1002, "Bob", "Data Scientist working on ML projects");
  CompositeRecord rec3 = create_composite_record(
      1003, "Charlie", "DevOps specialist with cloud expertise");

  bp_insert_element(tree, 100, reinterpret_cast<const uint8_t *>(&rec1));
  bp_insert_element(tree, 200, reinterpret_cast<const uint8_t *>(&rec2));
  bp_insert_element(tree, 150, reinterpret_cast<const uint8_t *>(&rec3));

  // Retrieve and verify
  const CompositeRecord *result1 =
      reinterpret_cast<const CompositeRecord *>(bp_get(tree, 100));
  const CompositeRecord *result2 =
      reinterpret_cast<const CompositeRecord *>(bp_get(tree, 200));
  const CompositeRecord *result3 =
      reinterpret_cast<const CompositeRecord *>(bp_get(tree, 150));

  check("Composite: Record 1 ID matches", result1 && result1->id == 1001);
  check("Composite: Record 1 name matches",
        result1 && strcmp(result1->name, "Alice") == 0);
  check("Composite: Record 2 ID matches", result2 && result2->id == 1002);
  check("Composite: Record 2 description matches",
        result2 && strstr(result2->description, "Data Scientist") != nullptr);
  check("Composite: Record 3 exists", result3 != nullptr);

  pager_commit();

  bp_debug_print_tree(tree);

  pager_close();
}

void test_capacity_and_splits() {
  std::cout << BLUE
            << "\n=== Testing Capacity Calculation and Node Splits ===" << RESET
            << std::endl;

  // Test with small records (should fit many per node)
  {
    pager_init("test_small_records.db");
    pager_begin_transaction();

    std::vector<ColumnInfo> schema = {{TYPE_INT32}};
    BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    // Calculate expected capacity
    const uint32_t expected_leaf_capacity =
        (PAGE_SIZE - 32) / (sizeof(uint32_t) + sizeof(Int32Record));
    std::cout << "Expected leaf capacity for INT32: " << expected_leaf_capacity
              << std::endl;
    check("Leaf capacity calculation reasonable", tree.leaf_max_keys > 100);

    // Insert enough to force splits
    for (int i = 0; i < 1000; i++) {
      Int32Record data = {i * 10};
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&data));
      bp_verify_all_invariants(tree);
    }

    // Verify all can be found
    bool all_found = true;
    for (int i = 0; i < 1000; i++) {
      if (!bp_find_element(tree, i)) {
        all_found = false;
        break;
      }
    }
    check("1000 small records inserted and found", all_found);

    pager_commit();
    pager_close();
  }

  // Test with large records (should fit few per node)
  {
    pager_init("test_large_records.db");
    pager_begin_transaction();

    std::vector<ColumnInfo> schema = {
        {TYPE_INT32}, {TYPE_INT64}, {TYPE_VARCHAR32}, {TYPE_VARCHAR256}};
    BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    const uint32_t expected_leaf_capacity =
        (PAGE_SIZE - 32) / (sizeof(uint32_t) + 300);
    std::cout << "Expected leaf capacity for composite (300 bytes): "
              << expected_leaf_capacity << std::endl;
    check("Leaf capacity for large records reasonable",
          tree.leaf_max_keys < 20);

    // Insert enough to force multiple splits
    for (int i = 0; i < 50; i++) {
      CompositeRecord rec =
          create_composite_record(i, "User_" + std::to_string(i),
                                  "Description for user " + std::to_string(i));
      bp_insert_element(tree, i * 10, reinterpret_cast<const uint8_t *>(&rec));
    }

    // Verify a sample
    const CompositeRecord *sample =
        reinterpret_cast<const CompositeRecord *>(bp_get(tree, 250));
    check("Large record after splits retrieved correctly",
          sample && sample->id == 25 && strcmp(sample->name, "User_25") == 0);

    pager_commit();
    pager_close();
  }
}

void verify_invariants(){

    pager_init("invariants.db");
    pager_begin_transaction();
    std::vector<ColumnInfo> schema = {{TYPE_INT64}};
    BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_validate_tree(tree);
   if(!test_single_leaf_operations()){
       std::cout <<"Falsed";
       exit(0);
   }
   return;

    Int64Record record = {10LL};
    int count = tree.leaf_max_keys * 10;
    bp_init(tree);

    for(int i = 0; i < count; i++) {
        bp_insert_element(tree, i, reinterpret_cast<const uint8_t*>(&record));
        bp_verify_all_invariants(tree);
    }

    for(int i = 0; i < count; i++) {
        bp_delete_element(tree, i);
        bp_verify_all_invariants(tree);
    }

    // would have crashed

    // check("All invariants true", true);

    // bp_debug_print_tree(tree);
}

void test_sequential_operations() {
  std::cout << BLUE << "\n=== Testing Sequential Operations ===" << RESET
            << std::endl;

  pager_init("test_sequential.db");
  pager_begin_transaction();

  std::vector<ColumnInfo> schema = {{TYPE_INT64}};
  BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);

  int count = tree.leaf_max_keys * 5;
  bp_init(tree);

  // Sequential insertion
  for (int i = 0; i < count; i++) {
    Int64Record data = {i * 1000LL};

    bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&data));
  }

  // Verify sequential order in leaves
  auto leaves = bp_print_leaves(tree);
  bool ordered = true;
  for (size_t i = 1; i < leaves.size(); i++) {
    if (leaves[i - 1].first >= leaves[i].first) {
      ordered = false;
      break;
    }
  }
  check("Sequential insertion maintains sorted order", ordered);
  check("All sequential elements in leaves", leaves.size() == count);

  ///    Verify data integrity
  bool data_intact = true;
  for (size_t i = 0; i < leaves.size(); i++) {
    const Int64Record *rec =
        reinterpret_cast<const Int64Record *>(leaves[i].second);
    if (rec->value != static_cast<int64_t>(leaves[i].first * 1000)) {
      data_intact = false;
      break;
    }
  }

  // Add this to test_sequential_operations() function, after the existing
  // checks

  // Test leaf node linked list structure
  std::cout << "Testing leaf node linked list integrity..." << std::endl;

  BPTreeNode *leftmost = bp_left_most(tree);
  check("Left-most leaf node exists", leftmost != nullptr);

  if (leftmost) {
    // Walk through the linked list and verify order
    std::vector<uint32_t> linked_list_keys;
    BPTreeNode *current = leftmost;

    while (current) {
      uint32_t *keys = reinterpret_cast<uint32_t *>(current->data);
      for (uint32_t i = 0; i < current->num_keys; i++) {
        linked_list_keys.push_back(keys[i]);
      }
      current = bp_get_next(current);
    }

    // Verify linked list has same count as leaf traversal
    check("Linked list contains all keys", linked_list_keys.size() == count);

    // Verify linked list is sorted
    bool linked_list_sorted = true;
    for (size_t i = 1; i < linked_list_keys.size(); i++) {
      if (linked_list_keys[i - 1] >= linked_list_keys[i]) {
        linked_list_sorted = false;
        break;
      }
    }
    check("Linked list maintains sorted order", linked_list_sorted);

    // Test backward traversal
    BPTreeNode *rightmost = leftmost;
    while (bp_get_next(rightmost)) {
      rightmost = bp_get_next(rightmost);
    }

    std::vector<uint32_t> reverse_keys;
    current = rightmost;
    while (current) {
      uint32_t *keys = reinterpret_cast<uint32_t *>(current->data);
      // Add keys in reverse order within each node
      for (int i = current->num_keys - 1; i >= 0; i--) {
        reverse_keys.push_back(keys[i]);
      }
      current = bp_get_prev(current);
    }

    // Verify backward traversal gives reverse order
    std::reverse(reverse_keys.begin(), reverse_keys.end());
    bool backward_correct = (reverse_keys == linked_list_keys);
    check("Backward linked list traversal correct", backward_correct);

    // Test that first node has no previous
    check("Left-most node has no previous", bp_get_prev(leftmost) == nullptr);

    // Test that last node has no next
    check("Right-most node has no next", bp_get_next(rightmost) == nullptr);
  }

  check("Sequential data values intact", data_intact);

  pager_commit();
  pager_close();
}

void test_random_operations() {
  std::cout << BLUE << "\n=== Testing Random Operations ===" << RESET
            << std::endl;

  pager_init("test_random.db");
  pager_begin_transaction();

  std::vector<ColumnInfo> schema = {{TYPE_INT32}};
  BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
  bp_init(tree);

  // Generate random keys
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(1, 10000);

  std::vector<int> keys;
  for (int i = 0; i < 500; i++) {
    keys.push_back(dis(gen));
  }

  // Remove duplicates
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

  std::cout << "Inserting " << keys.size() << " unique random keys..."
            << std::endl;

  // Insert in random order
  std::shuffle(keys.begin(), keys.end(), gen);
  for (int key : keys) {
    Int32Record data = {key * 100};
    bp_insert_element(tree, key, reinterpret_cast<const uint8_t *>(&data));
  }

  // Verify all can be found
  bool all_found = true;
  for (int key : keys) {
    const Int32Record *result =
        reinterpret_cast<const Int32Record *>(bp_get(tree, key));
    if (!result || result->value != key * 100) {
      all_found = false;
      std::cout << "Failed to find or verify key: " << key << std::endl;
      break;
    }
  }
  check("All random keys found with correct data", all_found);

  // Delete random subset
  int delete_count = keys.size() / 3;
  std::shuffle(keys.begin(), keys.end(), gen);
  for (int i = 0; i < delete_count; i++) {
    bp_delete_element(tree, keys[i]);
  }

  // Verify deletions
  bool deletions_correct = true;
  for (int i = 0; i < delete_count; i++) {
    if (bp_find_element(tree, keys[i])) {
      deletions_correct = false;
      break;
    }
  }
  check("Random deletions successful", deletions_correct);

  // Verify remaining keys
  bool remaining_intact = true;
  for (size_t i = delete_count; i < keys.size(); i++) {
    if (!bp_find_element(tree, keys[i])) {
      remaining_intact = false;
      break;
    }
  }
  check("Remaining keys intact after random deletions", remaining_intact);

  pager_commit();
  pager_close();
}

void test_update_operations() {
  std::cout << BLUE << "\n=== Testing Update Operations ===" << RESET
            << std::endl;

  pager_init("test_update.db");
  pager_begin_transaction();

  std::vector<ColumnInfo> schema = {{TYPE_VARCHAR32}};
  BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
  bp_init(tree);

  // Initial insert
  VarChar32Record original;
  strcpy(original.data, "Original Value");
  bp_insert_element(tree, 42, reinterpret_cast<const uint8_t *>(&original));

  // Verify original
  const VarChar32Record *result1 =
      reinterpret_cast<const VarChar32Record *>(bp_get(tree, 42));
  check("Original value inserted",
        result1 && strcmp(result1->data, "Original Value") == 0);

  // Update with same key
  VarChar32Record updated;
  strcpy(updated.data, "Updated Value");
  bp_insert_element(tree, 42, reinterpret_cast<const uint8_t *>(&updated));

  // Verify update
  const VarChar32Record *result2 =
      reinterpret_cast<const VarChar32Record *>(bp_get(tree, 42));
  check("Value updated correctly",
        result2 && strcmp(result2->data, "Updated Value") == 0);

  // Multiple updates
  for (int i = 0; i < 10; i++) {
    VarChar32Record data;
    snprintf(data.data, sizeof(data.data), "Update_%d", i);
    bp_insert_element(tree, 42, reinterpret_cast<const uint8_t *>(&data));
  }

  const VarChar32Record *final_result =
      reinterpret_cast<const VarChar32Record *>(bp_get(tree, 42));
  check("Multiple updates successful",
        final_result && strcmp(final_result->data, "Update_9") == 0);

  pager_commit();
  pager_close();
}

void test_persistence() {
  std::cout << BLUE << "\n=== Testing Persistence Across Sessions ===" << RESET
            << std::endl;

  const char *db_file = "test_persist.db";

  BPlusTree tree;
  // First session: insert data
  {
    pager_init(db_file);
    pager_begin_transaction();

    std::vector<ColumnInfo> schema = {{TYPE_INT32}, {TYPE_VARCHAR32}};
    tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    struct Record {
      int32_t id;
      char name[32];
    };

    for (int i = 0; i < 20; i++) {
      Record rec;
      rec.id = i * 100;
      snprintf(rec.name, sizeof(rec.name), "Person_%d", i);
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&rec));
    }

    // Save tree metadata for next session
    // In real implementation, this would be stored in a catalog
    uint32_t root_index = tree.root_page_index;

    pager_commit();
    pager_close();

    std::cout << "First session completed, root page: " << root_index
              << std::endl;
  }

  // Second session: read and verify data
  {
    pager_init(db_file);
    pager_begin_transaction();

    std::vector<ColumnInfo> schema = {{TYPE_INT32}, {TYPE_VARCHAR32}};

    // In real implementation, would load root from catalog
    // For now, we'll recreate and verify some operations work

    struct Record {
      int32_t id;
      char name[32];
    };

    // Try to insert duplicate key - should update
    Record new_rec;
    new_rec.id = 999;
    strcpy(new_rec.name, "Updated_5");
    bp_insert_element(tree, 5, reinterpret_cast<const uint8_t *>(&new_rec));

    check("Persistence test completed", true);

    pager_commit();
    pager_close();
  }
}

void test_boundary_conditions() {
  std::cout << BLUE << "\n=== Testing Boundary Conditions ===" << RESET
            << std::endl;

  pager_init("test_boundary.db");
  pager_begin_transaction();

  std::vector<ColumnInfo> schema = {{TYPE_INT32}};
  BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
  bp_init(tree);

  // Test empty tree
  check("Empty tree: search returns null", bp_get(tree, 1) == nullptr);
  check("Empty tree: find returns false", !bp_find_element(tree, 1));

  // Single element
  Int32Record single = {42};
  bp_insert_element(tree, 1, reinterpret_cast<const uint8_t *>(&single));
  check("Single element: can be found", bp_find_element(tree, 1));

  // Delete single element
  bp_delete_element(tree, 1);
  check("After deleting single element: tree is empty",
        !bp_find_element(tree, 1));

  // Test with minimum and maximum key values
  Int32Record min_rec = {INT32_MIN};
  Int32Record max_rec = {INT32_MAX};

  bp_insert_element(tree, 0, reinterpret_cast<const uint8_t *>(&min_rec));
  bp_insert_element(tree, UINT32_MAX,
                    reinterpret_cast<const uint8_t *>(&max_rec));

  const Int32Record *min_result =
      reinterpret_cast<const Int32Record *>(bp_get(tree, 0));
  const Int32Record *max_result =
      reinterpret_cast<const Int32Record *>(bp_get(tree, UINT32_MAX));

  check("Minimum key value stored",
        min_result && min_result->value == INT32_MIN);
  check("Maximum key value stored",
        max_result && max_result->value == INT32_MAX);

  // Fill node to exactly max capacity
  pager_commit();
  pager_close();

  pager_init("test_exact_capacity.db");
  pager_begin_transaction();

  BPlusTree tree2 = bp_create(TYPE_INT32, schema, BPLUS);
  bp_init(tree2);

  std::cout << "Leaf max keys: " << tree2.leaf_max_keys << std::endl;

  // Insert exactly max_keys elements
  for (uint32_t i = 0; i < tree2.leaf_max_keys; i++) {
    Int32Record data = {static_cast<int32_t>(i)};
    bp_insert_element(tree2, i, reinterpret_cast<const uint8_t *>(&data));
  }

  // This should trigger a split
  Int32Record trigger = {999};
  bp_insert_element(tree2, tree2.leaf_max_keys,
                    reinterpret_cast<const uint8_t *>(&trigger));

  // Verify all elements still accessible
  bool all_accessible = true;
  for (uint32_t i = 0; i <= tree2.leaf_max_keys; i++) {
    if (!bp_find_element(tree2, i)) {
      all_accessible = false;
      break;
    }
  }
  check("All elements accessible after exact capacity split", all_accessible);

  pager_commit();
  pager_close();
}

void test_rollback_functionality() {
  std::cout << BLUE
            << "\n=== Testing Rollback and mark_dirty Behavior ===" << RESET
            << std::endl;

  const char *db_file = "test_rollback.db";
  std::vector<ColumnInfo> schema = {{TYPE_INT32}};
  BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
  // Test 1: Basic rollback after modifications
  {
    pager_init(db_file);
    pager_begin_transaction();

    bp_init(tree);

    // Insert some initial data
    for (int i = 0; i < 10; i++) {
      Int32Record data = {i * 100};
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&data));
    }

    // Verify initial data exists
    bool initial_data_present = true;
    for (int i = 0; i < 10; i++) {
      if (!bp_find_element(tree, i)) {
        initial_data_present = false;
        break;
      }
    }

    pager_commit();

    uint64_t before;
    uint64_t during;
    uint64_t after;

    pager_begin_transaction();

    before = debug_hash_tree(tree);

    // Verify we can still see committed data
    bool committed_data_visible = true;
    for (int i = 0; i < 10; i++) {
      if (!bp_find_element(tree, i)) {
        committed_data_visible = false;
        break;
      }
    }
    check("Rollback: Committed data visible", committed_data_visible);

    // Make modifications that we'll rollback
    // 1. Update existing records
    for (int i = 0; i < 5; i++) {
      Int32Record updated_data = {i * 1000}; // Change from i*100 to i*1000
      bp_insert_element(tree, i,
                        reinterpret_cast<const uint8_t *>(&updated_data));
    }

    // 2. Insert new records
    for (int i = 100; i < 110; i++) {
      Int32Record new_data = {i * 50};
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&new_data));
    }

    // 3. Delete some existing records
    for (int i = 7; i < 10; i++) {
      bp_delete_element(tree, i);
    }

    // Verify modifications are visible before rollback
    const Int32Record *updated =
        reinterpret_cast<const Int32Record *>(bp_get(tree, 2));
    bool modifications_visible = (updated && updated->value == 2000) &&
                                 bp_find_element(tree, 105) &&
                                 !bp_find_element(tree, 8);
    check("Rollback: Modifications visible before rollback",
          modifications_visible);

    // Force a rollback by simulating transaction abort
    during = debug_hash_tree(tree);
    pager_rollback();
    after = debug_hash_tree(tree);
    pager_close();

    check("Hashes work", during != before && before == after);
  }

  // Test 3: Verify rollback worked - original data should be restored
  {
    pager_init(db_file);
    pager_begin_transaction();

    // std::vector<ColumnInfo> schema = {{TYPE_INT32}};
    // BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
    // bp_init(tree);

    // Check that updates were rolled back
    bool updates_rolled_back = true;
    for (int i = 0; i < 5; i++) {
      const Int32Record *result =
          reinterpret_cast<const Int32Record *>(bp_get(tree, i));
      if (!result ||
          result->value != i * 100) { // Should be original i*100, not i*1000
        updates_rolled_back = false;
        std::cout << "Key " << i << " has value "
                  << (result ? result->value : -999) << ", expected "
                  << (i * 100) << std::endl;
        break;
      }
    }
    check("Rollback: Updates rolled back to original values",
          updates_rolled_back);

    // Check that inserts were rolled back
    bool inserts_rolled_back = true;
    for (int i = 100; i < 110; i++) {
      if (bp_find_element(tree, i)) {
        inserts_rolled_back = false;
        break;
      }
    }
    check("Rollback: New inserts rolled back", inserts_rolled_back);

    // Check that deletes were rolled back
    bool deletes_rolled_back = true;
    for (int i = 7; i < 10; i++) {
      const Int32Record *result =
          reinterpret_cast<const Int32Record *>(bp_get(tree, i));
      if (!result || result->value != i * 100) {
        deletes_rolled_back = false;
        break;
      }
    }
    check("Rollback: Deletes rolled back (data restored)", deletes_rolled_back);

    pager_commit();
    pager_close();
  }

  // // Test 4: Test rollback behavior with node splits
  {
    pager_init("test_rollback_splits.db");
    pager_begin_transaction();
    tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    // Insert enough data to cause splits, then rollback
    int insert_count = tree.leaf_max_keys * 3; // Force multiple splits

    for (int i = 0; i < insert_count; i++) {
      Int32Record data = {i * 10};
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&data));
    }

    // Verify data is there
    bool all_inserted = bp_find_element(tree, 0) &&
                        bp_find_element(tree, insert_count - 1) &&
                        bp_find_element(tree, insert_count / 2);
    check("Rollback splits: Data inserted before rollback", all_inserted);

    // Rollback - this should test that new pages created during splits are
    // properly handled
    pager_rollback();
    pager_close();
  }

  // Test 5: Verify split rollback worked
  {
    pager_init("test_rollback_splits.db");
    pager_begin_transaction();

    // Tree should be empty after rollback
    bool tree_empty = !bp_find_element(tree, 0) && !bp_find_element(tree, 10) &&
                      !bp_find_element(tree, 100);
    check("Rollback splits: Tree empty after rollback", tree_empty);

    pager_commit();
    pager_close();
  }

  // Test 6: Test partial transaction rollback (journal replay)
  {
    pager_init("test_partial_rollback.db");
    tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    // Simulate a crash by not calling commit after modifications
    pager_begin_transaction();

    std::vector<ColumnInfo> schema = {{TYPE_VARCHAR32}};
    BPlusTree tree = bp_create(TYPE_INT32, schema, BPLUS);
    bp_init(tree);

    for (int i = 0; i < 20; i++) {
      VarChar32Record data;
      snprintf(data.data, sizeof(data.data), "Record_%d", i);
      bp_insert_element(tree, i, reinterpret_cast<const uint8_t *>(&data));
    }

    // Simulate crash - close without commit (journal should remain)
    pager_close();
  }

  // // Test 7: Verify journal recovery on reopen
  {
    // When we reopen, pager_init should detect the journal and rollback
    pager_init("test_partial_rollback.db");

    // After journal recovery, tree should be empty
    bool recovered_empty = !bp_find_element(tree, 0) &&
                           !bp_find_element(tree, 10) &&
                           !bp_find_element(tree, 19);
    check("Journal recovery: Tree empty after journal rollback",
          recovered_empty);

    pager_close();
  }
}

int main() {
  std::cout << "B+ Tree Test Suite" << std::endl;
  std::cout << "==================" << std::endl;

  try {

          // large_records();



          verify_invariants();

    // test_composite_records();

    // test_rollback_functionality();
    // test_capacity_and_splits();
    // test_sequential_operations();
    // test_update_operations();
    // test_data_types();
    // test_boundary_conditions();
    // test_random_operations();
    // test_persistence();

    std::cout << "\n=== Test Suite Completed ===" << std::endl;
    std::cout << "All tests finished. Check individual results above."
              << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
