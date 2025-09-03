
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
#define TEST_DB "test"

#define CUSTOMERS "customers"
#define ID		  "id"
#define NAME	  "name"
#define AGE		  "age"

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

std::vector<Column> customers = {Column{ID, TYPE_U32}, Column{NAME, TYPE_CHAR16}, Column{AGE, TYPE_U8}};

bool
vmfunc_create_table(TypedValue *result, TypedValue *args, uint32_t arg_count, MemoryContext *ctx)
{
	auto table_name = args->as_char();
	auto structure = catalog[table_name].to_layout();
	catalog[table_name].storage.btree = bplustree_create(structure.layout.at(0), structure.record_size, true);
	return true;
}

inline static void
test_create_table()
{

	catalog[CUSTOMERS] = Structure::from(CUSTOMERS, customers);

	ProgramBuilder prog;
	prog.begin_transaction();
	int reg = prog.load(TypedValue::make(TYPE_CHAR16, (void *)CUSTOMERS));
	prog.call_function(vmfunc_create_table, reg, 1);
	prog.commit_transaction();
	prog.halt();
	vm_execute(prog.instructions.data, prog.instructions.size, &ctx);
}

inline static void
test_insert_()
{
	ProgramBuilder prog;
	int			   cursor_id = 0;

	uint32_t   id1 = 1;
	TypedValue id = TypedValue::make(TYPE_U32, &id1);

	uint32_t   name1 = 1;
	TypedValue name = TypedValue::make(TYPE_CHAR16, &name1);

	uint8_t	   age1 = 25;
	TypedValue age = TypedValue::make(TYPE_U8, &age1);

	CursorContext cctx;
	cctx.type = CursorType::BPLUS;
	cctx.storage.tree = catalog[CUSTOMERS].storage.btree;
	cctx.layout = catalog[CUSTOMERS].to_layout();

	prog.begin_transaction();

	prog.open_cursor(cursor_id, &cctx);
	int start_reg = prog.load(id);
	prog.load(name);
	prog.load(age);
	prog.insert_record(cursor_id, start_reg, 3);
	prog.commit_transaction();
	prog.halt();


	vm_execute(prog.instructions.data, prog.instructions.size, &ctx);
}

inline void
test_programs()
{
	arena::init<QueryArena>();
	pager_open(TEST_DB);

	test_create_table();
	_debug = true;
	test_insert_();

	pager_close();
	os_file_delete(TEST_DB);
	std::cout << "ALL TESTS PASSED\n";
}
