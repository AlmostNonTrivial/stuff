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

int main() {

  arena::init<QueryArena>(PAGE_SIZE * 20);
  arena::init<SchemaArena>(PAGE_SIZE * 10);
  btree_init("db");

  std::vector<const char *> queries = {
      "BEGIN; CREATE TABLE X (INT id, INT age, VARCHAR32 name); COMMIT;",
      "BEGIN; INSERT INTO X VALUES (1, 18, 'ricky'); COMMIT;",
      "BEGIN; INSERT INTO X VALUES (2, 22, 'marky'); COMMIT;",
      "BEGIN; INSERT INTO X VALUES (3, 16, 'marshal'); COMMIT;",
      "SELECT * FROM X;"};

  for (auto query : queries) {
    execute(query);
  }

}
