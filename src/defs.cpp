#include "defs.hpp"
#include <cstdio>
#include <iostream>
#include <cstdint>


void debug_type(uint8_t *data, DataType type) {
  // Print raw bytes first
  printf("Raw bytes: ");
  for (size_t i = 0; i < type && i < 16; ++i) {
    printf("%02x ", data[i]);
  }
  printf("\n");

  switch (type) {
  case TYPE_4: {
    uint32_t val;
    memcpy(&val, data, 4);
    printf("INT32: %u (0x%08x)\n", val, val);
    break;
  }
  case TYPE_8: {
    uint64_t val;
    memcpy(&val, data, 8);
    printf("INT64: %lu (0x%016lx)\n", val, val);
    break;
  }
  case TYPE_32:
  case TYPE_256:
    printf("VARCHAR: \"");
    for (size_t i = 0; i < type; ++i) {
      if (data[i] == 0) break;
      if (data[i] >= 32 && data[i] < 127) {
        printf("%c", data[i]);
      } else {
        printf("\\x%02x", data[i]);
      }
    }
    printf("\"\n");
    break;
  }
}
// type_ops.cpp


// Global dispatch table
TypeOps type_ops[257];

// ============================================================================
// UINT32 Operations
// ============================================================================
static int cmp_u32(const uint8_t* a, const uint8_t* b) {
    uint32_t val1 = *(uint32_t*)a;
    uint32_t val2 = *(uint32_t*)b;
    return (val1 > val2) - (val1 < val2);
}

static void copy_u32(uint8_t* dst, const uint8_t* src) {
    *(uint32_t*)dst = *(uint32_t*)src;
}

static uint64_t to_u64_u32(const uint8_t* src) {
    return (uint64_t)(*(uint32_t*)src);
}

static void from_u64_u32(uint8_t* dst, uint64_t val) {
    *(uint32_t*)dst = (uint32_t)val;
}

static void print_u32(const uint8_t* data) {
    std::cout << *(uint32_t*)data;
}

static size_t size_u32() { return 4; }

static bool add_u32(uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    *(uint32_t*)dst = *(uint32_t*)a + *(uint32_t*)b;
    return true;
}

static bool sub_u32(uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    *(uint32_t*)dst = *(uint32_t*)a - *(uint32_t*)b;
    return true;
}

static bool mul_u32(uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    *(uint32_t*)dst = *(uint32_t*)a * *(uint32_t*)b;
    return true;
}

static bool div_u32(uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    uint32_t divisor = *(uint32_t*)b;
    if (divisor == 0) return false;
    *(uint32_t*)dst = *(uint32_t*)a / divisor;
    return true;
}

static bool mod_u32(uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    uint32_t divisor = *(uint32_t*)b;
    if (divisor == 0) return false;
    *(uint32_t*)dst = *(uint32_t*)a % divisor;
    return true;
}

// ============================================================================
// UINT64 Operations
// ============================================================================
static int cmp_u64(const uint8_t* a, const uint8_t* b) {
    uint64_t val1 = *(uint64_t*)a;
    uint64_t val2 = *(uint64_t*)b;
    return (val1 > val2) - (val1 < val2);
}

static void copy_u64(uint8_t* dst, const uint8_t* src) {
    *(uint64_t*)dst = *(uint64_t*)src;
}

static uint64_t to_u64_u64(const uint8_t* src) {
    return *(uint64_t*)src;
}

static void from_u64_u64(uint8_t* dst, uint64_t val) {
    *(uint64_t*)dst = val;
}

static void print_u64(const uint8_t* data) {
    std::cout << *(uint64_t*)data;
}

static size_t size_u64() { return 8; }

static bool add_u64(uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    *(uint64_t*)dst = *(uint64_t*)a + *(uint64_t*)b;
    return true;
}

static bool sub_u64(uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    *(uint64_t*)dst = *(uint64_t*)a - *(uint64_t*)b;
    return true;
}

static bool mul_u64(uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    *(uint64_t*)dst = *(uint64_t*)a * *(uint64_t*)b;
    return true;
}

static bool div_u64(uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    uint64_t divisor = *(uint64_t*)b;
    if (divisor == 0) return false;
    *(uint64_t*)dst = *(uint64_t*)a / divisor;
    return true;
}

static bool mod_u64(uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    uint64_t divisor = *(uint64_t*)b;
    if (divisor == 0) return false;
    *(uint64_t*)dst = *(uint64_t*)a % divisor;
    return true;
}

// ============================================================================
// VARCHAR32 Operations
// ============================================================================
static int cmp_varchar32(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, 32);
}

static void copy_varchar32(uint8_t* dst, const uint8_t* src) {
    memcpy(dst, src, 32);
}

static uint64_t to_u64_varchar(const uint8_t* src) {
    // Try to parse as number, return 0 if not numeric
    char buf[33];
    memcpy(buf, src, 32);
    buf[32] = '\0';
    return strtoull(buf, nullptr, 10);
}

static void from_u64_varchar32(uint8_t* dst, uint64_t val) {
    char buf[33];
    snprintf(buf, 33, "%llu", val);
    memset(dst, 0, 32);
    memcpy(dst, buf, strlen(buf));
}

static void print_varchar32(const uint8_t* data) {
    for (uint32_t i = 0; i < 32 && data[i]; ++i) {
        printf("%c", data[i]);
    }
}

static size_t size_varchar32() { return 32; }

// No arithmetic for strings
static bool arith_fail(uint8_t*, const uint8_t*, const uint8_t*) {
    return false;
}

// ============================================================================
// VARCHAR256 Operations
// ============================================================================
static int cmp_varchar256(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, 256);
}

static void copy_varchar256(uint8_t* dst, const uint8_t* src) {
    memcpy(dst, src, 256);
}

static void from_u64_varchar256(uint8_t* dst, uint64_t val) {
    char buf[257];
    snprintf(buf, 257, "%llu", val);
    memset(dst, 0, 256);
    memcpy(dst, buf, strlen(buf));
}

static void print_varchar256(const uint8_t* data) {
    for (uint32_t i = 0; i < 256 && data[i]; ++i) {
        printf("%c", data[i]);
    }
}

static size_t size_varchar256() { return 256; }

// ============================================================================
// NULL Operations
// ============================================================================
static int cmp_null(const uint8_t*, const uint8_t*) { return 0; }
static void copy_null(uint8_t*, const uint8_t*) {}
static uint64_t to_u64_null(const uint8_t*) { return 0; }
static void from_u64_null(uint8_t*, uint64_t) {}
static void print_null(const uint8_t*) { printf("NULL"); }
static size_t size_null() { return 0; }

// ============================================================================
// Initialize dispatch tables
// ============================================================================
void init_type_ops() {
    // NULL type
    type_ops[TYPE_NULL] = {
        cmp_null, copy_null, to_u64_null, from_u64_null, print_null, size_null,
        arith_fail, arith_fail, arith_fail, arith_fail, arith_fail
    };

    // UINT32
    type_ops[TYPE_4] = {
        cmp_u32, copy_u32, to_u64_u32, from_u64_u32, print_u32, size_u32,
        add_u32, sub_u32, mul_u32, div_u32, mod_u32
    };

    // UINT64
    type_ops[TYPE_8] = {
        cmp_u64, copy_u64, to_u64_u64, from_u64_u64, print_u64, size_u64,
        add_u64, sub_u64, mul_u64, div_u64, mod_u64
    };

    // VARCHAR32
    type_ops[TYPE_32] = {
        cmp_varchar32, copy_varchar32, to_u64_varchar, from_u64_varchar32,
        print_varchar32, size_varchar32,
        arith_fail, arith_fail, arith_fail, arith_fail, arith_fail
    };

    // VARCHAR256
    type_ops[TYPE_256] = {
        cmp_varchar256, copy_varchar256, to_u64_varchar, from_u64_varchar256,
        print_varchar256, size_varchar256,
        arith_fail, arith_fail, arith_fail, arith_fail, arith_fail
    };
}

// ============================================================================
// Arithmetic dispatch helper
// ============================================================================
bool do_arithmetic(ArithOp op, DataType type, uint8_t* dst,
                   const uint8_t* a, const uint8_t* b) {
    switch (op) {
        case ARITH_ADD: return type_ops[type].add(dst, a, b);
        case ARITH_SUB: return type_ops[type].sub(dst, a, b);
        case ARITH_MUL: return type_ops[type].mul(dst, a, b);
        case ARITH_DIV: return type_ops[type].div(dst, a, b);
        case ARITH_MOD: return type_ops[type].mod(dst, a, b);
    }
    return false;
}
