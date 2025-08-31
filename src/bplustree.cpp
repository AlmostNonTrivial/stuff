

#include "bplustree.hpp"
#include "arena.hpp"
#include "defs.hpp"
#include "pager.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <sys/types.h>

// Constants
#define NODE_HEADER_SIZE 24
#define NODE_DATA_SIZE	 PAGE_SIZE - NODE_HEADER_SIZE

// B+Tree node structure -
// fits in a single page
struct BTreeNode
{
	// Node header (24 bytes)
	uint32_t index;		// Page index
	uint32_t parent;	// Parent page
						// index (0 if
						// root)
	uint32_t next;		// Next sibling
						// (for leaf
						// nodes)
	uint32_t previous;	// Previous
						// sibling
						// (for leaf
						// nodes)
	uint32_t num_keys;	// Number of
						// keys in
						// this node
	uint8_t is_leaf;	// 1 if leaf, 0
						// if internal
	uint8_t padding[3]; // Alignment
						// padding
	// Data area - stores keys,
	// children pointers, and
	// data Layout for internal
	// nodes: [keys][children]
	// Layout for leaf nodes:
	// [keys][records]
	uint8_t data[NODE_DATA_SIZE]; // Rest
								  // of
								  // the
								  // page
								  // (4064
								  // bytes)
};

static_assert(sizeof(BTreeNode) == PAGE_SIZE, "BTreeNode must be "
											  "exactly PAGE_SIZE");

// Forward declarations
struct FindResult
{
	BTreeNode *node;
	uint32_t   index;
	bool	   found;
};

// Internal function
// declarations
static void
repair_after_delete(BPlusTree &tree, BTreeNode *node);

// Helper functions
static uint32_t
get_max_keys(BPlusTree &tree, BTreeNode *node)
{
	return node->is_leaf ? tree.leaf_max_keys : tree.internal_max_keys;
}

static uint32_t
get_min_keys(BPlusTree &tree, BTreeNode *node)
{
	return node->is_leaf ? tree.leaf_min_keys : tree.internal_min_keys;
}

static uint32_t
get_split_index(BPlusTree &tree, BTreeNode *node)
{
	return node->is_leaf ? tree.leaf_split_index : tree.internal_split_index;
}

static BTreeNode *
get_root(BPlusTree &tree)
{
	return reinterpret_cast<BTreeNode *>(pager_get(tree.root_page_index));
}

static void
mark_dirty(BTreeNode *node)
{
	pager_mark_dirty(node->index);
}

static uint8_t *
get_keys(BTreeNode *node)
{
	return node->data;
}

static uint8_t *
get_key_at(BPlusTree &tree, BTreeNode *node, uint32_t index)
{
	return get_keys(node) + index * tree.node_key_size;
}

static uint32_t *
get_children(BPlusTree &tree, BTreeNode *node)
{
	if (node->is_leaf)
	{
		return nullptr;
	}

	return reinterpret_cast<uint32_t *>(node->data + tree.internal_max_keys * tree.node_key_size);
}

static uint8_t *
get_internal_record_data(BPlusTree &tree, BTreeNode *node)
{
	return node->data + tree.internal_max_keys * tree.node_key_size;
}

static uint8_t *
get_internal_record_at(BPlusTree &tree, BTreeNode *node, uint32_t index)
{
	return get_internal_record_data(tree, node) + (index * tree.record_size);
}

static uint8_t *
get_leaf_record_data(BPlusTree &tree, BTreeNode *node)
{
	return node->data + tree.leaf_max_keys * tree.node_key_size;
}

static uint8_t *
get_leaf_record_at(BPlusTree &tree, BTreeNode *node, uint32_t index)
{
	return get_leaf_record_data(tree, node) + (index * tree.record_size);
}

static uint8_t *
get_records(BPlusTree &tree, BTreeNode *node)
{
	return node->is_leaf ? get_leaf_record_data(tree, node) : get_internal_record_data(tree, node);
}

static uint8_t *
get_record_at(BPlusTree &tree, BTreeNode *node, uint32_t index)
{
	if (node->is_leaf)
	{
		return get_leaf_record_at(tree, node, index);
	}
	return get_internal_record_at(tree, node, index);
}

static uint32_t
binary_search(BPlusTree &tree, BTreeNode *node, const uint8_t *key)
{
	uint32_t left = 0;
	uint32_t right = node->num_keys;

	while (left < right)
	{
		uint32_t mid = left + (right - left) / 2;
		int		 cmp_result = cmp(tree.node_key_size, get_key_at(tree, node, mid), key);

		if (cmp_result < 0)
		{
			left = mid + 1;
		}
		else if (cmp_result == 0)
		{
			if (node->is_leaf)
			{
				// No duplicates, this is the only instance
				return mid;
			}
			return mid + 1;
		}
		else
		{
			right = mid;
		}
	}

	return left;
}

static BTreeNode *
create_node(BPlusTree &tree, bool is_leaf)
{
	uint32_t page_index = pager_new();
	assert(page_index != PAGE_INVALID);
	BTreeNode *node = reinterpret_cast<BTreeNode *>(pager_get(page_index));

	node->index = page_index;
	node->parent = 0;
	node->next = 0;
	node->previous = 0;
	node->num_keys = 0;
	node->is_leaf = is_leaf ? 1 : 0;

	mark_dirty(node);
	return node;
}

BPlusTree
bplustree_create(DataType key, uint32_t record_size, bool init = false)
{
	BPlusTree tree = {0};
	tree.node_key_size = key;

	tree.record_size = record_size;

	constexpr uint32_t USABLE_SPACE = PAGE_SIZE - NODE_HEADER_SIZE;

	if ((record_size * MIN_ENTRY_COUNT) > USABLE_SPACE)
	{
		std::cout << "btree record to "
					 "big\n";
		exit(1);
		return tree;
	}

	uint32_t leaf_entry_size = tree.node_key_size + record_size;
	uint32_t leaf_max_entries = USABLE_SPACE / leaf_entry_size;

	tree.leaf_max_keys = std::max((uint32_t)MIN_ENTRY_COUNT, leaf_max_entries);
	tree.leaf_min_keys = tree.leaf_max_keys / 2;
	tree.leaf_split_index = tree.leaf_max_keys / 2;

	uint32_t child_ptr_size = TYPE_4;
	uint32_t internal_max_entries = (USABLE_SPACE - child_ptr_size) / (tree.node_key_size + child_ptr_size);

	if (internal_max_entries % 2 == 0)
	{
		tree.internal_min_keys = (internal_max_entries / 2) - 1;
	}
	else
	{
		tree.internal_min_keys = (internal_max_entries) / 2;
	}

	tree.internal_max_keys = std::max((uint32_t)MIN_ENTRY_COUNT, internal_max_entries);
	tree.internal_split_index = tree.internal_max_keys / 2;

	if (init)
	{
		BTreeNode *root = create_node(tree, true);
		tree.root_page_index = root->index;
	}
	return tree;
}

static void
set_next(BTreeNode *node, uint32_t index)
{
	mark_dirty(node);
	node->next = index;
}

static void
set_prev(BTreeNode *node, uint32_t index)
{
	mark_dirty(node);
	node->previous = index;
}

static BTreeNode *
get_parent(BTreeNode *node)
{
	if (!node || node->parent == 0)
	{
		return nullptr;
	}
	return reinterpret_cast<BTreeNode *>(pager_get(node->parent));
}

static BTreeNode *
get_child(BPlusTree &tree, BTreeNode *node, uint32_t index)
{
	if (!node || node->is_leaf)
	{
		return nullptr;
	}
	uint32_t *children = get_children(tree, node);
	return reinterpret_cast<BTreeNode *>(pager_get(children[index]));
}

static BTreeNode *
get_next(BTreeNode *node)
{
	if (!node || node->next == 0)
	{
		return nullptr;
	}
	return reinterpret_cast<BTreeNode *>(pager_get(node->next));
}

static BTreeNode *
get_prev(BTreeNode *node)
{
	if (!node || node->previous == 0)
	{
		return nullptr;
	}
	return reinterpret_cast<BTreeNode *>(pager_get(node->previous));
}

static void
set_parent(BTreeNode *node, uint32_t parent_index)
{
	mark_dirty(node);
	node->parent = parent_index;

	if (parent_index != 0)
	{
		pager_mark_dirty(parent_index);
	}
}

static void
set_child(BPlusTree &tree, BTreeNode *node, uint32_t child_index, uint32_t node_index)
{
	if (!node || node->is_leaf)
	{
		return;
	}

	mark_dirty(node);
	uint32_t *children = get_children(tree, node);
	children[child_index] = node_index;

	if (node_index != 0)
	{
		BTreeNode *child_node = reinterpret_cast<BTreeNode *>(pager_get(node_index));
		if (child_node)
		{
			set_parent(child_node, node->index);
		}
	}
}
static void
swap_with_root(BPlusTree &tree, BTreeNode *root, BTreeNode *other)
{

	// Fix parent for other node (it's no longer root)
	// The caller will set the correct parent if needed

	mark_dirty(root);
	mark_dirty(other);
	// Verify root is actually the root
	assert(root->index == tree.root_page_index);

	// Swap everything EXCEPT the index field
	uint32_t saved_root_index = root->index;
	uint32_t saved_other_index = other->index;

	// Swap all content
	BTreeNode temp;
	memcpy(&temp, other, sizeof(BTreeNode));
	memcpy(other, root, sizeof(BTreeNode));
	memcpy(root, &temp, sizeof(BTreeNode));

	// Restore the original indices
	root->index = saved_root_index;
	other->index = saved_other_index;

	// Fix parent pointers for root's new children
	root->parent = 0; // Root has no parent
	if (!root->is_leaf)
	{
		uint32_t *children = get_children(tree, root);
		for (uint32_t i = 0; i <= root->num_keys; i++)
		{
			if (children[i])
			{
				BTreeNode *child = reinterpret_cast<BTreeNode *>(pager_get(children[i]));
				set_parent(child, tree.root_page_index);
			}
		}
	}
}

static void
destroy_node(BTreeNode *node)
{
	if (node->is_leaf)
	{
		if (node->previous != 0)
		{
			BTreeNode *prev_node = get_prev(node);
			if (prev_node)
			{
				set_next(prev_node, node->next);
			}
		}

		if (node->next != 0)
		{
			BTreeNode *next_node = get_next(node);
			if (next_node)
			{
				set_prev(next_node, node->previous);
			}
		}
	}

	pager_delete(node->index);
}

static bool
is_match(BPlusTree &tree, BTreeNode *node, uint32_t index, const uint8_t *key)
{
	return cmp(tree.node_key_size, get_key_at(tree, node, index), key) == 0;
}
static BTreeNode *
split(BPlusTree &tree, BTreeNode *node)
{
	BTreeNode *right_node = create_node(tree, node->is_leaf);
	uint32_t   split_index = get_split_index(tree, node);

	// Save the rising key VALUE before any modifications
	uint8_t rising_key_value[256]; // Max possible key size
	memcpy(rising_key_value, get_key_at(tree, node, split_index), tree.node_key_size);

	mark_dirty(right_node);
	mark_dirty(node);

	BTreeNode *parent = get_parent(node);
	uint32_t   parent_index = 0;

	if (!parent)
	{
		auto new_internal = create_node(tree, false);
		auto root = reinterpret_cast<BTreeNode *>(pager_get(tree.root_page_index));
		// After swap: root contains empty internal node, new_internal contains old root data
		swap_with_root(tree, root, new_internal);

		// Fix parent pointers for new_internal's children (which came from old root)
		if (!new_internal->is_leaf)
		{
			uint32_t *children = get_children(tree, new_internal);
			for (uint32_t i = 0; i <= new_internal->num_keys; i++)
			{
				if (children[i])
				{
					BTreeNode *child = reinterpret_cast<BTreeNode *>(pager_get(children[i]));
					set_parent(child, new_internal->index);
				}
			}
		}

		set_child(tree, root, 0, new_internal->index);

		parent = root; // Continue with root as the parent
		node = new_internal;
	}
	else
	{
		mark_dirty(parent);

		uint32_t *parent_children = get_children(tree, parent);
		while (parent_children[parent_index] != node->index)
			parent_index++;

		memcpy(parent_children + parent_index + 2, parent_children + parent_index + 1,
			   (parent->num_keys - parent_index) * TYPE_4);

		memcpy(get_key_at(tree, parent, parent_index + 1), get_key_at(tree, parent, parent_index),
			   (parent->num_keys - parent_index) * tree.node_key_size);
	}

	// Use the saved rising key value for parent
	memcpy(get_key_at(tree, parent, parent_index), rising_key_value, tree.node_key_size);
	set_child(tree, parent, parent_index + 1, right_node->index);
	parent->num_keys++;

	if (node->is_leaf)
	{
		right_node->num_keys = node->num_keys - split_index;
		memcpy(get_keys(right_node), get_key_at(tree, node, split_index), tree.node_key_size * right_node->num_keys);
		memcpy(get_leaf_record_data(tree, right_node), get_record_at(tree, node, split_index),
			   right_node->num_keys * tree.record_size);

		right_node->next = node->next;
		right_node->previous = node->index;
		if (node->next != 0)
		{
			BTreeNode *next = get_next(node);
			if (next)
				set_prev(next, right_node->index);
		}
		node->next = right_node->index;
	}
	else
	{
		right_node->num_keys = node->num_keys - split_index - 1;
		memcpy(get_keys(right_node), get_key_at(tree, node, split_index + 1),
			   right_node->num_keys * tree.node_key_size);

		if (!node->is_leaf)
		{
			uint32_t *src_children = get_children(tree, node);
			for (uint32_t i = 0; i <= right_node->num_keys; i++)
			{
				uint32_t child = src_children[split_index + 1 + i];
				if (child)
				{
					set_child(tree, right_node, i, child);
					src_children[split_index + 1 + i] = 0;
				}
			}
		}
	}

	node->num_keys = split_index;
	return parent;
}
static void
insert_repair(BPlusTree &tree, BTreeNode *node)
{
	if (node->num_keys < get_max_keys(tree, node))
	{
		return;
	}
	else if (node->parent == 0)
	{
		split(tree, node);
	}
	else
	{
		BTreeNode *new_node = split(tree, node);
		insert_repair(tree, new_node);
	}
}

static bool
insert(BPlusTree &tree, BTreeNode *node, uint8_t *key, const uint8_t *data)
{
	if (node->is_leaf)
	{
		uint8_t *record_data = get_leaf_record_data(tree, node);
		uint32_t insert_index = binary_search(tree, node, key);

		/* we do the duplicate validation in the cursor by finding first */

		if (node->num_keys >= tree.leaf_max_keys)
		{
			insert_repair(tree, node);
			return false;
		}

		mark_dirty(node);

		uint32_t num_to_shift = node->num_keys - insert_index;

		memcpy(get_key_at(tree, node, insert_index + 1), get_key_at(tree, node, insert_index),
			   num_to_shift * tree.node_key_size);

		memcpy(record_data + (insert_index + 1) * tree.record_size, record_data + insert_index * tree.record_size,
			   num_to_shift * tree.record_size);

		memcpy(get_key_at(tree, node, insert_index), key, tree.node_key_size);
		memcpy(record_data + insert_index * tree.record_size, data, tree.record_size);

		node->num_keys++;
		return true;
	}
	else
	{
		uint32_t   child_index = binary_search(tree, node, key);
		BTreeNode *child_node = get_child(tree, node, child_index);
		if (child_node)
		{
			return insert(tree, child_node, key, data);
		}
	}
	return false;
}

static void
insert_element(BPlusTree &tree, void *key, const uint8_t *data)
{

	BTreeNode *root = get_root(tree);

	if (root->num_keys == 0)
	{
		mark_dirty(root);
		uint8_t *keys = get_keys(root);
		uint8_t *record_data = get_leaf_record_data(tree, root);

		memcpy(keys, key, tree.node_key_size);
		memcpy(record_data, data, tree.record_size);
		root->num_keys = 1;
	}
	else
	{
		if (!insert(tree, root, reinterpret_cast<uint8_t *>(key), data))
		{
			insert(tree, get_root(tree), reinterpret_cast<uint8_t *>(key), data);
		}
	}
}

static void
update_parent_keys(BPlusTree &tree, BTreeNode *node, const uint8_t *deleted_key)
{
	uint8_t	  *next_smallest = nullptr;
	BTreeNode *parent_node = get_parent(node);

	uint32_t *parent_children = get_children(tree, parent_node);
	uint32_t  parent_index;

	for (parent_index = 0; parent_children[parent_index] != node->index; parent_index++)
		;

	if (node->num_keys == 0)
	{
		if (parent_index == parent_node->num_keys)
		{
			next_smallest = nullptr;
		}
		else
		{
			BTreeNode *next_sibling = get_child(tree, parent_node, parent_index + 1);
			if (next_sibling)
			{
				next_smallest = get_key_at(tree, next_sibling, 0);
			}
		}
	}
	else
	{
		next_smallest = get_key_at(tree, node, 0);
	}

	BTreeNode *current_parent = parent_node;
	while (current_parent)
	{
		if (parent_index > 0 &&
			cmp(tree.node_key_size, get_key_at(tree, current_parent, parent_index - 1), deleted_key) == 0)
		{
			mark_dirty(current_parent);
			if (next_smallest)
			{
				memcpy(get_key_at(tree, current_parent, parent_index - 1), next_smallest, tree.node_key_size);
			}
		}

		BTreeNode *grandparent = get_parent(current_parent);
		if (grandparent)
		{
			uint32_t *grandparent_children = get_children(tree, grandparent);
			for (parent_index = 0; grandparent_children[parent_index] != current_parent->index; parent_index++)
				;
		}
		current_parent = grandparent;
	}
}

static void
do_delete_bplus(BPlusTree &tree, BTreeNode *node, const uint8_t *key, uint32_t i)
{
	if (node->is_leaf)
	{
		mark_dirty(node);

		uint8_t *record_data = get_leaf_record_data(tree, node);
		uint32_t shift_count = node->num_keys - i - 1;

		memcpy(get_key_at(tree, node, i), get_key_at(tree, node, i + 1), tree.node_key_size * shift_count);
		memcpy(record_data + i * tree.record_size, record_data + (i + 1) * tree.record_size,
			   tree.record_size * shift_count);

		node->num_keys--;

		if (i == 0 && node->parent != 0)
		{
			update_parent_keys(tree, node, key);
		}

		repair_after_delete(tree, node);
	}
}

static void
do_delete(BPlusTree &tree, BTreeNode *node, const uint8_t *key, uint32_t index)
{
	if (node->parent == 0 && node->num_keys <= 1 && node->is_leaf)
	{
		mark_dirty(node);
		node->num_keys = 0;
		return;
	}

	do_delete_bplus(tree, node, key, index);
}

static BTreeNode *
steal_from_right(BPlusTree &tree, BTreeNode *node, uint32_t parent_index)
{
	BTreeNode *parent_node = get_parent(node);
	BTreeNode *right_sibling = get_child(tree, parent_node, parent_index + 1);

	mark_dirty(node);
	mark_dirty(parent_node);
	mark_dirty(right_sibling);

	if (node->is_leaf)
	{
		memcpy(get_key_at(tree, node, node->num_keys), get_key_at(tree, right_sibling, 0), tree.node_key_size);

		uint8_t *node_records = get_leaf_record_data(tree, node);
		uint8_t *sibling_records = get_leaf_record_data(tree, right_sibling);

		memcpy(node_records + node->num_keys * tree.record_size, sibling_records, tree.record_size);

		uint32_t shift_count = right_sibling->num_keys - 1;
		memcpy(get_key_at(tree, right_sibling, 0), get_key_at(tree, right_sibling, 1),
			   shift_count * tree.node_key_size);
		memcpy(sibling_records, sibling_records + tree.record_size, shift_count * tree.record_size);

		memcpy(get_key_at(tree, parent_node, parent_index), get_key_at(tree, right_sibling, 0), tree.node_key_size);
	}
	else
	{
		memcpy(get_key_at(tree, node, node->num_keys), get_key_at(tree, parent_node, parent_index), tree.node_key_size);

		memcpy(get_key_at(tree, parent_node, parent_index), get_key_at(tree, right_sibling, 0), tree.node_key_size);

		uint32_t shift_count = right_sibling->num_keys - 1;
		memcpy(get_key_at(tree, right_sibling, 0), get_key_at(tree, right_sibling, 1),
			   shift_count * tree.node_key_size);

		uint32_t *node_children = get_children(tree, node);
		uint32_t *sibling_children = get_children(tree, right_sibling);

		set_child(tree, node, node->num_keys + 1, sibling_children[0]);

		for (uint32_t i = 0; i < right_sibling->num_keys; i++)
		{
			set_child(tree, right_sibling, i, sibling_children[i + 1]);
		}
	}

	node->num_keys++;
	right_sibling->num_keys--;

	return parent_node;
}

static BTreeNode *
merge_right(BPlusTree &tree, BTreeNode *node)
{
	BTreeNode *parent = get_parent(node);
	if (!parent)
	{
		return node;
	}

	uint32_t *parent_children = get_children(tree, parent);

	uint32_t node_index = 0;
	for (; node_index <= parent->num_keys; node_index++)
	{
		if (parent_children[node_index] == node->index)
		{
			break;
		}
	}

	if (node_index >= parent->num_keys)
	{
		return node;
	}

	BTreeNode *right_sibling = get_child(tree, parent, node_index + 1);
	if (!right_sibling)
	{
		return node;
	}

	mark_dirty(node);
	mark_dirty(parent);

	if (node->is_leaf)
	{
		uint8_t *node_records = get_leaf_record_data(tree, node);
		uint8_t *sibling_records = get_leaf_record_data(tree, right_sibling);

		memcpy(get_key_at(tree, node, node->num_keys), get_key_at(tree, right_sibling, 0),
			   right_sibling->num_keys * tree.node_key_size);
		memcpy(node_records + node->num_keys * tree.record_size, sibling_records,
			   right_sibling->num_keys * tree.record_size);

		node->num_keys += right_sibling->num_keys;

		set_next(node, right_sibling->next);
		if (right_sibling->next != 0)
		{
			BTreeNode *next_node = get_next(right_sibling);
			if (next_node)
			{
				set_prev(next_node, node->index);
			}
		}
	}
	else
	{
		memcpy(get_key_at(tree, node, node->num_keys), get_key_at(tree, parent, node_index), tree.node_key_size);

		memcpy(get_key_at(tree, node, node->num_keys + 1), get_key_at(tree, right_sibling, 0),
			   right_sibling->num_keys * tree.node_key_size);

		uint32_t *node_children = get_children(tree, node);
		uint32_t *sibling_children = get_children(tree, right_sibling);

		for (uint32_t i = 0; i <= right_sibling->num_keys; i++)
		{
			set_child(tree, node, node->num_keys + 1 + i, sibling_children[i]);
		}

		node->num_keys += 1 + right_sibling->num_keys;
	}

	uint32_t shift_count = parent->num_keys - node_index - 1;

	memcpy(get_key_at(tree, parent, node_index), get_key_at(tree, parent, node_index + 1),
		   shift_count * tree.node_key_size);
	memcpy(parent_children + node_index + 1, parent_children + node_index + 2, shift_count * sizeof(uint32_t));

	parent->num_keys--;

	destroy_node(right_sibling);

	if (parent->num_keys < get_min_keys(tree, parent))
	{
		if (parent->parent == 0 && parent->num_keys == 0)
		{
			swap_with_root(tree, parent, node);
			// parent = node after swap, so delete node
			destroy_node(node);
			return node;
		}
		else
		{
			repair_after_delete(tree, parent);
		}
	}

	return node;
}

static BTreeNode *
steal_from_left(BPlusTree &tree, BTreeNode *node, uint32_t parent_index)
{
	BTreeNode *parent_node = get_parent(node);
	BTreeNode *left_sibling = get_child(tree, parent_node, parent_index - 1);

	mark_dirty(node);
	mark_dirty(parent_node);
	mark_dirty(left_sibling);

	memcpy(get_key_at(tree, node, 1), get_key_at(tree, node, 0), tree.node_key_size * node->num_keys);

	if (node->is_leaf)
	{
		memcpy(get_key_at(tree, node, 0), get_key_at(tree, left_sibling, left_sibling->num_keys - 1),
			   tree.node_key_size);

		uint8_t *node_records = get_leaf_record_data(tree, node);
		uint8_t *sibling_records = get_leaf_record_data(tree, left_sibling);

		memcpy(node_records + tree.record_size, node_records, node->num_keys * tree.record_size);
		memcpy(node_records, sibling_records + (left_sibling->num_keys - 1) * tree.record_size, tree.record_size);
		memcpy(get_key_at(tree, parent_node, parent_index - 1), get_key_at(tree, node, 0), tree.node_key_size);
	}
	else
	{
		memcpy(get_key_at(tree, node, 0), get_key_at(tree, parent_node, parent_index - 1), tree.node_key_size);

		memcpy(get_key_at(tree, parent_node, parent_index - 1),
			   get_key_at(tree, left_sibling, left_sibling->num_keys - 1), tree.node_key_size);

		uint32_t *node_children = get_children(tree, node);
		uint32_t *sibling_children = get_children(tree, left_sibling);

		for (uint32_t i = node->num_keys + 1; i > 0; i--)
		{
			set_child(tree, node, i, node_children[i - 1]);
		}
		set_child(tree, node, 0, sibling_children[left_sibling->num_keys]);
	}

	node->num_keys++;
	left_sibling->num_keys--;

	return parent_node;
}

static void
repair_after_delete(BPlusTree &tree, BTreeNode *node)
{
	if (node->num_keys >= get_min_keys(tree, node))
	{
		return;
	}

	if (node->parent == 0)
	{
		if (node->num_keys == 0 && !node->is_leaf)
		{
			BTreeNode *only_child = get_child(tree, node, 0);
			if (only_child)
			{
				swap_with_root(tree, node, only_child);
				// after swap, destroy this one
				destroy_node(only_child);
			}
		}
		return;
	}

	BTreeNode *parent = get_parent(node);
	uint32_t  *parent_children = get_children(tree, parent);

	uint32_t node_index = 0;
	for (; node_index <= parent->num_keys; node_index++)
	{
		if (parent_children[node_index] == node->index)
		{
			break;
		}
	}

	if (node_index > 0)
	{
		BTreeNode *left_sibling = get_child(tree, parent, node_index - 1);
		if (left_sibling && left_sibling->num_keys > get_min_keys(tree, left_sibling))
		{
			steal_from_left(tree, node, node_index);
			return;
		}
	}

	if (node_index < parent->num_keys)
	{
		BTreeNode *right_sibling = get_child(tree, parent, node_index + 1);
		if (right_sibling && right_sibling->num_keys > get_min_keys(tree, right_sibling))
		{
			steal_from_right(tree, node, node_index);
			return;
		}
	}

	if (node_index < parent->num_keys)
	{
		merge_right(tree, node);
	}
	else if (node_index > 0)
	{
		BTreeNode *left_sibling = get_child(tree, parent, node_index - 1);
		if (left_sibling)
		{
			merge_right(tree, left_sibling);
		}
	}
}

void
clear_recurse(BPlusTree &tree, BTreeNode *node)
{
	if (node->is_leaf)
	{
		pager_delete(node->index);
		return;
	}

	uint32_t   i = 0;
	BTreeNode *child = get_child(tree, node, i);
	while (child != nullptr)
	{
		clear_recurse(tree, child);
		child = get_child(tree, node, i++);
	}

	pager_delete(node->index);
}

bool
bplustree_clear(BPlusTree *tree)
{
	if (0 == tree->root_page_index)
	{
		// unitialised table
		return true;
	}

	clear_recurse(*tree, reinterpret_cast<BTreeNode *>(pager_get(tree->root_page_index)));
	return true;
}

/* ------ CURSOR -----------
 */

static FindResult
find_containing_node(BPlusTree &tree, BTreeNode *node, const uint8_t *key, BPtCursor *cursor)
{
	uint32_t index = binary_search(tree, node, key);

	if (node->is_leaf)
	{
		return {node, index, (index < node->num_keys && is_match(tree, node, index, key))};
	}

	// Need to descend to child
	uint32_t child_pos = index;

	BTreeNode *child = get_child(tree, node, child_pos);
	if (!child)
	{
		return {node, index, false};
	}

	return find_containing_node(tree, child, key, cursor);
}
static void
cursor_clear(BPtCursor *cursor)
{
	cursor->path.current_page = 0;
	cursor->path.current_index = 0;
	cursor->state = BPT_CURSOR_INVALID;
}

static void
cursor_save_state(BPtCursor *cursor)
{
	cursor->saved = cursor->path;
}

static void
cursor_restore_state(BPtCursor *cursor)
{
	cursor->path = cursor->saved;
}

static bool
cursor_move_in_subtree(BPtCursor *cursor, BTreeNode *root, bool left)
{
	BTreeNode *current = root;

	while (!current->is_leaf)
	{
		if (left)
		{

			current = get_child(*cursor->tree, current, 0);
		}
		else
		{
			uint32_t child_pos = current->num_keys;

			current = get_child(*cursor->tree, current, child_pos);
		}

		if (!current)
		{
			cursor->state = BPT_CURSOR_FAULT;
			return false;
		}
	}

	if (left)
	{
		cursor->path.current_page = current->index;
		cursor->path.current_index = 0;
	}
	else
	{
		cursor->path.current_page = current->index;
		cursor->path.current_index = current->num_keys - 1;
	}
	cursor->state = BPT_CURSOR_VALID;
	return true;
}

static bool
cursor_move_end(BPtCursor *cursor, bool first)
{
	cursor_clear(cursor);

	BTreeNode *root = get_root(*cursor->tree);
	if (!root || root->num_keys == 0)
	{
		cursor->state = BPT_CURSOR_INVALID;
		return false;
	}

	return cursor_move_in_subtree(cursor, root, first);
}

bool
bplustree_cursor_seek_cmp(BPtCursor *cursor, const void *key, CompareOp op)
{
	bool exact_match_ok = (op == GE || op == LE);
	bool forward = (op == GE || op == GT);

	if (exact_match_ok && bplustree_cursor_seek(cursor, key))
	{
		return true;
	}
	else
	{
		bplustree_cursor_seek(cursor, key);
	}

	do
	{
		const uint8_t *current_key = bplustree_cursor_key(cursor);
		if (!current_key)
			continue;

		int cmp_result = cmp(cursor->tree->node_key_size, current_key, reinterpret_cast<const uint8_t *>(key));

		bool satisfied = (op == GE && cmp_result >= 0) || (op == GT && cmp_result > 0) ||
						 (op == LE && cmp_result <= 0) || (op == LT && cmp_result < 0);
		if (satisfied)
			return true;
	} while (forward ? bplustree_cursor_next(cursor) : bplustree_cursor_previous(cursor));

	return false;
}

// Public cursor functions
bool
bplustree_cursor_is_valid(BPtCursor *cursor)
{
	return cursor->state == BPT_CURSOR_VALID;
}

uint8_t *
bplustree_cursor_key(BPtCursor *cursor)
{
	if (cursor->state != BPT_CURSOR_VALID)
	{
		return nullptr;
	}

	BTreeNode *node = reinterpret_cast<BTreeNode *>(pager_get(cursor->path.current_page));
	if (!node || cursor->path.current_index >= node->num_keys)
	{
		return nullptr;
	}

	return get_key_at(*cursor->tree, node, cursor->path.current_index);
}

uint8_t *
bplustree_cursor_record(BPtCursor *cursor)
{
	if (cursor->state != BPT_CURSOR_VALID)
	{
		return nullptr;
	}

	BTreeNode *node = reinterpret_cast<BTreeNode *>(pager_get(cursor->path.current_page));
	if (!node || cursor->path.current_index >= node->num_keys)
	{
		return nullptr;
	}

	return get_record_at(*cursor->tree, node, cursor->path.current_index);
}

bool
bplustree_cursor_seek(BPtCursor *cursor, const void *key)
{
	cursor_clear(cursor);

	if (!cursor->tree->root_page_index)
	{
		return false;
	}

	FindResult result =
		find_containing_node(*cursor->tree, get_root(*cursor->tree), reinterpret_cast<const uint8_t *>(key), cursor);

	cursor->path.current_page = result.node->index;
	cursor->path.current_index = result.index;

	if (result.found)
	{
		cursor->state = BPT_CURSOR_VALID;

		return true;
	}

	cursor->state = (result.index < result.node->num_keys) ? BPT_CURSOR_VALID : BPT_CURSOR_INVALID;
	return false;
}

bool
bplustree_cursor_delete(BPtCursor *cursor)
{
	if (cursor->state != BPT_CURSOR_VALID)
	{
		return false;
	}

	const uint8_t *key = bplustree_cursor_key(cursor);
	if (!key)
	{
		return false;
	}

	auto	 node = reinterpret_cast<BTreeNode *>(pager_get(cursor->path.current_page));
	uint32_t index = node->index;

	do_delete(*cursor->tree, node, key, cursor->path.current_index);

	// After delete, cursor
	// position depends on what
	// happened
	node = reinterpret_cast<BTreeNode *>(pager_get(cursor->path.current_page));
	if (!node || node->index != index)
	{
		cursor->state = BPT_CURSOR_INVALID;
		return true;
	}

	if (cursor->path.current_index >= node->num_keys)
	{
		if (node->num_keys > 0)
		{
			cursor->path.current_index = node->num_keys - 1;
		}
		else
		{
			cursor->state = BPT_CURSOR_INVALID;
		}
	}

	return true;
}

bool
bplustree_cursor_insert(BPtCursor *cursor, const void *key, const uint8_t *record)
{

	FindResult result =
		find_containing_node(*cursor->tree, get_root(*cursor->tree), reinterpret_cast<const uint8_t *>(key), cursor);

	if (result.found)
	{
		return false;
	}

	insert_element(*cursor->tree, const_cast<void *>(key), record);
	return true;
}

bool
bplustree_cursor_update(BPtCursor *cursor, const uint8_t *record)
{
	if (cursor->state != BPT_CURSOR_VALID)
	{
		return false;
	}
	pager_mark_dirty(cursor->path.current_page);
	uint8_t *data = bplustree_cursor_record(cursor);
	memcpy(data, record, cursor->tree->record_size);
	return true;
}

bool
bplustree_cursor_seek_ge(BPtCursor *cursor, const void *key)
{
	return bplustree_cursor_seek_cmp(cursor, key, GE);
}

bool
bplustree_cursor_seek_gt(BPtCursor *cursor, const void *key)
{
	return bplustree_cursor_seek_cmp(cursor, key, GT);
}

bool
bplustree_cursor_seek_le(BPtCursor *cursor, const void *key)
{
	return bplustree_cursor_seek_cmp(cursor, key, LE);
}

bool
bplustree_cursor_seek_lt(BPtCursor *cursor, const void *key)
{
	return bplustree_cursor_seek_cmp(cursor, key, LT);
}

bool
bplustree_cursor_first(BPtCursor *cursor)
{
	return cursor_move_end(cursor, true);
}

bool
bplustree_cursor_last(BPtCursor *cursor)
{
	return cursor_move_end(cursor, false);
}

bool
bplustree_cursor_next(BPtCursor *cursor)
{
	if (cursor->state != BPT_CURSOR_VALID)
	{
		return false;
	}

	BTreeNode *node = reinterpret_cast<BTreeNode *>(pager_get(cursor->path.current_page));
	if (!node)
	{
		cursor->state = BPT_CURSOR_FAULT;
		return false;
	}

	cursor_save_state(cursor);

	if (!node->is_leaf)
	{
		cursor_restore_state(cursor);
		return false;
	}

	cursor->path.current_index++;
	if (cursor->path.current_index >= node->num_keys)
	{
		if (node->next != 0)
		{
			BTreeNode *next_node = get_next(node);
			if (next_node && next_node->num_keys > 0)
			{
				cursor->path.current_page = next_node->index;
				cursor->path.current_index = 0;
				return true;
			}
		}

		cursor_restore_state(cursor);
		return false;
	}
	return true;
}

bool
bplustree_cursor_previous(BPtCursor *cursor)
{
	if (cursor->state != BPT_CURSOR_VALID)
	{
		return false;
	}

	BTreeNode *node = reinterpret_cast<BTreeNode *>(pager_get(cursor->path.current_page));
	if (!node)
	{
		cursor->state = BPT_CURSOR_FAULT;
		return false;
	}

	cursor_save_state(cursor);

	// If leaf node in B+tree,
	// use previous pointer for
	// efficiency

	if (!node->is_leaf)
	{
		cursor_restore_state(cursor);
		return false;
	}
	if (cursor->path.current_index > 0)
	{
		cursor->path.current_index--;
		return true;
	}

	// Move to previous leaf
	if (node->previous != 0)
	{
		BTreeNode *prev_node = get_prev(node);
		if (prev_node && prev_node->num_keys > 0)
		{
			cursor->path.current_page = prev_node->index;
			cursor->path.current_index = prev_node->num_keys - 1;
			return true;
		}
	}

	cursor_restore_state(cursor);
	return false;
}

bool
bplustree_cursor_has_next(BPtCursor *cursor)
{
	if (bplustree_cursor_next(cursor))
	{
		bplustree_cursor_previous(cursor);
		return true;
	}
	return false;
}

bool
bplustree_cursor_has_previous(BPtCursor *cursor)
{
	if (bplustree_cursor_previous(cursor))
	{
		bplustree_cursor_next(cursor);
		return true;
	}
	return false;
}

// wrap pager

bool
bplustree_cursor_seek_exact(BPtCursor *cursor, const void *key, const uint8_t *record)
{

	// First seek to key
	if (!bplustree_cursor_seek(cursor, key))
	{
		return false;
	}

	return true;
}

// Add this to bplustree.cpp

#include <cassert>
#include <unordered_set>

// Validation result structure to pass information up the recursion
struct ValidationResult
{
	uint32_t   depth;
	uint8_t	  *min_key;
	uint8_t	  *max_key;
	BTreeNode *leftmost_leaf;
	BTreeNode *rightmost_leaf;
};

// Forward declaration
static ValidationResult
validate_node_recursive(BPlusTree &tree, BTreeNode *node, uint32_t expected_parent, uint8_t *parent_min_bound,
						uint8_t *parent_max_bound, std::unordered_set<uint32_t> &visited);

// Main validation function
void
bplustree_validate(BPlusTree *tree)
{
	assert(tree != nullptr);

	// Empty tree is valid
	if (tree->root_page_index == 0)
	{
		return;
	}

	BTreeNode *root = get_root(*tree);
	assert(root != nullptr);

	// Root specific checks
	assert(root->parent == 0); // Root has no parent
	assert(root->index == tree->root_page_index);

	// Track visited nodes to detect cycles
	std::unordered_set<uint32_t> visited;

	// Validate tree recursively
	ValidationResult result = validate_node_recursive(*tree, root, 0, nullptr, nullptr, visited);

	// If tree has data, verify leaf chain integrity
	if (root->is_leaf && root->num_keys > 0)
	{
		// Single leaf root should have no siblings
		assert(root->next == 0);
		assert(root->previous == 0);
	}
	else if (!root->is_leaf)
	{
		// Verify complete leaf chain by walking it
		BTreeNode					*first_leaf = result.leftmost_leaf;
		BTreeNode					*current = first_leaf;
		std::unordered_set<uint32_t> leaf_visited;

		assert(current->previous == 0); // First leaf has no previous

		while (current)
		{
			assert(current->is_leaf);
			assert(leaf_visited.find(current->index) == leaf_visited.end()); // No cycles in leaf chain
			leaf_visited.insert(current->index);

			if (current->next != 0)
			{
				BTreeNode *next = get_next(current);
				assert(next != nullptr);
				assert(next->previous == current->index); // Bidirectional link integrity
				current = next;
			}
			else
			{
				assert(current == result.rightmost_leaf); // Last leaf matches rightmost
				break;
			}
		}
	}
}

static ValidationResult
validate_node_recursive(BPlusTree &tree, BTreeNode *node, uint32_t expected_parent, uint8_t *parent_min_bound,
						uint8_t *parent_max_bound, std::unordered_set<uint32_t> &visited)
{
	assert(node != nullptr);

	// Check for cycles
	assert(visited.find(node->index) == visited.end());
	visited.insert(node->index);

	// Verify parent pointer
	assert(node->parent == expected_parent);

	// Check key count constraints
	uint32_t max_keys = get_max_keys(tree, node);
	uint32_t min_keys = get_min_keys(tree, node);

	assert(node->num_keys <= max_keys);

	// Non-root nodes must meet minimum
	if (expected_parent != 0)
	{
		assert(node->num_keys >= min_keys);
	}
	else
	{
		// Root can have fewer, but not zero (unless tree is being cleared)
		if (node->num_keys == 0)
		{
			assert(node->is_leaf); // Only leaf root can be empty during deletion
		}
	}

	// Validate key ordering and bounds
	uint8_t *prev_key = nullptr;
	uint8_t *first_key = nullptr;
	uint8_t *last_key = nullptr;

	for (uint32_t i = 0; i < node->num_keys; i++)
	{
		uint8_t *current_key = get_key_at(tree, node, i);

		if (i == 0)
		{
			first_key = current_key;
		}
		if (i == node->num_keys - 1)
		{
			last_key = current_key;
		}


		if (prev_key)
		{
			int cmp_result = cmp(tree.node_key_size, prev_key, current_key);
			assert(cmp_result < 0); // prev < current
		}

		// Check bounds from parent
		if (parent_min_bound)
		{
			assert(cmp(tree.node_key_size, current_key, parent_min_bound) >= 0);
		}
		if (parent_max_bound)
		{

			assert(cmp(tree.node_key_size, current_key, parent_max_bound) < 0);
		}
		prev_key = current_key;
	}

	ValidationResult result;
	result.min_key = first_key;
	result.max_key = last_key;

	if (node->is_leaf)
	{
		result.depth = 0;
		result.leftmost_leaf = node;
		result.rightmost_leaf = node;

		// Validate leaf data exists
		uint8_t *records = get_leaf_record_data(tree, node);
		assert(records != nullptr);

		// Verify leaf chain pointers are valid page indices or 0
		if (node->next != 0)
		{
			assert(node->next != node->index); // No self-reference
			BTreeNode *next = get_next(node);
			assert(next != nullptr);
			assert(next->is_leaf);
		}
		if (node->previous != 0)
		{
			assert(node->previous != node->index); // No self-reference
			BTreeNode *prev = get_prev(node);
			assert(prev != nullptr);
			assert(prev->is_leaf);
		}
	}
	else
	{
		// Internal node validation
		uint32_t *children = get_children(tree, node);
		assert(children != nullptr);

		uint32_t   child_depth = UINT32_MAX;
		BTreeNode *leftmost_leaf = nullptr;
		BTreeNode *rightmost_leaf = nullptr;

		// Internal nodes have num_keys + 1 children
		for (uint32_t i = 0; i <= node->num_keys; i++)
		{
			assert(children[i] != 0);			// No null children
			assert(children[i] != node->index); // No self-reference

			BTreeNode *child = get_child(tree, node, i);
			assert(child != nullptr);

			// Determine bounds for this child
			uint8_t *child_min = (i == 0) ? parent_min_bound : get_key_at(tree, node, i - 1);
			uint8_t *child_max = (i == node->num_keys) ? parent_max_bound : get_key_at(tree, node, i);

			ValidationResult child_result =
				validate_node_recursive(tree, child, node->index, child_min, child_max, visited);

			// All children must have same depth
			if (child_depth == UINT32_MAX)
			{
				child_depth = child_result.depth;
				leftmost_leaf = child_result.leftmost_leaf;
			}
			else
			{
				assert(child_depth == child_result.depth);
			}

			// Track rightmost leaf
			rightmost_leaf = child_result.rightmost_leaf;

			// Verify key bounds match child contents
			if (child_result.min_key && i > 0)
			{
				// First key in child >= separator key before it
				uint8_t *separator = get_key_at(tree, node, i - 1);
				assert(cmp(tree.node_key_size, child_result.min_key, separator) >= 0);
			}
			if (child_result.max_key && i < node->num_keys)
			{
				// Last key in child < separator key after it
				uint8_t *separator = get_key_at(tree, node, i);
				assert(cmp(tree.node_key_size, child_result.max_key, separator) <= 0);
			}
		}

		result.depth = child_depth + 1;
		result.leftmost_leaf = leftmost_leaf;
		result.rightmost_leaf = rightmost_leaf;

		// Internal nodes should not have leaf chain pointers
		assert(node->next == 0);
		assert(node->previous == 0);
	}

	return result;
}

// Add this to bplustree.cpp

#include <queue>
#include <iomanip>

// Helper to print a single key based on type
static void
print_key(BPlusTree &tree, uint8_t *key)
{
	if (!key)
	{
		printf("NULL");
		return;
	}

	switch (tree.node_key_size)
	{
	case TYPE_4: {
		uint32_t val;
		memcpy(&val, key, 4);
		printf("%u", val);
		break;
	}
	case TYPE_8: {
		uint64_t val;
		memcpy(&val, key, 8);
		printf("%lu", val);
		break;
	}
	case TYPE_32:
	case TYPE_256: {
		// Print as string, but limit length for readability
		uint32_t max_len = (tree.node_key_size == TYPE_32) ? 32 : 256;
		printf("\"");
		for (uint32_t i = 0; i < max_len && key[i]; i++)
		{
			if (key[i] >= 32 && key[i] < 127)
			{
				printf("%c", key[i]);
			}
			else
			{
				printf("\\x%02x", key[i]);
			}
			if (i > 10)
			{ // Truncate long strings
				printf("...");
				break;
			}
		}
		printf("\"");
		break;
	}
	default:
		printf("?");
	}
}

// Main B+Tree print function
void
bplustree_print(BPlusTree *tree)
{
	if (!tree || tree->root_page_index == 0)
	{
		printf("B+Tree: EMPTY\n");
		return;
	}

	printf("====================================\n");
	printf("B+Tree Structure (BFS)\n");
	printf("====================================\n");
	printf("Root: page_%u\n", tree->root_page_index);
	printf("Key type: %s, Record size: %u bytes\n", type_to_string(tree->node_key_size), tree->record_size);
	printf("Internal: max_keys=%u, min_keys=%u\n", tree->internal_max_keys, tree->internal_min_keys);
	printf("Leaf: max_keys=%u, min_keys=%u\n", tree->leaf_max_keys, tree->leaf_min_keys);
	printf("------------------------------------\n\n");

	// BFS traversal using two queues (current level and next level)
	std::queue<uint32_t> current_level;
	std::queue<uint32_t> next_level;

	current_level.push(tree->root_page_index);
	uint32_t depth = 0;

	while (!current_level.empty())
	{
		printf("LEVEL %u:\n", depth);
		printf("--------\n");

		while (!current_level.empty())
		{
			uint32_t page_index = current_level.front();
			current_level.pop();

			BTreeNode *node = reinterpret_cast<BTreeNode *>(pager_get(page_index));
			if (!node)
			{
				printf("  ERROR: Cannot read page %u\n", page_index);
				continue;
			}

			// Print node header
			printf("  Node[page_%u]:\n", node->index);
			printf("    Type: %s\n", node->is_leaf ? "LEAF" : "INTERNAL");
			printf("    Parent: %s\n", node->parent == 0 ? "ROOT" : ("page_" + std::to_string(node->parent)).c_str());
			printf("    Keys(%u): [", node->num_keys);

			// Print keys
			for (uint32_t i = 0; i < node->num_keys; i++)
			{
				if (i > 0)
					printf(", ");
				print_key(*tree, get_key_at(*tree, node, i));
			}
			printf("]\n");

			// Print children for internal nodes
			if (!node->is_leaf)
			{
				uint32_t *children = get_children(*tree, node);
				printf("    Children(%u): [", node->num_keys + 1);
				for (uint32_t i = 0; i <= node->num_keys; i++)
				{
					if (i > 0)
						printf(", ");
					printf("page_%u", children[i]);

					// Add children to next level queue
					next_level.push(children[i]);
				}
				printf("]\n");
			}
			else
			{
				// Print leaf chain info
				printf("    Leaf chain: ");
				if (node->previous != 0)
				{
					printf("prev=page_%u", node->previous);
				}
				else
				{
					printf("prev=NULL");
				}
				printf(", ");
				if (node->next != 0)
				{
					printf("next=page_%u", node->next);
				}
				else
				{
					printf("next=NULL");
				}
				printf("\n");
			}

			printf("\n");
		}

		// Move to next level
		if (!next_level.empty())
		{
			std::swap(current_level, next_level);
			depth++;
		}
	}

	// Print leaf chain traversal for verification
	printf("====================================\n");
	printf("Leaf Chain Traversal:\n");
	printf("------------------------------------\n");

	// Find leftmost leaf
	BTreeNode *current = get_root(*tree);
	while (!current->is_leaf)
	{
		current = get_child(*tree, current, 0);
		if (!current)
		{
			printf("ERROR: Cannot find leftmost leaf\n");
			return;
		}
	}

	printf("  ");
	uint32_t leaf_count = 0;
	while (current)
	{
		if (leaf_count > 0)
			printf(" -> ");
		printf("page_%u", current->index);

		// Safety check for cycles
		if (++leaf_count > 1000)
		{
			printf("\n  ERROR: Possible cycle detected in leaf chain!\n");
			break;
		}

		current = get_next(current);
	}
	printf("\n");
	printf("  Total leaves: %u\n", leaf_count);
	printf("====================================\n\n");
}

// Compact tree printer (single line per node)
void
bplustree_print_compact(BPlusTree *tree)
{
	if (!tree || tree->root_page_index == 0)
	{
		printf("B+Tree: EMPTY\n");
		return;
	}

	printf("B+Tree (page:type:keys:parent):\n");

	std::queue<uint32_t> queue;
	std::queue<uint32_t> levels;

	queue.push(tree->root_page_index);
	levels.push(0);

	uint32_t current_level = 0;

	while (!queue.empty())
	{
		uint32_t page_index = queue.front();
		uint32_t level = levels.front();
		queue.pop();
		levels.pop();

		if (level != current_level)
		{
			printf("\n");
			current_level = level;
		}

		BTreeNode *node = reinterpret_cast<BTreeNode *>(pager_get(page_index));
		if (!node)
			continue;

		// Print: page_index:type:num_keys:parent
		printf("[%u:%c:%u:%u] ", node->index, node->is_leaf ? 'L' : 'I', node->num_keys, node->parent);

		// Add children to queue
		if (!node->is_leaf)
		{
			uint32_t *children = get_children(*tree, node);
			for (uint32_t i = 0; i <= node->num_keys; i++)
			{
				queue.push(children[i]);
				levels.push(level + 1);
			}
		}
	}
	printf("\n");
}