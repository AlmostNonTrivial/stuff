#pragma once

#include "btree.hpp"
#include "common.hpp"
#include "types.hpp"
#include "vm.hpp"
#include "catalog.hpp"
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <sstream>

void
formatted_result_callback(TypedValue *result, size_t count);

// Simple CSV parser
struct CSVReader
{
	std::ifstream file;
	std::string	  line;

	CSVReader(const char *filename) : file(filename)
	{
		if (!file.is_open())
		{
			fprintf(stderr, "Failed to open CSV file: %s\n", filename);
		}
		// Skip header
		std::getline(file, line);
	}

	bool
	next_row(std::vector<std::string> &fields)
	{
		if (!std::getline(file, line))
		{
			return false;
		}

		// Remove carriage return if present
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}

		fields.clear();
		std::stringstream ss(line);
		std::string		  field;

		while (std::getline(ss, field, ','))
		{
			fields.push_back(field);
		}
		return true;
	}
};

bool
execute_sql_statement(const char *sql, bool asda = false);
inline void
create_all_tables_sql(bool create)
{
	if (!create)
	{
		return;
	}

	const char *create_users_sql = "CREATE TABLE users ("
								   "user_id INT, "
								   "username TEXT, "
								   "email TEXT, "
								   "age INT, "
								   "city TEXT"
								   ");";

	if (!execute_sql_statement(create_users_sql))
	{

		return;
	}


	const char *create_products_sql = "CREATE TABLE products ("
									  "product_id INT, "
									  "title TEXT, "
									  "category TEXT, "
									  "price INT, "
									  "stock INT, "
									  "brand TEXT"
									  ");";

	if (!execute_sql_statement(create_products_sql))
	{
		return;
	}

	const char *create_orders_sql = "CREATE TABLE orders ("
									"order_id INT, "
									"user_id INT, "
									"total INT, "
									"total_quantity INT, "
									"discount INT"
									");";

	if (!execute_sql_statement(create_orders_sql))
	{
		return;
	}
}

inline void
load_table_from_csv_sql(const char *csv_file, const char *table_name)
{
	CSVReader				 reader(csv_file);
	std::vector<std::string> fields;

	int		  count = 0;
	int		  batch_count = 0;
	const int BATCH_SIZE = 50;

	Relation *structure = catalog.get(table_name);
	if (!structure)
	{
		return;
	}

	auto column_list = stream_writer<query_arena>::begin();
	for (uint32_t i = 0; i < structure->columns.size(); i++)
	{
		if (i > 0)
		{
			column_list.write(", ");
		}

		column_list.write(structure->columns[i].name);
	}


	char* list = (char*)column_list.finish().data();
	while (reader.next_row(fields))
	{
		if (fields.size() != structure->columns.size())
		{
			printf("Warning: row has %zu fields, expected %zu\n", fields.size(), structure->columns.size());
			continue;
		}

		auto sql = stream_writer<query_arena>::begin();
		sql.write("INSERT INTO ");
		sql.write(table_name);
		sql.write(" (");
		sql.write(list);
		sql.write(") VALUES (");

		for (size_t i = 0; i < fields.size(); i++)
		{
			if (i > 0) {
				sql.write(", ");
			}


			DataType col_type = structure->columns[i].type;

			if (type_is_numeric(col_type))
			{
				sql.write(fields[i].c_str());
			}
			else if (type_is_string(col_type))
			{
				sql.write("'");
				for (char c : fields[i])
				{
					if (c == '\'')
					{
						sql.write("''", 1);
					}
					else
					{
						sql.write(&c, 1);
					}
				}
				sql.write("'", 1);
			}
		}

		sql.write(");");

		auto x = (char*)sql.finish().data();

		if (execute_sql_statement(x))
		{
			count++;
		}
		else
		{
			printf("❌ Failed to insert row %d\n", count + 1);
		}

		if (++batch_count >= BATCH_SIZE)
		{
			batch_count = 0;
		}
	}
}

inline void
load_all_data_sql()
{
	load_table_from_csv_sql("../users.csv", "users");
	load_table_from_csv_sql("../products.csv", "products");
	load_table_from_csv_sql("../orders.csv", "orders");
}

#include "arena.hpp"
#include "containers.hpp"
#include "catalog.hpp"
#include "compile.hpp"
#include "vm.hpp"
#include "blob.hpp"
#include "pager.hpp"
#include <chrono>
#include <cstring>
#include <cstdio>

// VM Function: LIKE pattern matching with % wildcard
bool
vmfunc_like(TypedValue *result, TypedValue *args, uint32_t arg_count)
{
	if (arg_count != 2)
		return false;

	const char *text = args[0].as_char();
	const char *pattern = args[1].as_char();

	// Simple % wildcard matching
	const char *t = text;
	const char *p = pattern;
	const char *star_t = nullptr;
	const char *star_p = nullptr;

	while (*t)
	{
		if (*p == '%')
		{
			star_p = p++;
			star_t = t;
			continue;
		}

		if (*p == *t)
		{
			p++;
			t++;
			continue;
		}

		if (star_p)
		{
			p = star_p + 1;
			t = ++star_t;
			continue;
		}

		// No match
		uint32_t match = 0;
		result->type = TYPE_U32;
		result->data = (uint8_t *)arena<query_arena>::alloc(sizeof(uint32_t));
		*(uint32_t *)result->data = match;
		return true;
	}

	// Skip trailing % in pattern
	while (*p == '%')
		p++;

	uint32_t match = (*p == '\0') ? 1 : 0;
	result->type = TYPE_U32;
	result->data = (uint8_t *)arena<query_arena>::alloc(sizeof(uint32_t));
	*(uint32_t *)result->data = match;
	return true;
}

// VM Function: Create structure (for composite index demo)
bool
vmfunc_create_index_structure(TypedValue *result, TypedValue *args, uint32_t arg_count)
{
	if (arg_count != 1)
		return false;

	const char *index_name = args[0].as_char();

	// Create composite index structure: (user_id, order_id) -> order_id
	array<Attribute, query_arena> columns;
	columns.push(Attribute{"key", make_dual(TYPE_U32, TYPE_U32)}); // Composite key

	// Relation index = Schema::from(index_name, columns);
	Relation	index = create_relation(index_name, columns);
	TupleFormat layout = tuple_format_from_relation(index);
	index.storage.btree = btree_create(layout.key_type, layout.record_size, false);

	// Add to catalog temporarily (will be rolled back)
	catalog.insert(index_name, index);

	result->type = TYPE_U32;
	result->data = (uint8_t *)arena<query_arena>::alloc(sizeof(uint32_t));
	*(uint32_t *)result->data = 1;
	return true;
}

// Demo 1: LIKE pattern matching
// Usage: .demo_like %Phone%
void
demo_like_pattern(const char *args)
{
	vm_set_result_callback(formatted_result_callback);
	// Parse pattern from args
	char pattern[64];
	if (args && *args)
	{
		strncpy(pattern, args, 63);
		pattern[63] = '\0';
	}
	else
	{
		strcpy(pattern, "%Phone%");
	}

	printf("\n=== LIKE Pattern Matching Demo ===\n");
	printf("Query: SELECT * FROM products WHERE title LIKE '%s'\n\n", pattern);

	ProgramBuilder prog;

	Relation *products = catalog.get("products");
	if (!products)
	{
		printf("Products table not found!\n");
		return;
	}

	auto products_ctx = from_structure(*products);
	int	 cursor = prog.open_cursor(products_ctx);

	// Load pattern into register
	// int pattern_reg = prog.load(TYPE_CHAR32, prog.alloc_string(pattern, 32));
	int pattern_reg = prog.load(prog.alloc_data_type(TYPE_CHAR32, pattern, 32));

	// Scan products
	int	 at_end = prog.first(cursor);
	auto loop = prog.begin_while(at_end);
	{
		prog.regs.push_scope();

		// Get title column (index 1)
		int title_reg = prog.get_column(cursor, 1);

		// Call LIKE: vmfunc_like(title, pattern)
		int args_start = prog.regs.allocate_range(2);
		prog.move(title_reg, args_start);
		prog.move(pattern_reg, args_start + 1);
		int match_reg = prog.call_function(vmfunc_like, args_start, 2);

		// If match, output row
		auto if_match = prog.begin_if(match_reg);
		{
			int row = prog.get_columns(cursor, 0, products->columns.size());
			prog.result(row, products->columns.size());
		}
		prog.end_if(if_match);

		prog.next(cursor, at_end);
		prog.regs.pop_scope();
	}
	prog.end_while(loop);

	prog.close_cursor(cursor);
	prog.halt();
	prog.resolve_labels();

	vm_execute(prog.instructions.front(), prog.instructions.size());
}

// Demo 2: Nested loop JOIN
// Usage: .demo_join [limit]
void
demo_nested_loop_join(const char *args)
{
	vm_set_result_callback(formatted_result_callback);
	int limit = 0; // 0 = no limit

	if (args && *args)
	{
		sscanf(args, "%d", &limit);
	}

	printf("\n=== Nested Loop JOIN Demo ===\n");
	printf("Query: SELECT username, city, order_id, total FROM users JOIN orders ON users.user_id = orders.user_id");
	if (limit > 0)
		printf(" LIMIT %d", limit);
	printf("\n\n");

	ProgramBuilder prog;

	Relation *users = catalog.get("users");
	Relation *orders = catalog.get("orders");
	if (!users || !orders)
	{
		printf("Required tables not found!\n");
		return;
	}

	auto users_ctx = from_structure(*users);
	auto orders_ctx = from_structure(*orders);

	int users_cursor = prog.open_cursor(users_ctx);
	int orders_cursor = prog.open_cursor(orders_ctx);

	// Counter for LIMIT
	// int count_reg = prog.load(TYPE_U32, prog.alloc_value(0U));
	uint32_t x = 0U;
	int		 count_reg = prog.load(prog.alloc_data_type(TYPE_U32, &x));

	// int limit_reg = prog.load(TYPE_U32, prog.alloc_value((uint32_t)limit));
	int limit_reg = prog.load(prog.alloc_data_type(TYPE_U32, &limit));
	// int one_reg = prog.load(TYPE_U32, prog.alloc_value(1U));
	uint32_t xx = 1U;
	int		 one_reg = prog.load(prog.alloc_data_type(TYPE_U32, &xx));

	// Outer loop: scan users
	int	 at_end_users = prog.first(users_cursor);
	auto outer_loop = prog.begin_while(at_end_users);
	{
		prog.regs.push_scope();
		int user_id = prog.get_column(users_cursor, 0);

		// Inner loop: scan ALL orders
		int	 at_end_orders = prog.first(orders_cursor);
		auto inner_loop = prog.begin_while(at_end_orders);
		{
			prog.regs.push_scope();

			// Check limit if specified
			if (limit > 0)
			{
				int limit_reached = prog.ge(count_reg, limit_reg);
				prog.jumpif_true(limit_reached, "done");
			}

			int order_user_id = prog.get_column(orders_cursor, 1);
			int match = prog.eq(user_id, order_user_id);

			auto if_match = prog.begin_if(match);
			{
				// Build result row
				int result_start = prog.regs.allocate_range(4);
				int username = prog.get_column(users_cursor, 1);
				int city = prog.get_column(users_cursor, 4);
				int order_id = prog.get_column(orders_cursor, 0);
				int total = prog.get_column(orders_cursor, 2);

				prog.move(username, result_start);
				prog.move(city, result_start + 1);
				prog.move(order_id, result_start + 2);
				prog.move(total, result_start + 3);

				prog.result(result_start, 4);

				if (limit > 0)
				{
					prog.add(count_reg, one_reg, count_reg);
				}
			}
			prog.end_if(if_match);

			prog.next(orders_cursor, at_end_orders);
			prog.regs.pop_scope();
		}
		prog.end_while(inner_loop);

		prog.next(users_cursor, at_end_users);
		prog.regs.pop_scope();
	}
	prog.end_while(outer_loop);

	prog.label("done");
	prog.close_cursor(users_cursor);
	prog.close_cursor(orders_cursor);
	prog.halt();
	prog.resolve_labels();

	vm_execute(prog.instructions.front(), prog.instructions.size());
}

// Demo 3: Subquery pattern
// Usage: .demo_subquery [age] [city]
void
demo_subquery_pattern(const char *args)
{
	vm_set_result_callback(formatted_result_callback);
	int	 age = 30;
	char city[32] = "Chicago";

	if (args && *args)
	{
		char temp_city[32];
		int	 parsed = sscanf(args, "%d %31s", &age, temp_city);
		if (parsed == 2)
		{
			strcpy(city, temp_city);
		}
		else if (parsed == 1)
		{
			// Just age provided, use default city
		}
	}

	printf("\n=== Subquery Pattern Demo ===\n");
	printf("Query: SELECT * FROM (SELECT * FROM users WHERE age > %d) WHERE city = '%s'\n\n", age, city);

	ProgramBuilder prog;

	Relation *users = catalog.get("users");
	if (!users)
	{
		printf("Users table not found!\n");
		return;
	}

	auto		users_ctx = from_structure(*users);
	TupleFormat temp_layout = users_ctx->layout;
	auto		temp_ctx = red_black(temp_layout);

	int users_cursor = prog.open_cursor(users_ctx);
	int temp_cursor = prog.open_cursor(temp_ctx);

	// Phase 1: Materialize subquery (age > threshold) into temp tree
	{
		prog.regs.push_scope();
		// int age_const = prog.load(TYPE_U32, prog.alloc_value((uint32_t)age));
		int age_const = prog.load(prog.alloc_data_type(TYPE_U32, &age));

		int	 at_end = prog.first(users_cursor);
		auto scan_loop = prog.begin_while(at_end);
		{
			prog.regs.push_scope();
			int age_reg = prog.get_column(users_cursor, 3);
			int age_test = prog.gt(age_reg, age_const);

			auto if_ctx = prog.begin_if(age_test);
			{
				int row_start = prog.get_columns(users_cursor, 0, users->columns.size());
				prog.insert_record(temp_cursor, row_start, users->columns.size());
			}
			prog.end_if(if_ctx);

			prog.next(users_cursor, at_end);
			prog.regs.pop_scope();
		}
		prog.end_while(scan_loop);
		prog.regs.pop_scope();
	}

	// Phase 2: Scan temp tree and filter by city
	{
		prog.regs.push_scope();
		// int city_const = prog.load(TYPE_CHAR16, prog.alloc_string(city, 16));
		int city_const = prog.load(prog.alloc_data_type(TYPE_CHAR32, city, 16));

		int	 at_end = prog.first(temp_cursor);
		auto scan_loop = prog.begin_while(at_end);
		{
			prog.regs.push_scope();
			int city_reg = prog.get_column(temp_cursor, 4);
			int city_test = prog.eq(city_reg, city_const);

			auto if_ctx = prog.begin_if(city_test);
			{
				int row_start = prog.get_columns(temp_cursor, 0, users->columns.size());
				prog.result(row_start, users->columns.size());
			}
			prog.end_if(if_ctx);

			prog.next(temp_cursor, at_end);
			prog.regs.pop_scope();
		}
		prog.end_while(scan_loop);
		prog.regs.pop_scope();
	}

	prog.close_cursor(users_cursor);
	prog.close_cursor(temp_cursor);
	prog.halt();
	prog.resolve_labels();

	vm_execute(prog.instructions.front(), prog.instructions.size());
}

// Demo 4: Composite index performance comparison
// Usage: .demo_index [user_id] [min_order_id]
void
demo_composite_index(const char *args)
{
	vm_set_result_callback(formatted_result_callback);
	int user_id = 11;
	int min_order_id = 5;

	if (args && *args)
	{
		sscanf(args, "%d %d", &user_id, &min_order_id);
	}

	printf("\n=== Composite Index Performance Demo ===\n");
	printf("Query: Find orders for user_id = %d where order_id > %d\n\n", user_id, min_order_id);

	// First, run query WITHOUT index (table scan)
	printf("1. Without index (table scan):\n");
	auto start = std::chrono::high_resolution_clock::now();

	{
		ProgramBuilder prog;
		Relation	  *orders = catalog.get("orders");
		if (!orders)
		{
			printf("Orders table not found!\n");
			return;
		}

		auto orders_ctx = from_structure(*orders);
		int	 cursor = prog.open_cursor(orders_ctx);

		// int target_user = prog.load(TYPE_U32, prog.alloc_value((uint32_t)user_id));
		int target_user = prog.load(prog.alloc_data_type(TYPE_U32, &user_id));
		// int threshold = prog.load(TYPE_U32, prog.alloc_value((uint32_t)min_order_id));
		int threshold = prog.load(prog.alloc_data_type(TYPE_U32, &min_order_id));

		int	 at_end = prog.first(cursor);
		auto loop = prog.begin_while(at_end);
		{
			prog.regs.push_scope();
			int curr_user = prog.get_column(cursor, 1);
			int curr_order = prog.get_column(cursor, 0);

			int user_match = prog.eq(curr_user, target_user);
			int order_check = prog.gt(curr_order, threshold);
			int both = prog.logic_and(user_match, order_check);

			auto if_match = prog.begin_if(both);
			{
				int row = prog.get_columns(cursor, 0, orders->columns.size());
				prog.result(row, orders->columns.size());
			}
			prog.end_if(if_match);

			prog.next(cursor, at_end);
			prog.regs.pop_scope();
		}
		prog.end_while(loop);

		prog.close_cursor(cursor);
		prog.halt();
		prog.resolve_labels();

		vm_execute(prog.instructions.front(), prog.instructions.size());
	}

	auto end = std::chrono::high_resolution_clock::now();
	auto ms_without = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
	printf("Time: %ld microseconds\n\n", ms_without);

	// Now create index and run with index
	printf("2. Creating composite index on (user_id, order_id)...\n");

	// Begin transaction so we can rollback
	pager_begin_transaction();

	// Create temporary index structure directly (not in catalog)
	DataType composite_key_type = make_dual(TYPE_U32, TYPE_U32);
	btree	 index_btree = btree_create(composite_key_type, 0, true); // No record, just key

	// Populate index from orders table
	{
		ProgramBuilder prog;

		Relation *orders = catalog.get("orders");
		auto	  orders_ctx = from_structure(*orders);

		// Create a context for the temporary index
		CursorContext index_context;
		index_context.type = BPLUS;
		index_context.storage.tree = &index_btree;
		array<DataType, query_arena> index_types = {composite_key_type};

		index_context.layout = tuple_format_from_types(index_types);

		int orders_cursor = prog.open_cursor(orders_ctx);
		int index_cursor = prog.open_cursor(&index_context);

		int	 at_end = prog.first(orders_cursor);
		auto loop = prog.begin_while(at_end);
		{
			prog.regs.push_scope();
			int order_id = prog.get_column(orders_cursor, 0);
			int user_id_val = prog.get_column(orders_cursor, 1);

			// Create composite key
			int composite = prog.pack2(user_id_val, order_id);

			// Insert into index (just key, no record)
			prog.insert_record(index_cursor, composite, 1);

			prog.next(orders_cursor, at_end);
			prog.regs.pop_scope();
		}
		prog.end_while(loop);

		prog.close_cursor(orders_cursor);
		prog.close_cursor(index_cursor);
		prog.halt();
		prog.resolve_labels();

		vm_execute(prog.instructions.front(), prog.instructions.size());
	}

	printf("3. With composite index (range seek):\n");
	start = std::chrono::high_resolution_clock::now();

	{
		ProgramBuilder prog;

		// Use the temporary index
		CursorContext index_context;
		index_context.type = BPLUS;
		index_context.storage.tree = &index_btree;
		array<DataType, query_arena> index_types = {composite_key_type};
		index_context.layout = tuple_format_from_types(index_types);

		int cursor = prog.open_cursor(&index_context);

		prog.regs.push_scope();

		// int user_reg = prog.load(TYPE_U32, prog.alloc_value((uint32_t)user_id));
		int user_reg = prog.load(prog.alloc_data_type(TYPE_U32, &user_id));
		// int order_threshold = prog.load(TYPE_U32, prog.alloc_value((uint32_t)(min_order_id + 1)));
		auto x = min_order_id + 1;
		int	 order_threshold = prog.load(prog.alloc_data_type(TYPE_U32, &x));

		// Create composite seek key
		int seek_key = prog.pack2(user_reg, order_threshold);

		// Seek to first entry >= (user_id, min_order_id+1)
		int found = prog.seek(cursor, seek_key, GE);

		auto scan_loop = prog.begin_while(found);
		{
			prog.regs.push_scope();

			// Get and unpack composite key
			int composite = prog.get_column(cursor, 0);
			int unpacked_start = prog.regs.allocate_range(2);
			prog.unpack2(composite, unpacked_start);

			int current_user = unpacked_start;

			// Check if still same user
			int same_user = prog.eq(current_user, user_reg);

			auto if_match = prog.begin_if(same_user);
			{
				prog.result(unpacked_start, 2);
			}
			prog.end_if(if_match);

			// Stop if different user
			prog.jumpif_zero(same_user, "done");

			prog.next(cursor, found);
			prog.regs.pop_scope();
		}
		prog.end_while(scan_loop);

		prog.label("done");
		prog.regs.pop_scope();

		prog.close_cursor(cursor);
		prog.halt();
		prog.resolve_labels();

		vm_execute(prog.instructions.front(), prog.instructions.size());
	}

	end = std::chrono::high_resolution_clock::now();
	auto ms_with = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
	printf("Time: %ld microseconds\n\n", ms_with);

	printf("Performance improvement: %.2fx faster\n", (double)ms_without / ms_with);
	printf("Rolling back transaction to clean up temporary index pages...\n");

	// Rollback to clean up the btree pages
	pager_rollback();
}

// Demo 5: GROUP BY with aggregation
// Usage: .demo_group [show_avg]
void
demo_group_by_aggregate(const char *args)
{
	vm_set_result_callback(formatted_result_callback);
	bool show_avg = false;

	if (args && *args)
	{
		show_avg = (strcmp(args, "avg") == 0 || strcmp(args, "1") == 0);
	}

	printf("\n=== GROUP BY Aggregate Demo ===\n");
	if (show_avg)
	{
		printf("Query: SELECT city, COUNT(*), SUM(age), AVG(age) FROM users GROUP BY city\n\n");
	}
	else
	{
		printf("Query: SELECT city, COUNT(*), SUM(age) FROM users GROUP BY city\n\n");
	}

	ProgramBuilder prog;

	Relation *users = catalog.get("users");
	if (!users)
	{
		printf("Users table not found!\n");
		return;
	}

	// Create layout for aggregation tree
	array<DataType, query_arena> agg_types = {
		(TYPE_CHAR16), // city (key)
		(TYPE_U32),	   // count
		(TYPE_U32),	   // sum_age
	};
	TupleFormat agg_layout = tuple_format_from_types(agg_types);

	auto users_ctx = from_structure(*users);
	auto agg_ctx = red_black(agg_layout);

	int users_cursor = prog.open_cursor(users_ctx);
	int agg_cursor = prog.open_cursor(agg_ctx);

	// Phase 1: Scan users and build aggregates
	{
		prog.regs.push_scope();
		// int one_const = prog.load(TYPE_U32, prog.alloc_value(1U));
		auto ss = 1U;
		int	 one_const = prog.load(prog.alloc_data_type(TYPE_U32, &ss));

		int	 at_end = prog.first(users_cursor);
		auto scan_loop = prog.begin_while(at_end);
		{
			prog.regs.push_scope();

			int city_reg = prog.get_column(users_cursor, 4);
			int age_reg = prog.get_column(users_cursor, 3);

			// Try to find existing aggregate for this city
			int found = prog.seek(agg_cursor, city_reg, EQ);

			auto if_found = prog.begin_if(found);
			{
				// Update existing aggregate
				int cur_count = prog.get_column(agg_cursor, 1);
				int cur_sum = prog.get_column(agg_cursor, 2);

				int update_start = prog.regs.allocate_range(2);
				prog.add(cur_count, one_const, update_start);
				prog.add(cur_sum, age_reg, update_start + 1);

				prog.update_record(agg_cursor, update_start);
			}
			prog.begin_else(if_found);
			{
				// Insert new aggregate
				int insert_start = prog.regs.allocate_range(3);
				prog.move(city_reg, insert_start);
				prog.move(one_const, insert_start + 1);
				prog.move(age_reg, insert_start + 2);

				prog.insert_record(agg_cursor, insert_start, 3);
			}
			prog.end_if(if_found);

			prog.next(users_cursor, at_end);
			prog.regs.pop_scope();
		}
		prog.end_while(scan_loop);
		prog.regs.pop_scope();
	}

	// Phase 2: Output aggregated results
	{
		prog.regs.push_scope();

		int	 at_end = prog.first(agg_cursor);
		auto output_loop = prog.begin_while(at_end);
		{
			prog.regs.push_scope();

			if (show_avg)
			{
				// Calculate average and output city, count, sum, avg
				int city = prog.get_column(agg_cursor, 0);
				int count = prog.get_column(agg_cursor, 1);
				int sum = prog.get_column(agg_cursor, 2);
				int avg = prog.div(sum, count);

				int result_start = prog.regs.allocate_range(4);
				prog.move(city, result_start);
				prog.move(count, result_start + 1);
				prog.move(sum, result_start + 2);
				prog.move(avg, result_start + 3);

				prog.result(result_start, 4);
			}
			else
			{
				// Just output city, count, sum
				int result_start = prog.get_columns(agg_cursor, 0, 3);
				prog.result(result_start, 3);
			}

			prog.next(agg_cursor, at_end);
			prog.regs.pop_scope();
		}
		prog.end_while(output_loop);
		prog.regs.pop_scope();
	}

	prog.close_cursor(users_cursor);
	prog.close_cursor(agg_cursor);
	prog.halt();
	prog.resolve_labels();

	vm_execute(prog.instructions.front(), prog.instructions.size());
}

// VM Function for blob operations
bool
vmfunc_write_blob(TypedValue *result, TypedValue *args, uint32_t arg_count)
{
	if (arg_count != 2)
		return false;

	void	*data = (void *)args[0].as_u64();
	uint32_t size = args[1].as_u32();

	uint32_t index = blob_create(data, size);

	result->type = TYPE_U32;
	result->data = (uint8_t *)arena<query_arena>::alloc(sizeof(uint32_t));
	*(uint32_t *)result->data = index;
	return true;
}

bool
vmfunc_read_blob(TypedValue *result, TypedValue *args, uint32_t arg_count)
{
	if (arg_count != 1)
		return false;

	uint32_t page_idx = args[0].as_u32();

	size_t size;
	auto   data = blob_read_full(page_idx, &size);

	if (!size)
		return false;

	result->type = TYPE_VARCHAR(size);
	result->data = (uint8_t *)data;
	return true;
}

// Demo 6: BLOB storage
// Usage: .demo_blob [doc_id]
void
demo_blob_storage(const char *args)
{
	vm_set_result_callback(formatted_result_callback);
	int doc_id = pager_get_next();

	if (args && *args)
	{
		sscanf(args, "%d", &doc_id);
	}

	printf("\n=== BLOB Storage Demo ===\n");
	printf("Creating document with ID=%d and storing large content as BLOB\n\n", doc_id);

	// Create documents table if it doesn't exist
	if (!catalog.get("documents"))
	{
		array<Attribute, query_arena> columns = {{"doc_id", TYPE_U32}, {"title", TYPE_CHAR32}, {"blob_ref", TYPE_U32}};

		pager_begin_transaction();
		Relation	docs = create_relation("documents", columns);
		TupleFormat layout = tuple_format_from_relation(docs);
		docs.storage.btree = btree_create(layout.key_type, layout.record_size, true);
		catalog.insert("documents", docs);
	}

	ProgramBuilder prog;
	// prog.begin_transaction();

	Relation *docs = catalog.get("documents");
	auto	  docs_ctx = from_structure(*docs);
	int		  cursor = prog.open_cursor(docs_ctx);

	// Insert a document with blob
	{
		prog.regs.push_scope();

		// Large content to store as blob
		const char *large_content = "This is a very large document content that would be inefficient "
									"to store directly in the btree. Instead, we store it as a blob "
									"and keep only the page reference in the table. This allows us to "
									"handle documents of arbitrary size efficiently. The blob storage "
									"system manages overflow pages automatically, splitting large content "
									"across multiple pages as needed. This is similar to how production "
									"databases handle TEXT and BLOB columns.";

		// Write blob and get reference
		// int content_ptr = prog.load(TYPE_U64, prog.alloc_value((uint64_t)large_content));
		int content_ptr = prog.load(prog.alloc_data_type(TYPE_U64, large_content));
		// int content_size = prog.load(TYPE_U32, prog.alloc_value((uint32_t)strlen(large_content)));
		auto xxx = strlen(large_content);
		int	 content_size = prog.load(prog.alloc_data_type(TYPE_U32, &xxx));
		int	 blob_ref = prog.call_function(vmfunc_write_blob, content_ptr, 2);

		// Prepare document row
		int row_start = prog.regs.allocate_range(3);
		// prog.load(TYPE_U32, prog.alloc_value((uint32_t)doc_id), row_start);
		prog.load(prog.alloc_data_type(TYPE_U32, &doc_id), row_start);
		// prog.load(TYPE_CHAR32, prog.alloc_string("Technical Manual", 32), row_start + 1);
		auto s = "Technical Manual";
		auto sl = strlen(s);
		prog.load(prog.alloc_data_type(TYPE_CHAR32, s, sl), row_start + 1);
		prog.move(blob_ref, row_start + 2);

		prog.insert_record(cursor, row_start, 3);

		printf("Inserted document: doc_id=%d, blob_ref=", doc_id);
		prog.result(row_start + 2, 1);
		prog.regs.pop_scope();
	}

	// Retrieve and read the blob
	printf("\nRetrieving document and reading BLOB content...\n");
	{
		prog.regs.push_scope();

		// int search_key = prog.load(TYPE_U32, prog.alloc_value((uint32_t)doc_id));
		int search_key = prog.load(prog.alloc_data_type(TYPE_U32, &doc_id));
		int found = prog.seek(cursor, search_key, EQ);

		auto if_found = prog.begin_if(found);
		{
			int doc_id_col = prog.get_column(cursor, 0);
			int title_col = prog.get_column(cursor, 1);
			int blob_ref_col = prog.get_column(cursor, 2);

			// Read blob content
			int blob_content = prog.call_function(vmfunc_read_blob, blob_ref_col, 1);

			// printf("Document found:\n");
			int result_start = prog.regs.allocate_range(4);
			prog.move(doc_id_col, result_start);
			prog.move(title_col, result_start + 1);
			prog.move(blob_ref_col, result_start + 2);
			prog.move(blob_content, result_start + 3);

			prog.result(result_start, 4);
		}
		prog.end_if(if_found);

		prog.regs.pop_scope();
	}

	prog.close_cursor(cursor);
	prog.commit_transaction();
	prog.halt();
	prog.resolve_labels();

	vm_execute(prog.instructions.front(), prog.instructions.size());
}
