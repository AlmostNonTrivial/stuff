// executor.cpp - Refactored with command categorization
#include "executor.hpp"

#include "arena.hpp"
#include "bplustree.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include "parser.hpp"
#include "compile.hpp"
#include "catalog.hpp"

#include "vm.hpp"
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <utility>

enum CommandCategory {
    CMD_DDL,  // Data Definition Language (CREATE, DROP, ALTER)
    CMD_DML,  // Data Manipulation Language (SELECT, INSERT, UPDATE, DELETE)
    CMD_TCL,  // Transaction Control Language (BEGIN, COMMIT, ROLLBACK)
};
CommandCategory get_cmd_category(const Statement* stmt) {
    switch (stmt->type) {
        case STMT_CREATE_TABLE:
        case STMT_CREATE_INDEX:
        case STMT_DROP_TABLE:
        case STMT_DROP_INDEX:
            return CMD_DDL; // Data Definition Language
        case STMT_SELECT:
        case STMT_INSERT:
        case STMT_UPDATE:
        case STMT_DELETE:
            return CMD_DML; // Data Manipulation Language
        case STMT_BEGIN:
        case STMT_COMMIT:
        case STMT_ROLLBACK:
            return CMD_TCL; // Transaction Control Language
        default:
            return CMD_DDL; // Default to DDL for unrecognized types, adjust as needed
    }
}

void
print_result_callback(TypedValue *result, size_t count)
{
	for (int i = 0; i < count; i++)
	{
		print_value(result[i].type, result[i].data);
		if (i != count - 1)
		{
			std::cout << ", ";
		}
	}
	std::cout << "\n";
}


MemoryContext ctx = {.alloc = arena::alloc<QueryArena>, .emit_row = print_result_callback};
static array<array<TypedValue, QueryArena>, QueryArena> last_results;


void print_results() {
    int count = last_results.size;
   	for (int i = 0; i < count; i++)
	{
	    print_result_callback(last_results.data[i].data, last_results.data[i].size);
	}

}

static void capture_result_callback(TypedValue* result, size_t count) {
    auto row = array_create<TypedValue, QueryArena>();
    for (size_t i = 0; i < count; i++) {
        array_push(row, result[i]);
    }
    array_push(&last_results, *row);
}

// Add a mode flag
static bool capture_mode = false;

void set_capture_mode(bool capture) {
    capture_mode = capture;
    if (capture) {
        ctx.emit_row = capture_result_callback;
        array_clear(&last_results);
    } else {
        ctx.emit_row = print_result_callback;
    }
}

// Simple accessors
size_t get_row_count() {
    return last_results.size;
}

bool check_int_value(size_t row, size_t col, int expected) {
    if (row >= last_results.size) return false;
    if (col >= last_results.data[row].size) return false;

    TypedValue& val = last_results.data[row].data[col];
    if (val.type != TYPE_4) return false;

    return *(uint32_t*)val.data == expected;
}

bool check_string_value(size_t row, size_t col, const char* expected) {
    if (row >= last_results.size) return false;
    if (col >= last_results.data[row].size) return false;

    TypedValue& val = last_results.data[row].data[col];
    return strcmp((char*)val.data, expected) == 0;
}

void clear_results() {
    array_clear(&last_results);
}



// ============================================================================
// Executor State
// ============================================================================

static struct ExecutorState
{
	bool initialized;
	bool in_transaction;
	uint32_t next_master_id;
} executor_state = {};

static void
insert_master_entry(const char *type, const char *name, const char *tbl_name, uint32_t rootpage, const char *sql)
{
	// Build INSERT statement for master table
	char buffer[2048];
	snprintf(buffer, sizeof(buffer), "INSERT INTO master_catalog VALUES (%u, '%s', '%s', '%s', %u, '%s')",
			 executor_state.next_master_id++, type, name, tbl_name, rootpage, sql ? sql : "");

	// Parse and execute (without triggering recursive catalog updates)
	auto stmts = parse_sql(buffer);

	assert(0!=stmts->size);
		print_ast(stmts->data[0]);


	array<VMInstruction, QueryArena> program = build_from_ast(stmts->data[0]);
	vm_execute(program.data, program.size, &ctx);
}

static void
delete_master_entry(const char *name)
{
	char buffer[256];
	snprintf(buffer, sizeof(buffer), "DELETE FROM master_catalog WHERE name = '%s';", name);

	auto stmts = parse_sql(buffer);

	assert(stmts->size != 0);

	array<VMInstruction, QueryArena> program = build_from_ast(stmts->data[0]);
	vm_execute(program.data, program.size, &ctx);
}



static void
load_schema_from_master()
{

	// Save and clear current schema (except master table)
	Table *master = get_table("master_catalog");
	assert(master != nullptr);

	// Query master table to rebuild schema
	const char *query = "SELECT * FROM master_catalog;";

	// Set up result callback to capture rows

	set_capture_mode(true);

	auto stmts = parse_sql(query);
	assert(0 != stmts->size);
	array<VMInstruction, QueryArena> program = build_from_ast(stmts->data[0]);
	vm_execute(program.data, program.size, &ctx);

	set_capture_mode(false);

	// Process results to rebuild schema
	for (size_t i = 0; i < last_results.size; i++)
	{
		auto &row = last_results.data[i];
		if (row.size < 6)
			continue;

		uint32_t id = *(uint32_t *)row.data[0].data;
		const char *type = (const char *)row.data[1].data;
		const char *name = (const char *)row.data[2].data;
		const char *tbl_name = (const char *)row.data[3].data;
		uint32_t rootpage = *(uint32_t *)row.data[4].data;
		const char *sql = (const char *)row.data[5].data;

		// Update ID counter
		if (id >= executor_state.next_master_id)
		{
			executor_state.next_master_id = id + 1;
		}

		if (strcmp(type, "table") == 0)
		{
			// Parse CREATE TABLE to rebuild schema

			auto create_stmts = *parse_sql(sql);
			if (0 != create_stmts.size && create_stmts.data[0]->type == STMT_CREATE_TABLE)
			{
				CreateTableStmt *node = (CreateTableStmt*)create_stmts.data[0];
				create_table(node, rootpage);

			}
		}
		else if (strcmp(type, "index") == 0)
		{
			// Rebuild index from metadata
			Table *table = get_table(tbl_name);
			if (table)
			{
				auto create_stmts = *parse_sql(sql);
				if (0!=create_stmts.size && create_stmts.data[0]->type == STMT_CREATE_INDEX)
				{
					CreateIndexStmt*node = (CreateIndexStmt*)create_stmts.data[0];
					create_index(node, rootpage);

				}
			}
		}
	}

	clear_results();
}

// ============================================================================
// Initialize executor
// ============================================================================

// ============================================================================
// DDL Command Handlers
// ============================================================================

static VM_RESULT
execute_create_table(CreateTableStmt*node)
{


	Table *table = create_table(node, 0);
	assert(table != nullptr);

	// Add to master catalog

	char buffer[1024];
	int offset = snprintf(buffer, 1024, "CREATE TABLE %s (", table->table_name.data);

	for (size_t i = 0; i < table->columns.size; i++)
	{
		if (i > 0)
		{
			offset += snprintf(buffer + offset, 1024 - offset, ", ");
		}
		offset += snprintf(buffer + offset, 1024 - offset, "%s %s", type_to_string(table->columns.data[i].type),
						   table->columns.data[i].name);
	}

	snprintf(buffer + offset, 1024 - offset, ")");
	insert_master_entry("table", node->table_name, node->table_name, table->bplustree.root_page_index, buffer);

	return OK;
}

static VM_RESULT
execute_create_index(CreateIndexStmt*node)
{
	// Create index structure
	Index *index = create_index(node, 0);

	char buffer[512];
	snprintf(buffer, 512, "CREATE INDEX %s ON %s (%s)", node->index_name, node->table_name, node->index_name);
	insert_master_entry("index", node->index_name, node->table_name, index->btree.root_page_index, buffer);

	return OK;
}

static VM_RESULT
execute_drop_index(DropIndexStmt*node)
{

	Index *index = get_index(node->index_name);

	assert(index != nullptr);

	remove_index(index->table_name.data, index->column_index);

	delete_master_entry(node->index_name);

	return OK;
}

static VM_RESULT
execute_drop_table(DropTableStmt*node)
{

	if (strcmp(node->table_name, "master_catalog") == 0)
	{
		printf("Error: Cannot drop master_catalog table\n");
		return ERR;
	}

	Table *table = get_table(node->table_name);
	assert(table != nullptr);
	remove_table(table->table_name.data);

	// Remove from master catalog
	delete_master_entry(node->table_name);

	return OK;
}

// ============================================================================
// TCL Command Handlers
// ============================================================================

static VM_RESULT
execute_begin()
{

	pager_begin_transaction();
	executor_state.in_transaction = true;

	return OK;
}

static VM_RESULT
execute_commit()
{
	pager_commit();
	executor_state.in_transaction = false;

	return OK;
}

static VM_RESULT
execute_rollback()
{
	pager_rollback();
	executor_state.in_transaction = false;
	schema_clear();
	create_master(true);
	// Reload schema from master table
	// root page will be the first thing written so it will always get
	// the root = 1;
	load_schema_from_master();

	return OK;
}

void
executor_init(bool existed)
{
	init_type_ops();
	pager_open("db");
	arena::init<QueryArena>(PAGE_SIZE * 30);
	arena::init<ParserArena>(PAGE_SIZE * 30);
	arena::init<SchemaArena>(PAGE_SIZE * 14);

	executor_state.initialized = true;
	executor_state.in_transaction = false;

	if (!existed)
	{
		pager_begin_transaction();
		create_master(false);
		pager_commit();
		executor_state.next_master_id = 1;
	}
	else
	{
		create_master(true);
		load_schema_from_master();
	}
}

// ============================================================================
// Command Execution Dispatchers
// ============================================================================

static VM_RESULT
execute_ddl_command(Statement*stmt)
{
    print_ast(stmt);
	switch (stmt->type)
	{
	case STMT_CREATE_TABLE:
		return execute_create_table((CreateTableStmt*)stmt->create_table_stmt);
	case STMT_CREATE_INDEX:
		return execute_create_index((CreateIndexStmt*)stmt->create_index_stmt);
	case STMT_DROP_TABLE:
		return execute_drop_table((DropTableStmt*)stmt->drop_table_stmt);
	case STMT_DROP_INDEX:
		return execute_drop_index((DropIndexStmt*)stmt->drop_index_stmt);

	default:
		printf("Error: Unimplemented DDL command: %s\n", stmt->type);
		return ERR;
	}
}

static VM_RESULT
execute_dml_command(Statement *stmt)
{

	// DML commands go through the VM
	array<VMInstruction, QueryArena> program = build_from_ast(stmt);

	VM_RESULT result = vm_execute(program.data, program.size, &ctx);

	return result;
}

static VM_RESULT
execute_tcl_command(Statement*stmt)
{

	switch (stmt->type)
	{
	case STMT_BEGIN:
		return execute_begin();

	case STMT_COMMIT:
		return execute_commit();
	case STMT_ROLLBACK:
		return execute_rollback();

	default:
		printf("Error: Unknown TCL command: %s\n", stmt->type);
		return ERR;
	}
}

// ============================================================================
// Main Execute Function
// ============================================================================



void
execute(const char *sql)
{
	auto statements = parse_sql(sql);

	assert(0 !=statements->size);

	for (size_t i = 0; i < statements->size; i++)
	{
		Statement*stmt = statements->data[i];

		if(_debug) {
		    print_ast(stmt);
		}


		VM_RESULT result = OK;

		CommandCategory category = get_cmd_category(stmt);

		// Handle auto-transaction for DML commands
		bool auto_transaction = false;
		if (category == CMD_DML && stmt->type != STMT_SELECT && !executor_state.in_transaction)
		{
			execute_begin();
			auto_transaction = true;
		}

		// Dispatch based on category
		switch (category)
		{
		case CMD_DDL:
			// DDL commands need a transaction
			if (!executor_state.in_transaction)
			{
				execute_begin();
				auto_transaction = true;
			}

			result = execute_ddl_command(stmt);
			break;

		case CMD_DML:
			// DML commands go through VM compilation
			result = execute_dml_command(stmt);

			break;

		case CMD_TCL:
			// Transaction control is handled directly
			result = execute_tcl_command(stmt);
			break;

		default:
			printf("Error: Unknown command category\n");
			result = ERR;
		}

		if (auto_transaction)
		{
			if (result == OK)
			{

				execute_commit();
			}
			else
			{
				execute_rollback();
			}
		}

		// Handle errors
		if (result != OK)
		{

			if (executor_state.in_transaction && !auto_transaction)
			{
				printf("Error occurred in transaction, rolling "
					   "back\n");
				execute_rollback();
			}
			break; // Stop processing remaining statements
		}
	}

	arena::reset_and_decommit<QueryArena>();
	arena::reset_and_decommit<ParserArena>();

}





static CommandCategory get_program_category(ProgramType type) {
    switch (type) {
        case PROG_DDL_CREATE_TABLE:
        case PROG_DDL_CREATE_INDEX:
        case PROG_DDL_DROP_TABLE:
        case PROG_DDL_DROP_INDEX:
            return CMD_DDL;

        case PROG_DML_SELECT:
        case PROG_DML_INSERT:
        case PROG_DML_UPDATE:
        case PROG_DML_DELETE:
            return CMD_DML;

        case PROG_TCL_BEGIN:
        case PROG_TCL_COMMIT:
        case PROG_TCL_ROLLBACK:
            return CMD_TCL;

        default:
            return CMD_DML;
    }
}

static VM_RESULT execute_compiled_ddl(CompiledProgram* prog) {
    // DDL needs special handling - use the AST node if provided
    if (prog->ast_node) {
        switch (prog->type) {
            case PROG_DDL_CREATE_TABLE:
                return execute_create_table((CreateTableStmt*)prog->ast_node);
            case PROG_DDL_CREATE_INDEX:
                return execute_create_index((CreateIndexStmt*)prog->ast_node);
            case PROG_DDL_DROP_TABLE:
                return execute_drop_table((DropTableStmt*)prog->ast_node);
            case PROG_DDL_DROP_INDEX:
                return execute_drop_index((DropIndexStmt*)prog->ast_node);
            default:
                break;
        }
    }
    // Fallback to VM execution if no AST node
    return vm_execute(prog->instructions.data, prog->instructions.size, &ctx);
}

static VM_RESULT execute_compiled_dml(CompiledProgram* prog) {
    return vm_execute(prog->instructions.data, prog->instructions.size, &ctx);
}

static VM_RESULT execute_compiled_tcl(CompiledProgram* prog) {
    switch (prog->type) {
        case PROG_TCL_BEGIN:
            return execute_begin();
        case PROG_TCL_COMMIT:
            return execute_commit();
        case PROG_TCL_ROLLBACK:
            return execute_rollback();
        default:
            return ERR;
    }
}

void execute_programs(CompiledProgram* programs, size_t program_count) {
    if (!executor_state.initialized) {
        printf("Error: Executor not initialized\n");
        return;
    }

    for (size_t i = 0; i < program_count; i++) {
        CompiledProgram* prog = &programs[i];

        if (_debug) {
            printf("Executing pre-compiled program %zu (type=%d)\n", i, prog->type);
        }

        VM_RESULT result = OK;
        CommandCategory category = get_program_category(prog->type);

        // Handle auto-transaction for DML commands (except SELECT)
        bool auto_transaction = false;
        if (category == CMD_DML &&
            prog->type != PROG_DML_SELECT &&
            !executor_state.in_transaction) {
            execute_begin();
            auto_transaction = true;
        }

        // DDL commands also need a transaction
        if (category == CMD_DDL && !executor_state.in_transaction) {
            execute_begin();
            auto_transaction = true;
        }

        // Dispatch based on category
        switch (category) {
            case CMD_DDL:
                result = execute_compiled_ddl(prog);
                break;

            case CMD_DML:
                result = execute_compiled_dml(prog);
                break;

            case CMD_TCL:
                result = execute_compiled_tcl(prog);
                break;

            default:
                printf("Error: Unknown command category\n");
                result = ERR;
        }

        // Handle auto-transaction completion
        if (auto_transaction) {
            if (result == OK) {
                execute_commit();
            } else {
                execute_rollback();
            }
        }

        // Handle errors
        if (result != OK) {
            if (executor_state.in_transaction && !auto_transaction) {
                printf("Error occurred in transaction, rolling back\n");
                execute_rollback();
            }
            break; // Stop processing remaining programs
        }
    }

    // Clean up arenas just like execute() does
    arena::reset_and_decommit<QueryArena>();
    arena::reset_and_decommit<ParserArena>();
}

void
executor_shutdown()
{
	schema_clear();
	executor_state.initialized = false;
	pager_close();
}
