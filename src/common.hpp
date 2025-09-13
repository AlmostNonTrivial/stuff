#pragma once
#include "arena.hpp"
#include <stdint.h>
#include <string_view>

using std::string_view;


struct query_arena
{
};


enum arith_op : uint8_t
{
	ARITH_ADD = 0,
	ARITH_SUB = 1,
	ARITH_MUL = 2,
	ARITH_DIV = 3
};

enum logic_op : uint8_t
{
	LOGIC_AND = 0,
	LOGIC_OR = 1,
};

enum comparison_op : uint8_t
{
	EQ = 0,
	NE = 1,
	LT = 2,
	LE = 3,
	GT = 4,
	GE = 5,
};


inline void
sv_to_cstr(std::string_view sv, char *dst, int size)
{
	assert(sv.size() < size);
	size_t len = sv.size();
	memcpy(dst, sv.data(), len);
	dst[len] = '\0';
}
