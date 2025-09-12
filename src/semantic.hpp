#pragma once
#include "catalog.hpp"
#include "parser.hpp"
struct semantic_result {
    bool success;
    string_view error;           // Error message
    string_view error_context;   // Additional context (table/column name)
};


semantic_result semantic_analyze(
    stmt_node*stmt);
