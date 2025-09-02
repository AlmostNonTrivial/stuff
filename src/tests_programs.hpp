// test_programs.hpp
#pragma once
#include "executor.hpp"
#include "compile.hpp"
#include "vm.hpp"
#include "catalog.hpp"

// Setup tables and indexes for testing

// Create base tables - customers, orders, products
void
setup_tables()
{
	// Customer table: id, name, city, credit_limit
	execute("CREATE TABLE customers (id INT, name VARCHAR(32), city VARCHAR(32), credit_limit INT)");

	// Orders table: id, customer_id, product_id, quantity, order_date
	execute("CREATE TABLE orders (id INT, customer_id INT, product_id INT, quantity INT, order_date INT)");

	// Products table: id, name, price, category
	execute("CREATE TABLE products (id INT, name VARCHAR(32), price INT, category VARCHAR(32))");
}

// Insert test data
void
populate_tables()
{
	// Customers - single row inserts
	execute("INSERT INTO customers VALUES (1, 'Alice', 'NYC', 5000)");
	execute("INSERT INTO customers VALUES (2, 'Bob', 'LA', 3000)");
	execute("INSERT INTO customers VALUES (3, 'Charlie', 'Chicago', 7000)");
	execute("INSERT INTO customers VALUES (4, 'Diana', 'NYC', 4000)");
	execute("INSERT INTO customers VALUES (5, 'Eve', 'LA', 6000)");

	// Products
	execute("INSERT INTO products VALUES (101, 'Laptop', 1200, 'Electronics')");
	execute("INSERT INTO products VALUES (102, 'Mouse', 25, 'Electronics')");
	execute("INSERT INTO products VALUES (103, 'Desk', 350, 'Furniture')");
	execute("INSERT INTO products VALUES (104, 'Chair', 150, 'Furniture')");
	execute("INSERT INTO products VALUES (105, 'Monitor', 400, 'Electronics')");

	// Orders - multiple per customer
	execute("INSERT INTO orders VALUES (1001, 1, 101, 1, 20240101)");
	execute("INSERT INTO orders VALUES (1002, 1, 102, 2, 20240102)");
	execute("INSERT INTO orders VALUES (1003, 2, 103, 1, 20240103)");
	execute("INSERT INTO orders VALUES (1004, 3, 101, 2, 20240104)");
	execute("INSERT INTO orders VALUES (1005, 2, 104, 4, 20240105)");
	execute("INSERT INTO orders VALUES (1006, 4, 105, 1, 20240106)");
	execute("INSERT INTO orders VALUES (1007, 1, 103, 1, 20240107)");
	execute("INSERT INTO orders VALUES (1008, 5, 102, 3, 20240108)");
}

// Create indexes via SQL (structure only)
void
create_indexes()
{
	// Index on foreign key for joins
	execute("CREATE INDEX idx_orders_customer ON orders(customer_id)");

	// Index on city for range/equality queries
	execute("CREATE INDEX idx_customers_city ON customers(city)");

	// Index on product category
	execute("CREATE INDEX idx_products_category ON products(category)");
}

// Hand-rolled bytecode to populate indexes

// Populate idx_orders_customer - maps customer_id -> order_id
void
populate_orders_customer_index()
{
	ProgramBuilder builder;

	// Open orders table for reading
	builder.emit(Opcodes::Open::create_btree(0, "orders", 0, false));

	// Open index for writing (column 1 is customer_id)
	builder.emit(Opcodes::Open::create_btree(1, "orders", 1, true));

	// Rewind orders table to beginning
	builder.emit(Opcodes::Rewind::create(0, "done", false));

	// Main scan loop
	builder.label("scan_loop");

	// Get customer_id (column 1) and order_id (column 0) from table
	int customer_id_reg = builder.regs.allocate();
	int order_id_reg = builder.regs.allocate();

	builder
		.emit(Opcodes::Column::create(0, 1, customer_id_reg)) // customer_id
		.emit(Opcodes::Column::create(0, 0, order_id_reg));	  // order_id (primary key)

	// Insert into index: key=customer_id, value=order_id
	builder.emit(Opcodes::Insert::create(1, customer_id_reg, 2));

	// Move to next row
	builder.emit(Opcodes::Step::create(0, "done", true));
	builder.emit(Opcodes::Goto::create("scan_loop"));

	// Cleanup
	builder.label("done");
	builder.emit(Opcodes::Close::create(0));
	builder.emit(Opcodes::Close::create(1));
	builder.emit(Opcodes::Halt::create(0));

	// Resolve labels and execute
	builder.resolve_labels();

	CompiledProgram program;
	program.type = PROG_DML_INSERT;
	program.instructions = builder.instructions;

	CompiledProgram programs[] = {program};
	execute_programs(programs, 1);

	printf("  - Populated idx_orders_customer\n");
}

// Populate idx_customers_city - maps city -> customer_id
void
populate_customers_city_index()
{
	ProgramBuilder builder;

	// Open customers table for reading
	builder.emit(Opcodes::Open::create_btree(0, "customers", 0, false));

	// Open city index for writing (column 2 is city)
	builder.emit(Opcodes::Open::create_btree(1, "customers", 2, true));

	// Rewind to beginning
	builder.emit(Opcodes::Rewind::create(0, "done", false));

	builder.label("scan_loop");

	// Get city (column 2) and customer_id (column 0)
	int city_reg = builder.regs.allocate();
	int customer_id_reg = builder.regs.allocate();

	builder
		.emit(Opcodes::Column::create(0, 2, city_reg))		   // city
		.emit(Opcodes::Column::create(0, 0, customer_id_reg)); // customer_id

	// Insert into index: key=city, value=customer_id
	builder.emit(Opcodes::Insert::create(1, city_reg, 2));

	// Next row
	builder.emit(Opcodes::Step::create(0, "done", true));
	builder.emit(Opcodes::Goto::create("scan_loop"));

	builder.label("done");
	builder.emit(Opcodes::Close::create(0));
	builder.emit(Opcodes::Close::create(1));
	builder.emit(Opcodes::Halt::create(0));

	builder.resolve_labels();

	CompiledProgram program;
	program.type = PROG_DML_INSERT;
	program.instructions = builder.instructions;

	CompiledProgram programs[] = {program};
	execute_programs(programs, 1);

	printf("  - Populated idx_customers_city\n");
}

// Populate idx_products_category - maps category -> product_id
void
populate_products_category_index()
{
	ProgramBuilder builder;

	// Open products table for reading
	builder.emit(Opcodes::Open::create_btree(0, "products", 0, false));

	// Open category index for writing (column 3 is category)
	builder.emit(Opcodes::Open::create_btree(1, "products", 3, true));

	// Rewind to beginning
	builder.emit(Opcodes::Rewind::create(0, "done", false));

	builder.label("scan_loop");

	// Get category (column 3) and product_id (column 0)
	int category_reg = builder.regs.allocate();
	int product_id_reg = builder.regs.allocate();

	builder
		.emit(Opcodes::Column::create(0, 3, category_reg))	  // category
		.emit(Opcodes::Column::create(0, 0, product_id_reg)); // product_id

	// Insert into index: key=category, value=product_id
	builder.emit(Opcodes::Insert::create(1, category_reg, 2));

	// Next row
	builder.emit(Opcodes::Step::create(0, "done", true));
	builder.emit(Opcodes::Goto::create("scan_loop"));

	builder.label("done");
	builder.emit(Opcodes::Close::create(0));
	builder.emit(Opcodes::Close::create(1));
	builder.emit(Opcodes::Halt::create(0));

	builder.resolve_labels();

	CompiledProgram program;
	program.type = PROG_DML_INSERT;
	program.instructions = builder.instructions;

	CompiledProgram programs[] = {program};
	execute_programs(programs, 1);

	printf("  - Populated idx_products_category\n");
}

// Cleanup
void
cleanup_test_environment()
{
	execute("DROP TABLE IF EXISTS orders");
	execute("DROP TABLE IF EXISTS customers");
	execute("DROP TABLE IF EXISTS products");
	executor_shutdown();
}

void
populate_indexes()
{
	populate_orders_customer_index();
	// populate_customers_city_index();
	// populate_products_category_index();
}

void
setup_test_environment()
{
	printf("Setting up test environment...\n");

	executor_init(false);

	// 1. Create tables using direct SQL
	setup_tables();

	// 2. Populate tables with test data
	populate_tables();

	// 3. Create indexes using SQL
	create_indexes();

	// 4. Populate indexes with hand-rolled bytecode
	populate_indexes();

	printf("Test environment ready.\n");
}

void
test_programs()
{
	setup_test_environment();

	execute("SELECT * FROM customers;");
}
