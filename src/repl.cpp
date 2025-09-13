#include "repl.hpp"
#include "arena.hpp"
#include "containers.hpp"
#include "catalog.hpp"
#include "common.hpp"
#include "types.hpp"
#include "demo.hpp"
#include "compile.hpp"
#include "pager.hpp"
#include "parser.hpp"
#include "semantic.hpp"
#include "vm.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <chrono>

static array<int, query_arena> result_column_widths;
static const char			  *current_database_path = nullptr;

static int
get_column_width(data_type type)
{
	switch (type)
	{
	case TYPE_U8:
	case TYPE_U16:
	case TYPE_U32:
	case TYPE_I8:
	case TYPE_I16:
	case TYPE_I32:
		return 10;
	case TYPE_U64:
	case TYPE_I64:
		return 15;
	case TYPE_F32:
	case TYPE_F64:
		return 12;
	case TYPE_CHAR8:
		return 10;
	case TYPE_CHAR16:
		return 18;
	case TYPE_CHAR32:
		return 35;
	case TYPE_CHAR64:
		return 35;
	case TYPE_CHAR128:
		return 40;
	case TYPE_CHAR256:
		return 50;
	default:
		return 15;
	}
}

static void
print_select_headers(select_stmt *select_stmt)
{
	relation *table = catalog.get(select_stmt->table_name);
	if (!table)
	{
		return;
	}

	printf("\n");

	if (select_stmt->is_star)
	{
		for (uint32_t i = 0; i < table->columns.size(); i++)
		{
			int width = get_column_width(table->columns[i].type);
			printf("%-*s  ", width, table->columns[i].name);
		}
		printf("\n");

		for (uint32_t i = 0; i < table->columns.size(); i++)
		{
			int width = get_column_width(table->columns[i].type);
			for (int j = 0; j < width; j++)
			{
				printf("-");
			}
			printf("  ");
		}
		printf("\n");
	}
	else
	{
		for (uint32_t i = 0; i < select_stmt->sem.column_indices.size(); i++)
		{
			uint32_t	col_idx = select_stmt->sem.column_indices[i];
			const char *name = table->columns[col_idx].name;
			data_type	type = table->columns[col_idx].type;

			int width = get_column_width(type);
			printf("%-*s  ", width, name);
		}
		printf("\n");

		for (uint32_t i = 0; i < select_stmt->sem.column_indices.size(); i++)
		{
			uint32_t  col_idx = select_stmt->sem.column_indices[i];
			data_type type = table->columns[col_idx].type;

			int width = get_column_width(type);
			for (int j = 0; j < width; j++)
			{
				printf("-");
			}
			printf("  ");
		}
		printf("\n");
	}
}

void
setup_result_formatting(select_stmt *select_stmt)
{
	result_column_widths.clear();

	relation *table = catalog.get(select_stmt->table_name);
	if (!table)
	{
		return;
	}

	if (select_stmt->is_star)
	{
		for (uint32_t i = 0; i < table->columns.size(); i++)
		{
			result_column_widths.push(get_column_width(table->columns[i].type));
		}
	}
	else
	{
		for (uint32_t i = 0; i < select_stmt->sem.column_indices.size(); i++)
		{
			uint32_t  col_idx = select_stmt->sem.column_indices[i];
			data_type type = table->columns[col_idx].type;
			result_column_widths.push(get_column_width(type));
		}
	}
}

void
formatted_result_callback(typed_value *result, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		int width = (i < result_column_widths.size()) ? result_column_widths[i] : 15;

		switch (type_id(result[i].type))
		{
		case TYPE_ID_U8:
		case TYPE_ID_U16:
		case TYPE_ID_U32:
		case TYPE_ID_U64:
			printf("%-*u  ", width, result[i].as_u32());
			break;

		case TYPE_ID_I8:
		case TYPE_ID_I16:
		case TYPE_ID_I32:
		case TYPE_ID_I64:
			printf("%-*d  ", width, result[i].as_i32());
			break;

		case TYPE_ID_F32:
		case TYPE_ID_F64:
			printf("%-*.2f  ", width, result[i].as_f64());
			break;

		case TYPE_ID_CHAR:
		case TYPE_ID_VARCHAR: {
			const char *str = result[i].as_char();
			printf("%-*s  ", width, str ? str : "NULL");
			break;
		}

		case TYPE_ID_NULL:
			printf("%-*s  ", width, "NULL");
			break;

		default:
			printf("%-*s  ", width, "???");
		}
	}
	printf("\n");
}

bool
execute_sql_statement(const char *sql)
{
	bool		  in_transaction = false;
	parser_result result = parse_sql(sql);
	if (!result.success)
	{

		printf("%s\n", result.error.data());

		return false;
	}

	for (auto &stmt : result.statements)
	{
		semantic_result res = semantic_analyze(stmt);
		if (!res.success)
		{

			printf("%s\n", res.error.data());

			if (in_transaction)
			{
				pager_rollback();
			}
			return false;
		}

		if (stmt->type == STMT_BEGIN && !in_transaction)
		{
			in_transaction = true;
		}
		else if (stmt->type == STMT_COMMIT || stmt->type == STMT_ROLLBACK)
		{
			in_transaction = false;
		}

		if (stmt->type == STMT_SELECT)
		{
			print_select_headers(&stmt->select_stmt);
			setup_result_formatting(&stmt->select_stmt);
			vm_set_result_callback(formatted_result_callback);
		}

		array<vm_instruction, query_arena> program = compile_program(stmt, !in_transaction);
		if (program.size() == 0)
		{

			printf("❌ Compilation failed: %s\n", sql);

			return false;
		}

		VM_RESULT vm_result = vm_execute(program.data(), program.size());
		if (vm_result != OK)
		{

			printf("❌ Execution failed: %s\n", sql);

			if (in_transaction)
			{
				pager_rollback();
			}
			return false;
		}

		printf("\n");
	}

	return true;
}

void
run_meta_command(const char *cmd)
{
	if (strcmp(cmd, ".quit") == 0 || strcmp(cmd, ".exit") == 0)
	{
		printf("Goodbye!\n");
		pager_close();
		exit(0);
	}
	else if (strcmp(cmd, ".help") == 0)
	{
		printf("Available commands:\n");
		printf("  .quit/.exit       Exit the REPL\n");
		printf("  .tables           List all tables\n");
		printf("  .schema <table>   Show table schema\n");
		printf("  .debug            Toggle debug mode\n");
		printf("  .reload           Reload catalog from disk\n");
		printf("  .demo1            Simple query demo\n");
		printf("  .demo2            Transaction demo\n");
		printf("  .demo3            Complex WHERE demo\n");
		printf("  .test_perf        Performance test\n");
		printf("  .test_order       ORDER BY test\n");
		printf("\n");
		printf("Everything else is treated as SQL.\n");
	}
	else if (strcmp(cmd, ".debug") == 0)
	{
		_debug = !_debug;
		printf("Debug mode: %s\n", _debug ? "ON" : "OFF");
	}
	else if (strcmp(cmd, ".tables") == 0)
	{
		printf("\nTables:\n");
		printf("-------\n");

		for (auto [name, relation] : catalog)
		{
			printf("  %.*s (%d columns)\n", (int)name.length(), name.c_str(), relation.columns.size());
		}

		printf("\n");
	}
	else if (strncmp(cmd, ".schema ", 8) == 0)
	{
		const char *table_name = cmd + 8;
		relation   *s = catalog.get(table_name);
		if (s)
		{
			printf("\nSchema for %s:\n", table_name);
			printf("--------------\n");
			for (uint32_t i = 0; i < s->columns.size(); i++)
			{
				printf("  %-20s %s\n", s->columns[i].name, type_name(s->columns[i].type));
			}
			printf("\n");
		}
		else
		{
			printf("Table '%s' not found\n", table_name);
		}
	}
	else if (strcmp(cmd, ".reload") == 0)
	{
		catalog_reload();
		printf("Catalog reloaded from disk\n");
	}
	else if (strcmp(cmd, ".demo1") == 0)
	{
		printf("\n-- Simple Query Demo --\n");
		execute_sql_statement("SELECT * FROM users WHERE age > 25 ORDER BY age");
		execute_sql_statement("SELECT username, city FROM users WHERE user_id < 10");
	}
	else if (strcmp(cmd, ".demo2") == 0)
	{
		printf("\n-- Transaction Demo --\n");
		execute_sql_statement("BEGIN");
		execute_sql_statement("UPDATE users SET age = 99 WHERE user_id = 1");
		execute_sql_statement("SELECT * FROM users WHERE user_id = 1");
		execute_sql_statement("ROLLBACK");
		execute_sql_statement("SELECT * FROM users WHERE user_id = 1");
	}
	else if (strcmp(cmd, ".demo3") == 0)
	{
		printf("\n-- Complex WHERE Demo --\n");
		execute_sql_statement("SELECT age, email FROM users WHERE (user_id >= 75 AND age < 30 AND age != 27) OR "
							  "username = 'hazeslg' ORDER BY age ASC");
		execute_sql_statement("SELECT * FROM products WHERE price > 100 AND stock < 50 ORDER BY price DESC");
	}
	else if (strncmp(cmd, ".demo_like", 10) == 0)
	{
		const char *args = cmd[10] ? cmd + 11 : "";
		demo_like_pattern(args);
	}
	else if (strncmp(cmd, ".demo_join", 10) == 0)
	{
		const char *args = cmd[10] ? cmd + 11 : "";
		demo_nested_loop_join(args);
	}
	else if (strncmp(cmd, ".demo_subquery", 14) == 0)
	{
		const char *args = cmd[14] ? cmd + 15 : "";
		demo_subquery_pattern(args);
	}
	else if (strncmp(cmd, ".demo_index", 11) == 0)
	{
		const char *args = cmd[11] ? cmd + 12 : "";
		demo_composite_index(args);
	}
	else if (strncmp(cmd, ".demo_group", 11) == 0)
	{
		const char *args = cmd[11] ? cmd + 12 : "";
		demo_group_by_aggregate(args);
	}
	else if (strncmp(cmd, ".demo_blob", 10) == 0)
	{
		const char *args = cmd[10] ? cmd + 11 : "";
		demo_blob_storage(args);
	}


	else
	{
		printf("Unknown command: %s (type .help for commands)\n", cmd);
	}
}

int
run_repl(const char *database_path)
{
	arena<query_arena>::init();

	current_database_path = database_path;
	bool existed = pager_open(database_path);

	if (!existed)
	{
		printf("Creating new database: %s\n", database_path);
		bootstrap_master(true);
		create_all_tables_sql();
		load_all_data_sql();
		printf("Database initialized with sample data.\n\n");
	}
	else
	{
		catalog_reload();
		printf("Opened existing database: %s\n", database_path);
	}

	char input[4096];

	printf("SQL Engine v0.1\n");
	printf("Type .help for commands or start typing SQL\n\n");

	while (true)
	{
		printf("sql> ");
		fflush(stdout);

		if (!fgets(input, sizeof(input), stdin))
		{
			printf("\n");
			break;
		}

		auto sql_buffer = stream_writer<query_arena>::begin();

		size_t len = strlen(input);
		if (len > 0 && input[len - 1] == '\n')
		{
			input[len - 1] = '\0';
		}

		if (strlen(input) == 0)
		{
			continue;
		}

		if (input[0] == '.')
		{
			run_meta_command(input);
			continue;
		}

		sql_buffer.write(input);

		// Multi-line SQL support
		while (!strchr((char *)sql_buffer.start, ';'))
		{
			printf("   ...> ");
			fflush(stdout);

			if (!fgets(input, sizeof(input), stdin))
			{
				printf("\n");
				break;
			}

			len = strlen(input);
			if (len > 0 && input[len - 1] == '\n')
			{
				input[len - 1] = '\0';
			}

			sql_buffer.write(" ");
			sql_buffer.write(input);
		}

		auto start = std::chrono::high_resolution_clock::now();
		bool success = execute_sql_statement((char *)sql_buffer.finish().data());
		auto end = std::chrono::high_resolution_clock::now();

		if (_debug && success)
		{
			auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
			printf("Query executed in %ld ms\n", ms.count());
		}

		// arena<query_arena>::reset_and_decommit();
	}

	pager_close();
	return 0;
}
