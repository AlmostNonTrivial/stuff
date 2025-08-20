// executor.cpp
#include "executor.hpp"
#include "arena.hpp"
#include "btree.hpp"
#include "parser.hpp"
#include "programbuilder.hpp"
#include "schema.hpp"
#include "vm.hpp"
#include <cstring>
#include <cstdio>

static struct ExecutorState {
  bool initialized;
  bool in_transaction;
  bool master_table_exists;
  uint32_t next_master_id;
} executor_state = {};

// Master table helpers
static const char *type_to_string(DataType type) {
  switch (type) {
  case TYPE_UINT32:
    return "INT32";
  case TYPE_UINT64:
    return "INT64";
  case TYPE_VARCHAR32:
    return "VARCHAR32";
  case TYPE_VARCHAR256:
    return "VARCHAR256";
  default:
    return "VARCHAR32";
  }
}

static char *generate_create_sql(const TableSchema *schema) {
  // Build CREATE TABLE statement
  char* buffer = (char*)arena::alloc<QueryArena>(1024);
  int offset = snprintf(buffer, 1024, "CREATE TABLE %s (",
                       schema->table_name.c_str());

  for (size_t i = 0; i < schema->columns.size(); i++) {
    if (i > 0) {
      offset += snprintf(buffer + offset, 1024 - offset, ", ");
    }
    offset += snprintf(buffer + offset, 1024 - offset, "%s %s",
                      type_to_string(schema->columns[i].type),
                      schema->columns[i].name.c_str());
  }

  snprintf(buffer + offset, 1024 - offset, ")");
  return buffer;
}

static char *generate_index_sql(const char *table_name, uint32_t column_index,
                               const char *index_name) {
  Table* table = get_table(table_name);
  if (!table || column_index >= table->schema.columns.size()) {
    return nullptr;
  }

  char* buffer = (char*)arena::alloc<QueryArena>(512);
  snprintf(buffer, 512, "CREATE INDEX %s ON %s (%s)",
           index_name,
           table_name,
           table->schema.columns[column_index].name.c_str());

  return buffer;
}

// Execute SQL through VM without triggering recursive events
static VM_RESULT execute_internal(const char *sql) {
  // Save and clear event queue to prevent recursive processing
  auto saved_events = vm_events();
  ArenaQueue<VmEvent, QueryArena> empty_queue;
  vm_events() = empty_queue;

  ArenaVector<ASTNode *, QueryArena> stmts = parse_sql(sql);
  if (stmts.empty()) {
    vm_events() = saved_events;
    return ERR;
  }

  ArenaVector<VMInstruction, QueryArena> program = build_from_ast(stmts[0]);
  VM_RESULT result = vm_execute(program);

  // Clear any events from internal operation
  vm_events().clear();

  // Restore original event queue
  vm_events() = saved_events;

  return result;
}

static void insert_master_table_entry(const char *type, const char *name,
                                     const char *tbl_name, uint32_t rootpage,
                                     const char *sql) {
  if (!executor_state.master_table_exists)
    return;

  char buffer[2048];
  snprintf(buffer, sizeof(buffer),
           "INSERT INTO sqlite_master VALUES (%u, '%s', '%s', '%s', %u, '%s')",
           executor_state.next_master_id++, type, name, tbl_name, rootpage,
           sql ? sql : "");

  execute_internal(buffer);
}

static void update_master_table_rootpage(const char *name, uint32_t new_rootpage) {
  if (!executor_state.master_table_exists)
    return;

  char buffer[512];
  snprintf(buffer, sizeof(buffer),
           "UPDATE sqlite_master SET rootpage = %u WHERE name = '%s'",
           new_rootpage, name);

  execute_internal(buffer);
}

static void delete_master_table_entry(const char *name) {
  if (!executor_state.master_table_exists)
    return;

  char buffer[256];
  snprintf(buffer, sizeof(buffer),
           "DELETE FROM sqlite_master WHERE name = '%s'", name);

  execute_internal(buffer);
}

static void create_master_table() {
  // Create the master table schema
  TableSchema *schema = (TableSchema *)arena::alloc<QueryArena>(sizeof(TableSchema));
  schema->table_name = "sqlite_master";

  // Columns: id (key), type, name, tbl_name, rootpage, sql
  schema->columns.push_back({"id", TYPE_UINT32});
  schema->columns.push_back({"type", TYPE_VARCHAR32});
  schema->columns.push_back({"name", TYPE_VARCHAR32});
  schema->columns.push_back({"tbl_name", TYPE_VARCHAR32});
  schema->columns.push_back({"rootpage", TYPE_UINT32});
  schema->columns.push_back({"sql", TYPE_VARCHAR256});

  calculate_column_offsets(schema);

  // Create table directly
  Table *master = (Table *)arena::alloc<QueryArena>(sizeof(Table));
  master->schema = *schema;
  master->tree = btree_create(schema->key_type(), schema->record_size, BPLUS);

  add_table(master);
  executor_state.master_table_exists = true;
  executor_state.next_master_id = 1;
}

static void rebuild_schema_from_master() {
  if (!executor_state.master_table_exists)
    return;

  // Save master table before clearing
  Table *master = get_table("sqlite_master");
  Table master_copy;
  if (master) {
    master_copy = *master;
  }

  // Clear all schema
  clear_schema();

  // Restore master table
  if (master) {
    add_table(&master_copy);
  }

  // Reset the ID counter
  executor_state.next_master_id = 1;

  // Query master table to rebuild schema
  const char *query = "SELECT * FROM sqlite_master ORDER BY id";
  ArenaVector<ASTNode *, QueryArena> stmts = parse_sql(query);
  if (stmts.empty())
    return;

  ArenaVector<VMInstruction, QueryArena> program = build_from_ast(stmts[0]);
  vm_execute(program);

  // Process results to rebuild tables and indexes
  auto& output = vm_output_buffer();
  for (auto &row : output) {
    if (row.size() < 6)
      continue;

    uint32_t id = *(uint32_t *)row[0].data;
    const char *type = (const char *)row[1].data;
    const char *name = (const char *)row[2].data;
    const char *tbl_name = (const char *)row[3].data;
    uint32_t rootpage = *(uint32_t *)row[4].data;
    const char *sql = (const char *)row[5].data;

    // Update next ID counter
    if (id >= executor_state.next_master_id) {
      executor_state.next_master_id = id + 1;
    }

    if (strcmp(type, "table") == 0 && strcmp(name, "sqlite_master") != 0) {
      // Parse CREATE TABLE to rebuild schema
      ArenaVector<ASTNode *, QueryArena> create_stmts = parse_sql(sql);
      if (!create_stmts.empty() && create_stmts[0]->type == AST_CREATE_TABLE) {
        CreateTableNode *node = (CreateTableNode *)create_stmts[0];

        Table *table = (Table *)arena::alloc<QueryArena>(sizeof(Table));
        table->schema.table_name = name;
        table->schema.columns.set(node->columns);

        calculate_column_offsets(&table->schema);

        // Restore btree with existing root page
        table->tree = btree_create(table->schema.key_type(),
                                  table->schema.record_size, BPLUS);
        table->tree.root_page_index = rootpage;

        add_table(table);
      }
    }
    else if (strcmp(type, "index") == 0) {
      Table *table = get_table(tbl_name);
      if (table) {
        // Extract column index from name (format: idx_tablename_columnindex)
        char expected_prefix[256];
        snprintf(expected_prefix, sizeof(expected_prefix), "idx_%s_", tbl_name);

        if (strncmp(name, expected_prefix, strlen(expected_prefix)) == 0) {
          const char *col_idx_str = name + strlen(expected_prefix);
          uint32_t col_idx = atoi(col_idx_str);

          if (col_idx > 0 && col_idx < table->schema.columns.size()) {
            Index *index = (Index *)arena::alloc<QueryArena>(sizeof(Index));
            index->column_index = col_idx;
            index->index_name = name;
            index->tree = btree_create(table->schema.columns[col_idx].type,
                                     table->schema.key_type(), BTREE);
            index->tree.root_page_index = rootpage;

            add_index(tbl_name, index);
          }
        }
      }
    }
  }
}

static void process_vm_events() {
  auto &events = vm_events();

  while (!events.empty()) {
    VmEvent event = events.front();
    events.pop();

    switch (event.type) {
    case EVT_TABLE_CREATED: {
      const char *table_name = event.context.table_info.table_name;
      Table *table = get_table(table_name);

      if (executor_state.master_table_exists && table &&
          strcmp(table_name, "sqlite_master") != 0) {
        char* sql = generate_create_sql(&table->schema);
        insert_master_table_entry("table",
            table_name,
            table_name,
            table->tree.root_page_index,
            sql);
      }
      break;
    }

    case EVT_INDEX_CREATED: {
      const char *table_name = event.context.index_info.table_name;
      uint32_t column_index = event.context.index_info.column_index;
      Index *index = get_index(table_name, column_index);

      if (executor_state.master_table_exists && index) {
        char index_name[256];
        snprintf(index_name, sizeof(index_name), "idx_%s_%u",
                table_name, column_index);

        char* sql = generate_index_sql(table_name, column_index, index_name);
        insert_master_table_entry("index",
            index_name,
            table_name,
            index->tree.root_page_index,
            sql);
      }
      break;
    }

    case EVT_TABLE_DROPPED: {
      const char *table_name = event.context.table_info.table_name;

      // Delete table and all its indexes from master
      delete_master_table_entry(table_name);

      // Also delete all indexes for this table
      char buffer[512];
      snprintf(buffer, sizeof(buffer),
               "DELETE FROM sqlite_master WHERE type = 'index' AND tbl_name = '%s'",
               table_name);
      execute_internal(buffer);
      break;
    }

    case EVT_INDEX_DROPPED: {
      const char *table_name = event.context.index_info.table_name;
      uint32_t column_index = event.context.index_info.column_index;

      char index_name[256];
      snprintf(index_name, sizeof(index_name), "idx_%s_%u",
              table_name, column_index);
      delete_master_table_entry(index_name);
      break;
    }

    case EVT_BTREE_ROOT_CHANGED: {
      const char *table_name = event.context.table_info.table_name;
      uint32_t column = event.context.table_info.column;

      if (executor_state.master_table_exists) {
        if (column == 0) {
          // Table root changed
          Table *table = get_table(table_name);
          if (table) {
            update_master_table_rootpage(table_name, table->tree.root_page_index);
          }
        } else {
          // Index root changed
          Index *index = get_index(table_name, column);
          if (index) {
            char index_name[256];
            snprintf(index_name, sizeof(index_name), "idx_%s_%u",
                    table_name, column);
            update_master_table_rootpage(index_name, index->tree.root_page_index);
          }
        }
      }
      break;
    }

    case EVT_TRANSACTION_BEGIN:
      if (!executor_state.in_transaction) {
        btree_begin_transaction();
        executor_state.in_transaction = true;
      }
      break;

    case EVT_TRANSACTION_COMMIT:
      if (executor_state.in_transaction) {
        btree_commit();
        executor_state.in_transaction = false;
      }
      break;

    case EVT_TRANSACTION_ROLLBACK:
      if (executor_state.in_transaction) {
        btree_rollback();
        executor_state.in_transaction = false;
        rebuild_schema_from_master();
      }
      break;

    default:
      break;
    }
  }
}

static void init_executor() {
  executor_state.initialized = true;
  executor_state.in_transaction = false;
  executor_state.master_table_exists = false;
  executor_state.next_master_id = 1;

  // Check if master table exists and load it
  if (get_table("sqlite_master") == nullptr) {
    create_master_table();
  } else {
    executor_state.master_table_exists = true;
    rebuild_schema_from_master();
  }
}




int executed = 0;
void execute(const char *sql) {

    executed++;
    std::cout  <<executed << ":  "<< sql << '\n';

  arena::reset<QueryArena>();

  if (!executor_state.initialized) {
    init_executor();
  }

  ArenaVector<ASTNode *, QueryArena> statements = parse_sql(sql);

  bool success = true;
  bool explicit_transaction = false;
  bool auto_transaction = false;

  // Check for explicit transaction commands
  for (int i = 0; i < statements.size(); i++) {
    if (statements[i]->type == AST_BEGIN ||
        statements[i]->type == AST_COMMIT ||
        statements[i]->type == AST_ROLLBACK) {
      explicit_transaction = true;
      break;
    }
  }

  for (int i = 0; i < statements.size(); i++) {
    auto statement = statements[i];
    bool is_read = (statement->type == AST_SELECT);
    bool is_write = !is_read && statement->type != AST_BEGIN &&
                   statement->type != AST_COMMIT && statement->type != AST_ROLLBACK;

    // Auto-begin transaction for writes if not in explicit transaction
    if (is_write && !executor_state.in_transaction && !explicit_transaction) {
      btree_begin_transaction();
      executor_state.in_transaction = true;
      auto_transaction = true;
    }

    // Build and execute program
    ArenaVector<VMInstruction, QueryArena> program = build_from_ast(statement);

    debug_print_program(program);
    if(statement->type == AST_UPDATE){
        _debug = true;
    }


    // if(statement->type == AST_CREATE_INDEX){
    //     _debug = true;
    // }

    VM_RESULT result = vm_execute(program);


    if (result != OK) {
      success = false;

      // Rollback on error
      if (executor_state.in_transaction) {
        btree_rollback();
        executor_state.in_transaction = false;
        rebuild_schema_from_master();
      }

      std::cout << "failed\n";
      exit(1);

      break;
    } else {
      // Process events immediately after each statement
      process_vm_events();

      // Auto-commit after write if we auto-began
      if (auto_transaction && is_write) {
        btree_commit();
        executor_state.in_transaction = false;
        auto_transaction = false;
      }
    }
  }
}
