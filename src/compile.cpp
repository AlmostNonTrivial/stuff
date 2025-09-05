#include "compile.hpp"
#include "catalog.hpp"
#include "common.hpp"
#include "parser.hpp"
#include "vm.hpp"

bool
vmfunc_create_structure(TypedValue *result, TypedValue *args, uint32_t arg_count)
{
	auto table_name = args->as_char();

	auto structure = catalog[table_name].to_layout();
	catalog[table_name].storage.btree = btree_create(structure.layout[0], structure.record_size, true);
	result->type = TYPE_U32;
	result->data = arena::alloc<query_arena>(sizeof(uint32_t));
	*(uint32_t *)result->data = catalog[table_name].storage.btree.root_page_index;
	return true;
}

int i = 1;

array<VMInstruction, query_arena>
compile_create_table(Statement *stmt)
{
	ProgramBuilder	 prog;
	CreateTableStmt *create_stmt = stmt->create_table_stmt;

	prog.begin_transaction();

	// 1. Create the actual table structure
	int table_name_reg = prog.load(TYPE_CHAR16, prog.alloc_string(create_stmt->table_name.c_str(), 16));
	int structure_reg = prog.call_function(vmfunc_create_structure, table_name_reg, 1);

	// 2. Open cursor to master catalog (always at root page 1)
	auto master_ctx = from_structure(catalog[MASTER_CATALOG]);
	int	 master_cursor = prog.open_cursor(&master_ctx);

	// 3. Prepare master catalog row data in contiguous registers
	int row_start = prog.regs.allocate_range(5);


	prog.load(alloc_u32(i++), row_start);

	// name = table name
	prog.load(TYPE_CHAR32, prog.alloc_string(create_stmt->table_name.c_str(), 32), row_start + 1);

	// tbl_name = table name (same as name for tables)
	prog.load(TYPE_CHAR32, prog.alloc_string(create_stmt->table_name.c_str(), 32), row_start + 2);

	// rootpage = root page number from created structure
	// TODO: Need a way to get the root page number from vmfunc_create_structure
	// For now, assume it returns the root page number
	prog.move(structure_reg, row_start + 3);

	// sql = original CREATE statement (reconstruct or store original)
	// For simplicity, create a minimal CREATE statement
	char *sql_text = (char *)arena::alloc<query_arena>(256);
	snprintf(sql_text, 256, "CREATE TABLE %s (...)", create_stmt->table_name.c_str());
	prog.load(TYPE_CHAR256, prog.alloc_string(sql_text, 256), row_start + 4);

	// 4. Insert the row into master catalog
	prog.insert_record(master_cursor, row_start, 5);

	// 5. Close master catalog cursor
	prog.close_cursor(master_cursor);

	prog.commit_transaction();
	prog.halt();

	return prog.instructions;
}

// Helper function to reconstruct CREATE TABLE SQL from AST
const char *
reconstruct_create_sql(CreateTableStmt *stmt)
{
	auto stream = arena::stream_begin<query_arena>(512);

	// Start with CREATE TABLE
	const char *prefix = "CREATE TABLE ";
	if (stmt->if_not_exists)
	{
		prefix = "CREATE TABLE IF NOT EXISTS ";
	}
	arena::stream_write(&stream, prefix, strlen(prefix));

	// Table name
	arena::stream_write(&stream, stmt->table_name.c_str(), stmt->table_name.length());
	arena::stream_write(&stream, " (", 2);

	// Columns
	for (uint32_t i = 0; i < stmt->columns.size; i++)
	{
		if (i > 0)
		{
			arena::stream_write(&stream, ", ", 2);
		}

		ColumnDef *col = stmt->columns[i];
		arena::stream_write(&stream, col->name, strlen(col->name));
		arena::stream_write(&stream, " ", 1);

		const char *type_nam = type_name(col->type);
		arena::stream_write(&stream, type_nam, strlen(type_nam));

		if (col->is_primary_key)
		{
			arena::stream_write(&stream, " PRIMARY KEY", 12);
		}
		if (col->is_not_null)
		{
			arena::stream_write(&stream, " NOT NULL", 9);
		}
	}

	arena::stream_write(&stream, ")", 1);
	arena::stream_write(&stream, "\0", 1); // Null terminate

	return (const char *)arena::stream_finish(&stream);
}

// Updated version using SQL reconstruction
array<VMInstruction, query_arena>
compile_create_table_complete(Statement *stmt)
{
	ProgramBuilder	 prog;
	CreateTableStmt *create_stmt = stmt->create_table_stmt;

	prog.begin_transaction();

	// 1. Create the actual table structure
	int table_name_reg = prog.load(TYPE_CHAR16, prog.alloc_string(create_stmt->table_name.c_str(), 16));
	int root_page_reg = prog.call_function(vmfunc_create_structure, table_name_reg, 1);

	// 2. Open cursor to master catalog
	auto master_ctx = from_structure(catalog[MASTER_CATALOG]);
	int	 master_cursor = prog.open_cursor(&master_ctx);

	// 3. Prepare master catalog row in contiguous registers
	int row_start = prog.regs.allocate_range(5);

	// Load all the master catalog fields
	prog.load(alloc_u32(i++), row_start);
	prog.load(TYPE_CHAR32, prog.alloc_string(create_stmt->table_name.c_str(), 32), row_start + 1);
	prog.load(TYPE_CHAR32, prog.alloc_string(create_stmt->table_name.c_str(), 32), row_start + 2);
	prog.move(root_page_reg, row_start + 3); // Root page from structure creation

	// Reconstruct the original SQL
	const char *sql = reconstruct_create_sql(create_stmt);
	prog.load(TYPE_CHAR256, prog.alloc_string(sql, 256), row_start + 4);

	// 4. Insert into master catalog
	prog.insert_record(master_cursor, row_start, 5);

	prog.close_cursor(master_cursor);
	prog.commit_transaction();
	prog.halt();

	return prog.instructions;
}

// Compile literal with optional type coercion
int
compile_literal(ProgramBuilder *prog, Expr *expr, DataType target_type)
{
	// If types match exactly, no coercion needed
	if (expr->lit_type == target_type)
	{
		switch (expr->lit_type)
		{
		case TYPE_U32:
			return prog->load(TYPE_U32, prog->alloc_value((uint32_t)expr->int_val));

		case TYPE_CHAR32:
			return prog->load(TYPE_CHAR32, prog->alloc_string(expr->str_val.c_str(), 32));

		// Add other literal types as needed
		default:
			return prog->load_null();
		}
	}

	// Handle type coercion
	if (type_is_numeric(expr->lit_type) && type_is_numeric(target_type))
	{
		// Numeric coercion
		if (type_is_unsigned(target_type))
		{
			uint64_t val = (uint64_t)expr->int_val;
			switch (target_type)
			{
			case TYPE_U8:
				return prog->load(TYPE_U8, prog->alloc_value((uint8_t)val));
			case TYPE_U16:
				return prog->load(TYPE_U16, prog->alloc_value((uint16_t)val));
			case TYPE_U32:
				return prog->load(TYPE_U32, prog->alloc_value((uint32_t)val));
			case TYPE_U64:
				return prog->load(TYPE_U64, prog->alloc_value((uint64_t)val));
			}
		}
		// Add signed and float coercions...
	}

	if (type_is_string(expr->lit_type) && type_is_string(target_type))
	{
		// String coercion - adjust size
		uint32_t target_size = type_size(target_type);
		return prog->load(target_type, prog->alloc_string(expr->str_val.c_str(), target_size));
	}

	return prog->load_null(); // Fallback for unsupported coercion
}

// Compile expression with target type for coercion
int
compile_expression(ProgramBuilder *prog, Expr *expr, DataType target_type)
{
	switch (expr->type)
	{
	case EXPR_LITERAL:
		return compile_literal(prog, expr, target_type);

	case EXPR_NULL:
		return prog->load_null();

	// TODO: Handle other expression types
	default:
		return prog->load_null(); // Fallback
	}
}

// Helper to compile a row of values into contiguous registers
int
compile_value_row(ProgramBuilder *prog, array<Expr *, parser_arena> *value_row, InsertStmt *stmt)
{
	int start_reg = prog->regs.allocate_range(value_row->size);

	for (uint32_t i = 0; i < value_row->size; i++)
	{
		Expr	*expr = value_row->data[i];
		uint32_t table_col_idx = stmt->sem.column_indices[i];
		DataType target_type = stmt->sem.table->columns[table_col_idx].type;

		int value_reg = compile_expression(prog, expr, target_type);
		prog->move(value_reg, start_reg + i);
	}

	return start_reg;
}

// Compile single INSERT statement
array<VMInstruction, query_arena>
compile_insert(Statement *stmt)
{
	ProgramBuilder prog;
	InsertStmt	  *insert_stmt = stmt->insert_stmt;

	// Must be semantically resolved first
	if (!insert_stmt->sem.is_resolved)
	{
		prog.halt(1); // Error
		return prog.instructions;
	}

	prog.begin_transaction();

	// Open cursor to target table
	auto table_ctx = from_structure(*insert_stmt->sem.table);
	int	 cursor = prog.open_cursor(&table_ctx);

	// Process each value row
	for (uint32_t row = 0; row < insert_stmt->values.size; row++)
	{
		prog.regs.push_scope();

		auto *value_row = insert_stmt->values[row];
		int	  start_reg = compile_value_row(&prog, value_row, insert_stmt);

		// Insert the record
		uint32_t value_count = value_row->size;
		prog.insert_record(cursor, start_reg, value_count);

		prog.regs.pop_scope();
	}

	prog.close_cursor(cursor);
	prog.commit_transaction();
	prog.halt();

	prog.resolve_labels();
	return prog.instructions;
}

// Optimized batch insert for large value sets
array<VMInstruction, query_arena> compile_batch_insert(Statement* stmt) {
    ProgramBuilder prog;
    InsertStmt* insert_stmt = stmt->insert_stmt;

    // Use same logic but with larger register allocation strategy
    // and potentially fewer transaction boundaries for very large batches

    prog.begin_transaction();

    auto table_ctx = from_structure(*insert_stmt->sem.table);
    int cursor = prog.open_cursor(&table_ctx);

    // For large batches, process in chunks to avoid register exhaustion
    const uint32_t BATCH_SIZE = 100;

    for (uint32_t start = 0; start < insert_stmt->values.size; start += BATCH_SIZE) {
        uint32_t end = (start + BATCH_SIZE < insert_stmt->values.size) ?
                       start + BATCH_SIZE : insert_stmt->values.size;

        for (uint32_t row = start; row < end; row++) {
            prog.regs.push_scope();

            auto* value_row = insert_stmt->values[row];
            int start_reg = compile_value_row(&prog, value_row, insert_stmt);
            prog.insert_record(cursor, start_reg, value_row->size);

            prog.regs.pop_scope();
        }
    }

    prog.close_cursor(cursor);
    prog.commit_transaction();
    prog.halt();

    prog.resolve_labels();
    return prog.instructions;
}

array<VMInstruction, query_arena>
compile_program(Statement *stmt)
{

	switch (stmt->type)
	{
	case STMT_CREATE_TABLE:
		return compile_create_table_complete(stmt);
	case STMT_INSERT:
		return compile_insert(stmt);
	}
}
