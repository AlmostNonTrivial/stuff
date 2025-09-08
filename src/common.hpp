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

inline const char *
debug_compare_op_name(comparison_op op)
{
	static const char *names[] = {"==", "!=", "<", "<=", ">", ">="};
	return (op >= EQ && op <= GE) ? names[op] : "UNKNOWN";
}

inline const char *
debug_arith_op_name(arith_op op)
{
	static const char *names[] = {"+", "-", "*", "/", "%"};
	return (op >= ARITH_ADD && op <= ARITH_MOD) ? names[op] : "UNKNOWN";
}

inline const char *
debug_logic_op_name(logic_op op)
{
	static const char *names[] = {"AND", "OR"};
	return (op >= LOGIC_AND && op <= LOGIC_OR) ? names[op] : "UNKNOWN";
}

/*Errors would only last for one query */
template <typename arena_tag = query_arena>
inline const char *
format_error(const char *fmt, ...)
{
	size_t	len = strlen(fmt);
	char   *buffer = (char *)arena<arena_tag>::alloc(len);
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, len, fmt, args);
	va_end(args);
	return buffer;
}

inline bool
to_str(std::string_view sv, char *buffer, int size)
{
	if (sv.size() >= size)
	{
		return false;
	}
	size_t len = sv.size();
	memcpy(buffer, sv.data(), len);
	buffer[len + 1] = '\0';
	return true;
}
