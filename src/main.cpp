#include "executor.hpp"
#include "pager.hpp"
#include <cstdlib>

const char *create_customers = "CREATE TABLE Customers (INT id, VAR32 name, VAR32 email);";
const char *create_products = "CREATE TABLE Products (INT id, VAR32 name, INT price);";
const char *insert_customer = "INSERT INTO Customers VALUES (1, 'john', 'john@smith.com');";
const char *insert_product = "INSERT INTO Products VALUES (1, 'Widget', 999);";
const char *select_customers = "SELECT * FROM Customers;";
const char *select_products = "SELECT * FROM Products;";
const char *select_tables = "SELECT * FROM sqlite_master;";

const char *
next_customer(int id)
{
	static char buf[100];
	sprintf(buf, "INSERT INTO Customers VALUES (%d, 'user%d', 'user%d@test.com');", id, id, id);
	return buf;
}

int
main()
{

    pager_init("db");
	printf("=== Phase 1: Fresh Database ===\n");
	executor_init(false);

	// Create tables and insert initial data
	printf("\nCreating tables...\n");
	execute(create_customers);
	execute(create_products);

	printf("\nMaster table after creates:\n");
	execute(select_tables);

	printf("\nInserting data...\n");
	execute(insert_customer);
	execute(next_customer(2));
	execute(insert_product);

	printf("\nCustomers table:\n");
	execute(select_customers);

	// Clean shutdown
	executor_shutdown();


	printf("\n=== Phase 2: Reopen Database ===\n");
	executor_init(true);

	printf("\nMaster table after reopen:\n");
	execute(select_tables);

	printf("\nCustomers table after reopen (should have 2 rows):\n");
	execute(select_customers);

	printf("\nProducts table after reopen (should have 1 row):\n");
	execute(select_products);

	// Verify we can still insert
	printf("\nInserting new customer after reopen...\n");
	execute(next_customer(3));

	printf("\nCustomers table with new row:\n");
	execute(select_customers);

	// Final cleanup
	executor_shutdown();


	printf("\n=== Phase 3: Final Verification ===\n");
	executor_init(true);

	printf("\nFinal customer count (should be 3):\n");
	execute(select_customers);

	printf("\nFinal master table:\n");
	execute(select_tables);

	executor_shutdown();


	printf("\n=== Test Complete ===\n");
	return 0;
}
