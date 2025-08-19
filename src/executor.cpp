// executor.cpp
#include "executor.hpp"
#include "arena.hpp"
#include "btree.hpp"
#include "parser.hpp"
#include "vm.hpp"
#include "schema.hpp"



static struct ExecutorState {
    bool initialized;
    bool in_transaction;
    bool master_table_exists;
    uint32_t next_master_id;  // Sequential ID counter
} executor_state = {};

// Master table helpers
static const char* type_to_string(DataType type) {
    switch (type) {
        case TYPE_UINT32: return "INT32";
        case TYPE_UINT64: return "INT64";
        case TYPE_VARCHAR32: return "VARCHAR32";
        case TYPE_VARCHAR256: return "VARCHAR256";
        default: return "VARCHAR32";
    }
}

static char* generate_create_sql(const TableSchema* schema) {
    ArenaString<QueryArena> sql("CREATE TABLE");
    //  sql .append(schema->table_name.c_str());
    //  sql.append(" (");
    // for (size_t i = 0; i < schema->columns.size(); i++) {
    //     if (i > 0) sql += ", ";
    //     sql += type_to_string(schema->columns[i].type);
    //     sql += " ";
    //     sql += schema->columns[i].name;
    // }
    // sql += ")";

    // char* result = (char*)arena::alloc<QueryArena>(sql.size() + 1);
    // strcpy(result, sql.c_str());
    // return result;
    std::cout << "not implemented";
    exit(1);
    return "";
}

static char* generate_index_sql(const char* table_name, uint32_t column_index) {
    // Table* table = get_table(table_name);
    // if (!table || column_index >= table->schema.columns.size()) {
    //     return nullptr;
    // }

    // std::string sql = "CREATE INDEX idx_" + std::string(table_name) + "_" +
    //                  table->schema.columns[column_index].name +
    //                  " ON " + table_name + " (" +
    //                  table->schema.columns[column_index].name + ")";

    // char* result = (char*)arena::alloc<QueryArena>(sql.size() + 1);
    // strcpy(result, sql.c_str());
    // return result;
    std::cout << "not implemented";
    exit(1);
    return "";
}

// Execute SQL through VM without triggering events
static VM_RESULT execute_internal(const char* sql) {
    ArenaVector<ASTNode*, QueryArena> stmts = parse_sql(sql);
    if (stmts.empty()) return ERR;

    ArenaVector<VMInstruction, QueryArena> program = build_from_ast(stmts[0]);
    VM_RESULT result = vm_execute(program);

    // Clear events from internal operations
    vm_clear_events();
    return result;
}

static void insert_master_table_entry(const char* type, const char* name,
                                     const char* tbl_name, uint32_t rootpage,
                                     const char* sql) {
    if (!executor_state.master_table_exists) return;

    char buffer[2048];
    snprintf(buffer, sizeof(buffer),
        "INSERT INTO sqlite_master VALUES (%u, '%s', '%s', '%s', %u, '%s')",
        executor_state.next_master_id++, type, name, tbl_name, rootpage, sql);

    execute_internal(buffer);
}

static void delete_master_table_entry(const char* name) {
    if (!executor_state.master_table_exists) return;

    char buffer[256];
    snprintf(buffer, sizeof(buffer),
        "DELETE FROM sqlite_master WHERE name = '%s'", name);

    execute_internal(buffer);
}

static void create_master_table() {
    // Create the master table schema
    TableSchema* schema = (TableSchema*)arena::alloc<QueryArena>(sizeof(TableSchema));
    schema->table_name = "sqlite_master";

    // Columns: id (key), type, name, tbl_name, rootpage, sql
    schema->columns.push_back({"id", TYPE_UINT32});       // Key column must be first
    schema->columns.push_back({"type", TYPE_VARCHAR32});
    schema->columns.push_back({"name", TYPE_VARCHAR32});
    schema->columns.push_back({"tbl_name", TYPE_VARCHAR32});
    schema->columns.push_back({"rootpage", TYPE_UINT32});
    schema->columns.push_back({"sql", TYPE_VARCHAR256});

    calculate_column_offsets(schema);

    // Create table directly
    Table* master = (Table*)arena::alloc<QueryArena>(sizeof(Table));
    master->schema = *schema;
    master->tree = btree_create(schema->key_type(), schema->record_size, BPLUS);

    add_table(master);
    executor_state.master_table_exists = true;
    executor_state.next_master_id = 1;  // Initialize counter
}

static void rebuild_schema_from_master() {
    if (!executor_state.master_table_exists) return;

    // Save master table before clearing
    Table* master = get_table("sqlite_master");
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

    // Reset the ID counter to max(id) + 1
    executor_state.next_master_id = 1;

    // First pass: find max ID
    const char* max_query = "SELECT * FROM sqlite_master";
    ArenaVector<ASTNode*, QueryArena> max_stmts = parse_sql(max_query);
    if (!max_stmts.empty()) {
        ArenaVector<VMInstruction, QueryArena> max_program = build_from_ast(max_stmts[0]);
        vm_execute(max_program);

        for (auto& row : vm_output_buffer()) {
            if (row.size() >= 6) {
                uint32_t id = *(uint32_t*)row[0].data;
                if (id >= executor_state.next_master_id) {
                    executor_state.next_master_id = id + 1;
                }
            }
        }
        vm_clear_events();
    }

    // Query master table to rebuild schema - tables first
    const char* query = "SELECT * FROM sqlite_master WHERE type = 'table' AND name != 'sqlite_master'";
    ArenaVector<ASTNode*, QueryArena> stmts = parse_sql(query);
    if (stmts.empty()) return;

    ArenaVector<VMInstruction, QueryArena> program = build_from_ast(stmts[0]);

    // Save current output buffer
    auto saved_buffer = vm_output_buffer();

    // Execute query
    vm_execute(program);

    // Process results to rebuild tables
    for (auto& row : vm_output_buffer()) {
        if (row.size() < 6) continue;

        // row[0] is id
        const char* type = (const char*)row[1].data;
        const char* name = (const char*)row[2].data;
        const char* tbl_name = (const char*)row[3].data;
        uint32_t rootpage = *(uint32_t*)row[4].data;
        const char* sql = (const char*)row[5].data;

        if (strcmp(type, "table") == 0) {
            // Parse CREATE TABLE to rebuild schema
            ArenaVector<ASTNode*, QueryArena> create_stmts = parse_sql(sql);
            if (!create_stmts.empty() && create_stmts[0]->type == AST_CREATE_TABLE) {
                CreateTableNode* node = (CreateTableNode*)create_stmts[0];

                Table* table = (Table*)arena::alloc<QueryArena>(sizeof(Table));
                table->schema.table_name = name;
                for(int i = 0; i < node->columns.size(); i++) {
                    table->schema.columns[i] = node->columns[i];
                }

                calculate_column_offsets(&table->schema);

                // Restore btree with existing root page
                table->tree = btree_create(
                    table->schema.key_type(),
                    table->schema.record_size,
                    BPLUS
                );
                table->tree.root_page_index = rootpage;

                add_table(table);
            }
        }
    }

    // Now rebuild indexes
    query = "SELECT * FROM sqlite_master WHERE type = 'index'";
    stmts = parse_sql(query);
    if (!stmts.empty()) {
        program = build_from_ast(stmts[0]);
        vm_execute(program);

        for (auto& row : vm_output_buffer()) {
            if (row.size() < 6) continue;

            // row[0] is id
            const char* type = (const char*)row[1].data;
            const char* name = (const char*)row[2].data;
            const char* tbl_name = (const char*)row[3].data;
            uint32_t rootpage = *(uint32_t*)row[4].data;

            if (strcmp(type, "index") == 0) {
                Table* table = get_table(tbl_name);
                if (table) {
                    // Parse index name to get column (simplified)
                    // Real implementation would parse the SQL
                    for (uint32_t i = 1; i < table->schema.columns.size(); i++) {
                        Index* index = (Index*)arena::alloc<QueryArena>(sizeof(Index));
                        index->column_index = i;
                        index->tree = btree_create(
                            table->schema.columns[i].type,
                            table->schema.key_type(),
                            BTREE
                        );
                        index->tree.root_page_index = rootpage;
                        add_index(tbl_name, index);
                        break; // Simplified - just add first index
                    }
                }
            }
        }
    }

    vm_clear_events();
}

static void process_vm_events() {
    auto& events = vm_events();

    while (!events.empty()) {
        VmEvent event = events.front();
        events.pop();

        switch (event.type) {
        case EVT_TABLE_CREATED: {
            Table* table = (Table*)event.data;

            // Apply immediately to in-memory schema
            add_table(table);

            // Insert into master table if not the master table itself
            // if (executor_state.master_table_exists &&
            //     table->schema.table_name != "sqlite_master") {
            //     char* sql = generate_create_sql(&table->schema);
            //     insert_master_table_entry("table",
            //         table->schema.table_name.c_str(),
            //         table->schema.table_name.c_str(),
            //         table->tree.root_page_index,
            //         sql);
            // }
            break;
        }

        // case EVT_TABLE_DROPPED: {
        //     const char* table_name = event.context.table_info.table_name;

        //     // Remove from in-memory schema
        //     remove_table(table_name);

        //     // Delete from master table
        //     delete_master_table_entry(table_name);
        //     break;
        // }

        // case EVT_INDEX_CREATED: {
        //     Index* index = (Index*)event.data;
        //     const char* table_name = event.context.index_info.table_name;

        //     // Apply immediately
        //     add_index(table_name, index);

        //     // Insert into master table
        //     if (executor_state.master_table_exists) {
        //         char* sql = generate_index_sql(table_name, index->column_index);
        //         char index_name[128];
        //         snprintf(index_name, sizeof(index_name), "idx_%s_%u",
        //                 table_name, index->column_index);

        //         insert_master_table_entry("index",
        //             index_name,
        //             table_name,
        //             index->tree.root_page_index,
        //             sql);
        //     }
        //     break;
        // }

        // case EVT_INDEX_DROPPED: {
        //     const char* table_name = event.context.index_info.table_name;
        //     uint32_t column_index = event.context.index_info.column_index;

        //     // Remove from in-memory schema
        //     remove_index(table_name, column_index);

        //     // Delete from master table
        //     char index_name[128];
        //     snprintf(index_name, sizeof(index_name), "idx_%s_%u",
        //             table_name, column_index);
        //     delete_master_table_entry(index_name);
        //     break;
        // }

        case EVT_BTREE_ROOT_CHANGED: {
            int i =0;
            // Update root page in master table if needed
            // This would require an UPDATE statement
            break;
        }

        case EVT_TRANSACTION_BEGIN:
            executor_state.in_transaction = true;
            break;

        case EVT_TRANSACTION_COMMIT:
            executor_state.in_transaction = false;
            break;

        case EVT_TRANSACTION_ROLLBACK:
            executor_state.in_transaction = false;
                    // rebuild_schema_from_master();
            // Schema will be rebuilt after btree rollback
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

    // Create master table if it doesn't exist
    // if (!get_table("sqlite_master")) {
    // create_master_table();
    // }
}

void execute(const char* sql) {
    arena::reset<QueryArena>();
    if (!executor_state.initialized) {
        init_executor();
    }

    ArenaVector<ASTNode*, QueryArena> statements = parse_sql(sql);




    bool success = true;
    bool explicit_transaction = false;

    for (int i = 0; i < statements.size();i++) {
        auto statement = statements[i];
        bool is_read = (statement->type == AST_SELECT);

        // Check for explicit transaction commands
        if (statement->type == AST_BEGIN) {
            explicit_transaction = true;
            btree_begin_transaction();
            executor_state.in_transaction = true;
        } else if (statement->type == AST_COMMIT) {
            if (explicit_transaction) {
                btree_commit();
            executor_state.in_transaction = false;
                explicit_transaction = false;
            }
        } else if (statement->type == AST_ROLLBACK) {
            if (explicit_transaction) {
                btree_rollback();
                rebuild_schema_from_master();
                executor_state.in_transaction = false;
                explicit_transaction = false;
            }
        } else {
            // Auto-transaction for write operations
            if (!explicit_transaction && !executor_state.in_transaction && !is_read) {
                btree_begin_transaction();
                executor_state.in_transaction = true;
            }

            // Build and execute program
            ArenaVector<VMInstruction, QueryArena> program = build_from_ast(statement);



            std::cout << program.size() << ", ";



            VM_RESULT result = vm_execute(program);

            if (result != OK) {
                success = false;

                // Rollback on error
                if (executor_state.in_transaction) {
                    btree_rollback();
                    rebuild_schema_from_master();
                    executor_state.in_transaction = false;
                }

                break; // Stop executing further statements
            } else {
                // Process events immediately on success
               process_vm_events();

               if(is_read) {
                   auto buffer = vm_output_buffer();
               }

                // Auto-commit for non-explicit transactions
                if (!explicit_transaction && executor_state.in_transaction && !is_read) {
                    btree_commit();
                    executor_state.in_transaction = false;
                }
            }
        }

        // Clear VM events after processing
        vm_clear_events();
    }

    // Reset arena after all statements complete
    arena::reset<QueryArena>();
}
