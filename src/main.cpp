#include "executor.hpp"
#include "os_layer.hpp"
#include "schema.hpp"

const char *create_customers = "CREATE TABLE Customers (INT id, VAR32 name, VAR32 email);";
const char *create_products = "CREATE TABLE Products (INT id, VAR32 name, VAR32 email);";
const char *insert_customer = "INSERT INTO Customers VALUES (1, 'john', 'john@smith.com');";
const char *select_customers = "SELECT * FROM Customers;";
const char *select_tables = "SELECT * FROM sqlite_master;";

const char *
next_customer(int id)
{
	static char buf[100];
	sprintf(buf, "INSERT INTO Customers VALUES (%d, 'john', 'john@smith.com');", id + 1);
	return buf;
}

int
main()
{

	_debug = false;
	bool existed = os_file_exists("db");
	init_executor();

	if (!existed)
	{
		execute(create_customers);
		for (int i = 1; i < 100; i++)
		{
			execute(next_customer(i));
		}
		// execute(create_products);
	}
	execute(select_tables);
	// print_table_info("sqlite_master");
	// print_table_info("Customers");
	// execute(insert_customer);
	// execute(select_customers);
	// execute(select_tables);

	return 0;
}
