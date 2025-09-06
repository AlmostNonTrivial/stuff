#include "compile.hpp"
#include "catalog.hpp"
#include "common.hpp"
#include "parser.hpp"
#include "vm.hpp"

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
	int	 master_cursor = prog.open_cursor(&master_ctx);

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

array<VMInstruction, query_arena> compile_create_index(Statement *stmt) {
    ProgramBuilder prog;
    CreateIndexStmt *create_stmt = stmt->create_index_stmt;

    // Must be semantically resolved first
    if (!create_stmt->sem.is_resolved) {
        prog.halt(1);
        return prog.instructions;
    }

    prog.begin_transaction();

    // 1. Create the index structure
    int index_name_reg = prog.load(TYPE_CHAR32, prog.alloc_string(create_stmt->index_name.c_str(), 32));
    int root_page_reg = prog.call_function(vmfunc_create_structure, index_name_reg, 1);

    // 2. Add entry to master catalog
    Structure &master = catalog[MASTER_CATALOG];
    auto master_ctx = from_structure(master);
    int master_cursor = prog.open_cursor(&master_ctx);

    // Prepare master catalog row
    int row_start = prog.regs.allocate_range(5);

    prog.load(alloc_u32(master.next_key++), row_start);  // id
    prog.load(TYPE_CHAR32, prog.alloc_string(create_stmt->index_name.c_str(), 32), row_start + 1);  // name
    prog.load(TYPE_CHAR32, prog.alloc_string(create_stmt->table_name.c_str(), 32), row_start + 2);  // tbl_name
    prog.move(root_page_reg, row_start + 3);  // rootpage

    const char *sql = reconstruct_index_sql(create_stmt);
    prog.load(TYPE_CHAR256, prog.alloc_string(sql, 256), row_start + 4);  // sql

    prog.insert_record(master_cursor, row_start, 5);
    prog.close_cursor(master_cursor);

    // 3. Populate the index from the source table
    Structure *source_table = create_stmt->sem.table;
    Structure *index_structure = catalog.get(create_stmt->index_name);

    auto source_ctx = from_structure(*source_table);
    auto index_ctx = from_structure(*index_structure);

    int source_cursor = prog.open_cursor(&source_ctx);
    int index_cursor = prog.open_cursor(&index_ctx);

    {
        prog.regs.push_scope();

        int at_end = prog.first(source_cursor);
        auto scan_loop = prog.begin_while(at_end);
        {
            // Extract indexed columns and build key
            int key_reg;

            if (create_stmt->sem.column_indices.size == 1) {
                // Single column index
                key_reg = prog.get_column(source_cursor, create_stmt->sem.column_indices[0]);
            } else {
                // Composite index - need to pack columns
                int first_col = prog.get_column(source_cursor, create_stmt->sem.column_indices[0]);

                key_reg = first_col;
                for (uint32_t i = 1; i < create_stmt->sem.column_indices.size; i++) {
                    int next_col = prog.get_column(source_cursor, create_stmt->sem.column_indices[i]);
                    int packed = prog.pack2(key_reg, next_col);
                    key_reg = packed;
                }
            }

            // Insert into index (key only, no record data)
            prog.insert_record(index_cursor, key_reg, 0);

            prog.next(source_cursor, at_end);
        }
        prog.end_while(scan_loop);

        prog.regs.pop_scope();
    }

    prog.close_cursor(source_cursor);
    prog.close_cursor(index_cursor);
    prog.commit_transaction();
    prog.halt();

    prog.resolve_labels();
    return prog.instructions;
}


int compile_literal(ProgramBuilder *prog, Expr *expr, DataType target_type);

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
	int	 cursor = prog.open_cursor(&table_ctx);

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
array<VMInstruction, query_arena> compile_batch_insert(Statement* stmt) {
    ProgramBuilder prog;
    InsertStmt* insert_stmt = stmt->insert_stmt;

    // Use same logic but with larger register allocation strategy
    // and potentially fewer transaction boundaries for very large batches

    prog.begin_transaction();

    auto table_ctx = from_structure(*insert_stmt->sem.table);
    int cursor = prog.open_cursor(&table_ctx);

    // For large batches, process in chunks to avoid register exhaustion
    const uint32_t BATCH_SIZE = 100;

    for (uint32_t start = 0; start < insert_stmt->values.size; start += BATCH_SIZE) {
        uint32_t end = (start + BATCH_SIZE < insert_stmt->values.size) ?
                       start + BATCH_SIZE : insert_stmt->values.size;

        for (uint32_t row = start; row < end; row++) {
            prog.regs.push_scope();

            auto* value_row = insert_stmt->values[row];
            int start_reg = compile_value_row(&prog, value_row, insert_stmt);
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
int compile_literal(ProgramBuilder *prog, Expr *expr, DataType target_type) {
    // First, ensure we have a valid literal type
    DataType lit_type = expr->lit_type;

    // If no target type specified, use the literal's own type
    if (target_type == TYPE_NULL) {
        target_type = lit_type;
    }

    // Handle exact match or compatible types
    if (lit_type == target_type ||
        (type_is_numeric(lit_type) && type_is_numeric(target_type))) {

        // For numeric literals, handle based on target type
        if (type_is_numeric(target_type)) {
            uint64_t val = (uint64_t)expr->int_val;

            switch (target_type) {
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
    if (type_is_string(lit_type) && type_is_string(target_type)) {
        uint32_t target_size = type_size(target_type);
        return prog->load(target_type, prog->alloc_string(expr->str_val.c_str(), target_size));
    }

    // Last resort - try to load as-is
    if (lit_type != TYPE_NULL) {
        return compile_literal(prog, expr, lit_type);
    }

    return prog->load_null();
}

// And update compile_where_expr to be clearer
int compile_where_expr(ProgramBuilder* prog, Expr* expr, int cursor_id) {
    switch (expr->type) {
        case EXPR_COLUMN: {
            // Load column value from cursor
            return prog->get_column(cursor_id, expr->sem.column_index);
        }

        case EXPR_LITERAL: {
            // Use the resolved type from semantic analysis
            DataType target = expr->sem.resolved_type;
            if (target == TYPE_NULL) {
                target = expr->lit_type; // Fallback to parser type
            }
            return compile_literal(prog, expr, target);
        }

        case EXPR_BINARY_OP: {
            // Recursively compile operands
            int left_reg = compile_where_expr(prog, expr->left, cursor_id);
            int right_reg = compile_where_expr(prog, expr->right, cursor_id);

            // Generate operation
            switch (expr->op) {
                case OP_EQ:  return prog->eq(left_reg, right_reg);
                case OP_NE:  return prog->ne(left_reg, right_reg);
                case OP_LT:  return prog->lt(left_reg, right_reg);
                case OP_LE:  return prog->le(left_reg, right_reg);
                case OP_GT:  return prog->gt(left_reg, right_reg);
                case OP_GE:  return prog->ge(left_reg, right_reg);

                case OP_AND: return prog->logic_and(left_reg, right_reg);
                case OP_OR:  return prog->logic_or(left_reg, right_reg);

                case OP_ADD: return prog->add(left_reg, right_reg);
                case OP_SUB: return prog->sub(left_reg, right_reg);
                case OP_MUL: return prog->mul(left_reg, right_reg);
                case OP_DIV: return prog->div(left_reg, right_reg);
                case OP_MOD: return prog->mod(left_reg, right_reg);

                default:
                    return prog->load_null();
            }
        }

        case EXPR_UNARY_OP: {
            int operand_reg = compile_where_expr(prog, expr->operand, cursor_id);

            if (expr->unary_op == OP_NOT) {
                // For NOT, we need to invert the boolean value
                int one = prog->load(TYPE_U32, prog->alloc_value(1U));
                return prog->sub(one, operand_reg); // 1 - x inverts boolean
            } else if (expr->unary_op == OP_NEG) {
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

// Compile SELECT statement
array<VMInstruction, query_arena> compile_select(Statement* stmt) {
    ProgramBuilder prog;
    SelectStmt* select_stmt = stmt->select_stmt;

    // Must be semantically resolved first
    if (!select_stmt->sem.is_resolved) {
        prog.halt(1); // Error
        return prog.instructions;
    }

    Structure* table = select_stmt->from_table->sem.resolved;

    // Open cursor to table
    auto table_ctx = from_structure(*table);
    int cursor = prog.open_cursor(&table_ctx);

    // Begin scan
    int at_end = prog.first(cursor);
    auto scan_loop = prog.begin_while(at_end);
    {
        prog.regs.push_scope();

        // Apply WHERE filter if present
        bool has_where = (select_stmt->where_clause != nullptr);
        CondContext where_ctx;

        if (has_where) {
            int where_result = compile_where_expr(&prog, select_stmt->where_clause, cursor);
            where_ctx = prog.begin_if(where_result);
        }

        // Generate result based on SELECT list
        int result_start = -1;
        int result_count = 0;

        // Check if we have SELECT *
        bool is_select_star = (select_stmt->select_list.size == 1 &&
                              select_stmt->select_list[0]->type == EXPR_STAR);

        if (is_select_star) {
            // SELECT * - get all columns
            result_start = prog.get_columns(cursor, 0, table->columns.size);
            result_count = table->columns.size;
        } else {
            // Specific columns/expressions
            result_start = prog.regs.allocate_range(select_stmt->select_list.size);

            for (uint32_t i = 0; i < select_stmt->select_list.size; i++) {
                Expr* expr = select_stmt->select_list[i];
                int value_reg = -1;

                if (expr->type == EXPR_COLUMN) {
                    value_reg = prog.get_column(cursor, expr->sem.column_index);
                } else {
                    // Compile other expression types
                    value_reg = compile_where_expr(&prog, expr, cursor);
                }

                prog.move(value_reg, result_start + i);
            }
            result_count = select_stmt->select_list.size;
        }

        // Emit result row
        prog.result(result_start, result_count);

        if (has_where) {
            prog.end_if(where_ctx);
        }

        prog.regs.pop_scope();

        // Move to next row
        prog.next(cursor, at_end);
    }
    prog.end_while(scan_loop);

    prog.close_cursor(cursor);
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
	case STMT_INSERT:
		return compile_insert(stmt);
	default:
		// Return empty program with error halt
		ProgramBuilder prog;
		prog.halt(1);
		return prog.instructions;
	}
}
