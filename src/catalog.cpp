#include "catalog.hpp"
#include "types.hpp"
#include <unordered_map>
#include <vector>

std::unordered_map<std::string_view, Structure> catalog;

Layout
Structure::to_layout()
{
	std::vector<DataType> column_types(columns.size());
	for (size_t i = 0; i < columns.size(); i++)
	{
		column_types[i] = columns[i].type;
	}
	return Layout::create(column_types);
}

Layout
Layout::create(std::vector<DataType> &cols)
{
	Layout layout;
	layout.layout = cols;
	layout.offsets.reserve(cols.size());

	int offset = 0;
	layout.offsets.push_back(0);
	for (int i = 1; i < cols.size(); i++)
	{
		offset += type_size(cols.at(i));
		layout.offsets.push_back(offset);
	}
	layout.record_size = offset;
	return layout;
}

Structure
Structure::from(const char *name, std::vector<Column> cols)
{
	Structure structure;
	structure.columns.assign(cols.begin(), cols.end());
	structure.name = name;
	return structure;
}
