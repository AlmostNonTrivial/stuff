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
#include <type_traits>
/*
 * The VM can have n cursors open at a time
 * we allocate a lot for them with a simple pool allocator
 */
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
		// no op
	}
};

/*
 * Registers are where we store values that we want to operate
 * upon, for example
 *
 * SELECT WHERE id > 5; will load said column into a register,
 * 5 in another and do OP_Compare outputting the result in another
 * register, which we could then do OP_Test
 *
 * If we then wanted to output the row we would load the remaining columns
 * in contigous registers and output them.
 *
 * The processing we do naturally uses loops (for some set of rows, do x)
 * so to not run out of registers we have a simple scope based system.
 *
 * When we begin loop, we put a scope, and then pop it.
 *
 *
 * For example: SELECT id, email FROM users WHERE id > 5;
 *
 * Load 5 into register 0,
 * go to the beginning of the tree
 * begin the loop and push the scope
 * load id into register 2,
 * test register 0 and 2, put the result in 1,
 * if true, load email into register 3,
 * call result, outputting registers 2 and 3,
 * pop the scope, now registers 2 and 3 are availble
 *
 *
 *
 *
 *

 */

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
};

/*
 *
 * Try to encapulate common patterns
 * like conditionally iterating through a table
 * */

struct while_context
{
	uint32_t loop_label;
	uint32_t end_label;
	int		 condition_reg;
	int		 saved_reg_mark;
};

struct conditional_context
{
	uint32_t else_label;
	uint32_t end_label;
	int		 saved_reg_mark;
	bool	 has_else;
};

struct program_builder
{
	array<vm_instruction, query_arena> instructions;

	// Label resolution - each label tracks where it points and what needs patching
	struct label_info
	{
		int32_t						 pc;		 // -1 if not yet defined
		array<uint32_t, query_arena> patch_list; // Instruction indices that jump here
	};

	array<label_info, query_arena> labels;

	register_allocator regs;
	cursor_allocator   cursors;

	void
	emit(vm_instruction inst)
	{
		instructions.push(inst);
	}
	// Create a new label that can be jumped to before it's defined
	uint32_t
	create_label()
	{
		labels.push({-1, {}});
		return labels.size() - 1;
	}

	// Define where a label actually points
	void
	define_label(uint32_t label_id)
	{
		assert(label_id < labels.size() && "Invalid label ID");
		label_info &label = labels[label_id];
		assert(label.pc == -1 && "Label already defined");

		label.pc = instructions.size();

		// Immediately patch all forward references
		for (uint32_t inst_idx : label.patch_list)
		{
			vm_instruction &inst = instructions[inst_idx];
			// Check both p2 and p3 for jump targets (JUMPIF uses both)
			if (inst.p2 == -1)
			{
				inst.p2 = label.pc;
			}
			if (inst.p3 == -1)
			{
				inst.p3 = label.pc;
			}
		}
		label.patch_list.clear();
	}

	// Jump to a label (may be forward reference)
	void
	jump_to(uint32_t label_id)
	{
		assert(label_id < labels.size() && "Invalid label ID");
		label_info &label = labels[label_id];

		if (label.pc >= 0)
		{
			// Label already defined, emit direct jump
			emit(GOTO_MAKE(label.pc));
		}
		else
		{
			// Forward reference - emit placeholder and record for patching
			label.patch_list.push(instructions.size());
			emit(GOTO_MAKE(-1));
		}
	}

	// Conditional jump to a label
	void
	jumpif(int test_reg, uint32_t label_id, bool jump_if_true)
	{
		assert(label_id < labels.size() && "Invalid label ID");
		label_info &label = labels[label_id];

		if (label.pc >= 0)
		{
			// Label already defined
			emit(JUMPIF_MAKE(test_reg, label.pc, jump_if_true));
		}
		else
		{
			// Forward reference
			label.patch_list.push(instructions.size());
			emit(JUMPIF_MAKE(test_reg, -1, jump_if_true));
		}
	}
	void
	halt(int exit_code = 0)
	{
		emit(HALT_MAKE(exit_code));
	}

	while_context
	begin_while(int condition_reg)
	{
		uint32_t loop_label = create_label();
		uint32_t end_label = create_label();

		define_label(loop_label);				 // Loop starts here
		jumpif(condition_reg, end_label, false); // Jump to end if condition false

		return {loop_label, end_label, condition_reg, regs.mark()};
	}

	void
	end_while(const while_context &ctx)
	{
		jump_to(ctx.loop_label);	 // Jump back to loop start
		define_label(ctx.end_label); // End is here
		regs.restore(ctx.saved_reg_mark);
	}

	conditional_context
	begin_if(int test_reg)
	{
		uint32_t else_label = create_label();
		uint32_t end_label = create_label();

		jumpif(test_reg, else_label, false); // Jump to else if false

		return {else_label, end_label, regs.mark(), false};
	}

	void
	begin_else(conditional_context &ctx)
	{
		jump_to(ctx.end_label);		  // Jump over else block
		define_label(ctx.else_label); // Else starts here
		ctx.has_else = true;
	}

	void
	end_if(const conditional_context &ctx)
	{
		if (!ctx.has_else)
		{
			define_label(ctx.else_label); // No else, so else label is here
		}
		define_label(ctx.end_label);
		regs.restore(ctx.saved_reg_mark);
	}

	void
	goto_label(uint32_t label_id)
	{
		jump_to(label_id);
	}

	uint32_t done_label = 0;

	void
	goto_label(const char *name)
	{
		if (strcmp(name, "done") == 0)
		{
			if (done_label == 0)
			{
				done_label = create_label();
			}
			jump_to(done_label);
		}
	}

	uint32_t
	label(const char *name = nullptr)
	{
		uint32_t id = create_label();
		define_label(id);
		return id;
	}
	int
	load_string(data_type type, const void *src, size_t src_len = 0, int dest_reg = -1)
	{
		assert(type_is_string(type));
		uint32_t size = type_size(type);

		size_t len = src_len ? src_len : strlen((char *)src);
		assert(len <= size && "String literal too long for column type");

		void *ptr = arena<query_arena>::alloc(size); // Allocate full type size
		memcpy(ptr, src, len);
		((char *)ptr)[len] = '\0';

		if (dest_reg == -1)
		{
			dest_reg = regs.allocate();
		}
		emit(LOAD_MAKE(dest_reg, (int64_t)type, ptr));
		return dest_reg;
	}

	template <typename T>
	int
	load(data_type type, const T &value, int dest_reg = -1)
	{
        static_assert(!std::is_pointer<T>());
		assert(!type_is_string(type));
		assert(sizeof(T) == type_size(type));

		if (dest_reg == -1)
		{
			dest_reg = regs.allocate();
		}

		T *ptr = (T *)arena<query_arena>::alloc(sizeof(T));
		memcpy(ptr, &value, sizeof(T));

		emit(LOAD_MAKE(dest_reg, (int64_t)type, ptr)); // Fixed: was just 'type'
		return dest_reg;
	}


	int
	load_ptr(void * ptr, int dest_reg = -1)
	{


		if (dest_reg == -1)
		{
			dest_reg = regs.allocate();
		}


		emit(LOAD_MAKE(dest_reg, (int64_t)TYPE_I64, ptr)); // Fixed: was just 'type'
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

	// Special handling for demos that use string labels
	void
	jumpif_true(int test_reg, const char *label_name)
	{
		if (strcmp(label_name, "done") == 0)
		{
			if (done_label == 0)
			{
				done_label = create_label();
			}
			jumpif(test_reg, done_label, true);
		}
	}

	void
	jumpif_zero(int test_reg, const char *label_name)
	{
		if (strcmp(label_name, "done") == 0)
		{
			if (done_label == 0)
			{
				done_label = create_label();
			}
			jumpif(test_reg, done_label, false);
		}
	}
};

cursor_context *
btree_cursor_from_relation(relation &structure);

cursor_context *
red_black_cursor_from_format(tuple_format &layout, bool allow_duplicates = true);

array<vm_instruction, query_arena>
compile_program(stmt_node *stmt, bool inject_transaction);

void
load_catalog_from_master();
