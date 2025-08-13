#include "btree_debug.hpp"
/*------------- PRINTING -------------- */
// Similar small modifications needed for:
// - bp_steal_from_right (mirror of above)
// - bp_merge_right (needs to handle internal records for B-tree)

// B-tree modifications for steal_from_right and merge operations

void print_uint8_as_chars(const uint8_t *data, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    printf("%c", data[i]);
  }
  printf("\n");
}

void bp_print_node(BPlusTree &tree, BPTreeNode *node) {
  if (!node) {
    std::cout << "NULL node" << std::endl;
    return;
  }

  // Print basic node information
  std::cout << "=== NODE " << node->index << " ===" << std::endl;

  std::cout << "Node Type: ";
  if (node->is_leaf) {
    std::cout << "LEAF";
  } else {
    std::cout << "INTERNAL";
  }
  std::cout << std::endl;

  // Print node metadata
  std::cout << "Page Index: " << node->index << std::endl;
  std::cout << "Parent: "
            << (node->parent == 0 ? "ROOT" : std::to_string(node->parent))
            << std::endl;
  std::cout << "Keys: " << node->num_keys;

  // Print capacity info based on node type
  if (node->is_leaf) {
    std::cout << "/" << tree.leaf_max_keys << " (min: " << tree.leaf_min_keys
              << ")";
  } else {
    std::cout << "/" << tree.internal_max_keys
              << " (min: " << tree.internal_min_keys << ")";
  }
  std::cout << std::endl;

  // Print sibling links for leaf nodes
  if (node->is_leaf) {
    std::cout << "Previous: "
              << (node->previous == 0 ? "NULL" : std::to_string(node->previous))
              << std::endl;
    std::cout << "Next: "
              << (node->next == 0 ? "NULL" : std::to_string(node->next))
              << std::endl;
  }

  std::cout << "Record Size: " << tree.record_size << " bytes" << std::endl;

  // Print keys (assuming they're uint32_t for display purposes)
  std::cout << "Keys: [";
  for (uint32_t i = 0; i < node->num_keys; i++) {
    if (tree.node_key_size == TYPE_INT32) {
      std::cout << *reinterpret_cast<const uint32_t *>(
                       get_key_at(tree, node, i))
                << ",";
    } else if (tree.node_key_size == TYPE_INT64) {
      std::cout << *reinterpret_cast<const uint64_t *>(
                       get_key_at(tree, node, i))
                << ",";
    } else {
      print_uint8_as_chars(get_key_at(tree, node, i), tree.node_key_size);
    }
  }

  std::cout << "]" << std::endl;

  // Print children for internal nodes
  if (!node->is_leaf) {
    uint32_t *children = get_children(tree, node);
    std::cout << "Children: [";
    for (uint32_t i = 0; i <= node->num_keys; i++) {
      if (i > 0)
        std::cout << ", ";
      std::cout << children[i];
    }
    std::cout << "]" << std::endl;
  }

  // Show memory layout information
  std::cout << "Memory Layout:" << std::endl;
  if (node->is_leaf) {
    uint32_t keys_size = tree.leaf_max_keys * tree.node_key_size;
    uint32_t records_size = tree.leaf_max_keys * tree.record_size;
    std::cout << "  Keys area: " << keys_size
              << " bytes (used: " << (node->num_keys * tree.node_key_size)
              << ")" << std::endl;
    std::cout << "  Records area: " << records_size
              << " bytes (used: " << (node->num_keys * tree.record_size) << ")"
              << std::endl;
    std::cout << "  Total data: " << (keys_size + records_size) << " / "
              << (PAGE_SIZE - NODE_HEADER_SIZE) << " bytes" << std::endl;
  } else {
    uint32_t keys_size = tree.internal_max_keys * tree.node_key_size;
    uint32_t children_size = (tree.internal_max_keys + 1) * tree.node_key_size;
    std::cout << "  Keys area: " << keys_size
              << " bytes (used: " << (node->num_keys * tree.node_key_size)
              << ")" << std::endl;
    std::cout << "  Children area: " << children_size
              << " bytes (used: " << ((node->num_keys + 1) * tree.node_key_size)
              << ")" << std::endl;
    std::cout << "  Total data: " << (keys_size + children_size) << " / "
              << (PAGE_SIZE - NODE_HEADER_SIZE) << " bytes" << std::endl;
  }

  std::cout << "=====================" << std::endl;
}

#include <queue>
#include <set>

void print_tree(BPlusTree &tree) {
  BPTreeNode *root = bp_get_root(tree);
  if (!root) {
    std::cout << "Tree is empty" << std::endl;
    return;
  }

  std::queue<BPTreeNode *> to_visit;
  std::set<BPTreeNode *> visited;
  to_visit.push(root);

  while (!to_visit.empty()) {
    // Get the number of nodes at the current level
    size_t level_size = to_visit.size();

    // Print all nodes at the current level
    for (size_t i = 0; i < level_size; i++) {
      BPTreeNode *node = to_visit.front();
      to_visit.pop();
      if (visited.find(node) != visited.end()) {

        std::cout << "CYCLE Detected\n";
        exit(0);
      }

      visited.insert(node);

      // Print the current node using the existing bp_print_node function
      bp_print_node(tree, node);

      // Add children to the queue if the node is not a leaf
      if (!node->is_leaf) {
        uint32_t *children = get_children(tree, node);
        for (uint32_t j = 0; j <= node->num_keys; j++) {
          if (children[j] != 0) {
            BPTreeNode *child = bp_get_child(tree, node, j);
            if (child) {
              to_visit.push(child);
            }
          }
        }
      }
    }
    // Print a separator between levels
    std::cout << "\n=== END OF LEVEL ===\n" << std::endl;
  }
}

/*------------- HASH -------------- */

uint64_t debug_hash_tree(BPlusTree &tree) {
  uint64_t hash = 0xcbf29ce484222325ULL;
  const uint64_t prime = 0x100000001b3ULL;

  // Hash tree metadata
  hash ^= tree.root_page_index;
  hash *= prime;
  hash ^= tree.internal_max_keys;
  hash *= prime;
  hash ^= tree.leaf_max_keys;
  hash *= prime;
  hash ^= tree.record_size;
  hash *= prime;

  // Recursive node hashing function
  std::function<void(BPTreeNode *, int)> hash_node = [&](BPTreeNode *node,
                                                         int depth) {
    if (!node)
      return;

    // Hash node metadata
    hash ^= node->index;
    hash *= prime;
    hash ^= node->parent;
    hash *= prime;
    hash ^= node->next;
    hash *= prime;
    hash ^= node->previous;
    hash *= prime;
    hash ^= node->num_keys;
    hash *= prime;
    hash ^= (node->is_leaf ? 1 : 0) | (depth << 1);
    hash *= prime;

    // Hash keys
    for (uint32_t i = 0; i < node->num_keys; i++) {
      uint8_t *key = get_key_at(tree, node, i);
      for (uint32_t j = 0; j < tree.node_key_size; j++) {
        hash ^= key[j];
        hash *= prime;
      }
    }

    if (node->is_leaf) {
      // Hash leaf node records
      uint8_t *record_data = get_leaf_record_data(tree, node);
      for (uint32_t i = 0; i < node->num_keys; i++) {
        const uint8_t *record = record_data + i * tree.record_size;
        uint32_t bytes_to_hash = std::min(8U, tree.record_size);
        for (uint32_t j = 0; j < bytes_to_hash; j++) {
          hash ^= record[j];
          hash *= prime;
        }
      }
    } else {
      // Recursively hash children
      uint32_t *children = get_children(tree, node);
      for (uint32_t i = 0; i <= node->num_keys; i++) {
        if (children[i] != 0) {
          BPTreeNode *child = bp_get_child(tree, node, i);
          if (child) {
            hash_node(child, depth + 1);
          }
        }
      }
    }
  };

  // Hash the tree starting from root
  if (tree.root_page_index != 0) {
    BPTreeNode *root = bp_get_root(tree);
    if (root) {
      hash_node(root, 0);
    }
  }

  return hash;
}

/* DBGINV */

#include <cassert>
#include <random>
#include <set>

static bool validate_key_separation(BPlusTree &tree, BPTreeNode *node) {
  if (!node || node->is_leaf)
    return true;

  for (uint32_t i = 0; i <= node->num_keys; i++) {
    BPTreeNode *child = bp_get_child(tree, node, i);
    if (!child)
      continue;

    // For child[i], check constraints based on separators
    // child[0] has keys < keys[0]
    // child[1] has keys >= keys[0] and < keys[1]
    // child[2] has keys >= keys[1] and < keys[2]
    // etc.

    // Check upper bound: child[i] should have keys < keys[i] (except for
    // rightmost child)
    if (i < node->num_keys) {
      uint8_t *upper_separator = get_key_at(tree, node, i);
      for (uint32_t j = 0; j < child->num_keys; j++) {
        if (tree.tree_type == BTREE) {
          if (cmp(tree, get_key_at(tree, child, j), upper_separator) > 0) {
            std::cerr << "INVARIANT VIOLATION: Key in child " << child->index
                      << " violates upper bound from parent " << node->index
                      << std::endl;
            return false;
          }
        } else {
          if (cmp(tree, get_key_at(tree, child, j), upper_separator) >= 0) {
            std::cerr << "INVARIANT VIOLATION: Key in child " << child->index
                      << " violates upper bound from parent " << node->index
                      << std::endl;
            return false;
          }
        }
      }
    }

    // Check lower bound: child[i] should have keys >= keys[i-1] (except for
    // leftmost child)
    if (i > 0) {
      uint8_t *lower_separator = get_key_at(tree, node, i - 1);
      for (uint32_t j = 0; j < child->num_keys; j++) {
        if (cmp(tree, get_key_at(tree, child, j), lower_separator) < 0) {
          std::cerr << "INVARIANT VIOLATION: Key in child " << child->index
                    << " violates lower bound from parent " << node->index
                    << std::endl;
          return false;
        }
      }
    }

    // Recursively check children
    return validate_key_separation(tree, child);
  }
  return true;
}

static bool validate_leaf_links(BPlusTree &tree) {
  BPTreeNode *current = bp_left_most(tree);
  BPTreeNode *prev = nullptr;

  while (current) {
    if (!current->is_leaf) {
      std::cerr << "INVARIANT VIOLATION: Non-leaf node " << current->index
                << " found in leaf traversal" << std::endl;
      return false;
    }

    // Check backward link
    if (prev && current->previous != (prev ? prev->index : 0)) {
      std::cerr << "INVARIANT VIOLATION: Leaf " << current->index
                << " has previous=" << current->previous << " but should be "
                << (prev ? prev->index : 0) << std::endl;
      return false;
    }

    // Check forward link consistency
    if (prev && prev->next != current->index) {
      std::cerr << "INVARIANT VIOLATION: Leaf " << prev->index
                << " has next=" << prev->next << " but should be "
                << current->index << std::endl;
      return false;
    }

    prev = current;
    current = bp_get_next(current);
  }
  return true;
}

static bool validate_tree_height(BPlusTree &tree, BPTreeNode *node,
                                 int expected_height, int current_height = 0) {
  if (!node)
    return true;

  if (node->is_leaf) {

    if (current_height != expected_height) {
      std::cerr << "INVARIANT VIOLATION: Leaf " << node->index << " at height "
                << current_height << " but expected height " << expected_height
                << std::endl;
      return false;
    }
  } else {
    for (uint32_t i = 0; i <= node->num_keys; i++) {
      BPTreeNode *child = bp_get_child(tree, node, i);
      if (child) {
        if (!validate_tree_height(tree, child, expected_height,
                                  current_height + 1)) {
          return false;
        }
      }
    }
  }
  return true;
}

static bool validate_bplus_leaf_node(BPlusTree &tree, BPTreeNode *node) {
  if (!node) {
    std::cout << "// Node pointer is null" << std::endl;
    return false;
    ;
  }

  // Must be marked as leaf
  if (!node->is_leaf) {
    std::cout << "// Node is not marked as leaf (is_leaf = 0)" << std::endl;
    return false;
    ;
  }

  // Key count must be within bounds
  uint32_t min_keys = (node->parent == 0)
                          ? 0
                          : tree.leaf_min_keys; // Root can have as few as 1 key
  if (node->num_keys < min_keys) {
    bp_print_node(tree, node);
    bp_print_node(tree, bp_get_parent(node));
    std::cout << node << "// Leaf node has too few keys: " << node->num_keys
              << " < " << min_keys << std::endl;
    return false;
    ;
  }

  if (node->num_keys > tree.leaf_max_keys) {
    std::cout << "// Leaf node has too many keys: " << node->num_keys << " > "
              << tree.leaf_max_keys << std::endl;
    return false;
    ;
  }

  // Keys must be sorted in ascending order
  for (uint32_t i = 1; i < node->num_keys; i++) {
    if (cmp(tree, get_key_at(tree, node, i), get_key_at(tree, node, i - 1)) <=
        0) {
      std::cout << "// Leaf keys not in ascending order at positions " << i - 1
                << " and " << i << std::endl;
      return false;
      ;
    }
  }

  // If this is not the root, validate parent relationship
  if (node->parent != 0) {
    BPTreeNode *parent = static_cast<BPTreeNode *>(pager_get(node->parent));
    if (!parent) {
      std::cout << "// Cannot access parent node at page " << node->parent
                << std::endl;
      return false;
      ;
    }

    if (parent->is_leaf) {
      std::cout << "// Parent node is marked as leaf but has children"
                << std::endl;
      return false;
      ;
    }

    // Verify that parent actually points to this node
    uint32_t *parent_children = get_children(tree, parent);
    bool found_in_parent = false;
    for (uint32_t i = 0; i <= parent->num_keys; i++) {
      if (parent_children[i] == node->index) {
        found_in_parent = true;
        break;
      }
    }
    if (!found_in_parent) {
      std::cout << "// Node not found in parent's children array" << std::endl;
      return false;
      ;
    }
  }

  // Validate sibling links if they exist
  if (node->next != 0) {
    BPTreeNode *next_node = static_cast<BPTreeNode *>(pager_get(node->next));
    if (next_node && next_node->previous != node->index) {
      // std::cout << "// Next sibling's previous pointer does not point back
      // to
      // "
      //              "this node"
      //           << std::endl;
      return false;
      ;
    }
  }

  if (node->previous != 0) {
    BPTreeNode *prev_node =
        static_cast<BPTreeNode *>(pager_get(node->previous));
    if (prev_node && prev_node->next != node->index) {
      // std::cout << "/////Previous sibling's next pointer does not point to
      // this node"
      //     << std::endl;
      return false;
      ;
    }
  }
  return true;
}

static bool validate_bplus_internal_node(BPlusTree &tree, BPTreeNode *node) {
  if (!node) {
    std::cout << "// Node pointer is null" << std::endl;
    return false;
    ;
  }

  // Must not be marked as leaf
  if (node->is_leaf) {
    std::cout << "// Node is marked as leaf but should be internal"
              << std::endl;
    return false;
    ;
  }

  // Key count must be within bounds
  uint32_t min_keys =
      (node->parent == 0)
          ? 1
          : tree.internal_min_keys; // Root can have as few as 1 key
  if (node->num_keys < min_keys) {

    std::cout << "// Internal node has too few keys: " << node->num_keys
              << " < " << min_keys << std::endl;
    return false;
    ;
  }

  if (node->num_keys > tree.internal_max_keys) {
    std::cout << "// Internal node has too many keys: " << node->num_keys
              << " > " << tree.internal_max_keys << std::endl;
    return false;
    ;
  }

  // Keys must be sorted in ascending order
  for (uint32_t i = 1; i < node->num_keys; i++) {
    if (cmp(tree, get_key_at(tree, node, i), get_key_at(tree, node, i - 1)) <=
        0) {
      std::cout << "// Internal keys not in ascending order at positions "
                << i - 1 << " and " << i << std::endl;
      return false;
      ;
    }
  }

  // Must have n+1 children for n keys
  uint32_t *children = get_children(tree, node);
  for (uint32_t i = 0; i <= node->num_keys; i++) {
    if (children[i] == 0) {
      std::cout << "// Internal node missing child at index " << i << std::endl;
      return false;
      ;
    }

    // Verify child exists and points back to this node as parent
    BPTreeNode *child = static_cast<BPTreeNode *>(pager_get(children[i]));
    if (!child) {
      std::cout << "// Cannot access child node at page " << children[i]
                << std::endl;
      return false;
      ;
    }

    if (child->parent != node->index) {

      std::cout << "// Child node's parent pointer does not point to this node"
                << std::endl;
      return false;
      ;
    }

    // Check no self-reference
    if (children[i] == node->index) {
      std::cout << "// Node references itself as child" << std::endl;
      return false;
      ;
    }
  }

  // Internal nodes should not have next/previous pointers (only leaves do)
  if (node->next != 0 || node->previous != 0) {
    std::cout << "// Internal node has sibling pointers (next=" << node->next
              << ", prev=" << node->previous << "), but only leaves should"
              << std::endl;
    return false;
    ;
  }

  // If this is not the root, validate parent relationship
  if (node->parent != 0) {
    BPTreeNode *parent = static_cast<BPTreeNode *>(pager_get(node->parent));
    if (!parent) {
      std::cout << "// Cannot access parent node at page " << node->parent
                << std::endl;
      return false;
      ;
    }

    if (parent->is_leaf) {
      std::cout << "// Parent node is marked as leaf but has children"
                << std::endl;
      return false;
      ;
    }

    // Verify that parent actually points to this node
    uint32_t *parent_children = get_children(tree, parent);
    bool found_in_parent = false;
    for (uint32_t i = 0; i <= parent->num_keys; i++) {
      if (parent_children[i] == node->index) {
        found_in_parent = true;
        break;
      }
    }
    if (!found_in_parent) {
      std::cout << "// Node not found in parent's children array" << std::endl;
      return false;
      ;
    }
  }
  return true;
}

static bool validate_btree_node(BPlusTree &tree, BPTreeNode *node) {
  if (!node) {
    std::cout << "// Node pointer is null" << std::endl;
    return false;
    ;
  }

  // For regular B-trees, both internal and leaf nodes store records
  // Key count must be within bounds (same for both leaf and internal in
  // regular B-tree)
  uint32_t min_keys =
      (node->parent == 0) ? 0 : tree.leaf_min_keys; // Using leaf limits since
                                                    // they're the same in BTREE
  uint32_t max_keys = tree.leaf_max_keys; // Same for both in regular B-tree

  if (node->num_keys < min_keys && 0 != node->parent) {
    std::cout << "// B-tree node has too few keys: " << node->num_keys << " < "
              << min_keys << std::endl;
    return false;
    ;
  }

  if (node->num_keys > max_keys) {
    std::cout << "// B-tree node has too many keys: " << node->num_keys << " > "
              << max_keys << std::endl;
    return false;
    ;
  }

  // Keys must be sorted in ascending order
  for (uint32_t i = 1; i < node->num_keys; i++) {
    if (cmp(tree, get_key_at(tree, node, i), get_key_at(tree, node, i - 1)) <
        0) {
      std::cout << "// B-tree keys not in ascending order at positions "
                << i - 1 << " and " << i << std::endl;
      return false;
      ;
    }
  }

  // If internal node, validate children
  if (!node->is_leaf) {
    uint32_t *children = get_children(tree, node);
    for (uint32_t i = 0; i <= node->num_keys; i++) {
      if (children[i] == 0) {
        std::cout << "// B-tree internal node missing child at index " << i
                  << std::endl;
        return false;
        ;
      }

      // Verify child exists and points back to this node as parent
      BPTreeNode *child = static_cast<BPTreeNode *>(pager_get(children[i]));
      if (!child) {
        std::cout << "// Cannot access child node at page " << children[i]
                  << std::endl;
        return false;
        ;
      }

      if (child->parent != node->index) {
        std::cout
            << "// Child node's parent pointer does not point to this node"
            << std::endl;
        return false;
        ;
      }

      // Check no self-reference
      if (children[i] == node->index) {
        std::cout << "// Node references itself as child" << std::endl;
        return false;
        ;
      }
    }

    // Internal nodes in B-tree should not have next/previous pointers
    if (node->next != 0 || node->previous != 0) {
      std::cout
          << "// B-tree internal node has sibling pointers, but should not"
          << std::endl;
      return false;
      ;
    }
  }

  // If this is not the root, validate parent relationship
  if (node->parent != 0) {
    BPTreeNode *parent = static_cast<BPTreeNode *>(pager_get(node->parent));
    if (!parent) {
      std::cout << "// Cannot access parent node at page " << node->parent
                << std::endl;
      return false;
      ;
    }

    if (parent->is_leaf) {
      std::cout << "// Parent node is marked as leaf but has children"
                << std::endl;
      return false;
      ;
    }

    // Verify that parent actually points to this node
    uint32_t *parent_children = get_children(tree, parent);
    bool found_in_parent = false;
    for (uint32_t i = 0; i <= parent->num_keys; i++) {
      if (parent_children[i] == node->index) {
        found_in_parent = true;
        break;
      }
    }
    if (!found_in_parent) {
      std::cout << "// Node not found in parent's children array" << std::endl;
      return false;
      ;
    }
  }
  return true;
}

bool bp_validate_all_invariants(BPlusTree &tree) {
  BPTreeNode *root = bp_get_root(tree);
  if (!root)
    return true;

  int expected_height = 0;
  while (root && !root->is_leaf) {
    root = bp_get_child(tree, root, 0);
    expected_height++;
  }

  // Verify all nodes
  std::queue<BPTreeNode *> to_visit;
  root = bp_get_root(tree);
  to_visit.push(root);

  while (!to_visit.empty()) {
    BPTreeNode *node = to_visit.front();
    to_visit.pop();

    if (tree.tree_type == BTREE) {
      if (!validate_btree_node(tree, node)) {
        return false;
      }
    } else {
      if (node->is_leaf) {
        if (!validate_bplus_leaf_node(tree, node)) {
          return false;
        }
      } else {
        if (!validate_bplus_internal_node(tree, node)) {
          return false;
        }
      }
    }

    if (!node->is_leaf) {
      for (uint32_t i = 0; i <= node->num_keys; i++) {
        BPTreeNode *child = bp_get_child(tree, node, i);
        if (child) {
          to_visit.push(child);
        }
      }
    }
  }

  root = bp_get_root(tree);
  // Verify key separation
  return validate_key_separation(tree, root) &&

         // Verify leaf links
         validate_leaf_links(tree) &&

         // Verify uniform height
         validate_tree_height(tree, root, expected_height);
}

/*---------------- CURSOR --------------------- */
