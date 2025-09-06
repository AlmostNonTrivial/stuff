// test_arena_containers.cpp
#include "arena.hpp"
#include "catalog.hpp"
#include "common.hpp"
#include "utils.hpp"
#include "compile.hpp"
#include "pager.hpp"
#include "parser.hpp"
#include "semantic.hpp"

#include "vm.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <numeric>
#include <chrono>

bool
execute_sql_statement(const char *sql, bool asda = false);
// #include "tests/tests_pager.hpp"
// #include "tests/tests_parser.hpp"
// void
// print_result_callback(TypedValue *result, size_t count)
// {
// 	for (int i = 0; i < count; i++)
// 	{
// 		result[i].print();
// 		if (i != count - 1)
// 		{
// 			std::cout << ", ";
// 		}
// 	}
// 	std::cout << "\n";
// }

void
load_catalog_from_master()
{
	// Set the callback
	vm_set_result_callback(catalog_bootstrap_callback);

	// Run your existing master table scan
	ProgramBuilder prog = {};
	auto		   cctx = from_structure(catalog[MASTER_CATALOG]);
	int			   cursor = prog.open_cursor(&cctx);
	int			   is_at_end = prog.rewind(cursor, false);
	auto		   while_context = prog.begin_while(is_at_end);
	int			   dest_reg = prog.get_columns(cursor, 0, cctx.layout.count());
	prog.result(dest_reg, cctx.layout.count());
	prog.next(cursor, is_at_end);
	prog.end_while(while_context);
	prog.close_cursor(cursor);
	prog.halt();
	prog.resolve_labels();

	vm_execute(prog.instructions.data, prog.instructions.size);

	// Restore normal callback
	vm_set_result_callback(print_result_callback);
}

void
reload_catalog()
{
	catalog.clear();
	bootstrap_master(false);
	load_catalog_from_master();
}

// SQL-based table creation and data loading
inline void
create_all_tables_sql(bool create)
{
	printf("=== Creating tables using SQL CREATE TABLE statements ===\n\n");

	if (!create)
	{
		printf("Tables already exist, skipping creation\n");
		return;
	}

	// Users table
	const char *create_users_sql = "CREATE TABLE users ("
								   "user_id U32 PRIMARY KEY, "
								   "username CHAR16, "
								   "email CHAR32, "
								   "age U32, "
								   "city CHAR16"
								   ");";

	printf("Creating users table...\n");
	if (!execute_sql_statement(create_users_sql))
	{
		printf("❌ Failed to create users table\n");
		return;
	}
	printf("✅ Users table created\n");

	// Products table
	const char *create_products_sql = "CREATE TABLE products ("
									  "product_id U32 PRIMARY KEY, "
									  "title CHAR32, "
									  "category CHAR16, "
									  "price U32, "
									  "stock U32, "
									  "brand CHAR16"
									  ");";

	printf("Creating products table...\n");
	if (!execute_sql_statement(create_products_sql))
	{
		printf("❌ Failed to create products table\n");
		return;
	}

	printf("✅ Products table created\n");

	// Orders table
	const char *create_orders_sql = "CREATE TABLE orders ("
									"order_id U32 PRIMARY KEY, "
									"user_id U32, "
									"total U32, "
									"total_quantity U32, "
									"discount U32"
									");";

	printf("Creating orders table...\n");
	if (!execute_sql_statement(create_orders_sql))
	{
		printf("❌ Failed to create orders table\n");
		return;
	}
	printf("✅ Orders table created\n");

	printf("\n✅ All tables created successfully using SQL!\n\n");
}

// Load individual table from CSV using SQL INSERT
inline void
load_table_from_csv_sql(const char *csv_file, const char *table_name)
{
	CSVReader				 reader(csv_file);
	std::vector<std::string> fields;

	printf("Loading %s from %s...\n", table_name, csv_file);

	int		  count = 0;
	int		  batch_count = 0;
	const int BATCH_SIZE = 50;

	// Get table structure for column names
	Structure *structure = catalog.get(table_name);
	if (!structure)
	{
		printf("❌ Table %s not found in catalog\n", table_name);
		return;
	}

	// Build column list
	string<query_arena> column_list;
	for (uint32_t i = 0; i < structure->columns.size; i++)
	{
		if (i > 0)
			column_list.append(", ");
		column_list.append(structure->columns[i].name);
	}

	// Process rows one by one
	while (reader.next_row(fields))
	{
		if (fields.size() != structure->columns.size)
		{
			printf("Warning: row has %zu fields, expected %zu\n", fields.size(), structure->columns.size);
			continue;
		}

		// Build INSERT statement
		string<query_arena> sql;
		sql.append("INSERT INTO ");
		sql.append(table_name);
		sql.append(" (");
		sql.append(column_list);
		sql.append(") VALUES (");

		// Add values with proper formatting
		for (size_t i = 0; i < fields.size(); i++)
		{
			if (i > 0)
				sql.append(", ");

			DataType col_type = structure->columns[i].type;

			if (type_is_numeric(col_type))
			{
				// Numbers don't need quotes
				sql.append(fields[i].c_str());
			}
			else if (type_is_string(col_type))
			{
				// Strings need quotes and basic escaping
				sql.append("'");
				for (char c : fields[i])
				{
					if (c == '\'')
					{
						sql.append("''"); // Escape single quotes
					}
					else
					{
						sql.append(&c, 1);
					}
				}
				sql.append("'");
			}
		}

		sql.append(");");

		// Execute the INSERT
		if (execute_sql_statement(sql.c_str()))
		{
			count++;
		}
		else
		{
			printf("❌ Failed to insert row %d\n", count + 1);
		}

		// Progress indicator
		if (++batch_count >= BATCH_SIZE)
		{
			printf("  Inserted %d rows...\n", count);
			batch_count = 0;
		}
	}

	printf("✅ Loaded %d records into %s\n", count, table_name);
}

// Load all CSV data using SQL
inline void
load_all_data_sql()
{
	printf("=== Loading data from CSV files using SQL INSERT ===\n\n");

	// Load in dependency order (no foreign keys to worry about for now)
	load_table_from_csv_sql("../users.csv", "users");
	load_table_from_csv_sql("../products.csv", "products");
	load_table_from_csv_sql("../orders.csv", "orders");

	printf("\n✅ All data loaded successfully using SQL pipeline!\n");
}

// Execute SQL statement through full pipeline
bool
execute_sql_statement(const char *sql, bool print_as)
{
	// 1. Parse

	bool							 in_transaction = false;
	array<Statement *, parser_arena> statements = *parse_sql(sql);

	for (auto &stmt : statements)
	{
		if (print_as)
		{
			print_ast(stmt);
		}

		// 2. Semantic analysis
		SemanticContext sem_ctx;
		if (!semantic_resolve_statement(stmt, &sem_ctx))
		{
			printf("❌ Semantic error in: %s\n", sql);
			for (uint32_t i = 0; i < sem_ctx.errors.size; i++)
			{
				printf("  Error: %s", sem_ctx.errors[i].message);
				if (sem_ctx.errors[i].context)
				{
					printf(" (%s)", sem_ctx.errors[i].context);
				}
				printf("\n");
			}

			if (in_transaction)
			{
				pager_rollback();
			}

			reload_catalog();
			return false;
		}

		if (stmt->type == STMT_BEGIN && !in_transaction)
		{
			in_transaction = true;
		}
		else if (stmt->type == STMT_COMMIT || stmt->type == STMT_ROLLBACK)
		{
			in_transaction = false;
		}

		// 3. Compile to VM bytecode
		array<VMInstruction, query_arena> program = compile_program(stmt, !in_transaction);
		if (program.size == 0)
		{
			printf("❌ Compilation failed: %s\n", sql);
			return false;
		}

		// 4. Execute on VM
		VM_RESULT result = vm_execute(program.data, program.size);

		if (result != OK)
		{
			printf("❌ Execution failed: %s\n", sql);
			return false;
		}
	}

	return true;
}
#include "tests/tests_parser.hpp"
// Updated main test function
int
main()
{

    test_parser();
	arena::init<query_arena>();
	bool existed = pager_open("relational_test.db");

	printf("=== Setting up relational database with SQL ===\n\n");

	// Create master catalog first

	if (!existed)
	{
		bootstrap_master(true);
		// Create tables using SQL
		create_all_tables_sql(true);

		// Load data using SQL
		load_all_data_sql();
	}
	else
	{
		reload_catalog();
		printf("Database already exists, skipping table creation and data loading\n");
		execute_sql_statement("SELECT * FROM users;", true);

		for (auto [a, b] : catalog)
		{
			std::cout << a.c_str() << b.storage.btree.root_page_index << "\n";
		}

		return 0;
	}

	// validation_end();

	// _debug = true;

	pager_close();

	main();

	printf("\n✅ All SQL tests completed!\n");
}
