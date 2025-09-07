
#pragma once
#include "../types.hpp"
#include <stdio.h>
#include "../blob.hpp"
#include "../btree.hpp"
#include "../catalog.hpp"
#include "../compile.hpp"
#include "../common.hpp"
#include "../os_layer.hpp"
#include "../pager.hpp"
#include "../types.hpp"
#include "../vm.hpp"
#include "../btree.hpp"
#include <cstdint>
#include <iostream>
#include <fstream>
#include <sstream>

#include "../types.hpp"
#include <cstring>

#include <cstdint>

// Table definitions matching the CSV structure
#define USERS	 "users"
#define USER_ID	 "user_id"
#define USERNAME "username"
#define EMAIL	 "email"
#define USER_AGE "age"
#define CITY	 "city"

#define PRODUCTS   "products"
#define PRODUCT_ID "product_id"
#define TITLE	   "title"
#define CATEGORY   "category"
#define PRICE	   "price"
#define STOCK	   "stock"
#define BRAND	   "brand"

#define ORDERS_BY_USER "idx_orders_by_user"
#define INDEX_KEY	   "key"

#define ORDERS		   "orders"
#define ORDER_ID	   "order_id"
#define TOTAL		   "total"
#define TOTAL_QUANTITY "total_quantity"
#define DISCOUNT	   "discount"

#define ORDER_ITEMS "order_items"
#define ITEM_ID		"item_id"

#define POSTS	  "posts"
#define POST_ID	  "post_id"
#define VIEWS	  "views"
#define REACTIONS "reactions"

#define COMMENTS   "comments"
#define COMMENT_ID "comment_id"
#define BODY	   "body"
#define LIKES	   "likes"

#define TAGS	 "tags"
#define TAG_ID	 "tag_id"
#define TAG_NAME "tag_name"

#define POST_TAGS "post_tags"

#define USER_FOLLOWERS "user_followers"
#define FOLLOWER_ID	   "follower_id"
#define FOLLOWED_ID	   "followed_id"




// VM Function: LIKE pattern matching with % wildcard
// Args: [0] = text (CHAR32), [1] = pattern (CHAR32)
// Result: U32 (1 = match, 0 = no match)
bool
vmfunc_like(TypedValue *result, TypedValue *args, uint32_t arg_count)
{
	if (arg_count != 2)
		return false;

	const char *pattern = args[0].as_char();
	const char *text = args[1].as_char();

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

		return false;
	}

	while (*p == '%')
		p++;

	uint32_t match = (*p == '\0') ? 1 : 0;
	result->type = TYPE_U32;
	result->data = (uint8_t *)arena::alloc<query_arena>(sizeof(uint32_t));
	*(uint32_t *)result->data = match;

	return true;
}

// Single program: Find products where title LIKE '%Phone%'
void
test_like_pattern()
{
	printf("\n=== LIKE Pattern Demo: SELECT * FROM products WHERE title LIKE '%%Ess%%' ===\n\n");

	ProgramBuilder prog;

	// Open products cursor
	auto products_ctx = from_structure(catalog[PRODUCTS]);
	prog.open_cursor(&products_ctx);

	// Load pattern "%Phone%" into register
	int pattern_reg = prog.load(TYPE_CHAR32, prog.alloc_string("%Ess%", 32));
	int title_reg = prog.regs.allocate();

	// Scan products
	int	 at_end = prog.first(0);
	auto loop = prog.begin_while(at_end);
	{
		// Get title column (index 1)
		prog.get_column(0, 1, title_reg);

		// Call LIKE: vmfunc_like(title, pattern)
		int match_reg = prog.call_function(vmfunc_like, pattern_reg, 2);

		// If match, output row
		auto if_match = prog.begin_if(match_reg);
		{
			int row = prog.get_columns(0, 0, 6);
			prog.result(row, 6);
		}
		prog.end_if(if_match);

		prog.next(0, at_end);
	}
	prog.end_while(loop);

	prog.close_cursor(0);
	prog.halt();

	prog.resolve_labels();
	vm_execute(prog.instructions.data, prog.instructions.size);
}

inline void
test_select()
{
	ProgramBuilder prog;
	int			   cursor = 0;
	auto		   cctx = from_structure(catalog[USERS]);
	cursor = prog.open_cursor(&cctx);
	int	 is_at_end = prog.rewind(cursor, false);
	auto while_context = prog.begin_while(is_at_end);
	int	 dest_reg = prog.get_columns(cursor, 0, cctx.layout.count());
	prog.result(dest_reg, cctx.layout.count());
	prog.next(cursor, is_at_end);
	prog.end_while(while_context);
	prog.close_cursor(cursor);
	prog.halt();
	prog.resolve_labels();

	vm_execute(prog.instructions.data, prog.instructions.size);
}

inline void
test_select_order_by()
{
	ProgramBuilder prog;
	int			   cursor;
	int			   memcursor;
	auto		   cctx = from_structure(catalog[USERS]);
	Layout		   sorted_by_age = cctx.layout.reorder({3, 0, 1, 2, 4});
	auto		   mem = red_black(sorted_by_age);

	cursor = prog.open_cursor(&cctx);
	memcursor = prog.open_cursor(&mem);

	{
		prog.regs.push_scope();
		int	 at_end = prog.first(cursor);
		auto while_context = prog.begin_while(at_end);
		int	 dest_reg = prog.regs.allocate();
		prog.get_column(cursor, 3, dest_reg);	  // age -> dest_reg
		prog.get_column(cursor, 0, dest_reg + 1); // user_id -> dest_reg+1
		prog.get_column(cursor, 1, dest_reg + 2); // username -> dest_reg+2
		prog.get_column(cursor, 2, dest_reg + 3); // email -> dest_reg+3
		prog.get_column(cursor, 4, dest_reg + 4); // city -> dest_reg+4
		prog.insert_record(memcursor, dest_reg, 5);
		prog.next(cursor, at_end);
		prog.end_while(while_context);
		prog.regs.pop_scope();
	}
	{
		prog.regs.push_scope();
		int	 at_end = prog.last(memcursor);
		auto while_ctx = prog.begin_while(at_end);
		int	 dest_reg = prog.get_columns(memcursor, 0, 5);
		prog.result(dest_reg, 5);
		prog.step(memcursor, at_end);
		prog.end_while(while_ctx);
		prog.regs.pop_scope();
	}

	prog.close_cursor(cursor);
	prog.close_cursor(memcursor);
	prog.halt();
	prog.resolve_labels();

	vm_execute(prog.instructions.data, prog.instructions.size);
}

inline void
test_nested_loop_join()
{
	printf("\n=== NESTED LOOP JOIN ===\n");
	printf(
		"Query: SELECT username, city, order_id, total FROM users JOIN orders ON users.user_id = orders.user_id\n\n");

	ProgramBuilder prog;

	auto users_ctx = from_structure(catalog[USERS]);
	auto orders_ctx = from_structure(catalog[ORDERS]);

	prog.open_cursor(&users_ctx);
	prog.open_cursor(&orders_ctx);

	// Outer loop: scan users
	{
		prog.regs.push_scope();

		int	 at_end_users = prog.first(0);
		auto outer_loop = prog.begin_while(at_end_users);
		{
			int user_id = prog.get_column(0, 0);

			// Inner loop: scan ALL orders
			int	 at_end_orders = prog.first(1);
			auto inner_loop = prog.begin_while(at_end_orders);
			{
				int order_user_id = prog.get_column(1, 1); // user_id is column 1 in orders
				int match = prog.eq(user_id, order_user_id);

				auto if_match = prog.begin_if(match);
				{
					// Output matched row
					int username = prog.get_column(0, 1);
					int city = prog.get_column(0, 4);
					int order_id = prog.get_column(1, 0);
					int total = prog.get_column(1, 2);

					prog.result(username, 4);
				}
				prog.end_if(if_match);

				prog.next(1, at_end_orders);
			}
			prog.end_while(inner_loop);

			prog.next(0, at_end_users);
		}
		prog.end_while(outer_loop);

		prog.regs.pop_scope();
	}

	prog.close_cursor(0);
	prog.close_cursor(1);
	prog.halt();

	prog.resolve_labels();
	vm_execute(prog.instructions.data, prog.instructions.size);
}

inline void
test_subquery_pattern()
{
	printf("\n=== SUBQUERY PATTERN DEMO ===\n");
	printf("Simulating: SELECT * FROM (SELECT * FROM users WHERE age > 30) WHERE city='Chicago'\n\n");

	ProgramBuilder prog;

	// Cursor 0: source table (users)
	// Cursor 1: temporary red-black tree for intermediate results

	auto users_ctx = from_structure(catalog[USERS]);

	// Create red-black tree with same layout as users table
	Layout temp_layout = users_ctx.layout;
	auto   temp_ctx = red_black(temp_layout);

	prog.open_cursor(&users_ctx);
	prog.open_cursor(&temp_ctx);

	// ========================================================================
	// PHASE 1: Scan users table, filter by age > 30, insert into temp tree
	// ========================================================================

	{
		prog.regs.push_scope();
		// Load constant 30 for age comparison
		int age_const = prog.load(TYPE_U32, prog.alloc_value(30U));

		int	 at_end = prog.first(0);
		auto scan_loop = prog.begin_while(at_end);
		{
			// Get age column (index 3)
			int age_reg = prog.get_column(0, 3);

			// Test if age > 30
			int age_test = prog.gt(age_reg, age_const);

			// If condition met, insert into temp tree
			auto if_ctx = prog.begin_if(age_test);
			{
				// Get all columns from current row
				int row_start = prog.get_columns(0, 0, 5);

				// Insert into red-black tree (cursor 1)
				prog.insert_record(1, row_start, 5);
			}
			prog.end_if(if_ctx);

			// Move to next record
			prog.next(0, at_end);
		}
		prog.end_while(scan_loop);

		prog.regs.pop_scope();
	}

	// ========================================================================
	// PHASE 2: Scan temp tree, filter by city = 'Chigaco', output results
	// ========================================================================

	{
		prog.regs.push_scope();

		// Load constant "Portland" for city comparison

		int city_const = prog.load(TYPE_CHAR16, prog.alloc_string("Chicago", type_size(TYPE_CHAR16)));

		// Rewind temp tree to start
		int at_end = prog.first(1);

		// Begin scanning loop
		auto scan_loop = prog.begin_while(at_end);
		{
			// Get city column (index 4)
			int city_reg = prog.get_column(1, 4);

			// Test if city == "Portland"
			int city_test = prog.eq(city_reg, city_const);

			// If condition met, output the row
			auto if_ctx = prog.begin_if(city_test);
			{
				// Get all columns from current row
				int row_start = prog.get_columns(1, 0, 5);

				// Output result
				prog.result(row_start, 5);
			}
			prog.end_if(if_ctx);

			// Move to next record
			prog.next(1, at_end);
		}
		prog.end_while(scan_loop);

		prog.regs.pop_scope();
	}

	// Cleanup
	prog.close_cursor(0);
	prog.close_cursor(1);
	prog.halt();

	// Resolve labels and execute
	prog.resolve_labels();

	printf("Executing subquery pattern...\n");
	vm_execute(prog.instructions.data, prog.instructions.size);
}

inline void
test_composite_index_range_query()
{
	printf("\n=== COMPOSITE INDEX RANGE QUERY ===\n");
	printf("Query: Find orders for user_id = 11 where order_id > 5\n\n");

	ProgramBuilder prog;

	auto index_ctx = from_structure(catalog[ORDERS_BY_USER]);
	prog.open_cursor(&index_ctx);

	{
		prog.regs.push_scope();

		// Target: user_id = 2, order_id > 5
		uint32_t target_user = 11;
		uint32_t min_order_id = 6; // > 5 means >= 6

		int user_reg = prog.load(TYPE_U32, prog.alloc_value(target_user));
		int order_threshold = prog.load(TYPE_U32, prog.alloc_value(min_order_id));

		// Create composite seek key: (2, 6)
		int seek_key = prog.pack2(user_reg, order_threshold);

		// Seek to first entry >= (2, 6)
		int found = prog.seek(0, seek_key, GE);

		auto scan_loop = prog.begin_while(found);
		{
			// Get and unpack composite key
			int composite = prog.get_column(0, 0);

			// Allocate space for unpacked values before unpacking
			int unpacked_start = prog.regs.allocate_range(2);
			prog.unpack2(composite, unpacked_start);

			// Now we know exactly where the values are
			int current_user = unpacked_start;
			int current_order = unpacked_start + 1;

			// Check if still same user
			int	 same_user = prog.eq(current_user, user_reg);
			auto if_ctx = prog.begin_if(same_user);
			prog.result(unpacked_start, 2);
			prog.end_if(if_ctx);
			prog.jumpif_zero(same_user, "done");

			prog.next(0, found);
		}
		prog.end_while(scan_loop);

		prog.label("done");
		prog.regs.pop_scope();
	}

	prog.close_cursor(0);
	prog.halt();

	prog.resolve_labels();
	vm_execute(prog.instructions.data, prog.instructions.size);
}

inline void
test_create_composite_index()
{
	printf("\n=== CREATING COMPOSITE INDEX ON ORDERS ===\n");
	printf("Index: idx_orders_by_user (user_id, order_id) -> order_id\n\n");

	catalog[ORDERS_BY_USER] = Structure::from(ORDERS_BY_USER, orders_by_user_index);
	ProgramBuilder prog;
	prog.begin_transaction();

	// Create index structure
	{
		prog.regs.push_scope();

		// Composite key type: DUAL(u32, u32)
		DataType composite_type = make_dual(TYPE_U32, TYPE_U32);

		int name_reg = prog.load(TYPE_CHAR32, prog.alloc_string(ORDERS_BY_USER, 32));
		int key_type_reg = prog.load(TYPE_U64, prog.alloc_value((uint64_t)composite_type));
		int record_size = prog.load(TYPE_U32, prog.alloc_value(0));
		int unique = prog.load(TYPE_U32, prog.alloc_value(0U)); // non-unique

		prog.call_function(vmfunc_create_structure, name_reg, 4);
		prog.regs.pop_scope();
	}

	// Populate from orders table
	auto orders_ctx = from_structure(catalog[ORDERS]);

	auto index_ctx = from_structure(catalog[ORDERS_BY_USER]);

	prog.open_cursor(&orders_ctx);
	prog.open_cursor(&index_ctx);

	{
		prog.regs.push_scope();

		int	 at_end = prog.first(0);
		auto scan = prog.begin_while(at_end);
		{
			// Extract columns
			int order_id = prog.get_column(0, 0);
			int user_id = prog.get_column(0, 1);

			// Create composite key
			int composite_key = prog.pack2(user_id, order_id);

			// Insert: composite_key -> order_id
			prog.insert_record(1, composite_key);

			prog.next(0, at_end);
		}
		prog.end_while(scan);

		prog.regs.pop_scope();
	}

	prog.close_cursor(0);
	prog.close_cursor(1);
	prog.commit_transaction();
	prog.halt();

	prog.resolve_labels();
	vm_execute(prog.instructions.data, prog.instructions.size);
}

inline void
test_group_by_aggregate()
{
	printf("\n=== GROUP BY AGGREGATE DEMO ===\n");
	printf("Query: SELECT city, COUNT(*), SUM(age) FROM users GROUP BY city\n\n");

	ProgramBuilder prog;

	// Create layout for aggregation tree:
	// Key: city (CHAR16)
	// Value: count (U32), sum_age (U32)
	std::vector<DataType> agg_types = {
		TYPE_CHAR16, // city (key)
		TYPE_U32,	 // count
		TYPE_U32	 // sum_age
	};
	Layout agg_layout = Layout::create(agg_types);

	auto users_ctx = from_structure(catalog[USERS]);
	auto agg_ctx = red_black(agg_layout);

	prog.open_cursor(&users_ctx); // Users table
	prog.open_cursor(&agg_ctx);	  // Aggregation tree

	// Phase 1: Scan users and build aggregates
	{
		prog.regs.push_scope();

		// Constants
		int one_const = prog.load(TYPE_U32, prog.alloc_value(1U));
		int zero_const = prog.load(TYPE_U32, prog.alloc_value(0U));

		int	 at_end = prog.first(0);
		auto scan_loop = prog.begin_while(at_end);
		{
			// Get city and age from current user
			int city_reg = prog.get_column(0, 4); // city column
			int age_reg = prog.get_column(0, 3);  // age column

			// Try to find existing aggregate for this city
			int found = prog.seek(1, city_reg, EQ);

			auto if_found = prog.begin_if(found);
			{
				// City exists - update aggregates
				int cur_count = prog.get_column(1, 1);
				int cur_sum = prog.get_column(1, 2);

				// Calculate new values in contiguous registers
				int update_start = prog.regs.allocate();
				prog.add(cur_count, one_const, update_start); // new_count -> update_start
				prog.add(cur_sum, age_reg, update_start + 1); // new_sum -> update_start + 1

				// Update the record (passes both count and sum)
				prog.update_record(1, update_start);
			}
			prog.begin_else(if_found);
			{
				// New city - insert with initial values
				// Need contiguous: city, count=1, sum=age
				int insert_start = prog.regs.allocate_range(3);
				prog.move(city_reg, insert_start);		// city
				prog.move(one_const, insert_start + 1); // count = 1
				prog.move(age_reg, insert_start + 2);	// sum = age

				prog.insert_record(1, insert_start, 3);
			}
			prog.end_if(if_found);

			prog.next(0, at_end);
		}
		prog.end_while(scan_loop);

		prog.regs.pop_scope();
	}

	// Phase 2: Output aggregated results
	{
		prog.regs.push_scope();

		int	 at_end = prog.first(1);
		auto output_loop = prog.begin_while(at_end);
		{
			// Get all aggregate columns in contiguous registers
			int result_start = prog.get_columns(1, 0, 3); // city, count, sum_age

			// Output: city, count, sum_age
			prog.result(result_start, 3);

			prog.next(1, at_end);
		}
		prog.end_while(output_loop);

		prog.regs.pop_scope();
	}

	prog.close_cursor(0);
	prog.close_cursor(1);
	prog.halt();

	prog.resolve_labels();
	vm_execute(prog.instructions.data, prog.instructions.size);
}

// Add these VM functions for blob operations
bool
vmfunc_write_blob(TypedValue *result, TypedValue *args, uint32_t arg_count)
{
	if (arg_count != 2)
		return false;

	// args[0] = data pointer, args[1] = size
	void	*data = (void *)args[0].as_u64(); // Assuming we pass pointer as u64
	uint32_t size = args[1].as_u32();

	uint32_t index = blob_create(data, size);

	result->type = TYPE_U32;
	result->data = arena::alloc<query_arena>(sizeof(uint32_t));
	*(uint32_t *)result->data = index;

	return true;
}

bool
vmfunc_read_blob(TypedValue *result, TypedValue *args, uint32_t arg_count)
{
	if (arg_count != 1)
	{
		return false;
	}

	uint32_t page_idx = args[0].as_u32();
	assert(0 != page_idx);
	uint64_t size;
	void	*data = blob_read_full(page_idx, &size);

	assert(size > 0);

	result->type = TYPE_VARCHAR(size);
	result->data = (uint8_t *)data;

	return true;
}
inline void
test_blob_storage()
{
	printf("\n=== BLOB STORAGE DEMO ===\n");
	printf("Creating documents table with blob references\n\n");

	// Define table structure and add to catalog FIRST
	std::vector<Column> documents = {
		Column{"doc_id", TYPE_U32}, Column{"title", TYPE_CHAR32}, Column{"blob_ref", TYPE_U32} // Stores blob page index
	};
	catalog["documents"] = Structure::from("documents", documents);

	ProgramBuilder prog;
	prog.begin_transaction();

	// Now create the btree for the structure that's already in the catalog
	prog.regs.push_scope();
	int name_reg = prog.load(TYPE_CHAR16, prog.alloc_string("documents", 16));
	prog.call_function(vmfunc_create_structure, name_reg, 1);
	prog.regs.pop_scope();

	// Open cursor to documents table
	auto docs_ctx = from_structure(catalog["documents"]);
	prog.open_cursor(&docs_ctx);

	// Insert a document with blob
	{
		prog.regs.push_scope();

		// Simulate large content to store as blob
		const char *large_content = "This is a very large document content that would be inefficient "
									"to store directly in the btree. Instead, we store it as a blob "
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."
									"and keep only the page reference in the table..."

			;

		// Write blob first and get page reference
		int content_ptr = prog.load(TYPE_U64, prog.alloc_value((uint64_t)large_content));
		int content_size = prog.load(TYPE_U32, prog.alloc_value((uint32_t)strlen(large_content)));
		int blob_ref = prog.call_function(vmfunc_write_blob, content_ptr, 2);

		// Now prepare row data in contiguous registers
		int row_start = prog.regs.allocate_range(3);

		// Load all three values in contiguous registers
		prog.load(TYPE_U32, prog.alloc_value(1U), row_start);							  // doc_id (key)
		prog.load(TYPE_CHAR32, prog.alloc_string("Technical Manual", 32), row_start + 1); // title
		prog.move(blob_ref, row_start + 2);												  // blob_ref

		// Insert row with all 3 values
		prog.insert_record(0, row_start, 3);

		printf("Inserted document with ID=1, blob_ref=");
		prog.result(row_start + 2, 1); // Output just the blob_ref

		prog.regs.pop_scope();
	}

	// Now retrieve and read the blob
	{
		prog.regs.push_scope();

		// Seek to our document
		int search_key = prog.load(TYPE_U32, prog.alloc_value(1U));
		int found = prog.first(0);
		{
			// Get all columns
			int doc_id = prog.get_column(0, 0);	  // doc_id
			int title = prog.get_column(0, 1);	  // title
			int blob_ref = prog.get_column(0, 2); // blob_ref

			// Read blob using the reference
			int blob_reg = prog.call_function(vmfunc_read_blob, blob_ref, 1);
			prog.result(blob_reg);

			// Output: doc_id, title, blob_ref, blob_size
			printf("Retrieved document:\n");
			prog.result(doc_id, 4); // Output starting from doc_id for 4 values
		}

		prog.regs.pop_scope();
	}

	prog.close_cursor(0);
	prog.commit_transaction();
	prog.halt();

	prog.resolve_labels();
	vm_execute(prog.instructions.data, prog.instructions.size);
}

// Call this in test_programs() after loading data:
// test_subquery_pattern();
//
// Add to test_programs.hpp - Replace or augment existing validation system

// ============================================================================
// Queue-based Validation System
// ============================================================================

struct ExpectedRow
{
	array<TypedValue, query_arena> values;
};
static array<ExpectedRow, query_arena> expected_queue;
static uint32_t						   validation_failures = 0;
static uint32_t						   validation_row_count = 0;
static bool							   validation_active = false;
// Validation callback
void
validation_callback(TypedValue *result, size_t count)
{
	validation_row_count++;

	if (expected_queue.size == 0)
	{
		printf("❌ Row %u: Unexpected row (no more expected)\n", validation_row_count);
		printf("   Got: ");
		print_result_callback(result, count);
		validation_failures++;
		return;
	}

	// Pop from front
	ExpectedRow &expected = expected_queue.data[0];

	// Validate column count
	if (expected.values.size != count)
	{
		printf("❌ Row %u: Column count mismatch (expected %u, got %zu)\n", validation_row_count, expected.values.size,
			   count);
		validation_failures++;
	}
	else
	{
		// Validate each column
		bool row_matches = true;
		for (size_t i = 0; i < count; i++)
		{
			if (expected.values.data[i].type != result[i].type ||
				type_compare(result[i].type, result[i].data, expected.values.data[i].data) != 0)
			{

				if (row_matches)
				{ // First mismatch in this row
					printf("❌ Row %u: Value mismatch\n", validation_row_count);
					row_matches = false;
				}

				printf("   Column %zu: expected ", i);
				type_print(expected.values.data[i].type, expected.values.data[i].data);
				printf(" (%s), got ", type_name(expected.values.data[i].type));
				type_print(result[i].type, result[i].data);
				printf(" (%s)\n", type_name(result[i].type));
			}
		}

		if (!row_matches)
		{
			validation_failures++;
		}
	}

	// Remove from queue (shift array)
	for (uint32_t i = 1; i < expected_queue.size; i++)
	{
		expected_queue.data[i - 1] = expected_queue.data[i];
	}
	expected_queue.size--;
}

// Clear validation state
inline void
validation_reset()
{
	array_clear(&expected_queue);
	validation_failures = 0;
	validation_row_count = 0;
	validation_active = false;
}

// Start validation mode
inline void
validation_begin()
{
	validation_reset();
	validation_active = true;

	vm_set_result_callback(validation_callback);
}

// End validation and report
inline bool
validation_end()
{
	validation_active = false;

	vm_set_result_callback(print_result_callback);

	bool success = (validation_failures == 0);

	if (expected_queue.size > 0)
	{
		printf("❌ %u expected rows were not emitted\n", expected_queue.size);
		for (uint32_t i = 0; i < expected_queue.size; i++)
		{
			printf("   Missing row %u: ", validation_row_count + i + 1);
			for (uint32_t j = 0; j < expected_queue.data[i].values.size; j++)
			{
				if (j > 0)
					printf(", ");
				type_print(expected_queue.data[i].values.data[j].type, expected_queue.data[i].values.data[j].data);
			}
			printf("\n");
		}
		success = false;
	}

	if (success)
	{
		printf("✅ All %u rows validated successfully\n", validation_row_count);
	}
	else
	{
		printf("❌ Validation failed: %u mismatches\n", validation_failures);
	}

	return success;
}

// Add expected row with TypedValues
inline void
expect_row_values(std::initializer_list<TypedValue> values)
{
	ExpectedRow row;
	array_reserve(&row.values, values.size());

	for (const auto &val : values)
	{
		// Deep copy the value
		uint32_t size = type_size(val.type);
		uint8_t *data = (uint8_t *)arena::alloc<query_arena>(size);
		type_copy(val.type, data, val.data);
		array_push(&row.values, TypedValue::make(val.type, data));
	}

	array_push(&expected_queue, row);
}

// ============================================================================
// Example test using validation queue
// ============================================================================

inline void
test_select_with_validation()
{
	printf("\n=== SELECT with Queue Validation ===\n");

	// Setup expected results BEFORE execution
	validation_begin();
	expect_row_values({alloc_u32(1), alloc_char16("emilys"), alloc_char32("emily.johnson@x.dummyjson.com"),
					   alloc_u32(28), alloc_char16("Phoenix")});

	expect_row_values({alloc_u32(2), alloc_char16("michaelw"), alloc_char32("michael.williams@x.dummyjson.com"),
					   alloc_u32(35), alloc_char16("Houston")});

	expect_row_values({alloc_u32(3), alloc_char16("sophiab"), alloc_char32("sophia.brown@x.dummyjson.com"),
					   alloc_u32(42), alloc_char16("Washington")});

	// Build and execute program
	ProgramBuilder prog;
	auto		   cctx = from_structure(catalog[USERS]);
	prog.open_cursor(&cctx);

	// Only get first 3 rows for this test
	int count = 0;
	int three = prog.load(TYPE_U32, prog.alloc_value(3U));
	int counter = prog.load(TYPE_U32, prog.alloc_value(0U));

	int	 at_end = prog.first(0);
	auto while_ctx = prog.begin_while(at_end);
	{
		int row = prog.get_columns(0, 0, 5);
		prog.result(row, 5);

		// Increment counter
		int one = prog.load(TYPE_U32, prog.alloc_value(1U));
		prog.add(counter, one, counter);

		// Check if we've done 3 rows
		int done = prog.ge(counter, three);
		prog.jumpif_true(done, "exit");

		prog.next(0, at_end);
	}
	prog.end_while(while_ctx);

	prog.label("exit");
	prog.close_cursor(0);
	prog.halt();
	prog.resolve_labels();

	// Execute with validation active
	vm_execute(prog.instructions.data, prog.instructions.size);

	// Check results
	validation_end();
}

// Macro for quick row expectations
#define EXPECT_ROW(...) expect_row_values({__VA_ARGS__})
