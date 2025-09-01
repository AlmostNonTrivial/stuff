#pragma once

#include <iostream>

#define PAGE_SIZE 512
#include <cstdint>

inline void
thr(char *err)
{

	std::cout << '\n' << err << '\n';
	exit(1);
}

struct Buffer
{
	void	*ptr;
	uint32_t size;
};

inline bool
evaluate_like_pattern(const uint8_t *str, const uint8_t *pattern, uint32_t str_len, uint32_t pattern_len)
{
	uint32_t s = 0, p = 0;
	uint32_t star_s = UINT32_MAX, star_p = UINT32_MAX;

	// Remove trailing spaces for VARCHAR comparison
	while (str_len > 0 && str[str_len - 1] == ' ')
		str_len--;
	while (pattern_len > 0 && pattern[pattern_len - 1] == ' ')
		pattern_len--;

	while (s < str_len)
	{
		if (p < pattern_len && pattern[p] == '%')
		{
			// Save position for backtracking
			star_p = p++;
			star_s = s;
		}
		else if (p < pattern_len && (pattern[p] == '_' || pattern[p] == str[s]))
		{
			// Single char match
			p++;
			s++;
		}
		else if (star_p != UINT32_MAX)
		{
			// Backtrack to last %
			p = star_p + 1;
			s = ++star_s;
		}
		else
		{
			return false;
		}
	}

	// Consume trailing %
	while (p < pattern_len && pattern[p] == '%')
		p++;

	return p == pattern_len;
}

struct QueryArena
{
};
// #define FAIL(msg) ((std::cout << msg << std::endl; exit(1)))

enum DataType : uint32_t
{
	TYPE_NULL = 0, //
	TYPE_2 = 2,
	TYPE_4 = 4,		// 4-byte integer
	TYPE_8 = 8,		// 8-byte integer
	TYPE_32 = 32,	// Variable char up to 32 bytes
	TYPE_256 = 256, // Variable char up to 256 bytes
	TYPE_BLOB = 257
};

// VM value - uses arena allocation for data
struct TypedValue
{
	DataType type;
	uint8_t *data; // Points to arena-allocated memory
};

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

enum CompareOp
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
	void (*emit_row)(TypedValue *values, size_t count); // Result callback
};

int
cmp(DataType key_size, const uint8_t *key1, const uint8_t *key2);

void
debug_type(uint8_t *data, DataType type);
#define NL	  '\n'
#define END	  << '\n'
#define PRINT std::cout <<
#define COMMA << ", " <<

// Function type definitions
typedef int (*CmpFn)(const uint8_t *, const uint8_t *);
typedef void (*CopyFn)(uint8_t *dst, const uint8_t *src);
typedef uint64_t (*ToU64Fn)(const uint8_t *);
typedef void (*FromU64Fn)(uint8_t *dst, uint64_t val);
typedef void (*PrintFn)(const uint8_t *);
typedef size_t (*SizeFn)();

// Arithmetic operations return success/failure for div-by-zero
typedef bool (*ArithFn)(uint8_t *dst, const uint8_t *a, const uint8_t *b);

// Type operations dispatch table
struct TypeOps
{
	CmpFn	  cmp;
	CopyFn	  copy;
	ToU64Fn	  to_u64;	// Convert to u64 for arithmetic
	FromU64Fn from_u64; // Convert from u64 back
	PrintFn	  print;
	SizeFn	  size;

	// Arithmetic ops
	ArithFn add;
	ArithFn sub;
	ArithFn mul;
	ArithFn div;
	ArithFn mod;
};

// Global dispatch table indexed by DataType
extern TypeOps type_ops[257];

// Initialize dispatch tables (call once at startup)
void
init_type_ops();

// Convenience functions that use dispatch tables
inline int
cmp(DataType type, const uint8_t *a, const uint8_t *b)
{
	return type_ops[type].cmp(a, b);
}

inline void
copy_value(DataType type, uint8_t *dst, const uint8_t *src)
{
	type_ops[type].copy(dst, src);
}

inline size_t
type_size(DataType type)
{
	return type_ops[type].size();
}

inline void
print_value(DataType type, const uint8_t *data)
{
	type_ops[type].print(data);
}

// Arithmetic with type promotion
bool
do_arithmetic(ArithOp op, DataType type, uint8_t *dst, const uint8_t *a, const uint8_t *b);
inline const char *
type_to_string(DataType type)
{
	switch (type)
	{
	case TYPE_4:
		return "INT32";
	case TYPE_8:
		return "INT64";
	case TYPE_32:
		return "VARCHAR32";
	case TYPE_256:
		return "VARCHAR256";
	default:
		return "VARCHAR32";
	}
}
