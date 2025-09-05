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
	*(uint32_t*)result->data = catalog[table_name].storage.btree.root_page_index;
	return true;
}

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

	// type = "table"
	prog.load(TYPE_CHAR16, prog.alloc_string("table", 16), row_start);

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
	prog.load(TYPE_CHAR16, prog.alloc_string("table", 16), row_start);
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

	vm_execute(prog.instructions.data, prog.instructions.size);

	return prog.instructions;
}
array<VMInstruction, query_arena>
compile_program(Statement *stmt)
{

	switch (stmt->type)
	{
	case STMT_CREATE_TABLE:
		return compile_create_table_complete(stmt);
	}
}
