#include "executor.hpp"
#include "btree.hpp"
#include "parser.hpp"
#include "vm.hpp"
#include "schema.hpp"
#include <vector>
#include <unordered_map>

struct PendingSchemaChange {
    enum Type { ADD_TABLE, DROP_TABLE, ADD_INDEX, DROP_INDEX };
    Type type;
    union {
        Table* table;
        struct {
            const char* table_name;
            Index* index;
        } index_data;
        const char* drop_name;
    } data;
    uint32_t column_index; // For index operations
};

static struct ExecutorState {
    bool in_transaction;
    std::vector<PendingSchemaChange> pending_changes;
    std::unordered_map<std::string, Table> table_backups;
    bool initialized;
} executor_state = {};

static void init_executor() {
    executor_state.initialized = true;
    executor_state.in_transaction = false;
    executor_state.pending_changes.clear();
    executor_state.table_backups.clear();
}

static void apply_pending_changes() {
    for (const auto& change : executor_state.pending_changes) {
        switch (change.type) {
        case PendingSchemaChange::ADD_TABLE:
            add_table(change.data.table);
            break;
        case PendingSchemaChange::DROP_TABLE:
            remove_table(change.data.drop_name);
            break;
        case PendingSchemaChange::ADD_INDEX:
            add_index(change.data.index_data.table_name, change.data.index_data.index);
            break;
        case PendingSchemaChange::DROP_INDEX:
            remove_index(change.data.drop_name, change.column_index);
            break;
        }
    }
    executor_state.pending_changes.clear();
}

static void rollback_schema_changes() {
    // Restore backed up tables
    for (const auto& [name, table] : executor_state.table_backups) {
        add_table(const_cast<Table*>(&table));
    }
    executor_state.table_backups.clear();
    executor_state.pending_changes.clear();
}

static void backup_table(const char* table_name) {
    Table* table = get_table(table_name);
    if (table) {
        executor_state.table_backups[table_name] = *table;
    }
}

static void process_vm_events(bool commit_changes) {
    auto& events = vm_events();

    while (!events.empty()) {
        VmEvent event = events.front();
        events.pop();

        switch (event.type) {
        case EVT_TABLE_CREATED:
            if (executor_state.in_transaction) {
                PendingSchemaChange change;
                change.type = PendingSchemaChange::ADD_TABLE;
                change.data.table = (Table*)event.data;
                executor_state.pending_changes.push_back(change);
            } else if (commit_changes) {
                add_table((Table*)event.data);
            }
            break;

        case EVT_TABLE_DROPPED:
            if (executor_state.in_transaction) {
                backup_table(event.context.table_info.table_name);
                PendingSchemaChange change;
                change.type = PendingSchemaChange::DROP_TABLE;
                change.data.drop_name = event.context.table_info.table_name;
                executor_state.pending_changes.push_back(change);
            } else if (commit_changes) {
                remove_table(event.context.table_info.table_name);
            }
            break;

        case EVT_INDEX_CREATED:
            if (executor_state.in_transaction) {
                PendingSchemaChange change;
                change.type = PendingSchemaChange::ADD_INDEX;
                change.data.index_data.table_name = event.context.index_info.table_name;
                change.data.index_data.index = (Index*)event.data;
                change.column_index = event.context.index_info.column_index;
                executor_state.pending_changes.push_back(change);
            } else if (commit_changes) {
                add_index(event.context.index_info.table_name, (Index*)event.data);
            }
            break;

        case EVT_INDEX_DROPPED:
            if (executor_state.in_transaction) {
                backup_table(event.context.index_info.table_name);
                PendingSchemaChange change;
                change.type = PendingSchemaChange::DROP_INDEX;
                change.data.drop_name = event.context.index_info.table_name;
                change.column_index = event.context.index_info.column_index;
                executor_state.pending_changes.push_back(change);
            } else if (commit_changes) {
                remove_index(event.context.index_info.table_name,
                           event.context.index_info.column_index);
            }
            break;

        case EVT_TRANSACTION_BEGIN:
            executor_state.in_transaction = true;
            break;

        case EVT_TRANSACTION_COMMIT:
            if (executor_state.in_transaction) {
                apply_pending_changes();
                executor_state.in_transaction = false;
                executor_state.table_backups.clear();
            }
            break;

        case EVT_TRANSACTION_ROLLBACK:
            if (executor_state.in_transaction) {
                rollback_schema_changes();
                executor_state.in_transaction = false;
            }
            break;

        default:
            break;
        }
    }
}



void execute(const char* sql) {
    if (!executor_state.initialized) {
        init_executor();
    }
    arena_reset();

    std::vector<ASTNode*> statements = parse_sql(sql);

    bool success = true;
    bool explicit_transaction = false;


    for (auto statement : statements) {
        bool read = false;
        // Check for explicit transaction commands
        if (statement->type == AST_BEGIN) {
            explicit_transaction = true;
        } else if (statement->type == AST_COMMIT || statement->type == AST_ROLLBACK) {
            explicit_transaction = false;
        } else if (statement->type == AST_SELECT) {
           read = true;
        }

        if(!explicit_transaction && !executor_state.in_transaction && !read) {
            btree_begin_transaction();
        }

        // Build program from AST
        std::vector<VMInstruction> program = build_from_ast(statement);

        // Execute program
        VM_RESULT result = vm_execute(program);

        // Process events immediately
        bool should_commit = (result == OK && !executor_state.in_transaction);
        process_vm_events(should_commit);

        if (result != OK) {
            success = false;

            // Auto-rollback if in transaction and not explicit
            if (executor_state.in_transaction && !explicit_transaction) {
                btree_rollback();
                rollback_schema_changes();
                executor_state.in_transaction = false;
            }

            break; // Stop executing further statements
        }

        // Clear VM events after processing
        vm_clear_events();
    }

    // Reset arena after all statements complete
    arena_reset();
}
