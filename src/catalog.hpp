#pragma once

#include "btree.hpp"
#include "types.hpp"
#include <unordered_map>
#include <vector>
#include <cstdint>

struct Column
{
	const char *name;
	DataType	type;
};

struct Layout
{
	std::vector<DataType> layout;
	std::vector<uint32_t> offsets;

	uint32_t record_size;

	static Layout
	create(std::vector<DataType> &column_types);

	uint32_t
	count()
	{
		return layout.size();
	}
	DataType
	key_type()
	{
		return layout[0];
	}

	// Create a new layout with swapped columns
    Layout reorder(std::initializer_list<uint32_t> new_order) const {
        std::vector<DataType> types = layout;
        int i = 0;
        for (uint32_t idx : new_order) {
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
	std::vector<Column> columns;

	Layout
	to_layout();

	static Structure
	from(const char *name, std::vector<Column> cols);

	uint32_t
	count()
	{
		return columns.size();
	}
	DataType
	key_type()
	{
		return columns[0].type;
	}
};

extern std::unordered_map<std::string_view, Structure> catalog;
