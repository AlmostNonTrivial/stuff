// type_system.h - 64-bit DataType with seamless composite support
#pragma once
#include <cstdint>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// 64-bit type encoding: [type_id:8][comp_count:8][size1:8][size2:8][size3:8][size4:8][total_size:16]
typedef uint64_t DataType;

// Type IDs - each type gets unique ID, no overlap between signed/unsigned
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

    TYPE_ID_MULTI = 0x50,   // Multi-component composite type

    TYPE_ID_NULL = 0xFF
};

// Helper to construct DataType
#define MAKE_TYPE_64(type_id, comp_count, size1, size2, size3, size4, total_size) \
    (((uint64_t)(type_id) << 56) | \
     ((uint64_t)(comp_count) << 48) | \
     ((uint64_t)(size1) << 40) | \
     ((uint64_t)(size2) << 32) | \
     ((uint64_t)(size3) << 24) | \
     ((uint64_t)(size4) << 16) | \
     ((uint64_t)(total_size)))

// Scalar type definitions
#define TYPE_U8  MAKE_TYPE_64(TYPE_ID_U8,  0, 1, 0, 0, 0, 1)
#define TYPE_U16 MAKE_TYPE_64(TYPE_ID_U16, 0, 2, 0, 0, 0, 2)
#define TYPE_U32 MAKE_TYPE_64(TYPE_ID_U32, 0, 4, 0, 0, 0, 4)
#define TYPE_U64 MAKE_TYPE_64(TYPE_ID_U64, 0, 8, 0, 0, 0, 8)

#define TYPE_I8  MAKE_TYPE_64(TYPE_ID_I8,  0, 1, 0, 0, 0, 1)
#define TYPE_I16 MAKE_TYPE_64(TYPE_ID_I16, 0, 2, 0, 0, 0, 2)
#define TYPE_I32 MAKE_TYPE_64(TYPE_ID_I32, 0, 4, 0, 0, 0, 4)
#define TYPE_I64 MAKE_TYPE_64(TYPE_ID_I64, 0, 8, 0, 0, 0, 8)

#define TYPE_F32 MAKE_TYPE_64(TYPE_ID_F32, 0, 4, 0, 0, 0, 4)
#define TYPE_F64 MAKE_TYPE_64(TYPE_ID_F64, 0, 8, 0, 0, 0, 8)

// Fixed-size strings
#define TYPE_CHAR8   MAKE_TYPE_64(TYPE_ID_CHAR, 0, 8, 0, 0, 0, 8)
#define TYPE_CHAR16  MAKE_TYPE_64(TYPE_ID_CHAR, 0, 16, 0, 0, 0, 16)
#define TYPE_CHAR32  MAKE_TYPE_64(TYPE_ID_CHAR, 0, 32, 0, 0, 0, 32)
#define TYPE_CHAR64  MAKE_TYPE_64(TYPE_ID_CHAR, 0, 64, 0, 0, 0, 64)
#define TYPE_CHAR128 MAKE_TYPE_64(TYPE_ID_CHAR, 0, 128, 0, 0, 0, 128)
#define TYPE_CHAR256 MAKE_TYPE_64(TYPE_ID_CHAR, 0, 256, 0, 0, 0, 256)

// Null type
#define TYPE_NULL MAKE_TYPE_64(TYPE_ID_NULL, 0, 0, 0, 0, 0, 0)

// VARCHAR with runtime size (up to 65535 bytes) - using size1 for length
#define TYPE_VARCHAR(len) MAKE_TYPE_64(TYPE_ID_VARCHAR, 0, ((len) & 0xFF), (((len) >> 8) & 0xFF), 0, 0, (len))

// Composite types - component_count > 0
#define TYPE_MULTI_U8_U8   MAKE_TYPE_64(TYPE_ID_MULTI, 2, 1, 1, 0, 0, 2)
#define TYPE_MULTI_U16_U16 MAKE_TYPE_64(TYPE_ID_MULTI, 2, 2, 2, 0, 0, 4)
#define TYPE_MULTI_U32_U32 MAKE_TYPE_64(TYPE_ID_MULTI, 2, 4, 4, 0, 0, 8)
#define TYPE_MULTI_U64_U64 MAKE_TYPE_64(TYPE_ID_MULTI, 2, 8, 8, 0, 0, 16)
#define TYPE_MULTI_U32_U64 MAKE_TYPE_64(TYPE_ID_MULTI, 2, 4, 8, 0, 0, 12)
#define TYPE_MULTI_I32_I32 MAKE_TYPE_64(TYPE_ID_MULTI, 2, 4, 4, 0, 0, 8)

// String composites (WARNING: second component uses memcmp, not strcmp!)
#define TYPE_MULTI_CHAR8_CHAR8   MAKE_TYPE_64(TYPE_ID_MULTI, 2, 8, 8, 0, 0, 16)
#define TYPE_MULTI_CHAR16_CHAR16 MAKE_TYPE_64(TYPE_ID_MULTI, 2, 16, 16, 0, 0, 32)

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
inline DataType make_char(uint16_t size) {
    return MAKE_TYPE_64(TYPE_ID_CHAR, 0, (size & 0xFF), ((size >> 8) & 0xFF), 0, 0, size);
}

__attribute__((always_inline))
inline DataType make_varchar(uint16_t size) {
    return TYPE_VARCHAR(size);
}

// Runtime composite type factory
__attribute__((always_inline))
inline DataType make_multi(uint8_t size1, uint8_t size2, uint8_t size3 = 0, uint8_t size4 = 0) {
    uint8_t comp_count = (size1 > 0) + (size2 > 0) + (size3 > 0) + (size4 > 0);
    uint16_t total = size1 + size2 + size3 + size4;
    return MAKE_TYPE_64(TYPE_ID_MULTI, comp_count, size1, size2, size3, size4, total);
}

// ============================================================================
// Type property extraction
// ============================================================================

__attribute__((always_inline))
inline uint16_t type_size(DataType type) {
    return type & 0xFFFF;
}

__attribute__((always_inline))
inline uint8_t type_id(DataType type) {
    return type >> 56;
}

__attribute__((always_inline))
inline uint8_t type_component_count(DataType type) {
    return (type >> 48) & 0xFF;
}

__attribute__((always_inline))
inline uint8_t type_component_size(DataType type, uint32_t index) {
    switch(index) {
        case 0: return (type >> 40) & 0xFF;
        case 1: return (type >> 32) & 0xFF;
        case 2: return (type >> 24) & 0xFF;
        case 3: return (type >> 16) & 0xFF;
        default: return 0;
    }
}

__attribute__((always_inline))
inline uint32_t type_component_offset(DataType type, uint32_t index) {
    uint32_t offset = 0;
    for (uint32_t i = 0; i < index; i++) {
        offset += type_component_size(type, i);
    }
    return offset;
}

// Alignment = size for most types, 1 for varchar
__attribute__((always_inline))
inline uint32_t type_align(DataType type) {
    uint32_t size = type_size(type);
    uint32_t is_varchar = (type_id(type) == TYPE_ID_VARCHAR);
    return size * !is_varchar + is_varchar;
}

// Type checking - based on type IDs
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
    return id <= TYPE_ID_F64;  // All numeric types have IDs <= 0x22
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
inline bool type_is_multi(DataType type) {
    return type_id(type) == TYPE_ID_MULTI;
}

// ============================================================================
// Type comparison - unified for scalar and composite
// ============================================================================

__attribute__((always_inline))
inline int type_compare(DataType type, const uint8_t* a, const uint8_t* b) {
    uint8_t tid = type_id(type);

    // Scalar type comparison with proper type dispatch
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
        case TYPE_ID_MULTI:
    {
               uint32_t comp_count = type_component_count(type);
               uint32_t offset = 0;

               for (uint32_t i = 0; i < comp_count; i++) {
                   uint32_t comp_size = type_component_size(type, i);
                   if (comp_size == 0) break;

                   int cmp = memcmp(a + offset, b + offset, comp_size);
                   if (cmp != 0) { return cmp; } // First difference determines result


                   offset += comp_size;
               }
               return 0;  // All components equal
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
inline bool type_note_equals(DataType type, const uint8_t* a, const uint8_t* b) {
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
        case 12: // Common for u32+u64
            *(uint64_t*)dst = *(uint64_t*)src;
            *(uint32_t*)(dst + 8) = *(uint32_t*)(src + 8);
            break;
        case 16: // Common for u64+u64
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

    if (type_is_multi(type)) {
        // Hash each component
        uint32_t comp_count = type_component_count(type);
        uint32_t offset = 0;

        for (uint32_t i = 0; i < comp_count; i++) {
            uint32_t comp_size = type_component_size(type, i);
            if (comp_size == 0) break;

            for (uint32_t j = 0; j < comp_size; j++) {
                hash = (hash ^ data[offset + j]) * 0x100000001b3ull;
            }
            offset += comp_size;
        }
        return hash;
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

        case TYPE_ID_MULTI: {
            printf("(");
            uint32_t comp_count = type_component_count(type);
            uint32_t offset = 0;

            for (uint32_t i = 0; i < comp_count; i++) {
                if (i > 0) printf(", ");
                uint32_t comp_size = type_component_size(type, i);
                if (comp_size == 0) break;

                // For now, just print as hex - could enhance with type info later
                printf("0x");
                for (uint32_t j = 0; j < comp_size; j++) {
                    printf("%02x", data[offset + j]);
                }
                offset += comp_size;
            }
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

        case TYPE_ID_MULTI: {
            uint32_t comp_count = type_component_count(type);
            uint32_t total_size = type_size(type);
            snprintf(buf, sizeof(buf), "MULTI(%u components, %u bytes)", comp_count, total_size);
            return buf;
        }

        case TYPE_ID_NULL: return "NULL";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// Composite key building helpers
// ============================================================================

__attribute__((always_inline))
inline void pack_u8_u8(uint8_t* dest, uint8_t first, uint8_t second) {
    dest[0] = first;
    dest[1] = second;
}

__attribute__((always_inline))
inline void pack_u16_u16(uint8_t* dest, uint16_t first, uint16_t second) {
    *(uint16_t*)dest = first;
    *(uint16_t*)(dest + 2) = second;
}

__attribute__((always_inline))
inline void pack_u32_u32(uint8_t* dest, uint32_t first, uint32_t second) {
    *(uint32_t*)dest = first;
    *(uint32_t*)(dest + 4) = second;
}

__attribute__((always_inline))
inline void pack_u64_u64(uint8_t* dest, uint64_t first, uint64_t second) {
    *(uint64_t*)dest = first;
    *(uint64_t*)(dest + 8) = second;
}

__attribute__((always_inline))
inline void pack_u32_u64(uint8_t* dest, uint32_t first, uint64_t second) {
    *(uint32_t*)dest = first;
    *(uint64_t*)(dest + 4) = second;
}

__attribute__((always_inline))
inline void pack_i32_i32(uint8_t* dest, int32_t first, int32_t second) {
    *(int32_t*)dest = first;
    *(int32_t*)(dest + 4) = second;
}

__attribute__((always_inline))
inline void pack_char8_char8(uint8_t* dest, const char* first, const char* second) {
    strncpy((char*)dest, first, 8);
    strncpy((char*)(dest + 8), second, 8);
}

__attribute__((always_inline))
inline void pack_char16_char16(uint8_t* dest, const char* first, const char* second) {
    strncpy((char*)dest, first, 16);
    strncpy((char*)(dest + 16), second, 16);
}

// Component extraction helpers
__attribute__((always_inline))
inline uint32_t extract_u32_at(const uint8_t* data, uint32_t offset) {
    return *(uint32_t*)(data + offset);
}

__attribute__((always_inline))
inline uint16_t extract_u16_at(const uint8_t* data, uint32_t offset) {
    return *(uint16_t*)(data + offset);
}

__attribute__((always_inline))
inline uint8_t extract_u8_at(const uint8_t* data, uint32_t offset) {
    return data[offset];
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
    inline bool is_multi() const { return type_is_multi(type); }

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

    // Comparison operators - work seamlessly with composite types
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
    static TypedValue make(DataType type, void * data = nullptr) {
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
