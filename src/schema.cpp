#include "schema.hpp"
#include "defs.hpp"
#include <cstdint>


  std::unordered_map<std::string, Table> tables;

void print_record(uint8_t *record, TableSchema *schema) {
  for (int i = 0; i < schema->columns.size(); i++) {
    debug_type((uint8_t *)record + schema->column_offsets[i - 1],
               schema->columns[i].type);
  }
}



Table* get_table(char * table_name) {
    if(tables.find(table_name) == tables.end()) {
        return nullptr;
    }
    return &tables[table_name];
}
Index* get_index(char *table_name, uint32_t column_index)
{
    if(column_index == 0){
        return nullptr;
    }

    Table*table = get_table(table_name);
    if(table == nullptr) {
        return nullptr;
    }

    if(table->indexes.find(column_index) != table->indexes.end()){
        return &table->indexes[column_index];
    }
    return nullptr;
}

uint32_t get_column_index(char * table_name, char* col_name) {

    Table* table = get_table(table_name);
    if(table == nullptr) {
        return 0;
    }

    auto schema = table->schema.columns;

    for (size_t i = 0; i < schema.size(); i++) {
        if (strcmp(schema[i].name, col_name) == 0) {
            return i;
        }
    }
    // Default to 0 if not found - in production would error
    return 0;
}
