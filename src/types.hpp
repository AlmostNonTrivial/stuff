// type_system.h
#pragma once
#include <cstdint>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// Type encoding: [type_id:8][flags:8][size:16]
// Layout is explicitly: TTTTTTTT FFFFFFFF SSSSSSSS SSSSSSSS
typedef uint32_t DataType;

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

    TYPE_ID_NULL = 0xFF
};

// Flags - keeping infrastructure but not using
enum TypeFlags : uint8_t {
    FLAG_NONE = 0x00
};

// Helper to construct DataType
#define MAKE_TYPE(id, flags, size) (((uint32_t)(id) << 24) | ((uint32_t)(flags) << 16) | (size))

// Static type definitions - all use FLAG_NONE now
#define TYPE_U8  MAKE_TYPE(TYPE_ID_U8,  FLAG_NONE, 1)
#define TYPE_U16 MAKE_TYPE(TYPE_ID_U16, FLAG_NONE, 2)
#define TYPE_U32 MAKE_TYPE(TYPE_ID_U32, FLAG_NONE, 4)
#define TYPE_U64 MAKE_TYPE(TYPE_ID_U64, FLAG_NONE, 8)

#define TYPE_I8  MAKE_TYPE(TYPE_ID_I8,  FLAG_NONE, 1)
#define TYPE_I16 MAKE_TYPE(TYPE_ID_I16, FLAG_NONE, 2)
#define TYPE_I32 MAKE_TYPE(TYPE_ID_I32, FLAG_NONE, 4)
#define TYPE_I64 MAKE_TYPE(TYPE_ID_I64, FLAG_NONE, 8)

#define TYPE_F32 MAKE_TYPE(TYPE_ID_F32, FLAG_NONE, 4)
#define TYPE_F64 MAKE_TYPE(TYPE_ID_F64, FLAG_NONE, 8)

// Fixed-size strings
#define TYPE_CHAR8   MAKE_TYPE(TYPE_ID_CHAR, FLAG_NONE, 8)
#define TYPE_CHAR16  MAKE_TYPE(TYPE_ID_CHAR, FLAG_NONE, 16)
#define TYPE_CHAR32  MAKE_TYPE(TYPE_ID_CHAR, FLAG_NONE, 32)
#define TYPE_CHAR64  MAKE_TYPE(TYPE_ID_CHAR, FLAG_NONE, 64)
#define TYPE_CHAR128 MAKE_TYPE(TYPE_ID_CHAR, FLAG_NONE, 128)
#define TYPE_CHAR256 MAKE_TYPE(TYPE_ID_CHAR, FLAG_NONE, 256)

// Null type
#define TYPE_NULL MAKE_TYPE(TYPE_ID_NULL, FLAG_NONE, 0)

// VARCHAR with runtime size (up to 65535 bytes)
#define TYPE_VARCHAR(len) MAKE_TYPE(TYPE_ID_VARCHAR, FLAG_NONE, ((len) & 0xFFFF))

// Factory method defines - runtime type creation
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
    return MAKE_TYPE(TYPE_ID_CHAR, FLAG_NONE, size);
}

__attribute__((always_inline))
inline DataType make_varchar(uint16_t size) {
    return TYPE_VARCHAR(size);
}

// ============================================================================
// Type property extraction - pure bit manipulation, no branches
// ============================================================================

__attribute__((always_inline))
inline uint32_t type_size(DataType type) {
    return type & 0xFFFF;
}

__attribute__((always_inline))
inline uint8_t type_id(DataType type) {
    return type >> 24;
}

__attribute__((always_inline))
inline uint8_t type_flags(DataType type) {
    return (type >> 16) & 0xFF;
}

// Alignment = size for most types, 1 for varchar (no branches via multiplication)
__attribute__((always_inline))
inline uint32_t type_align(DataType type) {
    uint32_t size = type_size(type);
    uint32_t is_varchar = (type_id(type) == TYPE_ID_VARCHAR);
    return size * !is_varchar + is_varchar;
}

// Type checking - based on type IDs, not flags
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

// ============================================================================
// Type comparison - dispatch by type ID
// ============================================================================

__attribute__((always_inline))
inline int type_compare(DataType type, const uint8_t* a, const uint8_t* b) {
    switch(type_id(type)) {
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

        // Note: modulo undefined for floats
    }
}

// ============================================================================
// Utility operations - optimized for common sizes
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
        default:
            // For strings and larger sizes
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

        // Unsigned integers
        case TYPE_ID_U8:  printf("%" PRIu8,  *(uint8_t*)data);  break;
        case TYPE_ID_U16: printf("%" PRIu16, *(uint16_t*)data); break;
        case TYPE_ID_U32: printf("%" PRIu32, *(uint32_t*)data); break;
        case TYPE_ID_U64: printf("%" PRIu64, *(uint64_t*)data); break;

        // Signed integers
        case TYPE_ID_I8:  printf("%" PRId8,  *(int8_t*)data);  break;
        case TYPE_ID_I16: printf("%" PRId16, *(int16_t*)data); break;
        case TYPE_ID_I32: printf("%" PRId32, *(int32_t*)data); break;
        case TYPE_ID_I64: printf("%" PRId64, *(int64_t*)data); break;

        // Floats
        case TYPE_ID_F32: printf("%g", *(float*)data);  break;
        case TYPE_ID_F64: printf("%g", *(double*)data); break;

        // Strings
        case TYPE_ID_CHAR: {
            uint32_t max_len = type_size(type);
            printf("%.*s", max_len, (const char*)data);
            break;
        }
        case TYPE_ID_VARCHAR:
            printf("%s", (const char*)data);
            break;
    }
}

__attribute__((always_inline))
inline const char* type_name(DataType type) {
    static char buf[32];

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
            if (size > 0) {
                snprintf(buf, sizeof(buf), "VARCHAR(%u)", size);
            } else {
                return "VARCHAR";
            }
            return buf;
        }

        case TYPE_ID_NULL: return "NULL";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// TypedValue struct
// ============================================================================

struct TypedValue {
    uint8_t* data;
    DataType type;

    // Property accessors
    inline uint8_t get_type_id() const { return type_id(type); }
    inline uint8_t get_flags() const { return type_flags(type); }
    inline uint32_t get_size() const { return type_size(type); }

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

    // Comparison operators
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

    inline const char* name() const {
        return type_name(type);
    }

    // Factory methods for creating typed values
    static TypedValue make(DataType type, void * data = nullptr) {
        return { (uint8_t*)data, type};
    }

    // static TypedValue make_null() {
    //     return { TYPE_NULL, nullptr };
    // }
};
