// programbuilder.cpp - Simplified version with full table scans only
#include "compile.hpp"

#include "arena.hpp"
#include "defs.hpp"
#include "types.hpp"
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
	RegisterAllocator	 regs;

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

			char *label = (char *)inst.p4;
			if (nullptr == label)
			{
				continue;
			}

			auto entry = stringmap_get(&labels, label);
			if (entry == nullptr)
			{
				continue;
			}
			if (inst.p2 == -1)
			{
				inst.p2 = *entry;
			}
			if (inst.p3 == -1)
			{
				inst.p3 = *entry;
			}
			inst.p4 = nullptr;
		}
	}
};

// test_ephemeral_builder.cpp - Test ephemeral tree using ProgramBuilder
#include "executor.hpp"
#include "compile.hpp"
#include "vm.hpp"
#include "catalog.hpp"
#include <cassert>
#include <cstdio>

void
test_ephemeral_with_builder()
{
	printf("Testing ephemeral tree using ProgramBuilder\n");

	executor_init(false);

	// Define the schema for ephemeral table
	static RecordLayout					ephemeral_layout;
	static array<DataType, SchemaArena> types;
	array_push(&types, TYPE_U32);	 // key: int
	array_push(&types, TYPE_CHAR32); // value: varchar(32)
	ephemeral_layout = RecordLayout::create(types);

	// Use ProgramBuilder to construct the program
	ProgramBuilder builder;

	// Static data for inserts
	static int	keys[] = {10, 5, 15, 3, 7, 12, 20};
	static char values[][32] = {"ten", "five", "fifteen", "three", "seven", "twelve", "twenty"};

	// 1. Open ephemeral cursor
	builder.emit(Opcodes::Open::create_ephemeral(0, &ephemeral_layout));

	// 2. Insert all values using allocated registers
	for (int i = 0; i < 7; i++)
	{
		int key_reg = builder.regs.allocate();
		int val_reg = builder.regs.allocate();

		builder.emit(Opcodes::Move::create_load(key_reg, TYPE_U32, &keys[i]))
			.emit(Opcodes::Move::create_load(val_reg, TYPE_CHAR32, values[i]))
			.emit(Opcodes::Insert::create(0, key_reg, 2));
	}

	// 3. Rewind to beginning
	builder.emit(Opcodes::Rewind::create(0, "scan_done", false));

	// 4. Scan loop
	builder.label("scan_loop");

	int result_key_reg = builder.regs.allocate();
	int result_val_reg = builder.regs.allocate();

	builder.emit(Opcodes::Column::create(0, 0, result_key_reg))
		.emit(Opcodes::Column::create(0, 1, result_val_reg))
		.emit(Opcodes::Result::create(result_key_reg, 2))
		.emit(Opcodes::Step::create(0, "scan_done", true))
		.emit(Opcodes::Goto::create("scan_loop"));

	// 5. Done
	builder.label("scan_done").emit(Opcodes::Close::create(0)).emit(Opcodes::Halt::create(0));

	// Resolve all labels
	builder.resolve_labels();

	// Create CompiledProgram wrapper
	CompiledProgram program;
	program.type = PROG_DML_SELECT;
	program.instructions = builder.instructions;
	program.ast_node = nullptr;

	// Execute the program
	set_capture_mode(false);
	CompiledProgram programs[] = {program};
	execute_programs(programs, 1);

	// Verify results - should be in sorted order by key
	assert(get_row_count() == 7);

	// Check sorted order: 3, 5, 7, 10, 12, 15, 20
	assert(check_int_value(0, 0, 3));
	assert(check_string_value(0, 1, "three"));

	assert(check_int_value(1, 0, 5));
	assert(check_string_value(1, 1, "five"));

	assert(check_int_value(2, 0, 7));
	assert(check_string_value(2, 1, "seven"));

	assert(check_int_value(3, 0, 10));
	assert(check_string_value(3, 1, "ten"));

	assert(check_int_value(4, 0, 12));
	assert(check_string_value(4, 1, "twelve"));

	assert(check_int_value(5, 0, 15));
	assert(check_string_value(5, 1, "fifteen"));

	assert(check_int_value(6, 0, 20));
	assert(check_string_value(6, 1, "twenty"));

	printf("  Results (sorted by key):\n");
	// for (size_t i = 0; i < get_row_count(); i++) {
	//     printf("    Key=%d, Value=%s\n",
	//            *(int*)last_results.data[i].data[0].data,
	//            (char*)last_results.data[i].data[1].data);
	// }

	clear_results();
	set_capture_mode(false);
	executor_shutdown();

	printf("  ✓ Ephemeral tree with ProgramBuilder test passed\n");
}

//------------------ PROGRAMS ---------------------//
// Build functions for compile.cpp using new AST structures

void
build_select(ProgramBuilder &prog, SelectStmt *node)
{
	// Get table info
	Table *table = get_table(node->from_table->table_name);
	if (!table)
	{
		printf("Error: Table '%s' not found\n", node->from_table->table_name);
		return;
	}

	int table_cursor = 0;

	// 1. Open the table cursor for reading
	prog.emit(Opcodes::Open::create_btree(table_cursor, node->from_table->table_name, 0, false));

	// 2. Get the table schema to know how many columns we have
	int num_columns = table->columns.size;

	// 3. Allocate registers for the result row
	int result_start_reg = prog.regs.allocate_range(num_columns);

	// 4. Rewind cursor to the beginning of the table
	prog.emit(Opcodes::Rewind::create(table_cursor, "select_done", false));

	// 5. Loop start - this is where we'll jump back to for each row
	prog.label("select_loop");

	// 6. Evaluate WHERE clause if present
	if (node->where_clause)
	{
		// Handle single comparison: column op value
		Expr *cond = node->where_clause;

		if (cond->type == EXPR_BINARY_OP)
		{
			// Get the comparison operator
			CompareOp compare_op;
			switch (cond->op)
			{
			case OP_EQ:
				compare_op = EQ;
				break;
			case OP_NE:
				compare_op = NE;
				break;
			case OP_LT:
				compare_op = LT;
				break;
			case OP_LE:
				compare_op = LE;
				break;
			case OP_GT:
				compare_op = GT;
				break;
			case OP_GE:
				compare_op = GE;
				break;
			default:
				compare_op = EQ;
				break;
			}

			// Load the column value from cursor
			if (cond->left->type == EXPR_COLUMN)
			{
				uint32_t col_idx = get_column_index(node->from_table->table_name, cond->left->column_name);

				int test_reg = prog.regs.allocate();
				int value_reg = prog.regs.allocate();
				int result_reg = prog.regs.allocate();

				// Load column value
				prog.emit(Opcodes::Column::create(table_cursor, col_idx, test_reg));

				// Load comparison value
				if (cond->right->type == EXPR_LITERAL)
				{
					DataType type = cond->right->lit_type;
					uint8_t *data = nullptr;

					if (type == TYPE_U32)
					{
						data = (uint8_t *)&cond->right->int_val;
					}
					else
					{
						data = (uint8_t *)cond->right->str_val;
					}

					prog.emit(Opcodes::Move::create_load(value_reg, type, data));
				}

				// Perform comparison
				prog.emit(Opcodes::Test::create(result_reg, test_reg, value_reg, compare_op));

				// Jump to "skip_row" if condition is false
				prog.emit(Opcodes::JumpIf::create(result_reg, "skip_row", false));
			}
		}
	}

	// 7. Extract each column from the current row into registers
	for (int i = 0; i < num_columns; i++)
	{
		prog.emit(Opcodes::Column::create(table_cursor, i, result_start_reg + i));
	}

	// 8. Emit the result row (calls the callback with the row data)
	prog.emit(Opcodes::Result::create(result_start_reg, num_columns));

	// 9. Label for skipping row (WHERE condition false)
	prog.label("skip_row");

	// 10. Step to the next row, jump to "select_done" if no more rows
	prog.emit(Opcodes::Step::create(table_cursor, "select_done", true));

	// 11. Loop back
	prog.emit(Opcodes::Goto::create("select_loop"));

	// 12. Done - close cursor and halt
	prog.label("select_done");
	prog.emit(Opcodes::Close::create(table_cursor));
	prog.emit(Opcodes::Halt::create(0));

	prog.resolve_labels();
}

void
build_insert(ProgramBuilder &prog, InsertStmt *node)
{
	int			table_cursor = 0;
	const char *table_name = node->table_name;

	// Open cursor with write access
	prog.emit(Opcodes::Open::create_btree(table_cursor, table_name, 0, true));

	// Process each row of values
	for (size_t row = 0; row < node->values->size; row++)
	{
		auto value_row = node->values->data[row];

		// First register is for the key (first column), rest for data
		int first_reg = prog.regs.allocate_range(value_row->size);

		// Load each value into its register
		for (size_t i = 0; i < value_row->size; i++)
		{
			Expr *value = value_row->data[i];
			int	  target_reg = first_reg + i;

			if (value->type == EXPR_LITERAL)
			{
				DataType type = value->lit_type;
				uint8_t *data = nullptr;

				if (type == TYPE_U32 || type == TYPE_U64)
				{
					data = (uint8_t *)&value->int_val;
				}
				else
				{
					data = (uint8_t *)value->str_val;
				}

				prog.emit(Opcodes::Move::create_load(target_reg, type, data));
			}
			else if (value->type == EXPR_NULL)
			{
				// Handle NULL values
				prog.emit(Opcodes::Move::create_load(target_reg, TYPE_NULL, nullptr));
			}
		}

		prog.emit(Opcodes::Insert::create(table_cursor, first_reg, value_row->size));
	}

	prog.emit(Opcodes::Close::create(table_cursor));
	prog.emit(Opcodes::Halt::create(0));

	prog.resolve_labels();
}

void
build_update(ProgramBuilder &prog, UpdateStmt *node)
{
	// Get table info
	Table *table = get_table(node->table_name);
	if (!table)
	{
		printf("Error: Table '%s' not found\n", node->table_name);
		return;
	}

	int table_cursor = 0;

	// 1. Open cursor for writing
	prog.emit(Opcodes::Open::create_btree(table_cursor, node->table_name, 0, true));

	// 2. Rewind to beginning, jump to "update_done" if empty
	prog.emit(Opcodes::Rewind::create(table_cursor, "update_done", false));

	// 3. Main loop start
	prog.label("update_loop");

	// 4. Evaluate WHERE clause if present
	if (node->where_clause)
	{
		Expr *cond = node->where_clause;

		if (cond->type == EXPR_BINARY_OP)
		{
			CompareOp compare_op;
			switch (cond->op)
			{
			case OP_EQ:
				compare_op = EQ;
				break;
			case OP_NE:
				compare_op = NE;
				break;
			case OP_LT:
				compare_op = LT;
				break;
			case OP_LE:
				compare_op = LE;
				break;
			case OP_GT:
				compare_op = GT;
				break;
			case OP_GE:
				compare_op = GE;
				break;
			default:
				compare_op = EQ;
				break;
			}

			// Load the column value from cursor
			if (cond->left->type == EXPR_COLUMN)
			{
				uint32_t col_idx = get_column_index(node->table_name, cond->left->column_name);

				int test_reg = prog.regs.allocate();
				int value_reg = prog.regs.allocate();
				int result_reg = prog.regs.allocate();

				// Load column value
				prog.emit(Opcodes::Column::create(table_cursor, col_idx, test_reg));

				// Load comparison value
				if (cond->right->type == EXPR_LITERAL)
				{
					DataType type = cond->right->lit_type;
					uint8_t *data = nullptr;

					if (type == TYPE_U32 || type == TYPE_U64)
					{
						data = (uint8_t *)&cond->right->int_val;
					}
					else
					{
						data = (uint8_t *)cond->right->str_val;
					}

					prog.emit(Opcodes::Move::create_load(value_reg, type, data));
				}

				// Perform comparison
				prog.emit(Opcodes::Test::create(result_reg, test_reg, value_reg, compare_op));

				// Jump to "skip_update" if condition is false
				prog.emit(Opcodes::JumpIf::create(result_reg, "skip_update", false));
			}
		}
	}

	// 5. Build the updated record
	// First, load all existing column values
	int num_columns = table->columns.size;
	int record_start_reg = prog.regs.allocate_range(num_columns - 1); // -1 because key is separate

	// Load existing values (skip key at index 0)
	for (int i = 1; i < num_columns; i++)
	{
		prog.emit(Opcodes::Column::create(table_cursor, i, record_start_reg + (i - 1)));
	}

	// Apply SET clauses - overwrite specific columns
	for (size_t i = 0; i < node->columns->size; i++)
	{
		const char *col_name = node->columns->data[i];
		uint32_t	col_idx = get_column_index(node->table_name, col_name);

		if (col_idx > 0 && col_idx < num_columns)
		{
			// Load new value into the appropriate register
			int	  target_reg = record_start_reg + (col_idx - 1);
			Expr *value = node->values->data[i];

			if (value->type == EXPR_LITERAL)
			{
				DataType type = value->lit_type;
				uint8_t *data = nullptr;

				if (type == TYPE_U32 || type == TYPE_U64)
				{
					data = (uint8_t *)&value->int_val;
				}
				else
				{
					data = (uint8_t *)value->str_val;
				}

				prog.emit(Opcodes::Move::create_load(target_reg, type, data));
			}
		}
	}

	// 6. Update the current row
	prog.emit(Opcodes::Update::create(table_cursor, record_start_reg));

	// 7. Label for skipping update (WHERE condition false)
	prog.label("skip_update");

	// 8. Step to next row, jump to "update_done" if no more rows
	prog.emit(Opcodes::Step::create(table_cursor, "update_done", true));

	// 9. Loop back
	prog.emit(Opcodes::Goto::create("update_loop"));

	// 10. Done - close and halt
	prog.label("update_done");
	prog.emit(Opcodes::Close::create(table_cursor));
	prog.emit(Opcodes::Halt::create(0));

	// Resolve all labels
	prog.resolve_labels();
}

void
build_delete(ProgramBuilder &prog, DeleteStmt *node)
{
	// Get table info to validate it exists
	Table *table = get_table(node->table_name);
	if (!table)
	{
		printf("Error: Table '%s' not found\n", node->table_name);
		return;
	}

	int table_cursor = 0;

	// 1. Open cursor for writing
	prog.emit(Opcodes::Open::create_btree(table_cursor, node->table_name, 0, true));

	// 2. Rewind to beginning, jump to "delete_done" if empty
	prog.emit(Opcodes::Rewind::create(table_cursor, "delete_done", false));

	// 3. Allocate registers for tracking state
	int cursor_valid_reg = prog.regs.allocate();
	int delete_occurred_reg = prog.regs.allocate();

	// 4. Main loop start
	prog.label("delete_loop");

	// 5. Evaluate WHERE clause if present
	if (node->where_clause)
	{
		Expr *cond = node->where_clause;

		if (cond->type == EXPR_BINARY_OP)
		{
			CompareOp compare_op;
			switch (cond->op)
			{
			case OP_EQ:
				compare_op = EQ;
				break;
			case OP_NE:
				compare_op = NE;
				break;
			case OP_LT:
				compare_op = LT;
				break;
			case OP_LE:
				compare_op = LE;
				break;
			case OP_GT:
				compare_op = GT;
				break;
			case OP_GE:
				compare_op = GE;
				break;
			default:
				compare_op = EQ;
				break;
			}

			// Load the column value from cursor
			if (cond->left->type == EXPR_COLUMN)
			{
				uint32_t col_idx = get_column_index(node->table_name, cond->left->column_name);

				int test_reg = prog.regs.allocate();
				int value_reg = prog.regs.allocate();
				int result_reg = prog.regs.allocate();

				// Load column value
				prog.emit(Opcodes::Column::create(table_cursor, col_idx, test_reg));

				// Load comparison value
				if (cond->right->type == EXPR_LITERAL)
				{
					DataType type = cond->right->lit_type;
					uint8_t *data = nullptr;

					if (type == TYPE_U32 || type == TYPE_U64)
					{
						data = (uint8_t *)&cond->right->int_val;
					}
					else
					{
						data = (uint8_t *)cond->right->str_val;
					}

					prog.emit(Opcodes::Move::create_load(value_reg, type, data));
				}

				// Perform comparison
				prog.emit(Opcodes::Test::create(result_reg, test_reg, value_reg, compare_op));

				// Jump to "no_delete" if condition is false
				prog.emit(Opcodes::JumpIf::create(result_reg, "no_delete", false));
			}
		}
	}

	// 6. Delete the current row (sets cursor_valid_reg and delete_occurred_reg)
	prog.emit(Opcodes::Delete::create(table_cursor, cursor_valid_reg, delete_occurred_reg));

	// 7. Check if cursor is still valid after delete
	prog.emit(Opcodes::JumpIf::create(cursor_valid_reg, "delete_done", false));

	// 8. Delete occurred and cursor valid, loop back (cursor already advanced due to key shift)
	prog.emit(Opcodes::Goto::create("delete_loop"));

	// 9. No delete case - need to step to next row
	prog.label("no_delete");
	prog.emit(Opcodes::Step::create(table_cursor, "delete_done", true));
	prog.emit(Opcodes::Goto::create("delete_loop"));

	// 10. Done - close and halt
	prog.label("delete_done");
	prog.emit(Opcodes::Close::create(table_cursor));
	prog.emit(Opcodes::Halt::create(0));

	// Resolve all labels
	prog.resolve_labels();
}

array<VMInstruction, QueryArena>
load_table_ids_program()
{
	ProgramBuilder					   prog;
	array<pair<const char *, Table *>> tables_array;
	stringmap_collect(&tables, &tables_array);

	int cursor_id = 0;
	int result_reg = 0;
	int table_name_reg = 1;
	for (int i = 0; i < tables_array.size; i++)
	{

		prog.emit(Opcodes::Open::create_btree(cursor_id, tables_array.data[0].key, 0));
		prog.emit(Opcodes::Rewind::create_to_end(cursor_id, "end or something"));
		prog.emit(Opcodes::Column::create(cursor_id, 0, result_reg));
		prog.emit(Opcodes::Move::create_load(table_name_reg, TYPE_CHAR32, (void *)tables_array.data[i].key));
		prog.emit(Opcodes::Function::create(0, 0, 1, &load_id));
		prog.emit(Opcodes::Close::create(cursor_id));
		prog.label("_" + 1);
	}

	prog.emit(Opcodes::Halt::create());

	return prog.instructions;
}

// Update build_from_ast to handle the statements
array<VMInstruction, QueryArena>
build_from_ast(Statement *ast)
{
	ProgramBuilder builder;

	switch (ast->type)
	{
	case STMT_INSERT:
		build_insert(builder, ast->insert_stmt);
		break;

	case STMT_SELECT:
		build_select(builder, ast->select_stmt);
		break;

	case STMT_UPDATE:
		build_update(builder, ast->update_stmt);
		break;

	case STMT_DELETE:
		build_delete(builder, ast->delete_stmt);
		break;

	default:
		break;
	}

	return builder.instructions;
}
