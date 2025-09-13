#pragma once

#include "parser.hpp"



void setup_result_formatting(select_stmt* select_stmt);

int run_repl(const char* database_path);

bool execute_sql_statement(const char* sql);

void run_meta_command(const char* cmd);
