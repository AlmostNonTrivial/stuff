#pragma once

#include "defs.hpp"
#include "vec.hpp"

#include "arena.hpp"
#include <cstdint>
struct ColumnInfo;
// ============================================================================
// Command Categories
// ============================================================================

enum CommandCategory {
    CMD_DDL,  // Data Definition Language (CREATE, DROP, ALTER)
    CMD_DML,  // Data Manipulation Language (SELECT, INSERT, UPDATE, DELETE)
    CMD_TCL,  // Transaction Control Language (BEGIN, COMMIT, ROLLBACK)
};

// ============================================================================
// AST Node Types
// ============================================================================

enum ASTNodeType {
    // DDL Commands
    AST_CREATE_TABLE,
    AST_CREATE_INDEX,
    AST_DROP_INDEX,
    AST_DROP_TABLE,
    AST_ALTER_TABLE, // not implemented

    // DML Commands
    AST_SELECT,
    AST_INSERT,
    AST_UPDATE,
    AST_DELETE,

    // TCL Commands
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

// ============================================================================
// Base AST Node
// ============================================================================
inline CommandCategory get_command_category(ASTNodeType type) {
    switch (type) {
        // DDL Commands
        case AST_CREATE_TABLE:
        case AST_CREATE_INDEX:
        case AST_DROP_TABLE:
        case AST_DROP_INDEX:
            return CMD_DDL;

        // DML Commands
        case AST_SELECT:
        case AST_INSERT:
        case AST_UPDATE:
        case AST_DELETE:
            return CMD_DML;

        // TCL Commands
        case AST_BEGIN:
        case AST_COMMIT:
        case AST_ROLLBACK:
            return CMD_TCL;

        default:
            return CMD_DML; // Default to DML for safety
    }
}
struct ASTNode {
    ASTNodeType type;
    uint32_t statement_index;
    // Helper methods
    CommandCategory category() const {
        return get_command_category(type);
    }

    bool is_ddl() const { return category() == CMD_DDL; }
    bool is_dml() const { return category() == CMD_DML; }
    bool is_tcl() const { return category() == CMD_TCL; }
    bool needs_vm() const { return is_dml(); }

    const char* type_name() const {
        switch(type) {
            // DDL
            case AST_CREATE_TABLE: return "CREATE TABLE";
            case AST_CREATE_INDEX: return "CREATE INDEX";
            case AST_DROP_TABLE: return "DROP TABLE";
            case AST_ALTER_TABLE: return "ALTER TABLE";

            // DML
            case AST_SELECT: return "SELECT";
            case AST_INSERT: return "INSERT";
            case AST_UPDATE: return "UPDATE";
            case AST_DELETE: return "DELETE";

            // TCL
            case AST_BEGIN: return "BEGIN";
            case AST_COMMIT: return "COMMIT";
            case AST_ROLLBACK: return "ROLLBACK";

            // Other
            case AST_BINARY_OP: return "BINARY_OP";
            case AST_COLUMN_REF: return "COLUMN_REF";
            case AST_LITERAL: return "LITERAL";
            case AST_AGGREGATE: return "AGGREGATE";
            case AST_WHERE: return "WHERE";
            case AST_ORDER_BY: return "ORDER BY";
            case AST_SET_CLAUSE: return "SET";

            default: return "UNKNOWN";
        }
    }
};

// ============================================================================
// Command Category Helper Functions
// ============================================================================



inline const char* get_category_name(CommandCategory cat) {
    switch (cat) {
        case CMD_DDL: return "DDL";
        case CMD_DML: return "DML";
        case CMD_TCL: return "TCL";
        default: return "UNKNOWN";
    }
}

inline bool needs_vm_compilation(ASTNodeType type) {
    return get_command_category(type) == CMD_DML;
}

inline bool is_transaction_command(ASTNodeType type) {
    return get_command_category(type) == CMD_TCL;
}

inline bool modifies_schema(ASTNodeType type) {
    return get_command_category(type) == CMD_DDL;
}
#pragma once






// Expression nodes
struct ColumnRefNode : ASTNode {
    const char* name;
    uint32_t index;  // Resolved later
};

struct LiteralNode : ASTNode {
    TypedValue value;
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
    Vec<ASTNode*, QueryArena> columns;  // empty = *
    WhereNode* where;
    AggregateNode* aggregate;
    OrderByNode* order_by;
};

struct InsertNode : ASTNode {
    const char* table;
    Vec<ASTNode*, QueryArena> values;
};

struct UpdateNode : ASTNode {
    const char* table;
    Vec<SetClauseNode*, QueryArena> set_clauses;
    WhereNode* where;
};

struct DeleteNode : ASTNode {
    const char* table;
    WhereNode* where;
};

struct CreateTableNode : ASTNode {
    const char* table;
    Vec<ColumnInfo, QueryArena> columns;
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
    TOK_DROP,

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
Vec<ASTNode*, QueryArena> parse_sql(const char* sql);
struct DropTableNode : ASTNode {
    const char* table;
};

struct DropIndexNode : ASTNode {
    const char* index_name;
};
