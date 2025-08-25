// schema.cpp - Compressed version with proper vector removal
#include "map.hpp"
#include "parser.hpp"
#include "schema.hpp"
#include "bplustree.hpp"
#include "defs.hpp"
#include "str.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

// ============================================================================
// Registry Storagegg
// ============================================================================

static StringMap<Table *, RegistryArena> tables;

// ============================================================================
// RecordLayout Implementation
// ============================================================================

RecordLayout
RecordLayout::create(Vec<DataType, QueryArena> &column_types)
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

RecordLayout
RecordLayout::create(DataType key, DataType rec)
{
	Vec<DataType, QueryArena> types;
	types.push_back(key);
	types.push_back(rec);

	return create(types);
}

// ============================================================================
// Index Implementation
// ============================================================================

RecordLayout
Index::to_layout() const
{
	DataType key = get_column_type(table_name, column_index);
	assert(column_index != 0);
	DataType rowid = get_column_type(table_name, 0);
	return RecordLayout::create(key, rowid);
}

// ============================================================================
// Table Implementation
// ============================================================================

RecordLayout
Table::to_layout() const
{
	Vec<DataType, QueryArena> types;
	for (size_t i = 0; i < columns.size(); i++)
	{
		types.push_back(columns[i].type);
	}
	return RecordLayout::create(types);
}

// ============================================================================
// Registry Operations - Tables
// ============================================================================

Table *
get_table(const char *table_name)
{
	Table *table = tables[table_name];
	return table;
}

void
remove_table(const char *table_name)
{
	Table *table = get_table(table_name);
	assert(table != nullptr);
	for (int i = 0; i < table->indexes.size(); i++)
	{
		Index *index = table->indexes[i];
		remove_index(table_name, index->column_index);
	}

	bplustree_clear(&table->tree.bplustree);
	// tables.remove()
}

// ============================================================================
// Registry Operations - Indexes
// ============================================================================

Index *
get_index(const char *table_name, uint32_t column_index)
{
	Table *table = get_table(table_name);
	assert(table != nullptr);

	if (table->indexes.contains(column_index))
	{
		return table->indexes[column_index];
	}
	return nullptr;
}
Index *
get_index(const char *table_name, const char *index_name)
{
	Table *table = get_table(table_name);
	assert(table != nullptr);

	for (size_t i = 0; i < table->indexes.size(); i++)
	{
		if (table->indexes[i]->index_name == index_name)
		{
			return table->indexes[i];
		}
	}
	return nullptr;
}
Index *
get_index(const char *index_name)
{
	auto t = (*tables.get_values());

	for (int j = 0; j < t.size(); j++)
	{
		auto table = t[j];
		for (size_t i = 0; i < table->indexes.size(); i++)
		{
			if (table->indexes[i]->index_name == index_name)
			{
				return table->indexes[i];
			}
		}
	}
	return nullptr;
}

void
remove_index(const char *table_name, uint32_t column_index)
{
	Table *table = get_table(table_name);
	assert(table != nullptr);

	Index *index = table->indexes[column_index];
	assert(index != nullptr);
	btree_clear(&index->tree.btree);

	table->indexes.remove(column_index);
}

// ============================================================================
// Schema Queries
// ============================================================================

uint32_t
get_column_index(const char *table_name, const char *col_name)
{
	Index *index = get_index(table_name, col_name);
	assert(index != nullptr);
	return index->column_index;
}

DataType
get_column_type(const char *table_name, uint32_t col_index)
{
	Table *table = get_table(table_name);
	assert(table != nullptr);
	assert(col_index < table->columns.size());
	return table->columns[col_index].type;
}

// ============================================================================
// Factory Functions
// ============================================================================

Table *
create_table(CreateTableNode *node)
{
	assert(node != nullptr);
	assert(!node->columns.empty());

	const char *table_name = node->table;

	// Don't allow creating sqlite_master or duplicates
	assert(strcmp(table_name, "sqlite_master") != 0);
	assert(get_table(table_name) == nullptr);

	Table *table = (Table *)arena::alloc<RegistryArena>(sizeof(Table));
	table->table_name = node->table;

	for (size_t i = 0; i < node->columns.size(); i++)
	{
		table->columns.push_back(node->columns[i]);
	}

	// Calculate record layout
	RecordLayout layout = table->to_layout();
	DataType key_type = table->columns[0].type;
	uint32_t record_size = layout.record_size - key_type;
	table->tree_type = TreeType::BPLUSTREE;

	// Create BPlusTree
	table->tree.bplustree = bplustree_create(key_type, record_size);

	assert(table != nullptr);
	assert(!table->columns.empty());
	assert(table->columns.size() <= MAX_RECORD_LAYOUT);
	assert(get_table(table->table_name.c_str()) == nullptr); // No duplicates

	tables.insert(table_name, table);
	return table;
}

Index *
create_index(CreateIndexNode *node)
{
	assert(node != nullptr);

	const char *table_name = node->table;
	const char *col_name = node->column;
	const char *index_name = node->index_name;

	Table *table = get_table(table_name);
	assert(table != nullptr);

	uint32_t col_idx = get_column_index(table_name, col_name);
	assert(col_idx != UINT32_MAX);
	assert(col_idx != 0);							   // Cannot index primary key
	assert(get_index(table_name, col_idx) == nullptr); // No duplicate index

	Index *index = (Index *)arena::alloc<RegistryArena>(sizeof(Index));
	index->index_name = index_name;
	index->table_name = table_name;
	index->column_index = col_idx;
	index->tree_type = TreeType::BTREE;

	DataType index_key_type = table->columns[col_idx].type;
	DataType rowid_type = table->columns[0].type;
	index->tree.btree = btree_create(index_key_type, rowid_type);

	assert(table != nullptr);
	assert(index != nullptr);
	assert(index->column_index < table->columns.size());
	assert(get_index(table_name, index->column_index) == nullptr); // No duplicate index

	// Generate index name if not provided
	if (index->index_name.empty())
	{
		char name_buf[256];
		snprintf(name_buf, sizeof(name_buf), "%s_%s_idx", table_name, table->columns[index->column_index].name.c_str());
		index->index_name = name_buf;
	}

	table->indexes.insert(index->column_index, index);
	return index;
}

void
create_master()
{
	Table *master = (Table *)arena::alloc<RegistryArena>(sizeof(Table));
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
	master->tree_type = TreeType::BPLUSTREE;
	master->tree.bplustree = bplustree_create(TYPE_4, record_size);

	Table *table = master;
	assert(table != nullptr);
	assert(!table->columns.empty());
	assert(table->columns.size() <= MAX_RECORD_LAYOUT);
	assert(get_table(table->table_name.c_str()) == nullptr); // No duplicates

	tables.insert(master->table_name, table);
}

// ============================================================================
// Transaction Support
// ============================================================================

SchemaSnapshots
take_snapshot()
{
	SchemaSnapshots snap;

	for (size_t i = 0; i < tables.size(); i++)
	{
		auto table = (*tables.get_values())[i];
		assert(table != nullptr);

		const char *name = table->table_name.c_str();
		uint32_t root = table->tree.bplustree.root_page_index;

		Vec<std::pair<uint32_t, uint32_t>, QueryArena> indexes;
		for (size_t j = 0; j < table->indexes.size(); j++)
		{
			if (table->indexes[j])
			{
				uint32_t idx_root = table->indexes[j]->tree.btree.root_page_index;

				indexes.push_back({table->indexes[j]->column_index, idx_root});
			}
		}

		snap.entries.push_back({name, root, indexes});
	}
	return snap;
}
