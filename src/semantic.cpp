#include "semantic.hpp"
#include "arena.hpp"
#include "containers.hpp"
#include "catalog.hpp"
#include "common.hpp"
#include "parser.hpp"
#include "types.hpp"
#include <cstring>
#include <cstdio>

static struct
{
	string_view error;
	string_view context;
} current_result;

static struct
{
	hash_map<string_view, Relation, query_arena> tables_to_create;
	hash_set<string_view, query_arena>			 tables_to_drop;
} catalog_changes;

char *
format_error(const char *fmt, ...)
{
	char   *buffer = (char *)arena<query_arena>::alloc(256);
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, 256, fmt, args);
	va_end(args);
	return buffer;
}

static void
set_error(string_view error, string_view context = {})
{
	current_result.error = error;
	current_result.context = context;
}

static Relation *
lookup_table(string_view name)
{
	if (catalog_changes.tables_to_drop.contains(name))
	{
		return nullptr;
	}

	Relation *pending = catalog_changes.tables_to_create.get(name);
	if (pending)
	{
		return pending;
	}

	return catalog.get(name);
}

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

static bool
resolve_column_list(Relation *table, array<string_view, query_arena> &column_names,
					array<int32_t, query_arena> &out_indices)
{

	out_indices.clear();

	for (auto column_name : column_names)
	{
		int32_t idx = find_column_index(table, column_name);

		if (idx < 0)
		{
			set_error("Column does not exist in table", column_name);
			return false;
		}

		out_indices.push(idx);
	}
	return true;
}

static const char *
sql_type_name(DataType type)
{
	return (type == TYPE_U32) ? "INT" : "TEXT";
}

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

static bool
validate_literal_value(Expr *expr, DataType expected_type, const char *column_name, const char *operation)
{
	if (expr->type != EXPR_LITERAL)
	{
		set_error(format_error("Only literal values allowed in %s", operation), column_name);
		return false;
	}

	if (expr->type == EXPR_LITERAL)
	{
		if (expr->lit_type != expected_type)
		{
			set_error(format_error("Type mismatch for column '%s': expected %s, got %s", column_name,
								   sql_type_name(expected_type), sql_type_name(expr->lit_type)),
					  column_name);
			return false;
		}
	}

	expr->sem.resolved_type = expected_type;
	return true;
}

static void
apply_catalog_changes()
{
	for (auto [name, _] : catalog_changes.tables_to_drop)
	{
		catalog_delete_relation(name);
	}

	for (auto [name, relation] : catalog_changes.tables_to_create)
	{
		catalog_add_relation(relation);
	}
}

static void
clear_catalog_changes()
{
	catalog_changes.tables_to_create.clear();
	catalog_changes.tables_to_drop.clear();
}

static bool
semantic_resolve_expr(Expr *expr, Relation *table)
{
	if (!expr)
	{
		return true;
	}

	switch (expr->type)
	{
	case EXPR_LITERAL:
		expr->sem.resolved_type = expr->lit_type;
		return true;

	case EXPR_COLUMN: {
		int32_t idx = find_column_index(table, expr->column_name);
		if (idx < 0)
		{
			set_error("Column not found", expr->column_name);
			return false;
		}

		expr->sem.column_index = idx;
		expr->sem.resolved_type = table->columns[idx].type;
		expr->sem.table = table;
		return true;
	}

	case EXPR_BINARY_OP: {
		if (!semantic_resolve_expr(expr->left, table))
		{
			return false;
		}
		if (!semantic_resolve_expr(expr->right, table))
		{
			return false;
		}

		DataType left_type = expr->left->sem.resolved_type;
		DataType right_type = expr->right->sem.resolved_type;

		if (left_type != right_type)
		{
			set_error("Types need to match");
			return false;
		}

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
		}

		return true;
	}
	case EXPR_UNARY_OP: {
		if (!semantic_resolve_expr(expr->operand, table))
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

		return true;
	}

	default:
		set_error("Unknown expression type");
		return false;
	}
}

static bool
resolve_where_clause(Expr *where_clause, Relation *table)
{
	if (!where_clause)
	{
		return true;
	}

	if (!semantic_resolve_expr(where_clause, table))
	{
		if (current_result.error.empty())
		{
			set_error("Invalid expression in WHERE clause");
		}
		return false;
	}

	if (where_clause->sem.resolved_type != TYPE_U32)
	{
		set_error("WHERE clause must evaluate to boolean");
		return false;
	}

	return true;
}

static bool
resolve_insert_columns(InsertStmt *stmt, Relation *table)
{
	if (stmt->columns.size() > 0)
	{
		return resolve_column_list(table, stmt->columns, stmt->sem.column_indices);
	}

	stmt->sem.column_indices.clear();
	for (uint32_t i = 0; i < table->columns.size(); i++)
	{
		stmt->sem.column_indices.push(i);
	}
	return true;
}

static bool
semantic_resolve_select(SelectStmt *stmt)
{
	Relation *table = require_table(stmt->table_name, &stmt->sem.table);
	if (!table)
	{
		return false;
	}

	if (stmt->is_star)
	{
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
		if (!resolve_column_list(table, stmt->columns, stmt->sem.column_indices))
		{
			return false;
		}

		stmt->sem.column_types.clear();
		for (uint32_t i = 0; i < stmt->sem.column_indices.size(); i++)
		{
			stmt->sem.column_types.push(table->columns[stmt->sem.column_indices[i]].type);
		}
	}

	if (!resolve_where_clause(stmt->where_clause, table))
	{
		return false;
	}

	if (stmt->order_by_column.size() > 0)
	{
		stmt->sem.order_by_index = find_column_index(table, stmt->order_by_column);
		if (stmt->sem.order_by_index < 0)
		{
			set_error("ORDER BY column does not exist in table", stmt->order_by_column);
			return false;
		}
	}

	return true;
}

static bool
semantic_resolve_insert(InsertStmt *stmt)
{
	Relation *table = require_table(stmt->table_name, &stmt->sem.table);
	if (!table)
	{
		return false;
	}

	if (!resolve_insert_columns(stmt, table))
	{
		return false;
	}

	if (stmt->values.size() != stmt->sem.column_indices.size())
	{
		set_error(format_error("Value count mismatch: expected %u, got %u", stmt->sem.column_indices.size(),
							   stmt->values.size()),
				  stmt->table_name);
		return false;
	}

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

	return true;
}

static bool
semantic_resolve_update(UpdateStmt *stmt)
{
	Relation *table = require_table(stmt->table_name, &stmt->sem.table);
	if (!table)
	{
		return false;
	}

	if (!resolve_column_list(table, stmt->columns, stmt->sem.column_indices))
	{
		return false;
	}

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

	if (!resolve_where_clause(stmt->where_clause, table))
	{
		return false;
	}

	return true;
}

static bool
semantic_resolve_delete(DeleteStmt *stmt)
{
	Relation *table = require_table(stmt->table_name, &stmt->sem.table);
	if (!table)
	{
		return false;
	}

	if (!resolve_where_clause(stmt->where_clause, table))
	{
		return false;
	}

	return true;
}

static bool
semantic_resolve_create_table(CreateTableStmt *stmt)
{
    
    if(stmt->table_name.size() > RELATION_NAME_MAX_SIZE) {
    set_error(ctx, format_error(ctx, "%u, got %u", stmt->sem.column_indices.size(),
							   stmt->values.size()),
        return false;
    }
    
	Relation *existing = lookup_table(ctx, stmt->table_name);
	if (existing)
	{
		set_error("Table already exists", stmt->table_name);
		return false;
	}

	if (stmt->columns.size() == 0)
	{
		set_error("Table must have at least one column", stmt->table_name);
		return false;
	}

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

	array<Attribute, query_arena> cols;
	for (ColumnDef &def : stmt->columns)
	{
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

	catalog_changes.tables_to_create.insert(new_relation.name, new_relation);

	stmt->sem.created_structure = stmt->table_name;
	return true;
}

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

	catalog_changes.tables_to_drop.insert(stmt->table_name, 1);

	return true;
}

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
		return true;

	default:
		set_error("Unknown statement type");
		return false;
	}
}

SemanticResult
semantic_analyze(array<Statement *, query_arena> &statements)
{
	SemanticResult result;
	result.success = true;
	result.error = {};
	result.error_context = {};
	result.failed_statement_index = -1;

	current_result.error = {};
	current_result.context = {};
	clear_catalog_changes();

	for (uint32_t i = 0; i < statements.size(); i++)
	{
		Statement *stmt = statements[i];

		current_result.error = {};
		current_result.context = {};

		if (!semantic_resolve_statement(stmt))
		{
			result.success = false;
			result.error = current_result.error;
			result.error_context = current_result.context;
			result.failed_statement_index = i;

			clear_catalog_changes();

			return result;
		}
	}

	apply_catalog_changes();

	return result;
}
