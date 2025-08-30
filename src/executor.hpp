#pragma once
#include "vm.hpp"
#include "parser.hpp"
#include "arena.hpp"

// Existing functions
void execute(const char * sql);
void executor_init(bool existed);
void executor_shutdown();
void set_capture_mode(bool capture);
size_t get_row_count();
bool check_int_value(size_t row, size_t col, int expected);
bool check_string_value(size_t row, size_t col, const char* expected);
void clear_results();
void print_results();

// ============================================================================
// Pre-compiled Program Execution
// ============================================================================

enum ProgramType : uint8_t {
    PROG_DDL_CREATE_TABLE,
    PROG_DDL_CREATE_INDEX,
    PROG_DDL_DROP_TABLE,
    PROG_DDL_DROP_INDEX,
    PROG_DML_SELECT,
    PROG_DML_INSERT,
    PROG_DML_UPDATE,
    PROG_DML_DELETE,
    PROG_TCL_BEGIN,
    PROG_TCL_COMMIT,
    PROG_TCL_ROLLBACK
};

struct CompiledProgram {
    ProgramType type;
    array<VMInstruction, QueryArena> instructions;

    // Optional metadata for DDL operations
    void* ast_node;  // Points to the original AST node for DDL operations
};

// Execute an array of pre-compiled programs
void execute_programs(CompiledProgram* programs, size_t program_count);
