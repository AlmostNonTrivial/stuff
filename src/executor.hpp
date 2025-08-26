#pragma once
#include "vm.hpp"

#include "parser.hpp"
#include "arena.hpp"


void execute(const char * sql);

void executor_init(bool existed);
void executor_shutdown();
void set_capture_mode(bool capture);
size_t get_row_count();
bool check_int_value(size_t row, size_t col, int expected);
bool check_string_value(size_t row, size_t col, const char* expected);
void clear_results();

void print_results() ;
