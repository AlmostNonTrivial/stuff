

#include "bplustree.hpp"
#include "arena.hpp"
#include "defs.hpp"
#include "types.hpp"
#include "pager.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <sys/types.h>

static void
print_key(BPlusTree &tree, uint8_t *key);

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
get_node(uint32_t index)
{
	if (PAGE_INVALID == index)
	{
		return nullptr;
	}
	return reinterpret_cast<BTreeNode *>(pager_get(index));
}
static BTreeNode *
get_root(BPlusTree &tree)
{
	return get_node(tree.root_page_index);
}

#define ASSERT_PRINT(condition, tree)                                                                                  \
                                                                                                                       \
	if (!(condition))                                                                                                  \
	{                                                                                                                  \
                                                                                                                       \
		bplustree_print(tree);                                                                                         \
		assert(condition);                                                                                             \
	}

#define mark_dirty(node) pager_mark_dirty((node)->index)

#define GET_KEYS(node) ((node)->data)

static uint8_t *
get_key_at(BPlusTree &tree, BTreeNode *node, uint32_t index)
{
	return GET_KEYS(node) + index * tree.node_key_size;
}

static uint32_t *
get_children(BPlusTree &tree, BTreeNode *node)
{
	ASSERT_PRINT(!node->is_leaf, &tree);
	return reinterpret_cast<uint32_t *>(node->data + tree.internal_max_keys * tree.node_key_size);
}

static uint8_t *
get_record_data(BPlusTree &tree, BTreeNode *node)
{
	return node->data + tree.leaf_max_keys * tree.node_key_size;
}

static uint8_t *
get_record_at(BPlusTree &tree, BTreeNode *node, uint32_t index)
{
	return get_record_data(tree, node) + (index * tree.record_size);
}

static uint32_t
binary_search(BPlusTree &tree, BTreeNode *node, const uint8_t *key)
{
	uint32_t left = 0;
	uint32_t right = node->num_keys;

	while (left < right)
	{
		uint32_t mid = left + (right - left) / 2;
		uint8_t *mid_key = get_key_at(tree, node, mid);

		if (type_less_than(tree.node_key_type, mid_key, key))
		{
			left = mid + 1;
		}
		else if (type_equals(tree.node_key_type, mid_key, key))
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
	ASSERT_PRINT(page_index != PAGE_INVALID, &tree);
	BTreeNode *node = get_node(page_index);

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

	tree.node_key_type = key;
	tree.node_key_size = type_size(key);

	tree.record_size = record_size;

	constexpr uint32_t USABLE_SPACE = PAGE_SIZE - NODE_HEADER_SIZE;

	assert(!((record_size * MIN_ENTRY_COUNT) > USABLE_SPACE));

	uint32_t leaf_entry_size = tree.node_key_size + record_size;
	uint32_t leaf_max_entries = USABLE_SPACE / leaf_entry_size;

	tree.leaf_max_keys = std::max((uint32_t)MIN_ENTRY_COUNT, leaf_max_entries);
	tree.leaf_min_keys = tree.leaf_max_keys / 2;
	tree.leaf_split_index = tree.leaf_max_keys / 2;

	uint32_t child_ptr_size = sizeof(uint32_t);
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
	if (node->parent == 0)
	{
		return nullptr;
	}
	return get_node(node->parent);
}

static BTreeNode *
get_child(BPlusTree &tree, BTreeNode *node, uint32_t index)
{
	ASSERT_PRINT(!node->is_leaf, &tree);
	uint32_t *children = get_children(tree, node);
	return get_node(children[index]);
}

static BTreeNode *
get_next(BTreeNode *node)
{
	return get_node(node->next);
}

static BTreeNode *
get_prev(BTreeNode *node)
{

	return get_node(node->previous);
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

	ASSERT_PRINT(!node->is_leaf, &tree);

	mark_dirty(node);
	uint32_t *children = get_children(tree, node);
	children[child_index] = node_index;

	if (node_index != 0)
	{
		BTreeNode *child_node = get_node(node_index);
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
	ASSERT_PRINT(root->index == tree.root_page_index, &tree);

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
				BTreeNode *child = get_node(children[i]);
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
		auto root = get_node(tree.root_page_index);
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
					BTreeNode *child = get_node(children[i]);
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
			   (parent->num_keys - parent_index) * sizeof(uint32_t));

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
		memcpy(GET_KEYS(right_node), get_key_at(tree, node, split_index), tree.node_key_size * right_node->num_keys);
		memcpy(get_record_data(tree, right_node), get_record_at(tree, node, split_index),
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
		memcpy(GET_KEYS(right_node), get_key_at(tree, node, split_index + 1),
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
		uint8_t *record_data = get_record_data(tree, node);
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
		uint8_t *keys = GET_KEYS(root);
		uint8_t *record_data = get_record_data(tree, root);

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
	BTreeNode *parent_node = get_parent(node);

	uint32_t *parent_children = get_children(tree, parent_node);
	uint32_t  parent_index;

	for (parent_index = 0; parent_children[parent_index] != node->index; parent_index++)
		;

	// Node should never be empty due to immediate repair_after_delete
	ASSERT_PRINT(node->num_keys > 0, &tree);

	BTreeNode *current_parent = parent_node;
	while (current_parent)
	{
		// we actually don't need to update the seperator key in the internal node, as it's just guiding.
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
do_delete(BPlusTree &tree, BTreeNode *node, const uint8_t *key, uint32_t index)
{
	if (node->parent == 0 && node->num_keys <= 1 && node->is_leaf)
	{
		mark_dirty(node);
		node->num_keys = 0;
		return;
	}

	ASSERT_PRINT(node->is_leaf, &tree);

	mark_dirty(node);

	uint8_t *record_data = get_record_data(tree, node);
	uint32_t shift_count = node->num_keys - index - 1;

	memcpy(get_key_at(tree, node, index), get_key_at(tree, node, index + 1), tree.node_key_size * shift_count);
	memcpy(record_data + index * tree.record_size, record_data + (index + 1) * tree.record_size,
		   tree.record_size * shift_count);

	node->num_keys--;

	if (index == 0 && node->parent != 0)
	{
		update_parent_keys(tree, node, key);
	}

	repair_after_delete(tree, node);
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

		uint8_t *node_records = get_record_data(tree, node);
		uint8_t *sibling_records = get_record_data(tree, right_sibling);

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
	ASSERT_PRINT(parent, &tree);
	uint32_t *parent_children = get_children(tree, parent);

	uint32_t node_index = 0;
	for (; node_index <= parent->num_keys; node_index++)
	{
		if (parent_children[node_index] == node->index)
		{
			break;
		}
	}

	BTreeNode *right_sibling = get_child(tree, parent, node_index + 1);
	ASSERT_PRINT(right_sibling, &tree);

	mark_dirty(node);
	mark_dirty(parent);

	if (node->is_leaf)
	{
		uint8_t *node_records = get_record_data(tree, node);
		uint8_t *sibling_records = get_record_data(tree, right_sibling);

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

		uint8_t *node_records = get_record_data(tree, node);
		uint8_t *sibling_records = get_record_data(tree, left_sibling);

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

// point

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

	clear_recurse(*tree, get_node(tree->root_page_index));
	return true;
}

/* ------ CURSOR -----------
 */
// Instead of FindResult struct and recursive find_containing_node:
static BTreeNode *
find_leaf(BPlusTree &tree, const uint8_t *key, uint32_t *out_index)
{
	BTreeNode *node = get_root(tree);

	while (!node->is_leaf)
	{
		uint32_t child_idx = binary_search(tree, node, key);
		node = get_child(tree, node, child_idx);
	}

	*out_index = binary_search(tree, node, key);
	return node;
}

static void
cursor_clear(BPtCursor *cursor)
{
	cursor->leaf_page = 0;
	cursor->leaf_index = 0;
	cursor->state = BPT_CURSOR_INVALID;
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
			ASSERT_PRINT(current, cursor->tree);
		}
	}

	if (left)
	{
		cursor->leaf_page = current->index;
		cursor->leaf_index = 0;
	}
	else
	{
		cursor->leaf_page = current->index;
		cursor->leaf_index = current->num_keys - 1;
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
seek_find(BPtCursor *cursor, const void *key);
bool
bplustree_cursor_seek(BPtCursor *cursor, const void *key, CompareOp op)
{
	bool exact_match_ok = (op == GE || op == LE);
	bool forward = (op == GE || op == GT);

	bool exact = seek_find(cursor, key);

	if (op == EQ)
	{
		return exact;
	}

	if (exact && exact_match_ok)
	{
		return true;
	}

	do
	{
		const uint8_t *current_key = bplustree_cursor_key(cursor);
		if (!current_key)
		{
			continue;
		}

		const uint8_t *key_bytes = reinterpret_cast<const uint8_t *>(key);

		bool satisfied = (op == GE && type_greater_equal(cursor->tree->node_key_type, current_key, key_bytes)) ||
						 (op == GT && type_greater_than(cursor->tree->node_key_type, current_key, key_bytes)) ||
						 (op == LE && type_less_equal(cursor->tree->node_key_type, current_key, key_bytes)) ||
						 (op == LT && type_less_than(cursor->tree->node_key_type, current_key, key_bytes));
		if (satisfied)
		{
			return true;
		}

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

	BTreeNode *node = get_node(cursor->leaf_page);
	if (!node || cursor->leaf_index >= node->num_keys)
	{
		return nullptr;
	}

	return get_key_at(*cursor->tree, node, cursor->leaf_index);
}

uint8_t *
bplustree_cursor_record(BPtCursor *cursor)
{
	if (cursor->state != BPT_CURSOR_VALID)
	{
		return nullptr;
	}

	BTreeNode *node = get_node(cursor->leaf_page);
	if (!node || cursor->leaf_index >= node->num_keys)
	{
		return nullptr;
	}

	return get_record_at(*cursor->tree, node, cursor->leaf_index);
}

bool
seek_find(BPtCursor *cursor, const void *key)
{
	cursor_clear(cursor);

	if (!cursor->tree->root_page_index)
	{
		cursor->state = BPT_CURSOR_INVALID;
		// Empty tree - cursor remains INVALID
		return false;
	}

	uint32_t   index;
	BTreeNode *leaf = find_leaf(*cursor->tree, (uint8_t *)key, &index);

	cursor->leaf_page = leaf->index;

	// Check for exact match
	bool found = index < leaf->num_keys &&
				 type_equals(cursor->tree->node_key_type, get_key_at(*cursor->tree, leaf, index), (uint8_t *)key);

	// Clamp index to valid range for iteration
	if (index >= leaf->num_keys)
	{
		cursor->leaf_index = leaf->num_keys - 1; // Position at last key
	}
	else
	{
		cursor->leaf_index = index; // Either exact match or insertion point
	}

	cursor->state = BPT_CURSOR_VALID;

	return found;
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

	auto	 node = get_node(cursor->leaf_page);
	uint32_t index = node->index;

	do_delete(*cursor->tree, node, key, cursor->leaf_index);

	// node will survive
	node = get_node(cursor->leaf_page);
	ASSERT_PRINT(node, cursor->tree);

	if (cursor->leaf_index >= node->num_keys)
	{
		if (node->num_keys > 0)
		{
			cursor->leaf_index = node->num_keys - 1;
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

	if (bplustree_cursor_seek(cursor, key))
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
	pager_mark_dirty(cursor->leaf_page);
	uint8_t *data = bplustree_cursor_record(cursor);
	memcpy(data, record, cursor->tree->record_size);
	return true;
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

	BTreeNode *node = get_node(cursor->leaf_page);
	if (!node)
	{
		cursor->state = BPT_CURSOR_FAULT;
		return false;
	}

	cursor->leaf_index++;

	if (cursor->leaf_index >= node->num_keys)
	{
		if (node->next != 0)
		{
			BTreeNode *next_node = get_next(node);
			if (next_node && next_node->num_keys > 0)
			{
				cursor->leaf_page = next_node->index;
				cursor->leaf_index = 0;
				return true;
			}
		}

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

	BTreeNode *node = get_node(cursor->leaf_page);
	if (!node)
	{
		cursor->state = BPT_CURSOR_FAULT;
		return false;
	}

	if (cursor->leaf_index > 0)
	{
		cursor->leaf_index--;
		return true;
	}

	// Move to previous leaf
	if (node->previous != 0)
	{
		BTreeNode *prev_node = get_prev(node);
		if (prev_node && prev_node->num_keys > 0)
		{
			cursor->leaf_page = prev_node->index;
			cursor->leaf_index = prev_node->num_keys - 1;
			return true;
		}
	}

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

/*NOCOVER_START*/

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
	ASSERT_PRINT(tree != nullptr, tree);

	// Empty tree is valid
	if (tree->root_page_index == 0)
	{
		return;
	}

	BTreeNode *root = get_root(*tree);
	ASSERT_PRINT(root != nullptr, tree);

	// Root specific checks
	ASSERT_PRINT(root->parent == 0, tree); // Root has no parent
	ASSERT_PRINT(root->index == tree->root_page_index, tree);

	// Track visited nodes to detect cycles
	std::unordered_set<uint32_t> visited;

	// Validate tree recursively
	ValidationResult result = validate_node_recursive(*tree, root, 0, nullptr, nullptr, visited);

	// If tree has data, verify leaf chain integrity
	if (root->is_leaf && root->num_keys > 0)
	{
		// Single leaf root should have no siblings
		ASSERT_PRINT(root->next == 0, tree);
		ASSERT_PRINT(root->previous == 0, tree);
	}
	else if (!root->is_leaf)
	{
		// Verify complete leaf chain by walking it
		BTreeNode					*first_leaf = result.leftmost_leaf;
		BTreeNode					*current = first_leaf;
		std::unordered_set<uint32_t> leaf_visited;

		ASSERT_PRINT(current->previous == 0, tree); // First leaf has no previous

		while (current)
		{
			ASSERT_PRINT(current->is_leaf, tree);
			ASSERT_PRINT(leaf_visited.find(current->index) == leaf_visited.end(), tree); // No cycles in leaf chain
			leaf_visited.insert(current->index);

			if (current->next != 0)
			{
				BTreeNode *next = get_next(current);
				ASSERT_PRINT(next != nullptr, tree);
				ASSERT_PRINT(next->previous == current->index, tree); // Bidirectional link integrity
				current = next;
			}
			else
			{
				ASSERT_PRINT(current == result.rightmost_leaf, tree); // Last leaf matches rightmost
				break;
			}
		}
	}
}
static ValidationResult
validate_node_recursive(BPlusTree &tree, BTreeNode *node, uint32_t expected_parent, uint8_t *parent_min_bound,
						uint8_t *parent_max_bound, std::unordered_set<uint32_t> &visited)
{
	ASSERT_PRINT(node != nullptr, &tree);

	// Check for cycles
	ASSERT_PRINT(visited.find(node->index) == visited.end(), &tree);
	visited.insert(node->index);

	// Verify parent pointer
	ASSERT_PRINT(node->parent == expected_parent, &tree);

	// Check key count constraints
	uint32_t max_keys = get_max_keys(tree, node);
	uint32_t min_keys = get_min_keys(tree, node);

	ASSERT_PRINT(node->num_keys <= max_keys, &tree);

	// Non-root nodes must meet minimum
	if (expected_parent != 0)
	{
		ASSERT_PRINT(node->num_keys >= min_keys, &tree);
	}
	else
	{
		// Root can have fewer, but not zero (unless tree is being cleared)
		if (node->num_keys == 0)
		{
			ASSERT_PRINT(node->is_leaf, &tree); // Only leaf root can be empty during deletion
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
			ASSERT_PRINT(type_less_than(tree.node_key_type, prev_key, current_key), &tree); // prev < current
		}

		// Check bounds from parent
		if (parent_min_bound)
		{
			ASSERT_PRINT(type_greater_equal(tree.node_key_type, current_key, parent_min_bound), &tree);
		}
		if (parent_max_bound)
		{
			ASSERT_PRINT(type_less_than(tree.node_key_type, current_key, parent_max_bound), &tree);
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
		uint8_t *records = get_record_data(tree, node);
		ASSERT_PRINT(records != nullptr, &tree);

		// Verify leaf chain pointers are valid page indices or 0
		if (node->next != 0)
		{
			ASSERT_PRINT(node->next != node->index, &tree); // No self-reference
			BTreeNode *next = get_next(node);
			ASSERT_PRINT(next != nullptr, &tree);
			ASSERT_PRINT(next->is_leaf, &tree);
		}
		if (node->previous != 0)
		{
			ASSERT_PRINT(node->previous != node->index, &tree); // No self-reference
			BTreeNode *prev = get_prev(node);
			ASSERT_PRINT(prev != nullptr, &tree);
			ASSERT_PRINT(prev->is_leaf, &tree);
		}
	}
	else
	{
		// Internal node validation
		uint32_t *children = get_children(tree, node);
		ASSERT_PRINT(children != nullptr, &tree);

		uint32_t   child_depth = UINT32_MAX;
		BTreeNode *leftmost_leaf = nullptr;
		BTreeNode *rightmost_leaf = nullptr;

		// Internal nodes have num_keys + 1 children
		for (uint32_t i = 0; i <= node->num_keys; i++)
		{
			ASSERT_PRINT(children[i] != 0, &tree);			 // No null children
			ASSERT_PRINT(children[i] != node->index, &tree); // No self-reference

			BTreeNode *child = get_child(tree, node, i);
			ASSERT_PRINT(child != nullptr, &tree);

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
				ASSERT_PRINT(child_depth == child_result.depth, &tree);
			}

			// Track rightmost leaf
			rightmost_leaf = child_result.rightmost_leaf;

			// Verify key bounds match child contents
			if (child_result.min_key && i > 0)
			{
				// First key in child >= separator key before it
				uint8_t *separator = get_key_at(tree, node, i - 1);
				ASSERT_PRINT(type_greater_equal(tree.node_key_type, child_result.min_key, separator), &tree);
			}
			if (child_result.max_key && i < node->num_keys)
			{
				// Last key in child < separator key after it
				uint8_t *separator = get_key_at(tree, node, i);
				ASSERT_PRINT(type_less_equal(tree.node_key_type, child_result.max_key, separator), &tree);
			}
		}

		result.depth = child_depth + 1;
		result.leftmost_leaf = leftmost_leaf;
		result.rightmost_leaf = rightmost_leaf;

		// Internal nodes should not have leaf chain pointers
		ASSERT_PRINT(node->next == 0, &tree);
		ASSERT_PRINT(node->previous == 0, &tree);
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
	type_print(tree.node_key_type, key);
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
	printf("Key type: %s, Record size: %u bytes\n", type_name(tree->node_key_size), tree->record_size);
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

			BTreeNode *node = get_node(page_index);
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

		BTreeNode *node = get_node(page_index);
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
/*NOCOVER_END*/
