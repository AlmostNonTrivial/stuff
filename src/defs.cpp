#include "defs.hpp"
#include <cstdint>

void print_ptr(uint8_t *data, DataType type) {
  switch (type) {
  case TYPE_UINT32:
    std::cout << *reinterpret_cast<uint32_t *>(data) << ",";
    break;
  case TYPE_UINT64:
    std::cout << *reinterpret_cast<uint64_t *>(data) << ",";
    break;
  case TYPE_VARCHAR32:
  case TYPE_VARCHAR256:
    for (uint32_t i = 0; i < type; ++i) {
      printf("%c", data[i]);
    }
    printf("%c", ',');
    break;
  }
}

void debug_type(uint8_t *data, DataType type) {
  // Print raw bytes first
  printf("Raw bytes: ");
  for (size_t i = 0; i < type && i < 16; ++i) {
    printf("%02x ", data[i]);
  }
  printf("\n");

  switch (type) {
  case TYPE_UINT32: {
    uint32_t val;
    memcpy(&val, data, 4);
    printf("INT32: %u (0x%08x)\n", val, val);
    break;
  }
  case TYPE_UINT64: {
    uint64_t val;
    memcpy(&val, data, 8);
    printf("INT64: %lu (0x%016lx)\n", val, val);
    break;
  }
  case TYPE_VARCHAR32:
  case TYPE_VARCHAR256:
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

    int cmp(DataType key_size, const uint8_t *key1, const uint8_t *key2) {
      switch (key_size) {

      case TYPE_UINT32: {

        uint32_t val1 = *reinterpret_cast<const uint32_t *>(key1);
        uint32_t val2 = *reinterpret_cast<const uint32_t *>(key2);
        if (val1 < val2)
          return -1;
        if (val1 > val2)
          return 1;
        return 0;
      }

      case TYPE_UINT64: {

        uint64_t val1 = *reinterpret_cast<const uint64_t *>(key1);
        uint64_t val2 = *reinterpret_cast<const uint64_t *>(key2);
        if (val1 < val2)
          return -1;
        if (val1 > val2)
          return 1;
        return 0;
      }
      case TYPE_VARCHAR32:

      case TYPE_VARCHAR256: {

        return memcmp(key1, key2, (uint32_t)key_size);
      }
      default:

        return 0;
      }
    }
