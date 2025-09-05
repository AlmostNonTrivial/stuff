#pragma once

#include "btree.hpp"
#include "types.hpp"
#include <vector>
#include "arena.hpp"

#include <cstdint>
struct catalog_arena
{
};

struct Column
{
	const char *name;
	DataType	type;
};

struct Layout
{
	array<DataType> layout;
	array<uint32_t> offsets;

	uint32_t record_size;

	static Layout
	create(array<DataType> &column_types);

	uint32_t
	count()
	{
		return layout.size;
	}
	DataType
	key_type()
	{
		return layout[0];
	}

	// Create a new layout with swapped columns
	Layout
	reorder(std::initializer_list<uint32_t>& new_order) const
	{
		array<DataType> types = layout;
		int				i = 0;
		for (uint32_t idx : new_order)
		{
			types[i++] = layout[idx];
		}
		return Layout::create(types);
	}
};

struct Structure
{
	const char *name;
	union {
		btree btree;
	} storage;
	array<Column> columns;

	Layout
	to_layout();

	static Structure
	from(const char *name, array<Column> cols);

	uint32_t
	count(){return columns.size;} DataType key_type()
	{
		return columns[0].type;
	}
};

extern hash_map<string<catalog_arena>, Structure, catalog_arena> catalog;
#define MASTER_CATALOG "sqlite_master"

// Column names for master catalog
#define MC_TYPE		"type"
#define MC_NAME		"name"
#define MC_TBL_NAME "tbl_name"
#define MC_ROOTPAGE "rootpage"
#define MC_SQL		"sql"

// Object types stored in master catalog
#define MC_TYPE_TABLE "table"
#define MC_TYPE_INDEX "index"
void
bootstrap_master(bool create);

void
catalog_bootstrap_callback(TypedValue *result, size_t count);
