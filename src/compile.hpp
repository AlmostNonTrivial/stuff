#pragma  once
#include "vm.hpp"
#include "catalog.hpp"
// Enhanced ProgramBuilder with inline implementations
struct RegisterAllocator {
    int next_free = 0;
    array<int, QueryArena> scope_stack;

    int allocate() {
        if (next_free >= REGISTERS) {
            printf("Error: Out of registers\n");
            exit(1);
        }
        return next_free++;
    }

    int allocate_range(int count) {
        if (next_free + count > REGISTERS) {
            printf("Error: Cannot allocate %d registers (only %d available)\n",
                   count, REGISTERS - next_free);
            exit(1);
        }
        int first = next_free;
        next_free += count;
        return first;
    }

    void push_scope() {
        array_push(&scope_stack, next_free);
    }

    void pop_scope() {
        if (scope_stack.size > 0) {
            next_free = scope_stack.data[scope_stack.size - 1];
            scope_stack.size--;
        }
    }

    int mark() {
        return next_free;
    }

    void restore(int mark) {
        next_free = mark;
    }

    void clear() {
        next_free = 0;
        array_clear(&scope_stack);
    }
};

struct LoopContext {
    const char* start_label;
    const char* end_label;
    int saved_reg_mark;
};

struct CondContext {
    const char* else_label;
    const char* end_label;
    int saved_reg_mark;
    bool has_else;
};

struct ProgramBuilder {
    array<VMInstruction, QueryArena> instructions;
    string_map<uint32_t, QueryArena> labels;
    array<uint32_t, QueryArena> unresolved_jumps;
    RegisterAllocator regs;
    int label_counter = 0;

    // Current control flow context
    LoopContext current_loop;

    // ========================================================================
    // Basic Operations
    // ========================================================================

    ProgramBuilder& emit(VMInstruction inst) {
        array_push(&instructions, inst);

        // Track instructions that need label resolution
        if (inst.p2 == -1 || inst.p3 == -1) {
            array_push(&unresolved_jumps, instructions.size - 1);
        }
        return *this;
    }

    ProgramBuilder& label(const char* name) {
        stringmap_insert(&labels, name, instructions.size);
        return *this;
    }

    const char* generate_label(const char* prefix = "L") {
        char* name = (char*)arena::alloc<QueryArena>(32);
        snprintf(name, 32, "%s%d", prefix, label_counter++);
        return name;
    }

    int here() {
        return instructions.size;
    }

    void resolve_labels() {
        for (uint32_t i = 0; i < unresolved_jumps.size; i++) {
            uint32_t inst_idx = unresolved_jumps.data[i];
            VMInstruction& inst = instructions.data[inst_idx];

            if (inst.p4) {  // Label stored temporarily in p4
                const char* label_name = (const char*)inst.p4;
                uint32_t* target = stringmap_get(&labels, label_name);

                if (target) {
                    if (inst.p2 == -1) inst.p2 = *target;
                    if (inst.p3 == -1) inst.p3 = *target;
                }
                inst.p4 = nullptr;  // Clean up
            }
        }
    }

    // ========================================================================
    // Control Flow
    // ========================================================================

    ProgramBuilder& goto_label(const char* label_name) {

        emit(GOTO_MAKE((void*)label_name));
        return *this;
    }

    ProgramBuilder& halt(int exit_code = 0) {
        emit(HALT_MAKE(exit_code));
        return *this;
    }

    ProgramBuilder& jumpif_true(int test_reg, const char* label) {
        emit(JUMPIF_MAKE(test_reg, (void*)label, true));
        return *this;
    }

    ProgramBuilder& jumpif_false(int test_reg, const char* label) {

        emit(JUMPIF_MAKE(test_reg, (void*)label, false));
        return *this;
    }

    // ========================================================================
    // Loop Management
    // ========================================================================

    LoopContext begin_loop(const char* name = nullptr) {
        if (!name) name = generate_label("loop");

        LoopContext ctx = {
            name,
            generate_label("end"),
            regs.mark()
        };

        label(ctx.start_label);
        current_loop = ctx;
        return ctx;
    }

    ProgramBuilder& end_loop(const LoopContext& ctx) {
        goto_label(ctx.start_label);
        label(ctx.end_label);
        regs.restore(ctx.saved_reg_mark);
        return *this;
    }

    ProgramBuilder& break_loop() {
        goto_label(current_loop.end_label);
        return *this;
    }

    ProgramBuilder& continue_loop() {
        goto_label(current_loop.start_label);
        return *this;
    }

    // ========================================================================
    // Conditional Management
    // ========================================================================

    CondContext begin_if(int test_reg) {
        CondContext ctx = {
            generate_label("else"),
            generate_label("endif"),
            regs.mark(),
            false
        };

        jumpif_false(test_reg, ctx.else_label);
        return ctx;
    }

    ProgramBuilder& begin_else(CondContext& ctx) {
        goto_label(ctx.end_label);  // Skip else block if we took the if path
        label(ctx.else_label);
        ctx.has_else = true;
        return *this;
    }

    ProgramBuilder& end_if(const CondContext& ctx) {
        if (!ctx.has_else) {
            label(ctx.else_label);  // Else label still needed for false jumps
        }
        label(ctx.end_label);
        regs.restore(ctx.saved_reg_mark);
        return *this;
    }

    // ========================================================================
    // Data Loading and Register Operations
    // ========================================================================

    int load(const TypedValue& value) {
        int reg = regs.allocate();
        emit(LOAD_MAKE(reg, value.type, value.data));
        return reg;
    }

    int move_reg(int src_reg) {
        int dest = regs.allocate();
        emit(MOVE_MOVE_MAKE(dest, src_reg));
        return dest;
    }

    // ========================================================================
    // Arithmetic and Logic
    // ========================================================================

    int arithmetic(int left_reg, int right_reg, ArithOp op) {
        int result = regs.allocate();
        emit(ARITHMETIC_MAKE(result, left_reg, right_reg, op));
        return result;
    }

    int test(int left_reg, int right_reg, CompareOp op) {
        int result = regs.allocate();
        emit(TEST_MAKE(result, left_reg, right_reg, op));
        return result;
    }

    int logic(int left_reg, int right_reg, LogicOp op) {
        int result = regs.allocate();
        emit(LOGIC_MAKE(result, left_reg, right_reg, op));
        return result;
    }

    // ========================================================================
    // Cursor Operations
    // ========================================================================

    ProgramBuilder& open_cursor(int cursor_id, CursorContext* context) {
        emit(OPEN_MAKE(cursor_id, context));
        return *this;
    }

    ProgramBuilder& close_cursor(int cursor_id) {
        emit(CLOSE_MAKE(cursor_id));
        return *this;
    }

    ProgramBuilder& rewind(int cursor_id, const char* jump_if_empty = nullptr, bool to_end = false) {
        emit(REWIND_MAKE(cursor_id, (void*)jump_if_empty, to_end));
        return *this;
    }

    ProgramBuilder& step(int cursor_id, const char* jump_if_done = nullptr, bool forward = true) {
        emit(STEP_MAKE(cursor_id, (void*)jump_if_done, forward));
        return *this;
    }

    ProgramBuilder& seek(int cursor_id, int key_reg, CompareOp op = EQ, const char* jump_if_not = nullptr) {
        emit(SEEK_MAKE(cursor_id, key_reg, (void*)jump_if_not, op));
        return *this;
    }

    int get_column(int cursor_id, int col_index) {
        int reg = regs.allocate();
        emit(COLUMN_MAKE(cursor_id, col_index, reg));
        return reg;
    }

    ProgramBuilder& insert_record(int cursor_id, int key_reg, int record_count = 1) {
        emit(INSERT_MAKE(cursor_id, key_reg, record_count));
        return *this;
    }

    int delete_record(int cursor_id) {
        int delete_occurred = regs.allocate();
        int cursor_valid = regs.allocate();
        emit(DELETE_MAKE(cursor_id, cursor_valid, delete_occurred));
        return delete_occurred;  // Return whether delete happened
    }

    ProgramBuilder& update_record(int cursor_id, int record_reg) {
        emit(UPDATE_MAKE(cursor_id, record_reg));
        return *this;
    }

    // ========================================================================
    // Result Output
    // ========================================================================

    ProgramBuilder& result(int first_reg, int reg_count) {
        emit(RESULT_MAKE(first_reg, reg_count));
        return *this;
    }

    ProgramBuilder& result(int single_reg) {
        return result(single_reg, 1);
    }

    // ========================================================================
    // Transaction Control
    // ========================================================================

    ProgramBuilder& begin_transaction() {
        emit(BEGIN_MAKE());
        return *this;
    }

    ProgramBuilder& commit_transaction() {
        emit(COMMIT_MAKE());
        return *this;
    }

    ProgramBuilder& rollback_transaction() {
        emit(ROLLBACK_MAKE());
        return *this;
    }

    // ========================================================================
    // Function Calls
    // ========================================================================

    int call_function(VMFunction fn, int first_arg_reg, int arg_count) {
        int result_reg = regs.allocate();
        emit(FUNCTION_MAKE(result_reg, first_arg_reg, arg_count, &fn));
        return result_reg;
    }
};
