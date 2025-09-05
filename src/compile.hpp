#pragma once
#include "types.hpp"
#include "common.hpp"
#include "arena.hpp"
#include "vm.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>

struct CursorAllocator
{
	uint32_t				next_cursor = 0;
	array<int, query_arena> free_list;

	int
	allocate()
	{
		if (free_list.size > 0)
		{
			return free_list.data[--free_list.size];
		}
		return next_cursor++;
	}

	void
	free(int cursor_id)
	{
		array_push(&free_list, cursor_id);
	}
};

// Enhanced RegisterAllocator with strong scope-based management
struct RegisterAllocator
{
	int						next_free = 0;
	array<int, query_arena> scope_stack;

	// Allocate a single register (optionally specific one)
	int
	allocate(int specific = -1)
	{
		if (specific >= 0)
		{
			assert(specific < REGISTERS && "Register out of range");
			// If requesting a specific register, it better be at or past our allocation point
			assert(specific >= next_free && "Cannot allocate already-used register");
			next_free = specific + 1;
			return specific;
		}

		assert(next_free < REGISTERS && "Out of registers");
		return next_free++;
	}

	// Allocate a contiguous range of registers
	int
	allocate_range(int count, int start_at = -1)
	{
		if (start_at >= 0)
		{
			assert(start_at + count <= REGISTERS && "Register range out of bounds");
			assert(start_at >= next_free && "Cannot allocate in used range");
			int first = start_at;
			next_free = start_at + count;
			return first;
		}

		assert(next_free + count <= REGISTERS && "Not enough registers for range");
		int first = next_free;
		next_free += count;
		return first;
	}

	// Reserve space without returning a register (for manual management)
	void
	reserve(int count)
	{
		assert(next_free + count <= REGISTERS && "Not enough registers to reserve");
		next_free += count;
	}

	void
	push_scope()
	{
		array_push(&scope_stack, next_free);
	}

	void
	pop_scope()
	{
		assert(scope_stack.size > 0 && "No scope to pop");
		next_free = scope_stack.data[scope_stack.size - 1];
		scope_stack.size--;
	}

	int
	mark() const
	{
		return next_free;
	}

	void
	restore(int mark)
	{
		assert(mark <= next_free && "Cannot restore to future position");
		next_free = mark;
	}

	int
	available() const
	{
		return REGISTERS - next_free;
	}

	void
	reset()
	{
		next_free = 0;
		array_clear(&scope_stack);
	}
};

struct LoopContext
{
	const char *start_label;
	const char *end_label;
	int			saved_reg_mark;
};

struct WhileContext
{
	const char *condition_label;
	const char *end_label;
	int			condition_reg;
	int			saved_reg_mark;
};

struct CondContext
{
	const char *else_label;
	const char *end_label;
	int			saved_reg_mark;
	bool		has_else;
};

struct ProgramBuilder
{
	array<VMInstruction, query_arena> instructions;
	string_map<uint32_t, query_arena> labels;
	array<uint32_t, query_arena>	  unresolved_jumps;
	RegisterAllocator				  regs;
	CursorAllocator					  cursors;
	int								  label_counter = 0;

	LoopContext current_loop;

	// ========================================================================
	// Basic Operations
	// ========================================================================

	template <typename T>
	T *
	alloc_value(T value)
	{
		T *ptr = (T *)arena::alloc<query_arena>(sizeof(T));
		*ptr = value;
		return ptr;
	}

	char *
	alloc_string(const char *str, size_t size)
	{
		char *ptr = (char *)arena::alloc<query_arena>(size);
		memset(ptr, 0, size); // Zero entire buffer
		if (str)
		{
			size_t len = strlen(str);
			size_t copy_len = (len < size - 1) ? len : size - 1;
			memcpy(ptr, str, copy_len); // Copy only what fits
		}
		ptr[size - 1] = '\0'; // Ensure null termination
		return ptr;
	}
	void
	emit(VMInstruction inst)
	{
		array_push(&instructions, inst);

		// Track instructions that need label resolution
		if (inst.p2 == -1 || inst.p3 == -1)
		{
			array_push(&unresolved_jumps, instructions.size - 1);
		}
		return;
		;
	}

	void
	label(const char *name)
	{
		stringmap_insert(&labels, name, instructions.size);
		return;
		;
	}

	const char *
	generate_label(const char *prefix = "L")
	{
		char *name = (char *)arena::alloc<query_arena>(32);
		snprintf(name, 32, "%s%d", prefix, label_counter++);
		return name;
	}

	int
	here() const
	{
		return instructions.size;
	}

	void
	resolve_labels()
	{
		for (uint32_t i = 0; i < unresolved_jumps.size; i++)
		{
			uint32_t	   inst_idx = unresolved_jumps.data[i];
			VMInstruction &inst = instructions.data[inst_idx];

			if (inst.p4)
			{ // Label stored temporarily in p4
				const char *label_name = (const char *)inst.p4;
				uint32_t   *target = stringmap_get(&labels, label_name);

				if (target)
				{
					if (inst.p2 == -1)
						inst.p2 = *target;
					if (inst.p3 == -1)
						inst.p3 = *target;
				}
				inst.p4 = nullptr;
			}
		}
	}

	// ========================================================================
	// Control Flow
	// ========================================================================

	void
	goto_label(const char *label_name)
	{
		emit(GOTO_MAKE((void *)label_name));
		return;
		;
	}

	void
	halt(int exit_code = 0)
	{
		emit(HALT_MAKE(exit_code));
		return;
		;
	}

	void
	jumpif_true(int test_reg, const char *label)
	{
		emit(JUMPIF_MAKE(test_reg, (void *)label, true));
		return;
		;
	}

	void
	jumpif_false(int test_reg, const char *label)
	{
		emit(JUMPIF_MAKE(test_reg, (void *)label, false));
		return;
		;
	}

	void
	jumpif_zero(int test_reg, const char *label)
	{
		return jumpif_false(test_reg, label);
	}

	void
	jumpif_not_zero(int test_reg, const char *label)
	{
		return jumpif_true(test_reg, label);
	}

	// ========================================================================
	// Loop Management
	// ========================================================================

	LoopContext
	begin_loop(const char *name = nullptr)
	{
		if (!name)
			name = generate_label("loop");

		LoopContext ctx = {name, generate_label("end"), regs.mark()};

		label(ctx.start_label);
		current_loop = ctx;
		return ctx;
	}

	void
	end_loop(const LoopContext &ctx)
	{
		goto_label(ctx.start_label);
		label(ctx.end_label);
		regs.restore(ctx.saved_reg_mark);
		return;
		;
	}

	void
	break_loop()
	{
		goto_label(current_loop.end_label);
		return;
		;
	}

	void
	continue_loop()
	{
		goto_label(current_loop.start_label);
		return;
		;
	}

	// ========================================================================
	// While Loop Management
	// ========================================================================

	WhileContext
	begin_while(int condition_reg)
	{
		WhileContext ctx = {generate_label("while_check"), generate_label("while_end"), condition_reg, regs.mark()};

		label(ctx.condition_label);
		jumpif_zero(condition_reg, ctx.end_label);
		return ctx;
	}

	void
	end_while(const WhileContext &ctx)
	{
		goto_label(ctx.condition_label);
		label(ctx.end_label);
		regs.restore(ctx.saved_reg_mark);
		return;
		;
	}

	WhileContext
	begin_do()
	{
		WhileContext ctx = {generate_label("do_start"), generate_label("do_end"),
							-1, // No condition reg yet
							regs.mark()};

		label(ctx.condition_label);
		return ctx;
	}

	void
	end_while_condition(const WhileContext &ctx, int condition_reg)
	{
		jumpif_not_zero(condition_reg, ctx.condition_label);
		label(ctx.end_label);
		regs.restore(ctx.saved_reg_mark);
		return;
		;
	}

	// ========================================================================
	// Conditional Management
	// ========================================================================

	CondContext
	begin_if(int test_reg)
	{
		CondContext ctx = {generate_label("else"), generate_label("endif"), regs.mark(), false};

		jumpif_false(test_reg, ctx.else_label);
		return ctx;
	}

	void
	begin_else(CondContext &ctx)
	{
		goto_label(ctx.end_label);
		label(ctx.else_label);
		ctx.has_else = true;
		return;
		;
	}

	void
	end_if(const CondContext &ctx)
	{
		if (!ctx.has_else)
		{
			label(ctx.else_label);
		}
		label(ctx.end_label);
		regs.restore(ctx.saved_reg_mark);
		return;
		;
	}

	// ========================================================================
	// Data Loading and Register Operations
	// ========================================================================

	int
	load(const TypedValue &value, int dest_reg = -1)
	{
		if (dest_reg == -1)
		{
			dest_reg = regs.allocate();
		}
		emit(LOAD_MAKE(dest_reg, (int64_t)value.type, value.data));
		return dest_reg;
	}

	int
	load(DataType type, void *value, int dest_reg = -1)
	{
		if (dest_reg == -1)
		{
			dest_reg = regs.allocate();
		}
		emit(LOAD_MAKE(dest_reg, (int64_t)type, value));
		return dest_reg;
	}

	int
	load_null(int dest_reg = -1)
	{
		return load(TypedValue::make(TYPE_NULL, nullptr), dest_reg);
	}

	int
	move(int src_reg, int dest_reg = -1)
	{
		if (dest_reg == -1)
		{
			dest_reg = regs.allocate();
		}
		emit(MOVE_MOVE_MAKE(dest_reg, src_reg));
		return dest_reg;
	}

	// ========================================================================
	// Arithmetic and Logic
	// ========================================================================

	int
	arithmetic(int left_reg, int right_reg, arith_op op, int dest_reg = -1)
	{
		if (dest_reg == -1)
		{
			dest_reg = regs.allocate();
		}
		emit(ARITHMETIC_MAKE(dest_reg, left_reg, right_reg, op));
		return dest_reg;
	}

	int
	add(int left_reg, int right_reg, int dest_reg = -1)
	{
		return arithmetic(left_reg, right_reg, ARITH_ADD, dest_reg);
	}

	int
	sub(int left_reg, int right_reg, int dest_reg = -1)
	{
		return arithmetic(left_reg, right_reg, ARITH_SUB, dest_reg);
	}

	int
	mul(int left_reg, int right_reg, int dest_reg = -1)
	{
		return arithmetic(left_reg, right_reg, ARITH_MUL, dest_reg);
	}

	int
	div(int left_reg, int right_reg, int dest_reg = -1)
	{
		return arithmetic(left_reg, right_reg, ARITH_DIV, dest_reg);
	}

	int
	mod(int left_reg, int right_reg, int dest_reg = -1)
	{
		return arithmetic(left_reg, right_reg, ARITH_MOD, dest_reg);
	}

	int
	test(int left_reg, int right_reg, comparison_op op, int dest_reg = -1)
	{
		if (dest_reg == -1)
		{
			dest_reg = regs.allocate();
		}
		emit(TEST_MAKE(dest_reg, left_reg, right_reg, op));
		return dest_reg;
	}

	int
	eq(int left_reg, int right_reg, int dest_reg = -1)
	{
		return test(left_reg, right_reg, EQ, dest_reg);
	}

	int
	ne(int left_reg, int right_reg, int dest_reg = -1)
	{
		return test(left_reg, right_reg, NE, dest_reg);
	}

	int
	lt(int left_reg, int right_reg, int dest_reg = -1)
	{
		return test(left_reg, right_reg, LT, dest_reg);
	}

	int
	le(int left_reg, int right_reg, int dest_reg = -1)
	{
		return test(left_reg, right_reg, LE, dest_reg);
	}

	int
	gt(int left_reg, int right_reg, int dest_reg = -1)
	{
		return test(left_reg, right_reg, GT, dest_reg);
	}

	int
	ge(int left_reg, int right_reg, int dest_reg = -1)
	{
		return test(left_reg, right_reg, GE, dest_reg);
	}

	int
	logic(int left_reg, int right_reg, logic_op op, int dest_reg = -1)
	{
		if (dest_reg == -1)
		{
			dest_reg = regs.allocate();
		}
		emit(LOGIC_MAKE(dest_reg, left_reg, right_reg, op));
		return dest_reg;
	}

	int
	logic_and(int left_reg, int right_reg, int dest_reg = -1)
	{
		return logic(left_reg, right_reg, LOGIC_AND, dest_reg);
	}

	int
	logic_or(int left_reg, int right_reg, int dest_reg = -1)
	{
		return logic(left_reg, right_reg, LOGIC_OR, dest_reg);
	}

	// ========================================================================
	// Cursor Operations
	// ========================================================================

	int
	open_cursor(CursorContext *context)
	{
		int cursor_id = cursors.allocate();
		emit(OPEN_MAKE(cursor_id, context));
		return cursor_id;
	}

	void
	close_cursor(int cursor_id)
	{
		emit(CLOSE_MAKE(cursor_id));
	}

	int
	rewind(int cursor_id, bool to_end = false, int result_reg = -1)
	{
		if (result_reg == -1)
		{
			result_reg = regs.allocate();
		}
		emit(REWIND_MAKE(cursor_id, result_reg, to_end));
		return result_reg;
	}

	int
	first(int cursor_id, int result_reg = -1)
	{
		return rewind(cursor_id, false, result_reg);
	}
	int
	last(int cursor_id, int result_reg = -1)
	{
		return rewind(cursor_id, false, result_reg);
	}

	int
	step(int cursor_id, int result_reg = -1, bool forward = true)
	{
		if (result_reg == -1)
		{
			result_reg = regs.allocate();
		}
		emit(STEP_MAKE(cursor_id, result_reg, forward));
		return result_reg;
	}

	int
	next(int cursor_id, int result_reg = -1)
	{
		return step(cursor_id, result_reg, true);
	}

	int
	prev(int cursor_id, int result_reg = -1)
	{
		return step(cursor_id, result_reg, false);
	}

	int
	seek(int cursor_id, int key_reg, comparison_op op = EQ, int result_reg = -1)
	{
		if (result_reg == -1)
		{
			result_reg = regs.allocate();
		}
		emit(SEEK_MAKE(cursor_id, key_reg, result_reg, op));
		return result_reg;
	}

	int
	get_column(int cursor_id, int col_index, int dest_reg = -1)
	{
		if (dest_reg == -1)
		{
			dest_reg = regs.allocate();
		}
		emit(COLUMN_MAKE(cursor_id, col_index, dest_reg));
		return dest_reg;
	}

	// Get multiple columns into contiguous registers
	int
	get_columns(int cursor_id, int start_col, int count, int first_dest_reg = -1)
	{
		if (first_dest_reg == -1)
		{
			first_dest_reg = regs.allocate_range(count);
		}

		for (int i = 0; i < count; i++)
		{
			emit(COLUMN_MAKE(cursor_id, start_col + i, first_dest_reg + i));
		}

		return first_dest_reg;
	}

	void
	insert_record(int cursor_id, int key_reg, int record_count = 1)
	{
		emit(INSERT_MAKE(cursor_id, key_reg, record_count));
		return;
		;
	}

	int
	delete_record(int cursor_id, int occurred_reg = -1, int valid_reg = -1)
	{
		if (occurred_reg == -1)
		{
			occurred_reg = regs.allocate();
		}
		if (valid_reg == -1)
		{
			valid_reg = regs.allocate();
		}
		emit(DELETE_MAKE(cursor_id, valid_reg, occurred_reg));
		return occurred_reg;
	}

	void
	update_record(int cursor_id, int record_reg)
	{
		emit(UPDATE_MAKE(cursor_id, record_reg));
		return;
		;
	}

	// ========================================================================
	// Result Output
	// ========================================================================

	void
	result(int first_reg, int reg_count = 1)
	{
		emit(RESULT_MAKE(first_reg, reg_count));
		return;
		;
	}

	// ========================================================================
	// Transaction Control
	// ========================================================================

	void
	begin_transaction()
	{
		emit(BEGIN_MAKE());
		return;
		;
	}

	void
	commit_transaction()
	{
		emit(COMMIT_MAKE());
		return;
		;
	}

	void
	rollback_transaction()
	{
		emit(ROLLBACK_MAKE());
		return;
		;
	}

	// ========================================================================
	// Function Calls
	// ========================================================================

	int
	call_function(VMFunction fn, int first_arg_reg, int arg_count, int result_reg = -1)
	{
		if (result_reg == -1)
		{
			result_reg = regs.allocate();
		}
		emit(FUNCTION_MAKE(result_reg, first_arg_reg, arg_count, (void *)fn));
		return result_reg;
	}

	// In ProgramBuilder (compile.hpp)
	int
	pack2(int left_reg, int right_reg, int dest_reg = -1)
	{
		if (dest_reg == -1)
		{
			dest_reg = regs.allocate();
		}
		emit(PACK2_MAKE(dest_reg, left_reg, right_reg));
		return dest_reg;
	}

	void
	unpack2(int src_reg, int first_dest_reg = -1)
	{
		if (first_dest_reg == -1)
		{
			first_dest_reg = regs.allocate_range(2);
		}
		emit(UNPACK2_MAKE(first_dest_reg, src_reg));
		return;
		;
	}
};

// ============================================================================
// Arena-allocated factory methods
// ============================================================================

// Allocate and initialize a value in the specified arena

static TypedValue
alloc(DataType type, const void *src = nullptr)
{
	uint32_t size = type_size(type);
	uint8_t *data = (uint8_t *)arena::alloc<query_arena>(size);

	if (src)
	{
		type_copy(type, data, (const uint8_t *)src);
	}
	else
	{
		type_zero(type, data);
	}

	return {data, type};
}

// Allocate scalar types
template <typename query_arena, typename T>
static TypedValue
alloc_scalar(DataType type, T value)
{
	static_assert(sizeof(T) <= 8, "Scalar too large");
	uint8_t *data = (uint8_t *)arena::alloc<query_arena>(sizeof(T));
	*(T *)data = value;
	return {data, type};
}

// Specialized allocators for common types

static TypedValue
alloc_u8(uint8_t val)
{
	return alloc_scalar<query_arena>(TYPE_U8, val);
}

static TypedValue
alloc_u16(uint16_t val)
{
	return alloc_scalar<query_arena>(TYPE_U16, val);
}

static TypedValue
alloc_u32(uint32_t val)
{
	return alloc_scalar<query_arena>(TYPE_U32, val);
}

static TypedValue
alloc_u64(uint64_t val)
{
	return alloc_scalar<query_arena>(TYPE_U64, val);
}

static TypedValue
alloc_i32(int32_t val)
{
	return alloc_scalar<query_arena>(TYPE_I32, val);
}

static TypedValue
alloc_i64(int64_t val)
{
	return alloc_scalar<query_arena>(TYPE_I64, val);
}

static TypedValue
alloc_f32(float val)
{
	return alloc_scalar<query_arena>(TYPE_F32, val);
}

static TypedValue
alloc_f64(double val)
{
	return alloc_scalar<query_arena>(TYPE_F64, val);
}

// String allocators - handles proper null termination and sizing

static TypedValue
alloc_char(const char *str, uint32_t size)
{

	// mem
	char *data = (char *)arena::alloc<query_arena>(size);
	if (str)
	{
		strncpy(data, str, size - 1);
	}
	return {(uint8_t *)data, make_char(size)};
}

static TypedValue
alloc_char8(const char *str)
{
	return alloc_char(str, 8);
}

static TypedValue
alloc_char16(const char *str)
{
	return alloc_char(str, 16);
}

static TypedValue
alloc_char32(const char *str)
{
	return alloc_char(str, 32);
}

static TypedValue
alloc_char64(const char *str)
{
	return alloc_char(str, 64);
}

static TypedValue
alloc_char128(const char *str)
{
	return alloc_char(str, 128);
}

static TypedValue
alloc_char256(const char *str)
{
	return alloc_char(str, 256);
}

// VARCHAR - dynamically sized

static TypedValue
alloc_varchar(const char *str, size_t size)
{
	size_t len;
	if (str)
	{
		if (size)
		{
			len = size;
		}
		else
		{
			len = strlen(str) + 1;
		}
	}
	else
	{
		len = 1;
	}

	char *data = (char *)arena::alloc<query_arena>(len);
	if (str)
	{
		strcpy(data, str);
	}
	else
	{
		data[0] = '\0';
	}
	return {(uint8_t *)data, TYPE_VARCHAR(len)};
}

// Null value

static TypedValue
alloc_null()
{
	return {nullptr, TYPE_NULL};
}

// Dual type allocator

static TypedValue
alloc_dual(DataType type1, const void *data1, DataType type2, const void *data2)
{
	DataType dual_type = make_dual(type1, type2);
	uint32_t total_size = type_size(dual_type);
	uint8_t *data = (uint8_t *)arena::alloc<query_arena>(total_size);

	pack_dual(data, type1, (const uint8_t *)data1, type2, (const uint8_t *)data2);
	return {data, dual_type};
}
