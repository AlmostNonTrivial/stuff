// src/parser.hpp
#pragma once
#include "defs.hpp"
#include <string>
#include <vector>

// Token types
enum TokenType : uint8_t {
    // Keywords
    TOK_SELECT, TOK_INSERT, TOK_UPDATE, TOK_DELETE, TOK_CREATE, TOK_DROP,
    TOK_TABLE, TOK_INDEX, TOK_INTO, TOK_VALUES, TOK_FROM, TOK_WHERE,
    TOK_AND, TOK_OR, TOK_ORDER, TOK_BY, TOK_ASC, TOK_DESC,
    TOK_COUNT, TOK_SUM, TOK_MIN, TOK_MAX, TOK_AVG,
    TOK_BEGIN, TOK_COMMIT, TOK_ROLLBACK,

    // Operators
    TOK_EQ, TOK_NE, TOK_LT, TOK_LE, TOK_GT, TOK_GE,
    TOK_STAR, TOK_COMMA, TOK_LPAREN, TOK_RPAREN, TOK_SEMICOLON,

    // Literals
    TOK_IDENTIFIER, TOK_INTEGER, TOK_STRING,

    TOK_EOF
};

struct Token {
    TokenType type;
    const char* start;
    uint32_t length;
    int64_t int_value;  // For integers
};

// AST nodes - all arena allocated
enum ExprType : uint8_t {
    EXPR_COLUMN,
    EXPR_LITERAL_INT,
    EXPR_LITERAL_STRING,
    EXPR_BINARY_OP
};

enum BinaryOp : uint8_t {
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE, OP_AND, OP_OR
};

struct Expr {
    ExprType type;
    union {
        struct { const char* name; uint32_t name_len; } column;
        struct { int64_t value; } int_lit;
        struct { const char* value; uint32_t len; } str_lit;
        struct {
            BinaryOp op;
            Expr* left;
            Expr* right;
        } binary;
    };
};

struct ColumnRef {
    const char* name;
    uint32_t name_len;
};

struct SetClause {
    const char* column;
    uint32_t column_len;
    Expr* value;
};

// Statement types
struct SelectStmt {
    ColumnRef* columns;     // null = SELECT *
    uint32_t column_count;
    const char* table;
    uint32_t table_len;
    Expr* where;           // null if no WHERE
    const char* order_by_column;
    uint32_t order_by_len;
    bool order_asc;
    const char* aggregate; // "COUNT", "MIN", etc or null
};

struct InsertStmt {
    const char* table;
    uint32_t table_len;
    Expr** values;         // Array of expressions
    uint32_t value_count;
};

struct UpdateStmt {
    const char* table;
    uint32_t table_len;
    SetClause* sets;
    uint32_t set_count;
    Expr* where;
};

struct DeleteStmt {
    const char* table;
    uint32_t table_len;
    Expr* where;
};

struct ColumnDef {
    const char* name;
    uint32_t name_len;
    DataType type;
    bool is_primary;
};

struct CreateTableStmt {
    const char* table;
    uint32_t table_len;
    ColumnDef* columns;
    uint32_t column_count;
};

struct CreateIndexStmt {
    const char* index_name;
    uint32_t index_name_len;
    const char* table;
    uint32_t table_len;
    const char* column;
    uint32_t column_len;
};

enum StmtType : uint8_t {
    STMT_SELECT, STMT_INSERT, STMT_UPDATE, STMT_DELETE,
    STMT_CREATE_TABLE, STMT_CREATE_INDEX,
    STMT_BEGIN, STMT_COMMIT, STMT_ROLLBACK
};

struct ParsedSQL {
    StmtType type;
    void* stmt;  // Points to one of the *Stmt structs
};

// Parser state
struct Parser {
    const char* input;
    uint32_t pos;
    uint32_t len;
    Token current;
    Token lookahead;
};

// Functions
void parser_init(Parser* p, const char* sql);
ParsedSQL* parse_sql(Parser* p);
