#pragma once
#include "vm.hpp"
#include "programbuilder.hpp"
#include "parser.hpp"
#include "arena.hpp"



struct ExecutionMeta {
    ArenaString<QueryArena> sql;
    AccessMethodEnum access;
    VM_RESULT result;
};

ExecutionMeta * execute(const char * sql);
