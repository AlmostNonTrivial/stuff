#pragma once
#include "bplustree.hpp"
#include "btree.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include "test_utils.hpp"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <ios>
#include <set>
#include <unordered_map>
#include <vector>
#include <iostream>

#define TEST_DB "test_btree.db"

// Recursive B-tree invariant validator
struct BTreeValidator
{
	BTree					&tree;
	std::set<uint32_t>		 visited_pages;
	std::vector<std::string> errors;

	struct NodeInfo
	{
		uint32_t depth;
		uint32_t min_key;
		uint32_t max_key;
		bool	 has_min;
		bool	 has_max;
	};

	NodeInfo
	validate_node(uint32_t page_index, uint32_t expected_parent, uint32_t depth, uint32_t min_bound, uint32_t max_bound,
				  bool check_min, bool check_max)
	{

		// Check for cycles
		if (visited_pages.count(page_index))
		{
			errors.push_back("Cycle detected at page " + std::to_string(page_index));
			return {depth, 0, 0, false, false};
		}
		visited_pages.insert(page_index);

		BTreeNode *node = reinterpret_cast<BTreeNode *>(pager_get(page_index));
		if (!node)
		{
			errors.push_back("Invalid page " + std::to_string(page_index));
			return {depth, 0, 0, false, false};
		}

		// Verify parent pointer
		if (node->parent != expected_parent)
		{
			errors.push_back("Parent mismatch at page " + std::to_string(page_index) + ": expected " +
							 std::to_string(expected_parent) + ", got " + std::to_string(node->parent));
		}

		// Check occupancy constraints (except root)
		if (expected_parent != 0)
		{
			if (node->num_keys < tree.min_keys)
			{
				errors.push_back("Underflow at page " + std::to_string(page_index) + ": has " +
								 std::to_string(node->num_keys) + " keys, min is " + std::to_string(tree.min_keys));
			}
		}

		if (node->num_keys > tree.max_keys)
		{
			errors.push_back("Overflow at page " + std::to_string(page_index) + ": has " +
							 std::to_string(node->num_keys) + " keys, max is " + std::to_string(tree.max_keys));
		}

		if (node->num_keys == 0 && expected_parent != 0)
		{
			errors.push_back("Non-root empty node at page " + std::to_string(page_index));
		}

		// Check key ordering and bounds (allowing duplicates)
		uint32_t prev_key = 0;
		bool	 has_prev = false;
		uint32_t actual_min = 0, actual_max = 0;
		bool	 has_actual_min = false, has_actual_max = false;

		for (uint32_t i = 0; i < node->num_keys; i++)
		{
			uint32_t *key_ptr = reinterpret_cast<uint32_t *>(node->data + i * tree.node_key_size);
			uint32_t  key = *key_ptr;

			// Track actual min/max
			if (!has_actual_min || key < actual_min)
			{
				actual_min = key;
				has_actual_min = true;
			}
			if (!has_actual_max || key > actual_max)
			{
				actual_max = key;
				has_actual_max = true;
			}

			// Check ordering (allow duplicates, so < not <=)
			if (has_prev && key < prev_key)
			{
				errors.push_back("Keys not sorted at page " + std::to_string(page_index) + ", position " +
								 std::to_string(i));
			}

			// Check bounds
			if (check_min && key < min_bound)
			{
				errors.push_back("Key " + std::to_string(key) + " violates min bound " + std::to_string(min_bound) +
								 " at page " + std::to_string(page_index));
			}
			if (check_max && key >= max_bound)
			{
				errors.push_back("Key " + std::to_string(key) + " violates max bound " + std::to_string(max_bound) +
								 " at page " + std::to_string(page_index));
			}

			prev_key = key;
			has_prev = true;
		}

		// Validate children if internal node
		uint32_t child_depth = 0;
		if (!node->is_leaf)
		{
			uint32_t *children = reinterpret_cast<uint32_t *>(node->data + tree.max_keys * tree.node_key_size +
															  tree.max_keys * tree.record_size);

			for (uint32_t i = 0; i <= node->num_keys; i++)
			{
				if (children[i] == 0)
				{
					errors.push_back("Null child pointer at page " + std::to_string(page_index) + ", position " +
									 std::to_string(i));
					continue;
				}

				// Determine bounds for this child
				uint32_t child_min =
					(i == 0) ? min_bound : *reinterpret_cast<uint32_t *>(node->data + (i - 1) * tree.node_key_size);
				uint32_t child_max = (i == node->num_keys)
										 ? max_bound
										 : *reinterpret_cast<uint32_t *>(node->data + i * tree.node_key_size);
				bool	 check_child_min = (i != 0) || check_min;
				bool	 check_child_max = (i != node->num_keys) || check_max;

				NodeInfo child_info = validate_node(children[i], page_index, depth + 1, child_min, child_max,
													check_child_min, check_child_max);

				// All children must have same depth
				if (i == 0)
				{
					child_depth = child_info.depth;
				}
				else if (child_info.depth != child_depth)
				{
					errors.push_back("Inconsistent child depths at page " + std::to_string(page_index));
				}
			}
		}

		return {node->is_leaf ? depth : child_depth, actual_min, actual_max, has_actual_min, has_actual_max};
	}

	bool
	validate()
	{
		errors.clear();
		visited_pages.clear();

		if (tree.root_page_index == 0)
		{
			errors.push_back("Tree has no root");
			return false;
		}

		validate_node(tree.root_page_index, 0, 0, 0, UINT32_MAX, false, false);

		if (!errors.empty())
		{
			std::cout << "B-Tree validation failed with " << errors.size() << " errors:\n";
			for (const auto &err : errors)
			{
				std::cout << "  - " << err << "\n";
			}
			return false;
		}

		return true;
	}
};

inline void
test_btree_stress()
{
	std::srand(42);
	os_file_delete(TEST_DB);

	pager_open(TEST_DB);
	pager_begin_transaction();

	// Create B-tree for uint32_t keys and uint32_t values
	BTree		   tree = btree_create(TYPE_4, TYPE_4, true);
	BTreeValidator validator{tree};

	// Track key counts (for duplicates)
	std::unordered_map<uint32_t, uint32_t> reference_counts;

	const int iterations = 1000;
	const int max_key = 100; // Lower to increase duplicate likelihood

	std::cout << "Starting B-tree stress test with " << iterations << " operations\n";

	for (int i = 0; i < iterations; i++)
	{
		int op = std::rand() % 100;

		if (op < 60 || reference_counts.empty())
		{
			// 60% insert (or force insert if empty)
			uint32_t key = std::rand() % max_key;
			uint32_t value = std::rand(); // Random value since duplicates allowed

			BtCursor cursor = {&tree, nullptr, {}, {}, CURSOR_INVALID};
			btree_cursor_insert(&cursor, &key, reinterpret_cast<uint8_t *>(&value));
			reference_counts[key]++;

			if (i % 100 == 0)
			{
				uint32_t total = 0;
				for (auto &p : reference_counts)
					total += p.second;
				std::cout << "After insert " << i << ": tree has " << total << " total entries, "
						  << reference_counts.size() << " unique keys\n";
			}
		}
		else if (op < 70)
		{
			// 30% delete
			if (!reference_counts.empty())
			{
				// Pick random key to delete
				auto it = reference_counts.begin();
				std::advance(it, std::rand() % reference_counts.size());
				uint32_t key = it->first;

				BtCursor cursor = {&tree, nullptr, {}, {}, CURSOR_INVALID};
				if (btree_cursor_seek(&cursor, &key))
				{
					btree_cursor_delete(&cursor);
					it->second--;
					if (it->second == 0)
					{
						reference_counts.erase(it);
					}
				}

				if (i % 100 == 0)
				{
					uint32_t total = 0;
					for (auto &p : reference_counts)
						total += p.second;
					std::cout << "After delete " << i << ": tree has " << total << " total entries, "
							  << reference_counts.size() << " unique keys\n";
				}
			}
		}
		else
		{
			// 10% lookup verification
			if (!reference_counts.empty())
			{
				auto it = reference_counts.begin();
				std::advance(it, std::rand() % reference_counts.size());
				uint32_t key = it->first;

				BtCursor cursor = {&tree, nullptr, {}, {}, CURSOR_INVALID};
				if (!btree_cursor_seek(&cursor, &key))
				{
					std::cout << "ERROR: Key " << key << " not found but should exist\n";
					assert(false);
				}
			}
		}

		// Validate tree structure periodically
		if (i % 50 == 0)
		{
			if (!validator.validate())
			{
				std::cout << "Tree validation failed at iteration " << i << "\n";
				assert(false);
			}
		}
	}

	// Final validation
	std::cout << "Final validation...\n";
	if (!validator.validate())
	{
		std::cout << "Final tree validation failed\n";
		assert(false);
	}

	// Count all entries in tree
	std::cout << "Counting tree entries...\n";
	std::unordered_map<uint32_t, uint32_t> tree_counts;
	BtCursor							   cursor = {&tree, nullptr, {}, {}, CURSOR_INVALID};
	uint32_t							   total_entries = 0;

	if (btree_cursor_first(&cursor))
	{
		do
		{
			uint32_t *key = reinterpret_cast<uint32_t *>(btree_cursor_key(&cursor));
			tree_counts[*key]++;
			total_entries++;
		} while (btree_cursor_next(&cursor));
	}

	// Verify counts match
	uint32_t expected_total = 0;
	for (auto &p : reference_counts)
		expected_total += p.second;

	if (total_entries != expected_total)
	{
		std::cout << "ERROR: Tree has " << total_entries << " entries, expected " << expected_total << "\n";
		assert(false);
	}

	// Verify each key has correct count
	for (auto &[key, count] : reference_counts)
	{
		if (tree_counts[key] != count)
		{
			std::cout << "ERROR: Key " << key << " has " << tree_counts[key] << " instances, expected " << count
					  << "\n";
			assert(false);
		}
	}

	// Check no extra keys
	for (auto &[key, count] : tree_counts)
	{
		if (reference_counts.find(key) == reference_counts.end())
		{
			std::cout << "ERROR: Unexpected key " << key << " in tree\n";
			assert(false);
		}
	}

	std::cout << "Final tree has " << total_entries << " total entries with " << reference_counts.size()
			  << " unique keys\n";

	pager_commit();
	pager_close();
	os_file_delete(TEST_DB);

	std::cout << "B-tree stress test passed!\n";
}

inline void
test_btree()
{
	// test_btree_stress();
	pager_open(TEST_DB);
	pager_begin_transaction();

	// Create B-tree for uint32_t keys and uint32_t values
	BPlusTree bptree = bplustree_create(TYPE_4, TYPE_8, true);
	BTree	  btree = btree_create(TYPE_4, TYPE_4, true);
	BPtCursor pcursor;
	BtCursor  cursor;
	pcursor.tree = &bptree;
	cursor.tree = &btree;

	int count = bptree.leaf_max_keys;
	std::cout << count << '\n';
	;
	for (int i = 0; i < count; i++)
	{
		bplustree_cursor_insert(&pcursor, (uint8_t *)&i, (uint8_t *)&i);
		btree_cursor_insert(&cursor, (uint8_t *)&i, (uint8_t *)&i);
	}

	bplustree_cursor_first(&pcursor);
	btree_cursor_first(&cursor);

	int i = 0;
	do
	{
		int *key = (int *)bplustree_cursor_key(&pcursor);
		// std::cout << i << ',' << i << '\n';
		assert(*key == i++);

	} while (bplustree_cursor_next(&pcursor));

	i = 0;
	do
	{
		int *key = (int *)btree_cursor_key(&cursor);
		// std::cout << i << ',' << i << '\n';
		assert(*key == i++);

	} while (btree_cursor_next(&cursor));

	bplustree_cursor_first(&pcursor);
	btree_cursor_first(&cursor);

	for (int i = 0; i < count; i++)
	{
		if (i % 2 == 0)
		{
			bplustree_cursor_delete(&pcursor);
		}
		else
		{
			bplustree_cursor_next(&pcursor);
		}
	}

	for (int i = 0; i < count; i++)
	{

	if (i % 2 == 0)
	{
	btree_cursor_delete(&cursor);
	} else {
	btree_cursor_next(&cursor);
	}
	}

	bplustree_cursor_first(&pcursor);
	btree_cursor_first(&cursor);

	// assert(bplustree_cursor_is_valid(&pcursor) != false);
	// assert(btree_cursor_is_valid(&cursor) != false);

	i = 1;
	do
	{
		int *key = (int *)bplustree_cursor_key(&pcursor);
		if (key != nullptr)
		{
			std::cout << *key << ',' << i << '\n';
			// assert(*key == i);
		}

	} while (bplustree_cursor_next(&pcursor));

	i = 1;
	do
	{
		int *key = (int *)btree_cursor_key(&cursor);
		if (key != nullptr)
		{
			std::cout << *key << ',' << i << '\n';
			// assert(*key == i);
		}

	} while (btree_cursor_next(&cursor));
}
