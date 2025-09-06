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

void
load_catalog_from_master()
{
	// Set the callback
	vm_set_result_callback(catalog_bootstrap_callback);

	// Run your existing master table scan
	ProgramBuilder prog = {};
	auto		   cctx = from_structure(catalog[MASTER_CATALOG]);
	int			   cursor = prog.open_cursor(cctx);
	int			   is_at_end = prog.rewind(cursor, false);
	auto		   while_context = prog.begin_while(is_at_end);
	int			   dest_reg = prog.get_columns(cursor, 0, cctx->layout.count());
	prog.result(dest_reg, cctx->layout.count());
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

	if (!create)
	{
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

	// _debug = true;
	if (!execute_sql_statement(create_users_sql))
	{

		return;
	}

	// Products table
	const char *create_products_sql = "CREATE TABLE products ("
									  "product_id U32 PRIMARY KEY, "
									  "title CHAR32, "
									  "category CHAR16, "
									  "price U32, "
									  "stock U32, "
									  "brand CHAR16"
									  ");";

	if (!execute_sql_statement(create_products_sql))
	{

		return;
	}

	// Orders table
	const char *create_orders_sql = "CREATE TABLE orders ("
									"order_id U32 PRIMARY KEY, "
									"user_id U32, "
									"total U32, "
									"total_quantity U32, "
									"discount U32"
									");";

	if (!execute_sql_statement(create_orders_sql))
	{

		return;
	}
}

// Load individual table from CSV using SQL INSERT
inline void
load_table_from_csv_sql(const char *csv_file, const char *table_name)
{
	CSVReader				 reader(csv_file);
	std::vector<std::string> fields;

	int		  count = 0;
	int		  batch_count = 0;
	const int BATCH_SIZE = 50;

	// Get table structure for column names
	Structure *structure = catalog.get(table_name);
	if (!structure)
	{

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

			batch_count = 0;
		}
	}
}

// Load all CSV data using SQL
inline void
load_all_data_sql()
{

	// Load in dependency order (no foreign keys to worry about for now)
	load_table_from_csv_sql("../users.csv", "users");
	load_table_from_csv_sql("../products.csv", "products");
	load_table_from_csv_sql("../orders.csv", "orders");
}
// Simple helper to determine column width based on type
int
get_column_width(DataType type)
{
	switch (type)
	{
	case TYPE_U8:
	case TYPE_U16:
	case TYPE_U32:
	case TYPE_I8:
	case TYPE_I16:
	case TYPE_I32:
		return 10;
	case TYPE_U64:
	case TYPE_I64:
		return 15;
	case TYPE_F32:
	case TYPE_F64:
		return 12;
	case TYPE_CHAR8:
		return 10;
	case TYPE_CHAR16:
		return 18;
	case TYPE_CHAR32:
		return 35; // For emails and such
	case TYPE_CHAR64:
		return 35;
	case TYPE_CHAR128:
		return 40;
	case TYPE_CHAR256:
		return 50;
	default:
		return 15;
	}
}

// Print column headers for SELECT statements
void
print_select_headers(SelectStmt *select_stmt)
{
	if (!select_stmt->sem.is_resolved)
	{
		return;
	}

	Structure *table = select_stmt->from_table->sem.resolved;
	if (!table)
	{
		return;
	}

	// Check if SELECT *
	bool is_select_star = (select_stmt->select_list.size == 1 && select_stmt->select_list[0]->type == EXPR_STAR);

	printf("\n");

	if (is_select_star)
	{
		// Print all column names from table
		for (uint32_t i = 0; i < table->columns.size; i++)
		{
			int width = get_column_width(table->columns[i].type);
			printf("%-*s  ", width, table->columns[i].name);
		}
		printf("\n");

		// Print separator line
		for (uint32_t i = 0; i < table->columns.size; i++)
		{
			int width = get_column_width(table->columns[i].type);
			for (int j = 0; j < width; j++)
				printf("-");
			printf("  ");
		}
	}
	else
	{
		// Print specific column names from select list
		for (uint32_t i = 0; i < select_stmt->select_list.size; i++)
		{
			Expr	   *expr = select_stmt->select_list[i];
			const char *name = "expr";
			DataType	type = TYPE_CHAR32; // default

			if (expr->type == EXPR_COLUMN)
			{
				name = table->columns[expr->sem.column_index].name;
				type = table->columns[expr->sem.column_index].type;
			}
			else if (expr->type == EXPR_FUNCTION)
			{
				name = expr->func_name.c_str();
			}

			int width = get_column_width(type);
			printf("%-*s  ", width, name);
		}
		printf("\n");

		// Print separator line
		for (uint32_t i = 0; i < select_stmt->select_list.size; i++)
		{
			Expr	*expr = select_stmt->select_list[i];
			DataType type = TYPE_CHAR32;

			if (expr->type == EXPR_COLUMN)
			{
				type = table->columns[expr->sem.column_index].type;
			}

			int width = get_column_width(type);
			for (int j = 0; j < width; j++)
				printf("-");
			printf("  ");
		}
	}

	printf("\n");
}

// Store column widths for result formatting
static array<int, query_arena> result_column_widths;

// Store column info for current SELECT
void
setup_result_formatting(SelectStmt *select_stmt)
{
	result_column_widths.clear();

	if (!select_stmt->sem.is_resolved)
	{
		return;
	}

	Structure *table = select_stmt->from_table->sem.resolved;
	if (!table)
	{
		return;
	}

	bool is_select_star = (select_stmt->select_list.size == 1 && select_stmt->select_list[0]->type == EXPR_STAR);

	if (is_select_star)
	{
		for (uint32_t i = 0; i < table->columns.size; i++)
		{
			result_column_widths.push(get_column_width(table->columns[i].type));
		}
	}
	else
	{
		for (uint32_t i = 0; i < select_stmt->select_list.size; i++)
		{
			Expr	*expr = select_stmt->select_list[i];
			DataType type = TYPE_CHAR32;

			if (expr->type == EXPR_COLUMN)
			{
				type = table->columns[expr->sem.column_index].type;
			}

			result_column_widths.push(get_column_width(type));
		}
	}
}

// Enhanced result callback that formats output nicely
void
formatted_result_callback(TypedValue *result, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		int width = (i < result_column_widths.size) ? result_column_widths[i] : 15;

		// Format based on type
		switch (result[i].type)
		{
		case TYPE_U32:
		case TYPE_U64:
		case TYPE_U16:
		case TYPE_U8:
			printf("%-*llu  ", width, result[i].as_u64());
			break;

		case TYPE_I32:
		case TYPE_I64:
		case TYPE_I16:
		case TYPE_I8:
			printf("%-*lld  ", width, result[i].as_i64());
			break;

		case TYPE_F32:
		case TYPE_F64:
			printf("%-*.2f  ", width, result[i].as_f64());
			break;

		case TYPE_CHAR8:
		case TYPE_CHAR16:
		case TYPE_CHAR32:
		case TYPE_CHAR64:
		case TYPE_CHAR128:
		case TYPE_CHAR256: {
			const char *str = result[i].as_char();
			printf("%-*s  ", width, str ? str : "NULL");
			break;
		}

		case TYPE_NULL:
			printf("%-*s  ", width, "NULL");
			break;

		default:
			printf("%-*s  ", width, "???");
		}
	}
	printf("\n");
}

// Execute SQL statement through full pipeline
bool
execute_sql_statement(const char *sql, bool test_mode)
{
	// 1. Parse
	bool							 in_transaction = false;
	array<Statement *, parser_arena> statements = *parse_sql(sql);

	for (auto &stmt : statements)
	{

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
			// reload_catalog();
			return false;
		}

		// Track transaction state
		if (stmt->type == STMT_BEGIN && !in_transaction)
		{
			in_transaction = true;
		}
		else if (stmt->type == STMT_COMMIT || stmt->type == STMT_ROLLBACK)
		{
			in_transaction = false;
		}

		if (!test_mode)
		{

			// 2.5 For SELECT statements, print column headers and setup formatting
			if (stmt->type == STMT_SELECT)
			{
				print_select_headers(stmt->select_stmt);
				setup_result_formatting(stmt->select_stmt);
				vm_set_result_callback(formatted_result_callback);
			}
			else
			{
				// Use default print callback for non-SELECT statements
				vm_set_result_callback(print_result_callback);
			}
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

		// Add a blank line after SELECT results
		if (stmt->type == STMT_SELECT)
		{
			printf("\n");
		}
	}
	return true;
}

// Test core SELECT features
void
test_select_features()
{

	// Test 1: SELECT * - should return all columns from users table
	printf("\n1. Testing SELECT * (first 3 users):\n");
	validation_begin(false);

	expect_row_values({alloc_u32(1), alloc_char16("emilys"), alloc_char32("emily.johnson@x.dummyjson.com"),
					   alloc_u32(28), alloc_char16("Phoenix")});

	expect_row_values({alloc_u32(2), alloc_char16("michaelw"), alloc_char32("michael.williams@x.dummyjson.com"),
					   alloc_u32(35), alloc_char16("Houston")});

	expect_row_values({alloc_u32(3), alloc_char16("sophiab"), alloc_char32("sophia.brown@x.dummyjson.com"),
					   alloc_u32(42), alloc_char16("Houston")});

	execute_sql_statement("SELECT * FROM users;", true);
	validation_end();

	// Test 2: SELECT specific columns
	printf("\n2. Testing SELECT specific columns:\n");
	validation_begin(true);

	expect_row_values({alloc_char16("emilys"), alloc_char16("Phoenix")});
	expect_row_values({alloc_char16("michaelw"), alloc_char16("Houston")});
	expect_row_values({alloc_char16("sophiab"), alloc_char16("Houston")});

	execute_sql_statement("SELECT username, city FROM users WHERE user_id <= 3;", true);
	validation_end();

	// Test 3: SELECT DISTINCT
	printf("\n3. Testing SELECT DISTINCT:\n");
	// Insert some duplicate cities for testing

	validation_begin(true); // Order may vary from red-black tree

	// // Should get unique cities only
	expect_row_values({alloc_char16("Houston")});
	expect_row_values({alloc_char16("Phoenix")});

	execute_sql_statement("SELECT DISTINCT city FROM users WHERE user_id <= 3 OR user_id > 100;", true);
	validation_end();
}

// Test GROUP BY and aggregates
void
test_group_by_aggregates()
{
    printf("\n=== Testing GROUP BY and Aggregates ===\n");
    // _debug = true;
    // Test 1: Simple GROUP BY with COUNT(*)
    printf("\n1. Testing GROUP BY city with COUNT(*):\n");
    // validation_begin(false);  // Order may vary from red-black tree

    // Based on the data, we should get counts per city
    // Houston appears multiple times, Phoenix, etc.
    // expect_row_values({alloc_char16("Houston"), alloc_u32(2)});  // Houston has 2 users in first 3
    // expect_row_values({alloc_char16("Phoenix"), alloc_u32(1)});  // Phoenix has 1

    execute_sql_statement("SELECT COUNT(*) FROM users;", false);
    // validation_end();

    return;
    // Test 2: GROUP BY with SUM
    printf("\n2. Testing GROUP BY with SUM(age):\n");
    validation_begin(false);

    expect_row_values({alloc_char16("Houston"), alloc_u32(77)});  // 35 + 42
    expect_row_values({alloc_char16("Phoenix"), alloc_u32(28)});  // 28

    execute_sql_statement("SELECT city, SUM(age) FROM users WHERE user_id <= 3 GROUP BY city;", true);
    validation_end();

    // Test 3: Multiple aggregates
    printf("\n3. Testing multiple aggregates:\n");
    validation_begin(false);

    expect_row_values({alloc_char16("Houston"), alloc_u32(2), alloc_u32(77), alloc_u32(35)});  // count, sum, min
    expect_row_values({alloc_char16("Phoenix"), alloc_u32(1), alloc_u32(28), alloc_u32(28)});

    execute_sql_statement("SELECT city, COUNT(*), SUM(age), MIN(age) FROM users WHERE user_id <= 3 GROUP BY city;", true);
    validation_end();
}

int
main()
{
	arena::init<query_arena>();
	bool existed = pager_open("relational_test_.db");

	if (!existed)
	{
		bootstrap_master(true);
		// Create tables using SQL
		create_all_tables_sql(true);
		// execute_sql_statement("CREATE UNIQUE INDEX idx_users_username ON users (username);");
		// Load data using SQL
		load_all_data_sql();
	}
	else
	{
		reload_catalog();
	}

	// Run the SELECT feature tests

	// test_select_features();
	test_group_by_aggregates();

	pager_close();
}
