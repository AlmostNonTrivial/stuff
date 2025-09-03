
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

void
print_results()
{
	int count = last_results.size();
	for (int i = 0; i < count; i++)
	{
		print_result_callback(last_results[i].data(), last_results[i].size());
	}
}

static void
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

void
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
size_t
get_row_count()
{
	return last_results.size();
}

bool
check_int_value(size_t row, size_t col, int expected)
{

	TypedValue &val = last_results[row][col];
	if (val.type != TYPE_U32)
		return false;

	return *(uint32_t *)val.data == expected;
}

bool
check_string_value(size_t row, size_t col, const char *expected)
{

	TypedValue &val = last_results[row][col];
	return strcmp((char *)val.data, expected) == 0;
}

void
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
	bplustree_create(structure.layout.at(0), structure.record_size, true);
	return true;
}

void
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

inline void
test_programs()
{
	arena::init<QueryArena>();
	pager_open(TEST_DB);

	_debug = true;
	test_create_table();

	pager_close();
	os_file_delete(TEST_DB);
	std::cout << "ALL TESTS PASSED\n";
}
