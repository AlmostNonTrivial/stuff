// main.cpp - Enhanced fuzzer for complete btree coverage
#include "arena.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include "parser.hpp"
#include "vm.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <set>
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

  vm_init();
  arena_init(PAGE_SIZE * 10);
  // create_master_table();

  const char *begin = "BEGIN;";

   const char * insert_master2 =
   "INSERT INTO Master VALUES (1, 0, 'tablue', 1, 'CREATE TABLE tablue (INT "
   "id, "
   "INT type, VAR32 name, INT root, VARCHAR sql);');";


   const char * update_master=
   "UPDATE Master SET name = 'nike' WHERE id = 1;";


  const char *commit = "COMMIT;";
  const char *del= "DELETE FROM Master WHERE name = 'Master';";
  const char *select = "SELECT * FROM Master;";
  const char * count = "SELECT COUNT(*) FROM Master;";

  arena_reset();

  auto b = parse_sql(begin);
  // auto program = parse_sql(create_master);
  // auto insert = parse_sql(insert_master);
 auto insert2 = parse_sql(insert_master2) ;

  auto end = parse_sql(commit);

  vm_execute(b);
  // vm_execute(program);
  // vm_execute(insert);
  vm_execute(insert2);
 auto update = parse_sql(update_master);
 auto counta = parse_sql(count);
 auto d= parse_sql(del);
  vm_execute(update);

  // vm_execute(d);
  vm_execute(end);
  auto s = parse_sql(select); // NEEDs to be after when schema is cached
// vm_execute(s);
vm_execute(counta);

  arena_reset();

  arena_shutdown();
  return 0;
}
