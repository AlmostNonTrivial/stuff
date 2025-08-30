// integration_tests.cpp
#include "executor.hpp"
#include "vm.hpp"
#include <cassert>
#include <cstdio>

// Helper macro for test assertions
#define TEST_ASSERT(cond, msg) \
    if (!(cond)) { \
        printf("FAILED: %s\n", msg); \
        return false; \
    }

inline bool test_create_and_insert() {
    printf("Testing CREATE TABLE and INSERT...\n");

    // Create a simple table
    execute("CREATE TABLE users (id INT, name VARCHAR(32), age INT)");

    // Insert some data
    execute("INSERT INTO users VALUES (1, 'Alice', 30)");
    execute("INSERT INTO users VALUES (2, 'Bob', 25)");
    execute("INSERT INTO users VALUES (3, 'Charlie', 35)");

    // Verify with SELECT
    set_capture_mode(true);
    execute("SELECT * FROM users");

    TEST_ASSERT(get_row_count() == 3, "Should have 3 rows");

    // Check first row
    TEST_ASSERT(check_int_value(0, 0, 1), "First row ID should be 1");
    TEST_ASSERT(check_string_value(0, 1, "Alice"), "First row name should be Alice");
    TEST_ASSERT(check_int_value(0, 2, 30), "First row age should be 30");

    // Check second row
    TEST_ASSERT(check_int_value(1, 0, 2), "Second row ID should be 2");
    TEST_ASSERT(check_string_value(1, 1, "Bob"), "Second row name should be Bob");
    TEST_ASSERT(check_int_value(1, 2, 25), "Second row age should be 25");

    clear_results();
    set_capture_mode(false);

    printf("  ✓ CREATE TABLE and INSERT passed\n");
    return true;
}

inline bool test_select_where_() {
    printf("Testing SELECT with WHERE...\n");

    // Use the users table from previous test
    set_capture_mode(true);

    // Test equality
    execute("SELECT * FROM users WHERE id = 2");
    TEST_ASSERT(get_row_count() == 1, "Should have 1 row with id=2");
    TEST_ASSERT(check_int_value(0, 0, 2), "ID should be 2");
    TEST_ASSERT(check_string_value(0, 1, "Bob"), "Name should be Bob");
    clear_results();

    // Test greater than
    execute("SELECT * FROM users WHERE age > 25");
    TEST_ASSERT(get_row_count() == 2, "Should have 2 rows with age > 25");
    clear_results();

    // Test less than
    execute("SELECT * FROM users WHERE age < 30");
    TEST_ASSERT(get_row_count() == 1, "Should have 1 row with age < 30");
    TEST_ASSERT(check_string_value(0, 1, "Bob"), "Should be Bob");
    clear_results();

    // Test string comparison
    execute("SELECT * FROM users WHERE name = 'Charlie'");
    TEST_ASSERT(get_row_count() == 1, "Should have 1 row with name='Charlie'");
    TEST_ASSERT(check_int_value(0, 0, 3), "ID should be 3");
    clear_results();

    set_capture_mode(false);

    printf("  ✓ SELECT with WHERE passed\n");
    return true;
}

inline bool test_update() {
    printf("Testing UPDATE...\n");

    // Update Bob's age
    execute("UPDATE users SET age = 26 WHERE name = 'Bob'");

    // Verify the update
    set_capture_mode(true);
    execute("SELECT * FROM users WHERE name = 'Bob'");
    TEST_ASSERT(get_row_count() == 1, "Should find Bob");
    TEST_ASSERT(check_int_value(0, 2, 26), "Bob's age should be updated to 26");
    clear_results();

    // Update multiple rows
    execute("UPDATE users SET age = 40 WHERE age > 30");

    // Verify multiple updates
    execute("SELECT * FROM users WHERE age = 40");
    TEST_ASSERT(get_row_count() == 2, "Should have 2 rows with age=40");
    clear_results();

    // Update without WHERE (update all)
    execute("UPDATE users SET age = 50");

    execute("SELECT * FROM users");
    TEST_ASSERT(get_row_count() == 3, "Should still have 3 rows");
    // Check all ages are 50
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT(check_int_value(i, 2, 50), "All ages should be 50");
    }
    clear_results();

    set_capture_mode(false);

    printf("  ✓ UPDATE passed\n");
    return true;
}

inline bool test_delete() {
    printf("Testing DELETE...\n");

    // Delete specific row
    execute("DELETE FROM users WHERE id = 2");

    set_capture_mode(true);
    execute("SELECT * FROM users");
    TEST_ASSERT(get_row_count() == 2, "Should have 2 rows after delete");

    // Verify Bob is gone
    bool found_bob = false;
    for (size_t i = 0; i < get_row_count(); i++) {
        if (check_string_value(i, 1, "Bob")) {
            found_bob = true;
        }
    }
    TEST_ASSERT(!found_bob, "Bob should be deleted");
    clear_results();

    // Delete with condition
    execute("DELETE FROM users WHERE age = 50");

    execute("SELECT * FROM users");
    TEST_ASSERT(get_row_count() == 0, "All rows should be deleted (all had age=50)");
    clear_results();

    // Re-insert for next test
    execute("INSERT INTO users VALUES (4, 'David', 45)");
    execute("INSERT INTO users VALUES (5, 'Eve', 28)");

    // Delete without WHERE (delete all)
    execute("DELETE FROM users");

    execute("SELECT * FROM users");
    TEST_ASSERT(get_row_count() == 0, "Table should be empty after DELETE without WHERE");
    clear_results();

    set_capture_mode(false);

    printf("  ✓ DELETE passed\n");
    return true;
}

inline bool test_mixed_operations() {
    printf("Testing mixed operations...\n");

    // Create a products table
    execute("CREATE TABLE products (id INT, name VARCHAR(100), price INT, stock INT)");

    // Insert products
    execute("INSERT INTO products VALUES (1, 'Laptop', 1000, 10)");
    execute("INSERT INTO products VALUES (2, 'Mouse', 20, 50)");
    execute("INSERT INTO products VALUES (3, 'Keyboard', 80, 25)");
    execute("INSERT INTO products VALUES (4, 'Monitor', 300, 15)");

    set_capture_mode(true);

    // Complex query
    execute("SELECT * FROM products WHERE price > 50");
    TEST_ASSERT(get_row_count() == 3, "Should have 3 products with price > 50");
    clear_results();

    // Update stock for expensive items
    execute("UPDATE products SET stock = 5 WHERE price > 500");

    execute("SELECT * FROM products WHERE id = 1");
    TEST_ASSERT(check_int_value(0, 3, 5), "Laptop stock should be 5");
    clear_results();

    // Delete cheap items
    execute("DELETE FROM products WHERE price < 50");

    execute("SELECT * FROM products");
    TEST_ASSERT(get_row_count() == 3, "Should have 3 products after deleting cheap ones");
    clear_results();

    set_capture_mode(false);

    // Clean up
    execute("DROP TABLE products");

    printf("  ✓ Mixed operations passed\n");
    return true;
}

inline bool test_multiple_inserts() {
    printf("Testing multiple row INSERT...\n");

    execute("CREATE TABLE test_multi (id INT, val VARCHAR(32))");

    // Insert multiple rows in one statement
    execute("INSERT INTO test_multi VALUES (1, 'one'), (2, 'two'), (3, 'three')");

    set_capture_mode(true);
    execute("SELECT * FROM test_multi");
    TEST_ASSERT(get_row_count() == 3, "Should have 3 rows from multi-insert");

    TEST_ASSERT(check_int_value(0, 0, 1) && check_string_value(0, 1, "one"), "First row check");
    TEST_ASSERT(check_int_value(1, 0, 2) && check_string_value(1, 1, "two"), "Second row check");
    TEST_ASSERT(check_int_value(2, 0, 3) && check_string_value(2, 1, "three"), "Third row check");

    clear_results();
    set_capture_mode(false);

    execute("DROP TABLE test_multi");

    printf("  ✓ Multiple row INSERT passed\n");
    return true;
}

void run_integration_tests() {
    printf("\n========================================\n");
    printf("    SQL ENGINE INTEGRATION TESTS\n");
    printf("========================================\n\n");

    // Initialize the executor (assumes DB doesn't exist)
    executor_init(false);

    bool all_passed = true;

    all_passed &= test_create_and_insert();
    all_passed &= test_select_where_();
    all_passed &= test_update();
    all_passed &= test_delete();
    all_passed &= test_mixed_operations();
    all_passed &= test_multiple_inserts();

    // Clean up
    execute("DROP TABLE IF EXISTS users");

    executor_shutdown();

    printf("\n========================================\n");
    if (all_passed) {
        printf("    ALL TESTS PASSED! ✓\n");
    } else {
        printf("    SOME TESTS FAILED ✗\n");
    }
    printf("========================================\n\n");
}

// Main entry point for tests
inline void test_integration() {
    _debug = true;
    run_integration_tests();
}
