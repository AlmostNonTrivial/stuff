// executor.cpp
#include "executor.hpp"
#include "arena.hpp"
#include "btree.hpp"
#include "parser.hpp"
#include "compile.hpp"
#include "schema.hpp"
#include "vm.hpp"
#include <cstring>
#include <cstdio>

static struct ExecutorState {
    bool initialized;
    bool in_transaction;
    bool master_table_exists;
    uint32_t next_master_id;
    uint32_t statements_executed;
    uint32_t transactions_completed;
} executor_state = {};

// ============================================================================
// Master Catalog Management
// ============================================================================

static void create_master_table() {
    if (_debug) {
        printf("EXECUTOR: Creating sqlite_master table\n");
    }

    // Create the master table schema
    Table *master = (Table *)arena::alloc<RegistryArena>(sizeof(Table));
    master->table_name = "sqlite_master";

    // Columns: id (key), type, name, tbl_name, rootpage, sql
    master->columns.push_back({"id", TYPE_4});
    master->columns.push_back({"type", TYPE_32});      // "table" or "index"
    master->columns.push_back({"name", TYPE_32});      // object name
    master->columns.push_back({"tbl_name", TYPE_32});  // table name (for indexes)
    master->columns.push_back({"rootpage", TYPE_4});   // root page of btree
    master->columns.push_back({"sql", TYPE_256});      // CREATE statement

    // Create BTree for master table
    RecordLayout layout = master->to_layout();
    uint32_t record_size = layout.record_size - TYPE_4;  // Subtract key size

    master->tree = btree_create(TYPE_4, record_size, BPLUS);

    // Add to schema registry
    add_table(master);

    executor_state.master_table_exists = true;
    executor_state.next_master_id = 1;

    if (_debug) {
        printf("EXECUTOR: sqlite_master table created\n");
    }
}

static const char* type_to_string(DataType type) {
    switch (type) {
        case TYPE_4: return "INT32";
        case TYPE_8: return "INT64";
        case TYPE_32: return "VARCHAR32";
        case TYPE_256: return "VARCHAR256";
        default: return "VARCHAR32";
    }
}

static char* generate_create_table_sql(const Table *table) {
    char* buffer = (char*)arena::alloc<QueryArena>(1024);
    int offset = snprintf(buffer, 1024, "CREATE TABLE %s (",
                         table->table_name.c_str());

    for (size_t i = 0; i < table->columns.size(); i++) {
        if (i > 0) {
            offset += snprintf(buffer + offset, 1024 - offset, ", ");
        }
        offset += snprintf(buffer + offset, 1024 - offset, "%s %s",
                          type_to_string(table->columns[i].type),
                          table->columns[i].name.c_str());
    }

    snprintf(buffer + offset, 1024 - offset, ")");
    return buffer;
}

static char* generate_create_index_sql(const char *index_name,
                                       const char *table_name,
                                       const char *column_name) {
    char* buffer = (char*)arena::alloc<QueryArena>(512);
    snprintf(buffer, 512, "CREATE INDEX %s ON %s (%s)",
            index_name, table_name, column_name);
    return buffer;
}

static void insert_master_entry(const char *type, const char *name,
                               const char *tbl_name, uint32_t rootpage,
                               const char *sql) {
    if (!executor_state.master_table_exists) {
        return;
    }

    if (_debug) {
        printf("EXECUTOR: Adding entry to sqlite_master: type=%s, name=%s\n",
               type, name);
    }

    // Build INSERT statement for master table
    char buffer[2048];
    snprintf(buffer, sizeof(buffer),
            "INSERT INTO sqlite_master VALUES (%u, '%s', '%s', '%s', %u, '%s')",
            executor_state.next_master_id++, type, name, tbl_name, rootpage,
            sql ? sql : "");

    // Parse and execute (without triggering recursive catalog updates)
    bool save_master = executor_state.master_table_exists;
    executor_state.master_table_exists = false;  // Temporarily disable

    Vec<ASTNode *, QueryArena> stmts = parse_sql(buffer);
    if (!stmts.empty()) {
        Vec<VMInstruction, QueryArena> program = build_from_ast(stmts[0]);
        vm_execute(program);
    }

    executor_state.master_table_exists = save_master;
}

static void update_master_rootpage(const char *name, uint32_t new_rootpage) {
    if (!executor_state.master_table_exists) {
        return;
    }

    if (_debug) {
        printf("EXECUTOR: Updating rootpage for '%s' to %u\n", name, new_rootpage);
    }

    char buffer[512];
    snprintf(buffer, sizeof(buffer),
            "UPDATE sqlite_master SET rootpage = %u WHERE name = '%s'",
            new_rootpage, name);

    bool save_master = executor_state.master_table_exists;
    executor_state.master_table_exists = false;

    Vec<ASTNode *, QueryArena> stmts = parse_sql(buffer);
    if (!stmts.empty()) {
        Vec<VMInstruction, QueryArena> program = build_from_ast(stmts[0]);
        vm_execute(program);
    }

    executor_state.master_table_exists = save_master;
}

static void delete_master_entry(const char *name) {
    if (!executor_state.master_table_exists) {
        return;
    }

    if (_debug) {
        printf("EXECUTOR: Removing entry for '%s' from sqlite_master\n", name);
    }

    char buffer[256];
    snprintf(buffer, sizeof(buffer),
            "DELETE FROM sqlite_master WHERE name = '%s' OR tbl_name = '%s'",
            name, name);

    bool save_master = executor_state.master_table_exists;
    executor_state.master_table_exists = false;

    Vec<ASTNode *, QueryArena> stmts = parse_sql(buffer);
    if (!stmts.empty()) {
        Vec<VMInstruction, QueryArena> program = build_from_ast(stmts[0]);
        vm_execute(program);
    }

    executor_state.master_table_exists = save_master;
}




static    Vec<Vec<TypedValue, QueryArena>, QueryArena> results;
static void load_schema_from_master() {
    if (!executor_state.master_table_exists) {
        return;
    }

    if (_debug) {
        printf("EXECUTOR: Loading schema from sqlite_master\n");
    }

    // Save and clear current schema (except master table)
    Table *master = get_table("sqlite_master");
    Table master_copy;
    if (master) {
        master_copy = *master;
    }

    clear_schema();

    // Restore master table
    if (master) {
        add_table(&master_copy);
    }

    // Query master table to rebuild schema
    const char *query = "SELECT * FROM sqlite_master ORDER BY id";

    // Set up result callback to capture rows

    ResultCallback old_callback = nullptr;
    auto capture_callback = [](Vec<TypedValue, QueryArena> row) {
        results.push_back(row);
    };

    vm_set_result_callback(capture_callback);

    Vec<ASTNode *, QueryArena> stmts = parse_sql(query);
    if (!stmts.empty()) {
        Vec<VMInstruction, QueryArena> program = build_from_ast(stmts[0]);
        vm_execute(program);
    }

    vm_set_result_callback(old_callback);

    // Process results to rebuild schema
    for (size_t i = 0; i < results.size(); i++) {
        auto &row = results[i];
        if (row.size() < 6) continue;

        uint32_t id = *(uint32_t *)row[0].data;
        const char *type = (const char *)row[1].data;
        const char *name = (const char *)row[2].data;
        const char *tbl_name = (const char *)row[3].data;
        uint32_t rootpage = *(uint32_t *)row[4].data;
        const char *sql = (const char *)row[5].data;

        // Update ID counter
        if (id >= executor_state.next_master_id) {
            executor_state.next_master_id = id + 1;
        }

        if (_debug) {
            printf("  Loading: type=%s, name=%s, rootpage=%u\n",
                   type, name, rootpage);
        }

        if (strcmp(type, "table") == 0 && strcmp(name, "sqlite_master") != 0) {
            // Parse CREATE TABLE to rebuild schema
            Vec<ASTNode *, QueryArena> create_stmts = parse_sql(sql);
            if (!create_stmts.empty() && create_stmts[0]->type == AST_CREATE_TABLE) {
                CreateTableNode *node = (CreateTableNode *)create_stmts[0];

                Table *table = (Table *)arena::alloc<RegistryArena>(sizeof(Table));
                table->table_name = name;

                for (size_t j = 0; j < node->columns.size(); j++) {
                    table->columns.push_back(node->columns[j]);
                }

                RecordLayout layout = table->to_layout();
                DataType key_type = table->columns[0].type;
                uint32_t record_size = layout.record_size - key_type;

                // Restore btree with existing root page
                table->tree = btree_create(key_type, record_size, BPLUS);
                table->tree.root_page_index = rootpage;

                add_table(table);
            }
        }
        else if (strcmp(type, "index") == 0) {
            // Rebuild index from metadata
            Table *table = get_table(tbl_name);
            if (table) {
                // Parse column name from SQL or index name
                // For simplicity, assume index name format: indexname_on_columnname
                Vec<ASTNode *, QueryArena> create_stmts = parse_sql(sql);
                if (!create_stmts.empty() && create_stmts[0]->type == AST_CREATE_INDEX) {
                    CreateIndexNode *node = (CreateIndexNode *)create_stmts[0];

                    uint32_t col_idx = get_column_index(tbl_name, node->column);
                    if (col_idx != UINT32_MAX) {
                        Index *index = (Index *)arena::alloc<RegistryArena>(sizeof(Index));
                        index->index_name = name;
                        index->column_index = col_idx;

                        DataType index_key = table->columns[col_idx].type;
                        DataType rowid_type = table->columns[0].type;

                        index->tree = btree_create(index_key, rowid_type, BTREE);
                        index->tree.root_page_index = rootpage;

                        add_index(tbl_name, index);
                    }
                }
            }
        }
    }

    if (_debug) {
        printf("EXECUTOR: Loaded %zu objects from sqlite_master\n", results.size());
    }

    results.clear();
}

// ============================================================================
// Initialize executor
// ============================================================================

static void init_executor() {
    if (_debug) {
        printf("EXECUTOR: Initializing executor\n");
    }

    executor_state.initialized = true;
    executor_state.in_transaction = false;
    executor_state.master_table_exists = false;
    executor_state.next_master_id = 1;
    executor_state.statements_executed = 0;
    executor_state.transactions_completed = 0;

    // Initialize arenas if needed
    arena::init<RegistryArena>(PAGE_SIZE * 10);

    // Check if master table exists
    Table *master = get_table("sqlite_master");
    if (!master) {
        create_master_table();
    } else {
        executor_state.master_table_exists = true;
        load_schema_from_master();
    }
}

// ============================================================================
// DDL Command Handlers (with catalog updates)
// ============================================================================

static VM_RESULT execute_create_table(CreateTableNode *node) {
    if (_debug) {
        printf("EXECUTOR: Creating table '%s'\n", node->table);
    }

    // Don't allow creating sqlite_master
    if (strcmp(node->table, "sqlite_master") == 0) {
        printf("Error: Cannot create sqlite_master table\n");
        return ERR;
    }

    // Build table structure
    Table *table = (Table *)arena::alloc<RegistryArena>(sizeof(Table));
    table->table_name = node->table;

    if (node->columns.empty()) {
        printf("Error: Table must have at least one column\n");
        return ERR;
    }

    for (size_t i = 0; i < node->columns.size(); i++) {
        table->columns.push_back(node->columns[i]);
    }

    // Calculate record layout
    RecordLayout layout = table->to_layout();
    DataType key_type = table->columns[0].type;
    uint32_t record_size = layout.record_size - key_type;

    // Create BTree
    table->tree = btree_create(key_type, record_size, BPLUS);

    if (table->tree.tree_type == INVALID) {
        printf("Error: Failed to create BTree for table\n");
        return ERR;
    }

    // Add to schema registry
    if (!add_table(table)) {
        printf("Error: Failed to register table '%s'\n", node->table);
        return ERR;
    }

    // Add to master catalog
    char *sql = generate_create_table_sql(table);
    insert_master_entry("table", node->table, node->table,
                       table->tree.root_page_index, sql);

    if (_debug) {
        printf("EXECUTOR: Table '%s' created successfully\n", node->table);
    }

    return OK;
}

static VM_RESULT execute_create_index(CreateIndexNode *node) {
    if (_debug) {
        printf("EXECUTOR: Creating index '%s' on %s(%s)\n",
               node->index_name, node->table, node->column);
    }

    Table *table = get_table(node->table);
    if (!table) {
        printf("Error: Table '%s' not found\n", node->table);
        return ERR;
    }

    uint32_t col_idx = get_column_index(node->table, node->column);
    if (col_idx == UINT32_MAX) {
        printf("Error: Column '%s' not found in table '%s'\n",
               node->column, node->table);
        return ERR;
    }

    if (col_idx == 0) {
        printf("Error: Cannot create index on primary key column\n");
        return ERR;
    }

    if (get_index(node->table, col_idx)) {
        printf("Error: Index already exists on column '%s'\n", node->column);
        return ERR;
    }

    // Create index structure
    Index *index = (Index *)arena::alloc<RegistryArena>(sizeof(Index));
    index->index_name = node->index_name;
    index->column_index = col_idx;

    DataType index_key_type = table->columns[col_idx].type;
    DataType rowid_type = table->columns[0].type;

    index->tree = btree_create(index_key_type, rowid_type, BTREE);

    if (index->tree.tree_type == INVALID) {
        printf("Error: Failed to create BTree for index\n");
        return ERR;
    }

    if (!add_index(node->table, index)) {
        printf("Error: Failed to register index\n");
        btree_clear(&index->tree);
        return ERR;
    }

    // Add to master catalog
    char *sql = generate_create_index_sql(node->index_name, node->table, node->column);
    insert_master_entry("index", node->index_name, node->table,
                       index->tree.root_page_index, sql);

    if (_debug) {
        printf("EXECUTOR: Index '%s' created successfully (unpopulated)\n",
               node->index_name);
    }

    return OK;
}

static VM_RESULT execute_drop_table(const char *table_name) {
    if (_debug) {
        printf("EXECUTOR: Dropping table '%s'\n", table_name);
    }

    if (strcmp(table_name, "sqlite_master") == 0) {
        printf("Error: Cannot drop sqlite_master table\n");
        return ERR;
    }

    Table *table = get_table(table_name);
    if (!table) {
        printf("Error: Table '%s' not found\n", table_name);
        return ERR;
    }

    // Clear btrees
    btree_clear(&table->tree);
    for (size_t i = 0; i < table->indexes.size(); i++) {
        btree_clear(&table->indexes[i].tree);
    }

    // Remove from master catalog
    delete_master_entry(table_name);

    // Remove from schema registry
    if (!remove_table(table_name)) {
        printf("Error: Failed to remove table from registry\n");
        return ERR;
    }

    if (_debug) {
        printf("EXECUTOR: Table '%s' dropped successfully\n", table_name);
    }

    return OK;
}

// ============================================================================
// Transaction Command Handlers
// ============================================================================

static VM_RESULT execute_begin() {
    if (executor_state.in_transaction) {
        printf("Error: Already in transaction\n");
        return ERR;
    }

    if (_debug) {
        printf("EXECUTOR: Beginning transaction\n");
    }

    btree_begin_transaction();
    executor_state.in_transaction = true;

    return OK;
}

static VM_RESULT execute_commit() {
    if (!executor_state.in_transaction) {
        printf("Error: Not in transaction\n");
        return ERR;
    }

    if (_debug) {
        printf("EXECUTOR: Committing transaction\n");
    }

    btree_commit();
    executor_state.in_transaction = false;
    executor_state.transactions_completed++;

    return OK;
}

static VM_RESULT execute_rollback() {
    if (!executor_state.in_transaction) {
        printf("Error: Not in transaction\n");
        return ERR;
    }

    if (_debug) {
        printf("EXECUTOR: Rolling back transaction\n");
    }

    btree_rollback();
    executor_state.in_transaction = false;

    // Reload schema from master table
    load_schema_from_master();

    return OK;
}

// ============================================================================
// Main Execute Function
// ============================================================================

void execute(const char *sql) {
    if (_debug) {
        printf("\n========================================\n");
        printf("EXECUTOR: Processing SQL: %s\n", sql);
        printf("========================================\n");
    }

    arena::reset<QueryArena>();

    if (!executor_state.initialized) {
        init_executor();
    }

    Vec<ASTNode *, QueryArena> statements = parse_sql(sql);

    if (statements.empty()) {
        printf("Error: Failed to parse SQL\n");
        return;
    }

    for (size_t i = 0; i < statements.size(); i++) {
        ASTNode *stmt = statements[i];
        VM_RESULT result = OK;

        switch (stmt->type) {
        case AST_CREATE_TABLE:
            result = execute_create_table((CreateTableNode *)stmt);
            break;

        case AST_CREATE_INDEX:
            result = execute_create_index((CreateIndexNode *)stmt);
            break;

        case AST_BEGIN:
            result = execute_begin();
            break;

        case AST_COMMIT:
            result = execute_commit();
            break;

        case AST_ROLLBACK:
            result = execute_rollback();
            break;

        case AST_SELECT:
        case AST_INSERT:
        case AST_UPDATE:
        case AST_DELETE: {
            bool auto_transaction = false;
            if (!executor_state.in_transaction && stmt->type != AST_SELECT) {
                execute_begin();
                auto_transaction = true;
            }

            Vec<VMInstruction, QueryArena> program = build_from_ast(stmt);
            result = vm_execute(program);

            if (auto_transaction) {
                if (result == OK) {
                    execute_commit();
                } else {
                    execute_rollback();
                }
            }
            break;
        }

        default:
            printf("Error: Unknown statement type\n");
            result = ERR;
        }

        if (result != OK) {
            if (executor_state.in_transaction) {
                execute_rollback();
            }
            break;
        }

        executor_state.statements_executed++;
    }
}
