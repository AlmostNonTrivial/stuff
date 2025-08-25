// schema.hpp - Compressed version with only used functions
#pragma once
#include "arena.hpp"
#include "map.hpp"
#include "btree.hpp"
#include "bplustree.hpp"
#include "defs.hpp"
#include "parser.hpp"
#include "str.hpp"
#include "vec.hpp"
#include <cassert>
#include <cstdint>

#define MAX_RECORD_LAYOUT 32

struct RegistryArena
{
};

// ============================================================================
// Core Types
// ============================================================================

enum class TreeType
{
	BTREE,
	BPLUSTREE
};

// RecordLayout - Pure record interpretation
struct RecordLayout
{
	Vec<DataType, QueryArena> layout;
	Vec<uint32_t, QueryArena> offsets;
	uint32_t record_size;

	DataType
	key_type() const
	{
		return layout[0];
	}
	uint32_t
	column_count() const
	{
		return layout.size();
	}
	uint32_t
	get_offset(uint32_t col_index) const
	{
		return offsets[col_index];
	}

	static RecordLayout
	create(Vec<DataType, QueryArena> &column_types);
	static RecordLayout
	create(DataType key, DataType rec = TYPE_NULL);
};

// Column metadata
struct ColumnInfo
{
	Str<QueryArena> name;
	DataType type;
};

// Index - Secondary index metadata
struct Index
{
	Str<RegistryArena> index_name;
	Str<RegistryArena> table_name;
	TreeType tree_type;
	union {
		BTree btree;
		BPlusTree bplustree;
	} tree;
	uint32_t column_index;

	RecordLayout
	to_layout() const;
};

// Table - Complete table metadata
struct Table
{
	Str<RegistryArena> table_name;
	Vec<ColumnInfo, RegistryArena> columns;
	Map<uint32_t, Index *, RegistryArena> indexes;
	TreeType tree_type;
	union {
		BTree btree;
		BPlusTree bplustree;
	} tree;

	RecordLayout
	to_layout() const;
};

// Schema snapshot for transaction support
struct SchemaSnapshots
{
	struct Entry
	{
		const char *table;
		uint32_t root;
		Vec<std::pair<uint32_t, uint32_t>, QueryArena> indexes;
	};
	Vec<Entry, QueryArena> entries;
};

// ============================================================================
// Registry Operations (Used by VM and Executor)
// ============================================================================

Table *
get_table(const char *table_name);
Index *
get_index(const char *table_name, uint32_t column_index); // VM uses this
Index *
get_index(const char *table_name, const char *index_name); // VM uses this
Index *
get_index(const char *index_name);
void
remove_index(const char *table_name, uint32_t column_index);
void
remove_table(const char *table_name);

// ============================================================================
// Schema Queries (Used by VM and Compiler)
// ============================================================================

uint32_t
get_column_index(const char *table_name, const char *col_name);
DataType
get_column_type(const char *table_name, uint32_t col_index);

// ============================================================================
// Factory Functions (Used by Executor)
// ============================================================================

Table *
create_table(CreateTableNode *node);
Index *
create_index(CreateIndexNode *node);
void
create_master(); // Special case for sqlite_master

// ============================================================================
// Transaction Support (Used by Executor)
// ============================================================================

SchemaSnapshots
take_snapshot();

void schema_clear();
