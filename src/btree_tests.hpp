#pragma once
#include "bplustree.hpp"
#include "defs.hpp"
#include "pager.hpp"
#include "test_utils.hpp"
#include "vm.hpp"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <ios>
#include <set>
#include <unordered_map>
#include <vector>
#include <iostream>

#define TEST_DB "test_btree.db"

inline void
test_btree()
{
	// test_btree_stress();
	pager_open(TEST_DB);
	pager_begin_transaction();

	bool print = false;

	// Create B-tree for uint32_t keys and uint32_t values
	BPlusTree bptree = bplustree_create(TYPE_4, TYPE_256, true);
	BPtCursor cursor = {.tree = &bptree};

	int count = 30;
	for (int i = 0; i < count; i++)
	{
		bplustree_cursor_insert(&cursor, &i, (uint8_t *)&i);
		bplustree_validate(&bptree);
	}

	for (int i = 0; i < count; i++)
	{
		assert(bplustree_cursor_seek(&cursor, &i));
	}

	for (int i = 0; i < count; i++)
	{
		bplustree_cursor_seek(&cursor, &i);
		if (i == 16)
		{
			_debug = true;
			bplustree_print(&bptree);
		}
		else
		{
			_debug = false;

		}
		bplustree_cursor_delete(&cursor);
		if (print)
		{
		}
		bplustree_validate(&bptree);
	}

	for (int i = 0; i < count; i++)
	{

		assert(!bplustree_cursor_seek(&cursor, &i));
	}

	std::cout << "Btree tests passed\n";
}
