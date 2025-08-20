// main.cpp - Enhanced fuzzer for complete btree coverage
#include "arena.hpp"
#include "executor.hpp"
#include "schema.hpp"
#include "vm.hpp"
#include <vector>

// Global output buffer for capturing query results
static Vector<Vector<TypedValue, QueryArena>, QueryArena> output_buffer;

// Callback function to capture VM output
void capture_output_callback(void* result, size_t result_size) {
    // We need to know the schema to properly parse the result
    // For now, we'll do a generic parse based on common patterns

    // This is a simplified parser - you'll need to adapt based on your actual needs
    uint8_t* data = (uint8_t*)result;
    Vector<TypedValue, QueryArena> row;

    // Parse the record based on the current query context
    // This is where you'd need to track what table/schema is being queried
    // For demonstration, let's handle some common cases:

    // You'll need to implement proper parsing based on the active schema
    // For now, this is a placeholder that shows the structure

    output_buffer.push_back(row);
}

// Helper function to parse a record based on a known schema
Vector<TypedValue, QueryArena> parse_record(void* result, TableSchema* schema) {
    Vector<TypedValue, QueryArena> row;
    uint8_t* data = (uint8_t*)result;

    // Parse each column according to schema
    for (size_t i = 0; i < schema->columns.size(); i++) {
        TypedValue val;
        val.type = schema->columns[i].type;

        if (i == 0) {
            // Key is handled separately (not in record)
            continue;
        }

        size_t offset = schema->column_offsets[i];
        size_t size = schema->columns[i].type;

        val.data = (uint8_t*)arena::alloc<QueryArena>(size);
        memcpy(val.data, data + offset, size);
        row.push_back(val);
    }

    return row;
}

// Improved callback that knows about table context
struct OutputCapture {
    static TableSchema* current_schema;
    static bool capture_enabled;

    static void callback(void* result, size_t result_size) {
        if (!capture_enabled) return;

        if (!current_schema) {
            // Generic parse - just store raw data
            Vector<TypedValue, QueryArena> row;
            TypedValue val;
            val.type = TYPE_VARCHAR256;
            val.data = (uint8_t*)result;
            row.push_back(val);
            output_buffer.push_back(row);
        } else {
            // Parse according to schema
            output_buffer.push_back(parse_record(result, current_schema));
        }
    }

    static void enable(TableSchema* schema = nullptr) {
        current_schema = schema;
        capture_enabled = true;
        output_buffer.clear();
        vm_set_result_callback(callback);
    }

    static void disable() {
        capture_enabled = false;
        vm_set_result_callback(nullptr);
    }
};

TableSchema* OutputCapture::current_schema = nullptr;
bool OutputCapture::capture_enabled = false;

Str<QueryArena> vm_values_to_string(
    const Vector<TypedValue, QueryArena>& values,
    const char* delimiter = ", ") {

    Str<QueryArena> result;

    for (size_t i = 0; i < values.size(); i++) {
        const TypedValue& val = values[i];

        if (i > 0 && delimiter) {
            result.append(delimiter);
        }

        switch (val.type) {
            case TYPE_UINT32: {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%u", *(uint32_t*)val.data);
                result.append(buffer);
                break;
            }

            case TYPE_UINT64: {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%lu", *(uint64_t*)val.data);
                result.append(buffer);
                break;
            }

            case TYPE_VARCHAR32:
            case TYPE_VARCHAR256: {
                // Add quotes for string values
                result.append("'");

                // Append string data, handling null termination
                size_t max_len = (val.type == TYPE_VARCHAR32) ? 32 : 256;
                for (size_t j = 0; j < max_len && val.data[j] != 0; j++) {
                    result.append(val.data[j]);
                }

                result.append("'");
                break;
            }

            case TYPE_NULL:
                result.append("NULL");
                break;

            default:
                result.append("<unknown>");
                break;
        }
    }

    return result;
}

Str<QueryArena> vm_values_to_row_string(
    const Vector<TypedValue, QueryArena>& values) {

    Str<QueryArena> result("(");
    result.append(vm_values_to_string(values, ", ").c_str());
    result.append(")");
    return result;
}

void print_buf(Vector<Vector<TypedValue, QueryArena>, QueryArena> buf){
    std::cout << "RESULTS =========================================\n";
    for (auto &row : buf) {
        std::cout << vm_values_to_row_string(row).c_str();
        std::cout << "\n";
    }
    std::cout << "=================================================\n";
}

// Execute with output capture
void execute_with_output(const char* sql) {
    // Parse to check if it's a SELECT query
    Vector<ASTNode*, QueryArena> stmts = parse_sql(sql);

    TableSchema* schema = nullptr;

    // Check if it's a SELECT and get the table schema
    if (!stmts.empty() && stmts[0]->type == AST_SELECT) {
        SelectNode* select = (SelectNode*)stmts[0];
        Table* table = get_table(select->table);
        if (table) {
            schema = &table->schema;
        }
    }

    // Enable output capture for SELECT queries
    if (!stmts.empty() && stmts[0]->type == AST_SELECT) {
        OutputCapture::enable(schema);
    }

    execute(sql);

    // Disable capture after execution
    if (!stmts.empty() && stmts[0]->type == AST_SELECT) {
        OutputCapture::disable();
    }
}

int main() {

    arena::init<QueryArena>(PAGE_SIZE * 30);
    arena::init<SchemaArena>(PAGE_SIZE * 14);
    btree_init("db");

    std::vector<const char *> queries = {
        "BEGIN; CREATE TABLE X (INT id, INT age, VARCHAR32 name); COMMIT;",
        // "BEGIN; CREATE TABLE Y (INT id, INT age, VARCHAR32 name); COMMIT;",
        // "BEGIN; INSERT INTO Y VALUES (1, 16, 'rickstar'); COMMIT;",
        // "SELECT * FROM sqlite_master;",
        "BEGIN; INSERT INTO X VALUES (1, 18, 'ricky'); COMMIT;",
        "BEGIN; CREATE INDEX index_x_name ON X (name);COMMIT;",
        "BEGIN; INSERT INTO X VALUES (2, 22, 'marky'); COMMIT;",
        // "BEGIN; INSERT INTO X VALUES (3, 16, 'marshal'); COMMIT;",
        // "SELECT * FROM sqlite_master;",
        // "SELECT * FROM Y;",
        // "BEGIN; DELETE FROM X WHERE name = 'ricky';COMMIT;",

        // "SELECT * FROM X;",
        "BEGIN; UPDATE X SET name = 'ricksmart' WHERE name = 'ricky';COMMIT;",
        "SELECT * FROM X WHERE name = 'ricksmart'",  // Fixed: added closing quote
        // "SELECT * FROM X;"
        // "SELECT COUNT(*) FROM X;",
    };

    for (auto query : queries) {
        output_buffer.clear();  // Clear buffer before each query
        execute_with_output(query);

        // Only print if we captured output (from SELECT queries)
        if (!output_buffer.empty()) {
            print_buf(output_buffer);
        }
        std::cout << '\n';
    }

    return 0;
}
