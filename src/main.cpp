
// btree_test.cpp
#include "btree.hpp"
#include "btree_tests.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

int main() {
  std::cout << "B+ Tree Test Suite" << std::endl;
  std::cout << "==================" << std::endl;

  try {

    // large_records();

    run_comprehensive_tests();

    // test_composite_records();

    // test_rollback_functionality();
    // test_capacity_and_splits();
    // test_sequential_operations();
    // test_update_operations();
    // test_data_types();
    // test_boundary_conditions();
    // test_random_operations();
    // test_persistence();

    std::cout << "\n=== Test Suite Completed ===" << std::endl;
    std::cout << "All tests finished. Check individual results above."
              << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
