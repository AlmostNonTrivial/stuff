#pragma once
#include "vm.hpp"
#include "parser.hpp"


array<VMInstruction, QueryArena>
build_from_ast(Statement *ast);


array<VMInstruction, QueryArena>
load_table_ids_program();

void test_ephemeral_with_builder() ;
