#pragma once
#include "vm.hpp"
#include "parser.hpp"


array<VMInstruction, QueryArena>
build_from_ast(Statement *ast);
