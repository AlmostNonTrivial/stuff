#pragma once

#include "vm.hpp"
#include "arena.hpp"
#include <vector>



// AST Node Types
enum ASTNodeType {
    AST_SELECT,
    AST_INSERT,
    AST_UPDATE,
    AST_DELETE,
    AST_CREATE_TABLE,
    AST_CREATE_INDEX,
    AST_BEGIN,
    AST_COMMIT,
    AST_ROLLBACK,

    // Expressions
    AST_BINARY_OP,
    AST_COLUMN_REF,
    AST_LITERAL,
    AST_AGGREGATE,

    // Clauses
    AST_WHERE,
    AST_ORDER_BY,
    AST_SET_CLAUSE,
};

// Base AST Node
struct ASTNode {
    ASTNodeType type;
};

// Expression nodes
struct ColumnRefNode : ASTNode {
    const char* name;
    uint32_t index;  // Resolved later
};

struct LiteralNode : ASTNode {
    VMValue value;
};

struct BinaryOpNode : ASTNode {
    CompareOp op;
    ASTNode* left;
    ASTNode* right;
    bool is_and;  // true for AND, false for comparison
};

struct AggregateNode : ASTNode {
    const char* function;
    ASTNode* arg;  // nullptr for COUNT(*)
};

// Clause nodes
struct WhereNode : ASTNode {
    ASTNode* condition;
};

struct OrderByNode : ASTNode {
    const char* column;
    uint32_t column_index;
    bool ascending;
};

struct SetClauseNode : ASTNode {
    const char* column;
    uint32_t column_index;
    ASTNode* value;
};

// Statement nodes
struct SelectNode : ASTNode {
    const char* table;
    ArenaVector<ASTNode*> columns;  // empty = *
    WhereNode* where;
    AggregateNode* aggregate;
    OrderByNode* order_by;
};

struct InsertNode : ASTNode {
    const char* table;
    ArenaVector<ASTNode*> values;
};

struct UpdateNode : ASTNode {
    const char* table;
    ArenaVector<SetClauseNode*> set_clauses;
    WhereNode* where;
};

struct DeleteNode : ASTNode {
    const char* table;
    WhereNode* where;
};

struct CreateTableNode : ASTNode {
    const char* table;
    ArenaVector<ColumnInfo> columns;
};

struct CreateIndexNode : ASTNode {
    const char* index_name;
    const char* table;
    const char* column;
};

struct BeginNode : ASTNode {};
struct CommitNode : ASTNode {};
struct RollbackNode : ASTNode {};

// Token types
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

    // Type keywords
    TOK_INT, TOK_INT32, TOK_INT64,
    TOK_VARCHAR, TOK_VARCHAR32, TOK_VARCHAR256,
    TOK_VAR32,

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

    // Error state
    const char* error_msg;
    size_t error_pos;
};



ArenaVector<ASTNode*> parse_sql(const char* sql);
