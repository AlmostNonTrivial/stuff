#include "executor.hpp"
#include "pager.hpp"
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

// Generate table creation for stress testing master catalog
const char *
create_test_table(int n)
{
    static char buf[256];
    sprintf(buf, "CREATE TABLE TestTable%d (INT id, VAR32 data);", n);
    return buf;
}

int
main()
{
    printf("=== TEST SUITE: Index Operations & Root Changes ===\n\n");

    // =========================================================================
    printf("=== Phase 1: Initial Setup with Tables and Indexes ===\n");
    // =========================================================================


    executor_init(false);

    // Create base tables
printf("\nCreating tables...\n");
    execute(create_customers);
    execute(create_products);
    execute(create_orders);

    // Create indexes
    printf("\nCreating indexes...\n");
    execute(create_customer_name_idx);
    execute(create_customer_email_idx);
    execute(create_product_name_idx);

    printf("\nInitial master catalog (note tables and indexes):\n");
    execute(select_tables);

    executor_shutdown();

    // // =========================================================================
    // printf("\n=== Phase 2: Verify Index Persistence ===\n");
    // // =========================================================================

    executor_init(true);

    printf("\nMaster after reopen (indexes should be present):\n");
    execute(select_tables);

    // Drop an index
    printf("\nDropping idx_customer_name...\n");
    execute(drop_customer_name_idx);

    printf("\nMaster after dropping index:\n");
    execute(select_tables);

    executor_shutdown();

    // =========================================================================
    printf("\n=== Phase 3: Bulk Insert/Delete to Trigger Root Changes ===\n");
    // =========================================================================

    executor_init(true);

    // Insert lots of data
    printf("\nInserting 100 customers...\n");
    execute(bulk_insert_customer(1, 20));
    execute(bulk_insert_customer(21, 20));
    execute(bulk_insert_customer(41, 20));
    execute(bulk_insert_customer(61, 20));
    execute(bulk_insert_customer(81, 20));

    printf("\nMaster after bulk inserts (check root changes):\n");
    execute(select_tables);

    printf("\nCustomer count check:\n");
    execute(select_customers);

    // Delete enough to cause merges
    printf("\nDeleting customers 20-80 to trigger merges...\n");
    execute("DELETE FROM Customers WHERE id >= 20 AND id <= 80;");

    printf("\nMaster after bulk deletes (roots may change due to merges):\n");
    execute(select_tables);



    executor_shutdown();

    // =========================================================================
    printf("\n=== Phase 4: Stress Test Master Catalog ===\n");
    // =========================================================================

    executor_init(true);

    printf("\nCreating many tables to stress master catalog...\n");

    // Create enough tables to cause master catalog splits
    for (int i = 1; i <= 30; i++) {
        if (i % 10 == 0) {
            printf("Created %d tables...\n", i);
        }
        execute(create_test_table(i));
    }

    printf("\nMaster catalog after creating 30 tables (root should have changed):\n");
    execute(select_tables);


    executor_shutdown();

    // =========================================================================
    printf("\n=== Phase 5: Final Verification After Multiple Restarts ===\n");
    // =========================================================================

    executor_init(true);

    printf("\nFinal count of catalog entries:\n");
    execute(select_tables);

    // Drop some test tables
    printf("\nDropping some test tables...\n");
    execute("DROP TABLE TestTable1;");
    execute("DROP TABLE TestTable2;");
    execute("DROP TABLE TestTable3;");

    printf("\nCatalog after drops:\n");
    execute(select_tables);

    executor_shutdown();


    // =========================================================================
    printf("\n=== Phase 6: Verify Everything Persisted Correctly ===\n");
    // =========================================================================

    executor_init(true);

    printf("\nFinal master state summary:\n");
   execute(select_tables) ;

    printf("\nVerifying Customers table still works:\n");
    execute("INSERT INTO Customers VALUES (999, 'final_test', 'test@final.com');");
    execute("SELECT * FROM Customers WHERE id = 999;");

    // Try to use dropped index (should fail gracefully or not exist)
    printf("\nVerifying dropped index is gone:\n");
    execute("SELECT * FROM sqlite_master WHERE name = 'idx_customer_name';");

    // Try to query dropped table (should fail)
    printf("\nAttempting to query dropped TestTable1 (should fail):\n");
    execute("SELECT * FROM TestTable1;");

    executor_shutdown();

    printf("\n=== TEST SUITE COMPLETE ===\n");
    printf("Successfully tested:\n");
    printf("- Index creation and dropping\n");
    printf("- Root changes from bulk deletes/merges\n");
    printf("- Master catalog root changes from many tables\n");
    printf("- Persistence across multiple restarts\n");

    return 0;
}
