// ============================================================================
// semantic.cpp
// ============================================================================
#include "semantic.hpp"
#include "arena.hpp" #include "containers.hpp"
#include "catalog.hpp"
#include "parser.hpp"
#include "types.hpp"
#include <cstring>
#include <cstdio>

// Static catalog changes tracker
static struct
{
	// Tables to be created (name -> Structure)
	hash_map<string<parser_arena>, Structure, parser_arena> tables_to_create;

	// Tables to be dropped (just names)
	array<string<parser_arena>, parser_arena> tables_to_drop;
} catalog_changes;

// Apply all pending changes to the actual catalog
static void
apply_catalog_changes()
{
	// Remove dropped tables first
	for (uint32_t i = 0; i < catalog_changes.tables_to_drop.size(); i++)
	{
		catalog.remove(catalog_changes.tables_to_drop[i]);
	}

	// Add new tables
	for (int i = 0; i < catalog_changes.tables_to_create.size();i++)
	{
		auto &entry = catalog_changes.tables_to_create.entries()[i];
		// [i];
		if (entry.state != hash_slot_state::OCCUPIED)
		{
			continue;
		}
		catalog.insert(entry.key, entry.value);
	}
}

// Clear without applying
static void
clear_catalog_changes()
{
	catalog_changes.tables_to_create.clear();
	catalog_changes.tables_to_drop.clear();
}

// Helper to lookup table (checks both real catalog and pending changes)
static Structure *
lookup_table(const string<parser_arena> &name)
{
	// First check if it's pending deletion
	for (uint32_t i = 0; i < catalog_changes.tables_to_drop.size();i++)
	{
		if (catalog_changes.tables_to_drop[i].equals(name))
		{
			return nullptr; // Table is marked for deletion
		}
	}

	// Check pending creations
	Structure *pending = catalog_changes.tables_to_create.get(name);
	if (pending)
	{
		return pending;
	}

	// Check actual catalog
	return catalog.get(name);
}

// Format error message
static const char *
format_error(const char *fmt, ...)
{
	char   *buffer = (char *)arena<parser_arena>::alloc(256);
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, 256, fmt, args);
	va_end(args);
	return buffer;
}

// ============================================================================
// EXPRESSION RESOLUTION
// ============================================================================

static bool
semantic_resolve_expr(Expr *expr, Structure *table)
{
	if (!expr)
		return true;

	switch (expr->type)
	{
	case EXPR_LITERAL:
		// Literals are already typed from parser
		expr->sem.resolved_type = expr->lit_type;
		expr->sem.is_resolved = true;
		return true;

	case EXPR_NULL:
		expr->sem.resolved_type = TYPE_NULL;
		expr->sem.is_resolved = true;
		return true;

	case EXPR_COLUMN: {
		// Find column in table
		for (uint32_t i = 0; i < table->columns.size();i++)
		{
			if (strcmp(expr->column_name.c_str(), table->columns[i].name) == 0)
			{
				expr->sem.column_index = i;
				expr->sem.resolved_type = table->columns[i].type;
				expr->sem.table = table;
				expr->sem.is_resolved = true;
				return true;
			}
		}
		return false; // Column not found
	}

	case EXPR_BINARY_OP: {
		// Recursively resolve operands
		if (!semantic_resolve_expr(expr->left, table))
		{
			return false;
		}
		if (!semantic_resolve_expr(expr->right, table))
		{
			return false;
		}

		// Comparison and logical operators always return boolean (TYPE_U32)
		switch (expr->op)
		{
		case OP_EQ:
		case OP_NE:
		case OP_LT:
		case OP_LE:
		case OP_GT:
		case OP_GE:
		case OP_AND:
		case OP_OR:
			expr->sem.resolved_type = TYPE_U32;
			break;

		default:
			expr->sem.resolved_type = TYPE_U32;
		}

		expr->sem.is_resolved = true;
		return true;
	}

	case EXPR_UNARY_OP: {
		if (!semantic_resolve_expr(expr->operand, table))
		{
			return false;
		}

		if (expr->unary_op == OP_NOT)
		{
			expr->sem.resolved_type = TYPE_U32; // Boolean
		}
		else if (expr->unary_op == OP_NEG)
		{
			expr->sem.resolved_type = expr->operand->sem.resolved_type;
		}

		expr->sem.is_resolved = true;
		return true;
	}

	default:
		return false;
	}
}

// ============================================================================
// SELECT STATEMENT RESOLUTION
// ============================================================================

static bool
semantic_resolve_select(SelectStmt *stmt, const char **error, const char **context)
{
	// 1. Validate table exists
	Structure *table = lookup_table(stmt->table_name);
	if (!table)
	{
		*error = "Table does not exist";
		*context = stmt->table_name.c_str();
		return false;
	}
	stmt->sem.table = table;

	// 2. Resolve column list
	if (stmt->is_star)
	{
		// SELECT * - get all columns
		stmt->sem.column_indices.clear();
		stmt->sem.column_types.clear();

		for (uint32_t i = 0; i < table->columns.size();i++)
		{
			stmt->sem.column_indices.push(i);
			stmt->sem.column_types.push(table->columns[i].type);
		}
	}
	else
	{
		// Specific columns
		stmt->sem.column_indices.clear();
		stmt->sem.column_types.clear();

		for (uint32_t i = 0; i < stmt->columns.size();i++)
		{
			bool found = false;
			for (uint32_t j = 0; j < table->columns.size();j++)
			{
				if (strcmp(stmt->columns[i].c_str(), table->columns[j].name) == 0)
				{
					stmt->sem.column_indices.push(j);
					stmt->sem.column_types.push(table->columns[j].type);
					found = true;
					break;
				}
			}
			if (!found)
			{
				*error = "Column does not exist in table";
				*context = stmt->columns[i].c_str();
				return false;
			}
		}
	}

	// 3. Resolve WHERE clause if present
	if (stmt->where_clause)
	{
		if (!semantic_resolve_expr(stmt->where_clause, table))
		{
			*error = "Invalid expression in WHERE clause";
			*context = nullptr;
			return false;
		}

		// WHERE should evaluate to boolean
		if (stmt->where_clause->sem.resolved_type != TYPE_U32 && stmt->where_clause->sem.resolved_type != TYPE_NULL)
		{
			*error = "WHERE clause must evaluate to boolean";
			*context = nullptr;
			return false;
		}
	}

	// 4. Resolve ORDER BY column if present
	if (stmt->order_by_column.size()> 0)
	{
		bool found = false;
		for (uint32_t i = 0; i < table->columns.size();i++)
		{
			if (strcmp(stmt->order_by_column.c_str(), table->columns[i].name) == 0)
			{
				stmt->sem.order_by_index = i;
				found = true;
				break;
			}
		}
		if (!found)
		{
			*error = "ORDER BY column does not exist in table";
			*context = stmt->order_by_column.c_str();
			return false;
		}
	}

	stmt->sem.is_resolved = true;
	return true;
}

// ============================================================================
// INSERT STATEMENT RESOLUTION
// ============================================================================

static bool
semantic_resolve_insert(InsertStmt *stmt, const char **error, const char **context)
{
	// 1. Validate table exists
	Structure *table = lookup_table(stmt->table_name);
	if (!table)
	{
		*error = "Table does not exist";
		*context = stmt->table_name.c_str();
		return false;
	}
	stmt->sem.table = table;

	// 2. Resolve column list or use all columns
	if (stmt->columns.size() > 0)
	{
		// Explicit column list
		stmt->sem.column_indices.clear();

		for (uint32_t i = 0; i < stmt->columns.size();i++)
		{
			bool found = false;
			for (uint32_t j = 0; j < table->columns.size();j++)
			{
				if (strcmp(stmt->columns[i].c_str(), table->columns[j].name) == 0)
				{
					stmt->sem.column_indices.push(j);
					found = true;
					break;
				}
			}
			if (!found)
			{
				*error = "Column does not exist in table";
				*context = stmt->columns[i].c_str();
				return false;
			}
		}
	}
	else
	{
		// No column list - use all columns in order
		stmt->sem.column_indices.clear();
		for (uint32_t i = 0; i < table->columns.size();i++)
		{
			stmt->sem.column_indices.push(i);
		}
	}

	// 3. Validate value count matches column count
	if (stmt->values.size ()!= stmt->sem.column_indices.size())
	{
		*error =
			format_error("Value count mismatch: expected %u, got %u", stmt->sem.column_indices.size(), stmt->values.size());
		*context = stmt->table_name.c_str();
		return false;
	}

	// 4. Resolve value expressions and check type compatibility
	for (uint32_t i = 0; i < stmt->values.size();i++)
	{
		Expr	*expr = stmt->values[i];
		uint32_t col_idx = stmt->sem.column_indices[i];
		DataType expected_type = table->columns[col_idx].type;

		// Only literals and NULL allowed in INSERT VALUES
		if (expr->type != EXPR_LITERAL && expr->type != EXPR_NULL)
		{
			*error = "Only literal values allowed in INSERT";
			*context = table->columns[col_idx].name;
			return false;
		}

		if (expr->type == EXPR_LITERAL)
		{
			// Check type compatibility (simple: must match exactly)
			if (expr->lit_type != expected_type)
			{
				*error = format_error("Type mismatch for column '%s': expected %s, got %s",
									  table->columns[col_idx].name, expected_type == TYPE_U32 ? "INT" : "TEXT",
									  expr->lit_type == TYPE_U32 ? "INT" : "TEXT");
				*context = table->columns[col_idx].name;
				return false;
			}
		}

		expr->sem.resolved_type = expected_type;
		expr->sem.is_resolved = true;
	}

	stmt->sem.is_resolved = true;
	return true;
}

// ============================================================================
// UPDATE STATEMENT RESOLUTION
// ============================================================================

static bool
semantic_resolve_update(UpdateStmt *stmt, const char **error, const char **context)
{
	// 1. Validate table exists
	Structure *table = lookup_table(stmt->table_name);
	if (!table)
	{
		*error = "Table does not exist";
		*context = stmt->table_name.c_str();
		return false;
	}
	stmt->sem.table = table;

	// 2. Resolve column indices
	stmt->sem.column_indices.clear();
	for (uint32_t i = 0; i < stmt->columns.size();i++)
	{
		bool found = false;
		for (uint32_t j = 0; j < table->columns.size();j++)
		{
			if (strcmp(stmt->columns[i].c_str(), table->columns[j].name) == 0)
			{
				stmt->sem.column_indices.push(j);
				found = true;
				break;
			}
		}
		if (!found)
		{
			*error = "Column does not exist in table";
			*context = stmt->columns[i].c_str();
			return false;
		}
	}

	// 3. Resolve value expressions
	for (uint32_t i = 0; i < stmt->values.size();i++)
	{
		Expr	*expr = stmt->values[i];
		uint32_t col_idx = stmt->sem.column_indices[i];
		DataType expected_type = table->columns[col_idx].type;

		// Only literals and NULL allowed in SET values
		if (expr->type != EXPR_LITERAL && expr->type != EXPR_NULL)
		{
			*error = "Only literal values allowed in UPDATE SET";
			*context = table->columns[col_idx].name;
			return false;
		}

		if (expr->type == EXPR_LITERAL)
		{
			// Check type compatibility
			if (expr->lit_type != expected_type)
			{
				*error = format_error("Type mismatch for column '%s'", table->columns[col_idx].name);
				*context = table->columns[col_idx].name;
				return false;
			}
		}

		expr->sem.resolved_type = expected_type;
		expr->sem.is_resolved = true;
	}

	// 4. Resolve WHERE clause if present
	if (stmt->where_clause)
	{
		if (!semantic_resolve_expr(stmt->where_clause, table))
		{
			*error = "Invalid expression in WHERE clause";
			*context = nullptr;
			return false;
		}

		// WHERE should evaluate to boolean
		if (stmt->where_clause->sem.resolved_type != TYPE_U32 && stmt->where_clause->sem.resolved_type != TYPE_NULL)
		{
			*error = "WHERE clause must evaluate to boolean";
			*context = nullptr;
			return false;
		}
	}

	stmt->sem.is_resolved = true;
	return true;
}

// ============================================================================
// DELETE STATEMENT RESOLUTION
// ============================================================================

static bool
semantic_resolve_delete(DeleteStmt *stmt, const char **error, const char **context)
{
	// 1. Validate table exists
	Structure *table = lookup_table(stmt->table_name);
	if (!table)
	{
		*error = "Table does not exist";
		*context = stmt->table_name.c_str();
		return false;
	}
	stmt->sem.table = table;

	// 2. Resolve WHERE clause if present
	if (stmt->where_clause)
	{
		if (!semantic_resolve_expr(stmt->where_clause, table))
		{
			*error = "Invalid expression in WHERE clause";
			*context = nullptr;
			return false;
		}

		// WHERE should evaluate to boolean
		if (stmt->where_clause->sem.resolved_type != TYPE_U32 && stmt->where_clause->sem.resolved_type != TYPE_NULL)
		{
			*error = "WHERE clause must evaluate to boolean";
			*context = nullptr;
			return false;
		}
	}

	stmt->sem.is_resolved = true;
	return true;
}

// ============================================================================
// CREATE TABLE STATEMENT RESOLUTION
// ============================================================================

static bool
semantic_resolve_create_table(CreateTableStmt *stmt, const char **error, const char **context)
{
	// Check if table already exists
	Structure *existing = lookup_table(stmt->table_name);
	if (existing)
	{
		*error = "Table already exists";
		*context = stmt->table_name.c_str();
		return false;
	}

	// Validate we have at least one column
	if (stmt->columns.size() == 0)
	{
		*error = "Table must have at least one column";
		*context = stmt->table_name.c_str();
		return false;
	}

	// Check for duplicate column names
	for (uint32_t i = 0; i < stmt->columns.size();i++)
	{
		for (uint32_t j = i + 1; j < stmt->columns.size();j++)
		{
			if (stmt->columns[i].name.equals(stmt->columns[j].name))
			{
				*error = "Duplicate column name";
				*context = stmt->columns[i].name.c_str();
				return false;
			}
		}
	}

	// Build Structure for pending catalog
	array<Column> cols;
	for (uint32_t i = 0; i < stmt->columns.size();i++)
	{
		ColumnDef &def = stmt->columns[i];

		// Validate type (should already be TYPE_U32 or TYPE_CHAR32 from parser)
		if (def.type != TYPE_U32 && def.type != TYPE_CHAR32)
		{
			*error = "Invalid column type";
			*context = def.name.c_str();
			return false;
		}

		// Allocate name in catalog arena for persistence
		char *persistent_name = (char *)arena<catalog_arena>::alloc(def.name.length() + 1);
		strcpy(persistent_name, def.name.c_str());

		cols.push(Column{persistent_name, def.type});
	}

	// Allocate table name in catalog arena
	char *persistent_table_name = (char *)arena<catalog_arena>::alloc(stmt->table_name.length() + 1);
	strcpy(persistent_table_name, stmt->table_name.c_str());

	Structure new_structure = Structure::from(persistent_table_name, cols);

	// Store in semantic info for later use
	stmt->sem.created_structure = (Structure *)arena<parser_arena>::alloc(sizeof(Structure));
	*stmt->sem.created_structure = new_structure;

	// Add to pending catalog changes
	catalog_changes.tables_to_create.insert(stmt->table_name, new_structure);

	stmt->sem.is_resolved = true;
	return true;
}

// ============================================================================
// DROP TABLE STATEMENT RESOLUTION
// ============================================================================

static bool
semantic_resolve_drop_table(DropTableStmt *stmt, const char **error, const char **context)
{
	Structure *table = lookup_table(stmt->table_name);
	if (!table)
	{
		*error = "Table does not exist";
		*context = stmt->table_name.c_str();
		return false;
	}

	stmt->sem.table = table;

	// Add to pending drops
	catalog_changes.tables_to_drop.push(stmt->table_name);

	stmt->sem.is_resolved = true;
	return true;
}

// ============================================================================
// STATEMENT RESOLUTION DISPATCHER
// ============================================================================

static bool
semantic_resolve_statement(Statement *stmt, const char **error, const char **context)
{
	switch (stmt->type)
	{
	case STMT_SELECT:
		return semantic_resolve_select(&stmt->select_stmt, error, context);

	case STMT_INSERT:
		return semantic_resolve_insert(&stmt->insert_stmt, error, context);

	case STMT_UPDATE:
		return semantic_resolve_update(&stmt->update_stmt, error, context);

	case STMT_DELETE:
		return semantic_resolve_delete(&stmt->delete_stmt, error, context);

	case STMT_CREATE_TABLE:
		return semantic_resolve_create_table(&stmt->create_table_stmt, error, context);

	case STMT_DROP_TABLE:
		return semantic_resolve_drop_table(&stmt->drop_table_stmt, error, context);

	case STMT_BEGIN:
	case STMT_COMMIT:
	case STMT_ROLLBACK:
		// Transaction statements need no semantic analysis
		stmt->sem.is_resolved = true;
		return true;

	default:
		*error = "Unknown statement type";
		*context = nullptr;
		return false;
	}
}

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================

SemanticResult
semantic_analyze(array<Statement *, parser_arena> *statements)
{
	SemanticResult result;
	result.success = true;
	result.error = nullptr;
	result.error_context = nullptr;
	result.failed_statement_index = -1;

	// Clear any previous pending changes
	clear_catalog_changes();

	// Analyze each statement
	for (uint32_t i = 0; i < statements->size(); i++)
	{
		Statement*stmt = statements->data()[i];

		const char *error = nullptr;
		const char *context = nullptr;

		if (!semantic_resolve_statement(stmt, &error, &context))
		{
			// Early exit on first error
			result.success = false;
			result.error = error;
			result.error_context = context;
			result.failed_statement_index = i;

			// Clear pending changes since we're not applying them
			clear_catalog_changes();

			return result;
		}

		stmt->sem.is_resolved = true;
	}

	// All statements validated successfully - apply catalog changes
	apply_catalog_changes();

	return result;
}
