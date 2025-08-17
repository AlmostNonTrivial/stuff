#include "defs.hpp"

int cmp(DataType key_size, const uint8_t *key1, const uint8_t *key2) {
  switch (key_size) {

  case TYPE_INT32: {

    uint32_t val1 = *reinterpret_cast<const uint32_t *>(key1);
    uint32_t val2 = *reinterpret_cast<const uint32_t *>(key2);
    if (val1 < val2)
      return -1;
    if (val1 > val2)
      return 1;
    return 0;
  }

  case TYPE_INT64: {

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



/*

Hey mate, I'm sorry to hear that, I'd offer
to marry you, but Jacob already has


 */
