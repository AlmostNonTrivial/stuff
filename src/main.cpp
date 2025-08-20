// main.cpp - Enhanced fuzzer for complete btree coverage
#include "arena.hpp"
#include "executor.hpp"
#include "schema.hpp"
#include "vm.hpp"
#include <vector>

/*

 SELECT * FROM X WHERE y > 4 AND/OR g == '' ORDER BY s ASC/DESC;
 SELECT COUNT/MIN ...(*) ..
 DELETE/UPDATE WHERE x and y
 INSERT INTO X VALUES (a,b,c)
 CREATE TABLE X (type (key)name, type name ...)
 CREATE INDEX ...

 BEGIN;
 COMMIT;
 ROLLBACK;

 */

// char * master_create_table() {
// char sql[512];
// snprintf(sql, sizeof(sql),
//     "INSERT INTO sqlite_master VALUES (%d, 0, '%s', %d, '%s');",
//     new_id, schema->table_name, root_page, create_sql);
// }
//
//
//
//
// In schema.hpp or a utility header
ArenaString<QueryArena> vm_values_to_string(
    const ArenaVector<VMValue, QueryArena>& values,
    const char* delimiter = ", ") {

    ArenaString<QueryArena> result;

    for (size_t i = 0; i < values.size(); i++) {
        const VMValue& val = values[i];

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
ArenaString<QueryArena> vm_values_to_row_string(
    const ArenaVector<VMValue, QueryArena>& values) {

    ArenaString<QueryArena> result("(");
    result.append(vm_values_to_string(values, ", ").c_str());
    result.append(")");
    return result;
}

void print_buf(ArenaVector<ArenaVector<VMValue, QueryArena>, QueryArena> buf){
    std::cout << "RESULTS =========================================\n";
    for (auto &row : buf) {
    std::cout << vm_values_to_row_string(row).c_str();
      std::cout << "\n";
    }


    std::cout << "=================================================\n";
}


/* NOTES

- can we pretty print the schema?
- what is the point of the rewind? Could we just do first and last?
- can we print the begining statement
- because we can have duplicates in index, need to match
both key and record
- make sure seek_lt, seek_gt works with jump
- I think it's the case that seek eq works with next, because the tree
scans left to right
- maybe we can create a err opcode that is passed halt
 */

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
      "SELECT * FROM X WHERE name = 'ricksmart",
      // "SELECT * FROM X;"
      // "SELECT COUNT(*) FROM X;",

  };

  for (auto query : queries) {
    execute(query);
    auto output = vm_output_buffer();
    print_buf(output);
    std::cout << '\n';
  }
}
