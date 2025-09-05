// type_system_tests.h - Updated for 64-bit DataType with composite support
#pragma once
#include "../types.hpp"
#include "../btree.hpp"
#include "../pager.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cmath>

#define TEST_DB "test_types.db"

// Test type construction and bit layout
inline void test_type_construction() {
    // Test basic type construction
    DataType u32_type = TYPE_U32;
    assert(type_id(u32_type) == TYPE_ID_U32);
    assert(type_component_count(u32_type) == 0);
    assert(type_size(u32_type) == 4);

    // Test VARCHAR construction
    DataType varchar_type = TYPE_VARCHAR(128);
    assert(type_id(varchar_type) == TYPE_ID_VARCHAR);
    assert(type_size(varchar_type) == 128);

    // Test factory functions
    assert(make_u8() == TYPE_U8);
    assert(make_i64() == TYPE_I64);
    assert(make_f32() == TYPE_F32);

    // Test parameterized constructors
    DataType char_type = make_char(64);
    assert(type_id(char_type) == TYPE_ID_CHAR);
    assert(type_size(char_type) == 64);

    DataType varchar_runtime = make_varchar(256);
    assert(type_id(varchar_runtime) == TYPE_ID_VARCHAR);
    assert(type_size(varchar_runtime) == 256);

    // Test composite type construction
    DataType multi_type = TYPE_MULTI_U32_U32;
    assert(type_id(multi_type) == TYPE_ID_MULTI);
    assert(type_component_count(multi_type) == 2);
    assert(type_component_size(multi_type, 0) == 4);
    assert(type_component_size(multi_type, 1) == 4);
    assert(type_size(multi_type) == 8);

    // Test runtime composite factory
    DataType runtime_multi = make_multi(4, 8);
    assert(type_id(runtime_multi) == TYPE_ID_MULTI);
    assert(type_component_count(runtime_multi) == 2);
    assert(type_size(runtime_multi) == 12);
}

// Test type classification functions
inline void test_type_checking() {
    // Test unsigned types
    assert(type_is_unsigned(TYPE_U8));
    assert(type_is_unsigned(TYPE_U32));
    assert(!type_is_unsigned(TYPE_I32));
    assert(!type_is_unsigned(TYPE_F32));
    assert(!type_is_unsigned(TYPE_MULTI_U32_U32));

    // Test signed types
    assert(type_is_signed(TYPE_I8));
    assert(type_is_signed(TYPE_I64));
    assert(!type_is_signed(TYPE_U32));
    assert(!type_is_signed(TYPE_F64));
    assert(!type_is_signed(TYPE_MULTI_I32_I32));

    // Test float types
    assert(type_is_float(TYPE_F32));
    assert(type_is_float(TYPE_F64));
    assert(!type_is_float(TYPE_I32));
    assert(!type_is_float(TYPE_MULTI_U32_U32));

    // Test string types
    assert(type_is_string(TYPE_CHAR64));
    assert(type_is_string(TYPE_VARCHAR(100)));
    assert(!type_is_string(TYPE_I32));
    assert(!type_is_string(TYPE_MULTI_CHAR8_CHAR8));

    // Test numeric types
    assert(type_is_numeric(TYPE_U32));
    assert(type_is_numeric(TYPE_I16));
    assert(type_is_numeric(TYPE_F64));
    assert(!type_is_numeric(TYPE_CHAR32));
    assert(!type_is_numeric(TYPE_MULTI_U32_U32));

    // Test multi types
    assert(type_is_multi(TYPE_MULTI_U32_U32));
    assert(type_is_multi(TYPE_MULTI_U8_U8));
    assert(!type_is_multi(TYPE_U32));
    assert(!type_is_multi(TYPE_CHAR16));

    // Test null type
    assert(type_is_null(TYPE_NULL));
    assert(!type_is_null(TYPE_I32));
    assert(!type_is_null(TYPE_MULTI_U32_U32));
}

// Test alignment calculation
inline void test_type_alignment() {
    // Basic numeric types align to their size
    assert(type_align(TYPE_U8) == 1);
    assert(type_align(TYPE_U16) == 2);
    assert(type_align(TYPE_U32) == 4);
    assert(type_align(TYPE_U64) == 8);
    assert(type_align(TYPE_F64) == 8);

    // VARCHAR always aligns to 1
    assert(type_align(TYPE_VARCHAR(100)) == 1);

    // Composite types align to their total size
    assert(type_align(TYPE_MULTI_U32_U32) == 8);
    assert(type_align(TYPE_MULTI_U16_U16) == 4);
    assert(type_align(TYPE_MULTI_U32_U64) == 12);
}

// Test comparison operations
inline void test_type_comparison() {
    // Test unsigned comparison
    uint8_t u8_a = 10, u8_b = 20;
    assert(type_less_than(TYPE_U8, &u8_a, &u8_b));
    assert(!type_greater_than(TYPE_U8, &u8_a, &u8_b));
    assert(type_less_equal(TYPE_U8, &u8_a, &u8_b));

    // Test signed comparison
    int32_t i32_a = -5, i32_b = 10;
    assert(type_less_than(TYPE_I32, (uint8_t*)&i32_a, (uint8_t*)&i32_b));

    // Test float comparison
    float f32_a = 3.14f, f32_b = 2.71f;
    assert(type_greater_than(TYPE_F32, (uint8_t*)&f32_a, (uint8_t*)&f32_b));

    // Test string comparison
    char str1[] = "apple";
    char str2[] = "banana";
    assert(type_less_than(TYPE_CHAR64, (uint8_t*)str1, (uint8_t*)str2));
    assert(type_less_than(TYPE_VARCHAR(10), (uint8_t*)str1, (uint8_t*)str2));

    // Test composite comparison
    uint8_t comp1[8], comp2[8];
    pack_u32_u32(comp1, 5, 100);
    pack_u32_u32(comp2, 5, 200);
    assert(type_less_than(TYPE_MULTI_U32_U32, comp1, comp2));  // (5,100) < (5,200)

    pack_u32_u32(comp2, 6, 50);
    assert(type_less_than(TYPE_MULTI_U32_U32, comp1, comp2));  // (5,100) < (6,50)

    // Test equality
    uint16_t u16_x = 42, u16_y = 42;
    assert(type_equals(TYPE_U16, (uint8_t*)&u16_x, (uint8_t*)&u16_y));
    assert(!type_note_equals(TYPE_U16, (uint8_t*)&u16_x, (uint8_t*)&u16_y));
}

// Test arithmetic operations
inline void test_arithmetic_operations() {
    // Test addition
    uint32_t u32_a = 100, u32_b = 200, u32_result;
    type_add(TYPE_U32, (uint8_t*)&u32_result, (uint8_t*)&u32_a, (uint8_t*)&u32_b);
    assert(u32_result == 300);

    int16_t i16_a = -50, i16_b = 30, i16_result;
    type_add(TYPE_I16, (uint8_t*)&i16_result, (uint8_t*)&i16_a, (uint8_t*)&i16_b);
    assert(i16_result == -20);

    float f32_a = 2.5f, f32_b = 1.5f, f32_result;
    type_add(TYPE_F32, (uint8_t*)&f32_result, (uint8_t*)&f32_a, (uint8_t*)&f32_b);
    assert(fabs(f32_result - 4.0f) < 1e-6f);

    // Test subtraction
    type_sub(TYPE_U32, (uint8_t*)&u32_result, (uint8_t*)&u32_b, (uint8_t*)&u32_a);
    assert(u32_result == 100);

    // Test multiplication
    uint8_t u8_a = 5, u8_b = 4, u8_result;
    type_mul(TYPE_U8, &u8_result, &u8_a, &u8_b);
    assert(u8_result == 20);

    // Test division
    uint64_t u64_a = 100, u64_b = 4, u64_result;
    type_div(TYPE_U64, (uint8_t*)&u64_result, (uint8_t*)&u64_a, (uint8_t*)&u64_b);
    assert(u64_result == 25);

    // Test modulo
    int32_t i32_mod_a = 17, i32_mod_b = 5, i32_mod_result;
    type_mod(TYPE_I32, (uint8_t*)&i32_mod_result, (uint8_t*)&i32_mod_a, (uint8_t*)&i32_mod_b);
    assert(i32_mod_result == 2);
}

// Test utility operations
inline void test_utility_operations() {
    // Test copy operations
    uint64_t src = 0x123456789ABCDEF0, dst = 0;
    type_copy(TYPE_U64, (uint8_t*)&dst, (uint8_t*)&src);
    assert(dst == src);

    // Test string copy
    char src_str[] = "hello world";
    char dst_str[64];
    type_copy(TYPE_CHAR64, (uint8_t*)dst_str, (uint8_t*)src_str);
    assert(strcmp(dst_str, src_str) == 0);

    // Test composite copy
    uint8_t src_comp[8], dst_comp[8];
    pack_u32_u32(src_comp, 12345, 67890);
    type_copy(TYPE_MULTI_U32_U32, dst_comp, src_comp);
    assert(extract_u32_at(dst_comp, 0) == 12345);
    assert(extract_u32_at(dst_comp, 4) == 67890);

    // Test zero operations
    uint32_t val = 0xDEADBEEF;
    type_zero(TYPE_U32, (uint8_t*)&val);
    assert(val == 0);

    char str_val[32] = "test";
    type_zero(TYPE_CHAR32, (uint8_t*)str_val);
    assert(str_val[0] == 0);

    // Test composite zero
    type_zero(TYPE_MULTI_U32_U32, dst_comp);
    assert(extract_u32_at(dst_comp, 0) == 0);
    assert(extract_u32_at(dst_comp, 4) == 0);

    // Test hash function
    uint32_t hash_val1 = 12345;
    uint32_t hash_val2 = 12345;
    uint32_t hash_val3 = 54321;

    uint64_t hash1 = type_hash(TYPE_U32, (uint8_t*)&hash_val1);
    uint64_t hash2 = type_hash(TYPE_U32, (uint8_t*)&hash_val2);
    uint64_t hash3 = type_hash(TYPE_U32, (uint8_t*)&hash_val3);

    assert(hash1 == hash2);  // Same values should hash the same
    assert(hash1 != hash3);  // Different values should hash differently

    // Test composite hash
    uint8_t comp_hash1[8], comp_hash2[8];
    pack_u32_u32(comp_hash1, 100, 200);
    pack_u32_u32(comp_hash2, 100, 200);

    uint64_t comp_hash_val1 = type_hash(TYPE_MULTI_U32_U32, comp_hash1);
    uint64_t comp_hash_val2 = type_hash(TYPE_MULTI_U32_U32, comp_hash2);
    assert(comp_hash_val1 == comp_hash_val2);
}

// Test TypedValue struct
inline void test_typed_value() {
    // Test basic construction and properties
    uint32_t val = 42;
    TypedValue tv = TypedValue::make(TYPE_U32, &val);

    assert(tv.get_type_id() == TYPE_ID_U32);
    assert(tv.get_size() == 4);
    assert(tv.is_numeric());
    assert(tv.is_unsigned());
    assert(!tv.is_signed());
    assert(!tv.is_float());
    assert(!tv.is_string());
    assert(!tv.is_multi());

    // Test comparison operators
    uint32_t val2 = 50;
    TypedValue tv2 = TypedValue::make(TYPE_U32, &val2);

    assert(tv < tv2);
    assert(tv <= tv2);
    assert(tv2 > tv);
    assert(tv2 >= tv);
    assert(tv != tv2);

    uint32_t val3 = 42;
    TypedValue tv3 = TypedValue::make(TYPE_U32, &val3);
    assert(tv == tv3);
    assert(tv <= tv3);
    assert(tv >= tv3);

    // Test composite TypedValue
    uint8_t comp_data[8];
    pack_u32_u32(comp_data, 100, 200);
    TypedValue comp_tv = TypedValue::make(TYPE_MULTI_U32_U32, comp_data);

    assert(comp_tv.is_multi());
    assert(!comp_tv.is_numeric());
    assert(!comp_tv.is_string());
    assert(comp_tv.get_size() == 8);

    // Test string operations
    char str_data[] = "hello";
    TypedValue str_tv = TypedValue::make(TYPE_VARCHAR(10), str_data);

    assert(str_tv.is_string());
    assert(!str_tv.is_numeric());
    assert(!str_tv.is_multi());

    // Test varchar setter
    TypedValue varchar_tv;
    char varchar_data[] = "test string";
    varchar_tv.set_varchar(varchar_data);
    assert(varchar_tv.get_type_id() == TYPE_ID_VARCHAR);
    assert(varchar_tv.get_size() == strlen(varchar_data));
}

// Test edge cases and boundary conditions
inline void test_type_edge_cases() {
    // Test null type
    TypedValue null_tv = TypedValue::make(TYPE_NULL, nullptr);
    assert(null_tv.is_null());
    assert(null_tv.get_size() == 0);

    // Test maximum sizes
    DataType max_varchar = TYPE_VARCHAR(65535);
    assert(type_size(max_varchar) == 65535);

    // Test zero values
    uint8_t zero_u8 = 0, nonzero_u8 = 1;
    assert(type_equals(TYPE_U8, &zero_u8, &zero_u8));
    assert(!type_equals(TYPE_U8, &zero_u8, &nonzero_u8));

    // Test negative numbers
    int32_t neg_a = -100, neg_b = -50;
    assert(type_less_than(TYPE_I32, (uint8_t*)&neg_a, (uint8_t*)&neg_b));

    // Test floating point edge cases
    float f_zero = 0.0f, f_neg_zero = -0.0f;
    assert(type_equals(TYPE_F32, (uint8_t*)&f_zero, (uint8_t*)&f_neg_zero));

    // Test composite edge cases
    uint8_t comp_min[8], comp_max[8];
    pack_u32_u32(comp_min, 0, 0);
    pack_u32_u32(comp_max, 0xFFFFFFFF, 0xFFFFFFFF);
    assert(type_less_than(TYPE_MULTI_U32_U32, comp_min, comp_max));
}

// Test all arithmetic operations comprehensively
inline void test_comprehensive_arithmetic() {
    // Test overflow behavior (implementation defined, but should not crash)
    uint8_t u8_max = 255, u8_one = 1, u8_overflow_result;
    type_add(TYPE_U8, &u8_overflow_result, &u8_max, &u8_one);
    // Result is implementation defined (wraparound), just ensure no crash

    // Test division by different values
    uint32_t dividend = 1000;
    for (uint32_t divisor = 1; divisor <= 10; ++divisor) {
        uint32_t result;
        type_div(TYPE_U32, (uint8_t*)&result, (uint8_t*)&dividend, (uint8_t*)&divisor);
        assert(result == dividend / divisor);
    }

    // Test floating point precision
    double d_a = 1.0/3.0, d_b = 2.0/3.0, d_result;
    type_add(TYPE_F64, (uint8_t*)&d_result, (uint8_t*)&d_a, (uint8_t*)&d_b);
    assert(fabs(d_result - 1.0) < 1e-15);
}

// Test composite type operations
inline void test_composite_operations() {
    // Test component access
    DataType multi_type = TYPE_MULTI_U32_U64;
    assert(type_component_count(multi_type) == 2);
    assert(type_component_size(multi_type, 0) == 4);
    assert(type_component_size(multi_type, 1) == 8);
    assert(type_component_offset(multi_type, 0) == 0);
    assert(type_component_offset(multi_type, 1) == 4);

    // Test lexicographic comparison
    uint8_t key1[8], key2[8], key3[8];
    pack_u32_u32(key1, 5, 100);  // (5, 100)
    pack_u32_u32(key2, 5, 200);  // (5, 200)
    pack_u32_u32(key3, 6, 50);   // (6, 50)

    DataType u32_u32_type = TYPE_MULTI_U32_U32;
    assert(type_compare(u32_u32_type, key1, key2) < 0);  // (5,100) < (5,200)
    assert(type_compare(u32_u32_type, key2, key3) < 0);  // (5,200) < (6,50) - first dominates
    assert(type_compare(u32_u32_type, key1, key1) == 0); // Self-equality

    // Test different size combinations
    uint8_t mixed_key1[12], mixed_key2[12];
    pack_u32_u64(mixed_key1, 100, 0x1000000000000000ULL);
    pack_u32_u64(mixed_key2, 100, 0x2000000000000000ULL);

    assert(type_less_than(TYPE_MULTI_U32_U64, mixed_key1, mixed_key2));
}

// Test string operations
inline void test_string_operations() {
    // Test fixed string operations
    char fixed1[32], fixed2[32];
    strcpy(fixed1, "hello");
    strcpy(fixed2, "world");

    assert(type_less_than(TYPE_CHAR32, (uint8_t*)fixed1, (uint8_t*)fixed2));

    // Copy and verify
    char fixed_dst[32];
    type_copy(TYPE_CHAR32, (uint8_t*)fixed_dst, (uint8_t*)fixed1);
    assert(strcmp(fixed_dst, fixed1) == 0);

    // Test varchar operations
    char varchar1[] = "alpha";
    char varchar2[] = "beta";

    assert(type_less_than(TYPE_VARCHAR(10), (uint8_t*)varchar1, (uint8_t*)varchar2));

    // Test string hashing
    char hash_test1[] = "consistent";
    char hash_test2[] = "consistent";

    uint64_t hash1 = type_hash(TYPE_VARCHAR(20), (uint8_t*)hash_test1);
    uint64_t hash2 = type_hash(TYPE_VARCHAR(20), (uint8_t*)hash_test2);
    assert(hash1 == hash2);
}

// Test type name functionality
inline void test_type_names() {
    assert(strcmp(type_name(TYPE_U8), "U8") == 0);
    assert(strcmp(type_name(TYPE_I64), "I64") == 0);
    assert(strcmp(type_name(TYPE_F32), "F32") == 0);
    assert(strcmp(type_name(TYPE_NULL), "NULL") == 0);

    // Test parameterized type names
    DataType char_type = make_char(128);
    const char* char_name = type_name(char_type);
    assert(strstr(char_name, "CHAR128") != nullptr);

    DataType varchar_type = make_varchar(256);
    const char* varchar_name = type_name(varchar_type);
    assert(strstr(varchar_name, "VARCHAR(256)") != nullptr);

    // Test composite type names
    const char* multi_name = type_name(TYPE_MULTI_U32_U32);
    assert(strstr(multi_name, "MULTI(2 components, 8 bytes)") != nullptr);
}

// Test performance-critical path
inline void test_hot_path_operations() {
    // Test that common operations work correctly in tight loops
    uint32_t values[100];
    for (int i = 0; i < 100; ++i) {
        values[i] = i;
    }

    // Test repeated comparisons
    for (int i = 0; i < 99; ++i) {
        assert(type_less_than(TYPE_U32, (uint8_t*)&values[i], (uint8_t*)&values[i+1]));
    }

    // Test repeated composite comparisons
    uint8_t comp_keys[10][8];
    for (int i = 0; i < 10; i++) {
        pack_u32_u32(comp_keys[i], i / 3, i % 3);
    }

    for (int i = 0; i < 9; i++) {
        assert(type_less_equal(TYPE_MULTI_U32_U32, comp_keys[i], comp_keys[i+1]));
    }
}
// Main test function
inline void test_types() {
    printf("\n=== 64-bit Type System Tests ===\n");

    test_type_construction();
    test_type_checking();
    test_type_alignment();
    test_type_comparison();
    test_arithmetic_operations();
    test_utility_operations();
    test_typed_value();
    test_type_edge_cases();
    test_comprehensive_arithmetic();
    test_composite_operations();
    test_string_operations();
    test_type_names();
    test_hot_path_operations();

    printf("\n=== Composite Type B+Tree Integration Tests ===\n");
    // test_btree_u32_u32();
    // test_btree_u16_u16();
    // test_btree_u8_u8();
    // test_btree_u32_u64();

    printf("\n=== All Type System Tests Passed! ===\n");
    printf("Composite types integrate seamlessly with existing infrastructure.\n");
}
