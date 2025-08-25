// programbuilder.cpp - Simplified version with full table scans only
#include "compile.hpp"
#include "arena.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include "parser.hpp"
#include "schema.hpp"
#include "vec.hpp"
#include "vm.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>

// Register allocator with named registers for debugging
// Register allocator for ProgramBuilder
struct RegisterAllocator
{
	int next_free = 0;

	// Allocate a single register
	int allocate() {
		if (next_free >= REGISTERS) {
			printf("Error: Out of registers\n");
			exit(1);
		}
		return next_free++;
	}

	// Allocate a contiguous range of registers
	int allocate_range(int count) {
		if (next_free + count > REGISTERS) {
			printf("Error: Cannot allocate %d registers (only %d available)\n",
			       count, REGISTERS - next_free);
			exit(1);
		}
		int first = next_free;
		next_free += count;
		return first;
	}

	// Reset allocator for new program
	void clear() {
		next_free = 0;
	}
};

struct ProgramBuilder
{
	Vec<VMInstruction, QueryArena> instructions;
	Vec<std::pair<Str<QueryArena>, int>, QueryArena> labels;
	RegisterAllocator regs;

	// Fluent interface
	ProgramBuilder &
	emit(VMInstruction inst)
	{
		instructions.push_back(inst);
		return *this;
	}

	ProgramBuilder &
	label(const char *name)
	{
		labels.push_back(std::make_pair(name, instructions.size()));
		return *this;
	}

	int
	here()
	{
		return instructions.size();
	}

	void
	resolve_labels()
	{
		for (size_t i = 0; i < instructions.size(); i++)
		{
			auto &inst = instructions[i];

			if (!inst.p4 && inst.p3 == -1)
			{
				continue;
			}
			char *label = (char *)inst.p4;

			auto it = labels.find_with(
				[label](const std::pair<Str<QueryArena>, uint32_t> entry) { return entry.first.starts_with(label); });

			if (it == -1)
			{
				continue;
			}
			inst.p3 = it;
			inst.p4 = nullptr;
		}
	}
};

//------------------ PROGRAMS ---------------------//

// Complete the build_select function in compile.cpp
void build_select(ProgramBuilder& prog, SelectNode* node) {
    // For educational clarity, we'll implement a simple full table scan
    // This demonstrates the core VM opcodes needed for SELECT

    int table_cursor = 0;

    // 1. Open the table cursor for reading
    prog.emit(Opcodes::Open::create_btree(table_cursor, node->table, 0, false));

    // 2. Get the table schema to know how many columns we have
    Table* table = get_table(node->table);
    int num_columns = table->columns.size();

    // 3. Allocate registers for the result row
    int result_start_reg = prog.regs.allocate_range(num_columns);

    // 4. Rewind cursor to the beginning of the table
    // Jump to end (Halt) if table is empty
    int halt_addr = prog.instructions.size() + 6 + num_columns; // Calculate where Halt will be
    prog.emit(Opcodes::Rewind::create(table_cursor, halt_addr, false));

    // 5. Loop start - this is where we'll jump back to for each row
    int loop_start = prog.here();

    // 6. Extract each column from the current row into registers
    for (int i = 0; i < num_columns; i++) {
        prog.emit(Opcodes::Column::create(table_cursor, i, result_start_reg + i));
    }

    // 7. Emit the result row (calls the callback with the row data)
    prog.emit(Opcodes::Result::create(result_start_reg, num_columns));

    // 8. Step to the next row, jump back to loop_start if there are more rows
    prog.emit(Opcodes::Step::create(table_cursor, halt_addr, true));
    prog.emit(Opcodes::Goto::create(loop_start));

    // 9. Clean up - close cursor and halt
    prog.emit(Opcodes::Close::create(table_cursor));
    prog.emit(Opcodes::Halt::create(0));
}

// Also need to fix a small issue in the Result opcode execution in vm.cpp
// The emit_row callback should pass the count parameter correctly:
// In vm.cpp, case OP_Result, change this line:
//     VM.ctx->emit_row(values, 0);
// To:
//     VM.ctx->emit_row(values, reg_count);
void
build_insert(ProgramBuilder &prog, InsertNode *node)
{
	int table_cursor = 0;
	const char *table = node->table;

	// Open cursor with write access
	prog.emit(Opcodes::Open::create_btree(table_cursor, table, 0, true));

	// First register is for the key (first column), rest for data
	int first_reg = prog.regs.allocate_range(node->values.size());

	// Load each value into its register
	for (size_t i = 0; i < node->values.size(); i++) {
		ASTNode* value = node->values[i];
		int target_reg = first_reg + i;
		if (value->type == AST_LITERAL) {
			LiteralNode* lit = (LiteralNode*)value;
			prog.emit(Opcodes::Move::create_load(target_reg, lit->value.type, lit->value.data));
		}
	}

	prog.emit(Opcodes::Insert::create(table_cursor, first_reg, node->values.size()));
	prog.emit(Opcodes::Close::create(table_cursor));
	prog.emit(Opcodes::Halt::create(0));
}




Vec<VMInstruction, QueryArena>
build_from_ast(ASTNode *ast)
{


	ProgramBuilder builder;

	switch (ast->type)
	{

	case AST_INSERT: {
		build_insert(builder, (InsertNode *)ast);
		break;
	}

	case AST_SELECT: {

	    build_select(builder, (SelectNode*)ast);
	}

	// these done internally
	case AST_CREATE_INDEX:
	case AST_CREATE_TABLE:
	case AST_DROP_TABLE:
	case AST_DROP_INDEX:
	default:
		break;
	}

	return builder.instructions;
}
