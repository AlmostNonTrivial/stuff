#include "compile.hpp"
#include "catalog.hpp"
#include "common.hpp"
#include "parser.hpp"

bool
vmfunc_create_structure(TypedValue *result, TypedValue *args, uint32_t arg_count)
{
	auto				table_name = args->as_char();
	string<query_arena> s;
	s.set(table_name);
	auto structure = catalog[s].to_layout();
	catalog[s].storage.btree = btree_create(structure.layout[0], structure.record_size, true);
	return true;
}

// array<VMInstruction, query_arena> compile(Statement*stmt) {}
array<VMInstruction, query_arena>
compile_create_table(Statement *stmt)
{
	ProgramBuilder prog;
	prog.begin_transaction();
	int reg = prog.load(TypedValue::make(TYPE_CHAR16, (void *)stmt->create_table_stmt->table_name.c_str()));
	prog.call_function(vmfunc_create_structure, reg, 1);

	prog.commit_transaction();
	prog.halt();

	return prog.instructions;
}

array<VMInstruction, query_arena>
compile_program(Statement *stmt)
{

	switch (stmt->type)
	{
	case STMT_CREATE_TABLE:
		return compile_create_table(stmt);
	}
}
