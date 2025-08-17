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

int main() {

  vm_init();
  arena_init(PAGE_SIZE * 10);

  const char *begin = "BEGIN;";

  const char *create_master = "CREATE TABLE Master (INT id, INT type, VAR32 "
                              "name, INT root, VARCHAR sql);";

  const char *insert_master =
      "INSERT INTO Master VALUES (0, 0, 'Master', 1, 'CREATE TABLE Master (INT "
      "id, "
      "INT type, VAR32 name, INT root, VARCHAR sql);');";

  const char *commit = "COMMIT;";

  const char *select = "SELECT * FROM Master;";

  arena_reset();

  auto b = parse_sql(begin);
  auto program = parse_sql(create_master);
  auto insert = parse_sql(insert_master);
  auto end = parse_sql(commit);

  vm_execute(b);
  vm_execute(program);
  vm_execute(insert);
  vm_execute(end);
  auto s = parse_sql(select); // NEEDs to be after when schema is cached
vm_execute(s);

  arena_reset();

  arena_shutdown();
  return 0;
}
