// executor.cpp - Refactored with command categorization
#include "executor.hpp"

#include "arena.hpp"
#include "bplustree.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include "parser.hpp"
#include "compile.hpp"
#include "schema.hpp"
#include "vec.hpp"
#include "vm.hpp"
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <utility>

struct SchemaSnapshots
{
	struct Entry
	{
		const char *table;
		uint32_t root;
		Vec<std::pair<uint32_t, uint32_t>, QueryArena> indexes;
	};
	Vec<Entry, QueryArena> entries;
};

SchemaSnapshots
take_snapshot()
{
	SchemaSnapshots snap;
	auto tables = get_all_table_names();
	for (int i = 0; i < tables.size(); i++)
	{

		auto table = get_table(tables[i]);

		auto name = table->table_name.c_str();
		auto root = table->tree.bplustree.root_page_index;
		Vec<std::pair<uint32_t, uint32_t>, QueryArena> indexes;
		for (int j = 0; j < table->indexes.size(); j++)
		{
			indexes.push_back({table->indexes[j]->column_index, table->indexes[j]->tree.btree.root_page_index});
		}
		snap.entries.push_back({.root = root, .table = name, .indexes = indexes});
	}
	return snap;
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

// ============================================================================
// Executor State
// ============================================================================

static struct ExecutorState
{
	bool initialized;
	bool in_transaction;
	uint32_t next_master_id;
	SchemaSnapshots snapshot;
} executor_state = {};

static void
insert_master_entry(const char *type, const char *name, const char *tbl_name, uint32_t rootpage, const char *sql)
{
	// Build INSERT statement for master table
	char buffer[2048];
	snprintf(buffer, sizeof(buffer), "INSERT INTO sqlite_master VALUES (%u, '%s', '%s', '%s', %u, '%s')",
			 executor_state.next_master_id++, type, name, tbl_name, rootpage, sql ? sql : "");

	// Parse and execute (without triggering recursive catalog updates)
	Vec<ASTNode *, QueryArena> stmts = parse_sql(buffer);
	assert(!stmts.empty());

	Vec<VMInstruction, QueryArena> program = build_from_ast(stmts[0]);
	vm_execute(program.get_data(), program.size(), &ctx);
}

static void
update_master_rootpage(const char *name, uint32_t new_rootpage)
{
	char buffer[512];
	snprintf(buffer, sizeof(buffer), "UPDATE sqlite_master SET rootpage = %u WHERE name = '%s'", new_rootpage, name);

	Vec<ASTNode *, QueryArena> stmts = parse_sql(buffer);

	assert(!stmts.empty());

	Vec<VMInstruction, QueryArena> program = build_from_ast(stmts[0]);
	vm_execute(program.get_data(), program.size(), &ctx);
}
void
update_roots_in_master(const SchemaSnapshots &before, const SchemaSnapshots &after)
{
	for (size_t i = 0; i < after.entries.size(); i++)
	{
		if (i < before.entries.size())
		{
			// Check table root changes
			if (before.entries[i].root != after.entries[i].root)
			{
				update_master_rootpage(after.entries[i].table, after.entries[i].root);
			}

			// Check index root changes
			for (size_t j = 0; j < after.entries[i].indexes.size(); j++)
			{
				if (j < before.entries[i].indexes.size() &&
					before.entries[i].indexes[j].second != after.entries[i].indexes[j].second)
				{
					Index *idx = get_index(after.entries[i].table, after.entries[i].indexes[j].first);
					if (idx)
					{
						update_master_rootpage(idx->index_name.c_str(), after.entries[i].indexes[j].second);
					}
				}
			}
		}
	}
}

static void
delete_master_entry(const char *name)
{
	char buffer[256];
	snprintf(buffer, sizeof(buffer), "DELETE FROM sqlite_master WHERE name = '%s' OR tbl_name = '%s'", name, name);

	Vec<ASTNode *, QueryArena> stmts = parse_sql(buffer);

	assert(!stmts.empty());

	Vec<VMInstruction, QueryArena> program = build_from_ast(stmts[0]);
	vm_execute(program.get_data(), program.size(), &ctx);
}

static Vec<Vec<TypedValue, QueryArena>, QueryArena> results;

static void
load_schema_from_master()
{

	// Save and clear current schema (except master table)
	Table *master = get_table("sqlite_master");
	assert(master != nullptr);

	// Query master table to rebuild schema
	const char *query = "SELECT * FROM sqlite_master ORDER BY id";

	// Set up result callback to capture rows

	auto capture_callback = [](TypedValue *cols, size_t size) {
		auto row = Vec<TypedValue, QueryArena>::create();
		for (int i = 0; i < size; i++)
		{
			row->push_back(cols[i]);
		}

		results.push_back(*row);
	};

	ctx.emit_row = capture_callback;

	Vec<ASTNode *, QueryArena> stmts = parse_sql(query);

	assert(!stmts.empty());
	Vec<VMInstruction, QueryArena> program = build_from_ast(stmts[0]);
	vm_execute(program.get_data(), program.size(), &ctx);

	ctx.emit_row = print_result_callback;

	// Process results to rebuild schema
	for (size_t i = 0; i < results.size(); i++)
	{
		auto &row = results[i];
		if (row.size() < 6)
			continue;

		uint32_t id = *(uint32_t *)row[0].data;
		const char *type = (const char *)row[1].data;
		const char *name = (const char *)row[2].data;
		const char *tbl_name = (const char *)row[3].data;
		uint32_t rootpage = *(uint32_t *)row[4].data;
		const char *sql = (const char *)row[5].data;

		// Update ID counter
		if (id >= executor_state.next_master_id)
		{
			executor_state.next_master_id = id + 1;
		}

		if (strcmp(type, "table") == 0)
		{
			// Parse CREATE TABLE to rebuild schema
			Vec<ASTNode *, QueryArena> create_stmts = parse_sql(sql);
			if (!create_stmts.empty() && create_stmts[0]->type == AST_CREATE_TABLE)
			{
				CreateTableNode *node = (CreateTableNode *)create_stmts[0];
				Table *table = create_table(node);
				table->tree.bplustree.root_page_index = rootpage;
			}
		}
		else if (strcmp(type, "index") == 0)
		{
			// Rebuild index from metadata
			Table *table = get_table(tbl_name);
			if (table)
			{
				Vec<ASTNode *, QueryArena> create_stmts = parse_sql(sql);
				if (!create_stmts.empty() && create_stmts[0]->type == AST_CREATE_INDEX)
				{
					CreateIndexNode *node = (CreateIndexNode *)create_stmts[0];
					Index *index = create_index(node);
					index->tree.btree.root_page_index = rootpage;
				}
			}
		}
	}

	results.clear();
}

// ============================================================================
// Initialize executor
// ============================================================================

// ============================================================================
// DDL Command Handlers
// ============================================================================

static VM_RESULT
execute_create_table(CreateTableNode *node)
{
	Table *table = create_table(node);
	assert(table != nullptr);

	// Add to master catalog

	char buffer[1024];
	int offset = snprintf(buffer, 1024, "CREATE TABLE %s (", table->table_name.c_str());

	for (size_t i = 0; i < table->columns.size(); i++)
	{
		if (i > 0)
		{
			offset += snprintf(buffer + offset, 1024 - offset, ", ");
		}
		offset += snprintf(buffer + offset, 1024 - offset, "%s %s", type_to_string(table->columns[i].type),
						   table->columns[i].name.c_str());
	}

	snprintf(buffer + offset, 1024 - offset, ")");
	insert_master_entry("table", node->table, node->table, table->tree.bplustree.root_page_index, buffer);

	return OK;
}

static VM_RESULT
execute_create_index(CreateIndexNode *node)
{
	// Create index structure
	Index *index = create_index(node);

	char buffer[512];
	snprintf(buffer, 512, "CREATE INDEX %s ON %s (%s)", node->index_name, node->table, node->column);
	insert_master_entry("index", node->index_name, node->table, index->tree.btree.root_page_index, buffer);

	return OK;
}

static VM_RESULT
execute_drop_index(DropIndexNode *node)
{

	Index *index = find_index(node->index_name);

	assert(index != nullptr);

	remove_index(index->table_name, index->column_index);

	delete_master_entry(node->index_name);

	return OK;
}

static VM_RESULT
execute_drop_table(DropTableNode *node)
{

	if (strcmp(node->table, "sqlite_master") == 0)
	{
		printf("Error: Cannot drop sqlite_master table\n");
		return ERR;
	}

	Table *table = get_table(node->table);
	assert(table != nullptr);
	remove_table(table->table_name);

	// Remove from master catalog
	delete_master_entry(node->table);

	return OK;
}

// ============================================================================
// TCL Command Handlers
// ============================================================================

static VM_RESULT
execute_begin()
{
	executor_state.snapshot = take_snapshot();
	pager_begin_transaction();
	executor_state.in_transaction = true;

	return OK;
}

static VM_RESULT
execute_commit()
{

	SchemaSnapshots current = take_snapshot();
	update_roots_in_master(executor_state.snapshot, current);

	pager_commit();
	executor_state.in_transaction = false;

	return OK;
}

static VM_RESULT
execute_rollback()
{
	pager_rollback();
	executor_state.in_transaction = false;

	// Reload schema from master table
	// root page will be the first thing written so it will always get
	// the root = 1;
	load_schema_from_master();

	return OK;
}

void
init_executor()
{
	init_type_ops();
	bool existed = pager_init("db");

	arena::init<QueryArena>(PAGE_SIZE * 30);
	arena::init<RegistryArena>(PAGE_SIZE * 14);
	if (_debug)
	{
		printf("EXECUTOR: Initializing executor\n");
	}

	executor_state.initialized = true;
	executor_state.in_transaction = false;

	executor_state.next_master_id = 1;

	// Reset statistics

	// load from index=1

	create_master();
	executor_state.next_master_id = 1;

	if (existed)
	{
		load_schema_from_master();
	}
}

// ============================================================================
// Command Execution Dispatchers
// ============================================================================

static VM_RESULT
execute_ddl_command(ASTNode *stmt)
{
	if (_debug)
	{
		printf("EXECUTOR: Dispatching DDL command: %s\n", stmt->type_name());
	}

	switch (stmt->type)
	{
	case AST_CREATE_TABLE:
		return execute_create_table((CreateTableNode *)stmt);

	case AST_CREATE_INDEX:
		return execute_create_index((CreateIndexNode *)stmt);

	case AST_DROP_TABLE:
		return execute_drop_table((DropTableNode *)stmt);
	case AST_ALTER_TABLE:
		return execute_drop_index((DropIndexNode *)stmt);

	default:
		printf("Error: Unimplemented DDL command: %s\n", stmt->type_name());
		return ERR;
	}
}

static VM_RESULT
execute_dml_command(ASTNode *stmt)
{
	if (_debug)
	{
		printf("EXECUTOR: Dispatching DML command to VM: %s\n", stmt->type_name());
	}

	// DML commands go through the VM
	Vec<VMInstruction, QueryArena> program = build_from_ast(stmt);

	if (_debug)
	{
		printf("EXECUTOR: Compiled to %zu VM instructions\n", program.size());
	}

	VM_RESULT result = vm_execute(program.get_data(), program.size(), &ctx);

	if (result == OK)
	{
		executor_state.stats.dml_commands++;
	}

	return result;
}

static VM_RESULT
execute_tcl_command(ASTNode *stmt)
{
	if (_debug)
	{
		printf("EXECUTOR: Dispatching TCL command: %s\n", stmt->type_name());
	}

	switch (stmt->type)
	{
	case AST_BEGIN:
		return execute_begin();

	case AST_COMMIT:
		return execute_commit();

	case AST_ROLLBACK:
		return execute_rollback();

	default:
		printf("Error: Unknown TCL command: %s\n", stmt->type_name());
		return ERR;
	}
}

// ============================================================================
// Main Execute Function
// ============================================================================

void
execute(const char *sql)
{
	arena::reset<QueryArena>();

	if (!executor_state.initialized)
	{
		init_executor();
	}

	Vec<ASTNode *, QueryArena> statements = parse_sql(sql);

	assert(statements.empty());

	for (size_t i = 0; i < statements.size(); i++)
	{
		ASTNode *stmt = statements[i];

		stmt->statement_index = i; // Track statement position

		VM_RESULT result = OK;

		CommandCategory category = stmt->category();

		// Handle auto-transaction for DML commands
		bool auto_transaction = false;
		if (category == CMD_DML && stmt->type != AST_SELECT && !executor_state.in_transaction)
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
}
