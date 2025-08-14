
#include <cstdint>
#include <cstring>

#include "arena.hpp"
#include "vm.hpp"
#include <string>
#include <unordered_map>
#include <vector>

struct RegisterAllocator {
  std::unordered_map<std::string, int32_t> name_to_register;
  int32_t next_register;
};

struct Pair {
  uint32_t column_index;
  VMValue value;
};

struct WhereCondition {
  uint32_t column_index;
  uint8_t compare_op; // Maps to CompareOp enum
  VMValue value;
  double selectivity;
};

// Register allocator functions
void reg_allocator_init(RegisterAllocator *allocator);
void reg_allocator_clear(RegisterAllocator *allocator);
int32_t reg_allocator_get(RegisterAllocator *allocator,
                          const std::string &name);

// Label resolution
void resolve_labels(std::vector<VMInstruction> &program,
                    const std::unordered_map<std::string, int32_t> &label_map);

// Helper functions
Pair make_pair(uint32_t index, VMValue value);
OpCode get_load_opcode(VMValue *value);
void load_value(std::vector<VMInstruction> &instructions, VMValue *value,
                int32_t target_reg);

// Table operations
std::vector<VMInstruction>
build_create_table(const std::string &table_name,
                   const std::vector<ColumnInfo> &columns);

std::vector<VMInstruction> build_drop_table(const std::string &table_name);

std::vector<VMInstruction> build_insert(const std::string &table_name,
                                        const std::vector<Pair> &values,
                                        bool implicit_begin);

// Index operations
std::vector<VMInstruction> build_create_index(const std::string &table_name,
                                              uint32_t column_index);

std::vector<VMInstruction> build_drop_index(const std::string &table_name,
                                            uint32_t column_index);

// Transaction helpers
void add_begin(std::vector<VMInstruction> &instructions);
void add_commit(std::vector<VMInstruction> &instructions);
void add_rollback(std::vector<VMInstruction> &instructions);

// Register allocator functions
void reg_allocator_init(RegisterAllocator *allocator) {
  allocator->name_to_register.clear();
  allocator->next_register = 0;
}

void reg_allocator_clear(RegisterAllocator *allocator) {
  allocator->name_to_register.clear();
  allocator->next_register = 0;
}

int32_t reg_allocator_get(RegisterAllocator *allocator,
                          const std::string &name) {
  auto it = allocator->name_to_register.find(name);
  if (it == allocator->name_to_register.end()) {
    int32_t reg = allocator->next_register++;
    allocator->name_to_register[name] = reg;
    return reg;
  }
  return it->second;
}

// Label resolution
void resolve_labels(std::vector<VMInstruction> &program,
                    const std::unordered_map<std::string, int32_t> &label_map) {
  for (auto &inst : program) {
    // Check if p2 is a label reference (stored in p4 as string for now)
    if (inst.p4 && inst.p2 == -1) {
      const char *label = (const char *)inst.p4;
      auto it = label_map.find(label);
      if (it != label_map.end()) {
        inst.p2 = it->second;
        inst.p4 = nullptr; // Clear the label string
      }
    }

    // Check p3 for label references (for jumps)
    if (inst.p4 && inst.p3 == -1) {
      const char *label = (const char *)inst.p4;
      auto it = label_map.find(label);
      if (it != label_map.end()) {
        inst.p3 = it->second;
        inst.p4 = nullptr;
      }
    }
  }
}

// Helper functions
Pair make_pair(uint32_t index, VMValue value) { return {index, value}; }

OpCode get_load_opcode(VMValue *value) {
  if (value->type == TYPE_INT32 || value->type == TYPE_INT64) {
    return OP_Integer;
  }
  return OP_String;
}

void load_value(std::vector<VMInstruction> &instructions, VMValue *value,
                int32_t target_reg) {
  if (value->type == TYPE_INT32) {
    uint32_t val = *(uint32_t *)value->data;
    instructions.push_back({.opcode = OP_Integer,
                            .p1 = target_reg,
                            .p2 = (int32_t)val,
                            .p3 = 0,
                            .p4 = nullptr,
                            .p5 = 0});
  } else if (value->type == TYPE_INT64) {
    // For int64, store pointer in p4
    instructions.push_back({
        .opcode = OP_Integer,
        .p1 = target_reg,
        .p2 = 0,
        .p3 = 0,
        .p4 = value->data,
        .p5 = 1 // Flag for 64-bit
    });
  } else {
    // String types
    instructions.push_back({.opcode = OP_String,
                            .p1 = target_reg,
                            .p2 = (int32_t)value->type, // Size
                            .p3 = 0,
                            .p4 = value->data,
                            .p5 = 0});
  }
}

// Transaction helpers
void add_begin(std::vector<VMInstruction> &instructions) {
  instructions.insert(
      instructions.begin(),
      {.opcode = OP_Begin, .p1 = 0, .p2 = 0, .p3 = 0, .p4 = nullptr, .p5 = 0});
}

void add_commit(std::vector<VMInstruction> &instructions) {
  instructions.push_back(
      {.opcode = OP_Commit, .p1 = 0, .p2 = 0, .p3 = 0, .p4 = nullptr, .p5 = 0});
}

void add_rollback(std::vector<VMInstruction> &instructions) {
  instructions.push_back({.opcode = OP_Rollback,
                          .p1 = 0,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});
}

// Create table
std::vector<VMInstruction>
build_create_table(const std::string &table_name,
                   const std::vector<ColumnInfo> &columns) {
  std::vector<VMInstruction> instructions;

  // Allocate and fill schema
  TableSchema *schema = ARENA_ALLOC(TableSchema);
  schema->table_name = table_name;
  schema->columns = columns;

  instructions.push_back({.opcode = OP_CreateTable,
                          .p1 = 0,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = schema,
                          .p5 = 0});

  instructions.push_back(
      {.opcode = OP_Halt, .p1 = 0, .p2 = 0, .p3 = 0, .p4 = nullptr, .p5 = 0});

  return instructions;
}

// Drop table
std::vector<VMInstruction> build_drop_table(const std::string &table_name) {
  std::vector<VMInstruction> instructions;

  // Allocate table name string

  instructions.push_back({.opcode = OP_DropTable,
                          .p1 = 0,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = (void *)table_name.c_str(),
                          .p5 = 0});

  instructions.push_back(
      {.opcode = OP_Halt, .p1 = 0, .p2 = 0, .p3 = 0, .p4 = nullptr, .p5 = 0});

  return instructions;
}

// Insert
std::vector<VMInstruction> build_insert(const std::string &table_name,
                                        const std::vector<Pair> &values,
                                        bool implicit_begin) {
  std::vector<VMInstruction> instructions;
  RegisterAllocator allocator;
  reg_allocator_init(&allocator);

  if (implicit_begin) {
    add_begin(instructions);
  }

  const int32_t cursor_id = 0;

  // Allocate table name

  // Open write cursor
  instructions.push_back({.opcode = OP_OpenWrite,
                          .p1 = cursor_id,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = (void *)table_name.c_str(),
                          .p5 = 0});

  // Load values into registers
  std::vector<int32_t> value_regs;
  for (size_t i = 0; i < values.size(); i++) {
    std::string reg_name = "value_" + std::to_string(i);
    int32_t reg = reg_allocator_get(&allocator, reg_name);
    value_regs.push_back(reg);

    VMValue value_copy = values[i].value;
    load_value(instructions, &value_copy, reg);
  }

  // Make record from values
  int32_t record_reg = reg_allocator_get(&allocator, "record");
  instructions.push_back({.opcode = OP_MakeRecord,
                          .p1 = value_regs[0],          // First register
                          .p2 = (int32_t)values.size(), // Number of fields
                          .p3 = record_reg,
                          .p4 = nullptr,
                          .p5 = 0});

  // Insert the record
  // For insert, we need the key in p2 and record in p3
  instructions.push_back({.opcode = OP_Insert,
                          .p1 = cursor_id,
                          .p2 = value_regs[0], // Key is first value
                          .p3 = record_reg,
                          .p4 = nullptr,
                          .p5 = 0});

  // Close cursor
  instructions.push_back({.opcode = OP_Close,
                          .p1 = cursor_id,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  instructions.push_back(
      {.opcode = OP_Halt, .p1 = 0, .p2 = 0, .p3 = 0, .p4 = nullptr, .p5 = 0});

  return instructions;
}

// Create index
std::vector<VMInstruction> build_create_index(const std::string &table_name,
                                              uint32_t column_index) {
  std::vector<VMInstruction> instructions;
  RegisterAllocator allocator;
  reg_allocator_init(&allocator);
  std::unordered_map<std::string, int32_t> labels;

  // Allocate table name

  // Create the index
  instructions.push_back({.opcode = OP_CreateIndex,
                          .p1 = (int32_t)column_index,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = (void *)table_name.c_str(),
                          .p5 = 0});

  const int32_t table_cursor = 0;
  const int32_t index_cursor = 1;

  instructions.push_back({.opcode = OP_OpenRead,
                          .p1 = table_cursor,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = (void *)table_name.c_str(),
                          .p5 = 0});

  // Open write cursor on index

  instructions.push_back({
      .opcode = OP_OpenWrite,
      .p1 = index_cursor,
      .p2 = (int32_t)column_index,
      .p3 = 0,
      .p4 = (void *)table_name.c_str(),
      .p5 = 1 // Flag for index cursor
  });

  // Rewind table cursor, jump to end if empty
  instructions.push_back({.opcode = OP_Rewind,
                          .p1 = table_cursor,
                          .p2 = -1, // Will be resolved to "end" label
                          .p3 = 0,
                          .p4 = (void *)"end",
                          .p5 = 0});

  labels["loop_start"] = instructions.size();

  // Get rowid from table
  int32_t rowid_reg = reg_allocator_get(&allocator, "rowid");
  instructions.push_back({.opcode = OP_Key,
                          .p1 = table_cursor,
                          .p2 = rowid_reg,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  // Get column value
  int32_t column_reg = reg_allocator_get(&allocator, "column_value");
  instructions.push_back({.opcode = OP_Column,
                          .p1 = table_cursor,
                          .p2 = (int32_t)column_index,
                          .p3 = column_reg,
                          .p4 = nullptr,
                          .p5 = 0});

  // Insert into index (key=column_value, data=rowid)
  instructions.push_back({
      .opcode = OP_Insert,
      .p1 = index_cursor,
      .p2 = column_reg,
      .p3 = rowid_reg,
      .p4 = nullptr,
      .p5 = 1 // Index insert flag
  });

  // Next row
  instructions.push_back({.opcode = OP_Next,
                          .p1 = table_cursor,
                          .p2 = -1, // Will be resolved to "loop_start"
                          .p3 = 0,
                          .p4 = (void *)"loop_start",
                          .p5 = 0});

  labels["end"] = instructions.size();

  // Close cursors
  instructions.push_back({.opcode = OP_Close,
                          .p1 = table_cursor,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  instructions.push_back({.opcode = OP_Close,
                          .p1 = index_cursor,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = nullptr,
                          .p5 = 0});

  instructions.push_back(
      {.opcode = OP_Halt, .p1 = 0, .p2 = 0, .p3 = 0, .p4 = nullptr, .p5 = 0});

  resolve_labels(instructions, labels);

  return instructions;
}

// Drop index
std::vector<VMInstruction> build_drop_index(const std::string &table_name,
                                            uint32_t column_index) {
  std::vector<VMInstruction> instructions;

  instructions.push_back({.opcode = OP_DropIndex,
                          .p1 = (int32_t)column_index,
                          .p2 = 0,
                          .p3 = 0,
                          .p4 = (void *)table_name.c_str(),
                          .p5 = 0});

  instructions.push_back(
      {.opcode = OP_Halt, .p1 = 0, .p2 = 0, .p3 = 0, .p4 = nullptr, .p5 = 0});

  return instructions;
}
