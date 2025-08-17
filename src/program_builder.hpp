#ifndef PROGRAM_BUILDER_HPP
#define PROGRAM_BUILDER_HPP

#include "vm.hpp"
#include <string>
#include <vector>
#include <unordered_map>


struct RegisterAllocator {
    std::unordered_map<std::string, int> name_to_register;
    int next_register = 0;

    int get(const std::string& name);
    void clear();
};

struct Pair {
    uint32_t column_index;
    VMValue value;
};

struct WhereCondition {
    uint32_t column_index;
    CompareOp operator_type;
    VMValue value;
    double selectivity = 0.5;
};

struct OrderBy {
    uint32_t column_index;
    std::string direction; // "ASC" or "DESC"
};

struct SelectOptions {
    std::string table_name;
    std::vector<ColumnInfo> schema;
    std::vector<uint32_t>* column_indices; // nullptr = SELECT *
    std::vector<WhereCondition> where_conditions;
    OrderBy* order_by = nullptr;
};

struct UpdateOptions {
    std::string table_name;
    std::vector<ColumnInfo> schema;
    std::vector<Pair> set_columns;
    std::vector<WhereCondition> where_conditions;
};

struct UnifiedOptions {
    std::string table_name;
    std::vector<ColumnInfo> schema;
    std::vector<Pair> set_columns;
    std::vector<WhereCondition> where_conditions;

    enum Operation { UPDATE, DELETE, SELECT, AGGREGATE } operation;
    std::vector<uint32_t>* select_columns = nullptr;
    OrderBy* order_by = nullptr;
    const char* aggregate_func = nullptr;
    uint32_t* aggregate_column = nullptr;
};

struct AccessMethod {
    enum Type { DIRECT_ROWID, INDEX_SCAN, FULL_TABLE_SCAN } type;
    WhereCondition* primary_condition = nullptr;
    WhereCondition* index_condition = nullptr;
    uint32_t index_col;
};

// Main functions
std::vector<VMInstruction> create_table(const std::string& table_name,
                                       const std::vector<ColumnInfo>& columns);
std::vector<VMInstruction> drop_table(const std::string& table_name);
std::vector<VMInstruction> drop_index(const std::string& index_name);
std::vector<VMInstruction> create_index(const std::string& table_name,
                                       uint32_t column_index,
                                       DataType key_type);

std::vector<VMInstruction> insert(const std::string& table_name,
                                 const std::vector<Pair>& values,
                                 bool implicit_begin);

std::vector<VMInstruction> select(const SelectOptions& options);
std::vector<VMInstruction> update(const UpdateOptions& options, bool implicit_begin);
std::vector<VMInstruction> _delete(const UpdateOptions& options, bool implicit_begin);

std::vector<VMInstruction> generate_aggregate_instructions(
    const std::string& table_name,
    const char* agg_func,
    uint32_t* column_index,
    const std::vector<WhereCondition>& where_conditions);

// Unified function
std::vector<VMInstruction> update_or_delete_or_select(const UnifiedOptions& options,
                                                     bool implicit_begin);

// Helper functions
void resolve_labels(std::vector<VMInstruction>& program,
                   const std::unordered_map<std::string, int>& map);
void add_begin(std::vector<VMInstruction>& instructions);
Pair make_pair(uint32_t index, const VMValue& value);
OpCode str_or_int(const VMValue& value);
void load_value(std::vector<VMInstruction>& instructions,
               const VMValue& value, int target_reg);

// Optimization functions
std::vector<WhereCondition> optimize_where_conditions(
    const std::vector<WhereCondition>& conditions,
    const std::vector<ColumnInfo>& schema,
    const std::string& table_name);

double estimate_selectivity(const WhereCondition& condition,
                           const std::vector<ColumnInfo>& schema,
                           const std::string& table_name);

AccessMethod choose_access_method(const std::vector<WhereCondition>& conditions,
                                 const std::vector<ColumnInfo>& schema,
                                 const std::string& table_name);

// Build operations
std::vector<VMInstruction> build_direct_rowid_operation(
    const std::string& table_name,
    const std::vector<ColumnInfo>& schema,
    const std::vector<Pair>& set_columns,
    const WhereCondition& primary_condition,
    const std::vector<WhereCondition>& remaining_conditions,
    RegisterAllocator& regs,
    UnifiedOptions::Operation operation,
    bool implicit_begin,
    std::vector<uint32_t>* select_columns,
    const char* aggregate_func,
    uint32_t* aggregate_column);

std::vector<VMInstruction> build_index_scan_operation(
    const std::string& table_name,
    const std::vector<ColumnInfo>& schema,
    const std::vector<Pair>& set_columns,
    const WhereCondition& index_condition,
    const std::vector<WhereCondition>& remaining_conditions,
    const std::string& index_name,
    RegisterAllocator& regs,
    UnifiedOptions::Operation operation,
    bool implicit_begin,
    std::vector<uint32_t>* select_columns,
    const char* aggregate_func,
    uint32_t* aggregate_column);

std::vector<VMInstruction> build_full_table_scan_operation(
    const std::string& table_name,
    const std::vector<ColumnInfo>& schema,
    const std::vector<Pair>& set_columns,
    const std::vector<WhereCondition>& conditions,
    RegisterAllocator& regs,
    UnifiedOptions::Operation operation,
    bool implicit_begin,
    std::vector<uint32_t>* select_columns,
    const char* aggregate_func,
    uint32_t* aggregate_column);

void build_where_checks(std::vector<VMInstruction>& instructions,
                       int cursor_id,
                       const std::vector<WhereCondition>& conditions,
                       const std::string& skip_label,
                       RegisterAllocator& regs);

OpCode get_negated_opcode(CompareOp op);
OpCode to_seek(CompareOp op);
OpCode to_opcode(CompareOp op);
bool ascending(CompareOp op);

// P5 flags
enum P5Flags {
    P5_CURSOR_TABLE = 0x01,
    P5_CURSOR_INDEX = 0x02,
    P5_INSERT_INDEX = 0x04
};

uint8_t set_p5(uint8_t current, uint8_t flag);

#endif
