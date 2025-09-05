
// semantic.cpp
#include "semantic.hpp"
#include "catalog.hpp"
#include "common.hpp"

bool semantic_resolve_create_table(CreateTableStmt* stmt, SemanticContext* ctx) {
    // Check if table already exists
    if (!stmt->if_not_exists) {
        Structure* existing = catalog.get(stmt->table_name);
        if (existing) {
            ctx->add_error("Table already exists", stmt->table_name);
            return false;
        }
    }

    // Validate column definitions
    if (!stmt->columns.size || stmt->columns.size == 0) {
        ctx->add_error("Table must have at least one column", stmt->table_name);
        return false;
    }

    // Check for duplicate column names
    hash_set<string<query_arena>, query_arena> column_names;
    for (uint32_t i = 0; i < stmt->columns.size; i++) {
        ColumnDef* col = stmt->columns[i];

        // Check duplicate;
        if (column_names.contains(col->name)){
            ctx->add_error("Duplicate column name", col->name);
            return false;
        }
        column_names.insert(col->name);

        // Mark BLOB columns
        if (strcmp(col->name, "blob") == 0 ||
            strstr(col->name, "_blob") != nullptr) {
            col->sem.is_blob_ref = true;
        }

        // Validate type
        if (col->type == TYPE_NULL) {
            ctx->add_error("Invalid column type", col->name);
            return false;
        }
    }

    // Build Structure for shadow catalog
    array<Column> cols;
    for (uint32_t i = 0; i < stmt->columns.size; i++) {
        ColumnDef* def = stmt->columns[i];
        cols.push(Column{def->name, def->type});
    }

    Structure* new_structure = (Structure*)arena::alloc<query_arena>(sizeof(Structure));

    *new_structure = Structure::from(stmt->table_name, cols);

    // Add to shadow catalog
    // catalog stmt->table_name, new_structure;

    catalog[stmt->table_name] = *new_structure;
    // Store in semantic info
    stmt->sem.created_structure = new_structure;
    stmt->sem.is_resolved = true;

    return true;
}


// Helper function for type compatibility
bool types_compatible(DataType source, DataType target) {
    // Exact match
    if (source == target) return true;

    // Numeric type promotions
    if (type_is_numeric(source) && type_is_numeric(target)) {
        // Allow promotion from smaller to larger types
        uint32_t source_size = type_size(source);
        uint32_t target_size = type_size(target);
        return source_size <= target_size;
    }

    // String compatibility
    if (type_is_string(source) && type_is_string(target)) {
        return true; // Allow string conversions
    }

    return false;
}

bool semantic_resolve_insert(InsertStmt* stmt, SemanticContext* ctx) {
    // 1. Validate table exists
    Structure* table = catalog.get(stmt->table_name);
    if (!table) {
        ctx->add_error("Table does not exist", stmt->table_name);
        return false;
    }
    stmt->sem.table = table;

    // 2. Resolve column list or use all columns
    if (stmt->columns.size > 0) {
        // Explicit column list - validate each column exists
        stmt->sem.column_indices.reserve(stmt->columns.size);

        for (uint32_t i = 0; i < stmt->columns.size; i++) {
            bool found = false;
            for (uint32_t j = 0; j < table->columns.size; j++) {
                if (strcmp(stmt->columns[i].c_str(), table->columns[j].name) == 0) {
                    stmt->sem.column_indices.push(j);
                    found = true;
                    break;
                }
            }
            if (!found) {
                ctx->add_error("Column does not exist", stmt->columns[i]);
                return false;
            }
        }
    } else {
        // No column list - use all columns in order
        stmt->sem.column_indices.reserve(table->columns.size);
        for (uint32_t i = 0; i < table->columns.size; i++) {
            stmt->sem.column_indices.push(i);
        }
    }

    // 3. Validate value rows
    uint32_t expected_columns = stmt->sem.column_indices.size;

    for (uint32_t row = 0; row < stmt->values.size; row++) {
        auto* value_row = stmt->values[row];

        if (value_row->size != expected_columns) {
            ctx->add_error("Value count mismatch", nullptr);
            return false;
        }

        // 4. Validate each value expression (simplified - just check literals for now)
        for (uint32_t col = 0; col < value_row->size; col++) {
            Expr* expr = value_row->data[col];
            uint32_t table_col_idx = stmt->sem.column_indices[col];
            DataType expected_type = table->columns[table_col_idx].type;

            // For now, just handle literals
            if (expr->type == EXPR_LITERAL) {
                // Basic type compatibility check
                if (!types_compatible(expr->lit_type, expected_type)) {
                    ctx->add_error("Type mismatch in value", nullptr);
                    return false;
                }
            }
            // TODO: Handle other expression types, function calls, etc.
        }
    }

    stmt->sem.is_resolved = true;
    return true;
}


// Main entry point for statement semantic analysis
bool semantic_resolve_statement(Statement* stmt, SemanticContext* ctx) {
    switch (stmt->type) {
    case STMT_CREATE_TABLE:
        return semantic_resolve_create_table(stmt->create_table_stmt, ctx);

        case STMT_INSERT:
            return semantic_resolve_insert(stmt->insert_stmt, ctx);

        // ... other statement types

        default:
            ctx->add_error("Unsupported statement type", nullptr);
            return false;
    }
}
