// schema.cpp - STL container version
#include "catalog.hpp"
#include "parser.hpp"
#include "bplustree.hpp"
#include "defs.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>

// ============================================================================
// Registry Storage
// ============================================================================

static std::unordered_map<std::string, std::unique_ptr<Table>> tables;

void schema_clear() {
    tables.clear();
}

// ============================================================================
// RecordLayout Implementation
// ============================================================================

RecordLayout RecordLayout::create(std::vector<DataType>& column_types)
{
    RecordLayout layout;
    layout.layout = column_types;
    layout.record_size = 0;

    // don't include key, as it's kept in a different position than record
    for (size_t i = 1; i < column_types.size(); i++)
    {
        layout.offsets.push_back(layout.record_size);
        layout.record_size += column_types[i];
    }

    return layout;
}

RecordLayout RecordLayout::create(DataType key, DataType rec)
{
    std::vector<DataType> types;
    types.push_back(key);
    types.push_back(rec);

    return create(types);
}

// ============================================================================
// Index Implementation
// ============================================================================

RecordLayout Index::to_layout() const
{
    DataType key = get_column_type(table_name.c_str(), column_index);
    assert(column_index != 0);
    DataType rowid = get_column_type(table_name.c_str(), 0);
    return RecordLayout::create(key, rowid);
}

// ============================================================================
// Table Implementation
// ============================================================================

RecordLayout Table::to_layout() const
{
    std::vector<DataType> types;
    for (size_t i = 0; i < columns.size(); i++)
    {
        types.push_back(columns[i].type);
    }
    return RecordLayout::create(types);
}

// ============================================================================
// Registry Operations - Tables
// ============================================================================

Table* get_table(const char* table_name)
{
    auto it = tables.find(table_name);
    if (it != tables.end()) {
        return it->second.get();
    }
    return nullptr;
}

void remove_table(const char* table_name)
{
    Table* table = get_table(table_name);
    assert(table != nullptr);

    // Clean up all indexes
    for (auto& [col_idx, index] : table->indexes)
    {
        if (index) {
            btree_clear(&index->btree);
            delete index;
        }
    }
    table->indexes.clear();

    bplustree_clear(&table->bplustree);
    tables.erase(table_name);
}

// ============================================================================
// Registry Operations - Indexes
// ============================================================================

Index* get_index(const char* table_name, uint32_t column_index)
{
    Table* table = get_table(table_name);
    assert(table != nullptr);

    auto it = table->indexes.find(column_index);
    if (it != table->indexes.end()) {
        return it->second;
    }
    return nullptr;
}

Index* get_index(const char* table_name, const char* index_name)
{
    Table* table = get_table(table_name);
    assert(table != nullptr);

    for (auto& [col_idx, index] : table->indexes)
    {
        if (index && index->index_name == index_name) {
            return index;
        }
    }
    return nullptr;
}

Index* get_index(const char* index_name)
{
    for (auto& [table_name, table] : tables)
    {
        for (auto& [col_idx, index] : table->indexes)
        {
            if (index && index->index_name == index_name) {
                return index;
            }
        }
    }
    return nullptr;
}

void remove_index(const char* table_name, uint32_t column_index)
{
    Table* table = get_table(table_name);
    assert(table != nullptr);

    auto it = table->indexes.find(column_index);
    assert(it != table->indexes.end());

    Index* index = it->second;
    assert(index != nullptr);

    btree_clear(&index->btree);
    delete index;

    table->indexes.erase(it);
}

// ============================================================================
// Schema Queries
// ============================================================================

uint32_t get_column_index(const char* table_name, const char* col_name)
{
    Table* table = get_table(table_name);
    assert(table != nullptr);

    for (uint32_t i = 0; i < table->columns.size(); i++) {
        if (table->columns[i].name == col_name) {
            return i;
        }
    }

    assert(false); // Column not found
    return UINT32_MAX;
}

DataType get_column_type(const char* table_name, uint32_t col_index)
{
    Table* table = get_table(table_name);
    assert(table != nullptr);
    assert(col_index < table->columns.size());
    return table->columns[col_index].type;
}

// ============================================================================
// Factory Functions
// ============================================================================

Table* create_table(CreateTableNode* node)
{
    assert(node != nullptr);
    assert(!node->columns.empty());

    const char* table_name = node->table;

    // Don't allow creating sqlite_master or duplicates
    assert(strcmp(table_name, "sqlite_master") != 0);
    assert(get_table(table_name) == nullptr);

    auto table = std::make_unique<Table>();
    table->table_name = table_name;

    for (size_t i = 0; i < node->columns.size(); i++)
    {
        table->columns.push_back({
            std::string(node->columns[i].name),
            node->columns[i].type
        });
    }

    // Calculate record layout
    RecordLayout layout = table->to_layout();
    DataType key_type = table->columns[0].type;
    uint32_t record_size = layout.record_size - key_type;


    // Create BPlusTree
    table->bplustree = bplustree_create(key_type, record_size);

    assert(table != nullptr);
    assert(!table->columns.empty());
    assert(table->columns.size() <= MAX_RECORD_LAYOUT);

    Table* raw_ptr = table.get();
    tables[table_name] = std::move(table);
    return raw_ptr;
}

Index* create_index(CreateIndexNode* node)
{
    assert(node != nullptr);

    const char* table_name = node->table;
    const char* col_name = node->column;
    const char* index_name = node->index_name;

    Table* table = get_table(table_name);
    assert(table != nullptr);

    uint32_t col_idx = get_column_index(table_name, col_name);
    assert(col_idx != UINT32_MAX);
    assert(col_idx != 0); // Cannot index primary key
    assert(get_index(table_name, col_idx) == nullptr); // No duplicate index

    Index* index = new Index();
    index->index_name = index_name;
    index->table_name = table_name;
    index->column_index = col_idx;


    DataType index_key_type = table->columns[col_idx].type;
    DataType rowid_type = table->columns[0].type;
    index->btree = btree_create(index_key_type, rowid_type);

    assert(index != nullptr);
    assert(index->column_index < table->columns.size());

    // Generate index name if not provided
    if (index->index_name.empty())
    {
        char name_buf[256];
        snprintf(name_buf, sizeof(name_buf), "%s_%s_idx",
                table_name, table->columns[index->column_index].name.c_str());
        index->index_name = name_buf;
    }

    table->indexes[index->column_index] = index;
    return index;
}

void create_master()
{
    auto master = std::make_unique<Table>();
    master->table_name = "sqlite_master";

    // Columns: id (key), type, name, tbl_name, rootpage, sql
    master->columns.push_back({"id", TYPE_4});
    master->columns.push_back({"type", TYPE_32});
    master->columns.push_back({"name", TYPE_32});
    master->columns.push_back({"tbl_name", TYPE_32});
    master->columns.push_back({"rootpage", TYPE_4});
    master->columns.push_back({"sql", TYPE_256});

    RecordLayout layout = master->to_layout();
    uint32_t record_size = layout.record_size - TYPE_4;

    master->bplustree = bplustree_create(TYPE_4, record_size);

    assert(master != nullptr);
    assert(!master->columns.empty());
    assert(master->columns.size() <= MAX_RECORD_LAYOUT);
    assert(get_table(master->table_name.c_str()) == nullptr); // No duplicates

    tables[master->table_name] = std::move(master);
}

// ============================================================================
// Transaction Support
// ============================================================================

SchemaSnapshots take_snapshot()
{
    SchemaSnapshots snap;

    for (auto& [table_name, table] : tables)
    {
        assert(table != nullptr);

        SchemaSnapshots::Entry entry;
        entry.table = table->table_name;
        entry.root = table->bplustree.root_page_index;

        for (auto& [col_idx, index] : table->indexes)
        {
            if (index) {
                uint32_t idx_root = index->btree.root_page_index;
                entry.indexes.push_back({col_idx, idx_root});
            }
        }

        snap.entries.push_back(entry);
    }
    return snap;
}
