// vm.hpp
#pragma once
#include "arena.hpp"
#include "bplustree.hpp"
#include "catalog.hpp"
#include "defs.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>

typedef bool (*VMFunction)(TypedValue	 *result,	 // Output register
						   TypedValue	 *args,		 // Input registers array
						   uint32_t		  arg_count, // Number of arguments
						   MemoryContext *ctx		 // For allocation if needed
);

enum class CursorType : uint8_t
{
	BPLUS,
	RED_BLACK,
	BLOB
};

struct CursorContext
{
    CursorType type;
	union {
	    Layout layout;
		struct
		{
		    Layout layout;
			BPlusTree tree;
		} btree;
	} context;
};

typedef void (*ResultCallback)(array<TypedValue, QueryArena> result);
extern bool _debug;

enum OpCode : uint8_t
{
	// Control flow
	OP_Goto = 1,
#define GOTO_MAKE(label)  {OP_Goto, 0, -1, 0, label, 0}
#define GOTO_TARGET(inst) ((inst).p2)

	OP_Halt = 2,
#define HALT_MAKE(exit_code) {OP_Halt, exit_code, 0, 0, nullptr, 0}
#define HALT_EXIT_CODE(inst) ((inst).p1)

	// Cursor operations
	OP_Open = 10,
#define OPEN_MAKE(cursor_id, context) {OP_Open, cursor_id, 0, 0, context, 0}
#define OPEN_CURSOR_ID(inst)			   ((inst).p1)
#define OPEN_LAYOUT(inst)				   ((CursorContext*)((inst).p4))

	OP_Close = 12,
#define CLOSE_MAKE(cursor_id) {OP_Close, cursor_id, 0, 0, nullptr, 0}
#define CLOSE_CURSOR_ID(inst) ((inst).p1)

	OP_Rewind = 13,
#define REWIND_MAKE(cursor_id, label, to_end) {OP_Rewind, cursor_id, -1, 0, label, (uint8_t)to_end}
#define REWIND_CURSOR_ID(inst)				  ((inst).p1)
#define REWIND_JUMP_IF_EMPTY(inst)			  ((inst).p2)
#define REWIND_TO_END(inst)					  ((inst).p5 != 0)

	OP_Step = 14,
#define STEP_MAKE(cursor_id, jump_label, forward) {OP_Step, cursor_id, -1, 0, jump_label, (uint8_t)forward}
#define STEP_CURSOR_ID(inst)					  ((inst).p1)
#define STEP_JUMP_IF_DONE(inst)					  ((inst).p2)
#define STEP_FORWARD(inst)						  ((inst).p5 != 0)

	OP_Seek = 20,
#define SEEK_MAKE(cursor_id, key_reg, jump_if_not, op) {OP_Seek, cursor_id, key_reg, jump_if_not, nullptr, (uint8_t)op}
#define SEEK_CURSOR_ID(inst)						   ((inst).p1)
#define SEEK_KEY_REG(inst)							   ((inst).p2)
#define SEEK_JUMP_IF_NOT(inst)						   ((inst).p3)
#define SEEK_OP(inst)								   ((CompareOp)((inst).p5))

	// Data operations
	OP_Column = 30,
#define COLUMN_MAKE(cursor_id, column_index, dest_reg) {OP_Column, cursor_id, column_index, dest_reg, nullptr, 0}
#define COLUMN_CURSOR_ID(inst)						   ((inst).p1)
#define COLUMN_INDEX(inst)							   ((inst).p2)
#define COLUMN_DEST_REG(inst)						   ((inst).p3)

	OP_Insert = 34,
#define INSERT_MAKE(cursor_id, start_reg, reg_count) {OP_Insert, cursor_id, start_reg, reg_count, nullptr, 0}
#define INSERT_CURSOR_ID(inst)						 ((inst).p1)
#define INSERT_KEY_REG(inst)						 ((inst).p2)
#define INSERT_REG_COUNT(inst)						 ((inst).p3)

	OP_Delete = 35,
#define DELETE_MAKE(cursor_id, cursor_valid_reg, delete_occured_reg)                                                   \
	{OP_Delete, cursor_id, cursor_valid_reg, delete_occured_reg, nullptr, 0}
#define DELETE_CURSOR_ID(inst)			((inst).p1)
#define DELETE_CURSOR_VALID_REG(inst)	((inst).p2)
#define DELETE_DELETE_OCCURED_REG(inst) ((inst).p3)

	OP_Update = 36,
#define UPDATE_MAKE(cursor_id, record_reg) {OP_Update, cursor_id, record_reg, 0, nullptr, 0}
#define UPDATE_CURSOR_ID(inst)			   ((inst).p1)
#define UPDATE_RECORD_REG(inst)			   ((inst).p2)

	// Register operations
	OP_Move = 40,
#define MOVE_MOVE_MAKE(dest_reg, src_reg) {OP_Move, dest_reg, 0, src_reg, nullptr, 0}
#define MOVE_DEST_REG(inst)				  ((inst).p1)
#define MOVE_SRC_REG(inst)				  ((inst).p3)

	OP_Load = 41,
#define LOAD_MAKE(dest_reg, type, data) {OP_Load, dest_reg, (int32_t)type, -1, data, 0}
#define LOAD_DEST_REG(inst)				((inst).p1)
#define LOAD_TYPE(inst)					((DataType)((inst).p2))
#define LOAD_DATA(inst)					((uint8_t *)((inst).p4))
#define LOAD_IS_LOAD(inst)				((inst).opcode == OP_Load)

	// Computation
	OP_Arithmetic = 51,
#define ARITHMETIC_MAKE(dest_reg, left_reg, right_reg, op)                                                             \
	{OP_Arithmetic, dest_reg, left_reg, right_reg, nullptr, (uint8_t)op}
#define ARITHMETIC_DEST_REG(inst)  ((inst).p1)
#define ARITHMETIC_LEFT_REG(inst)  ((inst).p2)
#define ARITHMETIC_RIGHT_REG(inst) ((inst).p3)
#define ARITHMETIC_OP(inst)		   ((ArithOp)((inst).p5))

	OP_JumpIf = 52,
#define JUMPIF_MAKE(test_reg, jump_label, jump_on_true) {OP_JumpIf, test_reg, -1, 0, jump_label, (uint8_t)jump_on_true}
#define JUMPIF_TEST_REG(inst)							((inst).p1)
#define JUMPIF_JUMP_TARGET(inst)						((inst).p2)
#define JUMPIF_JUMP_ON_TRUE(inst)						((inst).p5 != 0)

	OP_Logic = 53,
#define LOGIC_MAKE(dest_reg, left_reg, right_reg, op) {OP_Logic, dest_reg, left_reg, right_reg, nullptr, (uint8_t)op}
#define LOGIC_DEST_REG(inst)						  ((inst).p1)
#define LOGIC_LEFT_REG(inst)						  ((inst).p2)
#define LOGIC_RIGHT_REG(inst)						  ((inst).p3)
#define LOGIC_OP(inst)								  ((LogicOp)((inst).p5))

	OP_Result = 54,
#define RESULT_MAKE(first_reg, reg_count) {OP_Result, first_reg, reg_count, 0, nullptr, 0}
#define RESULT_FIRST_REG(inst)			  ((inst).p1)
#define RESULT_REG_COUNT(inst)			  ((inst).p2)

	OP_Test = 60,
#define TEST_MAKE(dest_reg, left_reg, right_reg, op) {OP_Test, dest_reg, left_reg, right_reg, nullptr, (uint8_t)op}
#define TEST_DEST_REG(inst)							 ((inst).p1)
#define TEST_LEFT_REG(inst)							 ((inst).p2)
#define TEST_RIGHT_REG(inst)						 ((inst).p3)
#define TEST_OP(inst)								 ((CompareOp)((inst).p5))

	OP_Function = 61,
#define FUNCTION_MAKE(dest_reg, first_arg_reg, arg_count, fn_ptr)                                                      \
	{OP_Function, dest_reg, first_arg_reg, arg_count, fn_ptr, 0}
#define FUNCTION_DEST_REG(inst)		 ((inst).p1)
#define FUNCTION_FIRST_ARG_REG(inst) ((inst).p2)
#define FUNCTION_ARG_COUNT(inst)	 ((inst).p3)
#define FUNCTION_FUNCTION(inst)		 ((VMFunction)((inst).p4))

	OP_Begin = 62,
#define BEGIN_MAKE() {OP_Begin, 0, 0, 0, nullptr, 0}

	OP_Commit = 63,
#define COMMIT_MAKE() {OP_Commit, 0, 0, 0, nullptr, 0}

	OP_Rollback = 64,
#define ROLLBACK_MAKE() {OP_Rollback, 0, 0, 0, nullptr, 0}
};
struct VMInstruction
{
	OpCode	opcode;
	int32_t p1;
	int32_t p2;
	int32_t p3;
	void   *p4;
	uint8_t p5;
};

enum VM_RESULT : uint8_t
{
	OK,
	ABORT,
	ERR
};

// VM Functions
VM_RESULT
vm_execute(VMInstruction *instructions, int instruction_count, MemoryContext *ctx);

void
vm_debug_print_all_registers();
void
vm_debug_print_cursor(int cursor_id);

// VM Runtime Definitions
#define REGISTERS 40
