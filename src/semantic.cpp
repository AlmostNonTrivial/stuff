
// semantic.cpp
#include "semantic.hpp"
#include "catalog.hpp"
#include "common.hpp"
#include "parser.hpp"

// In semantic.cpp, add this function:
bool
semantic_resolve_create_index(CreateIndexStmt *stmt, SemanticContext *ctx)
{
	// 1. Validate table exists
	Structure *table = catalog.get(stmt->table_name);
	if (!table)
	{
		ctx->add_error("Table does not exist", stmt->table_name.c_str());
		return false;
	}
	stmt->sem.table = table;

	// 2. Check column count - we only support 1 or 2 columns
	if (stmt->columns.size > 2)
	{
		ctx->add_error("Indexes support at most 2 columns", stmt->index_name.c_str());
		return false;
	}

	if (stmt->columns.size == 0)
	{
		ctx->add_error("Index must specify at least one column", stmt->index_name.c_str());
		return false;
	}

	// 3. Validate columns exist and get their indices
	stmt->sem.column_indices.clear();
	stmt->sem.column_indices.reserve(2); // Always 2 for DUAL

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
			ctx->add_error("Column does not exist in table", stmt->columns[i].c_str());
			return false;
		}
	}

	// 4. If only 1 column specified, add primary key (column 0) as second
	if (stmt->columns.size == 1)
	{
		// Check that the specified column isn't already the PK
		if (stmt->sem.column_indices[0] == 0)
		{
			ctx->add_error("Cannot create single-column index on primary key", nullptr);
			return false;
		}
		stmt->sem.column_indices.push(0); // Add PK as second column
	}

	// 5. Build index structure with DUAL key
	array<Column> index_cols;

	DataType type1 = table->columns[stmt->sem.column_indices[0]].type;
	DataType type2 = table->columns[stmt->sem.column_indices[1]].type;
	DataType dual_type = make_dual(type1, type2);

	index_cols.push(Column{"key", dual_type});

	// Add index structure to catalog
	Structure *index_structure = (Structure *)arena::alloc<query_arena>(sizeof(Structure));
	*index_structure = Structure::from(stmt->index_name.c_str(), index_cols);

	catalog[stmt->index_name] = *index_structure;

	string<catalog_arena> s;
	string<catalog_arena> ss;

	s.set(stmt->table_name);
	ss.set(stmt->index_name);
	table_to_index.insert(s, ss);

	stmt->sem.is_resolved = true;
	return true;
}

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
		// Make sure lit_type is set from parser
		expr->sem.resolved_type = expr->lit_type;
		expr->sem.is_resolved = true;
		return true;

	case EXPR_NULL:
		expr->sem.resolved_type = TYPE_NULL;
		expr->sem.is_resolved = true;
		return true;

	case EXPR_BINARY_OP: {
		// Recursively resolve left and right operands
		if (!semantic_resolve_expr(expr->left, table, ctx))
		{
			return false;
		}
		if (!semantic_resolve_expr(expr->right, table, ctx))
		{
			return false;
		}

		// Determine result type based on operation
		DataType left_type = expr->left->sem.resolved_type;
		DataType right_type = expr->right->sem.resolved_type;

		switch (expr->op)
		{
		// Comparison operators always return U32 (boolean)
		case OP_EQ:
		case OP_NE:
		case OP_LT:
		case OP_LE:
		case OP_GT:
		case OP_GE:
			expr->sem.resolved_type = TYPE_U32;
			break;

		// Logical operators expect and return U32 (boolean)
		case OP_AND:
		case OP_OR:
			expr->sem.resolved_type = TYPE_U32;
			break;

		// Arithmetic operators - use wider type
		case OP_ADD:
		case OP_SUB:
		case OP_MUL:
		case OP_DIV:
		case OP_MOD:
			// Simple type promotion - use the larger type
			expr->sem.resolved_type = (type_size(left_type) >= type_size(right_type)) ? left_type : right_type;
			break;

		default:
			expr->sem.resolved_type = TYPE_NULL;
		}

		expr->sem.is_resolved = true;
		return true;
	}

	case EXPR_UNARY_OP: {
		// Resolve operand
		if (!semantic_resolve_expr(expr->operand, table, ctx))
		{
			return false;
		}

		// Result type depends on operator
		if (expr->unary_op == OP_NOT)
		{
			expr->sem.resolved_type = TYPE_U32; // Boolean result
		}
		else if (expr->unary_op == OP_NEG)
		{
			expr->sem.resolved_type = expr->operand->sem.resolved_type;
		}

		expr->sem.is_resolved = true;
		return true;
	}

	// TODO: Handle EXPR_FUNCTION, EXPR_SUBQUERY, EXPR_LIST
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

bool
semantic_resolve_delete(DeleteStmt *stmt, SemanticContext *ctx)
{
	// 1. Validate table exists
	Structure *table = catalog.get(stmt->table_name);
	if (!table)
	{
		ctx->add_error("Table does not exist", stmt->table_name.c_str());
		return false;
	}
	stmt->sem.table = table;

	// 2. Resolve WHERE clause if present
	if (stmt->where_clause)
	{
		if (!semantic_resolve_expr(stmt->where_clause, table, ctx))
		{
			return false;
		}

		// WHERE clause should evaluate to boolean
		if (stmt->where_clause->sem.resolved_type != TYPE_U32 && stmt->where_clause->sem.resolved_type != TYPE_NULL)
		{
			ctx->add_error("WHERE clause must evaluate to boolean", nullptr);
			return false;
		}
	}

	stmt->sem.is_resolved = true;
	return true;
}

// DROP INDEX semantic resolution
bool
semantic_resolve_drop_index(DropIndexStmt *stmt, SemanticContext *ctx)
{
	Structure *index = catalog.get(stmt->index_name);
	if (!index)
	{
		if (!stmt->if_exists)
		{
			ctx->add_error("Index does not exist", stmt->index_name.c_str());
			return false;
		}
		stmt->sem.is_resolved = true;
		return true;
	}

	stmt->sem.is_resolved = true;
	return true;
}

// DROP TABLE semantic resolution
bool
semantic_resolve_drop_table(DropTableStmt *stmt, SemanticContext *ctx)
{
	Structure *table = catalog.get(stmt->table_name);
	if (!table)
	{
		if (!stmt->if_exists)
		{
			ctx->add_error("Table does not exist", stmt->table_name.c_str());
			return false;
		}
		stmt->sem.is_resolved = true;
		return true;
	}

	stmt->sem.table = table;
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
	case STMT_CREATE_INDEX:
		return semantic_resolve_create_index(stmt->create_index_stmt, ctx);
	case STMT_DROP_INDEX:
		return semantic_resolve_drop_index(stmt->drop_index_stmt, ctx);
	case STMT_DROP_TABLE:
		return semantic_resolve_drop_table(stmt->drop_table_stmt, ctx);
	case STMT_DELETE:
		return semantic_resolve_delete(stmt->delete_stmt, ctx);
	case STMT_INSERT:
		return semantic_resolve_insert(stmt->insert_stmt, ctx);
	case STMT_SELECT:
		return semantic_resolve_select(stmt->select_stmt, ctx);

	default:
		ctx->add_error("Unsupported statement type", nullptr);
		return false;
	}
}
