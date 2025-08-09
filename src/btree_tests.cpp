#include "btree_tests.hpp"
#include "btree.hpp"
#include <cstring>
#include <iostream>

bool bp_validate_tree(BPlusTree &tree) {
  const uint32_t USABLE_SPACE = PAGE_SIZE - NODE_HEADER_SIZE;
  const uint32_t MIN_KEYS = 3;
  bool result = true;

  if (tree.node_key_size == 0) {
    std::cout << "FAIL: node_key_size is 0, must be > 0" << std::endl;
    return false;
  }

  if (tree.record_size == 0) {
    std::cout << "FAIL: record_size is 0, must be > 0" << std::endl;
    return false;
  }

  if (tree.leaf_min_keys >= tree.leaf_max_keys) {
    std::cout << "FAIL: leaf_min_keys (" << tree.leaf_min_keys
              << ") must be < leaf_max_keys (" << tree.leaf_max_keys << ")"
              << std::endl;
    return false;
  }

  if (tree.internal_min_keys >= tree.internal_max_keys) {
    std::cout << "FAIL: internal_min_keys (" << tree.internal_min_keys
              << ") must be < internal_max_keys (" << tree.internal_max_keys
              << ")" << std::endl;
    return false;
  }

  if (tree.leaf_max_keys < MIN_KEYS) {
    std::cout << "FAIL: leaf_max_keys (" << tree.leaf_max_keys
              << ") must be >= " << MIN_KEYS << std::endl;
    return false;
  }

  if (tree.internal_max_keys < MIN_KEYS) {
    std::cout << "FAIL: internal_max_keys (" << tree.internal_max_keys
              << ") must be >= " << MIN_KEYS << std::endl;
    return false;
  }

  if (tree.leaf_split_index == 0 ||
      tree.leaf_split_index >= tree.leaf_max_keys) {
    std::cout << "FAIL: leaf_split_index (" << tree.leaf_split_index
              << ") must be > 0 and < leaf_max_keys (" << tree.leaf_max_keys
              << ")" << std::endl;
    return false;
  }

  if (tree.internal_split_index == 0 ||
      tree.internal_split_index >= tree.internal_max_keys) {
    std::cout << "FAIL: internal_split_index (" << tree.internal_split_index
              << ") must be > 0 and < internal_max_keys ("
              << tree.internal_max_keys << ")" << std::endl;
    return false;
  }

  if (tree.tree_type == BTREE) {
    uint32_t entry_size = tree.node_key_size + tree.record_size;
    uint32_t expected_max = std::max(MIN_KEYS, USABLE_SPACE / entry_size);
    uint32_t expected_min = expected_max / 2;

    if (tree.leaf_max_keys != tree.internal_max_keys) {
      std::cout << "FAIL: B-TREE leaf_max_keys (" << tree.leaf_max_keys
                << ") must equal internal_max_keys (" << tree.internal_max_keys
                << ")" << std::endl;
      return false;
    }

    if (tree.leaf_min_keys != tree.internal_min_keys) {
      std::cout << "FAIL: B-TREE leaf_min_keys (" << tree.leaf_min_keys
                << ") must equal internal_min_keys (" << tree.internal_min_keys
                << ")" << std::endl;
      return false;
    }

    if (tree.leaf_max_keys != expected_max) {
      std::cout << "FAIL: leaf_max_keys is " << tree.leaf_max_keys
                << ", expected " << expected_max << std::endl;
      return false;
    }

    if (tree.leaf_min_keys != expected_min) {
      std::cout << "FAIL: leaf_min_keys is " << tree.leaf_min_keys
                << ", expected " << expected_min << std::endl;
      return false;
    }

    uint32_t space_used = tree.leaf_max_keys * entry_size;
    if (space_used > USABLE_SPACE) {
      std::cout << "FAIL: B-TREE max entries use " << space_used
                << " bytes, exceeds usable space " << USABLE_SPACE << std::endl;
      return false;
    }
  } else if (tree.tree_type == BPLUS) {
    uint32_t leaf_entry_size = tree.node_key_size + tree.record_size;
    uint32_t expected_leaf_max =
        std::max(MIN_KEYS, USABLE_SPACE / leaf_entry_size);
    uint32_t expected_leaf_min = expected_leaf_max / 2;

    if (tree.leaf_max_keys != expected_leaf_max) {
      std::cout << "FAIL: leaf_max_keys is " << tree.leaf_max_keys
                << ", expected " << expected_leaf_max << std::endl;
      return false;
    }

    if (tree.leaf_min_keys != expected_leaf_min) {
      std::cout << "FAIL: leaf_min_keys is " << tree.leaf_min_keys
                << ", expected " << expected_leaf_min << std::endl;
      return false;
    }

    uint32_t expected_internal_max =
        std::max(MIN_KEYS, (USABLE_SPACE - tree.node_key_size) /
                               (2 * tree.node_key_size));
    uint32_t expected_internal_min = expected_internal_max / 2;

    if (tree.internal_max_keys != expected_internal_max) {
      std::cout << "FAIL: internal_max_keys is " << tree.internal_max_keys
                << ", expected " << expected_internal_max << std::endl;
      return false;
    }

    if (tree.internal_min_keys != expected_internal_min) {
      std::cout << "FAIL: internal_min_keys is " << tree.internal_min_keys
                << ", expected " << expected_internal_min << std::endl;
      return false;
    }

    uint32_t leaf_space = tree.leaf_max_keys * leaf_entry_size;
    if (leaf_space > USABLE_SPACE) {
      std::cout << "FAIL: B+TREE leaf max entries use " << leaf_space
                << " bytes, exceeds usable space " << USABLE_SPACE << std::endl;
      return false;
    }

    uint32_t internal_space = tree.internal_max_keys * tree.node_key_size +
                              (tree.internal_max_keys + 1) * tree.node_key_size;
    if (internal_space > USABLE_SPACE) {
      std::cout << "FAIL: B+TREE internal max entries use " << internal_space
                << " bytes, exceeds usable space " << USABLE_SPACE << std::endl;
      return false;
    }

    if (tree.leaf_max_keys == tree.internal_max_keys &&
        tree.record_size > tree.node_key_size) {
      std::cout << "WARN: B+TREE leaf and internal capacities are equal ("
                << tree.leaf_max_keys << "), unusual for record_size > key_size"
                << std::endl;
    }
  } else {
    std::cout << "FAIL: tree_type is " << tree.tree_type
              << ", expected BTREE or BPLUS" << std::endl;
    return false;
  }

  std::cout << "PASS: All tree properties valid" << std::endl;
  return true;
}

/*
 * Support the btree,
 * Support different keys
 *
 */
#include <set>
bool test_single_leaf_operations() {
  std::vector<ColumnInfo> schema = {{TYPE_INT32}};
  for (auto type : {BTREE, BPLUS}) {

    BPlusTree tree = bp_create(TYPE_INT32, schema, type);
    bp_init(tree);

    // Check for valid tree creation
    if (tree.tree_type == INVALID) {
      std::cerr << "B+TREE Test failed: Invalid tree creation" << std::endl;
      return false;
    }

    int duplicate_test_count = 5;
    int in_addition = type == BPLUS ? 0 : duplicate_test_count;
    int insert_count = type == BTREE ? (tree.leaf_max_keys - duplicate_test_count) : tree.leaf_max_keys;


    // Create random sequence of keys
    std::vector<uint32_t> keys;
    for (uint32_t i = 0; i < insert_count; i++) {
      keys.push_back(i);
    }

    // Use simple shuffle (assumes srand() called elsewhere)
    for (uint32_t i = keys.size() - 1; i > 0; i--) {
      uint32_t j = rand() % (i + 1);
      std::swap(keys[i], keys[j]);
    }

    std::set<uint32_t> inserted_keys;

    // Insert keys randomly
    for (uint32_t key : keys) {
      uint8_t record[4];
      memcpy(record, &key, sizeof(key));

      bp_insert_element(tree, key, record);
      inserted_keys.insert(key);

      BPTreeNode *root = bp_get_root(tree);
      if (type == BTREE) {
        validate_btree_node(tree, root);
      } else {
        validate_bplus_leaf_node(tree, root);
      }

      if (!root || !root->is_leaf || root->num_keys != inserted_keys.size()) {
        std::cerr << "B+TREE Test failed: Invalid root state after insert key "
                  << key << std::endl;
        return false;
      }

      if (!bp_find_element(tree, key)) {
        std::cerr << "B+TREE Test failed: Key " << key
                  << " not found after insert" << std::endl;
        return false;
      }
    }

    // Test duplicate inserts don't add additional keys
    uint32_t initial_count = inserted_keys.size();
    for (int i = 0; i < duplicate_test_count; i++) {
      uint32_t dup_key = keys[i % keys.size()];
      uint8_t record[4];
      memcpy(record, &dup_key, sizeof(dup_key));

      bp_insert_element(tree, dup_key, record);

      BPTreeNode *root = bp_get_root(tree);

      if (type == BTREE) {
        validate_btree_node(tree, root);
      } else {
        validate_bplus_leaf_node(tree, root);
      }

      int plus = type == BPLUS ? 0 : i + 1;
      if (root->num_keys != initial_count  + plus) {
        std::cerr << "B+TREE Test failed: Duplicate insert of key " << dup_key
                  << " changed key count" << std::endl;
        return false;
      }
    }

    // Delete some keys
    std::vector<uint32_t> to_delete;
    for (uint32_t key : keys) {
      if (key >= 3 && key % 3 == 0) {
        to_delete.push_back(key);
      }
    }

    for (uint32_t delete_key : to_delete) {
      bp_delete_element(tree, delete_key);
      inserted_keys.erase(delete_key);

      if (bp_find_element(tree, delete_key) && type == BPLUS) {


        std::cerr << "B+TREE Test failed: Deleted key " << delete_key
                  << " still found" << std::endl;
        return false;
      }
      BPTreeNode *root = bp_get_root(tree);

      if (type == BTREE) {
        validate_btree_node(tree, root);
      } else {
        validate_bplus_leaf_node(tree, root);

      if (!root || root->num_keys != inserted_keys.size()) {
        std::cerr << "B+TREE Test failed: Invalid root state after delete key "
                  << delete_key << std::endl;
        return false;
      }
    }}

    // Fill back to maximum capacity
    uint32_t next_key = tree.leaf_max_keys;
    while (inserted_keys.size() < tree.leaf_max_keys) {
      uint8_t record[4];
      memcpy(record, &next_key, sizeof(next_key));

      bp_insert_element(tree, next_key, record);
      inserted_keys.insert(next_key);
      next_key++;
    }



    // Extract all leaf data using the tree's own functions
    auto leaf_data = bp_print_leaves(tree);

    if (leaf_data.size() != tree.leaf_max_keys + in_addition) {
      std::cerr << "B+TREE Test failed: Expected " << tree.leaf_max_keys +in_addition
                << " keys, found " << leaf_data.size() << std::endl;
      return false;
    }

    // Verify ordering and key-value correspondence
    for (size_t i = 1; i < leaf_data.size(); i++) {
      if (leaf_data[i - 1].first > leaf_data[i].first) {
        std::cerr << "B+TREE Test failed: Keys not in order at positions "
                  << (i - 1) << " and " << i << " (values "
                  << leaf_data[i - 1].first << ", " << leaf_data[i].first << ")"
                  << std::endl;
        return false;
      }
    }

    for (const auto &entry : leaf_data) {
      uint32_t stored_value = *reinterpret_cast<const uint32_t *>(entry.second);
      if (stored_value != entry.first) {
        std::cerr << "B+TREE Test failed: Value " << stored_value
                  << " does not match key " << entry.first << std::endl;
        return false;
      }
    }
    std::cout << "PASS 1\n";

  }
  return true;
}
