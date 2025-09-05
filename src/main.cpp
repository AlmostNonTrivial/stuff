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

int
main()
{

	arena::init<global_arena>();
	arena::init<catalog_arena>();
	arena::init<parser_arena>();
	bool existed = pager_open("test.db");
	bootstrap_master(!existed);

	const char *stm = "CREATE TABLE X (id INT);";
	Parser		p;
	parser_init(&p, stm);
	auto result = parser_parse_statement(&p);
	print_ast(result);

	SemanticContext ctx;

	if (!semantic_resolve_statement(result, &ctx))
	{
		for (auto err : ctx.errors)
		{
			std::cout << err.message << "\n";
		}
	}

	auto program = compile_program(result);

	if (OK!= vm_execute(program.data, program.size))
	{
		std::cout << "error\n";
	}


	for(auto [a,b] : catalog) {
	    std::cout << a.c_str() << ",";
	}
}
