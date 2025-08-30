
#include "btree.hpp"
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



// B+Tree node structure -
// fits in a single page


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
repair_after_delete(BTree &tree, BTreeNode *node);



static BTreeNode *
get_root(BTree &tree)
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
get_key_at(BTree &tree, BTreeNode *node, uint32_t index)
{
	return get_keys(node) + index * tree.node_key_size;
}

static uint32_t *
get_children(BTree &tree, BTreeNode *node)
{
	if (node->is_leaf)
	{
		return nullptr;
	}

	return reinterpret_cast<uint32_t *>(node->data + tree.max_keys * tree.node_key_size +
										tree.max_keys * tree.record_size);
}


static uint8_t *
get_records(BTree &tree, BTreeNode *node)
{
	return node->data + tree.max_keys * tree.node_key_size;
}

static uint8_t *
get_record_at(BTree &tree, BTreeNode *node, uint32_t index)
{
    return get_records(tree, node) + (index * tree.record_size);
}

static uint32_t
binary_search(BTree &tree, BTreeNode *node, const uint8_t *key)
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
			return mid;
		}
		else
		{
			right = mid;
		}
	}

	return left;
}

static BTreeNode *
create_node(BTree &tree, bool is_leaf)
{
	uint32_t   page_index = pager_new();
	BTreeNode *node = reinterpret_cast<BTreeNode *>(pager_get(page_index));

	node->index = page_index;
	node->parent = 0;

	node->num_keys = 0;
	node->is_leaf = is_leaf ? 1 : 0;

	mark_dirty(node);
	return node;
}

BTree
btree_create(DataType key, uint32_t record_size, bool init)
{
	BTree tree = {0};
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

	uint32_t key_record_size = tree.node_key_size + record_size;
	uint32_t child_ptr_size = TYPE_4;

	uint32_t max_keys = (USABLE_SPACE - child_ptr_size) / (key_record_size + child_ptr_size);

	uint32_t min_keys;
	if (max_keys % 2 == 0)
	{
		min_keys = (max_keys / 2) - 1;
	}
	else
	{
		min_keys = (max_keys) / 2;
	}

	max_keys = std::max((uint32_t)MIN_ENTRY_COUNT, max_keys);

	uint32_t split_index = max_keys / 2;

	tree.max_keys = max_keys;
	tree.min_keys = min_keys;
	tree.split_index = split_index;

	if (init)
	{
		BTreeNode *root = create_node(tree, true);
		tree.root_page_index = root->index;
	}

	return tree;
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
get_child(BTree &tree, BTreeNode *node, uint32_t index)
{
	if (!node || node->is_leaf)
	{
		return nullptr;
	}
	uint32_t *children = get_children(tree, node);
	return reinterpret_cast<BTreeNode *>(pager_get(children[index]));
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
swap_with_root(BTree &tree, BTreeNode *root, BTreeNode *other)
{
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

	mark_dirty(root);
	mark_dirty(other);
}

static void
set_child(BTree &tree, BTreeNode *node, uint32_t child_index, uint32_t node_index)
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
destroy_node(BTreeNode *node)
{
	pager_delete(node->index);
}

static bool
is_match(BTree &tree, BTreeNode *node, uint32_t index, const uint8_t *key)
{
	return cmp(tree.node_key_size, get_key_at(tree, node, index), key) == 0;
}

static BTreeNode *
split(BTree &tree, BTreeNode *node)
{
	BTreeNode *right_node = create_node(tree, node->is_leaf);
	uint32_t   split_index = tree.split_index;
	uint8_t	  *rising_key = get_key_at(tree, node, split_index);

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

		set_child(tree, root, 0, new_internal->index);

		parent = root;		 // Continue with root as the parent
		node = new_internal; // node now points to where its data actually is
		rising_key = get_key_at(tree, node, split_index);
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

		uint8_t *parent_records = get_records(tree, parent);
		memcpy(parent_records + (parent_index + 1) * tree.record_size, parent_records + parent_index * tree.record_size,
			   (parent->num_keys - parent_index) * tree.record_size);
	}

	memcpy(get_key_at(tree, parent, parent_index), rising_key, tree.node_key_size);
	set_child(tree, parent, parent_index + 1, right_node->index);
	parent->num_keys++;

	uint8_t *parent_records = get_records(tree, parent);
	uint8_t *source_records = get_records(tree, node);
	memcpy(parent_records + parent_index * tree.record_size, source_records + split_index * tree.record_size,
		   tree.record_size);

	right_node->num_keys = node->num_keys - split_index - 1;
	memcpy(get_keys(right_node), get_key_at(tree, node, split_index + 1), right_node->num_keys * tree.node_key_size);

	uint8_t *src_records = get_records(tree, node);
	uint8_t *dst_records = get_records(tree, right_node);
	memcpy(dst_records, src_records + (split_index + 1) * tree.record_size, right_node->num_keys * tree.record_size);

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

	node->num_keys = split_index;
	return parent;
}

static void
insert_repair(BTree &tree, BTreeNode *node)
{
	if (node->num_keys < tree.max_keys)
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
insert(BTree &tree, BTreeNode *node, uint8_t *key, const uint8_t *data)
{
	if (node->is_leaf)
	{
		uint8_t *record_data = get_records(tree, node);
		uint32_t insert_index = binary_search(tree, node, key);


		if (node->num_keys >= tree.max_keys)
		{
			insert_repair(tree, node);
			return false;
		}

		mark_dirty(node);

		while (insert_index < node->num_keys && is_match(tree, node, insert_index, key))
		{
			insert_index++;
		}

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
insert_element(BTree &tree, void *key, const uint8_t *data)
{
	if (tree.root_page_index == 0)
	{
		BTreeNode *root = create_node(tree, true);
		tree.root_page_index = root->index;
	}

	BTreeNode *root = get_root(tree);

	if (root->num_keys == 0)
	{
		mark_dirty(root);
		uint8_t *keys = get_keys(root);
		uint8_t *record_data = get_records(tree, root);

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
do_delete_btree(BTree &tree, BTreeNode *node, const uint8_t *key, uint32_t i)
{
	if (node->is_leaf)
	{
		mark_dirty(node);
		uint8_t *record_data = get_records(tree, node);

		uint32_t shift_count = node->num_keys - i - 1;

		memcpy(get_key_at(tree, node, i), get_key_at(tree, node, i + 1), tree.node_key_size * shift_count);
		memcpy(record_data + i * tree.record_size, record_data + (i + 1) * tree.record_size,
			   tree.record_size * shift_count);

		node->num_keys--;
		repair_after_delete(tree, node);
	}
	else
	{
		uint32_t   index = i;
		BTreeNode *curr = get_child(tree, node, index);

		while (!curr->is_leaf)
		{
			curr = get_child(tree, curr, curr->num_keys);
		}

		uint32_t pred_index = curr->num_keys - 1;
		uint8_t *pred_key = get_key_at(tree, curr, pred_index);
		uint8_t *pred_record = get_record_at(tree, curr, pred_index);

		mark_dirty(node);
		memcpy(get_key_at(tree, node, index), pred_key, tree.node_key_size);
		uint8_t *internal_records = get_records(tree, node);
		memcpy(internal_records + index * tree.record_size, pred_record, tree.record_size);

		mark_dirty(curr);
		uint8_t *leaf_records = get_records(tree, curr);

		uint32_t elements_to_shift = curr->num_keys - 1 - pred_index;
		memcpy(get_key_at(tree, curr, pred_index), get_key_at(tree, curr, pred_index + 1),
			   elements_to_shift * tree.node_key_size);
		memcpy(leaf_records + pred_index * tree.record_size, leaf_records + (pred_index + 1) * tree.record_size,
			   elements_to_shift * tree.record_size);

		curr->num_keys--;
		repair_after_delete(tree, curr);
	}
}

static void
do_delete(BTree &tree, BTreeNode *node, const uint8_t *key, uint32_t index)
{
	if (node->parent == 0 && node->num_keys <= 1 && node->is_leaf)
	{
		mark_dirty(node);
		node->num_keys = 0;
		return;
	}

	do_delete_btree(tree, node, key, index);
}

static BTreeNode *
steal_from_right(BTree &tree, BTreeNode *node, uint32_t parent_index)
{
	BTreeNode *parent_node = get_parent(node);
	BTreeNode *right_sibling = get_child(tree, parent_node, parent_index + 1);

	mark_dirty(node);
	mark_dirty(parent_node);
	mark_dirty(right_sibling);

	if (node->is_leaf)
	{
		memcpy(get_key_at(tree, node, node->num_keys), get_key_at(tree, right_sibling, 0), tree.node_key_size);

		uint8_t *node_records = get_records(tree, node);
		uint8_t *sibling_records = get_records(tree, right_sibling);

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

		uint8_t *node_records = get_records(tree, node);
		uint8_t *parent_records = get_records(tree, parent_node);
		uint8_t *sibling_records = get_records(tree, right_sibling);

		memcpy(node_records + node->num_keys * tree.record_size, parent_records + parent_index * tree.record_size,
			   tree.record_size);

		memcpy(get_key_at(tree, parent_node, parent_index), get_key_at(tree, right_sibling, 0), tree.node_key_size);
		memcpy(parent_records + parent_index * tree.record_size, sibling_records, tree.record_size);

		uint32_t shift_count = right_sibling->num_keys - 1;
		memcpy(sibling_records, sibling_records + tree.record_size, shift_count * tree.record_size);

		shift_count = right_sibling->num_keys - 1;
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
merge_right(BTree &tree, BTreeNode *node)
{
	BTreeNode *parent = get_parent(node);
	if (!parent)
		return node;

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
		uint8_t *node_records = get_records(tree, node);
		uint8_t *sibling_records = get_records(tree, right_sibling);

		memcpy(get_key_at(tree, node, node->num_keys), get_key_at(tree, right_sibling, 0),
			   right_sibling->num_keys * tree.node_key_size);
		memcpy(node_records + node->num_keys * tree.record_size, sibling_records,
			   right_sibling->num_keys * tree.record_size);

		node->num_keys += right_sibling->num_keys;
	}
	else
	{
		memcpy(get_key_at(tree, node, node->num_keys), get_key_at(tree, parent, node_index), tree.node_key_size);

		uint8_t *node_records = get_records(tree, node);
		uint8_t *parent_records = get_records(tree, parent);
		uint8_t *sibling_records = get_records(tree, right_sibling);

		memcpy(node_records + node->num_keys * tree.record_size, parent_records + node_index * tree.record_size,
			   tree.record_size);

		memcpy(node_records + (node->num_keys + 1) * tree.record_size, sibling_records,
			   right_sibling->num_keys * tree.record_size);

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

	if (!parent->is_leaf)
	{
		uint8_t *parent_records = get_records(tree, parent);
		memcpy(parent_records + node_index * tree.record_size, parent_records + (node_index + 1) * tree.record_size,
			   shift_count * tree.record_size);
	}

	parent->num_keys--;

	destroy_node(right_sibling);

	if (parent->num_keys < tree.min_keys)
	{
		if (parent->parent == 0 && parent->num_keys == 0)
		{
			swap_with_root(tree, parent, node);
			destroy_node(parent); // Destroy the old root (now in parent's location)
			return node;		  // Return node which is now the root
		}
		else
		{
			repair_after_delete(tree, parent);
		}
	}

	return node;
}

static BTreeNode *
steal_from_left(BTree &tree, BTreeNode *node, uint32_t parent_index)
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

		uint8_t *node_records = get_records(tree, node);
		uint8_t *sibling_records = get_records(tree, left_sibling);

		memcpy(node_records + tree.record_size, node_records, node->num_keys * tree.record_size);
		memcpy(node_records, sibling_records + (left_sibling->num_keys - 1) * tree.record_size, tree.record_size);
		memcpy(get_key_at(tree, parent_node, parent_index - 1), get_key_at(tree, node, 0), tree.node_key_size);
	}
	else
	{
		memcpy(get_key_at(tree, node, 0), get_key_at(tree, parent_node, parent_index - 1), tree.node_key_size);

		uint8_t *node_records = get_records(tree, node);
		uint8_t *parent_records = get_records(tree, parent_node);
		uint8_t *sibling_records = get_records(tree, left_sibling);

		memcpy(node_records + tree.record_size, node_records, node->num_keys * tree.record_size);

		memcpy(node_records, parent_records + (parent_index - 1) * tree.record_size, tree.record_size);

		memcpy(get_key_at(tree, parent_node, parent_index - 1),
			   get_key_at(tree, left_sibling, left_sibling->num_keys - 1), tree.node_key_size);
		memcpy(parent_records + (parent_index - 1) * tree.record_size,
			   sibling_records + (left_sibling->num_keys - 1) * tree.record_size, tree.record_size);

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
repair_after_delete(BTree &tree, BTreeNode *node)
{
	if (node->num_keys >= tree.min_keys)
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
				destroy_node(node); // Destroy the old root (now at node's location)
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
		if (left_sibling && left_sibling->num_keys > tree.min_keys)
		{
			steal_from_left(tree, node, node_index);
			return;
		}
	}

	if (node_index < parent->num_keys)
	{
		BTreeNode *right_sibling = get_child(tree, parent, node_index + 1);
		if (right_sibling && right_sibling->num_keys > tree.min_keys)
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
clear_recurse(BTree &tree, BTreeNode *node)
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
btree_clear(BTree *tree)
{
	clear_recurse(*tree, reinterpret_cast<BTreeNode *>(pager_get(tree->root_page_index)));
	return true;
}

/* ------ CURSOR -----------
 */

// Internal cursor helper
// functions
static void
cursor_push_stack(BtCursor *cursor, uint32_t page, uint32_t index, uint32_t child_pos)
{
	if (cursor->path.stack_depth < MAX_BTREE_DEPTH)
	{
		cursor->path.page_stack[cursor->path.stack_depth] = page;
		cursor->path.index_stack[cursor->path.stack_depth] = index;
		cursor->path.child_stack[cursor->path.stack_depth] = child_pos;
		cursor->path.stack_depth++;
	}
}

static FindResult
find_containing_node(BTree &tree, BTreeNode *node, const uint8_t *key, BtCursor *cursor)
{
	uint32_t index = binary_search(tree, node, key);
	auto	 stack = &cursor->path;

	if (node->is_leaf)
	{
		if (stack)
		{
			stack->page_stack[stack->stack_depth] = node->index;
			stack->index_stack[stack->stack_depth] = index;
			stack->child_stack[stack->stack_depth] = index;
			stack->stack_depth++;
		}

		return {node, index, (index < node->num_keys && is_match(tree, node, index, key))};
	}

	// For internal nodes in
	// B-tree, check if key
	// matches
	// if (tree.tree_type == BTREE) {
	// 	if (index < node->num_keys &&
	// 	    is_match(tree, node, index, key)) {
	// 		if (stack) {
	// 			stack->page_stack[stack->stack_depth] =
	// 			    node->index;
	// 			stack->index_stack[stack->stack_depth] = index;
	// 			stack->child_stack[stack->stack_depth] = index;
	// 			stack->stack_depth++;
	// 		}
	// 		return {node, index, true};
	// 	}
	// }

	// Need to descend to child
	uint32_t child_pos = index;

	// Add current node to stack
	// BEFORE descending
	if (stack)
	{
		stack->page_stack[stack->stack_depth] = node->index;
		stack->index_stack[stack->stack_depth] = (child_pos > 0) ? child_pos - 1 : 0;
		stack->child_stack[stack->stack_depth] = child_pos;
		stack->stack_depth++;
	}

	BTreeNode *child = get_child(tree, node, child_pos);
	if (!child)
	{
		return {node, index, false};
	}

	return find_containing_node(tree, child, key, cursor);
}

static void
cursor_pop_stack(BtCursor *cursor)
{
	if (cursor->path.stack_depth > 0)
	{
		cursor->path.stack_depth--;
		cursor->path.current_page = cursor->path.page_stack[cursor->path.stack_depth];
		cursor->path.current_index = cursor->path.index_stack[cursor->path.stack_depth];
	}
}

static void
cursor_clear(BtCursor *cursor)
{
	cursor->path.stack_depth = 0;
	cursor->path.current_page = 0;
	cursor->path.current_index = 0;
	cursor->state = CURSOR_INVALID;
}

static void
cursor_save_state(BtCursor *cursor)
{
	cursor->saved = cursor->path;
}

static void
cursor_restore_state(BtCursor *cursor)
{
	cursor->path = cursor->saved;
}

static bool
cursor_move_in_subtree(BtCursor *cursor, BTreeNode *root, bool left)
{
	BTreeNode *current = root;

	while (!current->is_leaf)
	{
		if (left)
		{
			cursor_push_stack(cursor, current->index, 0, 0);
			current = get_child(*cursor->tree, current, 0);
		}
		else
		{
			uint32_t child_pos = current->num_keys;
			cursor_push_stack(cursor, current->index, current->num_keys - 1, child_pos);
			current = get_child(*cursor->tree, current, child_pos);
		}

		if (!current)
		{
			cursor->state = CURSOR_FAULT;
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
	cursor->state = CURSOR_VALID;
	return true;
}

static bool
cursor_move_to_leftmost_in_subtree(BtCursor *cursor, BTreeNode *root)
{
	return cursor_move_in_subtree(cursor, root, true);
}

static bool
cursor_move_to_rightmost_in_subtree(BtCursor *cursor, BTreeNode *root)
{
	return cursor_move_in_subtree(cursor, root, false);
}

static bool
cursor_move_end(BtCursor *cursor, bool first)
{
	cursor_clear(cursor);

	BTreeNode *root = get_root(*cursor->tree);
	if (!root || root->num_keys == 0)
	{
		cursor->state = CURSOR_INVALID;
		return false;
	}

	return cursor_move_in_subtree(cursor, root, first);
}

bool
btree_cursor_seek_cmp(BtCursor *cursor, const void *key, CompareOp op)
{
	bool exact_match_ok = (op == GE || op == LE);
	bool forward = (op == GE || op == GT);

	if (exact_match_ok && btree_cursor_seek(cursor, key))
	{
		return true;
	}
	else
	{
		btree_cursor_seek(cursor, key);
	}

	do
	{
		const uint8_t *current_key = btree_cursor_key(cursor);
		if (!current_key)
			continue;

		int cmp_result = cmp(cursor->tree->node_key_size, current_key, reinterpret_cast<const uint8_t *>(key));

		bool satisfied = (op == GE && cmp_result >= 0) || (op == GT && cmp_result > 0) ||
						 (op == LE && cmp_result <= 0) || (op == LT && cmp_result < 0);
		if (satisfied)
			return true;
	} while (forward ? btree_cursor_next(cursor) : btree_cursor_previous(cursor));

	return false;
}

// Public cursor functions
bool
btree_cursor_is_valid(BtCursor *cursor)
{
	return cursor->state == CURSOR_VALID;
}

uint8_t *
btree_cursor_key(BtCursor *cursor)
{
	if (cursor->state != CURSOR_VALID)
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
btree_cursor_record(BtCursor *cursor)
{
	if (cursor->state != CURSOR_VALID)
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
btree_cursor_seek(BtCursor *cursor, const void *key)
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
		cursor->state = CURSOR_VALID;
		return true;
	}

	cursor->state = (result.index < result.node->num_keys) ? CURSOR_VALID : CURSOR_INVALID;
	return false;
}

bool
btree_cursor_delete(BtCursor *cursor)
{
	if (cursor->state != CURSOR_VALID)
	{
		return false;
	}

	const uint8_t *key = btree_cursor_key(cursor);
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
		cursor->state = CURSOR_INVALID;
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
			cursor->state = CURSOR_INVALID;
		}
	}

	return true;
}

bool
btree_cursor_insert(BtCursor *cursor, const void *key, const uint8_t *record)
{
	insert_element(*cursor->tree, const_cast<void *>(key), record);
	return true;
}

bool
btree_cursor_update(BtCursor *cursor, const uint8_t *record)
{
	if (cursor->state != CURSOR_VALID)
	{
		return false;
	}
	pager_mark_dirty(cursor->path.current_page);
	uint8_t *data = btree_cursor_record(cursor);
	memcpy(data, record, cursor->tree->record_size);
	return true;
}

bool
btree_cursor_seek_ge(BtCursor *cursor, const void *key)
{
	return btree_cursor_seek_cmp(cursor, key, GE);
}

bool
btree_cursor_seek_gt(BtCursor *cursor, const void *key)
{
	return btree_cursor_seek_cmp(cursor, key, GT);
}

bool
btree_cursor_seek_le(BtCursor *cursor, const void *key)
{
	return btree_cursor_seek_cmp(cursor, key, LE);
}

bool
btree_cursor_seek_lt(BtCursor *cursor, const void *key)
{
	return btree_cursor_seek_cmp(cursor, key, LT);
}

bool
btree_cursor_first(BtCursor *cursor)
{
	return cursor_move_end(cursor, true);
}

bool
btree_cursor_last(BtCursor *cursor)
{
	return cursor_move_end(cursor, false);
}

bool
btree_cursor_next(BtCursor *cursor)
{
	if (cursor->state != CURSOR_VALID)
	{
		return false;
	}

	BTreeNode *node = reinterpret_cast<BTreeNode *>(pager_get(cursor->path.current_page));
	if (!node)
	{
		cursor->state = CURSOR_FAULT;
		return false;
	}

	cursor_save_state(cursor);

	// BTREE traversal
	if (node->is_leaf)
	{
		cursor->path.current_index++;
		if (cursor->path.current_index < node->num_keys)
		{
			return true;
		}

		// Need to go up to find
		// next key
		while (cursor->path.stack_depth > 0)
		{
			uint32_t child_pos = cursor->path.child_stack[cursor->path.stack_depth - 1];

			cursor_pop_stack(cursor);

			if (cursor->path.stack_depth == 0)
			{
				cursor_restore_state(cursor);
				return false;
			}

			BTreeNode *parent = reinterpret_cast<BTreeNode *>(pager_get(cursor->path.current_page));

			// In B-tree, after
			// visiting left subtree
			// (child i), visit key
			// i
			if (child_pos <= cursor->path.current_index)
			{
				cursor->state = CURSOR_VALID;
				return true;
			}

			// After visiting key i,
			// go to child i+1
			if (child_pos == cursor->path.current_index + 1 && child_pos <= parent->num_keys)
			{
				cursor->path.current_index++;
				if (cursor->path.current_index < parent->num_keys)
				{
					return true;
				}
			}
		}
	}
	else
	{
		// Internal node - we need
		// to visit right subtree
		uint32_t   next_child = cursor->path.current_index + 1;
		BTreeNode *child = get_child(*cursor->tree, node, next_child);
		if (child)
		{
			cursor_push_stack(cursor, node->index, cursor->path.current_index, next_child);
			return cursor_move_to_leftmost_in_subtree(cursor, child);
		}
	}

	cursor_restore_state(cursor);
	return false;
}

bool
btree_cursor_previous(BtCursor *cursor)
{
	if (cursor->state != CURSOR_VALID)
	{
		return false;
	}

	BTreeNode *node = reinterpret_cast<BTreeNode *>(pager_get(cursor->path.current_page));
	if (!node)
	{
		cursor->state = CURSOR_FAULT;
		return false;
	}

	cursor_save_state(cursor);

	// If leaf node in B+tree,
	// use previous pointer for
	// efficiency

	// For B-tree traversal
	if (node->is_leaf)
	{
		if (cursor->path.current_index > 0)
		{
			cursor->path.current_index--;
			return true;
		}

		// Need to go up to find
		// previous key
		while (cursor->path.stack_depth > 0)
		{
			uint32_t child_pos = cursor->path.child_stack[cursor->path.stack_depth - 1];

			cursor_pop_stack(cursor);

			if (cursor->path.stack_depth == 0)
			{
				cursor_restore_state(cursor);
				return false;
			}

			BTreeNode *parent = reinterpret_cast<BTreeNode *>(pager_get(cursor->path.current_page));
			if (!parent)
			{
				cursor_restore_state(cursor);
				return false;
			}

			// In B-tree, after
			// visiting right
			// subtree (child i+1),
			// visit key i
			if (child_pos > 0 && child_pos == cursor->path.current_index + 1)
			{
				cursor->state = CURSOR_VALID;
				return true;
			}

			// If we came from child
			// 0, need to continue
			// up
			if (child_pos == 0)
			{
				continue;
			}

			// Move to previous
			// key's right subtree
			if (cursor->path.current_index > 0)
			{
				cursor->path.current_index--;
				BTreeNode *child = get_child(*cursor->tree, parent, cursor->path.current_index + 1);
				if (child)
				{
					cursor_push_stack(cursor, parent->index, cursor->path.current_index,
									  cursor->path.current_index + 1);
					return cursor_move_to_rightmost_in_subtree(cursor, child);
				}
			}
		}
	}
	else
	{
		// Internal node - visit
		// left subtree's
		// rightmost
		BTreeNode *child = get_child(*cursor->tree, node, cursor->path.current_index);
		if (child)
		{
			cursor_push_stack(cursor, node->index, cursor->path.current_index, cursor->path.current_index);
			return cursor_move_to_rightmost_in_subtree(cursor, child);
		}
	}

	cursor_restore_state(cursor);
	return false;
}

bool
btree_cursor_has_next(BtCursor *cursor)
{
	if (btree_cursor_next(cursor))
	{
		btree_cursor_previous(cursor);
		return true;
	}
	return false;
}

bool
btree_cursor_has_previous(BtCursor *cursor)
{
	if (btree_cursor_previous(cursor))
	{
		btree_cursor_next(cursor);
		return true;
	}
	return false;
}

// wrap pager

bool
btree_cursor_seek_exact(BtCursor *cursor, const void *key, const uint8_t *record)
{
	// First seek to key
	if (!btree_cursor_seek(cursor, key))
	{
		return false;
	}

	// Then scan through duplicates
	while (btree_cursor_is_valid(cursor))
	{
		uint8_t *current_key = btree_cursor_key(cursor);
		if (cmp(cursor->tree->node_key_size, current_key, (uint8_t *)key) != 0)
		{
			break; // Passed all duplicates
		}

		uint8_t *current_record = btree_cursor_record(cursor);
		if (memcmp(current_record, record, cursor->tree->record_size) == 0)
		{
			return true; // Found exact match
		}

		if (!btree_cursor_next(cursor))
		{
			break;
		}
	}

	return false;
}
