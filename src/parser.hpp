#pragma once

#include "vm.hpp"
#include <vector>


std::vector<VMInstruction> parse_sql(const char * sql);


enum TokenType {
    TOK_EOF,
    TOK_IDENTIFIER,
    TOK_INTEGER,
    TOK_FLOAT,
    TOK_STRING,

    // Keywords
    TOK_SELECT, TOK_FROM, TOK_WHERE, TOK_ORDER, TOK_BY,
    TOK_INSERT, TOK_INTO, TOK_VALUES,
    TOK_UPDATE, TOK_SET,
    TOK_DELETE,
    TOK_CREATE, TOK_TABLE, TOK_INDEX, TOK_ON,
    TOK_BEGIN, TOK_COMMIT, TOK_ROLLBACK,
    TOK_AND, TOK_OR,
    TOK_ASC, TOK_DESC,
    TOK_COUNT, TOK_MIN, TOK_MAX, TOK_SUM, TOK_AVG,

    // Operators
    TOK_LPAREN, TOK_RPAREN,
    TOK_COMMA, TOK_SEMICOLON,
    TOK_STAR,
    TOK_EQ, TOK_NE, TOK_LT, TOK_LE, TOK_GT, TOK_GE,
};


struct Parser {
    const char* input;
    size_t pos;
    size_t len;

    // Current token
    TokenType current_type;
    const char* current_start;
    size_t current_len;
    union {
        int64_t int_val;
        double float_val;
    } current_value;

    // Lookahead
    TokenType next_type;

    // Error state
    const char* error_msg;
    size_t error_pos;
};
