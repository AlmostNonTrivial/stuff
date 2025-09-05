
// semantic.cpp
#include "semantic.hpp"
#include "catalog.hpp"

bool semantic_resolve_create_table(CreateTableStmt* stmt, SemanticContext* ctx) {
    // Check if table already exists
    if (!stmt->if_not_exists) {

        Structure* existing = catalog.get(stmt->table_name);
        stringmap_get(&ctx->shadow_catalog, stmt->table_name);
        if (existing) {
            ctx->add_error("Table already exists", stmt->table_name);
            stmt->sem.has_errors = true;
            return false;
        }
    }

    // Validate column definitions
    if (!stmt->columns || stmt->columns->size == 0) {
        ctx->add_error("Table must have at least one column", stmt->table_name);
        return false;
    }

    // Check for duplicate column names
    string_set<query_arena> column_names;
    for (uint32_t i = 0; i < stmt->columns->size; i++) {
        ColumnDef* col = stmt->columns->data[i];

        Check duplicate
        if (hashset_contains(&column_names, col->name)) {
            ctx->add_error("Duplicate column name", col->name);
            return false;
        }
        hashset_insert(&column_names, col->name);

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
    std::vector<Column> cols;
    for (uint32_t i = 0; i < stmt->columns->size; i++) {
        ColumnDef* def = stmt->columns->data[i];
        cols.push_back(Column{def->name, def->type});
    }

    Structure* new_structure = (Structure*)arena::alloc<query_arena>(sizeof(Structure));
    *new_structure = Structure::from(stmt->table_name, cols);

    // Add to shadow catalog
    stringmap_insert(&ctx->shadow_catalog, stmt->table_name, new_structure);

    // Store in semantic info
    stmt->sem.created_structure = new_structure;
    stmt->sem.is_resolved = true;

    return true;
}

// Main entry point for statement semantic analysis
bool semantic_resolve_statement(Statement* stmt, SemanticContext* ctx) {
    switch (stmt->type) {
    case STMT_CREATE_TABLE:
        return semantic_resolve_create_table(stmt->create_table_stmt, ctx);

    // ... other statement types

    default:
        return false;
    }
}
