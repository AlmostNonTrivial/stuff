
// semantic.cpp
#include "semantic.hpp"
#include "catalog.hpp"
#include "common.hpp"
#include "parser.hpp"
#include "types.hpp"

bool
semantic_resolve_select(SelectStmt *stmt, SemanticContext *ctx);
struct TableContext
{
	struct TableBinding
	{
		Structure			*table;
		string<parser_arena> alias; // Empty if no alias
		string<parser_arena> name;	// Original table name
	};
	array<TableBinding, parser_arena> tables;

	// Add a table to the context
	void
	add_table(Structure *table, const string<parser_arena> &name, const string<parser_arena> &alias)
	{
		tables.push({table, alias, name});
	}

	// Find which table a column belongs to
	// Returns true if found, fills out parameters
	bool
	find_column(const char *table_ref, // Table name/alias or nullptr for unqualified
				const char *col_name, int32_t *out_table_idx, int32_t *out_col_idx, DataType *out_type,
				Structure **out_table)
	{

		// If table reference specified, find that specific table
		if (table_ref && strlen(table_ref) > 0)
		{
			for (uint32_t t = 0; t < tables.size; t++)
			{
				// Check both alias and original name
				if ((tables[t].alias.size > 0 && strcmp(tables[t].alias.c_str(), table_ref) == 0) ||
					strcmp(tables[t].name.c_str(), table_ref) == 0)
				{

					// Search columns in this table
					Structure *tbl = tables[t].table;
					for (uint32_t c = 0; c < tbl->columns.size; c++)
					{
						if (strcmp(tbl->columns[c].name, col_name) == 0)
						{
							*out_table_idx = t;
							*out_col_idx = c;
							*out_type = tbl->columns[c].type;
							*out_table = tbl;
							return true;
						}
					}
					return false; // Table found but column not in it
				}
			}
			return false; // Table not found
		}

		// Unqualified column - search all tables
		int32_t	   found_table_idx = -1;
		int32_t	   found_col_idx = -1;
		DataType   found_type;
		Structure *found_table = nullptr;
		int		   matches = 0;

		for (uint32_t t = 0; t < tables.size; t++)
		{
			Structure *tbl = tables[t].table;
			for (uint32_t c = 0; c < tbl->columns.size; c++)
			{
				if (strcmp(tbl->columns[c].name, col_name) == 0)
				{
					matches++;
					found_table_idx = t;
					found_col_idx = c;
					found_type = tbl->columns[c].type;
					found_table = tbl;
				}
			}
		}

		if (matches == 0)
			return false;
		if (matches > 1)
			return false; // Ambiguous column reference

		*out_table_idx = found_table_idx;
		*out_col_idx = found_col_idx;
		*out_type = found_type;
		*out_table = found_table;
		return true;
	}
};

#include "semantic.hpp"
#include "catalog.hpp"
#include <cstring>

// Table context for multi-table queries (flat namespace)
// Check if function is an aggregate
bool
is_aggregate_function(const string<parser_arena> &name)
{
	const char *aggregates[] = {"COUNT", "SUM", "AVG", "MIN", "MAX", nullptr};
	for (int i = 0; aggregates[i]; i++)
	{
		if (strcasecmp(name.c_str(), aggregates[i]) == 0)
		{
			return true;
		}
	}
	return false;
}
// Resolve expression with multi-table context
bool
semantic_resolve_expr_context(Expr *expr, TableContext *table_ctx, SemanticContext *ctx)
{
	switch (expr->type)
	{
	case EXPR_COLUMN: {
		// Handle * specially
		if (expr->column_name.equals("*"))
		{
			expr->sem.is_resolved = true;
			return true;
		}

		int32_t	   table_idx, col_idx;
		DataType   type;
		Structure *table;

		const char *table_ref = expr->table_name.size > 0 ? expr->table_name.c_str() : nullptr;

		if (!table_ctx->find_column(table_ref, expr->column_name.c_str(), &table_idx, &col_idx, &type, &table))
		{
			if (table_ref)
			{
				ctx->add_error("Column not found in table", expr->column_name.c_str());
			}
			else
			{
				ctx->add_error("Column not found or ambiguous", expr->column_name.c_str());
			}
			return false;
		}

		expr->sem.column_index = col_idx;
		expr->sem.resolved_type = type;
		expr->sem.table = table;
		expr->sem.is_resolved = true;
		return true;
	}

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

	case EXPR_BINARY_OP: {
		// Recursively resolve operands
		if (!semantic_resolve_expr_context(expr->left, table_ctx, ctx))
		{
			return false;
		}
		if (!semantic_resolve_expr_context(expr->right, table_ctx, ctx))
		{
			return false;
		}

		DataType left_type = expr->left->sem.resolved_type;
		DataType right_type = expr->right->sem.resolved_type;

		switch (expr->op)
		{
		case OP_EQ:
		case OP_NE:
		case OP_LT:
		case OP_LE:
		case OP_GT:
		case OP_GE:
		case OP_LIKE:
		case OP_IN:
			expr->sem.resolved_type = TYPE_U32; // Boolean
			break;

		case OP_AND:
		case OP_OR:
			expr->sem.resolved_type = TYPE_U32;
			break;

		case OP_ADD:
		case OP_SUB:
		case OP_MUL:
		case OP_DIV:
		case OP_MOD:
			// Use wider type
			expr->sem.resolved_type = (type_size(left_type) >= type_size(right_type)) ? left_type : right_type;
			break;

		default:
			expr->sem.resolved_type = TYPE_NULL;
		}

		expr->sem.is_resolved = true;
		return true;
	}

	case EXPR_UNARY_OP: {
		if (!semantic_resolve_expr_context(expr->operand, table_ctx, ctx))
		{
			return false;
		}

		if (expr->unary_op == OP_NOT)
		{
			expr->sem.resolved_type = TYPE_U32;
		}
		else if (expr->unary_op == OP_NEG)
		{
			expr->sem.resolved_type = expr->operand->sem.resolved_type;
		}

		expr->sem.is_resolved = true;
		return true;
	}

	case EXPR_FUNCTION: {
		// Check if aggregate
		if (is_aggregate_function(expr->func_name))
		{
			expr->sem.is_aggregate = true;

			// Resolve arguments
			for (uint32_t i = 0; i < expr->args.size; i++)
			{
				// COUNT(*) is special case
				if (strcmp(expr->func_name.c_str(), "COUNT") == 0 && expr->args[i]->type == EXPR_STAR)
				{
					expr->args[i]->sem.is_resolved = true;
					continue;
				}

				if (!semantic_resolve_expr_context(expr->args[i], table_ctx, ctx))
				{
					return false;
				}
			}

			// Determine result type
			if (strcasecmp(expr->func_name.c_str(), "COUNT") == 0)
			{
				expr->sem.resolved_type = TYPE_U64;
			}
			else if (strcasecmp(expr->func_name.c_str(), "AVG") == 0)
			{
				expr->sem.resolved_type = TYPE_F64;
			}
			else if (strcasecmp(expr->func_name.c_str(), "SUM") == 0)
			{
				// Use argument type, promote integers to I64
				DataType arg_type = expr->args[0]->sem.resolved_type;
				if (arg_type == TYPE_U32 || arg_type == TYPE_I32)
				{
					expr->sem.resolved_type = TYPE_I64;
				}
				else
				{
					expr->sem.resolved_type = arg_type;
				}
			}
			else
			{ // MIN/MAX
				expr->sem.resolved_type = expr->args[0]->sem.resolved_type;
			}
		}
		else
		{
			// Scalar function - resolve arguments
			for (uint32_t i = 0; i < expr->args.size; i++)
			{
				if (!semantic_resolve_expr_context(expr->args[i], table_ctx, ctx))
				{
					return false;
				}
			}

			// Determine result type based on function
			if (strcasecmp(expr->func_name.c_str(), "UPPER") == 0 ||
				strcasecmp(expr->func_name.c_str(), "LOWER") == 0 || strcasecmp(expr->func_name.c_str(), "SUBSTR") == 0)
			{
				expr->sem.resolved_type = TYPE_CHAR16;
			}
			else if (strcasecmp(expr->func_name.c_str(), "LENGTH") == 0)
			{
				expr->sem.resolved_type = TYPE_I32;
			}
			else
			{
				// Default to first argument type
				expr->sem.resolved_type = expr->args[0]->sem.resolved_type;
			}
		}

		expr->sem.is_resolved = true;
		return true;
	}

	case EXPR_SUBQUERY: {
		// Recursively resolve the subquery
		if (!semantic_resolve_select(expr->subquery, ctx))
		{
			return false;
		}

		// Scalar subquery must return single column
		if (expr->subquery->sem.output_types.size != 1)
		{
			ctx->add_error("Scalar subquery must return single column", nullptr);
			return false;
		}

		expr->sem.resolved_type = expr->subquery->sem.output_types[0];
		expr->sem.is_resolved = true;
		return true;
	}

	case EXPR_LIST: {
		// Resolve all items
		DataType common_type = TYPE_NULL;
		for (uint32_t i = 0; i < expr->list_items.size; i++)
		{
			if (!semantic_resolve_expr_context(expr->list_items[i], table_ctx, ctx))
			{
				return false;
			}

			// Track common type (simplified - just use first non-null)
			if (common_type == TYPE_NULL && expr->list_items[i]->sem.resolved_type != TYPE_NULL)
			{
				common_type = expr->list_items[i]->sem.resolved_type;
			}
		}

		expr->sem.resolved_type = common_type;
		expr->sem.is_resolved = true;
		return true;
	}

	default:
		return true;
	}
}
bool
semantic_resolve_expr(Expr *expr, Structure *table, SemanticContext *ctx)
{
	TableContext		 table_ctx;
	string<parser_arena> name;
	name.set(table->name);
	table_ctx.add_table(table, name, string<parser_arena>());
	return semantic_resolve_expr_context(expr, &table_ctx, ctx);
}
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

// Forward declarations
bool
semantic_resolve_expr_context(Expr *expr, TableContext *table_ctx, SemanticContext *ctx);
bool
semantic_resolve_update(UpdateStmt *stmt, SemanticContext *ctx);

// Helper for single-table context (backwards compatibility)

// Check if expression contains aggregates
bool
expr_has_aggregate(Expr *expr)
{
	if (!expr)
		return false;

	if (expr->sem.is_aggregate)
		return true;

	switch (expr->type)
	{
	case EXPR_BINARY_OP:
		return expr_has_aggregate(expr->left) || expr_has_aggregate(expr->right);
	case EXPR_UNARY_OP:
		return expr_has_aggregate(expr->operand);
	case EXPR_FUNCTION:
		if (expr->sem.is_aggregate)
			return true;
		for (uint32_t i = 0; i < expr->args.size; i++)
		{
			if (expr_has_aggregate(expr->args[i]))
				return true;
		}
		return false;
	default:
		return false;
	}
}

// Check if two expressions are semantically equivalent (for GROUP BY validation)
bool
expr_equivalent(Expr *a, Expr *b)
{
	if (a->type != b->type)
		return false;

	switch (a->type)
	{
	case EXPR_COLUMN:
		return a->sem.table == b->sem.table && a->sem.column_index == b->sem.column_index;
	case EXPR_LITERAL:
		// Compare literal values
		if (a->lit_type != b->lit_type)
			return false;
		// Simplified - would need proper value comparison
		return true;
	default:
		// Conservative - only handle simple cases
		return false;
	}
}

// Check if expression is in GROUP BY list
bool
is_in_group_by(Expr *expr, array<Expr *, parser_arena> &group_by)
{
	for (uint32_t i = 0; i < group_by.size; i++)
	{
		if (expr_equivalent(expr, group_by[i]))
		{
			return true;
		}
	}
	return false;
}

bool
semantic_resolve_select(SelectStmt *stmt, SemanticContext *ctx)
{
	// 1. Build table context starting with FROM
	TableContext table_ctx;

	if (!stmt->from_table)
	{
		ctx->add_error("SELECT requires FROM clause", nullptr);
		return false;
	}

	Structure *from_table = catalog.get(stmt->from_table->table_name);
	if (!from_table)
	{
		ctx->add_error("Table does not exist", stmt->from_table->table_name.c_str());
		return false;
	}
	stmt->from_table->sem.resolved = from_table;
	table_ctx.add_table(from_table, stmt->from_table->table_name, stmt->from_table->alias);

	// 2. Resolve JOINs
	for (uint32_t i = 0; i < stmt->joins.size; i++)
	{
		JoinClause *join = stmt->joins[i];

		Structure *joined_table = catalog.get(join->table->table_name);
		if (!joined_table)
		{
			ctx->add_error("Table does not exist", join->table->table_name.c_str());
			return false;
		}

		join->table->sem.resolved = joined_table;
		table_ctx.add_table(joined_table, join->table->table_name, join->table->alias);

		// Resolve ON condition with current table context
		if (join->condition)
		{
			if (!semantic_resolve_expr_context(join->condition, &table_ctx, ctx))
			{
				return false;
			}

			// ON condition should be boolean
			if (join->condition->sem.resolved_type != TYPE_U32 && join->condition->sem.resolved_type != TYPE_NULL)
			{
				ctx->add_error("JOIN condition must be boolean", nullptr);
				return false;
			}
		}
		else if (join->type != JOIN_CROSS)
		{
			ctx->add_error("JOIN requires ON condition", nullptr);
			return false;
		}

		join->sem.is_resolved = true;
	}

	// 3. Resolve WHERE clause
	if (stmt->where_clause)
	{
		if (!semantic_resolve_expr_context(stmt->where_clause, &table_ctx, ctx))
		{
			return false;
		}

		// No aggregates allowed in WHERE
		if (expr_has_aggregate(stmt->where_clause))
		{
			ctx->add_error("Aggregates not allowed in WHERE clause", nullptr);
			return false;
		}
	}

	// 4. Resolve GROUP BY expressions
	for (uint32_t i = 0; i < stmt->group_by.size; i++)
	{
		if (!semantic_resolve_expr_context(stmt->group_by[i], &table_ctx, ctx))
		{
			return false;
		}

		// No aggregates in GROUP BY
		if (expr_has_aggregate(stmt->group_by[i]))
		{
			ctx->add_error("Aggregates not allowed in GROUP BY", nullptr);
			return false;
		}
	}

	// 5. Resolve SELECT list
	stmt->sem.output_types.clear();
	stmt->sem.output_names.clear();
	stmt->sem.has_aggregates = false;

	for (uint32_t i = 0; i < stmt->select_list.size; i++)
	{
		Expr *expr = stmt->select_list[i];

		// Handle SELECT * specially
		if (expr->type == EXPR_STAR)
		{
			// Expand all columns from all tables
			for (uint32_t t = 0; t < table_ctx.tables.size; t++)
			{
				Structure *tbl = table_ctx.tables[t].table;
				for (uint32_t c = 0; c < tbl->columns.size; c++)
				{
					stmt->sem.output_types.push(tbl->columns[c].type);

					string<parser_arena> col_name;
					col_name.set(tbl->columns[c].name);
					stmt->sem.output_names.push(col_name);
				}
			}
			expr->sem.is_resolved = true;
		}
		else
		{
			// Resolve regular expression
			if (!semantic_resolve_expr_context(expr, &table_ctx, ctx))
			{
				return false;
			}

			// Track if we have aggregates
			if (expr_has_aggregate(expr))
			{
				stmt->sem.has_aggregates = true;
			}

			// Add to output schema
			stmt->sem.output_types.push(expr->sem.resolved_type);

			// Generate output name
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

	// 6. Validate GROUP BY requirements
	if (stmt->sem.has_aggregates || stmt->group_by.size > 0)
	{
		// Every non-aggregate expression in SELECT must be in GROUP BY
		for (uint32_t i = 0; i < stmt->select_list.size; i++)
		{
			Expr *expr = stmt->select_list[i];
			if (expr->type != EXPR_STAR && !expr_has_aggregate(expr))
			{
				if (!is_in_group_by(expr, stmt->group_by))
				{
					ctx->add_error("Non-aggregate expression must appear in GROUP BY", nullptr);
					return false;
				}
			}
		}
	}

	// 7. Resolve HAVING clause
	if (stmt->having_clause)
	{
		if (!semantic_resolve_expr_context(stmt->having_clause, &table_ctx, ctx))
		{
			return false;
		}

		// HAVING requires GROUP BY or aggregates
		if (stmt->group_by.size == 0 && !stmt->sem.has_aggregates)
		{
			ctx->add_error("HAVING requires GROUP BY or aggregate functions", nullptr);
			return false;
		}
	}

	// 8. Resolve ORDER BY
	for (uint32_t i = 0; i < stmt->order_by.size; i++)
	{
		OrderByClause *order = stmt->order_by[i];
		if (!semantic_resolve_expr_context(order->expr, &table_ctx, ctx))
		{
			return false;
		}
		order->sem.is_resolved = true;
	}

	stmt->sem.is_resolved = true;
	return true;
}

bool
semantic_resolve_update(UpdateStmt *stmt, SemanticContext *ctx)
{
	// 1. Validate table exists
	Structure *table = catalog.get(stmt->table_name);
	if (!table)
	{
		ctx->add_error("Table does not exist", stmt->table_name.c_str());
		return false;
	}
	stmt->sem.table = table;

	// 2. Resolve column indices and validate assignments
	stmt->sem.column_indices.clear();
	for (uint32_t i = 0; i < stmt->columns.size; i++)
	{
		// Find column in table
		int32_t col_idx = -1;
		for (uint32_t j = 0; j < table->columns.size; j++)
		{
			if (strcmp(table->columns[j].name, stmt->columns[i].c_str()) == 0)
			{
				col_idx = j;
				break;
			}
		}

		if (col_idx < 0)
		{
			ctx->add_error("Column not found", stmt->columns[i].c_str());
			return false;
		}

		stmt->sem.column_indices.push(col_idx);

		// Resolve value expression
		if (!semantic_resolve_expr(stmt->values[i], table, ctx))
		{
			return false;
		}

		// TODO: Type compatibility check
		// For now, allow any assignment
	}

	// 3. Resolve WHERE clause
	if (stmt->where_clause)
	{
		if (!semantic_resolve_expr(stmt->where_clause, table, ctx))
		{
			return false;
		}

		// WHERE should be boolean
		if (stmt->where_clause->sem.resolved_type != TYPE_U32 && stmt->where_clause->sem.resolved_type != TYPE_NULL)
		{
			ctx->add_error("WHERE clause must evaluate to boolean", nullptr);
			return false;
		}
	}

	stmt->sem.is_resolved = true;
	return true;
}

// Entry point remains the same
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
	case STMT_UPDATE:
		return semantic_resolve_update(stmt->update_stmt, ctx);

	default:
		ctx->add_error("Unsupported statement type", nullptr);
		return false;
	}
}
