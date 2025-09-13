#pragma once
#include "catalog.hpp"
#include "parser.hpp"
#include "types.hpp"
#include "common.hpp"
#include "arena.hpp"
#include "containers.hpp"
#include "vm.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>

struct cursor_allocator
{
	uint32_t				next_cursor = 0;
	array<int, query_arena> free_list;

	int
	allocate()
	{
		if (free_list.size() > 0)
		{
			return free_list.pop_value();
		}
		return next_cursor++;
	}

	void
	free(int cursor_id)
	{
	}
};

struct register_allocator
{
	int						next_free = 0;
	array<int, query_arena> scope_stack;

	int
	allocate(int specific = -1)
	{
		if (specific >= 0)
		{
			assert(specific < REGISTERS && "Register out of range");

			assert(specific >= next_free && "Cannot allocate already-used register");
			next_free = specific + 1;
			return specific;
		}

		assert(next_free < REGISTERS && "Out of registers");
		return next_free++;
	}

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

	void
	reserve(int count)
	{
		assert(next_free + count <= REGISTERS && "Not enough registers to reserve");
		next_free += count;
	}

	void
	push_scope()
	{
		scope_stack.push(next_free);
	}

	void
	pop_scope()
	{
		assert(scope_stack.size() > 0 && "No scope to pop");
		next_free = scope_stack.pop_value();
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
		scope_stack.clear();
	}
};

struct loop_context
{
	const char *start_label;
	const char *end_label;
	int			saved_reg_mark;
};

struct while_context
{
	string_view condition_label;
	string_view end_label;
	int			condition_reg;
	int			saved_reg_mark;
};

struct conditional_context
{
	const char *else_label;
	const char *end_label;
	int			saved_reg_mark;
	bool		has_else;
};

struct program_builder
{
	array<vm_instruction, query_arena>			 instructions;
	hash_map<string_view, uint32_t, query_arena> labels;
	array<uint32_t, query_arena>				 unresolved_jumps;
	register_allocator							 regs;
	cursor_allocator							 cursors;
	int											 label_counter = 0;

	loop_context current_loop;

	template <typename T>
	T *
	alloc(const T &value)
	{
		T *ptr = (T *)arena<query_arena>::alloc(sizeof(T));
		memcpy(ptr, &value, sizeof(T));
		return ptr;
	}

	typed_value
	alloc_data_type(data_type type, const void *src, size_t src_len = 0)
	{

		assert(src);
		typed_value tv;
		tv.type = type;
		uint32_t size = type_size(type);
		void	*ptr = arena<query_arena>::alloc(size);

		if (type_is_string(type))
		{

			size_t len = src_len ? src_len : strlen((char *)src);
			assert(len <= size && "String literal too long for column type");
			memcpy(ptr, src, len);
			((char *)ptr)[len] = '\0';
		}
		else
		{
			memcpy(ptr, src, size);
		}

		tv.data = ptr;
		return tv;
	}

	void
	emit(vm_instruction inst)
	{
		instructions.push(inst);

		if (inst.p2 == -1 || inst.p3 == -1)
		{
			unresolved_jumps.push(instructions.size() - 1);
		}
	}

	void
	label(const char *name)
	{
		labels.insert(arena_intern<query_arena>(name), instructions.size());
	}

	const char *
	generate_label(const char *prefix = "L")
	{
		char name[32];
		snprintf(name, 32, "%s%d", prefix, label_counter++);
		return arena_intern<query_arena>(name).data();
	}

	int
	here() const
	{
		return instructions.size();
	}

	void
	resolve_labels()
	{
		for (uint32_t i = 0; i < unresolved_jumps.size(); i++)
		{
			uint32_t		inst_idx = unresolved_jumps[i];
			vm_instruction &inst = instructions[inst_idx];

			if (inst.p4)
			{
				const char *label_name = (const char *)inst.p4;

				uint32_t *target = labels.get(label_name);

				if (target)
				{
					if (inst.p2 == -1)
					{
						inst.p2 = *target;
					}

					if (inst.p3 == -1)
					{
						inst.p3 = *target;
					}
				}
				inst.p4 = nullptr;
			}
		}
	}

	void
	goto_label(const char *label_name)
	{
		emit(GOTO_MAKE((void *)label_name));
	}

	void
	halt(int exit_code = 0)
	{
		emit(HALT_MAKE(exit_code));
	}

	void
	jumpif_true(int test_reg, const char *label)
	{
		emit(JUMPIF_MAKE(test_reg, (void *)label, true));
	}

	void
	jumpif_false(int test_reg, const char *label)
	{
		emit(JUMPIF_MAKE(test_reg, (void *)label, false));
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

	loop_context
	begin_loop(const char *name = nullptr)
	{
		if (!name)
			name = generate_label("loop");

		loop_context ctx = {name, generate_label("end"), regs.mark()};

		label(ctx.start_label);
		current_loop = ctx;
		return ctx;
	}

	void
	end_loop(const loop_context &ctx)
	{
		goto_label(ctx.start_label);
		label(ctx.end_label);
		regs.restore(ctx.saved_reg_mark);
	}

	void
	break_loop()
	{
		goto_label(current_loop.end_label);
	}

	void
	continue_loop()
	{
		goto_label(current_loop.start_label);
	}

	while_context
	begin_while(int condition_reg)
	{
		while_context ctx = {generate_label("while_check"), generate_label("while_end"), condition_reg, regs.mark()};

		label(ctx.condition_label.data());
		jumpif_zero(condition_reg, ctx.end_label.data());
		return ctx;
	}

	void
	end_while(const while_context &ctx)
	{
		goto_label(ctx.condition_label.data());
		label(ctx.end_label.data());
		regs.restore(ctx.saved_reg_mark);
	}

	while_context
	begin_do()
	{
		while_context ctx = {generate_label("do_start"), generate_label("do_end"), -1, regs.mark()};

		label(ctx.condition_label.data());
		return ctx;
	}

	void
	end_while_condition(const while_context &ctx, int condition_reg)
	{
		jumpif_not_zero(condition_reg, ctx.condition_label.data());
		label(ctx.end_label.data());
		regs.restore(ctx.saved_reg_mark);
	}

	conditional_context
	begin_if(int test_reg)
	{
		conditional_context ctx = {generate_label("else"), generate_label("endif"), regs.mark(), false};

		jumpif_false(test_reg, ctx.else_label);
		return ctx;
	}

	void
	begin_else(conditional_context &ctx)
	{
		goto_label(ctx.end_label);
		label(ctx.else_label);
		ctx.has_else = true;
	}

	void
	end_if(const conditional_context &ctx)
	{
		if (!ctx.has_else)
		{
			label(ctx.else_label);
		}
		label(ctx.end_label);
		regs.restore(ctx.saved_reg_mark);
	}

	int
	load(const typed_value &value, int dest_reg = -1)
	{
		if (dest_reg == -1)
		{
			dest_reg = regs.allocate();
		}
		emit(LOAD_MAKE(dest_reg, (int64_t)value.type, value.data));
		return dest_reg;
	}

	int
	load(data_type type, void *value, int dest_reg = -1)
	{
		if (dest_reg == -1)
		{
			dest_reg = regs.allocate();
		}
		emit(LOAD_MAKE(dest_reg, (int64_t)type, value));
		return dest_reg;
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

	int
	open_cursor(cursor_context *context)
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
		return rewind(cursor_id, true, result_reg);
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
	}

	void
	result(int first_reg, int reg_count = 1)
	{
		emit(RESULT_MAKE(first_reg, reg_count));
	}

	void
	begin_transaction()
	{
		emit(BEGIN_MAKE());
	}

	void
	commit_transaction()
	{
		emit(COMMIT_MAKE());
	}

	void
	rollback_transaction()
	{
		emit(ROLLBACK_MAKE());
	}

	int
	call_function(vm_function fn, int first_arg_reg, int arg_count, int result_reg = -1)
	{
		if (result_reg == -1)
		{
			result_reg = regs.allocate();
		}
		emit(FUNCTION_MAKE(result_reg, first_arg_reg, arg_count, (void *)fn));
		return result_reg;
	}

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
	}
};

cursor_context *
cursor_from_relation(relation &structure);

cursor_context *
red_black(tuple_format &layout, bool allow_duplicates = true);

array<vm_instruction, query_arena>
compile_program(stmt_node *stmt, bool inject_transaction);

void
load_catalog_from_master();
