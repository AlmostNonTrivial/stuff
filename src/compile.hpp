#pragma once
#include "vm.hpp"
#include "parser.hpp"
#include "arena.hpp"
#include "catalog.hpp"
#include <utility>

// Main entry points for AST-based building
Vec<VMInstruction, QueryArena> build_from_ast(ASTNode* ast);
