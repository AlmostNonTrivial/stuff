#include "compile.hpp"
#include "catalog.hpp"
#include "common.hpp"
#include "parser.hpp"
#include "vm.hpp"
#include <cstdint>

bool
vmfunc_create_structure(TypedValue *result, TypedValue *args, uint32_t arg_count)
{
	auto table_name = args->as_char();

	auto structure = catalog[table_name].to_layout();
	catalog[table_name].storage.btree = btree_create(structure.layout[0], structure.record_size, true);
	result->type = TYPE_U32;
	result->data = arena::alloc<query_arena>(sizeof(uint32_t));
	*(uint32_t *)result->data = catalog[table_name].storage.btree.root_page_index;
	return true;
}

// Updated version using SQL reconstruction
array<VMInstruction, query_arena>
compile_create_table_complete(Statement *stmt)
{
	ProgramBuilder	 prog;
	CreateTableStmt *create_stmt = stmt->create_table_stmt;

	prog.begin_transaction();

	// 1. Create the actual table structure
	int table_name_reg = prog.load(TYPE_CHAR16, prog.alloc_string(create_stmt->table_name.c_str(), 16));
	int root_page_reg = prog.call_function(vmfunc_create_structure, table_name_reg, 1);

	Structure &stru = catalog[MASTER_CATALOG];
	// 2. Open cursor to master catalog
	auto master_ctx = from_structure(stru);
	int	 master_cursor = prog.open_cursor(master_ctx);

	// 3. Prepare master catalog row in contiguous registers
	int row_start = prog.regs.allocate_range(5);

	// Load all the master catalog fields
	prog.load(alloc_u32(stru.next_key++), row_start);
	prog.load(TYPE_CHAR32, prog.alloc_string(create_stmt->table_name.c_str(), 32), row_start + 1);
	prog.load(TYPE_CHAR32, prog.alloc_string(create_stmt->table_name.c_str(), 32), row_start + 2);
	prog.move(root_page_reg, row_start + 3); // Root page from structure creation

	// Reconstruct the original SQL
	const char *sql = reconstruct_create_sql(create_stmt);
	prog.load(TYPE_CHAR256, prog.alloc_string(sql, 256), row_start + 4);

	// 4. Insert into master catalog
	prog.insert_record(master_cursor, row_start, 5);

	prog.close_cursor(master_cursor);
	prog.commit_transaction();
	prog.halt();

	return prog.instructions;
}


int
compile_literal(ProgramBuilder *prog, Expr *expr, DataType target_type);

// Compile expression with target type for coercion
int
compile_expression(ProgramBuilder *prog, Expr *expr, DataType target_type)
{
	switch (expr->type)
	{
	case EXPR_LITERAL:
		return compile_literal(prog, expr, target_type);

	case EXPR_NULL:
		return prog->load_null();

	// TODO: Handle other expression types
	default:
		return prog->load_null(); // Fallback
	}
}

// Helper to compile a row of values into contiguous registers
int
compile_value_row(ProgramBuilder *prog, array<Expr *, parser_arena> *value_row, InsertStmt *stmt)
{
	int start_reg = prog->regs.allocate_range(value_row->size);

	for (uint32_t i = 0; i < value_row->size; i++)
	{
		Expr	*expr = value_row->data[i];
		uint32_t table_col_idx = stmt->sem.column_indices[i];
		DataType target_type = stmt->sem.table->columns[table_col_idx].type;

		int value_reg = compile_expression(prog, expr, target_type);
		prog->move(value_reg, start_reg + i);
	}

	return start_reg;
}

// Compile single INSERT statement
array<VMInstruction, query_arena>
compile_insert(Statement *stmt)
{
	ProgramBuilder prog;
	InsertStmt	  *insert_stmt = stmt->insert_stmt;

	// Must be semantically resolved first
	if (!insert_stmt->sem.is_resolved)
	{
		prog.halt(1); // Error
		return prog.instructions;
	}

	prog.begin_transaction();

	// Open cursor to target table
	auto table_ctx = from_structure(*insert_stmt->sem.table);
	int	 cursor = prog.open_cursor(table_ctx);

	// Process each value row
	for (uint32_t row = 0; row < insert_stmt->values.size; row++)
	{
		prog.regs.push_scope();

		auto *value_row = insert_stmt->values[row];
		int	  start_reg = compile_value_row(&prog, value_row, insert_stmt);

		// Insert the record
		uint32_t value_count = value_row->size;
		prog.insert_record(cursor, start_reg, value_count);

		prog.regs.pop_scope();
	}

	prog.close_cursor(cursor);
	prog.commit_transaction();
	prog.halt();

	prog.resolve_labels();
	return prog.instructions;
}

// Optimized batch insert for large value sets
array<VMInstruction, query_arena>
compile_batch_insert(Statement *stmt)
{
	ProgramBuilder prog;
	InsertStmt	  *insert_stmt = stmt->insert_stmt;

	// Use same logic but with larger register allocation strategy
	// and potentially fewer transaction boundaries for very large batches

	prog.begin_transaction();

	auto table_ctx = from_structure(*insert_stmt->sem.table);
	int	 cursor = prog.open_cursor(table_ctx);

	// For large batches, process in chunks to avoid register exhaustion
	const uint32_t BATCH_SIZE = 100;

	for (uint32_t start = 0; start < insert_stmt->values.size; start += BATCH_SIZE)
	{
		uint32_t end = (start + BATCH_SIZE < insert_stmt->values.size) ? start + BATCH_SIZE : insert_stmt->values.size;

		for (uint32_t row = start; row < end; row++)
		{
			prog.regs.push_scope();

			auto *value_row = insert_stmt->values[row];
			int	  start_reg = compile_value_row(&prog, value_row, insert_stmt);
			prog.insert_record(cursor, start_reg, value_row->size);

			prog.regs.pop_scope();
		}
	}

	prog.close_cursor(cursor);
	prog.commit_transaction();
	prog.halt();

	prog.resolve_labels();
	return prog.instructions;
}

// ============================================================================
// NEW: SELECT compilation functions
// ============================================================================

// Also fix compile_literal to be more defensive
int
compile_literal(ProgramBuilder *prog, Expr *expr, DataType target_type)
{
	// First, ensure we have a valid literal type
	DataType lit_type = expr->lit_type;

	// If no target type specified, use the literal's own type
	if (target_type == TYPE_NULL)
	{
		target_type = lit_type;
	}

	// Handle exact match or compatible types
	if (lit_type == target_type || (type_is_numeric(lit_type) && type_is_numeric(target_type)))
	{

		// For numeric literals, handle based on target type
		if (type_is_numeric(target_type))
		{
			uint64_t val = (uint64_t)expr->int_val;

			switch (target_type)
			{
			case TYPE_U8:
				return prog->load(TYPE_U8, prog->alloc_value((uint8_t)val));
			case TYPE_U16:
				return prog->load(TYPE_U16, prog->alloc_value((uint16_t)val));
			case TYPE_U32:
				return prog->load(TYPE_U32, prog->alloc_value((uint32_t)val));
			case TYPE_U64:
				return prog->load(TYPE_U64, prog->alloc_value((uint64_t)val));
			case TYPE_I8:
				return prog->load(TYPE_I8, prog->alloc_value((int8_t)val));
			case TYPE_I16:
				return prog->load(TYPE_I16, prog->alloc_value((int16_t)val));
			case TYPE_I32:
				return prog->load(TYPE_I32, prog->alloc_value((int32_t)val));
			case TYPE_I64:
				return prog->load(TYPE_I64, prog->alloc_value((int64_t)val));
			case TYPE_F32:
				return prog->load(TYPE_F32, prog->alloc_value((float)expr->float_val));
			case TYPE_F64:
				return prog->load(TYPE_F64, prog->alloc_value((double)expr->float_val));
			default:
				// Fallback to U32 for unknown numeric
				return prog->load(TYPE_U32, prog->alloc_value((uint32_t)val));
			}
		}
	}

	// String literals
	if (type_is_string(lit_type) && type_is_string(target_type))
	{
		uint32_t target_size = type_size(target_type);
		return prog->load(target_type, prog->alloc_string(expr->str_val.c_str(), target_size));
	}

	// Last resort - try to load as-is
	if (lit_type != TYPE_NULL)
	{
		return compile_literal(prog, expr, lit_type);
	}

	return prog->load_null();
}

// And update compile_where_expr to be clearer
int
compile_where_expr(ProgramBuilder *prog, Expr *expr, int cursor_id)
{
	switch (expr->type)
	{
	case EXPR_COLUMN: {
		// Load column value from cursor
		return prog->get_column(cursor_id, expr->sem.column_index);
	}

	case EXPR_LITERAL: {
		// Use the resolved type from semantic analysis
		DataType target = expr->sem.resolved_type;
		if (target == TYPE_NULL)
		{
			target = expr->lit_type; // Fallback to parser type
		}
		return compile_literal(prog, expr, target);
	}

	case EXPR_BINARY_OP: {
		// Recursively compile operands
		int left_reg = compile_where_expr(prog, expr->left, cursor_id);
		int right_reg = compile_where_expr(prog, expr->right, cursor_id);

		// Generate operation
		switch (expr->op)
		{
		case OP_EQ:
			return prog->eq(left_reg, right_reg);
		case OP_NE:
			return prog->ne(left_reg, right_reg);
		case OP_LT:
			return prog->lt(left_reg, right_reg);
		case OP_LE:
			return prog->le(left_reg, right_reg);
		case OP_GT:
			return prog->gt(left_reg, right_reg);
		case OP_GE:
			return prog->ge(left_reg, right_reg);

		case OP_AND:
			return prog->logic_and(left_reg, right_reg);
		case OP_OR:
			return prog->logic_or(left_reg, right_reg);

		case OP_ADD:
			return prog->add(left_reg, right_reg);
		case OP_SUB:
			return prog->sub(left_reg, right_reg);
		case OP_MUL:
			return prog->mul(left_reg, right_reg);
		case OP_DIV:
			return prog->div(left_reg, right_reg);
		case OP_MOD:
			return prog->mod(left_reg, right_reg);

		default:
			return prog->load_null();
		}
	}

	case EXPR_UNARY_OP: {
		int operand_reg = compile_where_expr(prog, expr->operand, cursor_id);

		if (expr->unary_op == OP_NOT)
		{
			// For NOT, we need to invert the boolean value
			int one = prog->load(TYPE_U32, prog->alloc_value(1U));
			return prog->sub(one, operand_reg); // 1 - x inverts boolean
		}
		else if (expr->unary_op == OP_NEG)
		{
			// For negation, subtract from zero
			int zero = prog->load(TYPE_U32, prog->alloc_value(0U));
			return prog->sub(zero, operand_reg);
		}

		return operand_reg;
	}

	case EXPR_NULL: {
		return prog->load_null();
	}

	default:
		return prog->load_null();
	}
}

// Alternative simpler version using forward scan
array<VMInstruction, query_arena>
compile_delete_forward(Statement *stmt)
{
	ProgramBuilder prog;
	DeleteStmt	  *delete_stmt = stmt->delete_stmt;

	if (!delete_stmt->sem.is_resolved)
	{
		prog.halt(1);
		return prog.instructions;
	}

	prog.begin_transaction();

	auto table_ctx = from_structure(*delete_stmt->sem.table);
	int	 cursor = prog.open_cursor(table_ctx);

	// Forward scan - need to be careful about cursor invalidation
	int at_end = prog.first(cursor);

	auto scan_loop = prog.begin_while(at_end);
	{
		prog.regs.push_scope();

		// Remember if we should delete before actually deleting
		int should_delete = prog.regs.allocate();

		if (delete_stmt->where_clause)
		{
			should_delete = compile_where_expr(&prog, delete_stmt->where_clause, cursor);
		}
		else
		{
			// No WHERE clause - delete all
			prog.load(TYPE_U32, prog.alloc_value(1U), should_delete);
		}

		// Only delete if WHERE condition met
		auto delete_if = prog.begin_if(should_delete);
		{
			int deleted = prog.regs.allocate();
			int still_valid = prog.regs.allocate();
			prog.delete_record(cursor, deleted, still_valid);

			// After delete, check if cursor is still valid
			auto if_valid = prog.begin_if(still_valid);
			{
				// Cursor auto-advanced to next, update at_end flag
				prog.move(still_valid, at_end);
			}
			prog.begin_else(if_valid);
			{
				// Cursor invalid, try to reposition
				prog.first(cursor, at_end);
			}
			prog.end_if(if_valid);
		}
		prog.begin_else(delete_if);
		{
			// Not deleting, just move to next
			prog.next(cursor, at_end);
		}
		prog.end_if(delete_if);

		prog.regs.pop_scope();
	}
	prog.end_while(scan_loop);

	prog.close_cursor(cursor);
	prog.commit_transaction();
	prog.halt();

	prog.resolve_labels();
	return prog.instructions;
}

// compile_select_simple.cpp
// Simplified SELECT compilation - only *, columns, and DISTINCT

// Helper to compile aggregate function initialization
int compile_aggregate_init(ProgramBuilder *prog, Expr *expr)
{
    if (strcasecmp(expr->func_name.c_str(), "COUNT") == 0)
    {
        // COUNT starts at 0
        return prog->load(TYPE_U64, prog->alloc_value(0ULL));
    }
    else if (strcasecmp(expr->func_name.c_str(), "SUM") == 0)
    {
        // SUM starts at 0 (using appropriate type)
        DataType sum_type = expr->sem.resolved_type;
        if (sum_type == TYPE_I64)
            return prog->load(TYPE_I64, prog->alloc_value(0LL));
        else if (sum_type == TYPE_F64)
            return prog->load(TYPE_F64, prog->alloc_value(0.0));
        else
            return prog->load(TYPE_U32, prog->alloc_value(0U));
    }
    else if (strcasecmp(expr->func_name.c_str(), "MIN") == 0)
    {
        // MIN starts at NULL (will be replaced with first value)
        return prog->load_null();
    }
    else if (strcasecmp(expr->func_name.c_str(), "MAX") == 0)
    {
        // MAX starts at NULL (will be replaced with first value)
        return prog->load_null();
    }
    else if (strcasecmp(expr->func_name.c_str(), "AVG") == 0)
    {
        // AVG needs both sum and count - for now just return sum accumulator
        return prog->load(TYPE_F64, prog->alloc_value(0.0));
    }

    return prog->load_null();
}

// Helper to compile aggregate accumulation
void compile_aggregate_accumulate(ProgramBuilder *prog, Expr *expr, int acc_reg, int cursor_id, int count_reg = -1)
{
    if (strcasecmp(expr->func_name.c_str(), "COUNT") == 0)
    {
        // COUNT(*) - increment by 1
        int one = prog->load(TYPE_U64, prog->alloc_value(1ULL));
        prog->add(acc_reg, one, acc_reg);
    }
    else if (strcasecmp(expr->func_name.c_str(), "SUM") == 0)
    {
        // SUM(column) - add column value to accumulator
        if (expr->args.size > 0 && expr->args[0]->type == EXPR_COLUMN)
        {
            int value_reg = prog->get_column(cursor_id, expr->args[0]->sem.column_index);
            prog->add(acc_reg, value_reg, acc_reg);
        }
    }
    else if (strcasecmp(expr->func_name.c_str(), "MIN") == 0)
    {
        if (expr->args.size > 0 && expr->args[0]->type == EXPR_COLUMN)
        {
            int value_reg = prog->get_column(cursor_id, expr->args[0]->sem.column_index);

            // Check if accumulator is NULL (first value)
            int null_type = prog->load(TYPE_U64, prog->alloc_value((uint64_t)TYPE_NULL));
            int acc_type = prog->regs.allocate();
            prog->emit({OP_GetType, acc_type, acc_reg, 0, nullptr, 0});
            int is_null = prog->eq(acc_type, null_type);

            auto if_null = prog->begin_if(is_null);
            {
                // First value - just store it
                prog->move(value_reg, acc_reg);
            }
            prog->begin_else(if_null);
            {
                // Compare and keep minimum
                int is_less = prog->lt(value_reg, acc_reg);
                auto if_less = prog->begin_if(is_less);
                {
                    prog->move(value_reg, acc_reg);
                }
                prog->end_if(if_less);
            }
            prog->end_if(if_null);
        }
    }
    else if (strcasecmp(expr->func_name.c_str(), "MAX") == 0)
    {
        if (expr->args.size > 0 && expr->args[0]->type == EXPR_COLUMN)
        {
            int value_reg = prog->get_column(cursor_id, expr->args[0]->sem.column_index);

            // Check if accumulator is NULL (first value)
            int null_type = prog->load(TYPE_U64, prog->alloc_value((uint64_t)TYPE_NULL));
            int acc_type = prog->regs.allocate();
            prog->emit({OP_GetType, acc_type, acc_reg, 0, nullptr, 0});
            int is_null = prog->eq(acc_type, null_type);

            auto if_null = prog->begin_if(is_null);
            {
                // First value - just store it
                prog->move(value_reg, acc_reg);
            }
            prog->begin_else(if_null);
            {
                // Compare and keep maximum
                int is_greater = prog->gt(value_reg, acc_reg);
                auto if_greater = prog->begin_if(is_greater);
                {
                    prog->move(value_reg, acc_reg);
                }
                prog->end_if(if_greater);
            }
            prog->end_if(if_null);
        }
    }
    else if (strcasecmp(expr->func_name.c_str(), "AVG") == 0)
    {
        // AVG needs to accumulate sum and track count
        if (expr->args.size > 0 && expr->args[0]->type == EXPR_COLUMN)
        {
            int value_reg = prog->get_column(cursor_id, expr->args[0]->sem.column_index);
            // Convert to float and add
            prog->add(acc_reg, value_reg, acc_reg);
            // Increment count if we have a count register
            if (count_reg != -1)
            {
                int one = prog->load(TYPE_U64, prog->alloc_value(1ULL));
                prog->add(count_reg, one, count_reg);
            }
        }
    }
}

// Main compile_select function with aggregate support
array<VMInstruction, query_arena>
compile_select(Statement *stmt)
{
    ProgramBuilder prog;
    SelectStmt *select_stmt = stmt->select_stmt;

    // Must be semantically resolved first
    if (!select_stmt->sem.is_resolved)
    {
        prog.halt(1);
        return prog.instructions;
    }

    // Check if we have aggregates (no GROUP BY for now)
    bool has_aggregates = select_stmt->sem.has_aggregates;
    bool has_where = (select_stmt->where_clause != nullptr);

    // Single table for now
    if (!select_stmt->from_table || !select_stmt->from_table->sem.resolved)
    {
        prog.halt(1);
        return prog.instructions;
    }

    Structure *table = select_stmt->from_table->sem.resolved;
    auto table_ctx = from_structure(*table);
    int cursor = prog.open_cursor(table_ctx);

    if (has_aggregates)
    {
        // AGGREGATE PATH: Initialize accumulators, scan, then output final result

        // Initialize accumulator registers for each aggregate in SELECT list
        array<int, query_arena> accumulators;
        array<int, query_arena> count_regs; // For AVG functions

        for (uint32_t i = 0; i < select_stmt->select_list.size; i++)
        {
            Expr *expr = select_stmt->select_list[i];
            if (expr->type == EXPR_FUNCTION && expr->sem.is_aggregate)
            {
                int acc_reg = compile_aggregate_init(&prog, expr);
                accumulators.push(acc_reg);

                // For AVG, we need a separate count register
                if (strcasecmp(expr->func_name.c_str(), "AVG") == 0)
                {
                    int count_reg = prog.load(TYPE_U64, prog.alloc_value(0ULL));
                    count_regs.push(count_reg);
                }
                else
                {
                    count_regs.push(-1);
                }
            }
            else
            {
                // Non-aggregate in aggregate query - shouldn't happen without GROUP BY
                accumulators.push(-1);
                count_regs.push(-1);
            }
        }

        // Scan the table and accumulate
        int at_end = prog.first(cursor);
        auto scan_loop = prog.begin_while(at_end);
        {
            prog.regs.push_scope();

            // Check WHERE clause if present
            bool should_process = true;
            WhileContext where_ctx;
            if (has_where)
            {
                int where_result = compile_where_expr(&prog, select_stmt->where_clause, cursor);
                where_ctx = prog.begin_if(where_result);
                should_process = true;
            }

            if (should_process)
            {
                // Accumulate for each aggregate
                for (uint32_t i = 0; i < select_stmt->select_list.size; i++)
                {
                    Expr *expr = select_stmt->select_list[i];
                    if (expr->type == EXPR_FUNCTION && expr->sem.is_aggregate)
                    {
                        compile_aggregate_accumulate(&prog, expr, accumulators[i], cursor, count_regs[i]);
                    }
                }
            }

            if (has_where)
            {
                prog.end_if(where_ctx);
            }

            prog.regs.pop_scope();
            prog.next(cursor, at_end);
        }
        prog.end_while(scan_loop);

        // Finalize aggregates and output single result row
        int result_start = prog.regs.allocate_range(select_stmt->select_list.size);

        for (uint32_t i = 0; i < select_stmt->select_list.size; i++)
        {
            Expr *expr = select_stmt->select_list[i];
            if (expr->type == EXPR_FUNCTION && expr->sem.is_aggregate)
            {
                if (strcasecmp(expr->func_name.c_str(), "AVG") == 0 && count_regs[i] != -1)
                {
                    // AVG = sum / count
                    prog.div(accumulators[i], count_regs[i], result_start + i);
                }
                else
                {
                    // Other aggregates - just move the accumulator
                    prog.move(accumulators[i], result_start + i);
                }
            }
            else
            {
                // Non-aggregate - shouldn't happen without GROUP BY
                prog.load_null(result_start + i);
            }
        }

        prog.result(result_start, select_stmt->select_list.size);
    }
    else
    {
        // NON-AGGREGATE PATH: Output each row (existing logic)

        // Handle SELECT * expansion
        bool is_select_star = (select_stmt->select_list.size == 1 &&
                              select_stmt->select_list[0]->type == EXPR_STAR);

        // Scan and output rows
        int at_end = prog.first(cursor);
        auto scan_loop = prog.begin_while(at_end);
        {
            prog.regs.push_scope();

            // Check WHERE clause if present
            WhileContext where_ctx;
            if (has_where)
            {
                int where_result = compile_where_expr(&prog, select_stmt->where_clause, cursor);
                where_ctx = prog.begin_if(where_result);
            }

            // Project columns
            int result_start;
            int result_count;

            if (is_select_star)
            {
                // SELECT * - get all columns
                result_start = prog.get_columns(cursor, 0, table->columns.size);
                result_count = table->columns.size;
            }
            else
            {
                // Specific columns
                result_start = prog.regs.allocate_range(select_stmt->select_list.size);
                for (uint32_t i = 0; i < select_stmt->select_list.size; i++)
                {
                    Expr *expr = select_stmt->select_list[i];
                    if (expr->type == EXPR_NULL)
                    {
                        int null_reg = prog.load_null();
                        prog.move(null_reg, result_start + i);
                    }
                    else
                    {
                        // Get column value
                        int value_reg = prog.get_column(cursor, expr->sem.column_index);
                        prog.move(value_reg, result_start + i);
                    }
                }
                result_count = select_stmt->select_list.size;
            }

            // Emit result row
            prog.result(result_start, result_count);

            if (has_where)
            {
                prog.end_if(where_ctx);
            }

            prog.regs.pop_scope();
            prog.next(cursor, at_end);
        }
        prog.end_while(scan_loop);
    }

    prog.close_cursor(cursor);
    prog.halt();
    prog.resolve_labels();
    return prog.instructions;
}
// Compile any expression (not just WHERE expressions)
int
compile_expr(ProgramBuilder *prog, Expr *expr, int cursor_id)
{
	switch (expr->type)
	{
	case EXPR_COLUMN: {
		// Load column value from cursor
		return prog->get_column(cursor_id, expr->sem.column_index);
	}

	case EXPR_LITERAL: {
		// Use the resolved type from semantic analysis
		DataType target = expr->sem.resolved_type;
		if (target == TYPE_NULL)
		{
			target = expr->lit_type;
		}
		return compile_literal(prog, expr, target);
	}

	case EXPR_BINARY_OP: {
		// Recursively compile operands
		int left_reg = compile_expr(prog, expr->left, cursor_id);
		int right_reg = compile_expr(prog, expr->right, cursor_id);

		// Generate operation
		switch (expr->op)
		{
		// Comparison operators
		case OP_EQ:
			return prog->eq(left_reg, right_reg);
		case OP_NE:
			return prog->ne(left_reg, right_reg);
		case OP_LT:
			return prog->lt(left_reg, right_reg);
		case OP_LE:
			return prog->le(left_reg, right_reg);
		case OP_GT:
			return prog->gt(left_reg, right_reg);
		case OP_GE:
			return prog->ge(left_reg, right_reg);

		// Logical operators
		case OP_AND:
			return prog->logic_and(left_reg, right_reg);
		case OP_OR:
			return prog->logic_or(left_reg, right_reg);

		// Arithmetic operators
		case OP_ADD:
			return prog->add(left_reg, right_reg);
		case OP_SUB:
			return prog->sub(left_reg, right_reg);
		case OP_MUL:
			return prog->mul(left_reg, right_reg);
		case OP_DIV:
			return prog->div(left_reg, right_reg);
		case OP_MOD:
			return prog->mod(left_reg, right_reg);

		case OP_IN: {
			// Handle IN operator with list
			if (expr->right->type == EXPR_LIST)
			{
				// Start with false
				int result = prog->load(TYPE_U32, prog->alloc_value(0U));

				// Check each value in list
				for (uint32_t i = 0; i < expr->right->list_items.size; i++)
				{
					int item_reg = compile_expr(prog, expr->right->list_items[i], cursor_id);
					int match = prog->eq(left_reg, item_reg);
					result = prog->logic_or(result, match);
				}
				return result;
			}
			return prog->load_null();
		}

		case OP_LIKE: {
			// Call LIKE function if available
			// return prog->call_function(vmfunc_like, left_reg, 2);
		}

		default:
			return prog->load_null();
		}
	}

	case EXPR_UNARY_OP: {
		int operand_reg = compile_expr(prog, expr->operand, cursor_id);

		if (expr->unary_op == OP_NOT)
		{
			// For NOT, we need to invert the boolean value
			int one = prog->load(TYPE_U32, prog->alloc_value(1U));
			return prog->sub(one, operand_reg); // 1 - x inverts boolean
		}
		else if (expr->unary_op == OP_NEG)
		{
			// For negation, subtract from zero
			int zero = prog->load(TYPE_U32, prog->alloc_value(0U));
			return prog->sub(zero, operand_reg);
		}

		return operand_reg;
	}

	case EXPR_FUNCTION: {
		// Handle aggregate and scalar functions
		if (expr->sem.is_aggregate)
		{
			// Aggregate functions need special handling
			// For now, return a placeholder
			return prog->load_null();
		}
		else
		{
			// // Scalar functions
			// if (strcasecmp(expr->func_name.c_str(), "UPPER") == 0)
			// {
			//     int arg_reg = compile_expr(prog, expr->args[0], cursor_id);
			//     return prog->call_function(vmfunc_upper, arg_reg, 1);
			// }
			// else if (strcasecmp(expr->func_name.c_str(), "LOWER") == 0)
			// {
			//     int arg_reg = compile_expr(prog, expr->args[0], cursor_id);
			//     return prog->call_function(vmfunc_lower, arg_reg, 1);
			// }
			// else if (strcasecmp(expr->func_name.c_str(), "LENGTH") == 0)
			// {
			//     int arg_reg = compile_expr(prog, expr->args[0], cursor_id);
			//     return prog->call_function(vmfunc_length, arg_reg, 1);
			// }
			// Add more functions as needed
			return prog->load_null();
		}
	}

	case EXPR_NULL: {
		return prog->load_null();
	}

	case EXPR_STAR: {
		// Should not reach here for individual expression compilation
		// SELECT * is handled at a higher level
		return prog->load_null();
	}

	default:
		return prog->load_null();
	}
}

bool
vmfunc_drop_structure(TypedValue *result, TypedValue *args, uint32_t arg_count)
{
	if (arg_count != 2)
	{
		return false;
	}

	const char *name = args[0].as_char();
	uint32_t	is_table = args[1].as_u32();

	Structure *structure = catalog.get(name);
	if (!structure)
	{
		// Already gone (IF EXISTS case)
		result->type = TYPE_U32;
		result->data = arena::alloc<query_arena>(sizeof(uint32_t));
		*(uint32_t *)result->data = 1;
		return true;
	}

	// Clear the btree
	bt_clear(&structure->storage.btree);

	// If it's a table, also drop associated indexes
	string<catalog_arena> table_key;
	table_key.set(name);
	if (is_table)
	{
		string<catalog_arena> *index_name = table_to_index.get(table_key);
		if (index_name)
		{
			// Drop the index structure
			Structure *index = catalog.get(*index_name);
			if (index)
			{
				bt_clear(&index->storage.btree);
				catalog.remove(*index_name);
			}

			// Remove from mapping
			table_to_index.remove(table_key);
		}
	}

	// Remove from catalog

	catalog.remove(table_key);

	result->type = TYPE_U32;
	result->data = arena::alloc<query_arena>(sizeof(uint32_t));
	*(uint32_t *)result->data = 1;

	return true;
}



array<VMInstruction, query_arena>
compile_drop_table(Statement *stmt)
{
	ProgramBuilder prog;
	DropTableStmt *drop_stmt = stmt->drop_table_stmt;

	if (!drop_stmt->sem.is_resolved)
	{
		prog.halt(1);
		return prog.instructions;
	}

	if (!drop_stmt->sem.table)
	{
		prog.halt(0);
		return prog.instructions;
	}

	prog.begin_transaction();

	// First, check if table has any indexes to drop
	string<catalog_arena> table_key;
	table_key.set(drop_stmt->table_name);
	string<catalog_arena> *index_name = table_to_index.get(table_key);

	// Drop the table and its indexes (via callback)
	int name_reg = prog.load(TYPE_CHAR32, prog.alloc_string(drop_stmt->table_name.c_str(), 32));
	int is_table = prog.load(TYPE_U32, prog.alloc_value(1U));
	prog.call_function(vmfunc_drop_structure, name_reg, 2);

	// Delete table entry from master catalog
	Structure &master = catalog[MASTER_CATALOG];
	auto	   master_ctx = from_structure(master);
	int		   cursor = prog.open_cursor(master_ctx);

	// First pass - delete the table entry
	int	 at_end = prog.first(cursor);
	auto table_scan = prog.begin_while(at_end);
	{
		prog.regs.push_scope();

		int entry_name = prog.get_column(cursor, 1);
		int matches = prog.eq(entry_name, name_reg);

		auto delete_if = prog.begin_if(matches);
		{
			int deleted = prog.regs.allocate();
			int still_valid = prog.regs.allocate();
			prog.delete_record(cursor, deleted, still_valid);
			prog.goto_label("table_deleted");
		}
		prog.end_if(delete_if);

		prog.next(cursor, at_end);
		prog.regs.pop_scope();
	}
	prog.end_while(table_scan);

	prog.label("table_deleted");

	// Second pass - delete associated index entries if any
	if (index_name)
	{
		int index_name_reg = prog.load(TYPE_CHAR32, prog.alloc_string(index_name->c_str(), 32));

		// Rewind for second scan
		at_end = prog.first(cursor);
		auto index_scan = prog.begin_while(at_end);
		{
			prog.regs.push_scope();

			int entry_name = prog.get_column(cursor, 1);
			int matches = prog.eq(entry_name, index_name_reg);

			auto delete_if = prog.begin_if(matches);
			{
				int deleted = prog.regs.allocate();
				int still_valid = prog.regs.allocate();
				prog.delete_record(cursor, deleted, still_valid);
				prog.goto_label("index_deleted");
			}
			prog.end_if(delete_if);

			prog.next(cursor, at_end);
			prog.regs.pop_scope();
		}
		prog.end_while(index_scan);

		prog.label("index_deleted");
	}

	prog.close_cursor(cursor);
	prog.commit_transaction();
	prog.halt();

	prog.resolve_labels();
	return prog.instructions;
}

// Main dispatch function
array<VMInstruction, query_arena>
compile_program(Statement *stmt, bool inject_transaction)
{
	switch (stmt->type)
	{
	case STMT_SELECT:
		return compile_select(stmt);
	case STMT_CREATE_TABLE:
		return compile_create_table_complete(stmt);
	case STMT_CREATE_INDEX:
		return compile_create_index(stmt);
	case STMT_DROP_INDEX:
		return compile_drop_index(stmt);
	case STMT_DROP_TABLE:
		return compile_drop_table(stmt);
	case STMT_INSERT:
		return compile_insert(stmt);
	case STMT_DELETE:
		return compile_delete_forward(stmt);
	default:
		// Return empty program with error halt
		ProgramBuilder prog;
		prog.halt(1);
		return prog.instructions;
	}
}
