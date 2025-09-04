#pragma once
#include "types.hpp"
#include "arena.hpp"
#include <cstddef>

// ============================================================================
// Arena-allocated factory methods
// ============================================================================

// Allocate and initialize a value in the specified arena
template <typename ArenaTag = global_arena>
static TypedValue
alloc(DataType type, const void *src = nullptr)
{
	uint32_t size = type_size(type);
	uint8_t *data = (uint8_t *)arena::alloc<ArenaTag>(size);

	if (src)
	{
		type_copy(type, data, (const uint8_t *)src);
	}
	else
	{
		type_zero(type, data);
	}

	return {data, type};
}

// Allocate scalar types
template <typename ArenaTag, typename T>
static TypedValue
alloc_scalar(DataType type, T value)
{
	static_assert(sizeof(T) <= 8, "Scalar too large");
	uint8_t *data = (uint8_t *)arena::alloc<ArenaTag>(sizeof(T));
	*(T *)data = value;
	return {data, type};
}

// Specialized allocators for common types
template <typename ArenaTag = global_arena>
static TypedValue
alloc_u8(uint8_t val)
{
	return alloc_scalar<ArenaTag>(TYPE_U8, val);
}

template <typename ArenaTag = global_arena>
static TypedValue
alloc_u16(uint16_t val)
{
	return alloc_scalar<ArenaTag>(TYPE_U16, val);
}

template <typename ArenaTag = global_arena>
static TypedValue
alloc_u32(uint32_t val)
{
	return alloc_scalar<ArenaTag>(TYPE_U32, val);
}

template <typename ArenaTag = global_arena>
static TypedValue
alloc_u64(uint64_t val)
{
	return alloc_scalar<ArenaTag>(TYPE_U64, val);
}

template <typename ArenaTag = global_arena>
static TypedValue
alloc_i32(int32_t val)
{
	return alloc_scalar<ArenaTag>(TYPE_I32, val);
}

template <typename ArenaTag = global_arena>
static TypedValue
alloc_i64(int64_t val)
{
	return alloc_scalar<ArenaTag>(TYPE_I64, val);
}

template <typename ArenaTag = global_arena>
static TypedValue
alloc_f32(float val)
{
	return alloc_scalar<ArenaTag>(TYPE_F32, val);
}

template <typename ArenaTag = global_arena>
static TypedValue
alloc_f64(double val)
{
	return alloc_scalar<ArenaTag>(TYPE_F64, val);
}

// String allocators - handles proper null termination and sizing
template <typename ArenaTag = global_arena>
static TypedValue
alloc_char(const char *str, uint32_t size)
{

    // mem
	char *data = (char *)arena::alloc<ArenaTag>(size);
	if (str)
	{
		strncpy(data, str, size - 1);
	}
	return {(uint8_t *)data, make_char(size)};
}

template <typename ArenaTag = global_arena>
static TypedValue
alloc_char8(const char *str)
{
	return alloc_char<ArenaTag>(str, 8);
}

template <typename ArenaTag = global_arena>
static TypedValue
alloc_char16(const char *str)
{
	return alloc_char<ArenaTag>(str, 16);
}

template <typename ArenaTag = global_arena>
static TypedValue
alloc_char32(const char *str)
{
	return alloc_char<ArenaTag>(str, 32);
}

template <typename ArenaTag = global_arena>
static TypedValue
alloc_char64(const char *str)
{
	return alloc_char<ArenaTag>(str, 64);
}

template <typename ArenaTag = global_arena>
static TypedValue
alloc_char128(const char *str)
{
	return alloc_char<ArenaTag>(str, 128);
}

template <typename ArenaTag = global_arena>
static TypedValue
alloc_char256(const char *str)
{
	return alloc_char<ArenaTag>(str, 256);
}

// VARCHAR - dynamically sized
template <typename ArenaTag = global_arena>
static TypedValue
alloc_varchar(const char *str, size_t size)
{
	size_t len;
	if (str)
	{
		if (size)
		{
			len = size;
		}
		else
		{
			len = strlen(str) + 1;
		}
	}
	else
	{
		len = 1;
	}

char *data = (char *)arena::alloc<ArenaTag>(len);
if (str)
{
	strcpy(data, str);
}
else
{
	data[0] = '\0';
}
return {(uint8_t *)data, TYPE_VARCHAR(len)};
}

// Null value
template <typename ArenaTag = global_arena>
static TypedValue
alloc_null()
{
	return {nullptr, TYPE_NULL};
}

// Dual type allocator
template <typename ArenaTag = global_arena>
static TypedValue
alloc_dual(DataType type1, const void *data1, DataType type2, const void *data2)
{
	DataType dual_type = make_dual(type1, type2);
	uint32_t total_size = type_size(dual_type);
	uint8_t *data = (uint8_t *)arena::alloc<ArenaTag>(total_size);

	pack_dual(data, type1, (const uint8_t *)data1, type2, (const uint8_t *)data2);
	return {data, dual_type};
}
