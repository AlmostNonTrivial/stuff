
#include "executor.hpp"
#include "btree.hpp"
#include "parser.hpp"
#include "vm.hpp"
#include <vector>

void execute(const char *sql) {
  btree_init("db");
  arena_reset();

  std::vector<ASTNode *> statements;

  bool in_transaction = false;

  for (auto statement : statements) {
    std::vector<VMInstruction> program;
    VM_RESULT result = vm_execute(program);
    if(result != OK) {
        return;
    }

    auto queue = vm_events();

  }
}
