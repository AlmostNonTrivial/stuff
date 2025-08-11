
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





    void interpret(const uint8_t* record, DataType size)
    {
        if(size == TYPE_INT32){
            uint32_t x;
            memcpy(&x, record, sizeof(uint32_t));  // Copy all 4 bytes
            std::cout << x << "\n";
        } else if (size == TYPE_INT64) {
            uint64_t x;
            memcpy(&x, record, sizeof(uint64_t));  // Copy all 8 bytes
            std::cout << x << "\n";
        } else {
            std::string str_key(reinterpret_cast<const char *>(record), size);
            std::cout << str_key << '\n';
        }
    }





int cursor_test()
{
    pager_init("file");
    pager_begin_transaction();

    BPlusTree tree = bt_create(TYPE_INT32, TYPE_INT32, BPLUS);
    bp_init(tree);

    BtCursor *cursor = bt_cursor_create(&tree, true);
   const uint32_t k = 21;
    for(uint32_t i = 0; i < tree.internal_max_keys + 1; i++) {
        if(i == tree.internal_max_keys) {
           print_tree(tree) ;
        }
        bp_insert_element(tree, &i, (const uint8_t*)&k);
    }

    print_tree(tree);

    bool exists = bt_cursor_seek(cursor, &k);

    do {
      auto record = bt_cursor_get_record(cursor) ;
      // interpret(record, (DataType)tree.record_size);
    } while(bt_cursor_next(cursor));



   bt_cursor_destroy(cursor) ;
   pager_close();
}





int main() {
  std::cout << "B+ Tree Test Suite" << std::endl;
  std::cout << "==================" << std::endl;

  try {

    // large_records();

    // run_comprehensive_tests();
    cursor_test();


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
