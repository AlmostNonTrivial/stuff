// main.cpp - Simplified version
#include "arena.hpp"
#include "executor.hpp"
#include "schema.hpp"
#include "vm.hpp"
#include <vector>

// In main.cpp, replace print_result_callback with:
void print_result_callback(void* result, size_t result_size) {
    uint8_t* data = (uint8_t*)result;

    // Expected layout: id (4 bytes) + age (4 bytes) + name (32 bytes)
    // Total: 40 bytes

    // Extract id (first 4 bytes - this is the key)
    uint32_t id;
    memcpy(&id, data, sizeof(uint32_t));
    data += sizeof(uint32_t);

    // Extract age (next 4 bytes)
    uint32_t age;
    memcpy(&age, data, sizeof(uint32_t));
    data += sizeof(uint32_t);

    // Extract name (next 32 bytes)
    char name[33];  // +1 for null terminator
    memcpy(name, data, 32);
    name[32] = '\0';  // Ensure null termination

    // Clean up name (remove trailing nulls for display)
    for (int i = 31; i >= 0; i--) {
        if (name[i] != '\0' && name[i] != ' ') {
            name[i + 1] = '\0';
            break;
        }
    }

    printf("Row: id=%u, age=%u, name='%s'\n", id, age, name);
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
