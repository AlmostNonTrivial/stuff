#pragma once
#include "vm.hpp"

#include "parser.hpp"
#include "arena.hpp"


void execute(const char * sql);
void init_executor();
