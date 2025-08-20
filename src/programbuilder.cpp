// programbuilder.cpp - Simplified version
#include "programbuilder.hpp"
#include "arena.hpp"
#include "defs.hpp"
#include "schema.hpp"
#include "vm.hpp"
#include <cstring>

// Simple register allocator
struct RegisterAllocator {
    int next_reg = 0;

    int alloc() {
        return next_reg++;
    }

    void reset() {
        next_reg = 0;
    }
};

// Helper to load a value into a register
static void load_value(Vector<VMInstruction, QueryArena>& program,
                       const TypedValue& value, int reg) {
    if (value.type == TYPE_UINT32 || value.type == TYPE_UINT64) {
        uint32_t val = *(uint32_t*)value.data;
        program.push_back(Opcodes::Integer::create(reg, val));
    } else {
        program.push_back(Opcodes::String::create(reg, value.type, value.data));
    }
}

// Extract WHERE conditions from AST (simplified)
static Vector<WhereCondition, QueryArena>
extract_where_conditions(WhereNode* where, const char* table_name) {
    Vector<WhereCondition, QueryArena> conditions;
    if (!where || !where->condition) return conditions;

    // Simple traversal for AND-only conditions
    std::function<void(ASTNode*)> extract = [&](ASTNode* node) {
        if (!node) return;

        if (node->type == AST_BINARY_OP) {
            BinaryOpNode* binop = (BinaryOpNode*)node;

            if (binop->is_and) {
                extract(binop->left);
                extract(binop->right);
            } else {
                // This is a comparison
                WhereCondition cond;

                if (binop->left->type == AST_COLUMN_REF) {
                    ColumnRefNode* col = (ColumnRefNode*)binop->left;
                    cond.column_name = col->name;
                    cond.column_index = get_column_index(table_name, col->name);
                }

                if (binop->right->type == AST_LITERAL) {
                    LiteralNode* lit = (LiteralNode*)binop->right;
                    cond.value = lit->value;
                }

                cond.operator_type = binop->op;
                conditions.push_back(cond);
            }
        }
    };

    extract(where->condition);
    return conditions;
}

// Build WHERE condition checks (jump to skip_label if condition fails)
static void build_where_checks(Vector<VMInstruction, QueryArena>& program,
                              int cursor_id,
                              const Vector<WhereCondition, QueryArena>& conditions,
                              int skip_target,
                              RegisterAllocator& regs) {
    for (auto& cond : conditions) {
        int col_reg = regs.alloc();
        int val_reg = regs.alloc();

        // Load column value
        program.push_back(Opcodes::Column::create(cursor_id, cond.column_index, col_reg));

        // Load comparison value
        load_value(program, cond.value, val_reg);

        // Compare and jump if NOT matching (we want to skip non-matching rows)
        // Invert the operator for the jump
        CompareOp inverted_op;
        switch (cond.operator_type) {
            case EQ: inverted_op = NE; break;
            case NE: inverted_op = EQ; break;
            case LT: inverted_op = GE; break;
            case LE: inverted_op = GT; break;
            case GT: inverted_op = LE; break;
            case GE: inverted_op = LT; break;
            default: inverted_op = NE;
        }

        program.push_back(Opcodes::Compare::create(col_reg, val_reg, skip_target, inverted_op));
    }
}

// ============================================================================
// SELECT - Always full table scan
// ============================================================================
static Vector<VMInstruction, QueryArena>
build_select_from_ast(SelectNode* node) {
    Vector<VMInstruction, QueryArena> program;
    RegisterAllocator regs;

    const int cursor_id = 0;
    Table* table = get_table(node->table);
    if (!table) {
        program.push_back(Opcodes::Halt::create());
        return program;
    }

    // Open table for reading
    program.push_back(Opcodes::OpenRead::create(cursor_id, node->table));

    // Extract WHERE conditions
    auto conditions = extract_where_conditions(node->where, node->table);

    // Position to first record
    int empty_jump = program.size() + conditions.size() * 3 + 10; // Rough estimate
    program.push_back(Opcodes::First::create(cursor_id, empty_jump));

    // Loop start
    int loop_start = program.size();

    // Check WHERE conditions (skip to next_row if any fail)
    int next_row_target = loop_start + conditions.size() * 3 + table->schema.columns.size() + 3;
    build_where_checks(program, cursor_id, conditions, next_row_target, regs);

    // Output row - read all columns
    Vector<int, QueryArena> output_regs;
    for (size_t i = 0; i < table->schema.columns.size(); i++) {
        int reg = regs.alloc();
        output_regs.push_back(reg);
        program.push_back(Opcodes::Column::create(cursor_id, i, reg));
    }

    // Make record from registers and output it
    int record_reg = regs.alloc();
    program.push_back(Opcodes::MakeRecord::create(output_regs[0], output_regs.size(), record_reg));

    // Next row
    program.push_back(Opcodes::Next::create(cursor_id, loop_start));

    // Done
    program.push_back(Opcodes::Close::create(cursor_id));
    program.push_back(Opcodes::Halt::create());

    return program;
}

// ============================================================================
// INSERT - Insert into table and all indexes
// ============================================================================
static Vector<VMInstruction, QueryArena>
build_insert_from_ast(InsertNode* node) {
    Vector<VMInstruction, QueryArena> program;
    RegisterAllocator regs;

    Table* table = get_table(node->table);
    if (!table) {
        program.push_back(Opcodes::Halt::create());
        return program;
    }

    const int table_cursor = 0;
    int next_cursor = 1;

    // Open table for writing
    program.push_back(Opcodes::OpenWrite::create(table_cursor, node->table));

    // Open cursors for each index
    Vector<std::pair<int, uint32_t>, QueryArena> index_cursors; // cursor_id, column_index
    for (size_t i = 0; i < table->indexes.size(); i++) {
        uint32_t col_idx = *table->indexes.key_at(i);
        program.push_back(Opcodes::OpenWrite::create(next_cursor, node->table, col_idx));
        index_cursors.push_back({next_cursor, col_idx});
        next_cursor++;
    }

    // Load values into registers
    Vector<int, QueryArena> value_regs;
    for (size_t i = 0; i < node->values.size() && i < table->schema.columns.size(); i++) {
        if (node->values[i]->type == AST_LITERAL) {
            LiteralNode* lit = (LiteralNode*)node->values[i];
            int reg = regs.alloc();
            value_regs.push_back(reg);
            load_value(program, lit->value, reg);
        }
    }

    // Build record (excluding key which is at index 0)
    int record_reg = regs.alloc();
    if (value_regs.size() > 1) {
        program.push_back(Opcodes::MakeRecord::create(value_regs[1], value_regs.size() - 1, record_reg));
    }

    // Insert into table
    program.push_back(Opcodes::Insert::create(table_cursor, value_regs[0], record_reg));

    // Insert into indexes
    for (auto& idx : index_cursors) {
        int cursor = idx.first;
        uint32_t col_idx = idx.second;

        // Find the register containing this column's value
        if (col_idx < value_regs.size()) {
            program.push_back(Opcodes::Insert::create(cursor, value_regs[col_idx], value_regs[0]));
        }
    }

    // Close all cursors
    program.push_back(Opcodes::Close::create(table_cursor));
    for (auto& idx : index_cursors) {
        program.push_back(Opcodes::Close::create(idx.first));
    }

    program.push_back(Opcodes::Halt::create());
    return program;
}

// ============================================================================
// DELETE - Only delete from table, leave indexes alone
// ============================================================================
static Vector<VMInstruction, QueryArena>
build_delete_from_ast(DeleteNode* node) {
    Vector<VMInstruction, QueryArena> program;
    RegisterAllocator regs;

    const int cursor_id = 0;

    // Open table for writing
    program.push_back(Opcodes::OpenWrite::create(cursor_id, node->table));

    // Extract WHERE conditions
    auto conditions = extract_where_conditions(node->where, node->table);

    // Position to first record
    int empty_jump = program.size() + conditions.size() * 3 + 5;
    program.push_back(Opcodes::First::create(cursor_id, empty_jump));

    // Loop start
    int loop_start = program.size();

    // Check WHERE conditions
    int next_row_target = loop_start + conditions.size() * 3 + 2;
    build_where_checks(program, cursor_id, conditions, next_row_target, regs);

    // Delete the record (from table only)
    program.push_back(Opcodes::Delete::create(cursor_id));

    // Next row
    program.push_back(Opcodes::Next::create(cursor_id, loop_start));

    // Done
    program.push_back(Opcodes::Close::create(cursor_id));
    program.push_back(Opcodes::Halt::create());

    return program;
}

// ============================================================================
// UPDATE - Update table, insert new entries to affected indexes
// ============================================================================
static Vector<VMInstruction, QueryArena>
build_update_from_ast(UpdateNode* node) {
    Vector<VMInstruction, QueryArena> program;
    RegisterAllocator regs;

    Table* table = get_table(node->table);
    if (!table) {
        program.push_back(Opcodes::Halt::create());
        return program;
    }

    const int table_cursor = 0;
    int next_cursor = 1;

    // Open table for writing
    program.push_back(Opcodes::OpenWrite::create(table_cursor, node->table));

    // Figure out which columns are being updated
    Set<uint32_t, QueryArena> updated_columns;
    for (auto& set_clause : node->set_clauses) {
        uint32_t col_idx = get_column_index(node->table, set_clause->column);
        updated_columns.insert(col_idx);
    }

    // Open index cursors for columns being updated
    Vector<std::pair<int, uint32_t>, QueryArena> index_cursors;
    for (size_t i = 0; i < table->indexes.size(); i++) {
        uint32_t col_idx = *table->indexes.key_at(i);
        if (updated_columns.contains(col_idx)) {
            program.push_back(Opcodes::OpenWrite::create(next_cursor, node->table, col_idx));
            index_cursors.push_back({next_cursor, col_idx});
            next_cursor++;
        }
    }

    // Extract WHERE conditions
    auto conditions = extract_where_conditions(node->where, node->table);

    // Position to first record
    int empty_jump = program.size() + 100; // Rough estimate
    program.push_back(Opcodes::First::create(table_cursor, empty_jump));

    // Loop start
    int loop_start = program.size();

    // Check WHERE conditions
    int next_row_target = loop_start + 50; // Rough estimate
    build_where_checks(program, table_cursor, conditions, next_row_target, regs);

    // Read all current column values
    Vector<int, QueryArena> current_regs;
    for (size_t i = 0; i < table->schema.columns.size(); i++) {
        int reg = regs.alloc();
        current_regs.push_back(reg);
        program.push_back(Opcodes::Column::create(table_cursor, i, reg));
    }

    // Apply SET clauses
    for (auto& set_clause : node->set_clauses) {
        uint32_t col_idx = get_column_index(node->table, set_clause->column);
        if (set_clause->value->type == AST_LITERAL) {
            LiteralNode* lit = (LiteralNode*)set_clause->value;
            load_value(program, lit->value, current_regs[col_idx]);
        }
    }

    // Build new record (excluding key)
    int record_reg = regs.alloc();
    program.push_back(Opcodes::MakeRecord::create(current_regs[1], current_regs.size() - 1, record_reg));

    // Update the table record
    program.push_back(Opcodes::Update::create(table_cursor, record_reg));

    // Insert new entries into affected indexes (old entries remain)
    for (auto& idx : index_cursors) {
        int cursor = idx.first;
        uint32_t col_idx = idx.second;
        program.push_back(Opcodes::Insert::create(cursor, current_regs[col_idx], current_regs[0]));
    }

    // Next row
    program.push_back(Opcodes::Next::create(table_cursor, loop_start));

    // Close all cursors
    program.push_back(Opcodes::Close::create(table_cursor));
    for (auto& idx : index_cursors) {
        program.push_back(Opcodes::Close::create(idx.first));
    }

    program.push_back(Opcodes::Halt::create());
    return program;
}

// ============================================================================
// Schema operations
// ============================================================================
static Vector<VMInstruction, QueryArena>
build_create_table_from_ast(CreateTableNode* node) {
    TableSchema* schema = (TableSchema*)arena::alloc<QueryArena>(sizeof(TableSchema));
    schema->table_name = node->table;
    schema->columns.set(node->columns);

    Vector<VMInstruction, QueryArena> program;
    program.push_back(Opcodes::CreateTable::create(schema));
    program.push_back(Opcodes::Halt::create());
    return program;
}

static Vector<VMInstruction, QueryArena>
build_create_index_from_ast(CreateIndexNode* node) {
    Vector<VMInstruction, QueryArena> program;
    RegisterAllocator regs;

    Table* table = get_table(node->table);
    if (!table) {
        program.push_back(Opcodes::Halt::create());
        return program;
    }

    uint32_t col_idx = get_column_index(node->table, node->column);

    const int table_cursor = 0;
    const int index_cursor = 1;

    // Create the index
    program.push_back(Opcodes::CreateIndex::create(col_idx, node->table));

    // Populate it from existing data
    program.push_back(Opcodes::OpenRead::create(table_cursor, node->table));
    program.push_back(Opcodes::OpenWrite::create(index_cursor, node->table, col_idx));

    // Scan table
    int empty_jump = program.size() + 7;
    program.push_back(Opcodes::First::create(table_cursor, empty_jump));

    int loop_start = program.size();

    // Get key and column value
    int key_reg = regs.alloc();
    int col_reg = regs.alloc();
    program.push_back(Opcodes::Column::create(table_cursor, 0, key_reg));
    program.push_back(Opcodes::Column::create(table_cursor, col_idx, col_reg));

    // Insert into index
    program.push_back(Opcodes::Insert::create(index_cursor, col_reg, key_reg));

    // Next
    program.push_back(Opcodes::Next::create(table_cursor, loop_start));

    program.push_back(Opcodes::Close::create(table_cursor));
    program.push_back(Opcodes::Close::create(index_cursor));
    program.push_back(Opcodes::Halt::create());

    return program;
}

// Transaction operations
static Vector<VMInstruction, QueryArena> build_begin_from_ast(BeginNode* node) {
    Vector<VMInstruction, QueryArena> program;
    program.push_back(Opcodes::Begin::create());
    program.push_back(Opcodes::Halt::create());
    return program;
}

static Vector<VMInstruction, QueryArena> build_commit_from_ast(CommitNode* node) {
    Vector<VMInstruction, QueryArena> program;
    program.push_back(Opcodes::Commit::create());
    program.push_back(Opcodes::Halt::create());
    return program;
}

static Vector<VMInstruction, QueryArena> build_rollback_from_ast(RollbackNode* node) {
    Vector<VMInstruction, QueryArena> program;
    program.push_back(Opcodes::Rollback::create());
    program.push_back(Opcodes::Halt::create());
    return program;
}

// ============================================================================
// Main entry point
// ============================================================================
Vector<VMInstruction, QueryArena> build_from_ast(ASTNode* ast) {
    if (!ast) {
        Vector<VMInstruction, QueryArena> program;
        program.push_back(Opcodes::Halt::create());
        return program;
    }

    switch (ast->type) {
        case AST_SELECT: return build_select_from_ast((SelectNode*)ast);
        case AST_INSERT: return build_insert_from_ast((InsertNode*)ast);
        case AST_UPDATE: return build_update_from_ast((UpdateNode*)ast);
        case AST_DELETE: return build_delete_from_ast((DeleteNode*)ast);
        case AST_CREATE_TABLE: return build_create_table_from_ast((CreateTableNode*)ast);
        case AST_CREATE_INDEX: return build_create_index_from_ast((CreateIndexNode*)ast);
        case AST_BEGIN: return build_begin_from_ast((BeginNode*)ast);
        case AST_COMMIT: return build_commit_from_ast((CommitNode*)ast);
        case AST_ROLLBACK: return build_rollback_from_ast((RollbackNode*)ast);
        default: {
            Vector<VMInstruction, QueryArena> program;
            program.push_back(Opcodes::Halt::create());
            return program;
        }
    }
}
