// test_arena_containers.cpp
#include "arena.hpp"
#include "catalog.hpp"
#include "common.hpp"
#include "parser.hpp"
#include "semantic.hpp"
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
	arena::init<ParserArena>();

	string<catalog_arena> sds;
	sds.set("X");
	auto s = Structure::from("X", array<Column>{0});
	catalog.insert(sds, s);
	const char *stm = "CREATE TABLE X (id INT);";
	Parser		p;
	parser_init(&p, stm);
	auto result = parser_parse_statement(&p);

	SemanticContext ctx;

	if (!semantic_resolve_statement(result, &ctx))
	{
		for (auto err : ctx.errors)
		{
		    std::cout << err.message<< "\n";
		}
	}

	// test_parser();
}
