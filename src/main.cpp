// main.cpp - Simplified version
#include "arena.hpp"
#include "executor.hpp"
#include "schema.hpp"
#include "vm.hpp"
#include <vector>

// Simple callback that just prints raw results
void print_result_callback(void* result, size_t result_size) {

    // Just print the raw bytes as hex for now
    uint8_t* data = (uint8_t*)result;
    printf("Result (%zu bytes): ", result_size);

    // Print first few bytes as hex
    for (size_t i = 0; i < result_size && i < 32; i++) {
        printf("%02x ", data[i]);
    }
    printf("\n");
}

int main() {
    arena::init<QueryArena>(PAGE_SIZE * 30);
    arena::init<SchemaArena>(PAGE_SIZE * 14);
    btree_init("db");

    std::vector<const char *> queries = {
        "BEGIN; CREATE TABLE X (INT id, INT age, VARCHAR32 name); COMMIT;",
        "BEGIN; INSERT INTO X VALUES (1, 18, 'ricky'); COMMIT;",
        // "BEGIN; CREATE INDEX index_x_name ON X (name);COMMIT;",
        "BEGIN; INSERT INTO X VALUES (2, 22, 'marky'); COMMIT;",
        "BEGIN; UPDATE X SET name = 'ricksmart' WHERE name = 'ricky';COMMIT;",
        "SELECT * FROM X;",
    };

    for (auto query : queries) {
        printf("\nExecuting: %s\n", query);

        // Set callback for SELECT queries
        if (strstr(query, "SELECT")) {
            vm_set_result_callback(print_result_callback);
        }

        execute(query);

        // Clear callback after execution
        vm_set_result_callback(nullptr);
    }

    return 0;
}
