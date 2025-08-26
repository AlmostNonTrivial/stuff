
// vm.hpp
#pragma once
#include "arena.hpp"
#include "blob.hpp"
#include "memtree.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "catalog.hpp"
#include "vec.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
struct VMValue {
	DataType type;
	uint8_t data[TYPE_256];
};


typedef void (*ResultCallback)(Vec<TypedValue, QueryArena> result);
extern bool _debug;
enum OpCode : uint32_t {
	// Control flow
	OP_Goto = 1,
	OP_Halt = 2,
	// Cursor operations
	OP_Open = 10,
	OP_Close = 12,
	OP_Rewind = 13,
	OP_Step = 14,
	OP_Seek = 20,
	// Data operations
	OP_Column = 30,
	OP_Insert = 34,
	OP_Delete = 35,
	OP_Update = 36,

	// Register operations
	OP_Move = 40,
	OP_Load = 41,
	// Computation
	OP_Test = 60,
	OP_Arithmetic = 51,
	OP_JumpIf = 52,
	OP_Logic = 53,
	OP_Result = 54,
	OP_Pattern = 61,
};
struct VMInstruction {
	OpCode opcode;
	int32_t p1;
	int32_t p2;
	int32_t p3;
	void *p4;
	uint8_t p5;
};

enum PatternType : uint8_t {
	PATTERN_LIKE = 0,
	PATTERN_GLOB = 1,
	PATTERN_REGEXP = 2,
	PATTERN_CONTAINS = 3,
};
// ============================================================================
// Opcode Namespace
// ============================================================================
namespace Opcodes
{
// Control Flow Operations
namespace Goto
{
inline VMInstruction
create(char*label)
{
	return {OP_Goto, 0, -1, 0, label, 0};
}
inline int32_t
target(const VMInstruction &inst)
{
	return inst.p2;
}
inline void
print(const VMInstruction &inst)
{
	printf("Goto %d", inst.p2);
}
} // namespace Goto
namespace Halt
{
inline VMInstruction
create(int32_t exit_code = 0)
{
	return {OP_Halt, exit_code, 0, 0, nullptr, 0};
}
inline int32_t
exit_code(const VMInstruction &inst)
{
	return inst.p1;
}
inline void
print(const VMInstruction &inst)
{
	printf("Halt %d", inst.p1);
}
} // namespace Halt
// Cursor Operations
namespace Open
{
inline VMInstruction
create_btree(int32_t cursor_id, const char *table_name, int32_t index_col = 0,
	     bool is_write = false)
{
	uint8_t flags = (is_write ? 0x01 : 0);
	return {OP_Open, cursor_id, index_col, 0, (void *)table_name, flags};
}
inline VMInstruction
create_ephemeral(int32_t cursor_id, RecordLayout *layout)
{
	uint8_t flags = (0x01) | (0x02);
	return {OP_Open, cursor_id, 0, 0, layout, flags};
}
inline int32_t
cursor_id(const VMInstruction &inst)
{
	return inst.p1;
}
inline RecordLayout *
ephemeral_schema(const VMInstruction &inst)
{
	return (RecordLayout *)inst.p4;
}
inline int32_t
index_col(const VMInstruction &inst)
{
	return inst.p2;
}
inline const char *
table_name(const VMInstruction &inst)
{
	return (const char *)inst.p4;
}
inline bool
is_write(const VMInstruction &inst)
{
	return inst.p5 & 0x01;
}
inline bool
is_ephemeral(const VMInstruction &inst)
{
	return inst.p5 & 0x02;
}
inline void
print(const VMInstruction &inst)
{
	if (is_ephemeral(inst)) {
		printf("Open cursor=%d ephemeral write=%d", inst.p1,
		       is_write(inst));
	} else {
		printf("Open cursor=%d table=%s index_col=%d write=%d", inst.p1,
		       table_name(inst), inst.p2, is_write(inst));
	}
}
} // namespace Open
namespace Close
{
inline VMInstruction
create(int32_t cursor_id)
{
	return {OP_Close, cursor_id, 0, 0, nullptr, 0};
}
inline int32_t
cursor_id(const VMInstruction &inst)
{
	return inst.p1;
}
inline void
print(const VMInstruction &inst)
{
	printf("Close cursor=%d", inst.p1);
}
} // namespace Close
namespace Rewind
{
inline VMInstruction
create(int32_t cursor_id, char*label, bool to_end = false)
{
	return {OP_Rewind, cursor_id, -1,
		0,	   label,   (uint8_t)to_end};
}
inline int32_t
cursor_id(const VMInstruction &inst)
{
	return inst.p1;
}
inline int32_t
jump_if_empty(const VMInstruction &inst)
{
	return inst.p2;
}
inline bool
to_end(const VMInstruction &inst)
{
	return inst.p5 != 0;
}
inline void
print(const VMInstruction &inst)
{
	printf("Rewind cursor=%d jump_if_empty=%d to_end=%d", inst.p1, inst.p2,
	       inst.p5);
}
} // namespace Rewind
namespace Step
{
inline VMInstruction
create(int32_t cursor_id, char* jump_label = nullptr, bool forward = true)
{
	return {OP_Step, cursor_id, -1, 0, jump_label, (uint8_t)forward};
}
inline int32_t
cursor_id(const VMInstruction &inst)
{
	return inst.p1;
}
inline int32_t
jump_if_done(const VMInstruction &inst)
{
	return inst.p2;
}
inline bool
forward(const VMInstruction &inst)
{
	return inst.p5 != 0;
}
inline void
print(const VMInstruction &inst)
{
	printf("Step cursor=%d jump_if_done=%d forward=%d", inst.p1, inst.p2,
	       inst.p5);
}
} // namespace Step
namespace Seek
{
inline VMInstruction
create(int32_t cursor_id, int32_t key_reg, int32_t jump_if_not, CompareOp op)
{
	return {OP_Seek, cursor_id, key_reg, jump_if_not, nullptr, (uint8_t)op};
}
inline int32_t
cursor_id(const VMInstruction &inst)
{
	return inst.p1;
}
inline int32_t
key_reg(const VMInstruction &inst)
{
	return inst.p2;
}
inline int32_t
jump_if_not(const VMInstruction &inst)
{
	return inst.p3;
}
inline CompareOp
op(const VMInstruction &inst)
{
	return (CompareOp)inst.p5;
}
inline void
print(const VMInstruction &inst)
{
	const char *op_names[] = {"EQ", "NE", "LT", "LE", "GT", "GE"};
	printf("Seek cursor=%d key_reg=%d jump_if_not=%d op=%s", inst.p1,
	       inst.p2, inst.p3, op_names[inst.p5]);
}
} // namespace Seek
// Data Operations
namespace Column
{
inline VMInstruction
create(int32_t cursor_id, int32_t column_index, int32_t dest_reg)
{
	return {OP_Column, cursor_id, column_index, dest_reg, nullptr, 0};
}
inline int32_t
cursor_id(const VMInstruction &inst)
{
	return inst.p1;
}
inline int32_t
column_index(const VMInstruction &inst)
{
	return inst.p2;
}
inline int32_t
dest_reg(const VMInstruction &inst)
{
	return inst.p3;
}
inline void
print(const VMInstruction &inst)
{
	printf("Column cursor=%d col=%d dest_reg=%d", inst.p1, inst.p2,
	       inst.p3);
}
} // namespace Column
namespace Insert
{
inline VMInstruction
create(int32_t cursor_id, int32_t start_reg, int32_t reg_count)
{
	return {OP_Insert, cursor_id, start_reg,
		reg_count, nullptr,   (uint8_t)false};
}
inline VMInstruction
create_variable(int32_t cursor_id, int32_t src_reg, int32_t size)
{
	return {OP_Insert, cursor_id, src_reg, size, nullptr, (uint8_t)true};
}
inline int32_t
cursor_id(const VMInstruction &inst)
{
	return inst.p1;
}
inline int32_t
key_reg(const VMInstruction &inst)
{
	return inst.p2;
}
inline int32_t
reg_count(const VMInstruction &inst)
{
	return inst.p3;
}
uint32_t inline size(const VMInstruction &inst)
{
	return inst.p3;
}

inline bool
is_variable_length(const VMInstruction &inst)
{
	return (bool)inst.p5;
}
inline void
print(const VMInstruction &inst)
{
	printf("Insert cursor=%d key_reg=%d reg_count=%d", inst.p1, inst.p2,
	       inst.p3);
}
} // namespace Insert
namespace Delete
{
inline VMInstruction
create(int32_t cursor_id, int32_t cursor_valid_reg, int32_t delete_occured_reg)
{
	return {OP_Delete, cursor_id, cursor_valid_reg, delete_occured_reg, nullptr, 0};
}
inline int32_t
cursor_id(const VMInstruction &inst)
{
	return inst.p1;
}
inline int32_t
delete_occured_reg(const VMInstruction &inst)
{
	return inst.p3;
}
inline int32_t
cursor_valid_reg(const VMInstruction &inst)
{
	return inst.p2;
}


inline void
print(const VMInstruction &inst)
{
	printf("Delete cursor=%d", inst.p1);
}
} // namespace Delete
namespace Update
{
inline VMInstruction
create(int32_t cursor_id, int32_t record_reg)
{
	return {OP_Update, cursor_id, record_reg, 0, nullptr, 0};
}
inline int32_t
cursor_id(const VMInstruction &inst)
{
	return inst.p1;
}
inline int32_t
record_reg(const VMInstruction &inst)
{
	return inst.p2;
}
inline void
print(const VMInstruction &inst)
{
	printf("Update cursor=%d record_reg=%d", inst.p1, inst.p2);
}
} // namespace Update
// Register Operations
namespace Move
{
inline VMInstruction
create_load(int32_t dest_reg, DataType type, void *data)
{
	return {OP_Load, dest_reg, (int32_t)type, -1, data, 0};
}
inline VMInstruction
create_move(int32_t dest_reg, int32_t src_reg)
{
	return {OP_Move, dest_reg, 0, src_reg, nullptr, 0};
}
inline bool
is_load(const VMInstruction &inst)
{
	return inst.opcode == OP_Load;
}
inline int32_t
dest_reg(const VMInstruction &inst)
{
	return inst.p1;
}
inline int32_t
src_reg(const VMInstruction &inst)
{
	return inst.p3;
}
inline uint8_t *
data(const VMInstruction &inst)
{
	return (uint8_t *)inst.p4;
}
inline DataType
type(const VMInstruction &inst)
{
	return (DataType)inst.p2;
}
inline void
print(const VMInstruction &inst)
{
	if (inst.opcode == OP_Load) {
		printf("Load reg=%d type=%d", inst.p1, inst.p2);
		if (inst.p4) {
			printf(" value=");
			print_value((DataType)inst.p2, (uint8_t *)inst.p4);
		}
	} else {
		printf("Move dest=%d src=%d", inst.p1, inst.p3);
	}
}
} // namespace Move
// Computation
namespace Test
{
inline VMInstruction
create(int32_t dest_reg, int32_t left_reg, int32_t right_reg, CompareOp op)
{
	return {OP_Test, dest_reg, left_reg, right_reg, nullptr, (uint8_t)op};
}
inline int32_t
dest_reg(const VMInstruction &inst)
{
	return inst.p1;
}
inline int32_t
left_reg(const VMInstruction &inst)
{
	return inst.p2;
}
inline int32_t
right_reg(const VMInstruction &inst)
{
	return inst.p3;
}
inline CompareOp
op(const VMInstruction &inst)
{
	return (CompareOp)inst.p5;
}
inline void
print(const VMInstruction &inst)
{
	const char *op_names[] = {"EQ", "NE", "LT", "LE", "GT", "GE"};
	printf("Test dest=%d left=%d right=%d op=%s", inst.p1, inst.p2, inst.p3,
	       op_names[inst.p5]);
}
} // namespace Test
namespace Arithmetic
{
inline VMInstruction
create(int32_t dest_reg, int32_t left_reg, int32_t right_reg, ArithOp op)
{
	return {OP_Arithmetic, dest_reg, left_reg,
		right_reg,     nullptr,	 (uint8_t)op};
}
inline int32_t
dest_reg(const VMInstruction &inst)
{
	return inst.p1;
}
inline int32_t
left_reg(const VMInstruction &inst)
{
	return inst.p2;
}
inline int32_t
right_reg(const VMInstruction &inst)
{
	return inst.p3;
}
inline ArithOp
op(const VMInstruction &inst)
{
	return (ArithOp)inst.p5;
}
inline void
print(const VMInstruction &inst)
{
	const char *op_names[] = {"ADD", "SUB", "MUL", "DIV", "MOD"};
	printf("Arithmetic dest=%d left=%d right=%d op=%s", inst.p1, inst.p2,
	       inst.p3, op_names[inst.p5]);
}
} // namespace Arithmetic
namespace JumpIf
{
inline VMInstruction
create(int32_t test_reg, char* jump_label, bool jump_on_true = true)
{
	return {OP_JumpIf, test_reg, -1,
		0,	   jump_label,  (uint8_t)jump_on_true};
}
inline int32_t
test_reg(const VMInstruction &inst)
{
	return inst.p1;
}
inline int32_t
jump_target(const VMInstruction &inst)
{
	return inst.p2;
}
inline bool
jump_on_true(const VMInstruction &inst)
{
	return inst.p5 != 0;
}
inline void
print(const VMInstruction &inst)
{
	printf("JumpIf test_reg=%d target=%d on_true=%d", inst.p1, inst.p2,
	       inst.p5);
}
} // namespace JumpIf
namespace Logic
{
inline VMInstruction
create(int32_t dest_reg, int32_t left_reg, int32_t right_reg, LogicOp op)
{
	return {OP_Logic, dest_reg, left_reg, right_reg, nullptr, (uint8_t)op};
}
inline int32_t
dest_reg(const VMInstruction &inst)
{
	return inst.p1;
}
inline int32_t
left_reg(const VMInstruction &inst)
{
	return inst.p2;
}
inline int32_t
right_reg(const VMInstruction &inst)
{
	return inst.p3;
}
inline LogicOp
op(const VMInstruction &inst)
{
	return (LogicOp)inst.p5;
}
inline void
print(const VMInstruction &inst)
{
	const char *op_names[] = {"AND", "OR"};
	printf("Logic dest=%d left=%d right=%d op=%s", inst.p1, inst.p2,
	       inst.p3, op_names[inst.p5]);
}
} // namespace Logic
namespace Result
{
inline VMInstruction
create(int32_t first_reg, int32_t reg_count)
{
	return {OP_Result, first_reg, reg_count, 0, nullptr, 0};
}
inline int32_t
first_reg(const VMInstruction &inst)
{
	return inst.p1;
}
inline int32_t
reg_count(const VMInstruction &inst)
{
	return inst.p2;
}
inline void
print(const VMInstruction &inst)
{
	printf("Result first_reg=%d count=%d", inst.p1, inst.p2);
}
} // namespace Result
namespace Pattern
{
inline VMInstruction
create(int32_t str_reg, int32_t pattern_reg, int32_t result_reg,
       PatternType type)
{
	return {OP_Pattern,   str_reg, pattern_reg, result_reg, nullptr,

		(uint8_t)type};
}

inline int32_t
str_reg(const VMInstruction &inst)
{
	return inst.p1;
}
inline int32_t
pattern_reg(const VMInstruction &inst)
{
	return inst.p2;
}
inline int32_t
result_reg(const VMInstruction &inst)
{
	return inst.p3;
}
inline PatternType
pattern_type(const VMInstruction &inst)
{
	return (PatternType)inst.p5;
}

inline void
print(const VMInstruction &inst)
{
	printf(
	    "Like str_reg=%d pattern_reg=%d result_reg=%d case_insensitive=%d",
	    inst.p1, inst.p2, inst.p3, inst.p5);
}
} // namespace Pattern
} // namespace Opcodes
// ============================================================================
// Debug Namespace
// ============================================================================
namespace Debug
{
inline const char *
opcode_name(OpCode op)
{
	switch (op) {
	case OP_Goto:
		return "Goto";
	case OP_Halt:
		return "Halt";
	case OP_Open:
		return "Open";
	case OP_Close:
		return "Close";
	case OP_Rewind:
		return "Rewind";
	case OP_Step:
		return "Step";
	case OP_Seek:
		return "Seek";
	case OP_Column:
		return "Column";
	case OP_Insert:
		return "Insert";
	case OP_Delete:
		return "Delete";
	case OP_Update:
		return "Update";
	case OP_Move:
		return "Move";
	case OP_Load:
		return "Load";
	case OP_Test:
		return "Test";
	case OP_Arithmetic:
		return "Arithmetic";
	case OP_JumpIf:
		return "JumpIf";
	case OP_Logic:
		return "Logic";
	case OP_Result:
		return "Result";
	default:
		return "Unknown";
	}
}
inline void
print_instruction(const VMInstruction &inst, int pc = -1)
{
	if (pc >= 0) {
		printf("%3d: ", pc);
	}
	printf("%-12s ", opcode_name(inst.opcode));
	switch (inst.opcode) {
	case OP_Goto:
		Opcodes::Goto::print(inst);
		break;
	case OP_Halt:
		Opcodes::Halt::print(inst);
		break;
	case OP_Open:
		Opcodes::Open::print(inst);
		break;
	case OP_Close:
		Opcodes::Close::print(inst);
		break;
	case OP_Rewind:
		Opcodes::Rewind::print(inst);
		break;
	case OP_Step:
		Opcodes::Step::print(inst);
		break;
	case OP_Seek:
		Opcodes::Seek::print(inst);
		break;
	case OP_Column:
		Opcodes::Column::print(inst);
		break;
	case OP_Insert:
		Opcodes::Insert::print(inst);
		break;
	case OP_Delete:
		Opcodes::Delete::print(inst);
		break;
	case OP_Update:
		Opcodes::Update::print(inst);
		break;
	case OP_Move:
	case OP_Load:
		Opcodes::Move::print(inst);
		break;
	case OP_Test:
		Opcodes::Test::print(inst);
		break;
	case OP_Arithmetic:
		Opcodes::Arithmetic::print(inst);
		break;
	case OP_JumpIf:
		Opcodes::JumpIf::print(inst);
		break;
	case OP_Logic:
		Opcodes::Logic::print(inst);
		break;
	case OP_Result:
		Opcodes::Result::print(inst);
		break;
	default:
		printf("p1=%d p2=%d p3=%d", inst.p1, inst.p2, inst.p3);
	}
	printf("\n");
}
inline void
print_program(VMInstruction *program, int program_size)
{
	printf("===== VM PROGRAM (%zu instructions) =====\n", program_size);
	for (size_t i = 0; i < program_size; i++) {
		print_instruction(program[i], i);
	}
	printf("==========================================\n");
}
inline void
print_register(int reg_num, const VMValue &value)
{
	printf("R[%2d]: type=%3d ", reg_num, value.type);
	printf("value=");
	print_value(value.type, value.data);
	printf("\n");
}
inline void
print_cursor_state(int cursor_id, const char *state)
{
	printf("Cursor[%d]: %s\n", cursor_id, state);
}

} // namespace Debug
// VM Runtime Definitions
#define REGISTERS 40
#define CURSORS	  10
enum VM_RESULT { OK, ABORT, ERR };
// VM Functions
VM_RESULT
vm_execute(VMInstruction *instructions, int instruction_count,
	   MemoryContext *ctx);

void
vm_debug_print_all_registers();
void
vm_debug_print_cursor(int cursor_id);
