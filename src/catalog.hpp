#pragma once
#include "arena.hpp"
#include "bplustree.hpp"

#include <unordered_map>
#include <vector>
#include "defs.hpp"
#include "parser.hpp"
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
};

extern std::unordered_map<const char *, Structure> catalog;
