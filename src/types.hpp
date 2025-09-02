// type_system.h

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// Type encoding: [family:2][signed:1][size_log2:5]
enum DataType : uint8_t {
    // Integers: 00sxxxxx (s=signed bit, xxxxx=log2(size))
    TYPE_U8  = 0x00,  // 00000000 - size = 2^0 = 1
    TYPE_U16 = 0x01,  // 00000001 - size = 2^1 = 2
    TYPE_U32 = 0x02,  // 00000010 - size = 2^2 = 4
    TYPE_U64 = 0x03,  // 00000011 - size = 2^3 = 8

    TYPE_I8  = 0x20,  // 00100000 - size = 2^0 = 1
    TYPE_I16 = 0x21,  // 00100001 - size = 2^1 = 2
    TYPE_I32 = 0x22,  // 00100010 - size = 2^2 = 4
    TYPE_I64 = 0x23,  // 00100011 - size = 2^3 = 8

    // Floats: 01xxxxx
    TYPE_F32 = 0x42,  // 01000010 - size = 2^2 = 4
    TYPE_F64 = 0x43,  // 01000011 - size = 2^3 = 8

    // Fixed-size strings: 10xxxxx
    TYPE_CHAR8   = 0x83,  // 10000011 - size = 2^3 = 8
    TYPE_CHAR16  = 0x84,  // 10000100 - size = 2^4 = 16
    TYPE_CHAR32  = 0x85,  // 10000101 - size = 2^5 = 32
    TYPE_CHAR64  = 0x86,  // 10000110 - size = 2^6 = 64
    TYPE_CHAR128 = 0x87,  // 10000111 - size = 2^7 = 128
    TYPE_CHAR256 = 0x88,  // 10001000 - size = 2^8 = 256

    // Variable-size string: 11xxxxxx
    TYPE_VARCHAR = 0xC0,  // 11000000

    // Special
    TYPE_NULL = 0xFF
};

// ============================================================================
// Type property extraction
// ============================================================================

__attribute__((always_inline))
inline uint32_t type_size(uint8_t type) {
    if (unlikely(type == TYPE_NULL || type == TYPE_VARCHAR))
        return 0;
    return 1u << (type & 0x1F);
}

__attribute__((always_inline))
inline uint32_t type_align(uint8_t type) {
    if (unlikely(type == TYPE_VARCHAR)) return 1;
    return type_size(type);
}

__attribute__((always_inline))
inline bool type_is_integer(uint8_t type) {
    return (type & 0xC0) == 0x00;
}

__attribute__((always_inline))
inline bool type_is_float(uint8_t type) {
    return (type & 0xC0) == 0x40;
}

__attribute__((always_inline))
inline bool type_is_fixed_string(uint8_t type) {
    return (type & 0xC0) == 0x80;
}

__attribute__((always_inline))
inline bool type_is_varchar(uint8_t type) {
    return type == TYPE_VARCHAR;
}

__attribute__((always_inline))
inline bool type_is_string(uint8_t type) {
    return (type & 0x80);  // Both fixed (10) and variable (11) strings
}

__attribute__((always_inline))
inline bool type_is_numeric(uint8_t type) {
    return !(type & 0x80);  // Integers (00) or floats (01)
}

__attribute__((always_inline))
inline bool type_is_signed(uint8_t type) {
    return type_is_integer(type) && (type & 0x20);
}

__attribute__((always_inline))
inline bool type_is_null(uint8_t type) {
    return type == TYPE_NULL;
}

// ============================================================================
// Type comparison
// ============================================================================

__attribute__((always_inline))
inline int type_compare(uint8_t type, const uint8_t* a, const uint8_t* b) {
    // Most common numeric cases first
    if (likely(type_is_numeric(type))) {
        switch(type) {
            case TYPE_U8:  { uint8_t  av = *a, bv = *b; return (av > bv) - (av < bv); }
            case TYPE_U16: { uint16_t av = *(uint16_t*)a, bv = *(uint16_t*)b; return (av > bv) - (av < bv); }
            case TYPE_U32: { uint32_t av = *(uint32_t*)a, bv = *(uint32_t*)b; return (av > bv) - (av < bv); }
            case TYPE_U64: { uint64_t av = *(uint64_t*)a, bv = *(uint64_t*)b; return (av > bv) - (av < bv); }

            case TYPE_I8:  { int8_t  av = *(int8_t*)a,  bv = *(int8_t*)b;  return (av > bv) - (av < bv); }
            case TYPE_I16: { int16_t av = *(int16_t*)a, bv = *(int16_t*)b; return (av > bv) - (av < bv); }
            case TYPE_I32: { int32_t av = *(int32_t*)a, bv = *(int32_t*)b; return (av > bv) - (av < bv); }
            case TYPE_I64: { int64_t av = *(int64_t*)a, bv = *(int64_t*)b; return (av > bv) - (av < bv); }

            case TYPE_F32: {
                float av = *(float*)a, bv = *(float*)b;
                return (av > bv) - (av < bv);
            }
            case TYPE_F64: {
                double av = *(double*)a, bv = *(double*)b;
                return (av > bv) - (av < bv);
            }
        }
    }

    // String cases
    if (likely(type_is_string(type))) {
        return strcmp((char*)a, (char*)b);
    }

    return 0;
}

__attribute__((always_inline))
inline bool type_equals(uint8_t type, const uint8_t* a, const uint8_t* b) {
    // Most common numeric cases first
    if (likely(type_is_numeric(type))) {
        switch(type) {
            case TYPE_U8:  return *a == *b;
            case TYPE_U16: return *(uint16_t*)a == *(uint16_t*)b;
            case TYPE_U32: return *(uint32_t*)a == *(uint32_t*)b;
            case TYPE_U64: return *(uint64_t*)a == *(uint64_t*)b;

            case TYPE_I8:  return *(int8_t*)a == *(int8_t*)b;
            case TYPE_I16: return *(int16_t*)a == *(int16_t*)b;
            case TYPE_I32: return *(int32_t*)a == *(int32_t*)b;
            case TYPE_I64: return *(int64_t*)a == *(int64_t*)b;

            case TYPE_F32: return *(float*)a == *(float*)b;
            case TYPE_F64: return *(double*)a == *(double*)b;
        }
    }

    // String cases
    if (likely(type_is_string(type))) {
        return strcmp((char*)a, (char*)b) == 0;
    }

    return false;
}

// ============================================================================
// Arithmetic operations
// ============================================================================

__attribute__((always_inline))
inline void type_add(uint8_t type, uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    switch(type) {
        case TYPE_U8:  *(uint8_t*)dst  = *(uint8_t*)a  + *(uint8_t*)b;  break;
        case TYPE_U16: *(uint16_t*)dst = *(uint16_t*)a + *(uint16_t*)b; break;
        case TYPE_U32: *(uint32_t*)dst = *(uint32_t*)a + *(uint32_t*)b; break;
        case TYPE_U64: *(uint64_t*)dst = *(uint64_t*)a + *(uint64_t*)b; break;

        case TYPE_I8:  *(int8_t*)dst  = *(int8_t*)a  + *(int8_t*)b;  break;
        case TYPE_I16: *(int16_t*)dst = *(int16_t*)a + *(int16_t*)b; break;
        case TYPE_I32: *(int32_t*)dst = *(int32_t*)a + *(int32_t*)b; break;
        case TYPE_I64: *(int64_t*)dst = *(int64_t*)a + *(int64_t*)b; break;

        case TYPE_F32: *(float*)dst  = *(float*)a  + *(float*)b;  break;
        case TYPE_F64: *(double*)dst = *(double*)a + *(double*)b; break;
    }
}

__attribute__((always_inline))
inline void type_sub(uint8_t type, uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    switch(type) {
        case TYPE_U8:  *(uint8_t*)dst  = *(uint8_t*)a  - *(uint8_t*)b;  break;
        case TYPE_U16: *(uint16_t*)dst = *(uint16_t*)a - *(uint16_t*)b; break;
        case TYPE_U32: *(uint32_t*)dst = *(uint32_t*)a - *(uint32_t*)b; break;
        case TYPE_U64: *(uint64_t*)dst = *(uint64_t*)a - *(uint64_t*)b; break;

        case TYPE_I8:  *(int8_t*)dst  = *(int8_t*)a  - *(int8_t*)b;  break;
        case TYPE_I16: *(int16_t*)dst = *(int16_t*)a - *(int16_t*)b; break;
        case TYPE_I32: *(int32_t*)dst = *(int32_t*)a - *(int32_t*)b; break;
        case TYPE_I64: *(int64_t*)dst = *(int64_t*)a - *(int64_t*)b; break;

        case TYPE_F32: *(float*)dst  = *(float*)a  - *(float*)b;  break;
        case TYPE_F64: *(double*)dst = *(double*)a - *(double*)b; break;
    }
}

__attribute__((always_inline))
inline void type_mul(uint8_t type, uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    switch(type) {
        case TYPE_U8:  *(uint8_t*)dst  = *(uint8_t*)a  * *(uint8_t*)b;  break;
        case TYPE_U16: *(uint16_t*)dst = *(uint16_t*)a * *(uint16_t*)b; break;
        case TYPE_U32: *(uint32_t*)dst = *(uint32_t*)a * *(uint32_t*)b; break;
        case TYPE_U64: *(uint64_t*)dst = *(uint64_t*)a * *(uint64_t*)b; break;

        case TYPE_I8:  *(int8_t*)dst  = *(int8_t*)a  * *(int8_t*)b;  break;
        case TYPE_I16: *(int16_t*)dst = *(int16_t*)a * *(int16_t*)b; break;
        case TYPE_I32: *(int32_t*)dst = *(int32_t*)a * *(int32_t*)b; break;
        case TYPE_I64: *(int64_t*)dst = *(int64_t*)a * *(int64_t*)b; break;

        case TYPE_F32: *(float*)dst  = *(float*)a  * *(float*)b;  break;
        case TYPE_F64: *(double*)dst = *(double*)a * *(double*)b; break;
    }
}

__attribute__((always_inline))
inline void type_div(uint8_t type, uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    switch(type) {
        case TYPE_U8:  *(uint8_t*)dst  = *(uint8_t*)a  / *(uint8_t*)b;  break;
        case TYPE_U16: *(uint16_t*)dst = *(uint16_t*)a / *(uint16_t*)b; break;
        case TYPE_U32: *(uint32_t*)dst = *(uint32_t*)a / *(uint32_t*)b; break;
        case TYPE_U64: *(uint64_t*)dst = *(uint64_t*)a / *(uint64_t*)b; break;

        case TYPE_I8:  *(int8_t*)dst  = *(int8_t*)a  / *(int8_t*)b;  break;
        case TYPE_I16: *(int16_t*)dst = *(int16_t*)a / *(int16_t*)b; break;
        case TYPE_I32: *(int32_t*)dst = *(int32_t*)a / *(int32_t*)b; break;
        case TYPE_I64: *(int64_t*)dst = *(int64_t*)a / *(int64_t*)b; break;

        case TYPE_F32: *(float*)dst  = *(float*)a  / *(float*)b;  break;
        case TYPE_F64: *(double*)dst = *(double*)a / *(double*)b; break;
    }
}

__attribute__((always_inline))
inline void type_mod(uint8_t type, uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    switch(type) {
        case TYPE_U8:  *(uint8_t*)dst  = *(uint8_t*)a  % *(uint8_t*)b;  break;
        case TYPE_U16: *(uint16_t*)dst = *(uint16_t*)a % *(uint16_t*)b; break;
        case TYPE_U32: *(uint32_t*)dst = *(uint32_t*)a % *(uint32_t*)b; break;
        case TYPE_U64: *(uint64_t*)dst = *(uint64_t*)a % *(uint64_t*)b; break;

        case TYPE_I8:  *(int8_t*)dst  = *(int8_t*)a  % *(int8_t*)b;  break;
        case TYPE_I16: *(int16_t*)dst = *(int16_t*)a % *(int16_t*)b; break;
        case TYPE_I32: *(int32_t*)dst = *(int32_t*)a % *(int32_t*)b; break;
        case TYPE_I64: *(int64_t*)dst = *(int64_t*)a % *(int64_t*)b; break;
    }
}

// ============================================================================
// Utility operations
// ============================================================================

__attribute__((always_inline))
inline void type_copy(uint8_t type, uint8_t* dst, const uint8_t* src) {
    if (unlikely(type == TYPE_VARCHAR)) {
        strcpy((char*)dst, (char*)src);
        return;
    }

    uint32_t size = type_size(type);
    switch(size) {
        case 1: *dst = *src; break;
        case 2: *(uint16_t*)dst = *(uint16_t*)src; break;
        case 4: *(uint32_t*)dst = *(uint32_t*)src; break;
        case 8: *(uint64_t*)dst = *(uint64_t*)src; break;
        default: memcpy(dst, src, size); break;
    }
}

__attribute__((always_inline))
inline void type_zero(uint8_t type, uint8_t* dst) {
    if (unlikely(type == TYPE_VARCHAR)) {
        *dst = '\0';
        return;
    }

    uint32_t size = type_size(type);
    switch(size) {
        case 1: *dst = 0; break;
        case 2: *(uint16_t*)dst = 0; break;
        case 4: *(uint32_t*)dst = 0; break;
        case 8: *(uint64_t*)dst = 0; break;
        default: memset(dst, 0, size); break;
    }
}

__attribute__((always_inline))
inline uint64_t type_hash(uint8_t type, const uint8_t* data) {
    uint64_t hash = 0xcbf29ce484222325ull;

    if (unlikely(type_is_string(type))) {
        // Hash the actual string content
        const char* str = (const char*)data;
        while (*str) {
            hash = (hash ^ *str++) * 0x100000001b3ull;
        }
        return hash;
    }

    // Numeric types - more common path
    uint32_t size = type_size(type);
    switch(size) {
        case 1: return hash ^ *data;
        case 2: return hash ^ *(uint16_t*)data;
        case 4: return hash ^ *(uint32_t*)data;
        case 8: return hash ^ *(uint64_t*)data;
        default: return hash;
    }
}

struct TypedValue {
    DataType type;
    uint8_t* data;

    // Get size
    inline uint32_t get_size() const {
        return type_size(type);
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
        return !(*this == other);
    }

    inline void copy_to(TypedValue& dst) const {
        type_copy(type, dst.data, data);
    }
};
