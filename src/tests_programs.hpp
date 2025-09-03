
#pragma once

#include "bplustree.hpp"
#include "catalog.hpp"
#include "compile.hpp"
#include "defs.hpp"
#include "os_layer.hpp"
#include "pager.hpp"
#include "types.hpp"
#include "vm.hpp"
#include <cstdint>

#include <fstream>
#include <sstream>
#include "bplustree.hpp"
#include "catalog.hpp"
#include "compile.hpp"
#include "defs.hpp"
#include "os_layer.hpp"
#include "pager.hpp"
#include "types.hpp"
#include "vm.hpp"
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

CursorContext
from_structure(Structure &structure)
{
	CursorContext cctx;
	cctx.storage.tree = &structure.storage.btree;
	cctx.type = CursorType::BPLUS;
	cctx.layout = structure.to_layout();
	return cctx;
}

CursorContext
red_black(Layout &layout)
{
	CursorContext cctx;
	cctx.type = CursorType::RED_BLACK;
	cctx.layout = layout;
	return cctx;
}

void
print_result_callback(TypedValue *result, size_t count)
{
	for (int i = 0; i < count; i++)
	{
		result[i].print();
		if (i != count - 1)
		{
			std::cout << ", ";
		}
	}
	std::cout << "\n";
}

MemoryContext ctx = {
	.alloc = arena::alloc<QueryArena>, .free = arena::reclaim<QueryArena>, .emit_row = print_result_callback};
static std::vector<std::vector<TypedValue>> last_results;

inline static void
print_results()
{
	int count = last_results.size();
	for (int i = 0; i < count; i++)
	{
		print_result_callback(last_results[i].data(), last_results[i].size());
	}
}

inline static void
capture_result_callback(TypedValue *result, size_t count)
{
	std::vector<TypedValue> row;
	for (size_t i = 0; i < count; i++)
	{
		row.push_back(result[i]);
	}

	last_results.push_back(row);
}

// Add a mode flag
static bool capture_mode = false;

inline static void
set_capture_mode(bool capture)
{
	capture_mode = capture;
	if (capture)
	{
		ctx.emit_row = capture_result_callback;
		last_results.clear();
	}
	else
	{
		ctx.emit_row = print_result_callback;
	}
}

// Simple accessors
inline static size_t
get_row_count()
{
	return last_results.size();
}

inline static bool
check_int_value(size_t row, size_t col, int expected)
{

	TypedValue &val = last_results[row][col];
	if (val.type != TYPE_U32)
		return false;

	return *(uint32_t *)val.data == expected;
}

inline static bool
check_string_value(size_t row, size_t col, const char *expected)
{

	TypedValue &val = last_results[row][col];
	return strcmp((char *)val.data, expected) == 0;
}

inline static void
clear_results()
{
	last_results.clear();
}

bool
vmfunc_create_table(TypedValue *result, TypedValue *args, uint32_t arg_count, MemoryContext *ctx)
{
	auto table_name = args->as_char();
	auto structure = catalog[table_name].to_layout();
	catalog[table_name].storage.btree = bplustree_create(structure.layout.at(0), structure.record_size, true);
	return true;
}

// Table schemas
std::vector<Column> users = {Column{USER_ID, TYPE_U32}, Column{USERNAME, TYPE_CHAR16}, Column{EMAIL, TYPE_CHAR32},
							 Column{USER_AGE, TYPE_U32}, Column{CITY, TYPE_CHAR16}};

std::vector<Column> products = {Column{PRODUCT_ID, TYPE_U32}, Column{TITLE, TYPE_CHAR32}, Column{CATEGORY, TYPE_CHAR16},
								Column{PRICE, TYPE_U32},	  Column{STOCK, TYPE_U32},	  Column{BRAND, TYPE_CHAR16}};

std::vector<Column> orders = {Column{ORDER_ID, TYPE_U32}, Column{USER_ID, TYPE_U32}, // FK to users
							  Column{TOTAL, TYPE_U32}, Column{TOTAL_QUANTITY, TYPE_U32}, Column{DISCOUNT, TYPE_U32}};

std::vector<Column> order_items = {Column{ITEM_ID, TYPE_U32},	 Column{ORDER_ID, TYPE_U32}, // FK to orders
								   Column{PRODUCT_ID, TYPE_U32},							 // FK to products
								   Column{"quantity", TYPE_U32}, Column{PRICE, TYPE_U32},	 Column{TOTAL, TYPE_U32}};

std::vector<Column> posts = {Column{POST_ID, TYPE_U32}, Column{USER_ID, TYPE_U32}, // FK to users
							 Column{TITLE, TYPE_CHAR32}, Column{VIEWS, TYPE_U32}, Column{REACTIONS, TYPE_U32}};

std::vector<Column> comments = {Column{COMMENT_ID, TYPE_U32}, Column{POST_ID, TYPE_U32}, // FK to posts
								Column{USER_ID, TYPE_U32},								 // FK to users
								Column{BODY, TYPE_CHAR32}, Column{LIKES, TYPE_U32}};

std::vector<Column> tags = {Column{TAG_ID, TYPE_U32}, Column{TAG_NAME, TYPE_CHAR16}};

std::vector<Column> post_tags = {
	Column{POST_ID, TYPE_U32}, // Composite PK/FK to posts
	Column{TAG_ID, TYPE_U32}   // Composite PK/FK to tags
};

std::vector<Column> user_followers = {
	Column{FOLLOWER_ID, TYPE_U32}, // FK to users
	Column{FOLLOWED_ID, TYPE_U32}  // FK to users
};

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

inline static void
create_all_tables(bool create)
{
	// Register all table schemas in catalog
	catalog[USERS] = Structure::from(USERS, users);
	catalog[PRODUCTS] = Structure::from(PRODUCTS, products);
	catalog[ORDERS] = Structure::from(ORDERS, orders);
	catalog[ORDER_ITEMS] = Structure::from(ORDER_ITEMS, order_items);
	catalog[POSTS] = Structure::from(POSTS, posts);
	catalog[COMMENTS] = Structure::from(COMMENTS, comments);
	catalog[TAGS] = Structure::from(TAGS, tags);
	catalog[POST_TAGS] = Structure::from(POST_TAGS, post_tags);
	catalog[USER_FOLLOWERS] = Structure::from(USER_FOLLOWERS, user_followers);

	if (!create)
	{
		return;
	}

	ProgramBuilder prog;
	prog.begin_transaction();

	const char *tables[] = {USERS, PRODUCTS, ORDERS, ORDER_ITEMS, POSTS, COMMENTS, TAGS, POST_TAGS, USER_FOLLOWERS};

	for (auto table_name : tables)
	{
		prog.regs.push_scope();
		int reg = prog.load(TypedValue::make(TYPE_CHAR16, (void *)table_name));
		prog.call_function(vmfunc_create_table, reg, 1);

		prog.regs.pop_scope();
	}

	prog.commit_transaction();
	prog.halt();

	vm_execute(prog.instructions.data, prog.instructions.size, &ctx);

	printf("Created %zu tables\n", sizeof(tables) / sizeof(tables[0]));
}

inline static void
load_table_from_csv(const char *csv_file, const char *table_name)
{
	CSVReader				 reader(csv_file);
	std::vector<std::string> fields;

	auto &structure = catalog[table_name];
	auto  layout = structure.to_layout();

	ProgramBuilder prog;
	prog.begin_transaction();

	CursorContext cctx;
	cctx.type = CursorType::BPLUS;
	cctx.storage.tree = &structure.storage.btree;
	cctx.layout = layout;

	prog.open_cursor(0, &cctx);

	int count = 0;

	while (reader.next_row(fields))
	{

		prog.regs.push_scope();
		if (fields.size() != structure.columns.size())
		{
			printf("Warning: row has %zu fields, expected %zu for table %s\n", fields.size(), structure.columns.size(),
				   table_name);
			continue;
		}

		int start_reg = -1;
		for (size_t i = 0; i < fields.size(); i++)
		{
			DataType type = structure.columns[i].type;
			void	*data = nullptr;

			if (type == TYPE_U32)
			{
				uint32_t *val = (uint32_t *)arena::alloc<QueryArena>(sizeof(uint32_t));
				*val = std::stoul(fields[i]);
				data = val;
			}
			else if (type == TYPE_CHAR16)
			{
				char *val = (char *)arena::alloc<QueryArena>(16);
				memset(val, 0, 16);
				strncpy(val, fields[i].c_str(), 15);
				data = val;
			}
			else if (type == TYPE_CHAR32)
			{
				char *val = (char *)arena::alloc<QueryArena>(32);
				memset(val, 0, 32);
				strncpy(val, fields[i].c_str(), 31);
				data = val;
			}

			int reg = prog.load(TypedValue::make(type, data));
			if (start_reg == -1)
				start_reg = reg;
		}

		prog.insert_record(0, start_reg, fields.size());
		count++;
		batch_size++;

		prog.regs.pop_scope();
	}

	prog.close_cursor(0);
	prog.commit_transaction();
	prog.halt();

	printf("Loaded %d records into %s\n", count, table_name);
	vm_execute(prog.instructions.data, prog.instructions.size, &ctx);
}

inline static void
load_all_data()
{
	printf("Loading data from CSV files...\n\n");

	// Load in dependency order (tables with no FKs first)
	load_table_from_csv("../users.csv", USERS);
	load_table_from_csv("../products.csv", PRODUCTS);
	load_table_from_csv("../tags.csv", TAGS);

	// // Then tables with FKs
	// load_table_from_csv("../orders.csv", ORDERS);
	// load_table_from_csv("../posts.csv", POSTS);

	// // Then tables with multiple FKs
	// load_table_from_csv("../order_items.csv", ORDER_ITEMS);
	// load_table_from_csv("../comments.csv", COMMENTS);

	// // Junction tables last
	// load_table_from_csv("../post_tags.csv", POST_TAGS);
	// load_table_from_csv("../user_followers.csv", USER_FOLLOWERS);

	printf("\n✅ All data loaded successfully!\n");
}

inline void
test_select()
{
	ProgramBuilder	   prog;
	RegisterAllocator &reg = prog.regs;
	int				   cursor = 0;
	auto cctx = from_structure(catalog[USERS]);
	prog.open_cursor(cursor, &cctx);
	
}

// Main test function
inline void
test_programs()
{
	arena::init<QueryArena>();
	bool existed = pager_open("relational_test.db");

	_debug = true;
	printf("=== Setting up relational database ===\n\n");
	create_all_tables(!existed);
	if (!existed)
	{
		load_all_data();
	}

	// Run test queries
	// test_join_query();
	// test_many_to_many_query();

	pager_close();

	printf("\n✅ All relational tests completed!\n");
}
