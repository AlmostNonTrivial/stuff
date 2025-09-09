// compile.cpp - Simplified SQL compilation
#include "compile.hpp"
#include "arena.hpp"
#include "catalog.hpp"
#include "common.hpp"
#include "parser.hpp"
#include "types.hpp"
#include "vm.hpp"
#include <cstring>
#include <string_view>

// ============================================================================
// Helper Functions
// ============================================================================

// Compile a literal expression
static int
compile_literal(ProgramBuilder *prog, Expr *expr)
{
	switch (expr->lit_type)
	{
	case TYPE_U32:
		return prog->load(prog->alloc_data_type(TYPE_U32, &expr->int_val));
	case TYPE_CHAR32:
		return prog->load(prog->alloc_data_type(TYPE_CHAR32, expr->str_val.data(), expr->str_val.size()));
	}

	assert(false);
}

// Compile any expression (for WHERE clauses)
static int
compile_expr(ProgramBuilder *prog, Expr *expr, int cursor_id)
{
	switch (expr->type)
	{
	case EXPR_COLUMN:
		return prog->get_column(cursor_id, expr->sem.column_index);

	case EXPR_LITERAL:
		return compile_literal(prog, expr);

	case EXPR_BINARY_OP: {
		int left_reg = compile_expr(prog, expr->left, cursor_id);
		int right_reg = compile_expr(prog, expr->right, cursor_id);

		switch (expr->op)
		{
		case OP_EQ:
			return prog->eq(left_reg, right_reg);
		case OP_NE:
			return prog->ne(left_reg, right_reg);
		case OP_LT:
			return prog->lt(left_reg, right_reg);
		case OP_LE:
			return prog->le(left_reg, right_reg);
		case OP_GT:
			return prog->gt(left_reg, right_reg);
		case OP_GE:
			return prog->ge(left_reg, right_reg);
		case OP_AND:
			return prog->logic_and(left_reg, right_reg);
		case OP_OR:
			return prog->logic_or(left_reg, right_reg);
		default:
			assert(false);
		}
	}

	case EXPR_UNARY_OP: {
		int operand_reg = compile_expr(prog, expr->operand, cursor_id);
		if (expr->unary_op == OP_NOT)
		{
			// Invert boolean: 1 - x
			int one = prog->load(TYPE_U32, prog->alloc(1U));
			return prog->sub(one, operand_reg);
		}
		return operand_reg;
	}

	default:
	assert(false);
	}
}

// Check if WHERE clause is a simple primary key lookup
static bool
is_pk_lookup(Expr *where_clause, Relation *table, comparison_op *out_op, Expr **out_literal)
{
	if (!where_clause || where_clause->type != EXPR_BINARY_OP)
	{
		return false;
	}

	// Check if it's a comparison operator
	comparison_op op;
	switch (where_clause->op)
	{
	case OP_EQ:
		op = EQ;
		break;
	case OP_LT:
		op = LT;
		break;
	case OP_LE:
		op = LE;
		break;
	case OP_GT:
		op = GT;
		break;
	case OP_GE:
		op = GE;
		break;
	default:
		return false;
	}

	// Check if left side is primary key column (column 0)
	if (where_clause->left->type != EXPR_COLUMN || where_clause->left->sem.column_index != 0)
	{
		return false;
	}

	// Check if right side is a literal
	if (where_clause->right->type != EXPR_LITERAL)
	{
		return false;
	}

	*out_op = op;
	*out_literal = where_clause->right;
	return true;
}

// Reconstruct CREATE TABLE SQL from AST
static string_view
reconstruct_create_sql(CreateTableStmt *stmt)
{
	auto stream = stream_writer<query_arena>::begin();

	const char *prefix = "CREATE TABLE ";
	stream.write(prefix, strlen(prefix));
	stream.write(stmt->table_name.data(), stmt->table_name.length());
	stream.write(" (", 2);

	for (uint32_t i = 0; i < stmt->columns.size(); i++)
	{
		if (i > 0)
		{
			stream.write(", ", 2);
		}

		ColumnDef &col = stmt->columns[i];
		stream.write(col.name.data(), col.name.length());
		stream.write(" ", 1);

		const char *type_name = (col.type == TYPE_U32) ? "INT" : "TEXT";
		stream.write(type_name, strlen(type_name));
	}

	stream.write(")", 1);
	stream.write("\0", 1);

	return stream.finish();
}

// ============================================================================
// VM Functions for catalog operations
// ============================================================================

static bool
vmfunc_create_structure(TypedValue *result, TypedValue *args, uint32_t arg_count)
{
	const char *table_name = args[0].as_char();

	Relation *structure = catalog.get(table_name);
	if (!structure)
	{
		result->type = TYPE_U32;
		result->data = arena<query_arena>::alloc(sizeof(uint32_t));
		*(uint32_t *)result->data = 0;
		return false;
	}

	TupleFormat layout = tuple_format_from_relation(*structure);
	structure->storage.btree = btree_create(layout.key_type, layout.record_size, true);

	result->type = TYPE_U32;
	result->data = arena<query_arena>::alloc(sizeof(uint32_t));
	*(uint32_t *)result->data = structure->storage.btree.root_page_index;
	return true;
}

static bool
vmfunc_drop_structure(TypedValue *result, TypedValue *args, uint32_t arg_count)
{
	if (arg_count != 1)
	{
		return false;
	}

	const char *name = args[0].as_char();
	Relation   *structure = catalog.get(name);

	if (!structure)
	{
		// Already gone
		result->type = TYPE_U32;
		result->data = arena<query_arena>::alloc(sizeof(uint32_t));
		*(uint32_t *)result->data = 1;
		return true;
	}

	// Clear the btree
	bt_clear(&structure->storage.btree);

	catalog.remove(name);

	result->type = TYPE_U32;
	result->data = arena<query_arena>::alloc(sizeof(uint32_t));
	*(uint32_t *)result->data = 1;
	return true;
}

void
vmfunc_catalog_bootstrap(TypedValue *result, size_t count)
{
	if (count != 5)
		return;

	const uint32_t key = result[0].as_u32();
	const char	  *name = result[1].as_char();
	const char	  *tbl_name = result[2].as_char();
	uint32_t	   rootpage = result[3].as_u32();
	const char	  *sql = result[4].as_char();

	if (strcmp(name, MASTER_CATALOG) == 0)
		return;

	auto master = catalog.get(MASTER_CATALOG);
	if (master->next_key <= key)
	{
		master->next_key = key + 1;
	}

	Statement *stmt = parse_sql(sql).statements[0];

	array<Attribute, query_arena> columns;

	if (strcmp(tbl_name, name) == 0)
	{
		// It's a table
		CreateTableStmt &create_stmt = stmt->create_table_stmt;
		columns.reserve(create_stmt.columns.size());

		for (uint32_t i = 0; i < create_stmt.columns.size(); i++)
		{
			ColumnDef &col_def = create_stmt.columns[i];
			Attribute  col;
			to_str(col_def.name, col.name, ATTRIBUTE_NAME_MAX_SIZE);
			columns.push(col);
		}
	}

	// Create the structure
	Relation	structure = create_relation(name, columns);
	TupleFormat format = tuple_format_from_relation(structure);
	// Set up the btree with existing root page
	structure.storage.btree = btree_create(format.key_type, format.record_size, false);
	structure.storage.btree.root_page_index = rootpage;

	catalog.insert(name, structure);
}

// ============================================================================
// SELECT Compilation with ORDER BY via Red-Black Tree
// ============================================================================

array<VMInstruction, query_arena>
compile_select(Statement *stmt)
{
	ProgramBuilder prog;
	SelectStmt	  *select_stmt = &stmt->select_stmt;

	Relation *table = select_stmt->sem.table;
	bool	  has_order_by = (select_stmt->order_by_column.size() > 0);

	if (has_order_by)
	{
		// ====================================================================
		// WITH ORDER BY: Materialize into red-black tree, then output sorted
		// ====================================================================

		DataType order_by_type = table->columns[select_stmt->sem.order_by_index].type;

		// Build the layout for red-black tree records
		array<DataType, query_arena> rb_types;
		rb_types.push(order_by_type); // Sort key first

		// Add all output columns
		int output_column_count;
		if (select_stmt->is_star)
		{
			output_column_count = table->columns.size();
			for (uint32_t i = 0; i < table->columns.size(); i++)
			{
				rb_types.push(table->columns[i].type);
			}
		}
		else
		{
			output_column_count = select_stmt->sem.column_indices.size();
			for (uint32_t i = 0; i < select_stmt->sem.column_indices.size(); i++)
			{
				rb_types.push(table->columns[select_stmt->sem.column_indices[i]].type);
			}
		}

		// Create ephemeral red-black tree
		TupleFormat rb_layout = tuple_format_from_types(rb_types);

		auto rb_ctx = red_black(rb_layout, true); // Allow duplicates
		int	 rb_cursor = prog.open_cursor(rb_ctx);

		// Phase 1: Scan source table and populate red-black tree
		auto table_ctx = from_structure(*table);
		int	 table_cursor = prog.open_cursor(table_ctx);

		int	 at_end = prog.first(table_cursor);
		auto scan_loop = prog.begin_while(at_end);
		{
			prog.regs.push_scope();

			// Check WHERE clause if present
			bool		should_insert = true;
			CondContext where_ctx;
			if (select_stmt->where_clause)
			{
				int where_result = compile_expr(&prog, select_stmt->where_clause, table_cursor);
				where_ctx = prog.begin_if(where_result);
				should_insert = true;
			}

			if (should_insert)
			{
				// Build record for red-black tree
				int rb_record_size = 1 + output_column_count;
				int rb_record = prog.regs.allocate_range(rb_record_size);

				// First register: sort key
				int sort_key = prog.get_column(table_cursor, select_stmt->sem.order_by_index);
				prog.move(sort_key, rb_record);

				// Remaining registers: selected columns
				if (select_stmt->is_star)
				{
					for (uint32_t i = 0; i < table->columns.size(); i++)
					{
						int col = prog.get_column(table_cursor, i);
						prog.move(col, rb_record + 1 + i);
					}
				}
				else
				{
					for (uint32_t i = 0; i < select_stmt->sem.column_indices.size(); i++)
					{
						int col = prog.get_column(table_cursor, select_stmt->sem.column_indices[i]);
						prog.move(col, rb_record + 1 + i);
					}
				}

				// Insert into red-black tree
				prog.insert_record(rb_cursor, rb_record, rb_record_size);
			}

			if (select_stmt->where_clause)
			{
				prog.end_if(where_ctx);
			}

			prog.next(table_cursor, at_end);
			prog.regs.pop_scope();
		}
		prog.end_while(scan_loop);

		prog.close_cursor(table_cursor);

		// Phase 2: Read from red-black tree in sorted order
		int rb_at_end;
		if (select_stmt->order_desc)
		{
			rb_at_end = prog.last(rb_cursor);
		}
		else
		{
			rb_at_end = prog.first(rb_cursor);
		}

		auto output_loop = prog.begin_while(rb_at_end);
		{
			prog.regs.push_scope();

			// Skip the sort key (column 0) and output only the data columns
			int output_start = prog.get_columns(rb_cursor, 1, output_column_count);
			prog.result(output_start, output_column_count);

			if (select_stmt->order_desc)
			{
				prog.prev(rb_cursor, rb_at_end);
			}
			else
			{
				prog.next(rb_cursor, rb_at_end);
			}

			prog.regs.pop_scope();
		}
		prog.end_while(output_loop);

		prog.close_cursor(rb_cursor);
	}
	else
	{
		// ====================================================================
		// NO ORDER BY: Direct streaming output
		// ====================================================================

		auto table_ctx = from_structure(*table);
		int	 cursor = prog.open_cursor(table_ctx);

		// Check for primary key optimization
		comparison_op seek_op;
		Expr		 *seek_literal;
		bool		  use_seek = is_pk_lookup(select_stmt->where_clause, table, &seek_op, &seek_literal);

		if (use_seek && seek_op == EQ)
		{
			// Primary key equality lookup
			int key_reg = compile_literal(&prog, seek_literal);
			int found = prog.seek(cursor, key_reg, EQ);

			auto if_found = prog.begin_if(found);
			{
				int result_start;
				int result_count;

				if (select_stmt->is_star)
				{
					result_start = prog.get_columns(cursor, 0, table->columns.size());
					result_count = table->columns.size();
				}
				else
				{
					result_count = select_stmt->sem.column_indices.size();
					result_start = prog.regs.allocate_range(result_count);
					for (uint32_t i = 0; i < result_count; i++)
					{
						int col_reg = prog.get_column(cursor, select_stmt->sem.column_indices[i]);
						prog.move(col_reg, result_start + i);
					}
				}

				prog.result(result_start, result_count);
			}
			prog.end_if(if_found);
		}
		else
		{
			// Table scan (with optional seek for ranges)
			int at_end;

			if (use_seek && (seek_op == GT || seek_op == GE))
			{
				int key_reg = compile_literal(&prog, seek_literal);
				at_end = prog.seek(cursor, key_reg, seek_op);
			}
			else
			{
				at_end = prog.first(cursor);
			}

			auto scan_loop = prog.begin_while(at_end);
			{
				prog.regs.push_scope();

				// Evaluate WHERE clause if present
				bool		should_output = true;
				CondContext where_ctx;
				if (select_stmt->where_clause)
				{
					int where_result = compile_expr(&prog, select_stmt->where_clause, cursor);
					where_ctx = prog.begin_if(where_result);
					should_output = true;
				}

				if (should_output)
				{
					int result_start;
					int result_count;

					if (select_stmt->is_star)
					{
						result_start = prog.get_columns(cursor, 0, table->columns.size());
						result_count = table->columns.size();
					}
					else
					{
						result_count = select_stmt->sem.column_indices.size();
						result_start = prog.regs.allocate_range(result_count);
						for (uint32_t i = 0; i < result_count; i++)
						{
							int col_reg = prog.get_column(cursor, select_stmt->sem.column_indices[i]);
							prog.move(col_reg, result_start + i);
						}
					}

					prog.result(result_start, result_count);
				}

				if (select_stmt->where_clause)
				{
					prog.end_if(where_ctx);
				}

				prog.next(cursor, at_end);
				prog.regs.pop_scope();
			}
			prog.end_while(scan_loop);
		}

		prog.close_cursor(cursor);
	}

	prog.halt();
	prog.resolve_labels();
	return prog.instructions;
}

// ============================================================================
// INSERT Compilation (single row only)
// ============================================================================

array<VMInstruction, query_arena>
compile_insert(Statement *stmt)
{
	ProgramBuilder prog;
	InsertStmt	  *insert_stmt = &stmt->insert_stmt;

	prog.begin_transaction();

	// Open cursor to target table
	auto table_ctx = from_structure(*insert_stmt->sem.table);
	int	 cursor = prog.open_cursor(table_ctx);

	// Build the row in correct column order
	int row_size = insert_stmt->sem.table->columns.size();
	int row_start = prog.regs.allocate_range(row_size);



	// Fill in specified columns
	for (uint32_t i = 0; i < insert_stmt->values.size(); i++)
	{
		Expr	*expr = insert_stmt->values[i];
		uint32_t col_idx = insert_stmt->sem.column_indices[i];

		int value_reg;
		if (expr->type == EXPR_LITERAL)
		{
			value_reg = compile_literal(&prog, expr);
		}

		prog.move(value_reg, row_start + col_idx);
	}

	// Insert the record
	prog.insert_record(cursor, row_start, row_size);

	prog.close_cursor(cursor);
	prog.commit_transaction();
	prog.halt();

	prog.resolve_labels();
	return prog.instructions;
}

// ============================================================================
// UPDATE Compilation
// ============================================================================

array<VMInstruction, query_arena>
compile_update(Statement *stmt)
{
	ProgramBuilder prog;
	UpdateStmt	  *update_stmt = &stmt->update_stmt;

	prog.begin_transaction();

	Relation *table = update_stmt->sem.table;
	auto	  table_ctx = from_structure(*table);
	int		  cursor = prog.open_cursor(table_ctx);

	// Full table scan with updates
	int at_end = prog.first(cursor);

	auto scan_loop = prog.begin_while(at_end);
	{
		prog.regs.push_scope();

		// Check WHERE clause if present
		bool		should_update = true;
		CondContext where_ctx;
		if (update_stmt->where_clause)
		{
			int where_result = compile_expr(&prog, update_stmt->where_clause, cursor);
			where_ctx = prog.begin_if(where_result);
			should_update = true;
		}

		if (should_update)
		{
			// Load current row
			int row_start = prog.get_columns(cursor, 0, table->columns.size());

			// Apply updates to specific columns
			for (uint32_t i = 0; i < update_stmt->columns.size(); i++)
			{
				uint32_t col_idx = update_stmt->sem.column_indices[i];
				Expr	*value_expr = update_stmt->values[i];

				int new_value;
				if (value_expr->type == EXPR_LITERAL)
				{
					new_value = compile_literal(&prog, value_expr);
				}

				prog.move(new_value, row_start + col_idx);
			}

			// Write back the updated row
			prog.update_record(cursor, row_start);
		}

		if (update_stmt->where_clause)
		{
			prog.end_if(where_ctx);
		}

		prog.next(cursor, at_end);
		prog.regs.pop_scope();
	}
	prog.end_while(scan_loop);

	prog.close_cursor(cursor);
	prog.commit_transaction();
	prog.halt();

	prog.resolve_labels();
	return prog.instructions;
}

// ============================================================================
// DELETE Compilation
// ============================================================================

array<VMInstruction, query_arena>
compile_delete(Statement *stmt)
{
	ProgramBuilder prog;
	DeleteStmt	  *delete_stmt = &stmt->delete_stmt;

	prog.begin_transaction();

	auto table_ctx = from_structure(*delete_stmt->sem.table);
	int	 cursor = prog.open_cursor(table_ctx);

	// Forward scan with careful cursor management
	int at_end = prog.first(cursor);

	auto scan_loop = prog.begin_while(at_end);
	{
		prog.regs.push_scope();

		// Determine if we should delete
		int should_delete;
		if (delete_stmt->where_clause)
		{
			should_delete = compile_expr(&prog, delete_stmt->where_clause, cursor);
		}
		else
		{
			// No WHERE - delete all
			should_delete = prog.load(TYPE_U32, prog.alloc(1U));
		}

		auto delete_if = prog.begin_if(should_delete);
		{
			int deleted = prog.regs.allocate();
			int still_valid = prog.regs.allocate();
			prog.delete_record(cursor, deleted, still_valid);

			// After delete, check if cursor is still valid
			auto if_valid = prog.begin_if(still_valid);
			{
				// Cursor auto-advanced to next
				prog.move(still_valid, at_end);
			}
			prog.begin_else(if_valid);
			{
				// Cursor invalid, try to reposition
				prog.first(cursor, at_end);
			}
			prog.end_if(if_valid);
		}
		prog.begin_else(delete_if);
		{
			// Not deleting, just move to next
			prog.next(cursor, at_end);
		}
		prog.end_if(delete_if);

		prog.regs.pop_scope();
	}
	prog.end_while(scan_loop);

	prog.close_cursor(cursor);
	prog.commit_transaction();
	prog.halt();

	prog.resolve_labels();
	return prog.instructions;
}

// ============================================================================
// CREATE TABLE Compilation
// ============================================================================

array<VMInstruction, query_arena>
compile_create_table(Statement *stmt)
{
	ProgramBuilder	 prog;
	CreateTableStmt *create_stmt = &stmt->create_table_stmt;

	prog.begin_transaction();

	// 1. Create the actual table structure
	// int table_name_reg = prog.load(TYPE_CHAR32, (void *)create_stmt->table_name.data());
	int table_name_reg =
		prog.load(prog.alloc_data_type(TYPE_CHAR32, create_stmt->table_name.data(), create_stmt->table_name.size()));
	int root_page_reg = prog.call_function(vmfunc_create_structure, table_name_reg, 1);

	// 2. Add entry to master catalog
	Relation &master = *catalog.get(MASTER_CATALOG);
	auto	  master_ctx = from_structure(master);
	int		  master_cursor = prog.open_cursor(master_ctx);

	// Prepare master catalog row
	int row_start = prog.regs.allocate_range(5);

	// id (auto-increment)
	prog.load(prog.alloc_data_type(TYPE_U32, &master.next_key), row_start);
	master.next_key++;
	// name
	// prog.load(TYPE_CHAR32, prog.alloc_string(create_stmt->table_name.c_str(), 32), row_start + 1);
	prog.load(prog.alloc_data_type(TYPE_CHAR32, create_stmt->table_name.data(), create_stmt->table_name.size()),
			  row_start + 1);

	// tbl_name (same as name for tables)
	// prog.load(TYPE_CHAR32, prog.alloc_string(create_stmt->table_name.c_str(), 32), row_start + 2);
	prog.load(prog.alloc_data_type(TYPE_CHAR32, create_stmt->table_name.data(), create_stmt->table_name.size()),
			  row_start + 2);

	// rootpage
	prog.move(root_page_reg, row_start + 3);

	// sql (reconstructed CREATE statement)
	string_view sql = reconstruct_create_sql(create_stmt);
	// prog.load(TYPE_CHAR256, prog.alloc_string(sql, 256), row_start + 4);
	prog.load(prog.alloc_data_type(TYPE_CHAR256, sql.data(), sql.size()), row_start + 4);

	// Insert into master catalog
	prog.insert_record(master_cursor, row_start, 5);

	prog.close_cursor(master_cursor);
	prog.commit_transaction();
	prog.halt();

	prog.resolve_labels();
	return prog.instructions;
}

// ============================================================================
// DROP TABLE Compilation
// ============================================================================

array<VMInstruction, query_arena>
compile_drop_table(Statement *stmt)
{
	ProgramBuilder prog;
	DropTableStmt *drop_stmt = &stmt->drop_table_stmt;

	prog.begin_transaction();

	// 1. Drop the table structure
	// int name_reg = prog.load(TYPE_CHAR32, prog.alloc_string(drop_stmt->table_name.c_str(), 32));
	int name_reg =
		prog.load(prog.alloc_data_type(TYPE_CHAR32, drop_stmt->table_name.data(), drop_stmt->table_name.size()));
	prog.call_function(vmfunc_drop_structure, name_reg, 1);

	// 2. Delete from master catalog
	Relation &master = *catalog.get(MASTER_CATALOG);
	auto	  master_ctx = from_structure(master);
	int		  cursor = prog.open_cursor(master_ctx);

	// Scan for the table entry
	int	 at_end = prog.first(cursor);
	auto scan_loop = prog.begin_while(at_end);
	{
		prog.regs.push_scope();

		int entry_name = prog.get_column(cursor, 1); // name column
		int matches = prog.eq(entry_name, name_reg);

		auto delete_if = prog.begin_if(matches);
		{
			int deleted = prog.regs.allocate();
			int still_valid = prog.regs.allocate();
			prog.delete_record(cursor, deleted, still_valid);
			prog.goto_label("done");
		}
		prog.end_if(delete_if);

		prog.next(cursor, at_end);
		prog.regs.pop_scope();
	}
	prog.end_while(scan_loop);

	prog.label("done");
	prog.close_cursor(cursor);
	prog.commit_transaction();
	prog.halt();

	prog.resolve_labels();
	return prog.instructions;
}

// ============================================================================
// Transaction Statements
// ============================================================================

array<VMInstruction, query_arena>
compile_begin(Statement *stmt)
{
	ProgramBuilder prog;
	prog.begin_transaction();
	prog.halt();
	return prog.instructions;
}

array<VMInstruction, query_arena>
compile_commit(Statement *stmt)
{
	ProgramBuilder prog;
	prog.commit_transaction();
	prog.halt();
	return prog.instructions;
}

array<VMInstruction, query_arena>
compile_rollback(Statement *stmt)
{
	ProgramBuilder prog;
	prog.rollback_transaction();
	prog.halt();
	return prog.instructions;
}

// ============================================================================
// Main Dispatch Function
// ============================================================================

array<VMInstruction, query_arena>
compile_program(Statement *stmt, bool inject_transaction)
{
	// For non-transactional statements, we inject transaction boundaries
	// For explicit transaction statements, we don't

	switch (stmt->type)
	{
	case STMT_SELECT:
		return compile_select(stmt);

	case STMT_INSERT:
		return compile_insert(stmt);

	case STMT_UPDATE:
		return compile_update(stmt);

	case STMT_DELETE:
		return compile_delete(stmt);

	case STMT_CREATE_TABLE:
		return compile_create_table(stmt);

	case STMT_DROP_TABLE:
		return compile_drop_table(stmt);

	case STMT_BEGIN:
		return compile_begin(stmt);

	case STMT_COMMIT:
		return compile_commit(stmt);

	case STMT_ROLLBACK:
		return compile_rollback(stmt);

	default: {
		ProgramBuilder prog;
		prog.halt(1); // Error
		return prog.instructions;
	}
	}
}
void
load_catalog_from_master()
{
	// Set the callback

	vm_set_result_callback(vmfunc_catalog_bootstrap);

	// Run your existing master table scan
	ProgramBuilder prog = {};
	auto		   cctx = from_structure(*catalog.get(MASTER_CATALOG));
	int			   cursor = prog.open_cursor(cctx);
	int			   is_at_end = prog.rewind(cursor, false);
	auto		   while_context = prog.begin_while(is_at_end);
	int			   dest_reg = prog.get_columns(cursor, 0, cctx->layout.columns.size());
	prog.result(dest_reg, cctx->layout.columns.size());
	prog.next(cursor, is_at_end);
	prog.end_while(while_context);
	prog.close_cursor(cursor);
	prog.halt();
	prog.resolve_labels();

	vm_execute(prog.instructions.front(), prog.instructions.size());
}
