// semantic.hpp
#pragma once
#include "parser.hpp"
#include "catalog.hpp"

struct semantic_result {
    bool success;
    string_view error;           // Error message (nullptr if success)
    string_view error_context;   // Additional context (table/column name)
    int failed_statement_index;  // Which statement failed (-1 if none)
};

// Main entry point - modifies statements in place
semantic_result semantic_analyze(array<Statement*, query_arena>& statements);
