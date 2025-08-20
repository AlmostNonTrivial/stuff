#include "schema.hpp"
#include "arena.hpp"
#include "defs.hpp"
#include <cstdint>
#include <cstring>

static Map<Str<SchemaArena>, Table, SchemaArena> tables;

Table* get_table(const char* table_name) {
    return tables.find(table_name);
}

Index* get_index(const char* table_name, uint32_t column_index) {
    if (column_index == 0) {
        return nullptr;  // Column 0 is the primary key, not indexed separately
    }

    Table* table = get_table(table_name);
    if (!table) {
        return nullptr;
    }

    return table->indexes.find(column_index);
}

uint32_t get_column_index(const char* table_name, const char* col_name) {
    Table* table = get_table(table_name);
    if (!table) {
        return 0;
    }

    for (size_t i = 0; i < table->schema.columns.size(); i++) {
        if (table->schema.columns[i].name.equals(col_name)) {
            return i;
        }
    }

    return -1;  // Not found
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

    // Make a copy to store in the map
    Table copy = *table;
    tables.insert(table->schema.table_name.c_str(), copy);
    return true;
}

bool remove_table(const char* table_name) {
    Table* table = get_table(table_name);
    if (!table) {
        return false;
    }

    // Note: btree cleanup should be done by caller
    tables.erase(table_name);
    return true;
}

bool add_index(const char* table_name, Index* index) {
    Table* table = get_table(table_name);
    if (!table || !index) {
        return false;
    }

    if (table->indexes.contains(index->column_index)) {
        return false;  // Index already exists
    }

    table->indexes.insert(index->column_index, *index);
    return true;
}

bool remove_index(const char* table_name, uint32_t column_index) {
    Table* table = get_table(table_name);
    if (!table) {
        return false;
    }

    // Note: btree cleanup should be done by caller
    table->indexes.erase(column_index);
    return true;
}

void clear_schema() {


    tables.clear();
    // not sure about this
    arena::reset<SchemaArena>();
}

uint32_t calculate_record_size(const Vector<ColumnInfo, SchemaArena>& columns) {
    uint32_t size = 0;
    // Skip column 0 (key) in record size calculation
    for (size_t i = 1; i < columns.size(); i++) {
        size += columns[i].type;
    }
    return size;
}

void calculate_column_offsets(TableSchema* schema) {
    schema->column_offsets.resize(schema->columns.size());
    schema->column_offsets[0] = 0;  // Key has no offset in record

    uint32_t offset = 0;
    for (size_t i = 1; i < schema->columns.size(); i++) {
        schema->column_offsets[i] = offset;
        offset += schema->columns[i].type;
    }
    schema->record_size = offset;
}

void print_record(uint8_t* record, TableSchema* schema) {
    for (size_t i = 1; i < schema->columns.size(); i++) {  // Skip key
        PRINT schema->columns[i].name.c_str() << ": ";
        uint8_t* data = record + schema->column_offsets[i];
        print_ptr(data, schema->columns[i].type);
        PRINT " ";
    }
    PRINT NL;
}

// Helper to get all table names (useful for debugging)
Vector<Str<SchemaArena>, SchemaArena> get_all_table_names() {
    Vector<Str<SchemaArena>, SchemaArena> names;
    for (size_t i = 0; i < tables.size(); i++) {
        auto entry = tables.at(i);
        if (entry) {
            names.push_back(entry->key);
        }
    }
    return names;
}

// Helper to validate schema integrity
bool validate_schema() {
    // Check that all tables have valid btrees
    for (size_t i = 0; i < tables.size(); i++) {
        auto entry = tables.at(i);
        if (entry) {
            Table* table = &entry->value;

            // Check table btree
            if (table->tree.tree_type == INVALID) {
                return false;
            }

            // Check all indexes
            for (size_t j = 0; j < table->indexes.size(); j++) {
                auto idx_entry = table->indexes.at(j);
                if (idx_entry) {
                    if (idx_entry->value.tree.tree_type == INVALID) {
                        return false;
                    }
                }
            }
        }
    }

    return true;
}
