#include "validation.hpp"
#include "catalog.hpp"
#include "defs.hpp"
#include "arena.hpp"
#include "types.hpp"
#include <cstring>
#include <cstdio>

ValidationResult validate_select_stmt(SelectStmt* node);


bool types_compatible(DataType column_type, DataType expr_type) {
    return column_type == expr_type;
}

// ============================================================================
// Non-asserting catalog queries
// ============================================================================

// External access to the tables map from catalog.cpp
extern Table* get_table(const char* table_name);
extern Index* get_index(const char* table_name, uint32_t column_index);
extern Index* get_index(const char* index_name);

bool table_exists(const char* table_name) {
    Table* table = get_table(table_name);
    return table != nullptr;
}

int32_t find_column_index(const char* table_name, const char* col_name) {
    Table* table = get_table(table_name);
    if (!table) return -1;

    for (uint32_t i = 0; i < table->columns.size; i++) {
        if (strcmp(table->columns.data[i].name, col_name) == 0) {
            return i;
        }
    }
    return -1;
}

bool column_exists(const char* table_name, const char* col_name) {
    return find_column_index(table_name, col_name) >= 0;
}

DataType get_column_type_safe(const char* table_name, const char* col_name) {
    Table* table = get_table(table_name);
    if (!table) return (DataType)0xFF;  // Invalid type marker

    int32_t idx = find_column_index(table_name, col_name);
    if (idx < 0) return (DataType)0xFF;  // Invalid type marker

    return table->columns.data[idx].type;
}

// ============================================================================
// Type checking helpers
// ============================================================================

DataType infer_expression_type(Expr* expr, const char* table_context) {
    if (!expr) return (DataType)0xFF;

    switch (expr->type) {
        case EXPR_LITERAL:
            return expr->lit_type;

        case EXPR_COLUMN: {
            const char* table = expr->table_name ? expr->table_name : table_context;
            if (!table) return (DataType)0xFF;
            return get_column_type_safe(table, expr->column_name);
        }

        case EXPR_NULL:
            // NULL can be any type - needs special handling
            return (DataType)0xFE;  // Special marker for NULL

        case EXPR_BINARY_OP:
            // For now, assume binary ops preserve type of left operand
            return infer_expression_type(expr->left, table_context);

        case EXPR_UNARY_OP:
            return infer_expression_type(expr->operand, table_context);

        case EXPR_FUNCTION:
            // Would need function return type info
            return (DataType)0xFF;

        default:
            return (DataType)0xFF;
    }
}

// ============================================================================
// Error helpers
// ============================================================================

void add_error(ValidationResult* result, const char* message, const char* context) {
    ValidationError err;
    err.message = message;
    err.context = context;
    err.line = 0;  // Would be populated from AST nodes if they had location info
    err.column = 0;

    array_push(&result->errors, err);
    result->valid = false;
}

// ============================================================================
// Expression validation
// ============================================================================

void validate_expression(Expr* expr, const char* table_context, ValidationResult* result) {
    if (!expr) return;

    switch (expr->type) {
        case EXPR_COLUMN: {
            // If no table specified, use context
            const char* table = expr->table_name ? expr->table_name : table_context;

            if (!table) {
                add_error(result, "Column reference without table context", expr->column_name);
                return;
            }

            if (!table_exists(table)) {
                add_error(result, "Table does not exist", table);
                return;
            }

            if (!column_exists(table, expr->column_name)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Column '%s' does not exist in table '%s'",
                        expr->column_name, table);
                add_error(result, msg, expr->column_name);
            }
            break;
        }

        case EXPR_BINARY_OP: {
            validate_expression(expr->left, table_context, result);
            validate_expression(expr->right, table_context, result);

            // Could add type checking here
            // DataType left_type = infer_expression_type(expr->left, table_context);
            // DataType right_type = infer_expression_type(expr->right, table_context);
            break;
        }

        case EXPR_UNARY_OP: {
            validate_expression(expr->operand, table_context, result);
            break;
        }

        case EXPR_FUNCTION: {
            // Validate function arguments
            if (expr->args) {
                for (size_t i = 0; i < expr->args->size; i++) {
                    validate_expression(expr->args->data[i], table_context, result);
                }
            }
            break;
        }

        case EXPR_LIST: {
            if (expr->list_items) {
                for (size_t i = 0; i < expr->list_items->size; i++) {
                    validate_expression(expr->list_items->data[i], table_context, result);
                }
            }
            break;
        }

        case EXPR_SUBQUERY: {
            if (expr->subquery) {
                ValidationResult subquery_result = validate_select_stmt(expr->subquery);
                // Merge errors
                for (size_t i = 0; i < subquery_result.errors.size; i++) {
                    array_push(&result->errors, subquery_result.errors.data[i]);
                }
                if (!subquery_result.valid) {
                    result->valid = false;
                }
            }
            break;
        }

        case EXPR_LITERAL:
        case EXPR_STAR:
        case EXPR_NULL:
            // These are always valid
            break;
    }
}

void validate_expression_list(array<Expr*, ParserArena>* exprs, const char* table_context, ValidationResult* result) {
    if (!exprs) return;

    for (size_t i = 0; i < exprs->size; i++) {
        validate_expression(exprs->data[i], table_context, result);
    }
}

// ============================================================================
// Statement validation
// ============================================================================

ValidationResult validate_select_stmt(SelectStmt* node) {
    ValidationResult result;

    result.valid = true;

    // Validate FROM clause first to establish table context
    const char* table_context = nullptr;
    if (node->from_table) {
        if (!table_exists(node->from_table->table_name)) {
            add_error(&result, "Table does not exist in FROM clause", node->from_table->table_name);
            return result;  // Can't continue without valid table
        }
        table_context = node->from_table->table_name;
    }

    // Validate SELECT list
    if (node->select_list) {
        for (size_t i = 0; i < node->select_list->size; i++) {
            Expr* expr = node->select_list->data[i];

            // Special handling for SELECT *
            if (expr->type == EXPR_STAR && !table_context) {
                add_error(&result, "SELECT * requires FROM clause", nullptr);
                continue;
            }

            validate_expression(expr, table_context, &result);
        }
    }

    // Validate JOINs
    if (node->joins) {
        for (size_t i = 0; i < node->joins->size; i++) {
            JoinClause* join = node->joins->data[i];

            if (!table_exists(join->table->table_name)) {
                add_error(&result, "Table does not exist in JOIN", join->table->table_name);
                continue;
            }

            // JOIN conditions can reference both tables
            // This is simplified - proper implementation would track multiple tables
            if (join->condition) {
                validate_expression(join->condition, table_context, &result);
            }
        }
    }

    // Validate WHERE clause
    if (node->where_clause) {
        validate_expression(node->where_clause, table_context, &result);
    }

    // Validate GROUP BY
    if (node->group_by) {
        validate_expression_list(node->group_by, table_context, &result);
    }

    // Validate HAVING
    if (node->having_clause) {
        validate_expression(node->having_clause, table_context, &result);
    }

    // Validate ORDER BY
    if (node->order_by) {
        for (size_t i = 0; i < node->order_by->size; i++) {
            validate_expression(node->order_by->data[i]->expr, table_context, &result);
        }
    }

    return result;
}

ValidationResult validate_insert_stmt(InsertStmt* node) {
    ValidationResult result;

    result.valid = true;

    if (!table_exists(node->table_name)) {
        add_error(&result, "Table does not exist", node->table_name);
        return result;
    }

    Table* table = get_table(node->table_name);

    // Build column index mapping
    // If columns specified, map them to table column indices
    // Otherwise, assume all columns in order
    array<uint32_t, QueryArena> column_indices;

    if (node->columns) {
        // Verify all columns exist and build mapping
        for (size_t i = 0; i < node->columns->size; i++) {
            int32_t idx = find_column_index(node->table_name, node->columns->data[i]);
            if (idx < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Column '%s' does not exist", node->columns->data[i]);
                add_error(&result, msg, node->columns->data[i]);
                continue;
            }
            array_push(&column_indices, (uint32_t)idx);
        }
    } else {
        // Use all columns in order
        for (uint32_t i = 0; i < table->columns.size; i++) {
            array_push(&column_indices, i);
        }
    }

    // Validate VALUES - it's an array of value lists (for multi-row insert)
    if (node->values) {
        for (size_t row_idx = 0; row_idx < node->values->size; row_idx++) {
            array<Expr*, ParserArena>* value_list = node->values->data[row_idx];

            // Check value count matches column count
            size_t expected_cols = column_indices.size;
            if (value_list->size != expected_cols) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Row %zu: Value count (%zu) doesn't match column count (%zu)",
                        row_idx + 1, value_list->size, expected_cols);
                add_error(&result, msg, nullptr);
                continue;
            }

            // Type check each value against its column
            for (size_t val_idx = 0; val_idx < value_list->size && val_idx < column_indices.size; val_idx++) {
                Expr* value_expr = value_list->data[val_idx];
                uint32_t col_idx = column_indices.data[val_idx];
                DataType column_type = table->columns.data[col_idx].type;

                // First validate the expression structure
                validate_expression(value_expr, nullptr, &result);

                // Then check type compatibility
                DataType expr_type = infer_expression_type(value_expr, nullptr);
                if (!types_compatible(column_type, expr_type)) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Row %zu, column '%s': Type mismatch (expected %s, got %s)",
                            row_idx + 1, table->columns.data[col_idx].name,
                            type_name(column_type), type_name(expr_type));

                    add_error(&result, msg, table->columns.data[col_idx].name);
                }
            }
        }
    }

    return result;
}

ValidationResult validate_update_stmt(UpdateStmt* node) {
    ValidationResult result;

    result.valid = true;

    if (!table_exists(node->table_name)) {
        add_error(&result, "Table does not exist", node->table_name);
        return result;
    }

    Table* table = get_table(node->table_name);

    // Validate columns exist and build index mapping
    array<uint32_t, QueryArena> column_indices;


    if (node->columns) {
        for (size_t i = 0; i < node->columns->size; i++) {
            int32_t idx = find_column_index(node->table_name, node->columns->data[i]);
            if (idx < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Column '%s' does not exist", node->columns->data[i]);
                add_error(&result, msg, node->columns->data[i]);
                continue;
            }
            array_push(&column_indices, (uint32_t)idx);
        }
    }

    // Validate values and check types
    if (node->values) {
        // Check that column count matches value count
        if (node->columns && node->columns->size != node->values->size) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Column count (%zu) doesn't match value count (%zu)",
                    node->columns->size, node->values->size);
            add_error(&result, msg, nullptr);
        }

        // Type check each value against its column
        for (size_t i = 0; i < node->values->size && i < column_indices.size; i++) {
            Expr* value_expr = node->values->data[i];
            uint32_t col_idx = column_indices.data[i];
            DataType column_type = table->columns.data[col_idx].type;

            // First validate the expression structure
            validate_expression(value_expr, node->table_name, &result);

            // Then check type compatibility
            DataType expr_type = infer_expression_type(value_expr, node->table_name);
            if (!types_compatible(column_type, expr_type)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Column '%s': Type mismatch (expected %s, got %s)",
                        table->columns.data[col_idx].name,
                        type_name(column_type), type_name(expr_type));
                add_error(&result, msg, table->columns.data[col_idx].name);
            }
        }
    }

    // Validate WHERE clause
    if (node->where_clause) {
        validate_expression(node->where_clause, node->table_name, &result);
    }

    return result;
}

ValidationResult validate_delete_stmt(DeleteStmt* node) {
    ValidationResult result;

    result.valid = true;

    if (!table_exists(node->table_name)) {
        add_error(&result, "Table does not exist", node->table_name);
        return result;
    }

    // Validate WHERE clause
    if (node->where_clause) {
        validate_expression(node->where_clause, node->table_name, &result);
    }

    return result;
}

ValidationResult validate_create_table_stmt(CreateTableStmt* node) {
    ValidationResult result;

    result.valid = true;

    // Check table doesn't already exist
    if (table_exists(node->table_name)) {
        add_error(&result, "Table already exists", node->table_name);
        return result;
    }

    // Check for duplicate column names
    for (size_t i = 0; i < node->columns->size; i++) {
        for (size_t j = i + 1; j < node->columns->size; j++) {
            if (strcmp(node->columns->data[i]->name, node->columns->data[j]->name) == 0) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Duplicate column name '%s'", node->columns->data[i]->name);
                add_error(&result, msg, node->columns->data[i]->name);
            }
        }
    }

    // Ensure at least one column
    if (node->columns->size == 0) {
        add_error(&result, "Table must have at least one column", node->table_name);
    }

    return result;
}

ValidationResult validate_create_index_stmt(CreateIndexStmt* node) {
    ValidationResult result;

    result.valid = true;

    if (!table_exists(node->table_name)) {
        add_error(&result, "Table does not exist", node->table_name);
        return result;
    }

    // Validate columns array
    if (!node->columns || node->columns->size == 0) {
        add_error(&result, "Index must specify at least one column", nullptr);
        return result;
    }

    // The catalog implementation only supports single-column indexes
    if (node->columns->size > 1) {
        add_error(&result, "Multi-column indexes are not supported", nullptr);
        return result;
    }

    const char* column_name = node->columns->data[0];

    if (!column_exists(node->table_name, column_name)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Column '%s' does not exist in table '%s'",
                column_name, node->table_name);
        add_error(&result, msg, column_name);
        return result;
    }

    // Check if index already exists on this column
    int32_t col_idx = find_column_index(node->table_name, column_name);
    if (col_idx < 0) {
        // This shouldn't happen if column_exists returned true
        add_error(&result, "Internal error: column index not found", column_name);
        return result;
    }

    // Cannot index primary key (column 0)
    if (col_idx == 0) {
        add_error(&result, "Cannot create index on primary key column", column_name);
        return result;
    }

    // Check if index already exists
    if (get_index(node->table_name, (uint32_t)col_idx)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Index already exists on column '%s'", column_name);
        add_error(&result, msg, column_name);
    }

    return result;
}

ValidationResult validate_drop_table_stmt(DropTableStmt* node) {
    ValidationResult result;

    result.valid = true;

    if (!table_exists(node->table_name)) {
        add_error(&result, "Table does not exist", node->table_name);
    }

    if (strcmp(node->table_name, "master_catalog") == 0) {
        add_error(&result, "Cannot drop system table", node->table_name);
    }

    return result;
}

ValidationResult validate_drop_index_stmt(DropIndexStmt* node) {
    ValidationResult result;

    result.valid = true;

    // Try to find index by name
    Index* index = get_index(node->index_name);
    if (!index) {
        add_error(&result, "Index does not exist", node->index_name);
    }

    // If table_name is specified, validate it matches
    if (node->table_name && index) {
        if (strcmp(index->table_name.data, node->table_name) != 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Index '%s' does not exist on table '%s'",
                    node->index_name, node->table_name);
            add_error(&result, msg, node->index_name);
        }
    }

    return result;
}

// ============================================================================
// Main validation entry point
// ============================================================================

ValidationResult validate_statement(Statement* stmt) {
    switch (stmt->type) {
        case STMT_SELECT:
            return validate_select_stmt(stmt->select_stmt);
        case STMT_INSERT:
            return validate_insert_stmt(stmt->insert_stmt);
        case STMT_UPDATE:
            return validate_update_stmt(stmt->update_stmt);
        case STMT_DELETE:
            return validate_delete_stmt(stmt->delete_stmt);
        case STMT_CREATE_TABLE:
            return validate_create_table_stmt(stmt->create_table_stmt);
        case STMT_CREATE_INDEX:
            return validate_create_index_stmt(stmt->create_index_stmt);
        case STMT_DROP_TABLE:
            return validate_drop_table_stmt(stmt->drop_table_stmt);
        case STMT_DROP_INDEX:
            return validate_drop_index_stmt(stmt->drop_index_stmt);

        // Transaction statements don't need validation
        case STMT_BEGIN:
        case STMT_COMMIT:
        case STMT_ROLLBACK: {
            ValidationResult result;

            result.valid = true;
            return result;
        }

        default: {
            ValidationResult result;

            result.valid = false;
            add_error(&result, "Unknown statement type", nullptr);
            return result;
        }
    }
}
