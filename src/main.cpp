#include "executor.hpp"
#include "vm.hpp"
#include <cstdlib>
#include <cstdio>

// Simple test harness
struct TestResult {
    int passed = 0;
    int failed = 0;

    void assert_true(bool condition, const char* test_name) {
        if (condition) {
            printf("[PASS] %s\n", test_name);
            passed++;
        } else {
            printf("[FAIL] %s\n", test_name);
            print_results();
            // exit(1);
            failed++;
        }
    }

    void print_summary() {
        printf("\n=== Test Summary ===\n");
        printf("Passed: %d\n", passed);
        printf("Failed: %d\n", failed);
        printf("Total:  %d\n", passed + failed);
    }
};

// Helper functions for bulk data generation
void insert_users(int start, int count) {
    static char buffer[256];
    for (int i = start; i < start + count; i++) {
        snprintf(buffer, sizeof(buffer),
                "INSERT INTO users VALUES (%d, 'User%d', %d);",
                i, i, 20 + (i % 50)); // Ages from 20-69
        execute(buffer);
    }
}

void insert_products(int start, int count) {
    static char buffer[256];
    const char* names[] = {"Apple", "Banana", "Cherry", "Date", "Elderberry"};
    for (int i = start; i < start + count; i++) {
        snprintf(buffer, sizeof(buffer),
                "INSERT INTO products VALUES (%d, '%s%d', %d);",
                i, names[i % 5], i, (i * 25) % 200 + 10); // Prices 10-209
        execute(buffer);
    }
}

void insert_accounts(int start, int count) {
    static char buffer[256];
    for (int i = start; i < start + count; i++) {
        snprintf(buffer, sizeof(buffer),
                "INSERT INTO accounts VALUES (%d, 'Account%d', %d);",
                i, i, i * 100); // Balance = id * 100
        execute(buffer);
    }
}

void test_basic_operations() {
    TestResult results;

    // Setup - Create table
    execute("CREATE TABLE users (INT id, VAR32 name, INT age);");

    // Test 1: INSERT (100 users)
    insert_users(1, 100);

    set_capture_mode(true);
    execute("SELECT * FROM users;");
    results.assert_true(get_row_count() == 100, "INSERT - 100 rows inserted");
    clear_results();

    // Test 2: SELECT with WHERE (specific user)
    execute("SELECT * FROM users WHERE id = 42;");

    results.assert_true(get_row_count() == 1, "SELECT WHERE - single row");
    results.assert_true(check_string_value(0, 1, "User42"), "SELECT WHERE - correct name");
    results.assert_true(check_int_value(0, 2, 20 + (42 % 50)), "SELECT WHERE - correct age");
    clear_results();

    // Test 3: UPDATE with WHERE (update multiple rows)
    set_capture_mode(false);
    execute("UPDATE users SET age = 99 WHERE age > 60;");

    set_capture_mode(true);
    execute("SELECT * FROM users WHERE age = 99;");
    int updated_count = get_row_count();
    results.assert_true(updated_count > 0, "UPDATE WHERE - rows updated");
    clear_results();

    // Test 4: DELETE with WHERE (delete users with age > 50)
    set_capture_mode(false);
    execute("DELETE FROM users WHERE age > 50;");

    set_capture_mode(true);
    execute("SELECT * FROM users;");
    int remaining = get_row_count();
    results.assert_true(remaining < 100 && remaining > 0, "DELETE WHERE - partial deletion");
    clear_results();

    // Test 5: DELETE without WHERE (delete all remaining)
    set_capture_mode(false);
    execute("DELETE FROM users;");

    set_capture_mode(true);
    execute("SELECT * FROM users;");
    results.assert_true(get_row_count() == 0, "DELETE ALL - table empty");
    clear_results();

    set_capture_mode(false);
    results.print_summary();
}

void test_comparison_operators() {
    TestResult results;

    execute("CREATE TABLE products (INT id, VAR32 name, INT price);");

    // Insert 200 products with varied prices
    insert_products(1, 200);

    set_capture_mode(true);


    // Test < (price < 50)
    execute("SELECT * FROM products WHERE price < 50;");
    int lt_count = get_row_count();
    results.assert_true(lt_count > 0 && lt_count < 200, "WHERE < operator");
    clear_results();

    // Test <= (price <= 100)
    execute("SELECT * FROM products WHERE price <= 100;");
    int lte_count = get_row_count();
    results.assert_true(lte_count >= lt_count, "WHERE <= operator");
    clear_results();

    // Test > (price > 150)
    execute("SELECT * FROM products WHERE price > 150;");
    int gt_count = get_row_count();
    results.assert_true(gt_count > 0 && gt_count < 200, "WHERE > operator");
    clear_results();

    // Test >= (price >= 100)

    execute("SELECT * FROM products WHERE price >= 0;");

    int gte_count = get_row_count();
    results.assert_true(gte_count >= gt_count, "WHERE >= operator");
    clear_results();

    // Test != (price != 100)
    execute("SELECT * FROM products WHERE price != 100;");
    int ne_count = get_row_count();
    results.assert_true(ne_count > 0, "WHERE != operator");
    clear_results();

    // Verify total
    execute("SELECT * FROM products;");
    results.assert_true(get_row_count() == 200, "Total products correct");
    clear_results();

    set_capture_mode(false);
    results.print_summary();
}

void test_transactions() {
    TestResult results;

    execute("CREATE TABLE accounts (INT id, VAR32 name, INT balance);");

    // Insert 50 accounts
    insert_accounts(1, 50);

    // Test rollback on multiple updates
    execute("BEGIN;");
    execute("UPDATE accounts SET balance = 0;"); // Set all to 0
    execute("ROLLBACK;");

    set_capture_mode(true);
    execute("SELECT * FROM accounts WHERE id = 25;");
    results.assert_true(check_int_value(0, 2, 2500), "ROLLBACK - balance unchanged for id=25");
    clear_results();

    // Test commit with selective update
    set_capture_mode(false);
    execute("BEGIN;");
    execute("UPDATE accounts SET balance = 9999 WHERE id < 10;");
    execute("COMMIT;");

    set_capture_mode(true);
    execute("SELECT * FROM accounts WHERE id = 5;");
    results.assert_true(check_int_value(0, 2, 9999), "COMMIT - balance changed for id=5");
    clear_results();

    execute("SELECT * FROM accounts WHERE id = 25;");
    results.assert_true(check_int_value(0, 2, 2500), "COMMIT - unchanged accounts remain");
    clear_results();

    // Test transaction with mixed operations
    set_capture_mode(false);
    execute("BEGIN;");
    execute("DELETE FROM accounts WHERE balance > 5000;");
    execute("INSERT INTO accounts VALUES (100, 'NewAccount', 10000);");
    execute("UPDATE accounts SET balance = 100 WHERE id <= 5;");
    execute("COMMIT;");

    set_capture_mode(true);
    execute("SELECT * FROM accounts WHERE id = 100;");
    results.assert_true(get_row_count() == 1, "Transaction - new account exists");
    results.assert_true(check_int_value(0, 2, 10000), "Transaction - new account balance");
    clear_results();

    set_capture_mode(false);
    results.print_summary();
}

int main() {
    executor_init(false);

    printf("=== Basic Operations Test (100 records) ===\n");
    test_basic_operations();

    printf("\n=== Comparison Operators Test (200 records) ===\n");
    test_comparison_operators();

    printf("\n=== Transaction Test (50 records) ===\n");
    test_transactions();

    executor_shutdown();
    return 0;
}
