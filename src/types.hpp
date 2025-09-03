// type_system.h - 64-bit DataType with dual type support
#pragma once
#include <cstdint>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// 64-bit type encoding:
// Simplified layout for ALL types: [type_id:8][reserved:32][size:24]
// For duals: [TYPE_ID_DUAL:8][type1_id:8][type2_id:8][reserved:8][size1:12][size2:12][total_size:8]
typedef uint64_t DataType;

// Type IDs - each type gets unique ID
enum TypeId : uint8_t {
    TYPE_ID_U8   = 0x01,
    TYPE_ID_U16  = 0x02,
    TYPE_ID_U32  = 0x03,
    TYPE_ID_U64  = 0x04,

    TYPE_ID_I8   = 0x11,
    TYPE_ID_I16  = 0x12,
    TYPE_ID_I32  = 0x13,
    TYPE_ID_I64  = 0x14,

    TYPE_ID_F32  = 0x21,
    TYPE_ID_F64  = 0x22,

    TYPE_ID_CHAR = 0x31,    // Fixed-size string
    TYPE_ID_VARCHAR = 0x32, // Variable-size string

    TYPE_ID_DUAL = 0x40,    // Dual type (pair of any two types)

    TYPE_ID_NULL = 0xFF
};

// Simplified, consistent layout for ALL types
#define MAKE_TYPE(id, size) \
    (((uint64_t)(id) << 56) | ((uint64_t)(size) & 0xFFFFFF))

// Dual type: [TYPE_ID_DUAL:8][type1_id:8][type2_id:8][reserved:8][size1:12][size2:12][total_size:8]
#define MAKE_DUAL_TYPE(type1_id, type2_id, size1, size2) \
    (((uint64_t)(TYPE_ID_DUAL) << 56) | \
     ((uint64_t)(type1_id) << 48) | \
     ((uint64_t)(type2_id) << 40) | \
     ((uint64_t)((size1) & 0xFFF) << 28) | \
     ((uint64_t)((size2) & 0xFFF) << 16) | \
     ((uint64_t)(((size1) + (size2)) & 0xFF)))

// Scalar type definitions
#define TYPE_U8  MAKE_TYPE(TYPE_ID_U8,  1)
#define TYPE_U16 MAKE_TYPE(TYPE_ID_U16, 2)
#define TYPE_U32 MAKE_TYPE(TYPE_ID_U32, 4)
#define TYPE_U64 MAKE_TYPE(TYPE_ID_U64, 8)

#define TYPE_I8  MAKE_TYPE(TYPE_ID_I8,  1)
#define TYPE_I16 MAKE_TYPE(TYPE_ID_I16, 2)
#define TYPE_I32 MAKE_TYPE(TYPE_ID_I32, 4)
#define TYPE_I64 MAKE_TYPE(TYPE_ID_I64, 8)

#define TYPE_F32 MAKE_TYPE(TYPE_ID_F32, 4)
#define TYPE_F64 MAKE_TYPE(TYPE_ID_F64, 8)

// Fixed-size strings
#define TYPE_CHAR8   MAKE_TYPE(TYPE_ID_CHAR, 8)
#define TYPE_CHAR16  MAKE_TYPE(TYPE_ID_CHAR, 16)
#define TYPE_CHAR32  MAKE_TYPE(TYPE_ID_CHAR, 32)
#define TYPE_CHAR64  MAKE_TYPE(TYPE_ID_CHAR, 64)
#define TYPE_CHAR128 MAKE_TYPE(TYPE_ID_CHAR, 128)
#define TYPE_CHAR256 MAKE_TYPE(TYPE_ID_CHAR, 256)

// Null type
#define TYPE_NULL MAKE_TYPE(TYPE_ID_NULL, 0)

// VARCHAR with runtime size (up to 16MB)
#define TYPE_VARCHAR(len) MAKE_TYPE(TYPE_ID_VARCHAR, (len))

// Factory method defines
#define make_u8()    TYPE_U8
#define make_u16()   TYPE_U16
#define make_u32()   TYPE_U32
#define make_u64()   TYPE_U64

#define make_i8()    TYPE_I8
#define make_i16()   TYPE_I16
#define make_i32()   TYPE_I32
#define make_i64()   TYPE_I64

#define make_f32()   TYPE_F32
#define make_f64()   TYPE_F64

#define make_char8()   TYPE_CHAR8
#define make_char16()  TYPE_CHAR16
#define make_char32()  TYPE_CHAR32
#define make_char64()  TYPE_CHAR64
#define make_char128() TYPE_CHAR128
#define make_char256() TYPE_CHAR256

#define make_null()  TYPE_NULL

// Functions for parameterized types
__attribute__((always_inline))
inline DataType make_char(uint32_t size) {
    return MAKE_TYPE(TYPE_ID_CHAR, size);
}

__attribute__((always_inline))
inline DataType make_varchar(uint32_t size) {
    return MAKE_TYPE(TYPE_ID_VARCHAR, size);
}

// ============================================================================
// Type property extraction
// ============================================================================

__attribute__((always_inline))
inline uint32_t type_size(DataType type) {
    uint8_t tid = type >> 56;
    if (tid == TYPE_ID_DUAL) {
        return type & 0xFF;  // Total size in lowest byte for dual types
    }
    return type & 0xFFFFFF;  // Size always in bits 0-23 for regular types
}

__attribute__((always_inline))
inline uint8_t type_id(DataType type) {
    return type >> 56;
}

// Dual type specific extractors
__attribute__((always_inline))
inline uint8_t dual_type_id_1(DataType type) {
    return (type >> 48) & 0xFF;
}

__attribute__((always_inline))
inline uint8_t dual_type_id_2(DataType type) {
    return (type >> 40) & 0xFF;
}

__attribute__((always_inline))
inline uint16_t dual_size_1(DataType type) {
    return (type >> 28) & 0xFFF;
}

__attribute__((always_inline))
inline uint16_t dual_size_2(DataType type) {
    return (type >> 16) & 0xFFF;
}

// Reconstruct full DataType from component ID and size
__attribute__((always_inline))
inline DataType type_from_id_and_size(uint8_t id, uint32_t size) {
    switch(id) {
        case TYPE_ID_U8:  return TYPE_U8;
        case TYPE_ID_U16: return TYPE_U16;
        case TYPE_ID_U32: return TYPE_U32;
        case TYPE_ID_U64: return TYPE_U64;

        case TYPE_ID_I8:  return TYPE_I8;
        case TYPE_ID_I16: return TYPE_I16;
        case TYPE_ID_I32: return TYPE_I32;
        case TYPE_ID_I64: return TYPE_I64;

        case TYPE_ID_F32: return TYPE_F32;
        case TYPE_ID_F64: return TYPE_F64;

        case TYPE_ID_CHAR: return make_char(size);
        case TYPE_ID_VARCHAR: return make_varchar(size);

        default: return TYPE_NULL;
    }
}

// Factory function for dual types
__attribute__((always_inline))
inline DataType make_dual(DataType type1, DataType type2) {
    uint8_t id1 = type_id(type1);
    uint8_t id2 = type_id(type2);
    uint32_t size1 = type_size(type1);
    uint32_t size2 = type_size(type2);
    return MAKE_DUAL_TYPE(id1, id2, size1, size2);
}

// Get component types from dual
__attribute__((always_inline))
inline DataType dual_component_type(DataType type, uint32_t index) {
    if (type_id(type) != TYPE_ID_DUAL) return TYPE_NULL;

    if (index == 0) {
        return type_from_id_and_size(dual_type_id_1(type), dual_size_1(type));
    } else if (index == 1) {
        return type_from_id_and_size(dual_type_id_2(type), dual_size_2(type));
    }
    return TYPE_NULL;
}

// Component offset for dual types
__attribute__((always_inline))
inline uint32_t dual_component_offset(DataType type, uint32_t index) {
    if (index == 0) return 0;
    if (index == 1) return dual_size_1(type);
    return 0;
}



// Alignment
__attribute__((always_inline))
inline uint32_t type_align(DataType type) {
    uint32_t size = type_size(type);
    uint32_t is_varchar = (type_id(type) == TYPE_ID_VARCHAR);
    return size * !is_varchar + is_varchar;
}

// Type checking
__attribute__((always_inline))
inline bool type_is_unsigned(DataType type) {
    uint8_t id = type_id(type);
    return id >= TYPE_ID_U8 && id <= TYPE_ID_U64;
}

__attribute__((always_inline))
inline bool type_is_signed(DataType type) {
    uint8_t id = type_id(type);
    return id >= TYPE_ID_I8 && id <= TYPE_ID_I64;
}

__attribute__((always_inline))
inline bool type_is_float(DataType type) {
    uint8_t id = type_id(type);
    return id == TYPE_ID_F32 || id == TYPE_ID_F64;
}

__attribute__((always_inline))
inline bool type_is_string(DataType type) {
    uint8_t id = type_id(type);
    return id == TYPE_ID_CHAR || id == TYPE_ID_VARCHAR;
}

__attribute__((always_inline))
inline bool type_is_numeric(DataType type) {
    uint8_t id = type_id(type);
    return id <= TYPE_ID_F64;
}

__attribute__((always_inline))
inline bool type_is_integer(DataType type) {
    return type_is_unsigned(type) || type_is_signed(type);
}

__attribute__((always_inline))
inline bool type_is_fixed_string(DataType type) {
    return type_id(type) == TYPE_ID_CHAR;
}

__attribute__((always_inline))
inline bool type_is_varchar(DataType type) {
    return type_id(type) == TYPE_ID_VARCHAR;
}

__attribute__((always_inline))
inline bool type_is_null(DataType type) {
    return type_id(type) == TYPE_ID_NULL;
}

__attribute__((always_inline))
inline bool type_is_dual(DataType type) {
    return type_id(type) == TYPE_ID_DUAL;
}

// ============================================================================
// Type comparison - unified for scalar and dual
// ============================================================================

__attribute__((always_inline))
inline int type_compare(DataType type, const uint8_t* a, const uint8_t* b) {
    uint8_t tid = type_id(type);

    // Scalar type comparison
    switch(tid) {
        case TYPE_ID_U8:  { uint8_t  av = *a, bv = *b; return (av > bv) - (av < bv); }
        case TYPE_ID_U16: { uint16_t av = *(uint16_t*)a, bv = *(uint16_t*)b; return (av > bv) - (av < bv); }
        case TYPE_ID_U32: { uint32_t av = *(uint32_t*)a, bv = *(uint32_t*)b; return (av > bv) - (av < bv); }
        case TYPE_ID_U64: { uint64_t av = *(uint64_t*)a, bv = *(uint64_t*)b; return (av > bv) - (av < bv); }

        case TYPE_ID_I8:  { int8_t  av = *(int8_t*)a,  bv = *(int8_t*)b;  return (av > bv) - (av < bv); }
        case TYPE_ID_I16: { int16_t av = *(int16_t*)a, bv = *(int16_t*)b; return (av > bv) - (av < bv); }
        case TYPE_ID_I32: { int32_t av = *(int32_t*)a, bv = *(int32_t*)b; return (av > bv) - (av < bv); }
        case TYPE_ID_I64: { int64_t av = *(int64_t*)a, bv = *(int64_t*)b; return (av > bv) - (av < bv); }

        case TYPE_ID_F32: { float av = *(float*)a, bv = *(float*)b; return (av > bv) - (av < bv); }
        case TYPE_ID_F64: { double av = *(double*)a, bv = *(double*)b; return (av > bv) - (av < bv); }

        case TYPE_ID_CHAR:
        case TYPE_ID_VARCHAR:
            return strcmp((char*)a, (char*)b);

        case TYPE_ID_DUAL: {
            // Compare first component
            DataType type1 = dual_component_type(type, 0);
            int cmp1 = type_compare(type1, a, b);
            if (cmp1 != 0) return cmp1;

            // Compare second component if first is equal
            DataType type2 = dual_component_type(type, 1);
            uint32_t offset = dual_size_1(type);
            return type_compare(type2, a + offset, b + offset);
        }

        default: return 0;
    }
}

__attribute__((always_inline))
inline bool type_greater_than(DataType type, const uint8_t* a, const uint8_t* b) {
    return type_compare(type, a, b) > 0;
}

__attribute__((always_inline))
inline bool type_greater_equal(DataType type, const uint8_t* a, const uint8_t* b) {
    return type_compare(type, a, b) >= 0;
}

__attribute__((always_inline))
inline bool type_less_than(DataType type, const uint8_t* a, const uint8_t* b) {
    return type_compare(type, a, b) < 0;
}

__attribute__((always_inline))
inline bool type_less_equal(DataType type, const uint8_t* a, const uint8_t* b) {
    return type_compare(type, a, b) <= 0;
}

__attribute__((always_inline))
inline bool type_equals(DataType type, const uint8_t* a, const uint8_t* b) {
    return type_compare(type, a, b) == 0;
}

__attribute__((always_inline))
inline bool type_not_equals(DataType type, const uint8_t* a, const uint8_t* b) {
    return type_compare(type, a, b) != 0;
}

// ============================================================================
// Arithmetic operations
// ============================================================================

__attribute__((always_inline))
inline void type_add(DataType type, uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    switch(type_id(type)) {
        case TYPE_ID_U8:  *(uint8_t*)dst  = *(uint8_t*)a  + *(uint8_t*)b;  break;
        case TYPE_ID_U16: *(uint16_t*)dst = *(uint16_t*)a + *(uint16_t*)b; break;
        case TYPE_ID_U32: *(uint32_t*)dst = *(uint32_t*)a + *(uint32_t*)b; break;
        case TYPE_ID_U64: *(uint64_t*)dst = *(uint64_t*)a + *(uint64_t*)b; break;

        case TYPE_ID_I8:  *(int8_t*)dst  = *(int8_t*)a  + *(int8_t*)b;  break;
        case TYPE_ID_I16: *(int16_t*)dst = *(int16_t*)a + *(int16_t*)b; break;
        case TYPE_ID_I32: *(int32_t*)dst = *(int32_t*)a + *(int32_t*)b; break;
        case TYPE_ID_I64: *(int64_t*)dst = *(int64_t*)a + *(int64_t*)b; break;

        case TYPE_ID_F32: *(float*)dst  = *(float*)a  + *(float*)b;  break;
        case TYPE_ID_F64: *(double*)dst = *(double*)a + *(double*)b; break;
    }
}

__attribute__((always_inline))
inline void type_sub(DataType type, uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    switch(type_id(type)) {
        case TYPE_ID_U8:  *(uint8_t*)dst  = *(uint8_t*)a  - *(uint8_t*)b;  break;
        case TYPE_ID_U16: *(uint16_t*)dst = *(uint16_t*)a - *(uint16_t*)b; break;
        case TYPE_ID_U32: *(uint32_t*)dst = *(uint32_t*)a - *(uint32_t*)b; break;
        case TYPE_ID_U64: *(uint64_t*)dst = *(uint64_t*)a - *(uint64_t*)b; break;

        case TYPE_ID_I8:  *(int8_t*)dst  = *(int8_t*)a  - *(int8_t*)b;  break;
        case TYPE_ID_I16: *(int16_t*)dst = *(int16_t*)a - *(int16_t*)b; break;
        case TYPE_ID_I32: *(int32_t*)dst = *(int32_t*)a - *(int32_t*)b; break;
        case TYPE_ID_I64: *(int64_t*)dst = *(int64_t*)a - *(int64_t*)b; break;

        case TYPE_ID_F32: *(float*)dst  = *(float*)a  - *(float*)b;  break;
        case TYPE_ID_F64: *(double*)dst = *(double*)a - *(double*)b; break;
    }
}

__attribute__((always_inline))
inline void type_mul(DataType type, uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    switch(type_id(type)) {
        case TYPE_ID_U8:  *(uint8_t*)dst  = *(uint8_t*)a  * *(uint8_t*)b;  break;
        case TYPE_ID_U16: *(uint16_t*)dst = *(uint16_t*)a * *(uint16_t*)b; break;
        case TYPE_ID_U32: *(uint32_t*)dst = *(uint32_t*)a * *(uint32_t*)b; break;
        case TYPE_ID_U64: *(uint64_t*)dst = *(uint64_t*)a * *(uint64_t*)b; break;

        case TYPE_ID_I8:  *(int8_t*)dst  = *(int8_t*)a  * *(int8_t*)b;  break;
        case TYPE_ID_I16: *(int16_t*)dst = *(int16_t*)a * *(int16_t*)b; break;
        case TYPE_ID_I32: *(int32_t*)dst = *(int32_t*)a * *(int32_t*)b; break;
        case TYPE_ID_I64: *(int64_t*)dst = *(int64_t*)a * *(int64_t*)b; break;

        case TYPE_ID_F32: *(float*)dst  = *(float*)a  * *(float*)b;  break;
        case TYPE_ID_F64: *(double*)dst = *(double*)a * *(double*)b; break;
    }
}

__attribute__((always_inline))
inline void type_div(DataType type, uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    switch(type_id(type)) {
        case TYPE_ID_U8:  *(uint8_t*)dst  = *(uint8_t*)a  / *(uint8_t*)b;  break;
        case TYPE_ID_U16: *(uint16_t*)dst = *(uint16_t*)a / *(uint16_t*)b; break;
        case TYPE_ID_U32: *(uint32_t*)dst = *(uint32_t*)a / *(uint32_t*)b; break;
        case TYPE_ID_U64: *(uint64_t*)dst = *(uint64_t*)a / *(uint64_t*)b; break;

        case TYPE_ID_I8:  *(int8_t*)dst  = *(int8_t*)a  / *(int8_t*)b;  break;
        case TYPE_ID_I16: *(int16_t*)dst = *(int16_t*)a / *(int16_t*)b; break;
        case TYPE_ID_I32: *(int32_t*)dst = *(int32_t*)a / *(int32_t*)b; break;
        case TYPE_ID_I64: *(int64_t*)dst = *(int64_t*)a / *(int64_t*)b; break;

        case TYPE_ID_F32: *(float*)dst  = *(float*)a  / *(float*)b;  break;
        case TYPE_ID_F64: *(double*)dst = *(double*)a / *(double*)b; break;
    }
}

__attribute__((always_inline))
inline void type_mod(DataType type, uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    switch(type_id(type)) {
        case TYPE_ID_U8:  *(uint8_t*)dst  = *(uint8_t*)a  % *(uint8_t*)b;  break;
        case TYPE_ID_U16: *(uint16_t*)dst = *(uint16_t*)a % *(uint16_t*)b; break;
        case TYPE_ID_U32: *(uint32_t*)dst = *(uint32_t*)a % *(uint32_t*)b; break;
        case TYPE_ID_U64: *(uint64_t*)dst = *(uint64_t*)a % *(uint64_t*)b; break;

        case TYPE_ID_I8:  *(int8_t*)dst  = *(int8_t*)a  % *(int8_t*)b;  break;
        case TYPE_ID_I16: *(int16_t*)dst = *(int16_t*)a % *(int16_t*)b; break;
        case TYPE_ID_I32: *(int32_t*)dst = *(int32_t*)a % *(int32_t*)b; break;
        case TYPE_ID_I64: *(int64_t*)dst = *(int64_t*)a % *(int64_t*)b; break;
    }
}

// ============================================================================
// Utility operations
// ============================================================================

__attribute__((always_inline))
inline void type_copy(DataType type, uint8_t* dst, const uint8_t* src) {
    uint32_t size = type_size(type);

    // Common sizes get optimized paths
    switch(size) {
        case 0: break;  // NULL type
        case 1: *dst = *src; break;
        case 2: *(uint16_t*)dst = *(uint16_t*)src; break;
        case 4: *(uint32_t*)dst = *(uint32_t*)src; break;
        case 8: *(uint64_t*)dst = *(uint64_t*)src; break;
        case 12: // Common for dual u32+u64
            *(uint64_t*)dst = *(uint64_t*)src;
            *(uint32_t*)(dst + 8) = *(uint32_t*)(src + 8);
            break;
        case 16: // Common for dual u64+u64
            *(uint64_t*)dst = *(uint64_t*)src;
            *(uint64_t*)(dst + 8) = *(uint64_t*)(src + 8);
            break;
        default:
            // For strings and other sizes
            if (type_is_string(type)) {
                strcpy((char*)dst, (char*)src);
            } else {
                memcpy(dst, src, size);
            }
            break;
    }
}

__attribute__((always_inline))
inline void type_zero(DataType type, uint8_t* dst) {
    uint32_t size = type_size(type);

    switch(size) {
        case 0: break;  // NULL type
        case 1: *dst = 0; break;
        case 2: *(uint16_t*)dst = 0; break;
        case 4: *(uint32_t*)dst = 0; break;
        case 8: *(uint64_t*)dst = 0; break;
        case 12: *(uint64_t*)dst = 0; *(uint32_t*)(dst + 8) = 0; break;
        case 16: *(uint64_t*)dst = 0; *(uint64_t*)(dst + 8) = 0; break;
        default: memset(dst, 0, size); break;
    }
}

__attribute__((always_inline))
inline uint64_t type_hash(DataType type, const uint8_t* data) {
    uint64_t hash = 0xcbf29ce484222325ull;

    if (type_is_string(type)) {
        const char* str = (const char*)data;
        while (*str) {
            hash = (hash ^ *str++) * 0x100000001b3ull;
        }
        return hash;
    }

    if (type_is_dual(type)) {
        // Hash first component
        DataType type1 = dual_component_type(type, 0);
        uint64_t hash1 = type_hash(type1, data);

        // Hash second component
        DataType type2 = dual_component_type(type, 1);
        uint64_t hash2 = type_hash(type2, data + dual_size_1(type));

        // Combine hashes
        return hash1 ^ (hash2 * 0x100000001b3ull);
    }

    // Numeric types - hash based on size
    uint32_t size = type_size(type);
    switch(size) {
        case 1: return hash ^ *data;
        case 2: return hash ^ *(uint16_t*)data;
        case 4: return hash ^ *(uint32_t*)data;
        case 8: return hash ^ *(uint64_t*)data;
        default: return hash;
    }
}

__attribute__((always_inline))
inline void type_print(DataType type, const uint8_t* data) {
    switch(type_id(type)) {
        case TYPE_ID_NULL: printf("NULL"); break;

        case TYPE_ID_U8:  printf("%" PRIu8,  *(uint8_t*)data);  break;
        case TYPE_ID_U16: printf("%" PRIu16, *(uint16_t*)data); break;
        case TYPE_ID_U32: printf("%" PRIu32, *(uint32_t*)data); break;
        case TYPE_ID_U64: printf("%" PRIu64, *(uint64_t*)data); break;

        case TYPE_ID_I8:  printf("%" PRId8,  *(int8_t*)data);  break;
        case TYPE_ID_I16: printf("%" PRId16, *(int16_t*)data); break;
        case TYPE_ID_I32: printf("%" PRId32, *(int32_t*)data); break;
        case TYPE_ID_I64: printf("%" PRId64, *(int64_t*)data); break;

        case TYPE_ID_F32: printf("%g", *(float*)data);  break;
        case TYPE_ID_F64: printf("%g", *(double*)data); break;

        case TYPE_ID_CHAR: {
            uint32_t max_len = type_size(type);
            printf("%.*s", max_len, (const char*)data);
            break;
        }
        case TYPE_ID_VARCHAR:
            printf("%s", (const char*)data);
            break;

        case TYPE_ID_DUAL: {
            printf("(");

            // Print first component
            DataType type1 = dual_component_type(type, 0);
            type_print(type1, data);

            printf(", ");

            // Print second component
            DataType type2 = dual_component_type(type, 1);
            type_print(type2, data + dual_size_1(type));

            printf(")");
            break;
        }
    }
}

__attribute__((always_inline))
inline const char* type_name(DataType type) {
    static char buf[64];

    switch(type_id(type)) {
        case TYPE_ID_U8:  return "U8";
        case TYPE_ID_U16: return "U16";
        case TYPE_ID_U32: return "U32";
        case TYPE_ID_U64: return "U64";

        case TYPE_ID_I8:  return "I8";
        case TYPE_ID_I16: return "I16";
        case TYPE_ID_I32: return "I32";
        case TYPE_ID_I64: return "I64";

        case TYPE_ID_F32: return "F32";
        case TYPE_ID_F64: return "F64";

        case TYPE_ID_CHAR: {
            uint32_t size = type_size(type);
            snprintf(buf, sizeof(buf), "CHAR%u", size);
            return buf;
        }

        case TYPE_ID_VARCHAR: {
            uint32_t size = type_size(type);
            snprintf(buf, sizeof(buf), "VARCHAR(%u)", size);
            return buf;
        }

        case TYPE_ID_DUAL: {
            DataType type1 = dual_component_type(type, 0);
            DataType type2 = dual_component_type(type, 1);
            snprintf(buf, sizeof(buf), "DUAL(%s,%s)",
                     type_name(type1), type_name(type2));
            return buf;
        }

        case TYPE_ID_NULL: return "NULL";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// Dual type packing/unpacking helpers
// ============================================================================

__attribute__((always_inline))
inline void pack_dual(uint8_t* dest, DataType type1, const uint8_t* data1,
                      DataType type2, const uint8_t* data2) {
    type_copy(type1, dest, data1);
    type_copy(type2, dest + type_size(type1), data2);
}

__attribute__((always_inline))
inline void unpack_dual(DataType dual_type, const uint8_t* src,
                        uint8_t* data1, uint8_t* data2) {
    DataType type1 = dual_component_type(dual_type, 0);
    DataType type2 = dual_component_type(dual_type, 1);

    type_copy(type1, data1, src);
    type_copy(type2, data2, src + dual_size_1(dual_type));
}

// ============================================================================
// TypedValue struct - unchanged, works with 64-bit DataType
// ============================================================================

struct TypedValue {
    uint8_t* data;
    DataType type;

    // Property accessors
    inline uint8_t get_type_id() const { return type_id(type); }
    inline uint32_t get_size() const { return type_size(type); }
    inline bool is_dual() const { return type_is_dual(type); }

    // Type checking
    inline bool is_numeric() const { return type_is_numeric(type); }
    inline bool is_string() const { return type_is_string(type); }
    inline bool is_signed() const { return type_is_signed(type); }
    inline bool is_unsigned() const { return type_is_unsigned(type); }
    inline bool is_float() const { return type_is_float(type); }
    inline bool is_null() const { return type_is_null(type); }

    // Set a varchar value with size
    inline void set_varchar(const char* str, uint32_t len = 0) {
        len = len ? len : strlen(str);
        type = TYPE_VARCHAR(len);
        data = (uint8_t*)str;
    }

    // Comparison operators - work seamlessly with dual types
    inline int compare(const TypedValue& other) const {
        return type_compare(type, data, other.data);
    }

    inline bool operator>(const TypedValue& other) const {
        return compare(other) > 0;
    }

    inline bool operator>=(const TypedValue& other) const {
        return compare(other) >= 0;
    }

    inline bool operator<(const TypedValue& other) const {
        return compare(other) < 0;
    }

    inline bool operator<=(const TypedValue& other) const {
        return compare(other) <= 0;
    }

    inline bool operator==(const TypedValue& other) const {
        return type_equals(type, data, other.data);
    }

    inline bool operator!=(const TypedValue& other) const {
        return !type_equals(type, data, other.data);
    }

    // Operations
    inline void copy_to(TypedValue& dst) const {
        dst.type = type;
        type_copy(type, dst.data, data);
    }

    inline void print() const {
        type_print(type, data);
    }

    inline uint16_t size() const {
        return type_size(this->type);
    }

    inline const char* name() const {
        return type_name(type);
    }

    // Factory methods
    static TypedValue make(DataType type, void* data = nullptr) {
        return { (uint8_t*)data, type};
    }

    // Casting to unsigned integer types
    uint8_t as_u8() const {
        return *reinterpret_cast<uint8_t*>(data);
    }

    uint16_t as_u16() const {
        return *reinterpret_cast<uint16_t*>(data);
    }

    uint32_t as_u32() const {
        return *reinterpret_cast<uint32_t*>(data);
    }

    uint64_t as_u64() const {
        return *reinterpret_cast<uint64_t*>(data);
    }

    // Casting to signed integer types
    int8_t as_i8() const {
        return *reinterpret_cast<int8_t*>(data);
    }

    int16_t as_i16() const {
        return *reinterpret_cast<int16_t*>(data);
    }

    int32_t as_i32() const {
        return *reinterpret_cast<int32_t*>(data);
    }

    int64_t as_i64() const {
        return *reinterpret_cast<int64_t*>(data);
    }

    // Casting to floating-point types
    float as_f32() const {
        return *reinterpret_cast<float*>(data);
    }

    double as_f64() const {
        return *reinterpret_cast<double*>(data);
    }

    // Casting to string types
    const char* as_char() const {
        return reinterpret_cast<const char*>(data);
    }

    const char* as_varchar() const {
        return reinterpret_cast<const char*>(data);
    }
};
