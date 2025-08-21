// main.cpp - Simplified version
#include "arena.hpp"
#include "compile.hpp"
#include "defs.hpp"
#include "executor.hpp"
#include "schema.hpp"
#include "vm.hpp"
#include <vector>

// In main.cpp, replace print_result_callback with:
void print_result_callback(Vec<TypedValue, QueryArena> result) {
  for (int i = 0; i < result.size(); i++) {
    print_value(result[i].type, result[i].data);
    if (i != result.size() - 1) {
      std::cout << ", ";
    }
  }
  std::cout << "\n";
}

void test() {

  vm_set_result_callback(print_result_callback);

  auto prog = build_from_ast(nullptr);
  vm_execute(prog);
}

int main() {
    _debug = true;
  arena::init<QueryArena>(PAGE_SIZE * 30);
  arena::init<RegistryArena>(PAGE_SIZE * 14);
  init_type_ops();
  btree_init("db");

  test();

  return 0;

  std::vector<const char *> queries = {
      "BEGIN; CREATE TABLE X (INT id, INT age, VARCHAR32 name); COMMIT;",

      "BEGIN; INSERT INTO X VALUES (2, 18, 'ricky'); COMMIT;",
      "BEGIN; INSERT INTO X VALUES (1, 22, 'marky'); COMMIT;",
      // "BEGIN; CREATE INDEX index_x_name ON X (name);COMMIT;",
      // "BEGIN; UPDATE X SET name = 'ricksmart' WHERE name = 'ricky';COMMIT;",
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
