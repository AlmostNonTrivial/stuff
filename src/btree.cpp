// /*---------------- CURSOR --------------------- */
// ===== COVERAGE TRACKING CODE =====
#include <iostream>
#include <unordered_set>
#include <string>
#include <algorithm>
#include <vector>

// Global set of uncovered points - starts with all points
std::unordered_set<std::string> __uncovered_points = {"bp_get_max_keys_1", "bp_get_min_keys_2", "bp_get_split_index_3", "unknown_4", "bp_mark_dirty_5", "cmp_6", "unknown_7", "unknown_8", "unknown_9", "unknown_10", "unknown_11", "unknown_12", "unknown_13", "unknown_14", "unknown_15", "unknown_16", "unknown_17", "bp_binary_search_18", "bp_binary_search_19", "bp_binary_search_20", "bp_binary_search_21", "bp_binary_search_22", "bp_binary_search_23", "bt_create_24", "bt_create_25", "bt_create_26", "bt_create_27", "bt_create_28", "unknown_29", "bp_init_30", "bp_init_31", "bp_set_next_32", "bp_set_prev_33", "unknown_34", "unknown_35", "unknown_36", "unknown_37", "unknown_38", "unknown_39", "unknown_40", "bp_set_parent_41", "bp_set_parent_42", "bp_set_child_43", "bp_set_child_44", "bp_set_child_45", "bp_destroy_node_46", "bp_destroy_node_47", "bp_destroy_node_48", "bp_destroy_node_49", "bp_destroy_node_50", "bp_destroy_node_51", "unknown_52", "unknown_53", "unknown_54", "unknown_55", "unknown_56", "unknown_57", "unknown_58", "unknown_59", "unknown_60", "unknown_61", "unknown_62", "unknown_63", "bp_insert_repair_64", "bp_insert_repair_65", "bp_insert_repair_66", "bp_insert_67", "bp_insert_68", "bp_insert_69", "bp_insert_70", "bp_insert_71", "bp_insert_72", "bp_insert_element_73", "bp_insert_element_74", "bp_find_element_75", "unknown_76", "unknown_77", "unknown_78", "bp_delete_internal_btree_79", "bp_do_delete_btree_80", "bp_do_delete_btree_81", "bp_do_delete_btree_82", "bp_do_delete_btree_83", "bp_update_parent_keys_84", "bp_update_parent_keys_85", "bp_update_parent_keys_86", "bp_update_parent_keys_87", "bp_update_parent_keys_88", "bp_update_parent_keys_89", "bp_update_parent_keys_90", "bp_do_delete_bplus_91", "bp_do_delete_bplus_92", "bp_do_delete_bplus_93", "bp_do_delete_bplus_94", "bp_do_delete_bplus_95", "bp_do_delete_bplus_96", "bp_do_delete_97", "bp_do_delete_98", "bp_do_delete_99", "unknown_100", "unknown_101", "unknown_102", "unknown_103", "unknown_104", "unknown_105", "unknown_106", "unknown_107", "unknown_108", "unknown_109", "unknown_110", "unknown_111", "unknown_112", "unknown_113", "unknown_114", "bp_repair_after_delete_115", "bp_repair_after_delete_116", "bp_repair_after_delete_117", "bp_repair_after_delete_118", "bp_repair_after_delete_119", "bp_repair_after_delete_120", "bp_repair_after_delete_121", "bp_repair_after_delete_122", "bp_repair_after_delete_123", "bp_repair_after_delete_124", "bp_repair_after_delete_125", "bp_repair_after_delete_126", "bp_delete_element_127", "bp_delete_element_128", "bp_delete_element_129", "bp_delete_element_130", "print_uint8_as_chars_131", "bp_print_node_132", "bp_print_node_133", "bp_print_node_134", "bp_print_node_135", "bp_print_node_136", "bp_print_node_137", "bp_print_node_138", "bp_print_node_139", "bp_print_node_140", "print_tree_141", "print_tree_142", "print_tree_143", "print_tree_144", "print_tree_145", "debug_hash_tree_146", "debug_hash_tree_147", "debug_hash_tree_148", "debug_hash_tree_149", "debug_hash_tree_150", "debug_hash_tree_151", "validate_key_separation_152", "validate_key_separation_153", "validate_key_separation_154", "validate_key_separation_155", "validate_key_separation_156", "validate_key_separation_157", "validate_key_separation_158", "validate_leaf_links_159", "validate_leaf_links_160", "validate_leaf_links_161", "validate_leaf_links_162", "validate_tree_height_163", "validate_tree_height_164", "validate_tree_height_165", "validate_tree_height_166", "validate_tree_height_167", "validate_bplus_leaf_node_168", "validate_bplus_leaf_node_169", "validate_bplus_leaf_node_170", "validate_bplus_leaf_node_171", "validate_bplus_leaf_node_172", "validate_bplus_leaf_node_173", "validate_bplus_leaf_node_174", "validate_bplus_leaf_node_175", "validate_bplus_leaf_node_176", "validate_bplus_leaf_node_177", "validate_bplus_leaf_node_178", "validate_bplus_leaf_node_179", "validate_bplus_leaf_node_180", "validate_bplus_leaf_node_181", "validate_bplus_leaf_node_182", "validate_bplus_internal_node_183", "validate_bplus_internal_node_184", "validate_bplus_internal_node_185", "validate_bplus_internal_node_186", "validate_bplus_internal_node_187", "validate_bplus_internal_node_188", "validate_bplus_internal_node_189", "validate_bplus_internal_node_190", "validate_bplus_internal_node_191", "validate_bplus_internal_node_192", "validate_bplus_internal_node_193", "validate_bplus_internal_node_194", "validate_bplus_internal_node_195", "validate_bplus_internal_node_196", "validate_bplus_internal_node_197", "validate_bplus_internal_node_198", "validate_btree_node_199", "validate_btree_node_200", "validate_btree_node_201", "validate_btree_node_202", "validate_btree_node_203", "validate_btree_node_204", "validate_btree_node_205", "validate_btree_node_206", "validate_btree_node_207", "validate_btree_node_208", "validate_btree_node_209", "validate_btree_node_210", "validate_btree_node_211", "validate_btree_node_212", "validate_btree_node_213", "validate_btree_node_214", "bp_validate_all_invariants_215", "bp_validate_all_invariants_216", "bp_validate_all_invariants_217", "bp_validate_all_invariants_218", "bp_validate_all_invariants_219", "bp_validate_all_invariants_220", "bp_validate_all_invariants_221", "bp_validate_all_invariants_222"};

// Total number of coverage points
const size_t __total_points = 222;

// Function to mark coverage - removes from uncovered set
void COVER(const std::string& point) {
    __uncovered_points.erase(point);
}

// Function to print coverage report
void print_coverage_report() {
    size_t covered_count = __total_points - __uncovered_points.size();

    std::cout << "\n===== COVERAGE REPORT =====\n";
    std::cout << "Total coverage points: " << __total_points << "\n";
    std::cout << "Points covered: " << covered_count << "\n";
    std::cout << "Coverage: " << (100.0 * covered_count / __total_points) << "%\n\n";

    if (__uncovered_points.empty()) {
        std::cout << "✓ All paths covered!\n";
    } else {
        std::cout << "Uncovered points:\n";
        for (const auto& point : __uncovered_points) {
            std::cout << "  ✗ " << point << "\n";
        }
    }
}

// Automatically print report at program exit
struct CoverageReporter {
    ~CoverageReporter() {
        // print_coverage_report();
    }
};
CoverageReporter __coverage_reporter;

// ===== END COVERAGE TRACKING =====

#include "btree.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <sys/types.h>
#include <vector>
// Path tracking structure
// Global coverage tracker


void bp_repair_after_delete(BPlusTree &tree, BPTreeNode *node);
// Macro for marking paths as covered


// ============= FULL TREE IMPLEMENTATION WITH COVERAGE =============

#define PRINT(x) std::cout << x << "\n"

uint32_t bp_get_max_keys(BPlusTree &tree, BPTreeNode *node) {
  COVER("bp_get_max_keys_1");

  return node->is_leaf ? tree.leaf_max_keys : tree.internal_max_keys;
}

uint32_t bp_get_min_keys(BPlusTree &tree, BPTreeNode *node) {
  COVER("bp_get_min_keys_2");

  return node->is_leaf ? tree.leaf_min_keys : tree.internal_min_keys;
}

uint32_t bp_get_split_index(BPlusTree &tree, BPTreeNode *node) {
  COVER("bp_get_split_index_3");

  return node->is_leaf ? tree.leaf_split_index : tree.internal_split_index;
}

BPTreeNode *bp_get_root(BPlusTree &tree) {
  COVER("unknown_4");

  return static_cast<BPTreeNode *>(pager_get(tree.root_page_index));
}

void bp_mark_dirty(BPTreeNode *node) { pager_mark_dirty(node->index); }
// COVER("bp_mark_dirty_5");
int cmp(BPlusTree &tree, const uint8_t *key1, const uint8_t *key2) {
  COVER("cmp_6");

  switch (tree.node_key_size) {
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

    return memcmp(key1, key2, tree.node_key_size);
  }
  default:
    return 0;
  }
}

uint8_t *get_keys(BPTreeNode *node) { return node->data; }

uint8_t *get_key_at(BPlusTree &tree, BPTreeNode *node, uint32_t index) {
  COVER("unknown_8");

  return get_keys(node) + index * tree.node_key_size;
}

uint32_t *get_children(BPlusTree &tree, BPTreeNode *node) {
if (node->is_leaf) {
      return nullptr;  // Leaf nodes don't have children!
}
  COVER("unknown_9");

  if (tree.tree_type == BTREE) {
    COVER("unknown_10");

    return reinterpret_cast<uint32_t *>(
        node->data + tree.internal_max_keys * tree.node_key_size +
        tree.internal_max_keys * tree.record_size);
  }
  return reinterpret_cast<uint32_t *>(node->data + tree.internal_max_keys *
                                                       tree.node_key_size);
}

uint8_t *get_internal_record_data(BPlusTree &tree, BPTreeNode *node) {
  COVER("unknown_11");

  return node->data + tree.internal_max_keys * tree.node_key_size;
}

uint8_t *get_internal_record_at(BPlusTree &tree, BPTreeNode *node,
                                uint32_t index) {
  COVER("unknown_12");

  return get_internal_record_data(tree, node) + (index * tree.record_size);
}

uint8_t *get_leaf_record_data(BPlusTree &tree, BPTreeNode *node) {
  COVER("unknown_13");

  return node->data + tree.leaf_max_keys * tree.node_key_size;
}

uint8_t *get_leaf_record_at(BPlusTree &tree, BPTreeNode *node, uint32_t index) {
  COVER("unknown_14");

  return get_leaf_record_data(tree, node) + (index * tree.record_size);
}

uint8_t *get_records(BPlusTree &tree, BPTreeNode *node) {
  COVER("unknown_15");

  return node->is_leaf ? get_leaf_record_data(tree, node)
                       : get_leaf_record_data(tree, node);
}

uint8_t *get_record_at(BPlusTree &tree, BPTreeNode *node, uint32_t index) {
  COVER("unknown_16");

  if (node->is_leaf) {
    COVER("unknown_17");

    return get_leaf_record_at(tree, node, index);
  }
  return get_internal_record_at(tree, node, index);
}

uint32_t bp_binary_search(BPlusTree &tree, BPTreeNode *node,
                          const uint8_t *key) {
  COVER("bp_binary_search_18");

  uint32_t left = 0;
  uint32_t right = node->num_keys;

  while (left < right) {
    uint32_t mid = left + (right - left) / 2;
    int cmp_result = cmp(tree, get_key_at(tree, node, mid), key);

    if (cmp_result < 0) {
      COVER("bp_binary_search_19");
      left = mid + 1;
    } else if (cmp_result == 0) {
      COVER("bp_binary_search_20");

      if (tree.tree_type == BTREE) {
        COVER("bp_binary_search_21");
        return mid;
      }
      if (node->is_leaf) {
        COVER("bp_binary_search_22");
        return mid;
      }

      return mid + 1;
    } else {

      right = mid;
    }
  }

  if (node->is_leaf) {
    COVER("bp_binary_search_23");
    return left;
  } else {

    return left;
  }
}

BPlusTree bt_create(DataType key, uint32_t record_size, TreeType tree_type) {
  COVER("bt_create_24");

  BPlusTree tree;
  tree.node_key_size = key;
  tree.tree_type = tree_type;
  tree.record_size = record_size;

  constexpr uint32_t USABLE_SPACE = PAGE_SIZE - NODE_HEADER_SIZE;
  const uint32_t minimum_entry_count = 3U;

  if ((record_size * minimum_entry_count) > USABLE_SPACE) {
    COVER("bt_create_25");
    tree.tree_type = INVALID;
    return tree;
  }

  if (tree_type == BTREE) {
    COVER("bt_create_26");
    uint32_t key_record_size = tree.node_key_size + record_size;
    uint32_t child_ptr_size = TYPE_INT32;

    uint32_t max_keys =
        (USABLE_SPACE - child_ptr_size) / (key_record_size + child_ptr_size);

    uint32_t min_keys;
    if (max_keys % 2 == 0) {
      COVER("bt_create_27");

      min_keys = (max_keys / 2) - 1; // For max=12, min=5
    } else {
      min_keys = (max_keys) / 2; // For max=11, min=6
    }

    max_keys = std::max(minimum_entry_count, max_keys);

    uint32_t split_index = max_keys / 2;

    tree.leaf_max_keys = max_keys;
    tree.leaf_min_keys = min_keys;
    tree.leaf_split_index = split_index;
    tree.internal_max_keys = max_keys;
    tree.internal_min_keys = min_keys;
    tree.internal_split_index = split_index;
  } else if (tree_type == BPLUS) {
    COVER("bt_create_28");
    uint32_t leaf_entry_size = tree.node_key_size + record_size;
    uint32_t leaf_max_entries = USABLE_SPACE / leaf_entry_size;

    tree.leaf_max_keys = std::max(minimum_entry_count, leaf_max_entries);
    tree.leaf_min_keys = tree.leaf_max_keys / 2;
    tree.leaf_split_index = tree.leaf_max_keys / 2;

    uint32_t child_ptr_size = TYPE_INT32;
    uint32_t internal_max_entries =
        (USABLE_SPACE - child_ptr_size) / (tree.node_key_size + child_ptr_size);
    tree.internal_max_keys =
        std::max(minimum_entry_count, internal_max_entries);
    tree.internal_min_keys = tree.internal_max_keys / 2;
    tree.internal_split_index = tree.internal_max_keys / 2;
  } else {

    tree.tree_type = INVALID;
    return tree;
  }

  tree.root_page_index = 0;
  return tree;
}

BPTreeNode *bp_create_node(BPlusTree &tree, bool is_leaf) {
  COVER("unknown_29");

  uint32_t page_index = pager_new();
  BPTreeNode *node = static_cast<BPTreeNode *>(pager_get(page_index));

  node->index = page_index;
  node->parent = 0;
  node->next = 0;
  node->previous = 0;
  node->num_keys = 0;
  node->is_leaf = is_leaf ? 1 : 0;

  pager_mark_dirty(page_index);
  return node;
}

void bp_init(BPlusTree &tree) {
  COVER("bp_init_30");

  if (tree.root_page_index == 0) {
    COVER("bp_init_31");

    pager_begin_transaction();
    BPTreeNode *root = bp_create_node(tree, true);
    tree.root_page_index = root->index;
    pager_commit();
  }
}

void bp_set_next(BPTreeNode *node, uint32_t index) {
  COVER("bp_set_next_32");

  bp_mark_dirty(node);
  node->next = index;
}

void bp_set_prev(BPTreeNode *node, uint32_t index) {
  COVER("bp_set_prev_33");

  bp_mark_dirty(node);
  node->previous = index;
}

BPTreeNode *bp_get_parent(BPTreeNode *node) {
  COVER("unknown_34");

  if (!node || node->parent == 0) {
    COVER("unknown_35");
    return nullptr;
  }

  return static_cast<BPTreeNode *>(pager_get(node->parent));
}

BPTreeNode *bp_get_child(BPlusTree &tree, BPTreeNode *node, uint32_t index) {
  COVER("unknown_36");

  if (!node || node->is_leaf) {
    COVER("unknown_37");
    return nullptr;
  }

  uint32_t *children = get_children(tree, node);
  if (index >= node->num_keys + 1 || children[index] == 0) {
    COVER("unknown_38");
    return nullptr;
  }


  return static_cast<BPTreeNode *>(pager_get(children[index]));
}

BPTreeNode *bp_get_next(BPTreeNode *node) {
  COVER("unknown_39");

  if (!node || node->next == 0)
    return nullptr;
  return static_cast<BPTreeNode *>(pager_get(node->next));
}

BPTreeNode *bp_get_prev(BPTreeNode *node) {
  COVER("unknown_40");

  if (!node || node->previous == 0)
    return nullptr;
  return static_cast<BPTreeNode *>(pager_get(node->previous));
}

void bp_set_parent(BPTreeNode *node, uint32_t parent_index) {
  COVER("bp_set_parent_41");

  if (!node)
    return;

  bp_mark_dirty(node);
  node->parent = parent_index;

  if (parent_index != 0) {
    COVER("bp_set_parent_42");

    pager_mark_dirty(parent_index);
  }
}

void bp_set_child(BPlusTree &tree, BPTreeNode *node, uint32_t child_index,
                  uint32_t node_index) {
  COVER("bp_set_child_43");

  if (!node || node->is_leaf)
    return;

  bp_mark_dirty(node);
  uint32_t *children = get_children(tree, node);
  children[child_index] = node_index;

  if (node_index != 0) {
    COVER("bp_set_child_44");
    BPTreeNode *child_node = static_cast<BPTreeNode *>(pager_get(node_index));
    if (child_node) {
      COVER("bp_set_child_45");

      bp_set_parent(child_node, node->index);
    }
  } else {

  }
}

void bp_destroy_node(BPTreeNode *node) {
  COVER("bp_destroy_node_46");

  if (node->is_leaf) {
    COVER("bp_destroy_node_47");

    if (node->previous != 0) {
      COVER("bp_destroy_node_48");

      BPTreeNode *prev_node = bp_get_prev(node);
      if (prev_node) {
        COVER("bp_destroy_node_49");

        bp_set_next(prev_node, node->next);
      }
    }

    if (node->next != 0) {
      COVER("bp_destroy_node_50");

      BPTreeNode *next_node = bp_get_next(node);
      if (next_node) {
        COVER("bp_destroy_node_51");

        bp_set_prev(next_node, node->previous);
      }
    }
  }

  pager_delete(node->index);
}

BPTreeNode *bp_find_containing_node(BPlusTree &tree, BPTreeNode *node,
                                    const uint8_t *key, int iter = 0) {
  COVER("unknown_52");

  if (node->is_leaf) {
    COVER("unknown_53");
    return const_cast<BPTreeNode *>(node);
  }

  uint32_t child_or_key_index = bp_binary_search(tree, node, key);
  if (tree.tree_type == BTREE) {
    COVER("unknown_54");

    if (cmp(tree, key, get_key_at(tree, node, child_or_key_index)) == 0) {
      COVER("unknown_55");
      return const_cast<BPTreeNode *>(node);
    }
  }
  if(iter > 100) {
      // print_tree(tree);
      exit(0);
      std::cout <<"stack overflow\n";
  }

  return bp_find_containing_node(
      tree, bp_get_child(tree, node, child_or_key_index), key, iter + 1);
}

BPTreeNode *bp_split(BPlusTree &tree, BPTreeNode *node) {
  COVER("unknown_56");

  BPTreeNode *right_node = bp_create_node(tree, node->is_leaf);
  uint32_t split_index = bp_get_split_index(tree, node);
  uint8_t *rising_key = get_key_at(tree, node, split_index);

  bp_mark_dirty(right_node);
  bp_mark_dirty(node);

  BPTreeNode *parent = bp_get_parent(node);
  uint32_t parent_index = 0;

  if (!parent) {
    COVER("unknown_57");
    parent = bp_create_node(tree, false);
    bp_mark_dirty(parent);
    tree.root_page_index = parent->index;
    bp_set_child(tree, parent, 0, node->index);
  } else {

    bp_mark_dirty(parent);

    uint32_t *parent_children = get_children(tree, parent);
    while (parent_children[parent_index] != node->index)
      parent_index++;

    memcpy(parent_children + parent_index + 2,
           parent_children + parent_index + 1,
           (parent->num_keys - parent_index) * TYPE_INT32);

    memcpy(get_key_at(tree, parent, parent_index + 1),
           get_key_at(tree, parent, parent_index),
           (parent->num_keys - parent_index) * tree.node_key_size);

    if (tree.tree_type == BTREE) {
      COVER("unknown_58");
      uint8_t *parent_records = get_internal_record_data(tree, parent);
      memcpy(parent_records + (parent_index + 1) * tree.record_size,
             parent_records + parent_index * tree.record_size,
             (parent->num_keys - parent_index) * tree.record_size);
    }
  }

  memcpy(get_key_at(tree, parent, parent_index), rising_key,
         tree.node_key_size);
  bp_set_child(tree, parent, parent_index + 1, right_node->index);
  parent->num_keys++;

  if (tree.tree_type == BTREE) {
    COVER("unknown_59");

    uint8_t *parent_records = get_internal_record_data(tree, parent);
    uint8_t *source_records = get_records(tree, node);
    memcpy(parent_records + parent_index * tree.record_size,
           source_records + split_index * tree.record_size, tree.record_size);
  }

  if (node->is_leaf && tree.tree_type == BPLUS) {
    COVER("unknown_60");
    right_node->num_keys = node->num_keys - split_index;
    memcpy(get_keys(right_node), rising_key,
           tree.node_key_size * right_node->num_keys);
    memcpy(get_leaf_record_data(tree, right_node),
           get_record_at(tree, node, split_index),
           right_node->num_keys * tree.record_size);

    right_node->next = node->next;
    right_node->previous = node->index;
    if (node->next != 0) {
        BPTreeNode* next = bp_get_next(node);
        if (next) bp_set_prev(next, right_node->index);
    }
    node->next = right_node->index;
  } else {

    right_node->num_keys = node->num_keys - split_index - 1;
    memcpy(get_keys(right_node), get_key_at(tree, node, split_index + 1),
           right_node->num_keys * tree.node_key_size);

    if (tree.tree_type == BTREE) {
      COVER("unknown_61");

      uint8_t *src_records = get_records(tree, node);
      uint8_t *dst_records = get_records(tree, right_node);
      memcpy(dst_records, src_records + (split_index + 1) * tree.record_size,
             right_node->num_keys * tree.record_size);
    }

    if (!node->is_leaf) {
      COVER("unknown_62");
      uint32_t *src_children = get_children(tree, node);
      for (uint32_t i = 0; i <= right_node->num_keys; i++) {
        uint32_t child = src_children[split_index + 1 + i];
        if (child) {
          COVER("unknown_63");

          bp_set_child(tree, right_node, i, child);
          src_children[split_index + 1 + i] = 0;
        }
      }
    }
  }

  node->num_keys = split_index;
  return parent;
}

void bp_insert_repair(BPlusTree &tree, BPTreeNode *node) {
  COVER("bp_insert_repair_64");

  if (node->num_keys < bp_get_max_keys(tree, node)) {
    COVER("bp_insert_repair_65");
    return;
  } else if (node->parent == 0) {
    COVER("bp_insert_repair_66");
    BPTreeNode *new_root = bp_split(tree, node);
    tree.root_page_index = new_root->index;
  } else {

    BPTreeNode *new_node = bp_split(tree, node);
    bp_insert_repair(tree, new_node);
  }
}

void bp_insert(BPlusTree &tree, BPTreeNode *node, uint8_t *key,
               const uint8_t *data) {
  COVER("bp_insert_67");

  if (node->is_leaf) {
    COVER("bp_insert_68");

    uint8_t *keys = get_keys(node);
    uint8_t *record_data = get_leaf_record_data(tree, node);

    uint32_t insert_index = bp_binary_search(tree, node, key);

    if (tree.tree_type == BPLUS && insert_index < node->num_keys &&
        cmp(tree, get_key_at(tree, node, insert_index), key) == 0) {
      COVER("bp_insert_69");
      bp_mark_dirty(node);
      memcpy(record_data + insert_index * tree.record_size, data,
             tree.record_size);
      return;
    }

    if (node->num_keys >= tree.leaf_max_keys) {
      COVER("bp_insert_70");
      bp_insert_repair(tree, node);
      bp_insert(tree, bp_find_containing_node(tree, bp_get_root(tree), key),
                key, data);
      return;
    }

    bp_mark_dirty(node);

    if (tree.tree_type == BTREE) {
      COVER("bp_insert_71");
      while (insert_index < node->num_keys &&
             cmp(tree, get_key_at(tree, node, insert_index), key) == 0) {
        insert_index++;
      }
    }


    uint32_t num_to_shift = node->num_keys - insert_index;

    memcpy(get_key_at(tree, node, insert_index + 1),
           get_key_at(tree, node, insert_index),
           num_to_shift * tree.node_key_size);

    memcpy(record_data + (insert_index + 1) * tree.record_size,
           record_data + insert_index * tree.record_size,
           num_to_shift * tree.record_size);

    memcpy(get_key_at(tree, node, insert_index), key, tree.node_key_size);
    memcpy(record_data + insert_index * tree.record_size, data,
           tree.record_size);

    node->num_keys++;
  } else {

    uint32_t child_index = bp_binary_search(tree, node, key);

    BPTreeNode *child_node = bp_get_child(tree, node, child_index);
    if (child_node) {
      COVER("bp_insert_72");

      bp_insert(tree, child_node, key, data);
    }
  }
}

void bp_insert_element(BPlusTree &tree, void *key, const uint8_t *data) {
  COVER("bp_insert_element_73");

  BPTreeNode *root = bp_get_root(tree);

  if (root->num_keys == 0) {
    COVER("bp_insert_element_74");
    bp_mark_dirty(root);
    uint8_t *keys = get_keys(root);
    uint8_t *record_data = get_leaf_record_data(tree, root);

    memcpy(keys, key, tree.node_key_size);
    memcpy(record_data, data, tree.record_size);
    root->num_keys = 1;
  } else {
    bp_insert(tree, root, (uint8_t *)key, data);
  }

  pager_sync();
}

bool bp_find_element(BPlusTree &tree, void *key) {
  COVER("bp_find_element_75");

  auto containing_or_end_node =
      bp_find_containing_node(tree, bp_get_root(tree), (uint8_t *)key);

  uint32_t index =
      bp_binary_search(tree, containing_or_end_node, (uint8_t *)key);

  return index < containing_or_end_node->num_keys &&
         cmp(tree, get_key_at(tree, containing_or_end_node, index),
             (uint8_t *)key) == 0;
}

const uint8_t *bp_get(BPlusTree &tree, void *key) {
  COVER("unknown_76");

  BPTreeNode *root = bp_get_root(tree);
  BPTreeNode *leaf_node = bp_find_containing_node(tree, root, (uint8_t *)key);

  uint32_t pos = bp_binary_search(tree, leaf_node, (uint8_t *)key);
  if (pos < leaf_node->num_keys &&
      cmp(tree, get_key_at(tree, leaf_node, pos), (uint8_t *)key) == 0) {
    COVER("unknown_77");
    return get_record_at(tree, leaf_node, pos);
  }


  return nullptr;
}

BPTreeNode *bp_left_most(BPlusTree &tree) {
  COVER("unknown_78");

  BPTreeNode *temp = bp_get_root(tree);

  while (temp && !temp->is_leaf) {
    temp = bp_get_child(tree, temp, 0);
  }

  return temp;
}

static void bp_delete_internal_btree(BPlusTree &tree, BPTreeNode *node,
                                     uint32_t index) {
  COVER("bp_delete_internal_btree_79");

  BPTreeNode *curr = bp_get_child(tree, node, index);

  while (!curr->is_leaf) {
    curr = bp_get_child(tree, curr, curr->num_keys);
  }

  uint32_t pred_index = curr->num_keys - 1;
  uint8_t *pred_key = get_key_at(tree, curr, pred_index);
  uint8_t *pred_record = get_record_at(tree, curr, pred_index);

  bp_mark_dirty(node);
  memcpy(get_key_at(tree, node, index), pred_key, tree.node_key_size);
  uint8_t *internal_records = get_internal_record_data(tree, node);
  memcpy(internal_records + index * tree.record_size, pred_record,
         tree.record_size);

  bp_mark_dirty(curr);
  uint8_t *leaf_records = get_leaf_record_data(tree, curr);

  uint32_t elements_to_shift = curr->num_keys - 1 - pred_index;
  memcpy(get_key_at(tree, curr, pred_index),
         get_key_at(tree, curr, pred_index + 1),
         elements_to_shift * tree.node_key_size);
  memcpy(leaf_records + pred_index * tree.record_size,
         leaf_records + (pred_index + 1) * tree.record_size,
         elements_to_shift * tree.record_size);

  curr->num_keys--;
  bp_repair_after_delete(tree, curr);
}

void bp_do_delete_btree(BPlusTree &tree, BPTreeNode *node, const uint8_t *key) {
  COVER("bp_do_delete_btree_80");

  uint32_t i = bp_binary_search(tree, node, key);

  bool found_in_this_node =
      i < node->num_keys && cmp(tree, get_key_at(tree, node, i), key) == 0;

  if (found_in_this_node) {
    COVER("bp_do_delete_btree_81");

    if (node->is_leaf) {
      COVER("bp_do_delete_btree_82");
      bp_mark_dirty(node);
      uint8_t *record_data = get_leaf_record_data(tree, node);

      uint32_t shift_count = node->num_keys - i - 1;

      memcpy(get_key_at(tree, node, i), get_key_at(tree, node, i + 1),
             tree.node_key_size * shift_count);
      memcpy(record_data + i * tree.record_size,
             record_data + (i + 1) * tree.record_size,
             tree.record_size * shift_count);

      node->num_keys--;
      bp_repair_after_delete(tree, node);
    } else {

      bp_delete_internal_btree(tree, node, i);
    }
  } else if (!node->is_leaf) {
    COVER("bp_do_delete_btree_83");
    bp_do_delete_btree(tree, bp_get_child(tree, node, i), key);
  }
}

void bp_update_parent_keys(BPlusTree &tree, BPTreeNode *node,
                           const uint8_t *deleted_key) {
  COVER("bp_update_parent_keys_84");

  uint8_t *next_smallest = nullptr;
  BPTreeNode *parent_node = bp_get_parent(node);

  uint32_t *parent_children = get_children(tree, parent_node);
  uint32_t parent_index;

  for (parent_index = 0; parent_children[parent_index] != node->index;
       parent_index++)
    ;

  if (node->num_keys == 0) {
    COVER("bp_update_parent_keys_85");

    if (parent_index == parent_node->num_keys) {
      COVER("bp_update_parent_keys_86");

      next_smallest = nullptr;
    } else {
      BPTreeNode *next_sibling =
          bp_get_child(tree, parent_node, parent_index + 1);
      if (next_sibling) {
        COVER("bp_update_parent_keys_87");

        next_smallest = get_key_at(tree, next_sibling, 0);
      }
    }
  } else {
    next_smallest = get_key_at(tree, node, 0);
  }

  BPTreeNode *current_parent = parent_node;
  while (current_parent) {
    if (parent_index > 0 &&
        cmp(tree, get_key_at(tree, current_parent, parent_index - 1),
            deleted_key) == 0) {
      COVER("bp_update_parent_keys_88");

      bp_mark_dirty(current_parent);
      if (next_smallest) {
        COVER("bp_update_parent_keys_89");

        memcpy(get_key_at(tree, current_parent, parent_index - 1),
               next_smallest, tree.node_key_size);
      }
    }

    BPTreeNode *grandparent = bp_get_parent(current_parent);
    if (grandparent) {
      COVER("bp_update_parent_keys_90");

      uint32_t *grandparent_children = get_children(tree, grandparent);
      for (parent_index = 0;
           grandparent_children[parent_index] != current_parent->index;
           parent_index++)
        ;
    }
    current_parent = grandparent;
  }
}

void bp_do_delete_bplus(BPlusTree &tree, BPTreeNode *node, const uint8_t *key) {
  COVER("bp_do_delete_bplus_91");

  uint32_t i = bp_binary_search(tree, node, key);

  bool traverse_right = i == node->num_keys && !node->is_leaf;
  if (traverse_right) {
    COVER("bp_do_delete_bplus_92");
    bp_do_delete_bplus(tree, bp_get_child(tree, node, node->num_keys), key);
    return;
  }

  bool key_match = cmp(tree, get_key_at(tree, node, i), key) == 0;

  if (!node->is_leaf && key_match) {
    COVER("bp_do_delete_bplus_93");
    bp_do_delete_bplus(tree, bp_get_child(tree, node, i + 1), key);
    return;
  }

  if (!node->is_leaf) {
    COVER("bp_do_delete_bplus_94");
    bp_do_delete_bplus(tree, bp_get_child(tree, node, i), key);
    return;
  }

  if (node->is_leaf && key_match) {
    COVER("bp_do_delete_bplus_95");
    bp_mark_dirty(node);

    uint8_t *record_data = get_leaf_record_data(tree, node);
    uint32_t shift_count = node->num_keys - i - 1;

    memcpy(get_key_at(tree, node, i), get_key_at(tree, node, i + 1),
           tree.node_key_size * shift_count);
    memcpy(record_data + i * tree.record_size,
           record_data + (i + 1) * tree.record_size,
           tree.record_size * shift_count);

    node->num_keys--;

    if (i == 0 && node->parent != 0) {
      COVER("bp_do_delete_bplus_96");

      bp_update_parent_keys(tree, node, key);
    }

    bp_repair_after_delete(tree, node);
  } else {

  }
}

void bp_do_delete(BPlusTree &tree, BPTreeNode *node, const uint8_t *key) {
  COVER("bp_do_delete_97");

  if (!node) {
    COVER("bp_do_delete_98");

    return;
  }

  if (tree.tree_type == BTREE) {
    COVER("bp_do_delete_99");

    bp_do_delete_btree(tree, node, key);
  } else {
    bp_do_delete_bplus(tree, node, key);
  }
}

BPTreeNode *bp_steal_from_right(BPlusTree &tree, BPTreeNode *node,
                                uint32_t parent_index) {
  COVER("unknown_100");

  BPTreeNode *parent_node = bp_get_parent(node);
  BPTreeNode *right_sibling = bp_get_child(tree, parent_node, parent_index + 1);

  bp_mark_dirty(node);
  bp_mark_dirty(parent_node);
  bp_mark_dirty(right_sibling);

  if (node->is_leaf) {
    COVER("unknown_101");
    memcpy(get_key_at(tree, node, node->num_keys),
           get_key_at(tree, right_sibling, 0), tree.node_key_size);

    uint8_t *node_records = get_leaf_record_data(tree, node);
    uint8_t *sibling_records = get_leaf_record_data(tree, right_sibling);

    memcpy(node_records + node->num_keys * tree.record_size, sibling_records,
           tree.record_size);

    uint32_t shift_count = right_sibling->num_keys - 1;
    memcpy(get_key_at(tree, right_sibling, 0),
           get_key_at(tree, right_sibling, 1),
           shift_count * tree.node_key_size);
    memcpy(sibling_records, sibling_records + tree.record_size,
           shift_count * tree.record_size);

    memcpy(get_key_at(tree, parent_node, parent_index),
           get_key_at(tree, right_sibling, 0), tree.node_key_size);
  } else {
    memcpy(get_key_at(tree, node, node->num_keys),
           get_key_at(tree, parent_node, parent_index), tree.node_key_size);

    if (tree.tree_type == BTREE) {
      COVER("unknown_102");
      uint8_t *node_records = get_internal_record_data(tree, node);
      uint8_t *parent_records = get_internal_record_data(tree, parent_node);
      uint8_t *sibling_records = get_internal_record_data(tree, right_sibling);

      memcpy(node_records + node->num_keys * tree.record_size,
             parent_records + parent_index * tree.record_size,
             tree.record_size);

      memcpy(get_key_at(tree, parent_node, parent_index),
             get_key_at(tree, right_sibling, 0), tree.node_key_size);
      memcpy(parent_records + parent_index * tree.record_size, sibling_records,
             tree.record_size);

      uint32_t shift_count = right_sibling->num_keys - 1;
      memcpy(sibling_records, sibling_records + tree.record_size,
             shift_count * tree.record_size);
    } else {

      memcpy(get_key_at(tree, parent_node, parent_index),
             get_key_at(tree, right_sibling, 0), tree.node_key_size);
    }

    uint32_t shift_count = right_sibling->num_keys - 1;
    memcpy(get_key_at(tree, right_sibling, 0),
           get_key_at(tree, right_sibling, 1),
           shift_count * tree.node_key_size);

    uint32_t *node_children = get_children(tree, node);
    uint32_t *sibling_children = get_children(tree, right_sibling);

    bp_set_child(tree, node, node->num_keys + 1, sibling_children[0]);

    for (uint32_t i = 0; i < right_sibling->num_keys; i++) {
      bp_set_child(tree, right_sibling, i, sibling_children[i + 1]);
    }
  }

  node->num_keys++;
  right_sibling->num_keys--;

  return parent_node;
}

BPTreeNode *bp_merge_right(BPlusTree &tree, BPTreeNode *node) {
  COVER("unknown_103");

  BPTreeNode *parent = bp_get_parent(node);
  if (!parent)
    return node;

  uint32_t *parent_children = get_children(tree, parent);

  uint32_t node_index = 0;
  for (; node_index <= parent->num_keys; node_index++) {
    if (parent_children[node_index] == node->index)
      break;
  }

  if (node_index >= parent->num_keys)
    return node;

  BPTreeNode *right_sibling = bp_get_child(tree, parent, node_index + 1);
  if (!right_sibling)
    return node;

  bp_mark_dirty(node);
  bp_mark_dirty(parent);

  if (node->is_leaf) {
    COVER("unknown_104");
    uint8_t *node_records = get_leaf_record_data(tree, node);
    uint8_t *sibling_records = get_leaf_record_data(tree, right_sibling);

    memcpy(get_key_at(tree, node, node->num_keys),
           get_key_at(tree, right_sibling, 0),
           right_sibling->num_keys * tree.node_key_size);
    memcpy(node_records + node->num_keys * tree.record_size, sibling_records,
           right_sibling->num_keys * tree.record_size);

    node->num_keys += right_sibling->num_keys;

    if (tree.tree_type == BPLUS) {
      COVER("unknown_105");
      bp_set_next(node, right_sibling->next);
      if (right_sibling->next != 0) {
        COVER("unknown_106");

        BPTreeNode *next_node = bp_get_next(right_sibling);
        if (next_node) {
          COVER("unknown_107");

          bp_set_prev(next_node, node->index);
        }
      }
    }
  } else {
    memcpy(get_key_at(tree, node, node->num_keys),
           get_key_at(tree, parent, node_index), tree.node_key_size);

    if (tree.tree_type == BTREE) {
      COVER("unknown_108");
      uint8_t *node_records = get_internal_record_data(tree, node);
      uint8_t *parent_records = get_internal_record_data(tree, parent);
      uint8_t *sibling_records = get_internal_record_data(tree, right_sibling);

      memcpy(node_records + node->num_keys * tree.record_size,
             parent_records + node_index * tree.record_size, tree.record_size);

      memcpy(node_records + (node->num_keys + 1) * tree.record_size,
             sibling_records, right_sibling->num_keys * tree.record_size);
    } else {

    }

    memcpy(get_key_at(tree, node, node->num_keys + 1),
           get_key_at(tree, right_sibling, 0),
           right_sibling->num_keys * tree.node_key_size);

    uint32_t *node_children = get_children(tree, node);
    uint32_t *sibling_children = get_children(tree, right_sibling);

    for (uint32_t i = 0; i <= right_sibling->num_keys; i++) {
      bp_set_child(tree, node, node->num_keys + 1 + i, sibling_children[i]);
    }

    node->num_keys += 1 + right_sibling->num_keys;
  }

  uint32_t shift_count = parent->num_keys - node_index - 1;

  memcpy(get_key_at(tree, parent, node_index),
         get_key_at(tree, parent, node_index + 1),
         shift_count * tree.node_key_size);
  memcpy(parent_children + node_index + 1, parent_children + node_index + 2,
         shift_count * sizeof(uint32_t));

  if (tree.tree_type == BTREE && !parent->is_leaf) {
    COVER("unknown_109");

    uint8_t *parent_records = get_internal_record_data(tree, parent);
    memcpy(parent_records + node_index * tree.record_size,
           parent_records + (node_index + 1) * tree.record_size,
           shift_count * tree.record_size);
  }

  parent->num_keys--;

  bp_destroy_node(right_sibling);

  if (parent->num_keys < bp_get_min_keys(tree, parent)) {
    COVER("unknown_110");

    if (parent->parent == 0 && parent->num_keys == 0) {
      COVER("unknown_111");
      tree.root_page_index = node->index;
      bp_set_parent(node, 0);
      bp_destroy_node(parent);
      return node;
    } else {

      bp_repair_after_delete(tree, parent);
    }
  }

  return node;
}

BPTreeNode *bp_steal_from_left(BPlusTree &tree, BPTreeNode *node,
                               uint32_t parent_index) {
  COVER("unknown_112");

  BPTreeNode *parent_node = bp_get_parent(node);
  BPTreeNode *left_sibling = bp_get_child(tree, parent_node, parent_index - 1);

  bp_mark_dirty(node);
  bp_mark_dirty(parent_node);
  bp_mark_dirty(left_sibling);

  memcpy(get_key_at(tree, node, 1), get_key_at(tree, node, 0),
         tree.node_key_size * node->num_keys);

  if (node->is_leaf) {
    COVER("unknown_113");
    memcpy(get_key_at(tree, node, 0),
           get_key_at(tree, left_sibling, left_sibling->num_keys - 1),
           tree.node_key_size);

    uint8_t *node_records = get_leaf_record_data(tree, node);
    uint8_t *sibling_records = get_leaf_record_data(tree, left_sibling);

    memcpy(node_records + tree.record_size, node_records,
           node->num_keys * tree.record_size);

    memcpy(node_records,
           sibling_records + (left_sibling->num_keys - 1) * tree.record_size,
           tree.record_size);

    memcpy(get_key_at(tree, parent_node, parent_index - 1),
           get_key_at(tree, node, 0), tree.node_key_size);
  } else {
    memcpy(get_key_at(tree, node, 0),
           get_key_at(tree, parent_node, parent_index - 1), tree.node_key_size);

    if (tree.tree_type == BTREE) {
      COVER("unknown_114");
      uint8_t *node_records = get_internal_record_data(tree, node);
      uint8_t *parent_records = get_internal_record_data(tree, parent_node);
      uint8_t *sibling_records = get_internal_record_data(tree, left_sibling);

      memcpy(node_records + tree.record_size, node_records,
             node->num_keys * tree.record_size);

      memcpy(node_records,
             parent_records + (parent_index - 1) * tree.record_size,
             tree.record_size);

      memcpy(get_key_at(tree, parent_node, parent_index - 1),
             get_key_at(tree, left_sibling, left_sibling->num_keys - 1),
             tree.node_key_size);
      memcpy(parent_records + (parent_index - 1) * tree.record_size,
             sibling_records + (left_sibling->num_keys - 1) * tree.record_size,
             tree.record_size);
    } else {

      memcpy(get_key_at(tree, parent_node, parent_index - 1),
             get_key_at(tree, left_sibling, left_sibling->num_keys - 1),
             tree.node_key_size);
    }

    uint32_t *node_children = get_children(tree, node);
    uint32_t *sibling_children = get_children(tree, left_sibling);

    for (uint32_t i = node->num_keys + 1; i > 0; i--) {
      bp_set_child(tree, node, i, node_children[i - 1]);
    }
    bp_set_child(tree, node, 0, sibling_children[left_sibling->num_keys]);
  }

  node->num_keys++;
  left_sibling->num_keys--;

  return parent_node;
}

void bp_repair_after_delete(BPlusTree &tree, BPTreeNode *node) {
  COVER("bp_repair_after_delete_115");

  if (node->num_keys >= bp_get_min_keys(tree, node)) {
    COVER("bp_repair_after_delete_116");
    return;
  }

  if (node->parent == 0) {
    COVER("bp_repair_after_delete_117");

    if (node->num_keys == 0 && !node->is_leaf) {
      COVER("bp_repair_after_delete_118");
      BPTreeNode *only_child = bp_get_child(tree, node, 0);
      if (only_child) {
        COVER("bp_repair_after_delete_119");

        tree.root_page_index = only_child->index;
        bp_set_parent(only_child, 0);
        bp_destroy_node(node);
      }
    }
    return;
  }

  BPTreeNode *parent = bp_get_parent(node);
  uint32_t *parent_children = get_children(tree, parent);

  uint32_t node_index = 0;
  for (; node_index <= parent->num_keys; node_index++) {
    if (parent_children[node_index] == node->index)
      break;
  }

  if (node_index > 0) {
    COVER("bp_repair_after_delete_120");

    BPTreeNode *left_sibling = bp_get_child(tree, parent, node_index - 1);
    if (left_sibling &&
        left_sibling->num_keys > bp_get_min_keys(tree, left_sibling)) {
      COVER("bp_repair_after_delete_121");
      bp_steal_from_left(tree, node, node_index);
      return;
    }
  }

  if (node_index < parent->num_keys) {
    COVER("bp_repair_after_delete_122");

    BPTreeNode *right_sibling = bp_get_child(tree, parent, node_index + 1);
    if (right_sibling &&
        right_sibling->num_keys > bp_get_min_keys(tree, right_sibling)) {
      COVER("bp_repair_after_delete_123");
      bp_steal_from_right(tree, node, node_index);
      return;
    }
  }

  if (node_index < parent->num_keys) {
    COVER("bp_repair_after_delete_124");
    bp_merge_right(tree, node);
  } else if (node_index > 0) {
    COVER("bp_repair_after_delete_125");

    BPTreeNode *left_sibling = bp_get_child(tree, parent, node_index - 1);
    if (left_sibling) {
      COVER("bp_repair_after_delete_126");

      bp_merge_right(tree, left_sibling);
    }
  } else {

  }
}


void bp_delete_element(BPlusTree &tree, void *key) {
  COVER("bp_delete_element_127");

  BPTreeNode *root = bp_get_root(tree);
  if (!root)
    return;

  /* 0 or 1 key in a root && leaf node is a special case
   */
  if (root->num_keys <= 1 && root->is_leaf) {
    COVER("bp_delete_element_128");
    bp_mark_dirty(root);
    root->num_keys = 0;
    return;
  }

  bp_do_delete(tree, root, (uint8_t *)key);

  BPTreeNode *leaf = bp_find_containing_node(tree, root, (uint8_t *)key);

  if (root->num_keys == 0 && !root->is_leaf) {
    COVER("bp_delete_element_129");
    BPTreeNode *old_root = root;
    BPTreeNode *new_root = bp_get_child(tree, root, 0);
    if (new_root) {
      COVER("bp_delete_element_130");

      tree.root_page_index = new_root->index;
      bp_set_parent(new_root, 0);
    }
    bp_destroy_node(old_root);
  }

  pager_sync();
}




/*------------- PRINTING -------------- */
// Similar small modifications needed for:
// - bp_steal_from_right (mirror of above)
// - bp_merge_right (needs to handle internal records for B-tree)

// B-tree modifications for steal_from_right and merge operations

void print_uint8_as_chars(const uint8_t *data, size_t size) {
  COVER("print_uint8_as_chars_131");

  for (size_t i = 0; i < size; ++i) {
    printf("%c", data[i]);
  }
  printf("\n");
}

void bp_print_node(BPlusTree &tree, BPTreeNode *node) {
  COVER("bp_print_node_132");

  if (!node) {
    COVER("bp_print_node_133");

    std::cout << "NULL node" << std::endl;
    return;
  }

  // Print basic node information
  std::cout << "=== NODE " << node->index << " ===" << std::endl;

  std::cout << "Node Type: ";
  if (node->is_leaf) {
    COVER("bp_print_node_134");

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
    COVER("bp_print_node_135");

    std::cout << "/" << tree.leaf_max_keys << " (min: " << tree.leaf_min_keys
              << ")";
  } else {
    std::cout << "/" << tree.internal_max_keys
              << " (min: " << tree.internal_min_keys << ")";
  }
  std::cout << std::endl;

  // Print sibling links for leaf nodes
  if (node->is_leaf) {
    COVER("bp_print_node_136");

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
      COVER("bp_print_node_137");

      std::cout << *reinterpret_cast<const uint32_t *>(
                       get_key_at(tree, node, i))
                << ",";
    } else if (tree.node_key_size == TYPE_INT64) {
      COVER("bp_print_node_138");

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
    COVER("bp_print_node_139");

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
    COVER("bp_print_node_140");

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

void print_tree(BPlusTree &tree) {
  COVER("print_tree_141");

  BPTreeNode *root = bp_get_root(tree);
  if (!root) {
    COVER("print_tree_142");

    std::cout << "Tree is empty" << std::endl;
    return;
  }

  std::queue<BPTreeNode *> to_visit;
  to_visit.push(root);

  while (!to_visit.empty()) {
    // Get the number of nodes at the current level
    size_t level_size = to_visit.size();

    // Print all nodes at the current level
    for (size_t i = 0; i < level_size; i++) {
      BPTreeNode *node = to_visit.front();
      to_visit.pop();

      // Print the current node using the existing bp_print_node function
      bp_print_node(tree, node);

      // Add children to the queue if the node is not a leaf
      if (!node->is_leaf) {
        COVER("print_tree_143");

        uint32_t *children = get_children(tree, node);
        for (uint32_t j = 0; j <= node->num_keys; j++) {
          if (children[j] != 0) {
            COVER("print_tree_144");

            BPTreeNode *child = bp_get_child(tree, node, j);
            if (child) {
              COVER("print_tree_145");

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
  COVER("debug_hash_tree_146");

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
      COVER("debug_hash_tree_147");

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
          COVER("debug_hash_tree_148");

          BPTreeNode *child = bp_get_child(tree, node, i);
          if (child) {
            COVER("debug_hash_tree_149");

            hash_node(child, depth + 1);
          }
        }
      }
    }
  };

  // Hash the tree starting from root
  if (tree.root_page_index != 0) {
    COVER("debug_hash_tree_150");

    BPTreeNode *root = bp_get_root(tree);
    if (root) {
      COVER("debug_hash_tree_151");

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
  COVER("validate_key_separation_152");

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
      COVER("validate_key_separation_153");

      uint8_t *upper_separator = get_key_at(tree, node, i);
      for (uint32_t j = 0; j < child->num_keys; j++) {
        if (tree.tree_type == BTREE) {
          COVER("validate_key_separation_154");

          if (cmp(tree, get_key_at(tree, child, j), upper_separator) > 0) {
            COVER("validate_key_separation_155");

            std::cerr << "INVARIANT VIOLATION: Key in child " << child->index
                      << " violates upper bound from parent " << node->index
                      << std::endl;
            return false;
          }
        } else {
          if (cmp(tree, get_key_at(tree, child, j), upper_separator) >= 0) {
            COVER("validate_key_separation_156");

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
      COVER("validate_key_separation_157");

      uint8_t *lower_separator = get_key_at(tree, node, i - 1);
      for (uint32_t j = 0; j < child->num_keys; j++) {
        if (cmp(tree, get_key_at(tree, child, j), lower_separator) < 0) {
          COVER("validate_key_separation_158");

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
  COVER("validate_leaf_links_159");

  BPTreeNode *current = bp_left_most(tree);
  BPTreeNode *prev = nullptr;

  while (current) {
    if (!current->is_leaf) {
      COVER("validate_leaf_links_160");

      std::cerr << "INVARIANT VIOLATION: Non-leaf node " << current->index
                << " found in leaf traversal" << std::endl;
      return false;
    }

    // Check backward link
    if (prev && current->previous != (prev ? prev->index : 0)) {
      COVER("validate_leaf_links_161");

      std::cerr << "INVARIANT VIOLATION: Leaf " << current->index
                << " has previous=" << current->previous << " but should be "
                << (prev ? prev->index : 0) << std::endl;
      return false;
    }

    // Check forward link consistency
    if (prev && prev->next != current->index) {
      COVER("validate_leaf_links_162");

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
  COVER("validate_tree_height_163");

  if (!node)
    return true;

  if (node->is_leaf) {
    COVER("validate_tree_height_164");
    if (current_height != expected_height) {
      COVER("validate_tree_height_165");

      std::cerr << "INVARIANT VIOLATION: Leaf " << node->index << " at height "
                << current_height << " but expected height " << expected_height
                << std::endl;
      return false;
    }
  } else {
    for (uint32_t i = 0; i <= node->num_keys; i++) {
      BPTreeNode *child = bp_get_child(tree, node, i);
      if (child) {
        COVER("validate_tree_height_166");

        if (!validate_tree_height(tree, child, expected_height,
                                  current_height + 1)) {
          COVER("validate_tree_height_167");

          return false;
        }
      }
    }
  }
  return true;
}

static bool validate_bplus_leaf_node(BPlusTree &tree, BPTreeNode *node) {
  COVER("validate_bplus_leaf_node_168");

  if (!node) {
    COVER("validate_bplus_leaf_node_169");

    std::cout << "// Node pointer is null" << std::endl;
    return false;
    ;
  }

  // Must be marked as leaf
  if (!node->is_leaf) {
    COVER("validate_bplus_leaf_node_170");

    std::cout << "// Node is not marked as leaf (is_leaf = 0)" << std::endl;
    return false;
    ;
  }

  // Key count must be within bounds
  uint32_t min_keys = (node->parent == 0)
                          ? 0
                          : tree.leaf_min_keys; // Root can have as few as 1 key
  if (node->num_keys < min_keys) {
    COVER("validate_bplus_leaf_node_171");

    bp_print_node(tree, node);
    bp_print_node(tree, bp_get_parent(node));
    std::cout << node << "// Leaf node has too few keys: " << node->num_keys
              << " < " << min_keys << std::endl;
    return false;
    ;
  }

  if (node->num_keys > tree.leaf_max_keys) {
    COVER("validate_bplus_leaf_node_172");

    std::cout << "// Leaf node has too many keys: " << node->num_keys << " > "
              << tree.leaf_max_keys << std::endl;
    return false;
    ;
  }

  // Keys must be sorted in ascending order
  for (uint32_t i = 1; i < node->num_keys; i++) {
    if (cmp(tree, get_key_at(tree, node, i), get_key_at(tree, node, i - 1)) <=
        0) {
      COVER("validate_bplus_leaf_node_173");

      std::cout << "// Leaf keys not in ascending order at positions " << i - 1
                << " and " << i << std::endl;
      return false;
      ;
    }
  }

  // If this is not the root, validate parent relationship
  if (node->parent != 0) {
    COVER("validate_bplus_leaf_node_174");

    BPTreeNode *parent = static_cast<BPTreeNode *>(pager_get(node->parent));
    if (!parent) {
      COVER("validate_bplus_leaf_node_175");

      std::cout << "// Cannot access parent node at page " << node->parent
                << std::endl;
      return false;
      ;
    }

    if (parent->is_leaf) {
      COVER("validate_bplus_leaf_node_176");

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
        COVER("validate_bplus_leaf_node_177");

        found_in_parent = true;
        break;
      }
    }
    if (!found_in_parent) {
      COVER("validate_bplus_leaf_node_178");

      std::cout << "// Node not found in parent's children array" << std::endl;
      return false;
      ;
    }
  }

  // Validate sibling links if they exist
  if (node->next != 0) {
    COVER("validate_bplus_leaf_node_179");

    BPTreeNode *next_node = static_cast<BPTreeNode *>(pager_get(node->next));
    if (next_node && next_node->previous != node->index) {
      COVER("validate_bplus_leaf_node_180");

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
    COVER("validate_bplus_leaf_node_181");

    BPTreeNode *prev_node =
        static_cast<BPTreeNode *>(pager_get(node->previous));
    if (prev_node && prev_node->next != node->index) {
      COVER("validate_bplus_leaf_node_182");

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
  COVER("validate_bplus_internal_node_183");

  if (!node) {
    COVER("validate_bplus_internal_node_184");

    std::cout << "// Node pointer is null" << std::endl;
    return false;
    ;
  }

  // Must not be marked as leaf
  if (node->is_leaf) {
    COVER("validate_bplus_internal_node_185");

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
    COVER("validate_bplus_internal_node_186");
    std::cout << "// Internal node has too few keys: " << node->num_keys
              << " < " << min_keys << std::endl;
    return false;
    ;
  }

  if (node->num_keys > tree.internal_max_keys) {
    COVER("validate_bplus_internal_node_187");

    std::cout << "// Internal node has too many keys: " << node->num_keys
              << " > " << tree.internal_max_keys << std::endl;
    return false;
    ;
  }

  // Keys must be sorted in ascending order
  for (uint32_t i = 1; i < node->num_keys; i++) {
    if (cmp(tree, get_key_at(tree, node, i), get_key_at(tree, node, i - 1)) <=
        0) {
      COVER("validate_bplus_internal_node_188");

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
      COVER("validate_bplus_internal_node_189");

      std::cout << "// Internal node missing child at index " << i << std::endl;
      return false;
      ;
    }

    // Verify child exists and points back to this node as parent
    BPTreeNode *child = static_cast<BPTreeNode *>(pager_get(children[i]));
    if (!child) {
      COVER("validate_bplus_internal_node_190");

      std::cout << "// Cannot access child node at page " << children[i]
                << std::endl;
      return false;
      ;
    }

    if (child->parent != node->index) {
      COVER("validate_bplus_internal_node_191");
      std::cout << "// Child node's parent pointer does not point to this node"
                << std::endl;
      return false;
      ;
    }

    // Check no self-reference
    if (children[i] == node->index) {
      COVER("validate_bplus_internal_node_192");

      std::cout << "// Node references itself as child" << std::endl;
      return false;
      ;
    }
  }

  // Internal nodes should not have next/previous pointers (only leaves do)
  if (node->next != 0 || node->previous != 0) {
    COVER("validate_bplus_internal_node_193");

    std::cout << "// Internal node has sibling pointers (next=" << node->next
              << ", prev=" << node->previous << "), but only leaves should"
              << std::endl;
    return false;
    ;
  }

  // If this is not the root, validate parent relationship
  if (node->parent != 0) {
    COVER("validate_bplus_internal_node_194");

    BPTreeNode *parent = static_cast<BPTreeNode *>(pager_get(node->parent));
    if (!parent) {
      COVER("validate_bplus_internal_node_195");

      std::cout << "// Cannot access parent node at page " << node->parent
                << std::endl;
      return false;
      ;
    }

    if (parent->is_leaf) {
      COVER("validate_bplus_internal_node_196");

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
        COVER("validate_bplus_internal_node_197");

        found_in_parent = true;
        break;
      }
    }
    if (!found_in_parent) {
      COVER("validate_bplus_internal_node_198");

      std::cout << "// Node not found in parent's children array" << std::endl;
      return false;
      ;
    }
  }
  return true;
}

static bool validate_btree_node(BPlusTree &tree, BPTreeNode *node) {
  COVER("validate_btree_node_199");

  if (!node) {
    COVER("validate_btree_node_200");

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
    COVER("validate_btree_node_201");

    std::cout << "// B-tree node has too few keys: " << node->num_keys << " < "
              << min_keys << std::endl;
    return false;
    ;
  }

  if (node->num_keys > max_keys) {
    COVER("validate_btree_node_202");

    std::cout << "// B-tree node has too many keys: " << node->num_keys << " > "
              << max_keys << std::endl;
    return false;
    ;
  }

  // Keys must be sorted in ascending order
  for (uint32_t i = 1; i < node->num_keys; i++) {
    if (cmp(tree, get_key_at(tree, node, i), get_key_at(tree, node, i - 1)) <
        0) {
      COVER("validate_btree_node_203");

      std::cout << "// B-tree keys not in ascending order at positions "
                << i - 1 << " and " << i << std::endl;
      return false;
      ;
    }
  }

  // If internal node, validate children
  if (!node->is_leaf) {
    COVER("validate_btree_node_204");

    uint32_t *children = get_children(tree, node);
    for (uint32_t i = 0; i <= node->num_keys; i++) {
      if (children[i] == 0) {
        COVER("validate_btree_node_205");

        std::cout << "// B-tree internal node missing child at index " << i
                  << std::endl;
        return false;
        ;
      }

      // Verify child exists and points back to this node as parent
      BPTreeNode *child = static_cast<BPTreeNode *>(pager_get(children[i]));
      if (!child) {
        COVER("validate_btree_node_206");

        std::cout << "// Cannot access child node at page " << children[i]
                  << std::endl;
        return false;
        ;
      }

      if (child->parent != node->index) {
        COVER("validate_btree_node_207");

        std::cout
            << "// Child node's parent pointer does not point to this node"
            << std::endl;
        return false;
        ;
      }

      // Check no self-reference
      if (children[i] == node->index) {
        COVER("validate_btree_node_208");

        std::cout << "// Node references itself as child" << std::endl;
        return false;
        ;
      }
    }

    // Internal nodes in B-tree should not have next/previous pointers
    if (node->next != 0 || node->previous != 0) {
      COVER("validate_btree_node_209");

      std::cout
          << "// B-tree internal node has sibling pointers, but should not"
          << std::endl;
      return false;
      ;
    }
  }

  // If this is not the root, validate parent relationship
  if (node->parent != 0) {
    COVER("validate_btree_node_210");

    BPTreeNode *parent = static_cast<BPTreeNode *>(pager_get(node->parent));
    if (!parent) {
      COVER("validate_btree_node_211");

      std::cout << "// Cannot access parent node at page " << node->parent
                << std::endl;
      return false;
      ;
    }

    if (parent->is_leaf) {
      COVER("validate_btree_node_212");

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
        COVER("validate_btree_node_213");

        found_in_parent = true;
        break;
      }
    }
    if (!found_in_parent) {
      COVER("validate_btree_node_214");

      std::cout << "// Node not found in parent's children array" << std::endl;
      return false;
      ;
    }
  }
  return true;
}

bool bp_validate_all_invariants(BPlusTree &tree) {
  COVER("bp_validate_all_invariants_215");

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
      COVER("bp_validate_all_invariants_216");

      if (!validate_btree_node(tree, node)) {
        COVER("bp_validate_all_invariants_217");

        return false;
      }
    } else {
      if (node->is_leaf) {
        COVER("bp_validate_all_invariants_218");

        if (!validate_bplus_leaf_node(tree, node)) {
          COVER("bp_validate_all_invariants_219");

          return false;
        }
      } else {
        if (!validate_bplus_internal_node(tree, node)) {
          COVER("bp_validate_all_invariants_220");

          return false;
        }
      }
    }

    if (!node->is_leaf) {
      COVER("bp_validate_all_invariants_221");

      for (uint32_t i = 0; i <= node->num_keys; i++) {
        BPTreeNode *child = bp_get_child(tree, node, i);
        if (child) {
          COVER("bp_validate_all_invariants_222");

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
