// schema.cpp - Custom container version
#include "catalog.hpp"
#include "types.hpp"
#include "vm.hpp"
#include "arena.hpp"
#include "parser.hpp"
#include "bplustree.hpp"
#include "vm.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>

#define MAX_RECORD_LAYOUT 32

string_map<Table *, SchemaArena> tables;

void
schema_init()
{
	// Initialize the schema arena with a reasonable size
	Arena<SchemaArena>::init(1024 * 1024, 16 * 1024 * 1024); // 1MB initial, 16MB max

	// Initialize the tables map
	stringmap_init(&tables);
}

void
schema_clear()
{
	// Clean up all tables
	for (uint32_t i = 0; i < tables.capacity; i++)
	{
		auto &entry = tables.entries[i];
		if (entry.state == string_map<Table *, SchemaArena>::Entry::OCCUPIED)
		{
			Table *table = entry.value;
			if (table)
			{
				// Clean up indexes
				for (uint32_t j = 0; j < table->indexes.capacity; j++)
				{
					auto &idx_entry = table->indexes.entries[j];
					if (idx_entry.state == hash_map<uint32_t, Index *, SchemaArena>::Entry::OCCUPIED)
					{
						Index *index = idx_entry.value;
						if (index)
						{
							bplustree_clear(&index->btree);
						}
					}
				}
				bplustree_clear(&table->bplustree);
			}
		}
	}

	stringmap_clear(&tables);
}

// ============================================================================
// RecordLayout Implementation
RecordLayout
RecordLayout::create(array<DataType, SchemaArena> &column_types)
{
	RecordLayout layout;
	array_set(&layout.layout, column_types);
	layout.record_size = 0;

	// don't include key, as it's kept in a different position than record
	for (size_t i = 1; i < column_types.size; i++)
	{
		array_push(&layout.offsets, layout.record_size);
		layout.record_size += type_size(column_types.data[i]);
	}

	return layout;
}

RecordLayout
RecordLayout::create(DataType key, DataType rec)
{
	array<DataType, SchemaArena> types;
	array_push(&types, key);
	array_push(&types, rec);

	return create(types);
}

// ============================================================================
// Index Implementation
RecordLayout
Index::to_layout() const
{
	DataType key = get_column_type(table_name.data, column_index);
	assert(column_index != 0);
	DataType rowid = get_column_type(table_name.data, 0);
	return RecordLayout::create(key, rowid);
}

// ============================================================================
// Table Implementation
RecordLayout
Table::to_layout() const
{
	array<DataType, SchemaArena> types;
	for (size_t i = 0; i < columns.size; i++)
	{
		array_push(&types, columns.data[i].type);
	}
	return RecordLayout::create(types);
}

// ============================================================================
// Registry Operations - Tables
Table *
get_table(const char *table_name)
{
	Table **result = stringmap_get(&tables, table_name);
	return result ? *result : nullptr;
}

void
remove_table(const char *table_name)
{
	Table *table = get_table(table_name);
	assert(table != nullptr);

	// Clean up all indexes
	for (uint32_t i = 0; i < table->indexes.capacity; i++)
	{
		auto &entry = table->indexes.entries[i];
		if (entry.state == hash_map<uint32_t, Index *, SchemaArena>::Entry::OCCUPIED)
		{
			Index *index = entry.value;
			if (index)
			{
				bplustree_clear(&index->btree);
			}
		}
	}
	hashmap_clear(&table->indexes);

	bplustree_clear(&table->bplustree);
	stringmap_delete(&tables, table_name);
}

// ============================================================================
// Registry Operations - Indexes
Index *
get_index(const char *table_name, uint32_t column_index)
{
	Table *table = get_table(table_name);
	assert(table != nullptr);

	Index **result = hashmap_get(&table->indexes, column_index);
	return result ? *result : nullptr;
}

Index *
get_index(const char *table_name, const char *index_name)
{
	Table *table = get_table(table_name);
	assert(table != nullptr);

	for (uint32_t i = 0; i < table->indexes.capacity; i++)
	{
		auto &entry = table->indexes.entries[i];
		if (entry.state == hash_map<uint32_t, Index *, SchemaArena>::Entry::OCCUPIED)
		{
			Index *index = entry.value;
			if (index && strcmp(index->index_name.data, index_name) == 0)
			{
				return index;
			}
		}
	}
	return nullptr;
}

Index *
get_index(const char *index_name)
{
	for (uint32_t i = 0; i < tables.capacity; i++)
	{
		auto &entry = tables.entries[i];
		if (entry.state == string_map<Table *, SchemaArena>::Entry::OCCUPIED)
		{
			Table *table = entry.value;

			for (uint32_t j = 0; j < table->indexes.capacity; j++)
			{
				auto &idx_entry = table->indexes.entries[j];
				if (idx_entry.state == hash_map<uint32_t, Index *, SchemaArena>::Entry::OCCUPIED)
				{
					Index *index = idx_entry.value;
					if (index && strcmp(index->index_name.data, index_name) == 0)
					{
						return index;
					}
				}
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

	Index **result = hashmap_get(&table->indexes, column_index);
	assert(result != nullptr && *result != nullptr);

	Index *index = *result;
	bplustree_clear(&index->btree);

	hashmap_delete(&table->indexes, column_index);
}

// ============================================================================
// Schema Queries
uint32_t
get_column_index(const char *table_name, const char *col_name)
{
	Table *table = get_table(table_name);
	assert(table != nullptr);

	for (uint32_t i = 0; i < table->columns.size; i++)
	{
		if (strcmp(table->columns.data[i].name, col_name) == 0)
		{
			return i;
		}
	}

	assert(false); // Column not found
	return UINT32_MAX;
}

DataType
get_column_type(const char *table_name, uint32_t col_index)
{
	Table *table = get_table(table_name);
	assert(table != nullptr);
	assert(col_index < table->columns.size);
	return table->columns.data[col_index].type;
}

// ============================================================================
// Factory Functions
// ============================================================================
static const char *
intern_string(const char *str)
{
	size_t len = strlen(str) + 1;
	char  *interned = (char *)Arena<SchemaArena>::alloc(len);
	memcpy(interned, str, len);
	return interned;
}

Table *
create_table(CreateTableStmt *node, int root_page)
{

	assert(node != nullptr);
	assert(node->columns != 0);

	const char *table_name = node->table_name;

	// Don't allow creating master_catalog or duplicates
	assert(strcmp(table_name, "master_catalog") != 0);
	assert(get_table(table_name) == nullptr);

	// Allocate table from arena
	Table *table = (Table *)Arena<SchemaArena>::alloc(sizeof(Table));

	// Initialize table name
	string_set(&table->table_name, table_name);

	// Initialize columns array
	for (size_t i = 0; i < node->columns->size; i++)
	{
		Column col;
		col.name = intern_string(node->columns->data[i]->name);
		col.type = node->columns->data[i]->type;
		array_push(&table->columns, col);
	}

	// Calculate record layout
	RecordLayout layout = table->to_layout();
	DataType	 key_type = table->columns.data[0].type;
	uint32_t	 record_size = layout.record_size;

	// Create BPlusTree
	table->bplustree = bplustree_create(key_type, record_size, root_page == 0);
	if (root_page != 0)
	{
		table->bplustree.root_page_index = root_page;
	}

	assert(table != nullptr);
	assert(table->columns.size > 0);
	assert(table->columns.size <= MAX_RECORD_LAYOUT);

	// Add to registry
	stringmap_insert(&tables, table_name, table);

	return table;
}

Index *
create_index(CreateIndexStmt *node, int root_page)
{
	assert(node != nullptr);

	const char *table_name = node->table_name;
	const char *col_name = node->columns->data[0];
	const char *index_name = node->index_name;

	Table *table = get_table(table_name);
	assert(table != nullptr);

	uint32_t col_idx = get_column_index(table_name, col_name);
	assert(col_idx != UINT32_MAX);
	assert(col_idx != 0);							   // Cannot index primary key
	assert(get_index(table_name, col_idx) == nullptr); // No duplicate index

	// Allocate index from arena
	Index *index = (Index *)Arena<SchemaArena>::alloc(sizeof(Index));

	// Set string fields
	string_set(&index->index_name, index_name ? index_name : "");
	string_set(&index->table_name, table_name);
	index->column_index = col_idx;

	DataType index_key_type = table->columns.data[col_idx].type;
	DataType rowid_type = table->columns.data[0].type;
	index->btree = bplustree_create(type_size(index_key_type), type_size(rowid_type), root_page == 0);
	if (root_page != 0)
	{
		index->btree.root_page_index = root_page;
	}

	assert(index != nullptr);
	assert(index->column_index < table->columns.size);

	// Generate index name if not provided
	if (index->index_name.size == 0 || index->index_name.data[0] == '\0')
	{
		char name_buf[256];
		snprintf(name_buf, sizeof(name_buf), "%s_%s_idx", table_name, table->columns.data[index->column_index].name);
		string_set(&index->index_name, name_buf);
	}

	hashmap_insert(&table->indexes, index->column_index, index);
	return index;
}

void
create_master(bool existed)
{
	// Allocate table from arena
	Table *master = (Table *)Arena<SchemaArena>::alloc(sizeof(Table));

	string_set(&master->table_name, "master_catalog");

	// Columns: id (key), type, name, tbl_name, rootpage, sql
	Column cols[] = {{intern_string("id"), TYPE_U32},		  {intern_string("type"), TYPE_CHAR32},
					 {intern_string("name"), TYPE_CHAR32},	  {intern_string("tbl_name"), TYPE_CHAR32},
					 {intern_string("rootpage"), TYPE_U32}, {intern_string("sql"), TYPE_CHAR256}};

	for (int i = 0; i < 6; i++)
	{
		array_push(&master->columns, cols[i]);
	}

	RecordLayout layout = master->to_layout();
	uint32_t	 record_size = layout.record_size;

	master->bplustree = bplustree_create(TYPE_U32, record_size, !existed);
	master->bplustree.root_page_index = 1;

	assert(master != nullptr);
	assert(master->columns.size > 0);
	assert(master->columns.size <= MAX_RECORD_LAYOUT);
	assert(get_table(master->table_name.data) == nullptr); // No duplicates

	stringmap_insert(&tables, master->table_name.data, master);
}

/*EXAMPLE OF OP_FUNCTION */
bool
evaluate_like_pattern(const uint8_t *str, const uint8_t *pattern, uint32_t str_len, uint32_t pattern_len)
{
	uint32_t s = 0, p = 0;
	uint32_t star_s = UINT32_MAX, star_p = UINT32_MAX;

	// Remove trailing spaces for VARCHAR comparison
	while (str_len > 0 && str[str_len - 1] == ' ')
		str_len--;
	while (pattern_len > 0 && pattern[pattern_len - 1] == ' ')
		pattern_len--;

	while (s < str_len)
	{
		if (p < pattern_len && pattern[p] == '%')
		{
			// Save position for backtracking
			star_p = p++;
			star_s = s;
		}
		else if (p < pattern_len && (pattern[p] == '_' || pattern[p] == str[s]))
		{
			// Single char match
			p++;
			s++;
		}
		else if (star_p != UINT32_MAX)
		{
			// Backtrack to last %
			p = star_p + 1;
			s = ++star_s;
		}
		else
		{
			return false;
		}
	}

	// Consume trailing %
	while (p < pattern_len && pattern[p] == '%')
		p++;

	return p == pattern_len;
}

bool
func(TypedValue *result_reg, TypedValue *args, uint32_t arg_count, MemoryContext *ctx)
{
	uint8_t *str = args->data;
	uint8_t *pattern = (args + 1)->data;
	uint32_t str_len = *(uint32_t *)(args + 2)->data;
	uint32_t pattern_len = *(uint32_t *)(args + 3)->data;
	bool	 match = evaluate_like_pattern(str, pattern, str_len, pattern_len);
	*(uint32_t *)result_reg->data = match ? 1 : 0;
	return true;
}

bool
load_id(TypedValue *result_reg, TypedValue *args, uint32_t arg_count, MemoryContext *ctx)
{
	Table *table = get_table((const char *)args->data);
	if (!table)
	{
		return false;
	}

	if (table->next_id.type != (args + 1)->type)
	{
		return false;
	}

	table->next_id.data = (uint8_t *)arena::alloc<SchemaArena>(table->next_id.type);
	memcpy(table->next_id.data, (args + 1)->data, table->next_id.type);
}
