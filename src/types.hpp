#pragma once
#include <string>
#include <cstdint>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "common.hpp"




// 64-bit uniform encoding - size ALWAYS in bits 0-23:
// Single types: [type_id:8][reserved:32][size:24]
// Dual types:   [TYPE_ID_DUAL:8][type1_id:8][type2_id:8][size1:8][size2:8][total_size:24]
typedef uint64_t DataType;

// Type IDs - each type gets unique ID
enum TypeId : uint8_t
{
	TYPE_ID_U8 = 0x01,
	TYPE_ID_U16 = 0x02,
	TYPE_ID_U32 = 0x03,
	TYPE_ID_U64 = 0x04,

	TYPE_ID_I8 = 0x11,
	TYPE_ID_I16 = 0x12,
	TYPE_ID_I32 = 0x13,
	TYPE_ID_I64 = 0x14,

	TYPE_ID_F32 = 0x21,
	TYPE_ID_F64 = 0x22,

	TYPE_ID_CHAR = 0x31,	// Fixed-size string
	TYPE_ID_VARCHAR = 0x32, // Variable-size string

	TYPE_ID_DUAL = 0x40, // Dual type (pair of any two types)

	TYPE_ID_NULL = 0xFF
};

// Helper for single types - size ALWAYS in bits 0-23
#define MAKE_TYPE(id, size) (((uint64_t)(id) << 56) | ((uint64_t)(size) & 0xFFFFFF))

// Helper for dual types - total size STILL in bits 0-23
#define MAKE_DUAL_TYPE(type1_id, type2_id, size1, size2)                                                               \
	(((uint64_t)(TYPE_ID_DUAL) << 56) | ((uint64_t)(type1_id) << 48) | ((uint64_t)(type2_id) << 40) |                  \
	 ((uint64_t)(size1) << 32) | ((uint64_t)(size2) << 24) | ((uint64_t)((size1) + (size2)) & 0xFFFFFF))

// Scalar type definitions
#define TYPE_U8	 MAKE_TYPE(TYPE_ID_U8, 1)
#define TYPE_U16 MAKE_TYPE(TYPE_ID_U16, 2)
#define TYPE_U32 MAKE_TYPE(TYPE_ID_U32, 4)
#define TYPE_U64 MAKE_TYPE(TYPE_ID_U64, 8)

#define TYPE_I8	 MAKE_TYPE(TYPE_ID_I8, 1)
#define TYPE_I16 MAKE_TYPE(TYPE_ID_I16, 2)
#define TYPE_I32 MAKE_TYPE(TYPE_ID_I32, 4)
#define TYPE_I64 MAKE_TYPE(TYPE_ID_I64, 8)

#define TYPE_F32 MAKE_TYPE(TYPE_ID_F32, 4)
#define TYPE_F64 MAKE_TYPE(TYPE_ID_F64, 8)

// Fixed-size strings
#define TYPE_CHAR8	 MAKE_TYPE(TYPE_ID_CHAR, 8)
#define TYPE_CHAR16	 MAKE_TYPE(TYPE_ID_CHAR, 16)
#define TYPE_CHAR32	 MAKE_TYPE(TYPE_ID_CHAR, 32)
#define TYPE_CHAR64	 MAKE_TYPE(TYPE_ID_CHAR, 64)
#define TYPE_CHAR128 MAKE_TYPE(TYPE_ID_CHAR, 128)
#define TYPE_CHAR256 MAKE_TYPE(TYPE_ID_CHAR, 256)

// Null type
#define TYPE_NULL MAKE_TYPE(TYPE_ID_NULL, 0)

// VARCHAR with runtime size
#define TYPE_VARCHAR(len) MAKE_TYPE(TYPE_ID_VARCHAR, (len))

// Factory method defines
#define make_u8()  TYPE_U8
#define make_u16() TYPE_U16
#define make_u32() TYPE_U32
#define make_u64() TYPE_U64

#define make_i8()  TYPE_I8
#define make_i16() TYPE_I16
#define make_i32() TYPE_I32
#define make_i64() TYPE_I64

#define make_f32() TYPE_F32
#define make_f64() TYPE_F64

#define make_char8()   TYPE_CHAR8
#define make_char16()  TYPE_CHAR16
#define make_char32()  TYPE_CHAR32
#define make_char64()  TYPE_CHAR64
#define make_char128() TYPE_CHAR128
#define make_char256() TYPE_CHAR256

#define make_null() TYPE_NULL

// Function declarations
DataType make_char(uint32_t size);
DataType make_varchar(uint32_t size);

// Type property extraction
uint32_t type_size(DataType type);
uint8_t type_id(DataType type);

// Dual type specific extractors (only the necessary ones)
uint8_t dual_type_id_1(DataType type);
uint8_t dual_type_id_2(DataType type);

// Type reconstruction and factory functions
DataType type_from_id_and_size(uint8_t id, uint32_t size);
DataType make_dual(DataType type1, DataType type2);
DataType dual_component_type(DataType type, uint32_t index);
uint32_t dual_component_offset(DataType type, uint32_t index);

// Type properties



bool type_is_string(DataType type);
bool type_is_numeric(DataType type);
bool type_is_null(DataType type);
bool type_is_dual(DataType type);

// Type comparison functions
int type_compare(DataType type, const void *a, const void *b);
bool type_compare_op(comparison_op op, DataType type, const void *a, const void *b);

// Inline comparison helpers
inline bool type_greater_than(DataType type, const void *a, const void *b) {
	return type_compare(type, a, b) > 0;
}

inline bool type_greater_equal(DataType type, const void *a, const void *b) {
	return type_compare(type, a, b) >= 0;
}

inline bool type_less_than(DataType type, const void *a, const void *b) {
	return type_compare(type, a, b) < 0;
}

inline bool type_less_equal(DataType type, const void *a, const void *b) {
	return type_compare(type, a, b) <= 0;
}

inline bool type_equals(DataType type, const void *a, const void *b) {
	return type_compare(type, a, b) == 0;
}

inline bool type_not_equals(DataType type, const void *a, const void *b) {
	return type_compare(type, a, b) != 0;
}

// Arithmetic operations - using macro for definition
#define DECLARE_ARITHMETIC_OP(name) \
	void type_##name(DataType type, void *dst, const void *a, const void *b);

DECLARE_ARITHMETIC_OP(add)
DECLARE_ARITHMETIC_OP(sub)
DECLARE_ARITHMETIC_OP(mul)
DECLARE_ARITHMETIC_OP(div)


// Utility operations
void type_copy(DataType type, void *dst, const void *src);
void type_zero(DataType type, void *dst);
uint64_t type_hash(DataType type, const void *data);
void type_print(DataType type, const void *data);
void type_increment(DataType type, void *dst, const void *src);
const char *type_name(DataType type);

// Dual type packing/unpacking helpers
void pack_dual(void *dest, DataType type1, const void *data1, DataType type2, const void *data2);
void unpack_dual(DataType dual_type, const void *src, void *data1, void *data2);

// TypedValue struct
struct TypedValue
{
	void *data;
	DataType type;

	// Property accessors
	uint8_t get_type_id() const;
	uint32_t get_size() const;
	bool is_dual() const;

	// Type checking
	bool is_numeric() const;
	bool is_string() const;
	bool is_null() const;

	// Set a varchar value with size
	void set_varchar(const char *str, uint32_t len = 0);

	// Comparison operators
	int compare(const TypedValue &other) const;
	bool operator>(const TypedValue &other) const;
	bool operator>=(const TypedValue &other) const;
	bool operator<(const TypedValue &other) const;
	bool operator<=(const TypedValue &other) const;
	bool operator==(const TypedValue &other) const;
	bool operator!=(const TypedValue &other) const;

	// Operations
	void copy_to(TypedValue &dst) const;
	void print() const;
	uint16_t size() const;
	const char *name() const;

	// Factory methods
	static TypedValue make(DataType type, void *data = nullptr);

	// Casting to unsigned integer types
	uint8_t as_u8() const;
	uint16_t as_u16() const;
	uint32_t as_u32() const;
	uint64_t as_u64() const;

	// Casting to signed integer types
	int8_t as_i8() const;
	int16_t as_i16() const;
	int32_t as_i32() const;
	int64_t as_i64() const;

	// Casting to floating-point types
	float as_f32() const;
	double as_f64() const;

	// Casting to string types
	const char *as_char() const;
	const char *as_varchar() const;
};
