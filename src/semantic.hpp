// semantic.hpp
#pragma once
#include "parser.hpp"
#include "catalog.hpp"

struct SemanticResult {
    bool success;
    const char* error;           // Error message (nullptr if success)
    const char* error_context;   // Additional context (table/column name)
    int failed_statement_index;  // Which statement failed (-1 if none)
};

// Main entry point - modifies statements in place
SemanticResult semantic_analyze(array<Statement*, parser_arena>* statements);
