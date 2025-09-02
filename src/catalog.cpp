#include "catalog.hpp"
#include "types.hpp"
#include <unordered_map>
#include <vector>


std::unordered_map<const char *, Structure> catalog;

Layout
Structure::to_layout()
{
	std::vector<DataType> column_types(columns.size());
	for (size_t i = 0; i < columns.size(); i++)
	{
		column_types.push_back(columns[i].type);
	}
	return Layout::create(column_types);
}
