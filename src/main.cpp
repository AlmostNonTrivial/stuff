#include "executor.hpp"


#include <cstdlib>
#include <cstdio>

// Test table creation statements
const char *create_customers = "CREATE TABLE Customers (INT id, VAR32 name, VAR32 email);";
const char *create_products = "CREATE TABLE Products (INT id, VAR32 name, INT price);";
const char *create_orders = "CREATE TABLE Orders (INT id, INT customer_id, INT product_id);";

// Index statements
const char *create_customer_name_idx = "CREATE INDEX idx_customer_name ON Customers (name);";
const char *create_customer_email_idx = "CREATE INDEX idx_customer_email ON Customers (email);";
const char *create_product_name_idx = "CREATE INDEX idx_product_name ON Products (name);";
const char *drop_customer_name_idx = "DROP INDEX idx_customer_name;";

// Query statements
const char *select_tables = "SELECT * FROM sqlite_master;";
const char *select_customers = "SELECT * FROM Customers;";

// Generate bulk insert statements
const char *
bulk_insert_customer(int start, int count)
{
    static char buf[4096];
    int offset = 0;
    for (int i = start; i < start + count; i++) {
        offset += sprintf(buf + offset, "INSERT INTO Customers VALUES (%d, 'user%d', 'u%d@test.com');",
                          i, i, i);
    }
    return buf;
}

// Generate bulk delete statement
const char *
bulk_delete_customer(int start, int end)
{
    static char buf[256];
    sprintf(buf, "DELETE FROM Customers WHERE id >= %d AND id <= %d;", start, end);
    return buf;
}
// Add these to executor.hpp:
void set_capture_mode(bool capture);
size_t get_row_count();
bool check_int_value(size_t row, size_t col, int expected);
bool check_string_value(size_t row, size_t col, const char* expected);
void clear_results();

// In main.cpp, use like:
int main() {
    executor_init(false);

    // Setup
    execute(create_customers);

    execute(bulk_insert_customer(1, 1));

    // Test with capture
    set_capture_mode(true);
    execute("SELECT * FROM Customers;");

    // Assert
    if (get_row_count() != 1) {
        printf("FAIL: Expected 1 row, got %zu\n", get_row_count());
    }
    if (!check_int_value(0, 0, 1)) {
        printf("FAIL: Expected id=1\n");
    }
    if (!check_string_value(0, 1, "user1")) {
        printf("FAIL: Expected name='user1'\n");
    }

    // Back to print mode for debugging
    set_capture_mode(false);
    execute("SELECT * FROM Customers;");

    executor_shutdown();
}
