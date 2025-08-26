#include "executor.hpp"
#include "os_layer.hpp"
#include "pager.hpp"
#include "catalog.hpp"
#include "vm.hpp"
#include <cstdlib>

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
	bool pass = false;

label:

	init_executor();
	if (!pass)
	{

		execute(create_customers);
		execute(create_products);
		execute(select_tables);
	}


	executor_close();
	pager_close();

	// if (!pass)
	// {
	// 	pass = true;
	// 	goto label;
	// }



	// print_table_info("sqlite_master");
	// print_table_info("Customers");
	// execute(insert_customer);
	// execute(select_customers);
	// execute(select_tables);

	return 0;
}
