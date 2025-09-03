#pragma once

#include "bplustree.hpp"
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
        Layout result;
        result.layout.reserve(layout.size());

        for (uint32_t idx : new_order) {
            result.layout.push_back(layout[idx]);
        }

        // Rebuild offsets
        result.offsets.push_back(0);
        uint32_t offset = 0;
        for (size_t i = 1; i < result.layout.size(); i++) {
            offset += type_size(result.layout[i-1]);
            result.offsets.push_back(offset);
        }
        result.record_size = offset + type_size(result.layout.back());

        return result;
    }
};

struct Structure
{
	const char *name;
	union {
		BPlusTree btree;
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
