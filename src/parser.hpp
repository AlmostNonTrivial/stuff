#pragma once

#include "defs.hpp"
#include "vm.hpp"
#include "arena.hpp"
#include "schema.hpp"
#include <cstdint>

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
    AST_DROP_TABLE,    // Future
    AST_ALTER_TABLE,   // Future

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
        case AST_ALTER_TABLE:
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
