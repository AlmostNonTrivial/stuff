// programbuilder.cpp - Simplified version with full table scans only
#include "compile.hpp"
#include "arena.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include "parser.hpp"
#include "catalog.hpp"
#include "vm.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <utility>



// Register allocator with named registers for debugging
// Register allocator for ProgramBuilder
struct RegisterAllocator
{
	int next_free = 0;

	// Allocate a single register
	int
	allocate()
	{
		if (next_free >= REGISTERS)
		{
			printf("Error: Out of registers\n");
			exit(1);
		}
		return next_free++;
	}

	// Allocate a contiguous range of registers
	int
	allocate_range(int count)
	{
		if (next_free + count > REGISTERS)
		{
			printf("Error: Cannot allocate %d registers (only %d available)\n", count, REGISTERS - next_free);
			exit(1);
		}
		int first = next_free;
		next_free += count;
		return first;
	}

	// Reset allocator for new program
	void
	clear()
	{
		next_free = 0;
	}
};

struct ProgramBuilder
{
	array<VMInstruction, QueryArena> instructions;


	string_map<uint32_t> labels;
	RegisterAllocator regs;

	// Fluent interface
	ProgramBuilder &
	emit(VMInstruction inst)
	{
		// instructions.push_back(inst);
		array_push(&instructions, inst);
		return *this;
	}

	ProgramBuilder &
	label(const char *name)
	{
		stringmap_insert(&labels, name, instructions.size);
		return *this;
	}

	int
	here()
	{
		return instructions.size;
	}

	void
	resolve_labels()
	{
		for (size_t i = 0; i < instructions.size; i++)
		{
			auto &inst = instructions.data[i];

			if (!inst.p4)
			{
				continue;
			}
			char *label = (char *)inst.p4;


			auto entry = stringmap_get(&labels,label);
			if (entry == nullptr)
			{
				continue;
			}
			if(inst.p2 == -1) {
				inst.p2 = *entry;


			}
			if(inst.p3 == -1) {
				inst.p3 = *entry ;
			}
			inst.p4 = nullptr;
		}
	}
};

//------------------ PROGRAMS ---------------------//

// // Complete the build_select function in compile.cpp
// void
// build_select(ProgramBuilder &prog, SelectNode *node)
// {
// 	// Get table info
// 	Table *table = get_table(node->table);
// 	if (!table) {
// 		printf("Error: Table '%s' not found\n", node->table);
// 		return;
// 	}

// 	int table_cursor = 0;

// 	// 1. Open the table cursor for reading
// 	prog.emit(Opcodes::Open::create_btree(table_cursor, node->table, 0, false));

// 	// 2. Get the table schema to know how many columns we have
// 	int num_columns = table->columns.size();

// 	// 3. Allocate registers for the result row
// 	int result_start_reg = prog.regs.allocate_range(num_columns);

// 	// 4. Rewind cursor to the beginning of the table
// 	prog.emit(Opcodes::Rewind::create(table_cursor, "select_done", false));

// 	// 5. Loop start - this is where we'll jump back to for each row
// 	prog.label("select_loop");

// 	// 6. Evaluate WHERE clause if present
// 	if (node->where && node->where->condition) {
// 		// Handle single comparison: column op value
// 		BinaryOpNode *cond = (BinaryOpNode*)node->where->condition;

// 		if (cond->type == AST_BINARY_OP && !cond->is_and) {
// 			// Load the column value from cursor
// 			ColumnRefNode *col_ref = (ColumnRefNode*)cond->left;
// 			uint32_t col_idx = get_column_index(node->table, col_ref->name);

// 			int test_reg = prog.regs.allocate();
// 			int value_reg = prog.regs.allocate();
// 			int result_reg = prog.regs.allocate();

// 			// Load column value
// 			prog.emit(Opcodes::Column::create(table_cursor, col_idx, test_reg));

// 			// Load comparison value
// 			if (cond->right->type == AST_LITERAL) {
// 				LiteralNode *lit = (LiteralNode*)cond->right;
// 				prog.emit(Opcodes::Move::create_load(value_reg, lit->value.type, lit->value.data));
// 			}

// 			// Perform comparison
// 			prog.emit(Opcodes::Test::create(result_reg, test_reg, value_reg, cond->op));

// 			// Jump to "skip_row" if condition is false
// 			prog.emit(Opcodes::JumpIf::create(result_reg, "skip_row", false));
// 		}
// 	}

// 	// 7. Extract each column from the current row into registers
// 	for (int i = 0; i < num_columns; i++)
// 	{
// 		prog.emit(Opcodes::Column::create(table_cursor, i, result_start_reg + i));
// 	}

// 	// 8. Emit the result row (calls the callback with the row data)
// 	prog.emit(Opcodes::Result::create(result_start_reg, num_columns));

// 	// 9. Label for skipping row (WHERE condition false)
// 	prog.label("skip_row");

// 	// 10. Step to the next row, jump to "select_done" if no more rows
// 	prog.emit(Opcodes::Step::create(table_cursor, "select_done", true));

// 	// 11. Loop back
// 	prog.emit(Opcodes::Goto::create("select_loop"));

// 	// 12. Done - close cursor and halt
// 	prog.label("select_done");
// 	prog.emit(Opcodes::Close::create(table_cursor));
// 	prog.emit(Opcodes::Halt::create(0));

// 	prog.resolve_labels();
// }

// // Also need to fix a small issue in the Result opcode execution in vm.cpp
// // The emit_row callback should pass the count parameter correctly:
// // In vm.cpp, case OP_Result, change this line:
// //     VM.ctx->emit_row(values, 0);
// // To:
// //     VM.ctx->emit_row(values, reg_count);
// void
// build_insert(ProgramBuilder &prog, InsertNode *node)
// {
// 	int table_cursor = 0;
// 	const char *table = node->table;

// 	// Open cursor with write access
// 	prog.emit(Opcodes::Open::create_btree(table_cursor, table, 0, true));

// 	// First register is for the key (first column), rest for data
// 	int first_reg = prog.regs.allocate_range(node->values.size());

// 	// Load each value into its register
// 	for (size_t i = 0; i < node->values.size(); i++)
// 	{
// 		ASTNode *value = node->values[i];
// 		int target_reg = first_reg + i;
// 		if (value->type == AST_LITERAL)
// 		{
// 			LiteralNode *lit = (LiteralNode *)value;
// 			prog.emit(Opcodes::Move::create_load(target_reg, lit->value.type, lit->value.data));
// 		}
// 	}

// 	prog.emit(Opcodes::Insert::create(table_cursor, first_reg, node->values.size()));
// 	prog.emit(Opcodes::Close::create(table_cursor));
// 	prog.emit(Opcodes::Halt::create(0));

// 	prog.resolve_labels();
// }
// // Add this function to compile.cpp after build_insert

// void
// build_update(ProgramBuilder &prog, UpdateNode *node)
// {
// 	// Get table info
// 	Table *table = get_table(node->table);
// 	if (!table) {
// 		printf("Error: Table '%s' not found\n", node->table);
// 		return;
// 	}

// 	int table_cursor = 0;

// 	// 1. Open cursor for writing
// 	prog.emit(Opcodes::Open::create_btree(table_cursor, node->table, 0, true));

// 	// 2. Rewind to beginning, jump to "done" if empty
// 	prog.emit(Opcodes::Rewind::create(table_cursor, "update_done", false));


// 	// 3. Main loop start
// 	prog.label("update_loop");

// 	// 4. Evaluate WHERE clause if present
// 	if (node->where && node->where->condition) {
// 		// For simplicity, handle single comparison: column = value
// 		BinaryOpNode *cond = (BinaryOpNode*)node->where->condition;

// 		if (cond->type == AST_BINARY_OP && !cond->is_and) {
// 			// Load the column value from cursor
// 			ColumnRefNode *col_ref = (ColumnRefNode*)cond->left;
// 			uint32_t col_idx = get_column_index(node->table, col_ref->name);

// 			int test_reg = prog.regs.allocate();
// 			int value_reg = prog.regs.allocate();
// 			int result_reg = prog.regs.allocate();

// 			// Load column value
// 			prog.emit(Opcodes::Column::create(table_cursor, col_idx, test_reg));

// 			// Load comparison value
// 			if (cond->right->type == AST_LITERAL) {
// 				LiteralNode *lit = (LiteralNode*)cond->right;
// 				prog.emit(Opcodes::Move::create_load(value_reg, lit->value.type, lit->value.data));
// 			}

// 			// Perform comparison
// 			prog.emit(Opcodes::Test::create(result_reg, test_reg, value_reg, cond->op));

// 			// Jump to "skip_update" if condition is false
// 			VMInstruction jump_inst = Opcodes::JumpIf::create(result_reg, "skip_update", false);
// 			prog.emit(jump_inst);
// 		}
// 	}

// 	// 5. Build the updated record
// 	// First, load all existing column values
// 	int num_columns = table->columns.size();
// 	int record_start_reg = prog.regs.allocate_range(num_columns - 1); // -1 because key is separate

// 	// Load existing values (skip key at index 0)
// 	for (int i = 1; i < num_columns; i++) {
// 		prog.emit(Opcodes::Column::create(table_cursor, i, record_start_reg + (i - 1)));
// 	}

// 	// Apply SET clauses - overwrite specific columns
// 	for (size_t i = 0; i < node->set_clauses.size(); i++) {
// 		SetClauseNode *set_clause = node->set_clauses[i];
// 		uint32_t col_idx = get_column_index(node->table, set_clause->column);

// 		if (col_idx > 0 && col_idx < num_columns) {
// 			// Load new value into the appropriate register
// 			int target_reg = record_start_reg + (col_idx - 1);

// 			if (set_clause->value->type == AST_LITERAL) {
// 				LiteralNode *lit = (LiteralNode*)set_clause->value;
// 				prog.emit(Opcodes::Move::create_load(target_reg, lit->value.type, lit->value.data));
// 			}
// 		}
// 	}

// 	// 6. Update the current row
// 	prog.emit(Opcodes::Update::create(table_cursor, record_start_reg));

// 	// 7. Label for skipping update (WHERE condition false)
// 	prog.label("skip_update");

// 	// 8. Step to next row, jump to "done" if no more rows
// 	prog.emit(Opcodes::Step::create(table_cursor, "end", true));
// 	// Will resolve "done" label

// 	// 9. Loop back
// 	VMInstruction goto_inst = Opcodes::Goto::create("update_loop");
// 	prog.emit(goto_inst);

// 	// 10. Done - close and halt
// 	prog.label("update_done");
// 	prog.emit(Opcodes::Close::create(table_cursor));

// 	prog.label("end");
// 	prog.emit(Opcodes::Halt::create(0));

// 	// Resolve all labels
// 	prog.resolve_labels();
// }


// void build_delete(ProgramBuilder &prog, DeleteStmt *node)
// {
//     // Get table info to validate it exists
//     Table *table = get_table(node->table);
//     if (!table) {
//         printf("Error: Table '%s' not found\n", node->table);
//         return;
//     }

//     int table_cursor = 0;

//     // 1. Open cursor for writing
//     prog.emit(Opcodes::Open::create_btree(table_cursor, node->table, 0, true));

//     // 2. Rewind to beginning, jump to "delete_done" if empty
//     prog.emit(Opcodes::Rewind::create(table_cursor, "delete_done", false));

//     // 3. Allocate registers for tracking state
//     int cursor_valid_reg = prog.regs.allocate();
//     int delete_occurred_reg = prog.regs.allocate();

//     // 4. Main loop start
//     prog.label("delete_loop");

//     // 5. Evaluate WHERE clause if present
//     if (node->where && node->where->condition) {
//         // Handle WHERE clause evaluation
//         BinaryOpNode *cond = (BinaryOpNode*)node->where->condition;

//         if (cond->type == AST_BINARY_OP && !cond->is_and) {
//             // Load the column value from cursor
//             ColumnRefNode *col_ref = (ColumnRefNode*)cond->left;
//             uint32_t col_idx = get_column_index(node->table, col_ref->name);

//             int test_reg = prog.regs.allocate();
//             int value_reg = prog.regs.allocate();
//             int result_reg = prog.regs.allocate();

//             // Load column value
//             prog.emit(Opcodes::Column::create(table_cursor, col_idx, test_reg));

//             // Load comparison value
//             if (cond->right->type == AST_LITERAL) {
//                 LiteralNode *lit = (LiteralNode*)cond->right;
//                 prog.emit(Opcodes::Move::create_load(value_reg, lit->value.type, lit->value.data));
//             }

//             // Perform comparison
//             prog.emit(Opcodes::Test::create(result_reg, test_reg, value_reg, cond->op));

//             // Jump to "no_delete" if condition is false
//             prog.emit(Opcodes::JumpIf::create(result_reg, "no_delete", false));
//         }
//     }

//     // 6. Delete the current row (sets cursor_valid_reg and delete_occurred_reg)
//     prog.emit(Opcodes::Delete::create(table_cursor, cursor_valid_reg, delete_occurred_reg));

//     // 7. Check if cursor is still valid after delete
//     prog.emit(Opcodes::JumpIf::create(cursor_valid_reg, "delete_done", false));

//     // 8. Delete occurred and cursor valid, loop back (cursor already advanced due to key shift)
//     prog.emit(Opcodes::Goto::create("delete_loop"));

//     // 9. No delete case - need to step to next row
//     prog.label("no_delete");
//     prog.emit(Opcodes::Step::create(table_cursor, "delete_done", true));
//     prog.emit(Opcodes::Goto::create("delete_loop"));

//     // 10. Done - close and halt
//     prog.label("delete_done");
//     prog.emit(Opcodes::Close::create(table_cursor));
//     prog.emit(Opcodes::Halt::create(0));

//     // Resolve all labels
//     prog.resolve_labels();
// }


// Also update build_from_ast to handle UPDATE:
array<VMInstruction, QueryArena>
build_from_ast(Statement *ast)
{
	ProgramBuilder builder;

	switch (ast->type)
	{
	// case STMT_INSERT: {
	// 	build_insert(builder, (InsertStmt *)ast);
	// 	break;
	// }

	// case STMT_SELECT: {
	// 	build_select(builder, (SelectStmt *)ast);
	// 	break;
	// }

	// case STMT_UPDATE: {
	// 	build_update(builder, (UpdateStmt *)ast);
	// 	break;
	// }

	// case STMT_DELETE: {
	// build_delete(builder, (DeleteStmt*) ast);
	// break;
	// }


	default:
		break;
	}

	return builder.instructions;
}
