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

}
