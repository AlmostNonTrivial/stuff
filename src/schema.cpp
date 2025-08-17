#include "schema.hpp"
#include "defs.hpp"
#include <cstdint>

void print_record(uint8_t *record, TableSchema *schema) {
  for (int i = 0; i < schema->columns.size(); i++) {
    debug_type((uint8_t *)record + schema->column_offsets[i - 1],
               schema->columns[i].type);
  }
}
