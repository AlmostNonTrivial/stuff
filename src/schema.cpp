#include "schema.hpp"
#include "arena.hpp"
#include "defs.hpp"
#include <cstdint>
#include <cstring>

static Vec<Table, RegistryArena> tables;

Table *get_table(const char *table_name) {
  int index = tables.find_with([table_name](const Table &table) {
    return table.schema.table_name.starts_with(table_name);
  });
  return &tables[index];
}

Index *get_index(const char *table_name, uint32_t column_index) {
  if (column_index == 0) {
    return nullptr; // Column 0 is the primary key, not indexed separately
  }

  Table *table = get_table(table_name);
  if (!table) {
    return nullptr;
  }

  int index= table->indexes.find_with([column_index](const Index
     & index) {
      return index.column_index == column_index;
  });

  return &table->indexes[index];
}

uint32_t get_column_index(const char *table_name, const char *col_name) {
  Table *table = get_table(table_name);
  if (!table) {
    return 0;
  }

  for (size_t i = 0; i < table->schema.columns.size(); i++) {
    if (table->schema.columns[i].name.starts_with(col_name)) {
      return i;
    }
  }

  return -1; // Not found
}

DataType get_column_type(const char *table_name, uint32_t col_index) {
  Table *table = get_table(table_name);
  if (!table || col_index >= table->schema.columns.size()) {
    return TYPE_NULL;
  }
  return table->schema.columns[col_index].type;
}

bool add_table(Table *table) {
  if (!table)
    return false;

  // Make a copy to store in the map
  Table copy = *table;
  tables.push_back(copy);
  return true;
}

bool remove_table(const char *table_name) {
  Table *table = get_table(table_name);
  if (!table) {
    return false;
  }

  // Note: btree cleanup should be done by caller
  tables.erase_with([table_name](const Table& entry) {
      return entry.schema.table_name.starts_with(table_name);
  });
  return true;
}

bool add_index(const char *table_name, Index *index) {
  Table *table = get_table(table_name);
  if (!table || !index) {
    return false;
  }

  if (table->indexes.contains_with([index](const Index&entry) {
      return entry.column_index == index->column_index;
  })) {
    return false; // Index already exists
  }

  table->indexes.insert(index->column_index, *index);
  return true;
}

bool remove_index(const char *table_name, uint32_t column_index) {
  Table *table = get_table(table_name);
  if (!table) {
    return false;
  }

  // Note: btree cleanup should be done by caller
  table->indexes.remove(column_index);
  return true;
}

void clear_schema() {

  tables.clear();
  // not sure about this
  arena::reset<RegistryArena>();
}

uint32_t calculate_record_size(const Vec<ColumnInfo, RegistryArena> &columns) {
  uint32_t size = 0;
  // Skip column 0 (key) in record size calculation
  for (size_t i = 1; i < columns.size(); i++) {
    size += columns[i].type;
  }
  return size;
}

void calculate_column_offsets(Schema *schema) {
  schema->column_offsets.clear();
  schema->column_offsets.push_back(0); // Key has no offset in record

  uint32_t offset = 0;
  for (size_t i = 1; i < schema->columns.size(); i++) {
    schema->column_offsets.push_back( offset);
    offset += schema->columns[i].type;
  }
  schema->record_size = offset;
}

void print_record(uint8_t *record, Schema *schema) {
  for (size_t i = 1; i < schema->columns.size(); i++) { // Skip key
    PRINT schema->columns[i].name.c_str() << ": ";
    uint8_t *data = record + schema->column_offsets[i];
    print_ptr(data, schema->columns[i].type);
    PRINT " ";
  }
  PRINT NL;
}

// Helper to get all table names (useful for debugging)
Vec<Str<RegistryArena>, RegistryArena> get_all_table_names() {
  Vec<Str<RegistryArena>, RegistryArena> names;
  for (size_t i = 0; i < tables.size(); i++) {
    auto entry = tables[i];
    names.push_back(entry.schema.table_name);
  }
  return names;
}

// Helper to validate schema integrity
bool validate_schema() {
  // Check that all tables have valid btrees
  for (size_t i = 0; i < tables.size(); i++) {
    auto entry = &tables[i];

    Table *table = entry;

    // Check table btree
    if (table->tree.tree_type == INVALID) {
      return false;
    }

    // Check all indexes
    for (size_t j = 0; j < table->indexes.size(); j++) {
      auto idx_entry = &table->indexes[j];

      if (idx_entry->tree.tree_type == INVALID) {
        return false;
      }
    }
  }

  return true;
}
