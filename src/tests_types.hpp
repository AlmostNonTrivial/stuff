// test_type_system.cpp

#include <cassert>
#include <cstdio>
#include "types.hpp"

inline static void test_type_sizes() {
    assert(type_size(TYPE_U8) == 1);
    assert(type_size(TYPE_U16) == 2);
    assert(type_size(TYPE_U32) == 4);
    assert(type_size(TYPE_U64) == 8);

    assert(type_size(TYPE_I8) == 1);
    assert(type_size(TYPE_I16) == 2);
    assert(type_size(TYPE_I32) == 4);
    assert(type_size(TYPE_I64) == 8);

    assert(type_size(TYPE_F32) == 4);
    assert(type_size(TYPE_F64) == 8);

    assert(type_size(TYPE_CHAR8) == 8);
    assert(type_size(TYPE_CHAR16) == 16);
    assert(type_size(TYPE_CHAR32) == 32);
    assert(type_size(TYPE_CHAR64) == 64);
    assert(type_size(TYPE_CHAR128) == 128);
    assert(type_size(TYPE_CHAR256) == 256);
}

inline static void test_type_properties() {
    // Test integer detection
    assert(type_is_integer(TYPE_U8));
    assert(type_is_integer(TYPE_U16));
    assert(type_is_integer(TYPE_U32));
    assert(type_is_integer(TYPE_U64));
    assert(type_is_integer(TYPE_I8));
    assert(type_is_integer(TYPE_I16));
    assert(type_is_integer(TYPE_I32));
    assert(type_is_integer(TYPE_I64));
    assert(!type_is_integer(TYPE_F32));
    assert(!type_is_integer(TYPE_F64));
    assert(!type_is_integer(TYPE_CHAR8));
    assert(!type_is_integer(TYPE_VARCHAR));

    // Test float detection
    assert(type_is_float(TYPE_F32));
    assert(type_is_float(TYPE_F64));
    assert(!type_is_float(TYPE_U32));
    assert(!type_is_float(TYPE_I32));
    assert(!type_is_float(TYPE_CHAR8));

    // Test string detection
    assert(type_is_fixed_string(TYPE_CHAR8));
    assert(type_is_fixed_string(TYPE_CHAR16));
    assert(type_is_fixed_string(TYPE_CHAR32));
    assert(type_is_fixed_string(TYPE_CHAR64));
    assert(type_is_fixed_string(TYPE_CHAR128));
    assert(type_is_fixed_string(TYPE_CHAR256));
    assert(!type_is_fixed_string(TYPE_VARCHAR));
    assert(!type_is_fixed_string(TYPE_U32));

    assert(type_is_varchar(TYPE_VARCHAR));
    assert(!type_is_varchar(TYPE_CHAR8));

    assert(type_is_string(TYPE_CHAR8));
    assert(type_is_string(TYPE_VARCHAR));
    assert(!type_is_string(TYPE_U32));

    // Test numeric detection
    assert(type_is_numeric(TYPE_U8));
    assert(type_is_numeric(TYPE_I32));
    assert(type_is_numeric(TYPE_F64));
    assert(!type_is_numeric(TYPE_CHAR8));
    assert(!type_is_numeric(TYPE_VARCHAR));

    // Test signed detection
    assert(!type_is_signed(TYPE_U8));
    assert(!type_is_signed(TYPE_U16));
    assert(!type_is_signed(TYPE_U32));
    assert(!type_is_signed(TYPE_U64));
    assert(type_is_signed(TYPE_I8));
    assert(type_is_signed(TYPE_I16));
    assert(type_is_signed(TYPE_I32));
    assert(type_is_signed(TYPE_I64));
    assert(!type_is_signed(TYPE_F32)); // Floats don't count as signed integers

    // Test null detection
    assert(type_is_null(TYPE_NULL));
    assert(!type_is_null(TYPE_U8));


}

inline static void test_type_compare() {
    // Test unsigned integers
    uint8_t u8_a = 10, u8_b = 20, u8_c = 10;
    assert(type_compare(TYPE_U8, &u8_a, &u8_b) < 0);
    assert(type_compare(TYPE_U8, &u8_b, &u8_a) > 0);
    assert(type_compare(TYPE_U8, &u8_a, &u8_c) == 0);

    // Test signed integers
    int32_t i32_a = -10, i32_b = 10, i32_c = -10;
    assert(type_compare(TYPE_I32, (uint8_t*)&i32_a, (uint8_t*)&i32_b) < 0);
    assert(type_compare(TYPE_I32, (uint8_t*)&i32_b, (uint8_t*)&i32_a) > 0);
    assert(type_compare(TYPE_I32, (uint8_t*)&i32_a, (uint8_t*)&i32_c) == 0);

    // Test floats
    float f32_a = 1.5f, f32_b = 2.5f, f32_c = 1.5f;
    assert(type_compare(TYPE_F32, (uint8_t*)&f32_a, (uint8_t*)&f32_b) < 0);
    assert(type_compare(TYPE_F32, (uint8_t*)&f32_b, (uint8_t*)&f32_a) > 0);
    assert(type_compare(TYPE_F32, (uint8_t*)&f32_a, (uint8_t*)&f32_c) == 0);

    // Test strings
    char str1[] = "apple";
    char str2[] = "banana";
    char str3[] = "apple";
    assert(type_compare(TYPE_VARCHAR, (uint8_t*)str1, (uint8_t*)str2) < 0);
    assert(type_compare(TYPE_VARCHAR, (uint8_t*)str2, (uint8_t*)str1) > 0);
    assert(type_compare(TYPE_VARCHAR, (uint8_t*)str1, (uint8_t*)str3) == 0);
}

inline static void test_type_equals() {
    // Test integers
    uint32_t u32_a = 42, u32_b = 42, u32_c = 43;
    assert(type_equals(TYPE_U32, (uint8_t*)&u32_a, (uint8_t*)&u32_b));
    assert(!type_equals(TYPE_U32, (uint8_t*)&u32_a, (uint8_t*)&u32_c));

    // Test floats
    double f64_a = 3.14159, f64_b = 3.14159, f64_c = 2.71828;
    assert(type_equals(TYPE_F64, (uint8_t*)&f64_a, (uint8_t*)&f64_b));
    assert(!type_equals(TYPE_F64, (uint8_t*)&f64_a, (uint8_t*)&f64_c));

    // Test strings
    char str1[] = "hello";
    char str2[] = "hello";
    char str3[] = "world";
    assert(type_equals(TYPE_VARCHAR, (uint8_t*)str1, (uint8_t*)str2));
    assert(!type_equals(TYPE_VARCHAR, (uint8_t*)str1, (uint8_t*)str3));
}

inline static void test_arithmetic_ops() {
    // Test addition
    uint32_t u32_a = 100, u32_b = 200, u32_result;
    type_add(TYPE_U32, (uint8_t*)&u32_result, (uint8_t*)&u32_a, (uint8_t*)&u32_b);
    assert(u32_result == 300);

    int16_t i16_a = -50, i16_b = 30, i16_result;
    type_add(TYPE_I16, (uint8_t*)&i16_result, (uint8_t*)&i16_a, (uint8_t*)&i16_b);
    assert(i16_result == -20);

    float f32_a = 1.5f, f32_b = 2.5f, f32_result;
    type_add(TYPE_F32, (uint8_t*)&f32_result, (uint8_t*)&f32_a, (uint8_t*)&f32_b);
    assert(f32_result == 4.0f);

    // Test subtraction
    type_sub(TYPE_U32, (uint8_t*)&u32_result, (uint8_t*)&u32_b, (uint8_t*)&u32_a);
    assert(u32_result == 100);

    type_sub(TYPE_I16, (uint8_t*)&i16_result, (uint8_t*)&i16_b, (uint8_t*)&i16_a);
    assert(i16_result == 80);

    // Test multiplication
    uint8_t u8_a = 5, u8_b = 6, u8_result;
    type_mul(TYPE_U8, &u8_result, &u8_a, &u8_b);
    assert(u8_result == 30);

    int32_t i32_a = -4, i32_b = 5, i32_result;
    type_mul(TYPE_I32, (uint8_t*)&i32_result, (uint8_t*)&i32_a, (uint8_t*)&i32_b);
    assert(i32_result == -20);

    // Test division
    uint16_t u16_a = 100, u16_b = 4, u16_result;
    type_div(TYPE_U16, (uint8_t*)&u16_result, (uint8_t*)&u16_a, (uint8_t*)&u16_b);
    assert(u16_result == 25);

    double f64_a = 10.0, f64_b = 4.0, f64_result;
    type_div(TYPE_F64, (uint8_t*)&f64_result, (uint8_t*)&f64_a, (uint8_t*)&f64_b);
    assert(f64_result == 2.5);

    // Test modulo
    uint32_t u32_mod_a = 17, u32_mod_b = 5, u32_mod_result;
    type_mod(TYPE_U32, (uint8_t*)&u32_mod_result, (uint8_t*)&u32_mod_a, (uint8_t*)&u32_mod_b);
    assert(u32_mod_result == 2);

    int8_t i8_a = -17, i8_b = 5, i8_result;
    type_mod(TYPE_I8, (uint8_t*)&i8_result, (uint8_t*)&i8_a, (uint8_t*)&i8_b);
    assert(i8_result == -2);
}

inline static void test_utility_ops() {
    // Test copy
    uint64_t u64_src = 0xDEADBEEFCAFEBABE, u64_dst = 0;
    type_copy(TYPE_U64, (uint8_t*)&u64_dst, (uint8_t*)&u64_src);
    assert(u64_dst == u64_src);

    float f32_src = 3.14159f, f32_dst = 0;
    type_copy(TYPE_F32, (uint8_t*)&f32_dst, (uint8_t*)&f32_src);
    assert(f32_dst == f32_src);

    char str_src[] = "test string";
    char str_dst[20];
    type_copy(TYPE_VARCHAR, (uint8_t*)str_dst, (uint8_t*)str_src);
    assert(strcmp(str_dst, str_src) == 0);

    // Test zero
    uint32_t u32_val = 0xFFFFFFFF;
    type_zero(TYPE_U32, (uint8_t*)&u32_val);
    assert(u32_val == 0);

    double f64_val = 123.456;
    type_zero(TYPE_F64, (uint8_t*)&f64_val);
    assert(f64_val == 0.0);

    char str_val[] = "not empty";
    type_zero(TYPE_VARCHAR, (uint8_t*)str_val);
    assert(str_val[0] == '\0');

    // Test hash
    uint32_t val1 = 42, val2 = 42, val3 = 43;
    assert(type_hash(TYPE_U32, (uint8_t*)&val1) == type_hash(TYPE_U32, (uint8_t*)&val2));
    assert(type_hash(TYPE_U32, (uint8_t*)&val1) != type_hash(TYPE_U32, (uint8_t*)&val3));

    char str1[] = "hello";
    char str2[] = "hello";
    char str3[] = "world";
    assert(type_hash(TYPE_VARCHAR, (uint8_t*)str1) == type_hash(TYPE_VARCHAR, (uint8_t*)str2));
    assert(type_hash(TYPE_VARCHAR, (uint8_t*)str1) != type_hash(TYPE_VARCHAR, (uint8_t*)str3));
}

inline static void test_typed_value() {
    // Test comparison operators
    uint32_t val1 = 10, val2 = 20, val3 = 10;
    TypedValue tv1 = {TYPE_U32, (uint8_t*)&val1};
    TypedValue tv2 = {TYPE_U32, (uint8_t*)&val2};
    TypedValue tv3 = {TYPE_U32, (uint8_t*)&val3};

    assert(tv1 < tv2);
    assert(tv2 > tv1);
    assert(tv1 <= tv2);
    assert(tv2 >= tv1);
    assert(tv1 <= tv3);
    assert(tv1 >= tv3);
    assert(tv1 == tv3);
    assert(tv1 != tv2);

    // Test get_size
    assert(tv1.get_size() == 4);


    TypedValue tv_null = {TYPE_NULL};
    assert(tv_null.get_size() == 0);

    TypedValue tv_u8 = {TYPE_U8, nullptr};
    assert(tv_u8.get_size() == 1);

    TypedValue tv_f64 = {TYPE_F64, nullptr};
    assert(tv_f64.get_size() == 8);

    // Test copy_to
    uint32_t src = 0xDEADBEEF, dst = 0;
    TypedValue tv_src = {TYPE_U32, (uint8_t*)&src};
    TypedValue tv_dst = {TYPE_U32, (uint8_t*)&dst};
    tv_src.copy_to(tv_dst);
    assert(dst == src);

    // Test with strings
    char str1[] = "alpha";
    char str2[] = "beta";
    char str3[] = "alpha";
    TypedValue tv_str1 = {TYPE_VARCHAR, (uint8_t*)str1};
    TypedValue tv_str2 = {TYPE_VARCHAR, (uint8_t*)str2};
    TypedValue tv_str3 = {TYPE_VARCHAR, (uint8_t*)str3};

    assert(tv_str1 < tv_str2);
    assert(tv_str1 == tv_str3);
    assert(tv_str1 != tv_str2);
}

inline int test_types() {
    printf("Running type system tests...\n");

    test_type_sizes();
    printf("✓ Type sizes\n");

    test_type_properties();
    printf("✓ Type properties\n");

    test_type_compare();
    printf("✓ Type comparison\n");

    test_type_equals();
    printf("✓ Type equality\n");

    test_arithmetic_ops();
    printf("✓ Arithmetic operations\n");

    test_utility_ops();
    printf("✓ Utility operations\n");

    test_typed_value();
    printf("✓ TypedValue struct\n");

    printf("\nAll tests passed!\n");
    return 0;
}
