
// semantic.cpp
#include "semantic.hpp"
#include "catalog.hpp"
#include "common.hpp"
#include "parser.hpp"

bool
semantic_resolve_create_table(CreateTableStmt *stmt, SemanticContext *ctx)
{
	// Check if table already exists
	if (!stmt->if_not_exists)
	{
		Structure *existing = catalog.get(stmt->table_name);
		if (existing)
		{
			ctx->add_error("Table already exists", stmt->table_name);
			return false;
		}
	}

	// Validate column definitions
	if (!stmt->columns.size || stmt->columns.size == 0)
	{
		ctx->add_error("Table must have at least one column", stmt->table_name);
		return false;
	}

	// Check for duplicate column names
	hash_set<string<query_arena>, query_arena> column_names;
	for (uint32_t i = 0; i < stmt->columns.size; i++)
	{
		ColumnDef *col = stmt->columns[i];

		// Check duplicate;
		if (column_names.contains(col->name))
		{
			ctx->add_error("Duplicate column name", col->name);
			return false;
		}
		column_names.insert(col->name);

		// Mark BLOB columns
		if (strcmp(col->name, "blob") == 0 || strstr(col->name, "_blob") != nullptr)
		{
			col->sem.is_blob_ref = true;
		}

		// Validate type
		if (col->type == TYPE_NULL)
		{
			ctx->add_error("Invalid column type", col->name);
			return false;
		}
	}

	// Build Structure for shadow catalog
	array<Column> cols;
	for (uint32_t i = 0; i < stmt->columns.size; i++)
	{
		ColumnDef *def = stmt->columns[i];
		cols.push(Column{def->name, def->type});
	}

	Structure *new_structure = (Structure *)arena::alloc<query_arena>(sizeof(Structure));

	*new_structure = Structure::from(stmt->table_name, cols);

	// Add to shadow catalog
	// catalog stmt->table_name, new_structure;

	catalog[stmt->table_name] = *new_structure;
	// Store in semantic info
	stmt->sem.created_structure = new_structure;
	stmt->sem.is_resolved = true;

	return true;
}


bool
semantic_resolve_insert(InsertStmt *stmt, SemanticContext *ctx)
{
	// 1. Validate table exists
	Structure *table = catalog.get(stmt->table_name);
	if (!table)
	{
		ctx->add_error("Table does not exist", stmt->table_name);
		return false;
	}
	stmt->sem.table = table;

	// 2. Resolve column list or use all columns
	if (stmt->columns.size > 0)
	{
		// Explicit column list - validate each column exists
		stmt->sem.column_indices.reserve(stmt->columns.size);

		for (uint32_t i = 0; i < stmt->columns.size; i++)
		{
			bool found = false;
			for (uint32_t j = 0; j < table->columns.size; j++)
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
				ctx->add_error("Column does not exist", stmt->columns[i]);
				return false;
			}
		}
	}
	else
	{
		// No column list - use all columns in order
		stmt->sem.column_indices.reserve(table->columns.size);
		for (uint32_t i = 0; i < table->columns.size; i++)
		{
			stmt->sem.column_indices.push(i);
		}
	}

	// 3. Validate value rows
	uint32_t expected_columns = stmt->sem.column_indices.size;

	for (uint32_t row = 0; row < stmt->values.size; row++)
	{
		auto *value_row = stmt->values[row];

		if (value_row->size != expected_columns)
		{
			ctx->add_error("Value count mismatch", nullptr);
			return false;
		}

		// 4. Validate each value expression (simplified - just check literals for now)
		for (uint32_t col = 0; col < value_row->size; col++)
		{
			Expr	*expr = value_row->data[col];
			uint32_t table_col_idx = stmt->sem.column_indices[col];
			DataType expected_type = table->columns[table_col_idx].type;

			// For now, just handle literals
			if (expr->type == EXPR_LITERAL)
			{
				// Basic type compatibility check
				if (!types_compatible(expr->lit_type, expected_type))
				{
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

// Add to semantic.cpp

bool
semantic_resolve_column(Expr *expr, Structure *table, SemanticContext *ctx)
{
	if (expr->type != EXPR_COLUMN)
	{
		return true; // Not a column, let other resolvers handle it
	}

	// Handle * (star) - special case
	if (expr->column_name.equals("*"))
	{
		expr->sem.is_resolved = true;
		return true;
	}

	// Find column in table
	for (uint32_t i = 0; i < table->columns.size; i++)
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

	ctx->add_error("Column not found", expr->column_name.c_str());
	return false;
}

bool
semantic_resolve_expr(Expr *expr, Structure *table, SemanticContext *ctx)
{
	switch (expr->type)
	{
	case EXPR_COLUMN:
		return semantic_resolve_column(expr, table, ctx);

	case EXPR_STAR:
		expr->sem.is_resolved = true;
		return true;

	case EXPR_LITERAL:
		expr->sem.resolved_type = expr->lit_type;
		expr->sem.is_resolved = true;
		return true;

	case EXPR_NULL:
		expr->sem.resolved_type = TYPE_NULL;
		expr->sem.is_resolved = true;
		return true;

	// TODO: Handle other expression types (binary ops, functions, etc)
	default:
		return true;
	}
}

bool
semantic_resolve_select(SelectStmt *stmt, SemanticContext *ctx)
{
	// 1. Resolve FROM table
	if (!stmt->from_table)
	{
		ctx->add_error("SELECT requires FROM clause", nullptr);
		return false;
	}

	Structure *table = catalog.get(stmt->from_table->table_name);
	if (!table)
	{
		ctx->add_error("Table does not exist", stmt->from_table->table_name.c_str());
		return false;
	}
	stmt->from_table->sem.resolved = table;

	// 2. Resolve SELECT list
	for (uint32_t i = 0; i < stmt->select_list.size; i++)
	{
		Expr *expr = stmt->select_list[i];

		// Handle SELECT * specially
		if (expr->type == EXPR_STAR)
		{
			// Record that we're selecting all columns
			stmt->sem.output_types.reserve(table->columns.size);
			stmt->sem.output_names.reserve(table->columns.size);

			for (uint32_t j = 0; j < table->columns.size; j++)
			{
				stmt->sem.output_types.push(table->columns[j].type);

				string<parser_arena> col_name;
				col_name.set(table->columns[j].name);
				stmt->sem.output_names.push(col_name);
			}
			expr->sem.is_resolved = true;
		}
		else
		{
			// Resolve regular expression
			if (!semantic_resolve_expr(expr, table, ctx))
			{
				return false;
			}

			// Add to output schema
			stmt->sem.output_types.push(expr->sem.resolved_type);

			// Generate output name (column name or expr_N)
			string<parser_arena> output_name;
			if (expr->type == EXPR_COLUMN)
			{
				output_name = expr->column_name;
			}
			else
			{
				char name_buf[32];
				snprintf(name_buf, sizeof(name_buf), "expr_%u", i);
				output_name.set(name_buf);
			}
			stmt->sem.output_names.push(output_name);
		}
	}

	// 3. Resolve WHERE clause if present
	if (stmt->where_clause)
	{
		if (!semantic_resolve_expr(stmt->where_clause, table, ctx))
		{
			return false;
		}
	}

	// TODO: Handle JOINs, GROUP BY, HAVING, ORDER BY

	stmt->sem.is_resolved = true;
	return true;
}

// Main entry point for statement semantic analysis
bool
semantic_resolve_statement(Statement *stmt, SemanticContext *ctx)
{
	switch (stmt->type)
	{
	case STMT_CREATE_TABLE:
		return semantic_resolve_create_table(stmt->create_table_stmt, ctx);

	case STMT_INSERT:
		return semantic_resolve_insert(stmt->insert_stmt, ctx);

	case STMT_SELECT:
		return semantic_resolve_select(stmt->select_stmt, ctx);

	default:
		ctx->add_error("Unsupported statement type", nullptr);
		return false;
	}
}
