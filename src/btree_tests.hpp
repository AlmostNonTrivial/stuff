#include "btree.hpp"
#include <iostream>

bool bp_validate_tree(BPlusTree& tree) {
    const uint32_t USABLE_SPACE = PAGE_SIZE - NODE_HEADER_SIZE;
    const uint32_t MIN_KEYS = 3;


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
                  << ") must be < leaf_max_keys (" << tree.leaf_max_keys << ")" << std::endl;
        return false;
    }

    if (tree.internal_min_keys >= tree.internal_max_keys) {
        std::cout << "FAIL: internal_min_keys (" << tree.internal_min_keys
                  << ") must be < internal_max_keys (" << tree.internal_max_keys << ")" << std::endl;
        return false;
    }

    // INVARIANT: Max keys must support minimum branching factor
    // B-trees need at least 3 keys to have meaningful splits and merges
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

    // INVARIANT: Split indices must allow meaningful splits
    // Index 0 would create empty left node, index >= max would create empty right node
    if (tree.leaf_split_index == 0 || tree.leaf_split_index >= tree.leaf_max_keys) {
        std::cout << "FAIL: leaf_split_index (" << tree.leaf_split_index
                  << ") must be > 0 and < leaf_max_keys (" << tree.leaf_max_keys << ")" << std::endl;
        return false;
    }

    if (tree.internal_split_index == 0 || tree.internal_split_index >= tree.internal_max_keys) {
        std::cout << "FAIL: internal_split_index (" << tree.internal_split_index
                  << ") must be > 0 and < internal_max_keys (" << tree.internal_max_keys << ")" << std::endl;
        return false;
    }

    if (tree.tree_type == BTREE) {
        // B-TREE: Both nodes store records
        uint32_t entry_size = tree.node_key_size + tree.record_size;
        uint32_t expected_max = std::max(MIN_KEYS, USABLE_SPACE / entry_size);
        uint32_t expected_min = expected_max / 2;

        // INVARIANT: B-TREE symmetry - both node types store records
        // In B-trees, internal nodes also store data records, so leaf and internal
        // nodes have identical storage requirements and thus identical capacities
        if (tree.leaf_max_keys != tree.internal_max_keys) {
            std::cout << "FAIL: B-TREE leaf_max_keys (" << tree.leaf_max_keys
                      << ") must equal internal_max_keys (" << tree.internal_max_keys << ")" << std::endl;
            return false;
        }

        if (tree.leaf_min_keys != tree.internal_min_keys) {
            std::cout << "FAIL: B-TREE leaf_min_keys (" << tree.leaf_min_keys
                      << ") must equal internal_min_keys (" << tree.internal_min_keys << ")" << std::endl;
            return false;
        }

        // INVARIANT: Capacity calculation must match space allocation formula
        // entry_size = key + record, max_keys = floor(usable_space / entry_size)
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

        // INVARIANT: Physical space constraint - entries must fit in page
        // Total space used by max entries cannot exceed available page space
        uint32_t space_used = tree.leaf_max_keys * entry_size;
        if (space_used > USABLE_SPACE) {
            std::cout << "FAIL: B-TREE max entries use " << space_used
                      << " bytes, exceeds usable space " << USABLE_SPACE << std::endl;
            return false;
        }

    } else if (tree.tree_type == BPLUS) {
        // B+TREE: Different capacities for leaf vs internal

        // Leaf nodes: [keys][records]
        uint32_t leaf_entry_size = tree.node_key_size + tree.record_size;
        uint32_t expected_leaf_max = std::max(MIN_KEYS, USABLE_SPACE / leaf_entry_size);
        uint32_t expected_leaf_min = expected_leaf_max / 2;

        // INVARIANT: Leaf capacity calculation for B+trees
        // Leaves store [keys][records], so capacity = floor(space / (key_size + record_size))
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

        // Internal nodes: [keys][children] (no records)
        uint32_t expected_internal_max = std::max(MIN_KEYS,
            (USABLE_SPACE - tree.node_key_size) / (2 * tree.node_key_size));
        uint32_t expected_internal_min = expected_internal_max / 2;

        // INVARIANT: Internal capacity calculation for B+trees
        // Internals store [keys][child_ptrs], for n keys need n+1 pointers
        // So: n*key_size + (n+1)*ptr_size <= space, solve for n
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

        // INVARIANT: Physical space constraints for both node types
        // Each node type's max entries must fit within page boundaries
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

        // DIAGNOSTIC: Capacity difference expectation for B+trees
        // B+trees should typically have different leaf vs internal capacities
        // when records are larger than keys (which is the common case)
        if (tree.leaf_max_keys == tree.internal_max_keys && tree.record_size > tree.node_key_size) {
            std::cout << "WARN: B+TREE leaf and internal capacities are equal ("
                      << tree.leaf_max_keys << "), unusual for record_size > key_size" << std::endl;
        }

    } else {
        std::cout << "FAIL: tree_type is " << tree.tree_type << ", expected BTREE or BPLUS" << std::endl;
        return false;
    }

    std::cout << "PASS: All tree properties valid" << std::endl;
    return true;
}
