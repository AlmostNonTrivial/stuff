
#pragma once
#include "btree.hpp"
#include <cstdint>
#include <iostream>
// debug
uint64_t debug_hash_tree(BPlusTree &tree);
void print_tree(BPlusTree &tree);

bool bp_validate_all_invariants(BPlusTree &tree);
