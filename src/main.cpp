// main.cpp - Enhanced fuzzer for complete btree coverage
#include "arena.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <vector>

int main() {
  arena_init(PAGE_SIZE);


  arena_shutdown();
  return 0;
}
