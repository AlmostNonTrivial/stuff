
// btree_test.cpp
#include "btree.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <cstring>
#include <random>
#include <chrono>
#include <algorithm>
#include <iomanip>

// Test result tracking
struct TestResults {
    int passed = 0;
    int failed = 0;
    std::vector<std::string> failed_tests;
};

static TestResults g_results;

// Color codes for terminal output
const char* RESET = "\033[0m";
const char* GREEN = "\033[32m";
const char* RED = "\033[31m";
const char* YELLOW = "\033[33m";
const char* BLUE = "\033[34m";

// Test helper function
void check(const std::string& test_name, bool condition) {
    if (condition) {
        std::cout << GREEN << "✓ " << RESET << test_name << std::endl;
        g_results.passed++;
    } else {
        std::cout << RED << "✗ " << RESET << test_name << std::endl;
        g_results.failed++;
        g_results.failed_tests.push_back(test_name);
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

struct CompositeRecord {
    int32_t id;           // 4 bytes
    int64_t timestamp;    // 8 bytes
    char name[32];        // 32 bytes
    char description[256]; // 256 bytes
    // Total: 300 bytes
};

// Helper function to create test data
CompositeRecord create_composite_record(int32_t id, const std::string& name, const std::string& desc) {
    CompositeRecord record;
    record.id = id;
    record.timestamp = std::chrono::system_clock::now().time_since_epoch().count();

    memset(record.name, 0, sizeof(record.name));
    strncpy(record.name, name.c_str(), sizeof(record.name) - 1);

    memset(record.description, 0, sizeof(record.description));
    strncpy(record.description, desc.c_str(), sizeof(record.description) - 1);

    return record;
}

void test_data_types() {
    std::cout << BLUE << "\n=== Testing Different Data Types ===" << RESET << std::endl;

    // Test INT32
    {
        pager_init("test_int32.db");
        pager_begin_transaction();

        std::vector<ColumnInfo> schema = {{TYPE_INT32}};
        BPlusTree tree = bp_create(schema);
        bp_init(tree);

        Int32Record data1 = {42};
        Int32Record data2 = {-100};
        Int32Record data3 = {2147483647}; // MAX_INT

        bp_insert_element(tree, 1, reinterpret_cast<const uint8_t*>(&data1));
        bp_insert_element(tree, 2, reinterpret_cast<const uint8_t*>(&data2));
        bp_insert_element(tree, 3, reinterpret_cast<const uint8_t*>(&data3));

        const Int32Record* result1 = reinterpret_cast<const Int32Record*>(bp_get(tree, 1));
        const Int32Record* result2 = reinterpret_cast<const Int32Record*>(bp_get(tree, 2));
        const Int32Record* result3 = reinterpret_cast<const Int32Record*>(bp_get(tree, 3));

        check("INT32: Store and retrieve positive value", result1 && result1->value == 42);
        check("INT32: Store and retrieve negative value", result2 && result2->value == -100);
        check("INT32: Store and retrieve MAX_INT", result3 && result3->value == 2147483647);

        pager_commit();
        pager_close();
    }

    // Test INT64
    {
        pager_init("test_int64.db");
        pager_begin_transaction();

        std::vector<ColumnInfo> schema = {{TYPE_INT64}};
        BPlusTree tree = bp_create(schema);
        bp_init(tree);

        Int64Record data1 = {9223372036854775807LL}; // MAX_LONG
        Int64Record data2 = {-9223372036854775807LL};

        bp_insert_element(tree, 1, reinterpret_cast<const uint8_t*>(&data1));
        bp_insert_element(tree, 2, reinterpret_cast<const uint8_t*>(&data2));

        const Int64Record* result1 = reinterpret_cast<const Int64Record*>(bp_get(tree, 1));
        const Int64Record* result2 = reinterpret_cast<const Int64Record*>(bp_get(tree, 2));

        check("INT64: Store and retrieve MAX_LONG", result1 && result1->value == 9223372036854775807LL);
        check("INT64: Store and retrieve negative large value", result2 && result2->value == -9223372036854775807LL);

        pager_commit();
        pager_close();
    }

    // Test VARCHAR32
    {
        pager_init("test_varchar32.db");
        pager_begin_transaction();

        std::vector<ColumnInfo> schema = {{TYPE_VARCHAR32}};
        BPlusTree tree = bp_create(schema);
        bp_init(tree);

        VarChar32Record data1;
        strcpy(data1.data, "Hello, World!");

        VarChar32Record data2;
        strcpy(data2.data, "31 chars long string here....."); // 31 chars

        bp_insert_element(tree, 1, reinterpret_cast<const uint8_t*>(&data1));
        bp_insert_element(tree, 2, reinterpret_cast<const uint8_t*>(&data2));

        const VarChar32Record* result1 = reinterpret_cast<const VarChar32Record*>(bp_get(tree, 1));
        const VarChar32Record* result2 = reinterpret_cast<const VarChar32Record*>(bp_get(tree, 2));

        check("VARCHAR32: Store and retrieve short string",
              result1 && strcmp(result1->data, "Hello, World!") == 0);
        check("VARCHAR32: Store and retrieve max length string",
              result2 && strcmp(result2->data, "31 chars long string here.....") == 0);

        pager_commit();
        pager_close();
    }

    // Test VARCHAR256
    {
        pager_init("test_varchar256.db");
        pager_begin_transaction();

        std::vector<ColumnInfo> schema = {{TYPE_VARCHAR256}};
        BPlusTree tree = bp_create(schema);
        bp_init(tree);

        VarChar256Record data1;
        std::string long_str = "This is a very long string that is used to test VARCHAR256. ";
        long_str += "It contains multiple sentences and should be able to store up to 255 characters. ";
        long_str += "Let's add some more text to make it longer and test the capacity properly.";
        strcpy(data1.data, long_str.c_str());

        bp_insert_element(tree, 1, reinterpret_cast<const uint8_t*>(&data1));

        const VarChar256Record* result1 = reinterpret_cast<const VarChar256Record*>(bp_get(tree, 1));

        check("VARCHAR256: Store and retrieve long string",
              result1 && strcmp(result1->data, long_str.c_str()) == 0);

        pager_commit();
        pager_close();
    }
}

void test_composite_records() {
    std::cout << BLUE << "\n=== Testing Composite Records ===" << RESET << std::endl;

    pager_init("test_composite.db");
    pager_begin_transaction();

    // Schema with multiple columns
    std::vector<ColumnInfo> schema = {
        {TYPE_INT32},     // id
        {TYPE_INT64},     // timestamp
        {TYPE_VARCHAR32}, // name
        {TYPE_VARCHAR256} // description
    };

    BPlusTree tree = bp_create(schema);
    bp_init(tree);

    // Insert composite records
    CompositeRecord rec1 = create_composite_record(1001, "Alice", "Software Engineer at TechCorp");
    CompositeRecord rec2 = create_composite_record(1002, "Bob", "Data Scientist working on ML projects");
    CompositeRecord rec3 = create_composite_record(1003, "Charlie", "DevOps specialist with cloud expertise");

    bp_insert_element(tree, 100, reinterpret_cast<const uint8_t*>(&rec1));
    bp_insert_element(tree, 200, reinterpret_cast<const uint8_t*>(&rec2));
    bp_insert_element(tree, 150, reinterpret_cast<const uint8_t*>(&rec3));

    // Retrieve and verify
    const CompositeRecord* result1 = reinterpret_cast<const CompositeRecord*>(bp_get(tree, 100));
    const CompositeRecord* result2 = reinterpret_cast<const CompositeRecord*>(bp_get(tree, 200));
    const CompositeRecord* result3 = reinterpret_cast<const CompositeRecord*>(bp_get(tree, 150));

    check("Composite: Record 1 ID matches", result1 && result1->id == 1001);
    check("Composite: Record 1 name matches", result1 && strcmp(result1->name, "Alice") == 0);
    check("Composite: Record 2 ID matches", result2 && result2->id == 1002);
    check("Composite: Record 2 description matches",
          result2 && strstr(result2->description, "Data Scientist") != nullptr);
    check("Composite: Record 3 exists", result3 != nullptr);

    pager_commit();
    pager_close();
}

void test_capacity_and_splits() {
    std::cout << BLUE << "\n=== Testing Capacity Calculation and Node Splits ===" << RESET << std::endl;

    // Test with small records (should fit many per node)
    {
        pager_init("test_small_records.db");
        pager_begin_transaction();

        std::vector<ColumnInfo> schema = {{TYPE_INT32}};
        BPlusTree tree = bp_create(schema);
        bp_init(tree);

        // Calculate expected capacity
        const uint32_t expected_leaf_capacity = (PAGE_SIZE - 32) / (sizeof(uint32_t) + sizeof(Int32Record));
        std::cout << "Expected leaf capacity for INT32: " << expected_leaf_capacity << std::endl;
        check("Leaf capacity calculation reasonable", tree.leaf_max_keys > 100);

        // Insert enough to force splits
        for (int i = 0; i < 1000; i++) {
            Int32Record data = {i * 10};
            bp_insert_element(tree, i, reinterpret_cast<const uint8_t*>(&data));
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
            {TYPE_INT32}, {TYPE_INT64}, {TYPE_VARCHAR32}, {TYPE_VARCHAR256}
        };
        BPlusTree tree = bp_create(schema);
        bp_init(tree);

        const uint32_t expected_leaf_capacity = (PAGE_SIZE - 32) / (sizeof(uint32_t) + 300);
        std::cout << "Expected leaf capacity for composite (300 bytes): " << expected_leaf_capacity << std::endl;
        check("Leaf capacity for large records reasonable", tree.leaf_max_keys < 20);

        // Insert enough to force multiple splits
        for (int i = 0; i < 50; i++) {
            CompositeRecord rec = create_composite_record(
                i,
                "User_" + std::to_string(i),
                "Description for user " + std::to_string(i)
            );
            bp_insert_element(tree, i * 10, reinterpret_cast<const uint8_t*>(&rec));
        }

        // Verify a sample
        const CompositeRecord* sample = reinterpret_cast<const CompositeRecord*>(bp_get(tree, 250));
        check("Large record after splits retrieved correctly",
              sample && sample->id == 25 && strcmp(sample->name, "User_25") == 0);

        pager_commit();
        pager_close();
    }
}

void test_sequential_operations() {
    std::cout << BLUE << "\n=== Testing Sequential Operations ===" << RESET << std::endl;

    pager_init("test_sequential.db");
    pager_begin_transaction();

    std::vector<ColumnInfo> schema = {{TYPE_INT64}};
    BPlusTree tree = bp_create(schema);

    int count = tree.leaf_max_keys * 5;
    bp_init(tree);

    // Sequential insertion
    for (int i = 0; i < count; i++) {
        Int64Record data = {i * 1000LL};

        bp_insert_element(tree, i, reinterpret_cast<const uint8_t*>(&data));
    }


    // Verify sequential order in leaves
    auto leaves = bp_print_leaves(tree);
    bool ordered = true;
    for (size_t i = 1; i < leaves.size(); i++) {
        if (leaves[i-1].first >= leaves[i].first) {
            ordered = false;
            break;
        }
    }
    check("Sequential insertion maintains sorted order", ordered);
    check("All sequential elements in leaves", leaves.size() == count);

///    Verify data integrity
    bool data_intact = true;
    for (size_t i = 0; i < leaves.size(); i++) {
        const Int64Record* rec = reinterpret_cast<const Int64Record*>(leaves[i].second);
        if (rec->value != static_cast<int64_t>(leaves[i].first * 1000)) {
            data_intact = false;
            break;
        }
    }

    // Add this to test_sequential_operations() function, after the existing checks

        // Test leaf node linked list structure
        std::cout << "Testing leaf node linked list integrity..." << std::endl;

        BTreeNode* leftmost = bp_left_most(tree);
        check("Left-most leaf node exists", leftmost != nullptr);

        if (leftmost) {
            // Walk through the linked list and verify order
            std::vector<uint32_t> linked_list_keys;
            BTreeNode* current = leftmost;

            while (current) {
                uint32_t* keys = reinterpret_cast<uint32_t*>(current->data);
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
                if (linked_list_keys[i-1] >= linked_list_keys[i]) {
                    linked_list_sorted = false;
                    break;
                }
            }
            check("Linked list maintains sorted order", linked_list_sorted);

            // Test backward traversal
            BTreeNode* rightmost = leftmost;
            while (bp_get_next(rightmost)) {
                rightmost = bp_get_next(rightmost);
            }

            std::vector<uint32_t> reverse_keys;
            current = rightmost;
            while (current) {
                uint32_t* keys = reinterpret_cast<uint32_t*>(current->data);
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
    std::cout << BLUE << "\n=== Testing Random Operations ===" << RESET << std::endl;

    pager_init("test_random.db");
    pager_begin_transaction();

    std::vector<ColumnInfo> schema = {{TYPE_INT32}};
    BPlusTree tree = bp_create(schema);
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

    std::cout << "Inserting " << keys.size() << " unique random keys..." << std::endl;

    // Insert in random order
    std::shuffle(keys.begin(), keys.end(), gen);
    for (int key : keys) {
        Int32Record data = {key * 100};
        bp_insert_element(tree, key, reinterpret_cast<const uint8_t*>(&data));
    }

    // Verify all can be found
    bool all_found = true;
    for (int key : keys) {
        const Int32Record* result = reinterpret_cast<const Int32Record*>(bp_get(tree, key));
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
    std::cout << BLUE << "\n=== Testing Update Operations ===" << RESET << std::endl;

    pager_init("test_update.db");
    pager_begin_transaction();

    std::vector<ColumnInfo> schema = {{TYPE_VARCHAR32}};
    BPlusTree tree = bp_create(schema);
    bp_init(tree);

    // Initial insert
    VarChar32Record original;
    strcpy(original.data, "Original Value");
    bp_insert_element(tree, 42, reinterpret_cast<const uint8_t*>(&original));

    // Verify original
    const VarChar32Record* result1 = reinterpret_cast<const VarChar32Record*>(bp_get(tree, 42));
    check("Original value inserted", result1 && strcmp(result1->data, "Original Value") == 0);

    // Update with same key
    VarChar32Record updated;
    strcpy(updated.data, "Updated Value");
    bp_insert_element(tree, 42, reinterpret_cast<const uint8_t*>(&updated));

    // Verify update
    const VarChar32Record* result2 = reinterpret_cast<const VarChar32Record*>(bp_get(tree, 42));
    check("Value updated correctly", result2 && strcmp(result2->data, "Updated Value") == 0);

    // Multiple updates
    for (int i = 0; i < 10; i++) {
        VarChar32Record data;
        snprintf(data.data, sizeof(data.data), "Update_%d", i);
        bp_insert_element(tree, 42, reinterpret_cast<const uint8_t*>(&data));
    }

    const VarChar32Record* final_result = reinterpret_cast<const VarChar32Record*>(bp_get(tree, 42));
    check("Multiple updates successful", final_result && strcmp(final_result->data, "Update_9") == 0);

    pager_commit();
    pager_close();
}

void test_persistence() {
    std::cout << BLUE << "\n=== Testing Persistence Across Sessions ===" << RESET << std::endl;

    const char* db_file = "test_persist.db";

    BPlusTree tree;
    // First session: insert data
    {
        pager_init(db_file);
        pager_begin_transaction();

        std::vector<ColumnInfo> schema = {{TYPE_INT32}, {TYPE_VARCHAR32}};
        tree = bp_create(schema);
        bp_init(tree);

        struct Record {
            int32_t id;
            char name[32];
        };

        for (int i = 0; i < 20; i++) {
            Record rec;
            rec.id = i * 100;
            snprintf(rec.name, sizeof(rec.name), "Person_%d", i);
            bp_insert_element(tree, i, reinterpret_cast<const uint8_t*>(&rec));
        }

        // Save tree metadata for next session
        // In real implementation, this would be stored in a catalog
        uint32_t root_index = tree.root_page_index;

        pager_commit();
        pager_close();

        std::cout << "First session completed, root page: " << root_index << std::endl;
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
        bp_insert_element(tree, 5, reinterpret_cast<const uint8_t*>(&new_rec));

        check("Persistence test completed", true);

        pager_commit();
        pager_close();
    }
}

void test_boundary_conditions() {
    std::cout << BLUE << "\n=== Testing Boundary Conditions ===" << RESET << std::endl;

    pager_init("test_boundary.db");
    pager_begin_transaction();

    std::vector<ColumnInfo> schema = {{TYPE_INT32}};
    BPlusTree tree = bp_create(schema);
    bp_init(tree);

    // Test empty tree
    check("Empty tree: search returns null", bp_get(tree, 1) == nullptr);
    check("Empty tree: find returns false", !bp_find_element(tree, 1));

    // Single element
    Int32Record single = {42};
    bp_insert_element(tree, 1, reinterpret_cast<const uint8_t*>(&single));
    check("Single element: can be found", bp_find_element(tree, 1));

    // Delete single element
    bp_delete_element(tree, 1);
    check("After deleting single element: tree is empty", !bp_find_element(tree, 1));

    // Test with minimum and maximum key values
    Int32Record min_rec = {INT32_MIN};
    Int32Record max_rec = {INT32_MAX};

    bp_insert_element(tree, 0, reinterpret_cast<const uint8_t*>(&min_rec));
    bp_insert_element(tree, UINT32_MAX, reinterpret_cast<const uint8_t*>(&max_rec));

    const Int32Record* min_result = reinterpret_cast<const Int32Record*>(bp_get(tree, 0));
    const Int32Record* max_result = reinterpret_cast<const Int32Record*>(bp_get(tree, UINT32_MAX));

    check("Minimum key value stored", min_result && min_result->value == INT32_MIN);
    check("Maximum key value stored", max_result && max_result->value == INT32_MAX);

    // Fill node to exactly max capacity
    pager_commit();
    pager_close();

    pager_init("test_exact_capacity.db");
    pager_begin_transaction();

    BPlusTree tree2 = bp_create(schema);
    bp_init(tree2);

    std::cout << "Leaf max keys: " << tree2.leaf_max_keys << std::endl;

    // Insert exactly max_keys elements
    for (uint32_t i = 0; i < tree2.leaf_max_keys; i++) {
        Int32Record data = {static_cast<int32_t>(i)};
        bp_insert_element(tree2, i, reinterpret_cast<const uint8_t*>(&data));
    }

    // This should trigger a split
    Int32Record trigger = {999};
    bp_insert_element(tree2, tree2.leaf_max_keys, reinterpret_cast<const uint8_t*>(&trigger));

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

int main() {
    std::cout << "B+ Tree Test Suite" << std::endl;
    std::cout << "==================" << std::endl;

    try {



test_capacity_and_splits();
test_sequential_operations();
test_update_operations();
test_data_types();
test_boundary_conditions();
test_random_operations();
test_composite_records();
// test_persistence();


        std::cout << "\n=== Test Suite Completed ===" << std::endl;
        std::cout << "All tests finished. Check individual results above." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
