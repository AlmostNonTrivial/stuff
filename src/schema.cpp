#include "schema.hpp"
#include "defs.hpp"
#include <cstdint>
#include <cstring>

static std::unordered_map<std::string, Table> tables;

Table* get_table(const char* table_name) {
    auto it = tables.find(table_name);
    if (it == tables.end()) {
        return nullptr;
    }
    return &it->second;
}

Index* get_index(const char* table_name, uint32_t column_index) {
    if (column_index == 0) {
        return nullptr;
    }

    Table* table = get_table(table_name);
    if (!table) {
        return nullptr;
    }

    auto it = table->indexes.find(column_index);
    if (it == table->indexes.end()) {
        return nullptr;
    }
    return &it->second;
}

uint32_t get_column_index(const char* table_name, const char* col_name) {
    Table* table = get_table(table_name);
    if (!table) {
        return 0;
    }

    for (size_t i = 0; i < table->schema.columns.size(); i++) {
        if (strcmp(table->schema.columns[i].name, col_name) == 0) {
            return i;
        }
    }
    return 0;
}

DataType get_column_type(const char* table_name, uint32_t col_index) {
    Table* table = get_table(table_name);
    if (!table || col_index >= table->schema.columns.size()) {
        return TYPE_NULL;
    }
    return table->schema.columns[col_index].type;
}

bool add_table(Table* table) {
    if (!table) return false;

    std::string name = table->schema.table_name;
    if (tables.find(name) != tables.end()) {
        return false; // Table already exists
    }

    tables[name] = *table;
    return true;
}

bool remove_table(const char* table_name) {
    return tables.erase(table_name) > 0;
}

bool add_index(const char* table_name, Index* index) {
    Table* table = get_table(table_name);
    if (!table || !index) {
        return false;
    }

    if (table->indexes.find(index->column_index) != table->indexes.end()) {
        return false; // Index already exists
    }

    table->indexes[index->column_index] = *index;
    return true;
}

bool remove_index(const char* table_name, uint32_t column_index) {
    Table* table = get_table(table_name);
    if (!table) {
        return false;
    }

    return table->indexes.erase(column_index) > 0;
}

void clear_schema() {
    tables.clear();
}

uint32_t calculate_record_size(const std::vector<ColumnInfo>& columns) {
    uint32_t size = 0;
    // Skip column 0 (key) in record size calculation
    for (size_t i = 1; i < columns.size(); i++) {
        size += columns[i].type;
    }
    return size;
}

void calculate_column_offsets(TableSchema* schema) {
    schema->column_offsets.resize(schema->columns.size());
    schema->column_offsets[0] = 0; // Key has no offset in record

    uint32_t offset = 0;
    for (size_t i = 1; i < schema->columns.size(); i++) {
        schema->column_offsets[i] = offset;
        offset += schema->columns[i].type;
    }
    schema->record_size = offset;
}

void print_record(uint8_t* record, TableSchema* schema) {
    for (size_t i = 0; i < schema->columns.size(); i++) {
        if (i == 0) continue; // Skip key

        uint8_t* data = record + schema->column_offsets[i];
        debug_type(data, schema->columns[i].type);
    }
}
