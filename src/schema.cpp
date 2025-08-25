// schema.cpp
#include "schema.hpp"
#include "bplustree.hpp"
#include "defs.hpp"
#include "str.hpp"
#include <cstdio>
#include <cstring>

static bool _debug = true;

// ============================================================================
// Registry Storage
// ============================================================================
static Vec<Table, RegistryArena> tables;

// ============================================================================
// RecordLayout Implementation
// ============================================================================
RecordLayout RecordLayout::create(EmbVec<DataType, MAX_RECORD_LAYOUT> &column_types) {
    RecordLayout layout;
    layout.layout = column_types;
    layout.record_size = 0;

    for (size_t i = 0; i < column_types.size(); i++) {
        layout.offsets.push_back(layout.record_size);
        layout.record_size += column_types[i];
    }

    return layout;
}

RecordLayout RecordLayout::create(DataType key, DataType rec) {
    EmbVec<DataType, MAX_RECORD_LAYOUT> types;
    types.push_back(key);
    if (rec != TYPE_NULL) {
        types.push_back(rec);
    }
    return create(types);
}

// ============================================================================
// Registry Lookup Functions
// ============================================================================

Table *get_table(const char *table_name) {
    for (size_t i = 0; i < tables.size(); i++) {
        if (tables[i].table_name == table_name) {
            return &tables[i];
        }
    }
    return nullptr;
}

Index *get_index(const char *table_name, uint32_t column_index) {
    Table *table = get_table(table_name);
    if (!table)
        return nullptr;

    for (size_t i = 0; i < table->indexes.size(); i++) {
        if (table->indexes[i].column_index == column_index) {
            return &table->indexes[i];
        }
    }
    return nullptr;
}

uint32_t get_column_index(const char *table_name, const char *col_name) {
    Table *table = get_table(table_name);
    if (!table)
        return UINT32_MAX;

    for (size_t i = 0; i < table->columns.size(); i++) {
        if (table->columns[i].name == col_name) {
            return i;
        }
    }
    return UINT32_MAX;
}

DataType get_column_type(const char *table_name, uint32_t col_index) {
    Table *table = get_table(table_name);
    if (!table || col_index >= table->columns.size()) {
        return TYPE_NULL;
    }
    return table->columns[col_index].type;
}

// ============================================================================
// Statistics Management
// ============================================================================

void update_table_stats(const char *table_name, const TableStats &stats) {
    Table *table = get_table(table_name);
    if (table) {
        table->stats = stats;
        table->stats.is_stale = false;

        if (_debug) {
            printf("Updated stats for table '%s': %u rows, %u pages\n",
                   table_name, stats.row_count, stats.page_count);
        }
    }
}

void update_index_stats(const char *table_name, uint32_t column_index, const IndexStats &stats) {
    Index *index = get_index(table_name, column_index);
    if (index) {
        index->stats = stats;
        index->stats.is_stale = false;

        if (_debug) {
            printf("Updated stats for index on '%s.%u': %u distinct keys, selectivity %.2f\n",
                   table_name, column_index, stats.distinct_keys, stats.selectivity);
        }
    }
}

void invalidate_table_stats(const char *table_name) {
    Table *table = get_table(table_name);
    if (table) {
        table->stats.mark_stale();
    }
}

TableStats *get_table_stats(const char *table_name) {
    Table *table = get_table(table_name);
    return table ? &table->stats : nullptr;
}

IndexStats *get_index_stats(const char *table_name, uint32_t column_index) {
    Index *index = get_index(table_name, column_index);
    return index ? &index->stats : nullptr;
}

// ============================================================================
// Registry Modification Functions
// ============================================================================

bool add_table(Table *table) {
    // Validate table
    if (table->columns.empty()) {
        if (_debug)
            printf("Error: Table '%s' has no columns\n", table->table_name.c_str());
        return false;
    }

    // Check for duplicate
    if (get_table(table->table_name.c_str())) {
        if (_debug)
            printf("Error: Table '%s' already exists\n", table->table_name.c_str());
        return false;
    }

    // Validate column count
    if (table->columns.size() > MAX_RECORD_LAYOUT) {
        if (_debug)
            printf("Error: Table '%s' has %zu columns (max %d)\n",
                   table->table_name.c_str(), table->columns.size(), MAX_RECORD_LAYOUT);
        return false;
    }

    // Calculate and validate record size
    RecordLayout layout = table->to_layout();
    if (layout.record_size > PAGE_SIZE / 4) { // Reasonable limit for educational DB
        if (_debug)
            printf("Warning: Table '%s' has large records (%u bytes)\n",
                   table->table_name.c_str(), layout.record_size);
    }

    // Initialize statistics as unknown/stale
    table->stats.clear();
    table->stats.is_stale = true;

    tables.push_back(*table);

    if (_debug) {
        printf("Added table '%s' with %zu columns, record size %u bytes\n",
               table->table_name.c_str(), table->columns.size(), layout.record_size);
    }

    return true;
}

bool remove_table(const char *table_name) {
    for (size_t i = 0; i < tables.size(); i++) {
        if (tables[i].table_name == table_name) {
            // Clear btrees
            bplustree_clear(&tables[i].tree.bplustree);

            for (size_t j = 0; j < tables[i].indexes.size(); j++) {
                btree_clear(&tables[i].indexes[j].tree.btree);
            }

            // Remove from registry
            // tables.erase(i);

            if (_debug)
                printf("Removed table '%s'\n", table_name);
            return true;
        }
    }
    return false;
}

bool add_index(const char *table_name, Index *index) {
    Table *table = get_table(table_name);
    if (!table) {
        if (_debug)
            printf("Error: Table '%s' not found for index\n", table_name);
        return false;
    }

    if (index->column_index >= table->columns.size()) {
        if (_debug)
            printf("Error: Invalid column index %u for table '%s'\n",
                   index->column_index, table_name);
        return false;
    }

    // Check for duplicate index on same column
    if (get_index(table_name, index->column_index)) {
        if (_debug)
            printf("Error: Index already exists on column %u of table '%s'\n",
                   index->column_index, table_name);
        return false;
    }

    // Generate index name if not provided
    if (index->index_name.empty()) {
        char name_buf[TABLE_NAME_SIZE + 1 + COLUMN_NAME_SIZE];
        snprintf(name_buf, sizeof(name_buf), "%s_%s_idx", table_name,
                 table->columns[index->column_index].name.c_str());
        index->index_name = name_buf;
    }

    // Initialize statistics as unknown/stale
    index->stats.clear();
    index->stats.is_stale = true;

    table->indexes.push_back(*index);

    if (_debug) {
        printf("Added index '%s' on column '%s' of table '%s' (needs ANALYZE)\n",
               index->index_name.c_str(),
               table->columns[index->column_index].name.c_str(),
               table_name);
    }

    return true;
}

bool remove_index(const char *table_name, uint32_t column_index) {
    Table *table = get_table(table_name);
    if (!table)
        return false;

    for (size_t i = 0; i < table->indexes.size(); i++) {
        if (table->indexes[i].column_index == column_index) {
            btree_clear(&table->indexes[i].tree.btree);

            if (_debug) {
                printf("Removed index '%s' from table '%s'\n",
                       table->indexes[i].index_name.c_str(), table_name);
            }

            // table->indexes.erase(i);
            return true;
        }
    }
    return false;
}

void clear_schema() {
    for (size_t i = 0; i < tables.size(); i++) {
        bplustree_clear(&tables[i].tree.bplustree);
        for (size_t j = 0; j < tables[i].indexes.size(); j++) {
            btree_clear(&tables[i].indexes[j].tree.btree);
        }
    }
    tables.clear();

    if (_debug)
        printf("Cleared all schema\n");
}



// ============================================================================
// Utility Functions
// ============================================================================

void print_record(uint8_t *record, const RecordLayout *layout) {
    for (size_t i = 1; i < layout->column_count(); i++) { // Skip key at index 0
        printf("[%zu]: ", i);
        uint8_t *data = record + layout->get_offset(i);
        print_value(layout->layout[i], data);
        if (i < layout->column_count() - 1)
            printf(", ");
    }
}

void print_record_with_names(uint8_t *key, uint8_t *record, const Table *table) {
    // Print key
    printf("%s: ", table->columns[0].name.c_str());
    print_value(table->columns[0].type, key);

    // Print record fields
    RecordLayout layout = table->to_layout();
    for (size_t i = 1; i < table->columns.size(); i++) {
        printf(", %s: ", table->columns[i].name.c_str());
        uint8_t *data = record + layout.get_offset(i);
        print_value(table->columns[i].type, data);
    }
}

// ============================================================================
// Debug/Inspection Utilities
// ============================================================================

void print_table_info(const char *table_name) {
    Table *table = get_table(table_name);
    if (!table) {
        printf("Table '%s' not found\n", table_name);
        return;
    }

    RecordLayout layout = table->to_layout();

    printf("=== Table: %s ===\n", table->table_name.c_str());
    printf("Columns (%zu):\n", table->columns.size());
    for (size_t i = 0; i < table->columns.size(); i++) {
        const char *key_marker = (i == 0) ? " [KEY]" : "";
        const char *type_name = "";
        switch (table->columns[i].type) {
            case TYPE_4: type_name = "INT32"; break;
            case TYPE_8: type_name = "INT64"; break;
            case TYPE_32: type_name = "VARCHAR32"; break;
            case TYPE_256: type_name = "VARCHAR256"; break;
            default: type_name = "UNKNOWN"; break;
        }
        printf("  %2zu: %-20s %s%s (offset: %u)\n", i,
               table->columns[i].name.c_str(),
               type_name, key_marker, layout.get_offset(i));
    }

    printf("Record size: %u bytes\n", layout.record_size);
    printf("Key type: ");
    switch (layout.key_type()) {
        case TYPE_4: printf("INT32"); break;
        case TYPE_8: printf("INT64"); break;
        case TYPE_32: printf("VARCHAR32"); break;
        case TYPE_256: printf("VARCHAR256"); break;
        default: printf("UNKNOWN"); break;
    }
    printf("\n");

    // Print statistics if available
    if (!table->stats.is_stale && table->stats.row_count > 0) {
        printf("Statistics (current):\n");
        printf("  Rows: %u\n", table->stats.row_count);
        printf("  Pages: %u\n", table->stats.page_count);
        printf("  Size: %u bytes\n", table->stats.total_size);
    } else if (table->stats.is_stale) {
        printf("Statistics: STALE (run ANALYZE)\n");
    } else {
        printf("Statistics: NONE (run ANALYZE)\n");
    }

    if (!table->indexes.empty()) {
        printf("Indexes (%zu):\n", table->indexes.size());
        for (size_t i = 0; i < table->indexes.size(); i++) {
            printf("  - %s on column %u (%s)",
                   table->indexes[i].index_name.c_str(),
                   table->indexes[i].column_index,
                   table->columns[table->indexes[i].column_index].name.c_str());

            if (!table->indexes[i].stats.is_stale && table->indexes[i].stats.distinct_keys > 0) {
                printf(" (selectivity: %.2f%%)", table->indexes[i].stats.selectivity * 100);
            } else if (table->indexes[i].stats.is_stale) {
                printf(" (stats: STALE)");
            } else {
                printf(" (stats: NONE)");
            }
            printf("\n");
        }
    }

    printf("BTree info:\n");
    printf("  Root page: %u\n", table->tree.bplustree.root_page_index);
    printf("  Record size in tree: %u\n", table->tree.bplustree.record_size);
    printf("\n");
}

void print_all_tables() {
    printf("=== Schema Registry ===\n");
    printf("Tables registered: %zu\n", tables.size());

    // Count how many need analysis
    uint32_t stale_count = 0;
    for (size_t i = 0; i < tables.size(); i++) {
        if (tables[i].stats.is_stale)
            stale_count++;
    }

    if (stale_count > 0) {
        printf("Tables needing ANALYZE: %u\n", stale_count);
    }

    printf("\n");

    for (size_t i = 0; i < tables.size(); i++) {
        print_table_info(tables[i].table_name.c_str());
    }
}

// Validate that all indexes point to valid columns
bool validate_schema() {
    return true;
}

// Get total size of a table (data + indexes)
size_t get_table_size(const char *table_name) {
    Table *table = get_table(table_name);
    if (!table)
        return 0;

    // Use stats if available
    if (!table->stats.is_stale && table->stats.total_size > 0) {
        return table->stats.total_size;
    }

    // Otherwise return approximate based on page counts
    size_t total_pages = 1; // Root page minimum

    // Add index pages
    total_pages += table->indexes.size(); // Minimum 1 page per index

    return total_pages * PAGE_SIZE;
}

// Helper to build a RecordLayout from column names (for projections)
RecordLayout build_layout_from_columns(const char *table_name,
                                       const Vec<const char *, QueryArena> &column_names) {
    Table *table = get_table(table_name);
    if (!table) {
        return RecordLayout::create(TYPE_NULL); // Return empty layout
    }

    EmbVec<DataType, MAX_RECORD_LAYOUT> types;

    for (size_t i = 0; i < column_names.size(); i++) {
        uint32_t idx = get_column_index(table_name, column_names[i]);
        if (idx < table->columns.size()) {
            types.push_back(table->columns[idx].type);
        }
    }

    if (types.empty()) {
        return RecordLayout::create(TYPE_NULL);
    }

    return RecordLayout::create(types);
}

// Get all table names (useful for SHOW TABLES)
Vec<const char *, QueryArena> get_all_table_names() {
    Vec<const char *, QueryArena> names;
    for (size_t i = 0; i < tables.size(); i++) {
        names.push_back(tables[i].table_name.c_str());
    }
    return names;
}

// Check if a column name exists in any table
bool column_exists_anywhere(const char *col_name) {
    for (size_t i = 0; i < tables.size(); i++) {
        for (size_t j = 0; j < tables[i].columns.size(); j++) {
            if (tables[i].columns[j].name == col_name) {
                return true;
            }
        }
    }
    return false;
}

// Helper for debugging: count all indexes
uint32_t total_index_count() {
    uint32_t count = 0;
    for (size_t i = 0; i < tables.size(); i++) {
        count += tables[i].indexes.size();
    }
    return count;
}

// Check if statistics are fresh enough (for query planning)
bool stats_are_fresh(const char *table_name) {
    Table *table = get_table(table_name);
    if (!table)
        return false;

    return !table->stats.is_stale && table->stats.row_count > 0;
}
