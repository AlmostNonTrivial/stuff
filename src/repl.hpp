#pragma once

#include "parser.hpp"
#include <cstdio>


void setup_result_formatting(select_stmt* select_stmt);

// Initialize and run the REPL with the specified database file
// Returns 0 on success, non-zero on error
int run_repl(const char* database_path);

// Execute a single SQL statement
// Returns true on success, false on error
// test_mode suppresses some output for benchmarking
bool execute_sql_statement(const char* sql);

// Process meta commands (commands starting with '.')
void run_meta_command(const char* cmd);
