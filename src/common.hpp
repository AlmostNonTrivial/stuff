#pragma once
#include "arena.hpp"
#include <stdint.h>
#include <string_view>

using std::string_view;



struct query_arena
{
};
// #define FAIL(msg) ((std::cout << msg << std::endl; exit(1)))

enum arith_op : uint8_t
{
	ARITH_ADD = 0,
	ARITH_SUB = 1,
	ARITH_MUL = 2,
	ARITH_DIV = 3,
	ARITH_MOD = 4,
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



inline bool
to_str(std::string_view sv, char *dst, int size)
{
	if (sv.size() >= size)
	{
		return false;
	}
	size_t len = sv.size();
	memcpy(dst, sv.data(), len);
	dst[len] = '\0';
	return true;
}
