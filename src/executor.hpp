#pragma once
#include "vm.hpp"

#include "parser.hpp"
#include "arena.hpp"


void execute(const char * sql);

void executor_init(bool existed);
void executor_shutdown();
