#pragma once

#include <cstddef>
#include <iostream>
#include <cstdint>
#include "types.hpp"

#define PAGE_SIZE 512

struct Buffer
{
	void  *ptr;
	size_t size;
};

struct QueryArena
{
};
// #define FAIL(msg) ((std::cout << msg << std::endl; exit(1)))

enum ArithOp : uint8_t
{
	ARITH_ADD = 0,
	ARITH_SUB = 1,
	ARITH_MUL = 2,
	ARITH_DIV = 3,
	ARITH_MOD = 4,
};

enum LogicOp : uint8_t
{
	LOGIC_AND = 0,
	LOGIC_OR = 1,
};

enum CompareOp : uint8_t
{
	EQ = 0,
	NE = 1,
	LT = 2,
	LE = 3,
	GT = 4,
	GE = 5,
};

struct MemoryContext
{
	void *(*alloc)(size_t size);						// Function pointer for allocation
	void (*free)(void *ptr, size_t size);				// Function pointer for allocation
	void (*emit_row)(TypedValue *values, size_t count); // Result callback
};

inline void
arithmetic(ArithOp op, DataType type, uint8_t *dst, uint8_t *a, uint8_t *b)
{
	switch (op)
	{
	case ARITH_ADD:
		type_add(type, dst, a, b);
		break;
	case ARITH_SUB:
		type_sub(type, dst, a, b);
		break;
	case ARITH_MUL:
		type_mul(type, dst, a, b);
		break;
	case ARITH_DIV:
		type_div(type, dst, a, b);
		break;
	case ARITH_MOD:
		type_mod(type, dst, a, b);
		break;
	}
}
