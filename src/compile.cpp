// programbuilder.cpp - Simplified version with full table scans only
#include "compile.hpp"
#include "arena.hpp"
#include "defs.hpp"
#include "schema.hpp"
#include "vm.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>

// Register allocator with named registers for debugging
struct RegisterAllocator {
  Vec<std::pair<Str<QueryArena>, uint32_t>, QueryArena, REGISTERS>
      name_to_register;

  // we need array allocation to be contiguous, so for simplicity just increment
  // them
  int get(const char *name) {
    auto it = name_to_register.find_with(
        [name](const std::pair<Str<QueryArena>, uint32_t> entry) {
          return entry.first.starts_with(name);
        });

    if (it != -1) {
      return name_to_register[it].second;
    }

    std::cout << "out of registers\n";
    exit(1);
  }

  void free(const char *name) {}

  void clear() { name_to_register.clear(); }
};

struct ProgramBuilder {
  Vec<VMInstruction, QueryArena> instructions;
  Vec<std::pair<Str<QueryArena>, int>, QueryArena> labels;
  RegisterAllocator regs;

  // Fluent interface
  ProgramBuilder &emit(VMInstruction inst) {
    instructions.push_back(inst);
    return *this;
  }

  ProgramBuilder &label(const char *name) {
    labels.push_back(std::make_pair(name, instructions.size()));
    return *this;
  }

  int here() { return instructions.size(); }

  void resolve_labels() {
    for (size_t i = 0; i < instructions.size(); i++) {
      auto &inst = instructions[i];

      if (!inst.p4 && inst.p3 == -1) {
        continue;
      }
      char *label = (char *)inst.p4;

      auto it = labels.find_with(
          [label](const std::pair<Str<QueryArena>, uint32_t> entry) {
            return entry.first.starts_with(label);
          });

      if (it == -1) {
        continue;
      }
      inst.p3 = it;
      inst.p4 = nullptr;
    }
  }
};

// ===================================================================================

// Helper to resolve labels to addresses

// // Extract WHERE conditions from AST
// static Vec<WhereCondition, QueryArena>
// extract_where_conditions(WhereNode *where, const char *table_name) {
//   Vec<WhereCondition, QueryArena> conditions;
//   if (!where || !where->condition)
//     return conditions;

//   // Simple traversal for AND-only conditions
//   std::function<void(ASTNode *)> extract = [&](ASTNode *node) {
//     if (!node)
//       return;

//     if (node->type == AST_BINARY_OP) {
//       BinaryOpNode *binop = (BinaryOpNode *)node;

//       if (binop->is_and) {
//         extract(binop->left);
//         extract(binop->right);
//       } else {
//         // This is a comparison
//         WhereCondition cond;

//         if (binop->left->type == AST_COLUMN_REF) {
//           ColumnRefNode *col = (ColumnRefNode *)binop->left;
//           cond.column_name = col->name;
//           cond.column_index = get_column_index(table_name, col->name);
//         }

//         if (binop->right->type == AST_LITERAL) {
//           LiteralNode *lit = (LiteralNode *)binop->right;
//           cond.value = lit->value;
//         }

//         cond.operator_type = binop->op;
//         conditions.push_back(cond);
//       }
//     }
//   };

//   extract(where->condition);
//   return conditions;
// }

Vec<VMInstruction, QueryArena> build_select_from_ast(SelectNode *ast) {

  ProgramBuilder program;

  int mem_cursor = 0;

  TypedValue a = {.type = TYPE_8};

  auto *ptr = (uint64_t *)arena::alloc<QueryArena>(TYPE_4);
  auto *ptr2 = (uint64_t *)arena::alloc<QueryArena>(TYPE_4);

  *ptr = 5;
  *ptr2 = 11;

  uint8_t *data = (uint8_t *)arena::alloc<QueryArena>(TYPE_256);
  memcpy(data, "hey there besty\0", 232);

  // Schema *schema = (Schema *)arena::alloc<QueryArena>(sizeof(Schema));
  // schema->columns.push_back({.name = "key", .type = TYPE_32});
  // schema->record_size = 0;
  // program.emit(Opcodes::Open::create_ephemeral(mem_cursor, schema));
  program.emit(Opcodes::Move::create_load(1, TYPE_256, data));
  program.emit(Opcodes::Insert::create(mem_cursor, 1, 1));
  program.emit(Opcodes::Seek::create(mem_cursor, 1, 10, EQ));
  program.emit(Opcodes::Column::create(mem_cursor, 0, 2));
  program.emit(Opcodes::Result::create(2, 1));

  // program.emit(Opcodes::Move::create(2, TYPE_4, ptr));
  // auto repeat = program.here();
  // program.emit(Opcodes::Arithmetic::create(2, 2, 2, ARITH_ADD));
  // program.emit(Opcodes::Test::create(4, 1, 2, GE));
  // program.emit(Opcodes::Test::create(5, 1, 2, LT));
  // program.emit(Opcodes::Logic::create(6, 4, 5, LOGIC_AND));
  // program.emit(Opcodes::JumpIf::create(6, repeat));
  // program.emit(Opcodes::Result::create(3, 1));

  // program.emit(Opcodes::Result::create(0, 1));
  return program.instructions;
}

// ============================================================================
// Main entry point
// ============================================================================
Vec<VMInstruction, QueryArena> build_from_ast(ASTNode *ast) {
  return build_select_from_ast(nullptr);

  if (!ast) {
    Vec<VMInstruction, QueryArena> program;
    program.push_back(Opcodes::Halt::create());
    return program;
  }

  switch (ast->type) {
  case AST_SELECT:
    // return build_select_from_ast((SelectNode *)ast);
  // case AST_INSERT:
  //   return build_insert_from_ast((InsertNode *)ast);
  // case AST_UPDATE:
  //   return build_update_from_ast((UpdateNode *)ast);
  // case AST_DELETE:
  //   return build_delete_from_ast((DeleteNode *)ast);
  // case AST_CREATE_TABLE:
  //   return build_create_table_from_ast((CreateTableNode *)ast);
  // case AST_CREATE_INDEX:
  //   return build_create_index_from_ast((CreateIndexNode *)ast);
  // case AST_BEGIN:
  //   return build_begin_from_ast((BeginNode *)ast);
  // case AST_COMMIT:
  //   return build_commit_from_ast((CommitNode *)ast);
  // case AST_ROLLBACK:
  //   return build_rollback_from_ast((RollbackNode *)ast);
  default: {
    Vec<VMInstruction, QueryArena> program;
    program.push_back(Opcodes::Halt::create());
    return program;
  }
  }
}
