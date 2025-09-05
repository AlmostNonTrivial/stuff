// semantic.hpp
#pragma once
#include "parser.hpp"
#include "catalog.hpp"
#include "arena.hpp"

struct SemanticContext {
    // Error reporting
    struct Error {
        const char* message;
        const char* context;  // table/column name
    };
    array<Error, query_arena> errors;

    void add_error(const char* msg, const char* ctx = nullptr) {
        errors.push(Error{msg,ctx});
    }
};

bool semantic_resolve_statement(Statement* stmt, SemanticContext* ctx);
