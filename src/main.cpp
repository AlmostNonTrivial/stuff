// main.cpp - Enhanced fuzzer for complete btree coverage
#include "arena.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "pager.hpp"
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
  arena_init(PAGE_SIZE);














  arena_shutdown();
  return 0;
}
