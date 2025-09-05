#include "catalog.hpp"
#include "arena.hpp"
#include "types.hpp"

hash_map<string<catalog_arena>, Structure, catalog_arena> catalog;

Layout
Structure::to_layout()
{
	array<DataType> column_types;
	for (size_t i = 0; i < columns.size; i++)
	{
		column_types[i] = columns[i].type;
	}
	return Layout::create(column_types);
}

Layout
Layout::create(array<DataType> &cols)
{
	Layout layout;
	layout.layout = cols;
	layout.offsets.reserve(cols.size);

	int offset = 0;
	layout.offsets.push(0);
	for (int i = 1; i < cols.size; i++)
	{
		offset += type_size(cols[i]);
		layout.offsets.push(offset);
	}
	layout.record_size = offset;
	return layout;
}

Structure
Structure::from(const char *name, array<Column> cols)
{
	Structure structure;
	structure.columns = cols;
	structure.name = name;
	return structure;
}
