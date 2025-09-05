// test_arena_containers.cpp
#include "arena.hpp"
#include "catalog.hpp"
#include "common.hpp"

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

// #include "tests/tests_pager.hpp"
// #include "tests/tests_parser.hpp"
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

int
main()
{
	arena::init<global_arena>();
	arena::init<catalog_arena>();
	arena::init<parser_arena>();
	bool existed = pager_open("test.db");
	bootstrap_master(!existed);
	if (existed)
	{

		load_catalog_from_master();
	}

	const char *stm = "CREATE TABLE X (id INT);";
	Parser		p;
	parser_init(&p, stm);
	auto result = parser_parse_statement(&p);
	// print_ast(result);

	SemanticContext ctx;

	if (!semantic_resolve_statement(result, &ctx))
	{
		for (auto err : ctx.errors)
		{
			std::cout << err.message << "\n";
		}
	}

	auto program = compile_program(result);

	if (OK != vm_execute(program.data, program.size))
	{
		reload_catalog();
	}

	pager_close();
}
