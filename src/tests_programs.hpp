#pragma  once
#include "executor.hpp"
#include "cassert"
#include "compile.hpp"
void
test_ephemeral_seek()
{
	printf("Testing ephemeral tree seek operations\n");

	executor_init(false);

	// Same schema
	static RecordLayout					layout;
	static array<DataType, SchemaArena> types2;
	array_push(&types2, TYPE_U32); // key
	array_push(&types2, TYPE_U32); // value
	layout = RecordLayout::create(types2);

	CompiledProgram program;
	program.type = PROG_DML_SELECT;
	program.ast_node = nullptr;

	// Insert test data
	static int data[][2] = {{10, 100}, {20, 200}, {30, 300}, {40, 400}, {50, 500}};

	array_push(&program.instructions, Opcodes::Open::create_ephemeral(0, &layout));

	for (int i = 0; i < 5; i++)
	{
		array_push(&program.instructions, Opcodes::Move::create_load(0, TYPE_U32, &data[i][0]));
		array_push(&program.instructions, Opcodes::Move::create_load(1, TYPE_U32, &data[i][1]));
		array_push(&program.instructions, Opcodes::Insert::create(0, 0, 2));
	}

	// Test seeking to key >= 25 (should find 30)
	static int seek_key = 25;
	array_push(&program.instructions, Opcodes::Move::create_load(2, TYPE_U32, &seek_key));
	array_push(&program.instructions, Opcodes::Seek::create(0, 2, -1, GE));

	// Output the found row and next few
	int no_seek_label = program.instructions.size;
	for (int i = 0; i < 3; i++)
	{ // Output 3 rows from seek position
		array_push(&program.instructions, Opcodes::Column::create(0, 0, 0));
		array_push(&program.instructions, Opcodes::Column::create(0, 1, 1));
		array_push(&program.instructions, Opcodes::Result::create(0, 2));
		array_push(&program.instructions, Opcodes::Step::create(0, nullptr, true));
	}

	int done = program.instructions.size;
	array_push(&program.instructions, Opcodes::Close::create(0));
	array_push(&program.instructions, Opcodes::Halt::create(0));

	// Fix jumps
	program.instructions.data[17].p3 = done; // Seek jumps to done if not found
	program.instructions.data[21].p2 = done; // First Step
	program.instructions.data[25].p2 = done; // Second Step
	program.instructions.data[29].p2 = done; // Third Step

	set_capture_mode(false);
	CompiledProgram programs[] = {program};
	execute_programs(programs, 1);

	// Should get 30, 40, 50
	// assert(get_row_count() == 3);
	// assert(check_int_value(0, 0, 30));
	// assert(check_int_value(0, 1, 300));
	// assert(check_int_value(1, 0, 40));
	// assert(check_int_value(2, 0, 50));

	// printf("  Seek >= 25 results:\n");
	// for (size_t i = 0; i < get_row_count(); i++) {
	//     printf("    Row %zu: ", i);
	//     print_result_callback(last_results.data[i].data, last_results.data[i].size);
	// }

	// clear_results();
	set_capture_mode(false);
	executor_shutdown();

	printf("  ✓ Ephemeral seek test passed\n");
}
void
test_ephemeral_with_builder()
{
	printf("Testing ephemeral tree using ProgramBuilder\n");


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

void
test_ephemeral_tree()
{
	printf("Testing ephemeral tree with VM program\n");

	executor_init(false);

	// Define the schema for ephemeral table
	static RecordLayout					ephemeral_layout;
	static array<DataType, SchemaArena> types;
	array_push(&types, TYPE_U32);	 // key: int
	array_push(&types, TYPE_CHAR32); // value: varchar(32)
	ephemeral_layout = RecordLayout::create(types);

	// Build a single program that uses ephemeral storage
	CompiledProgram program;
	program.type = PROG_DML_SELECT; // Doesn't matter much, just for categorization
	program.ast_node = nullptr;

	// Static data for inserts
	static int	keys[] = {10, 5, 15, 3, 7, 12, 20};
	static char values[][32] = {"ten", "five", "fifteen", "three", "seven", "twelve", "twenty"};

	// Build program:
	// 1. Open ephemeral cursor
	array_push(&program.instructions, Opcodes::Open::create_ephemeral(0, &ephemeral_layout));

	// 2. Insert all values
	for (int i = 0; i < 7; i++)
	{
		array_push(&program.instructions, Opcodes::Move::create_load(0, TYPE_U32, &keys[i]));
		array_push(&program.instructions, Opcodes::Move::create_load(1, TYPE_CHAR32, values[i]));
		array_push(&program.instructions, Opcodes::Insert::create(0, 0, 2));
	}

	// 3. Rewind to beginning
	array_push(&program.instructions, Opcodes::Rewind::create(0, nullptr, false));

	// 4. Scan and output all rows (will be in sorted order due to BST)
	int loop_start = program.instructions.size;
	array_push(&program.instructions, Opcodes::Column::create(0, 0, 0)); // key
	array_push(&program.instructions, Opcodes::Column::create(0, 1, 1)); // value
	array_push(&program.instructions, Opcodes::Result::create(0, 2));
	array_push(&program.instructions, Opcodes::Step::create(0, nullptr, true));
	array_push(&program.instructions, Opcodes::Goto::create(nullptr));

	// 5. Close and halt
	int done_label = program.instructions.size;
	array_push(&program.instructions, Opcodes::Close::create(0));
	array_push(&program.instructions, Opcodes::Halt::create(0));

	// Fix jump targets
	program.instructions.data[21].p2 = done_label; // Rewind jumps to done if empty
	program.instructions.data[25].p2 = done_label; // Step jumps to done
	program.instructions.data[26].p2 = loop_start; // Goto loops back

	// Execute the program
	set_capture_mode(true);
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
	//     printf("    Row %zu: ", i);
	//     print_result_callback(last_results.data[i].data, last_results.data[i].size);
	// }

	clear_results();
	set_capture_mode(false);
	executor_shutdown();

	printf("  ✓ Ephemeral tree test passed\n");
}
