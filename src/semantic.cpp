// ============================================================================
// semantic.cpp
// ============================================================================
#include "semantic.hpp"
#include "arena.hpp"
#include "containers.hpp"
#include "catalog.hpp"
#include "common.hpp"
#include "parser.hpp"
#include "types.hpp"
#include <cstring>
#include <cstdio>

// Result tracking for current analysis
static struct
{
	string_view error;
	string_view context;
} current_result;

// Static catalog changes tracker
static struct
{
	// Tables to be created (name -> Relation)
	hash_map<string_view, Relation, query_arena> tables_to_create;

	// Tables to be dropped (just names)
	hash_set<string_view, query_arena> tables_to_drop;
} catalog_changes;

// ============================================================================
// Helper Functions
// ============================================================================

// Set error in current result
static void
set_error(string_view error, string_view context = {})
{
	current_result.error = error;
	current_result.context = context;
}

// Helper to lookup table (checks both real catalog and pending changes)
static Relation *
lookup_table(string_view name)
{
	// Check if it was deleted in a previous statement
	if (catalog_changes.tables_to_drop.contains(name))
	{
		return nullptr;
	}

	// Check pending creations
	Relation *pending = catalog_changes.tables_to_create.get(name);
	if (pending)
	{
		return pending;
	}

	// Check actual catalog
	return catalog.get(name);
}

// Helper to find a column index in a table
static int32_t
find_column_index(Relation *table, string_view column_name)
{
	for (uint32_t i = 0; i < table->columns.size(); i++)
	{
		if (column_name.compare(table->columns[i].name) == 0)
		{
			return i;
		}
	}
	return -1;
}

// Helper to resolve multiple columns and populate indices
static bool
resolve_column_list(Relation *table, const array<string_view, query_arena> &column_names,
					array<int32_t, query_arena> &out_indices)
{

	out_indices.clear();

	for (uint32_t i = 0; i < column_names.size(); i++)
	{
		int32_t idx = find_column_index(table, column_names[i]);

		if (idx < 0)
		{
			set_error("Column does not exist in table", column_names[i]);
			return false;
		}

		out_indices.push(idx);
	}
	return true;
}

// Get SQL type name for error messages
static const char *
sql_type_name(DataType type)
{
	return (type == TYPE_U32) ? "INT" : "TEXT";
}

// Require table exists and optionally set output field
static Relation *
require_table(string_view table_name, Relation **out_field)
{
	Relation *table = lookup_table(table_name);
	if (!table)
	{
		set_error("Table does not exist", table_name);
		return nullptr;
	}
	if (out_field)
	{
		*out_field = table;
	}
	return table;
}

// Validate literal value expression for INSERT/UPDATE
static bool
validate_literal_value(Expr *expr, DataType expected_type, const char *column_name, const char *operation)
{
	// Only literals and NULL allowed
	if (expr->type != EXPR_LITERAL && expr->type != EXPR_NULL)
	{
		set_error(format_error("Only literal values allowed in %s", operation), column_name);
		return false;
	}

	if (expr->type == EXPR_LITERAL)
	{
		// Check type compatibility
		if (expr->lit_type != expected_type)
		{
			set_error(format_error("Type mismatch for column '%s': expected %s, got %s", column_name,
								   sql_type_name(expected_type), sql_type_name(expr->lit_type)),
					  column_name);
			return false;
		}
	}

	expr->sem.resolved_type = expected_type;
	expr->sem.is_resolved = true;
	return true;
}

// Apply all pending changes to the actual catalog
static void
apply_catalog_changes()
{
	// Remove dropped tables first
	for (auto [name, _] : catalog_changes.tables_to_drop)
	{
		catalog_delete_relation(name);
	}

	for (auto [name, relation] : catalog_changes.tables_to_create)
	{
		catalog_add_relation(relation);
	}
}

// Clear without applying
static void
clear_catalog_changes()
{
	catalog_changes.tables_to_create.clear();
	catalog_changes.tables_to_drop.clear();
}

// ============================================================================
// EXPRESSION RESOLUTION
// ============================================================================

static bool
semantic_resolve_expr(Expr *expr, Relation *table)
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
		int32_t idx = find_column_index(table, expr->column_name);
		if (idx < 0)
		{
			set_error("Column not found", expr->column_name);
			return false;
		}

		expr->sem.column_index = idx;
		expr->sem.resolved_type = table->columns[idx].type;
		expr->sem.table = table;
		expr->sem.is_resolved = true;
		return true;
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
		set_error("Unknown expression type");
		return false;
	}
}

// Resolve WHERE clause (shared by SELECT, UPDATE, DELETE)
static bool
resolve_where_clause(Expr *where_clause, Relation *table)
{
	if (!where_clause)
	{
		return true; // No WHERE clause is valid
	}

	if (!semantic_resolve_expr(where_clause, table))
	{
		if (current_result.error.empty())
		{
			set_error("Invalid expression in WHERE clause");
		}
		return false;
	}

	// WHERE should evaluate to boolean
	if (where_clause->sem.resolved_type != TYPE_U32 && where_clause->sem.resolved_type != TYPE_NULL)
	{
		set_error("WHERE clause must evaluate to boolean");
		return false;
	}

	return true;
}

// Resolve INSERT column list (explicit or implicit)
static bool
resolve_insert_columns(InsertStmt *stmt, Relation *table)
{
	if (stmt->columns.size() > 0)
	{
		return resolve_column_list(table, stmt->columns, stmt->sem.column_indices);
	}

	// No column list - use all columns in order
	stmt->sem.column_indices.clear();
	for (uint32_t i = 0; i < table->columns.size(); i++)
	{
		stmt->sem.column_indices.push(i);
	}
	return true;
}

// ============================================================================
// SELECT STATEMENT RESOLUTION
// ============================================================================

static bool
semantic_resolve_select(SelectStmt *stmt)
{
	// 1. Validate table exists
	Relation *table = require_table(stmt->table_name, &stmt->sem.table);
	if (!table)
		return false;

	// 2. Resolve column list
	if (stmt->is_star)
	{
		// SELECT * - get all columns
		stmt->sem.column_indices.clear();
		stmt->sem.column_types.clear();

		for (uint32_t i = 0; i < table->columns.size(); i++)
		{
			stmt->sem.column_indices.push(i);
			stmt->sem.column_types.push(table->columns[i].type);
		}
	}
	else
	{
		// Specific columns
		if (!resolve_column_list(table, stmt->columns, stmt->sem.column_indices))
		{
			return false;
		}

		// Populate types after indices are resolved
		stmt->sem.column_types.clear();
		for (uint32_t i = 0; i < stmt->sem.column_indices.size(); i++)
		{
			stmt->sem.column_types.push(table->columns[stmt->sem.column_indices[i]].type);
		}
	}

	// 3. Resolve WHERE clause if present
	if (!resolve_where_clause(stmt->where_clause, table))
	{
		return false;
	}

	// 4. Resolve ORDER BY column if present
	if (stmt->order_by_column.size() > 0)
	{
		stmt->sem.order_by_index = find_column_index(table, stmt->order_by_column);
		if (stmt->sem.order_by_index < 0)
		{
			set_error("ORDER BY column does not exist in table", stmt->order_by_column);
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
semantic_resolve_insert(InsertStmt *stmt)
{
	// 1. Validate table exists
	Relation *table = require_table(stmt->table_name, &stmt->sem.table);
	if (!table)
		return false;

	// 2. Resolve column list or use all columns
	if (!resolve_insert_columns(stmt, table))
	{
		return false;
	}

	// 3. Validate value count matches column count
	if (stmt->values.size() != stmt->sem.column_indices.size())
	{
		set_error(format_error("Value count mismatch: expected %u, got %u", stmt->sem.column_indices.size(),
							   stmt->values.size()),
				  stmt->table_name);
		return false;
	}

	// 4. Resolve value expressions and check type compatibility
	for (uint32_t i = 0; i < stmt->values.size(); i++)
	{
		Expr	*expr = stmt->values[i];
		uint32_t col_idx = stmt->sem.column_indices[i];
		DataType expected_type = table->columns[col_idx].type;

		if (!validate_literal_value(expr, expected_type, table->columns[col_idx].name, "INSERT"))
		{
			return false;
		}
	}

	stmt->sem.is_resolved = true;
	return true;
}

// ============================================================================
// UPDATE STATEMENT RESOLUTION
// ============================================================================

static bool
semantic_resolve_update(UpdateStmt *stmt)
{
	// 1. Validate table exists
	Relation *table = require_table(stmt->table_name, &stmt->sem.table);
	if (!table)
		return false;

	// 2. Resolve column indices
	if (!resolve_column_list(table, stmt->columns, stmt->sem.column_indices))
	{
		return false;
	}

	// 3. Resolve value expressions
	for (uint32_t i = 0; i < stmt->values.size(); i++)
	{
		Expr	*expr = stmt->values[i];
		uint32_t col_idx = stmt->sem.column_indices[i];
		DataType expected_type = table->columns[col_idx].type;

		if (!validate_literal_value(expr, expected_type, table->columns[col_idx].name, "UPDATE SET"))
		{
			return false;
		}
	}

	// 4. Resolve WHERE clause if present
	if (!resolve_where_clause(stmt->where_clause, table))
	{
		return false;
	}

	stmt->sem.is_resolved = true;
	return true;
}

// ============================================================================
// DELETE STATEMENT RESOLUTION
// ============================================================================

static bool
semantic_resolve_delete(DeleteStmt *stmt)
{
	// 1. Validate table exists
	Relation *table = require_table(stmt->table_name, &stmt->sem.table);
	if (!table)
		return false;

	// 2. Resolve WHERE clause if present
	if (!resolve_where_clause(stmt->where_clause, table))
	{
		return false;
	}

	stmt->sem.is_resolved = true;
	return true;
}

// ============================================================================
// CREATE TABLE STATEMENT RESOLUTION
// ============================================================================

static bool
semantic_resolve_create_table(CreateTableStmt *stmt)
{
	// Check if table already exists
	Relation *existing = lookup_table(stmt->table_name);
	if (existing)
	{
		set_error("Table already exists", stmt->table_name);
		return false;
	}

	// Validate we have at least one column
	if (stmt->columns.size() == 0)
	{
		set_error("Table must have at least one column", stmt->table_name);
		return false;
	}

	// Check for duplicate column names
	for (uint32_t i = 0; i < stmt->columns.size(); i++)
	{
		for (uint32_t j = i + 1; j < stmt->columns.size(); j++)
		{
			if (stmt->columns[i].name.compare(stmt->columns[j].name) == 0)
			{
				set_error("Duplicate column name", stmt->columns[i].name);
				return false;
			}
		}
	}

	// Build Relation for pending catalog
	array<Attribute, query_arena> cols;
	for (uint32_t i = 0; i < stmt->columns.size(); i++)
	{
		ColumnDef &def = stmt->columns[i];

		// Validate type (should already be TYPE_U32 or TYPE_CHAR32 from parser)
		if (def.type != TYPE_U32 && def.type != TYPE_CHAR32)
		{
			set_error("Invalid column type", def.name);
			return false;
		}

		Attribute attr;
		to_str(def.name, attr.name, ATTRIBUTE_NAME_MAX_SIZE);
		attr.type = def.type;

		cols.push(attr);
	}

	Relation new_relation = create_relation(stmt->table_name, cols);

	// Add to pending catalog changes
	catalog_changes.tables_to_create.insert(new_relation.name, new_relation);

	stmt->sem.created_structure = stmt->table_name;
	stmt->sem.is_resolved = true;
	return true;
}

// ============================================================================
// DROP TABLE STATEMENT RESOLUTION
// ============================================================================

static bool
semantic_resolve_drop_table(DropTableStmt *stmt)
{
	Relation *table = lookup_table(stmt->table_name);
	if (!table)
	{
		set_error("Table does not exist", stmt->table_name);
		return false;
	}

	stmt->sem.table = table;

	// Add to pending drops
	catalog_changes.tables_to_drop.insert(stmt->table_name, 1);

	stmt->sem.is_resolved = true;
	return true;
}

// ============================================================================
// STATEMENT RESOLUTION DISPATCHER
// ============================================================================

static bool
semantic_resolve_statement(Statement *stmt)
{
	switch (stmt->type)
	{
	case STMT_SELECT:
		return semantic_resolve_select(&stmt->select_stmt);

	case STMT_INSERT:
		return semantic_resolve_insert(&stmt->insert_stmt);

	case STMT_UPDATE:
		return semantic_resolve_update(&stmt->update_stmt);

	case STMT_DELETE:
		return semantic_resolve_delete(&stmt->delete_stmt);

	case STMT_CREATE_TABLE:
		return semantic_resolve_create_table(&stmt->create_table_stmt);

	case STMT_DROP_TABLE:
		return semantic_resolve_drop_table(&stmt->drop_table_stmt);

	case STMT_BEGIN:
	case STMT_COMMIT:
	case STMT_ROLLBACK:
		// Transaction statements need no semantic analysis
		stmt->sem.is_resolved = true;
		return true;

	default:
		set_error("Unknown statement type");
		return false;
	}
}

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================

SemanticResult
semantic_analyze(array<Statement *, query_arena> &statements)
{
	SemanticResult result;
	result.success = true;
	result.error = {};
	result.error_context = {};
	result.failed_statement_index = -1;

	// Clear any previous result and pending changes
	current_result.error = {};
	current_result.context = {};
	clear_catalog_changes();

	// Analyze each statement
	for (uint32_t i = 0; i < statements.size(); i++)
	{
		Statement *stmt = statements.data()[i];

		// Clear current result for this statement
		current_result.error = {};
		current_result.context = {};

		if (!semantic_resolve_statement(stmt))
		{
			// Early exit on first error
			result.success = false;
			result.error = current_result.error;
			result.error_context = current_result.context;
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
