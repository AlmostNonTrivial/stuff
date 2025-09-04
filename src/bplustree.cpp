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

// B+Tree node structure - fits in a single page
struct BTreeNode
{
	// Node header (24 bytes)
	uint32_t index;		 // Page index
	uint32_t parent;	 // Parent page index (0 if root)
	uint32_t next;		 // Next sibling (for leaf nodes)
	uint32_t previous;	 // Previous sibling (for leaf nodes)
	uint32_t num_keys;	 // Number of keys in this node
	uint8_t	 is_leaf;	 // 1 if leaf, 0 if internal
	uint8_t	 padding[3]; // Alignment padding
	// Data area - stores keys, children pointers, and data
	// Layout for internal nodes: [keys][children]
	// Layout for leaf nodes: [keys][records]
	uint8_t data[NODE_DATA_SIZE];
};

static_assert(sizeof(BTreeNode) == PAGE_SIZE, "BTreeNode must be exactly PAGE_SIZE");

// Internal function declarations
static void
repair_after_delete(BPlusTree &tree, BTreeNode *node);

// ============================================================================
// ACCESSOR MACROS - Direct memory layout access
// ============================================================================

// Node type predicates
#define IS_LEAF(node)	  ((node)->is_leaf)
#define IS_INTERNAL(node) (!(node)->is_leaf)
#define IS_ROOT(node)	  ((node)->parent == 0)

// Node capacity
#define GET_MAX_KEYS(tree, node) ((node)->is_leaf ? (tree).leaf_max_keys : (tree).internal_max_keys)
#define GET_MIN_KEYS(tree, node) ((node)->is_leaf ? (tree).leaf_min_keys : (tree).internal_min_keys)
#define GET_SPLIT_INDEX(tree, node) ((node)->is_leaf ? (tree).leaf_split_index : (tree).internal_split_index)

// Node retrieval
#define GET_NODE(index) (reinterpret_cast<BTreeNode *>(pager_get(index)))
#define GET_ROOT(tree)	 GET_NODE((tree).root_page_index)
#define GET_PARENT(node) GET_NODE((node)->parent)

// Node navigation
#define GET_NEXT(node) GET_NODE((node)->next)
#define GET_PREV(node) GET_NODE((node)->previous)

// Memory layout accessors
#define GET_KEY_AT(tree, node, idx) ((node)->data + (idx) * (tree).node_key_size)

#define GET_CHILDREN(tree, node)                                                                                       \
	(reinterpret_cast<uint32_t *>((node)->data + (tree).internal_max_keys * (tree).node_key_size))
#define GET_CHILD(tree, node, idx) GET_NODE(GET_CHILDREN(tree, node)[idx])
#define GET_RECORD_DATA(tree, node) ((node)->data + (tree).leaf_max_keys * (tree).node_key_size)
#define GET_RECORD_AT(tree, node, idx) (GET_RECORD_DATA(tree, node) + (idx) * (tree).record_size)

// Node state checks
#define NODE_IS_FULL(tree, node)	((node)->num_keys >= GET_MAX_KEYS(tree, node))
#define NODE_IS_MINIMAL(tree, node) ((node)->num_keys <= GET_MIN_KEYS(tree, node))
#define NODE_CAN_SPARE(tree, node)	((node)->num_keys > GET_MIN_KEYS(tree, node))

// Utility macros
#define MARK_DIRTY(node) pager_mark_dirty((node)->index)
#define ASSERT_PRINT(condition, tree)                                                                                  \
	if (!(condition))                                                                                                  \
	{                                                                                                                  \
		bplustree_print(tree);                                                                                         \
		assert(condition);                                                                                             \
	}


// ============================================================================
// MEMORY OPERATION HELPERS - Core shift and copy operations
// ============================================================================

// Shift keys right within a node (creates gap at from_idx)
static void
shift_keys_right(BPlusTree &tree, BTreeNode *node, uint32_t from_idx, uint32_t count)
{
	if (count == 0)
		return;
	uint8_t *src = GET_KEY_AT(tree, node, from_idx);
	uint8_t *dst = GET_KEY_AT(tree, node, from_idx + 1);
	memcpy(dst, src, count * tree.node_key_size);
}

// Shift keys left within a node (removes gap at from_idx)
static void
shift_keys_left(BPlusTree &tree, BTreeNode *node, uint32_t from_idx, uint32_t count)
{
	if (count == 0)
		return;
	uint8_t *dst = GET_KEY_AT(tree, node, from_idx);
	uint8_t *src = GET_KEY_AT(tree, node, from_idx + 1);
	memcpy(dst, src, count * tree.node_key_size);
}

// Shift records right in a leaf node
static void
shift_records_right(BPlusTree &tree, BTreeNode *node, uint32_t from_idx, uint32_t count)
{
	if (count == 0)
		return;
	uint8_t *base = GET_RECORD_DATA(tree, node);
	uint8_t *src = base + from_idx * tree.record_size;
	uint8_t *dst = base + (from_idx + 1) * tree.record_size;
	memcpy(dst, src, count * tree.record_size);
}

// Shift records left in a leaf node
static void
shift_records_left(BPlusTree &tree, BTreeNode *node, uint32_t from_idx, uint32_t count)
{
	if (count == 0)
		return;
	uint8_t *base = GET_RECORD_DATA(tree, node);
	uint8_t *dst = base + from_idx * tree.record_size;
	uint8_t *src = base + (from_idx + 1) * tree.record_size;
	memcpy(dst, src, count * tree.record_size);
}

// Shift children right in an internal node
static void
shift_children_right(BPlusTree &tree, BTreeNode *node, uint32_t from_idx, uint32_t count)
{
	if (count == 0)
		return;
	uint32_t *children = GET_CHILDREN(tree, node);
	memcpy(&children[from_idx + 1], &children[from_idx], count * sizeof(uint32_t));
}

// Shift children left in an internal node
static void
shift_children_left(BPlusTree &tree, BTreeNode *node, uint32_t from_idx, uint32_t count)
{
	if (count == 0)
		return;
	uint32_t *children = GET_CHILDREN(tree, node);
	memcpy(&children[from_idx], &children[from_idx + 1], count * sizeof(uint32_t));
}

// Copy keys between nodes
static void
copy_keys(BPlusTree &tree, BTreeNode *src, uint32_t src_idx, BTreeNode *dst, uint32_t dst_idx, uint32_t count)
{
	if (count == 0)
		return;
	memcpy(GET_KEY_AT(tree, dst, dst_idx), GET_KEY_AT(tree, src, src_idx), count * tree.node_key_size);
}

// Copy records between leaf nodes
static void
copy_records(BPlusTree &tree, BTreeNode *src, uint32_t src_idx, BTreeNode *dst, uint32_t dst_idx, uint32_t count)
{
	if (count == 0)
		return;
	uint8_t *src_records = GET_RECORD_DATA(tree, src);
	uint8_t *dst_records = GET_RECORD_DATA(tree, dst);
	memcpy(dst_records + dst_idx * tree.record_size, src_records + src_idx * tree.record_size,
		   count * tree.record_size);
}

// Copy a single key
static void
copy_key(BPlusTree &tree, uint8_t *dst, uint8_t *src)
{
	memcpy(dst, src, tree.node_key_size);
}

// Copy a single record
static void
copy_record(BPlusTree &tree, uint8_t *dst, uint8_t *src)
{
	memcpy(dst, src, tree.record_size);
}

// ============================================================================
// NODE RELATIONSHIP FUNCTIONS
// ============================================================================

// Find a child's index in its parent's children array
static uint32_t
find_child_index(BPlusTree &tree, BTreeNode *parent, BTreeNode *child)
{
	uint32_t *children = GET_CHILDREN(tree, parent);
	for (uint32_t i = 0; i <= parent->num_keys; i++)
	{
		if (children[i] == child->index)
		{
			return i;
		}
	}
	ASSERT_PRINT(false, &tree); // Should never happen
	return 0;
}

// Link two leaf nodes together in the chain
static void
link_leaf_nodes(BTreeNode *left, BTreeNode *right)
{
	if (left)
	{
		MARK_DIRTY(left);
		left->next = right ? right->index : 0;
	}
	if (right)
	{
		MARK_DIRTY(right);
		right->previous = left ? left->index : 0;
	}
}

// Remove a leaf node from the chain, updating its neighbors
static void
unlink_leaf_node(BTreeNode *node)
{
	if (!IS_LEAF(node))
		return;

	BTreeNode *prev_node = nullptr;
	BTreeNode *next_node = nullptr;

	if (node->previous != 0)
	{
		prev_node = GET_PREV(node);
	}
	if (node->next != 0)
	{
		next_node = GET_NEXT(node);
	}

	link_leaf_nodes(prev_node, next_node);
}

// Set parent and update child's parent pointer
static void
set_parent(BTreeNode *node, uint32_t parent_index)
{
	MARK_DIRTY(node);
	node->parent = parent_index;

	if (parent_index != 0)
	{
		pager_mark_dirty(parent_index);
	}
}

// Set child at index and update its parent
static void
set_child(BPlusTree &tree, BTreeNode *node, uint32_t child_index, uint32_t node_index)
{
	ASSERT_PRINT(IS_INTERNAL(node), &tree);

	MARK_DIRTY(node);
	uint32_t *children = GET_CHILDREN(tree, node);
	children[child_index] = node_index;

	if (node_index != 0)
	{
		BTreeNode *child_node = GET_NODE(node_index);
		if (child_node)
		{
			set_parent(child_node, node->index);
		}
	}
}

// ============================================================================
// CORE B+TREE ALGORITHMS
// ============================================================================

static uint32_t
binary_search(BPlusTree &tree, BTreeNode *node, const uint8_t *key)
{
	uint32_t left = 0;
	uint32_t right = node->num_keys;

	while (left < right)
	{
		uint32_t mid = left + (right - left) / 2;
		uint8_t *mid_key = GET_KEY_AT(tree, node, mid);

		if (type_less_than(tree.node_key_type, mid_key, key))
		{
			left = mid + 1;
		}
		else if (type_equals(tree.node_key_type, mid_key, key))
		{
			if (IS_LEAF(node))
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
	BTreeNode *node = GET_NODE(page_index);

	node->index = page_index;
	node->parent = 0;
	node->next = 0;
	node->previous = 0;
	node->num_keys = 0;
	node->is_leaf = is_leaf ? 1 : 0;

	MARK_DIRTY(node);
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
swap_with_root(BPlusTree &tree, BTreeNode *root, BTreeNode *other)
{
	MARK_DIRTY(root);
	MARK_DIRTY(other);
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
	if (IS_INTERNAL(root))
	{
		uint32_t *children = GET_CHILDREN(tree, root);
		for (uint32_t i = 0; i <= root->num_keys; i++)
		{
			if (children[i])
			{
				BTreeNode *child = GET_NODE(children[i]);
				set_parent(child, tree.root_page_index);
			}
		}
	}

}

static void
destroy_node(BTreeNode *node)
{
	unlink_leaf_node(node);
	pager_delete(node->index);
}

static BTreeNode *
split(BPlusTree &tree, BTreeNode *node)
{
	BTreeNode *right_node = create_node(tree, IS_LEAF(node));
	uint32_t   split_index = GET_SPLIT_INDEX(tree, node);

	// Save the rising key VALUE before any modifications
	uint8_t rising_key_value[256]; // Max possible key size
	copy_key(tree, rising_key_value, GET_KEY_AT(tree, node, split_index));

	MARK_DIRTY(right_node);
	MARK_DIRTY(node);

	BTreeNode *parent = GET_PARENT(node);
	uint32_t   parent_index = 0;

	if (!parent)
	{
		auto new_internal = create_node(tree, false);
		auto root = GET_NODE(tree.root_page_index);
		// After swap: root contains empty internal node, new_internal contains old root data
		swap_with_root(tree, root, new_internal);



		set_child(tree, root, 0, new_internal->index);

		parent = root; // Continue with root as the parent
		node = new_internal;
	}
	else
	{
		MARK_DIRTY(parent);

		parent_index = find_child_index(tree, parent, node);

		// Shift parent's children right
		shift_children_right(tree, parent, parent_index + 1, parent->num_keys - parent_index);

		// Shift parent's keys right
		shift_keys_right(tree, parent, parent_index, parent->num_keys - parent_index);
	}

	// Use the saved rising key value for parent
	copy_key(tree, GET_KEY_AT(tree, parent, parent_index), rising_key_value);
	set_child(tree, parent, parent_index + 1, right_node->index);
	parent->num_keys++;

	if (IS_LEAF(node))
	{
		right_node->num_keys = node->num_keys - split_index;

		// Copy keys and records to right node
		copy_keys(tree, node, split_index, right_node, 0, right_node->num_keys);
		copy_records(tree, node, split_index, right_node, 0, right_node->num_keys);

		// Update leaf chain
		link_leaf_nodes(right_node, GET_NEXT(node));
		link_leaf_nodes(node, right_node);
	}
	else
	{
		right_node->num_keys = node->num_keys - split_index - 1;

		// Copy keys to right node
		copy_keys(tree, node, split_index + 1, right_node, 0, right_node->num_keys);

		if (IS_INTERNAL(node))
		{
			uint32_t *src_children = GET_CHILDREN(tree, node);
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
	if (!NODE_IS_FULL(tree, node))
	{
		return;
	}
	else if (IS_ROOT(node))
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
	if (IS_LEAF(node))
	{
		uint8_t *record_data = GET_RECORD_DATA(tree, node);
		uint32_t insert_index = binary_search(tree, node, key);

		/* we do the duplicate validation in the cursor by finding first */

		if (node->num_keys >= tree.leaf_max_keys)
		{
			insert_repair(tree, node);
			return false;
		}

		MARK_DIRTY(node);

		uint32_t num_to_shift = node->num_keys - insert_index;

		// Shift existing entries right to make room
		shift_keys_right(tree, node, insert_index, num_to_shift);
		shift_records_right(tree, node, insert_index, num_to_shift);

		// Insert new key and record
		copy_key(tree, GET_KEY_AT(tree, node, insert_index), key);
		copy_record(tree, record_data + insert_index * tree.record_size, (uint8_t *)data);

		node->num_keys++;
		return true;
	}
	else
	{
		uint32_t   child_index = binary_search(tree, node, key);
		BTreeNode *child_node = GET_CHILD(tree, node, child_index);
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
	BTreeNode *root = GET_ROOT(tree);

	if (root->num_keys == 0)
	{
		MARK_DIRTY(root);
		uint8_t *keys = root->data;
		uint8_t *record_data = GET_RECORD_DATA(tree, root);

		copy_key(tree, keys, (uint8_t *)key);
		copy_record(tree, record_data, (uint8_t *)data);
		root->num_keys = 1;
	}
	else
	{
		if (!insert(tree, root, reinterpret_cast<uint8_t *>(key), data))
		{
			insert(tree, GET_ROOT(tree), reinterpret_cast<uint8_t *>(key), data);
		}
	}
}

static void
update_parent_keys(BPlusTree &tree, BTreeNode *node, const uint8_t *deleted_key)
{
	BTreeNode *parent_node = GET_PARENT(node);
	uint32_t   parent_index = find_child_index(tree, parent_node, node);

	// Node should never be empty due to immediate repair_after_delete
	ASSERT_PRINT(node->num_keys > 0, &tree);

	BTreeNode *current_parent = parent_node;
	while (current_parent)
	{
		// we actually don't need to update the seperator key in the internal node, as it's just guiding.
		BTreeNode *grandparent = GET_PARENT(current_parent);
		if (grandparent)
		{
			parent_index = find_child_index(tree, grandparent, current_parent);
		}
		current_parent = grandparent;
	}
}

static void
do_delete(BPlusTree &tree, BTreeNode *node, const uint8_t *key, uint32_t index)
{
	if (IS_ROOT(node) && node->num_keys <= 1 && IS_LEAF(node))
	{
		MARK_DIRTY(node);
		node->num_keys = 0;
		return;
	}

	ASSERT_PRINT(IS_LEAF(node), &tree);

	MARK_DIRTY(node);

	uint8_t *record_data = GET_RECORD_DATA(tree, node);
	uint32_t shift_count = node->num_keys - index - 1;

	// Shift remaining entries left
	shift_keys_left(tree, node, index, shift_count);
	shift_records_left(tree, node, index, shift_count);

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
	BTreeNode *parent_node = GET_PARENT(node);
	BTreeNode *right_sibling = GET_CHILD(tree, parent_node, parent_index + 1);

	MARK_DIRTY(node);
	MARK_DIRTY(parent_node);
	MARK_DIRTY(right_sibling);

	if (IS_LEAF(node))
	{
		// Copy first entry from right sibling to end of node
		copy_key(tree, GET_KEY_AT(tree, node, node->num_keys), GET_KEY_AT(tree, right_sibling, 0));

		uint8_t *node_records = GET_RECORD_DATA(tree, node);
		uint8_t *sibling_records = GET_RECORD_DATA(tree, right_sibling);
		copy_record(tree, node_records + node->num_keys * tree.record_size, sibling_records);

		// Shift remaining entries in right sibling left
		uint32_t shift_count = right_sibling->num_keys - 1;
		shift_keys_left(tree, right_sibling, 0, shift_count);
		shift_records_left(tree, right_sibling, 0, shift_count);

		// Update parent separator
		copy_key(tree, GET_KEY_AT(tree, parent_node, parent_index), GET_KEY_AT(tree, right_sibling, 0));
	}
	else
	{
		// Copy separator from parent to end of node
		copy_key(tree, GET_KEY_AT(tree, node, node->num_keys), GET_KEY_AT(tree, parent_node, parent_index));

		// Move first key from right sibling to parent
		copy_key(tree, GET_KEY_AT(tree, parent_node, parent_index), GET_KEY_AT(tree, right_sibling, 0));

		// Shift remaining keys in right sibling left
		uint32_t shift_count = right_sibling->num_keys - 1;
		shift_keys_left(tree, right_sibling, 0, shift_count);

		uint32_t *node_children = GET_CHILDREN(tree, node);
		uint32_t *sibling_children = GET_CHILDREN(tree, right_sibling);

		// Move first child from right sibling to node
		set_child(tree, node, node->num_keys + 1, sibling_children[0]);

		// Shift remaining children in right sibling left
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
	BTreeNode *parent = GET_PARENT(node);
	ASSERT_PRINT(parent, &tree);

	uint32_t node_index = find_child_index(tree, parent, node);

	BTreeNode *right_sibling = GET_CHILD(tree, parent, node_index + 1);
	ASSERT_PRINT(right_sibling, &tree);

	MARK_DIRTY(node);
	MARK_DIRTY(parent);

	if (IS_LEAF(node))
	{
		// Copy all entries from right sibling to node
		copy_keys(tree, right_sibling, 0, node, node->num_keys, right_sibling->num_keys);
		copy_records(tree, right_sibling, 0, node, node->num_keys, right_sibling->num_keys);

		node->num_keys += right_sibling->num_keys;

		// Update leaf chain - node now points to what right_sibling pointed to
		link_leaf_nodes(node, GET_NEXT(right_sibling));
	}
	else
	{
		// Copy separator from parent
		copy_key(tree, GET_KEY_AT(tree, node, node->num_keys), GET_KEY_AT(tree, parent, node_index));

		// Copy all keys from right sibling
		copy_keys(tree, right_sibling, 0, node, node->num_keys + 1, right_sibling->num_keys);

		uint32_t *node_children = GET_CHILDREN(tree, node);
		uint32_t *sibling_children = GET_CHILDREN(tree, right_sibling);

		// Move all children from right sibling
		for (uint32_t i = 0; i <= right_sibling->num_keys; i++)
		{
			set_child(tree, node, node->num_keys + 1 + i, sibling_children[i]);
		}

		node->num_keys += 1 + right_sibling->num_keys;
	}

	// Remove separator from parent
	uint32_t shift_count = parent->num_keys - node_index - 1;
	shift_keys_left(tree, parent, node_index, shift_count);
	shift_children_left(tree, parent, node_index + 1, shift_count);

	parent->num_keys--;

	destroy_node(right_sibling);

	if (NODE_IS_MINIMAL(tree, parent))
	{
		if (IS_ROOT(parent) && parent->num_keys == 0)
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
	BTreeNode *parent_node = GET_PARENT(node);
	BTreeNode *left_sibling = GET_CHILD(tree, parent_node, parent_index - 1);

	MARK_DIRTY(node);
	MARK_DIRTY(parent_node);
	MARK_DIRTY(left_sibling);

	// Make room at beginning of node
	shift_keys_right(tree, node, 0, node->num_keys);

	if (IS_LEAF(node))
	{
		// Copy last entry from left sibling to beginning of node
		copy_key(tree, GET_KEY_AT(tree, node, 0), GET_KEY_AT(tree, left_sibling, left_sibling->num_keys - 1));

		uint8_t *node_records = GET_RECORD_DATA(tree, node);
		uint8_t *sibling_records = GET_RECORD_DATA(tree, left_sibling);

		shift_records_right(tree, node, 0, node->num_keys);
		copy_record(tree, node_records, sibling_records + (left_sibling->num_keys - 1) * tree.record_size);

		// Update parent separator
		copy_key(tree, GET_KEY_AT(tree, parent_node, parent_index - 1), GET_KEY_AT(tree, node, 0));
	}
	else
	{
		// Copy separator from parent to beginning of node
		copy_key(tree, GET_KEY_AT(tree, node, 0), GET_KEY_AT(tree, parent_node, parent_index - 1));

		// Move last key from left sibling to parent
		copy_key(tree, GET_KEY_AT(tree, parent_node, parent_index - 1),
				 GET_KEY_AT(tree, left_sibling, left_sibling->num_keys - 1));

		uint32_t *node_children = GET_CHILDREN(tree, node);
		uint32_t *sibling_children = GET_CHILDREN(tree, left_sibling);

		// Shift children right and move last child from left sibling
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
	if (!NODE_IS_MINIMAL(tree, node))
	{
		return;
	}

	if (IS_ROOT(node))
	{
		return;
	}

	BTreeNode *parent = GET_PARENT(node);
	uint32_t   node_index = find_child_index(tree, parent, node);

	if (node_index > 0)
	{
		BTreeNode *left_sibling = GET_CHILD(tree, parent, node_index - 1);
		if (left_sibling && NODE_CAN_SPARE(tree, left_sibling))
		{
			steal_from_left(tree, node, node_index);
			return;
		}
	}

	if (node_index < parent->num_keys)
	{
		BTreeNode *right_sibling = GET_CHILD(tree, parent, node_index + 1);
		if (right_sibling && NODE_CAN_SPARE(tree, right_sibling))
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
		BTreeNode *left_sibling = GET_CHILD(tree, parent, node_index - 1);
		if (left_sibling)
		{
			merge_right(tree, left_sibling);
		}
	}
}

void
clear_recurse(BPlusTree &tree, BTreeNode *node)
{
	if (IS_LEAF(node))
	{
		pager_delete(node->index);
		return;
	}

	uint32_t   i = 0;
	BTreeNode *child = GET_CHILD(tree, node, i);
	while (child != nullptr)
	{
		clear_recurse(tree, child);
		child = GET_CHILD(tree, node, i++);
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

	clear_recurse(*tree, GET_NODE(tree->root_page_index));
	return true;
}

/* ------ CURSOR -----------
 */
// Instead of FindResult struct and recursive find_containing_node:
static BTreeNode *
find_leaf(BPlusTree &tree, const uint8_t *key, uint32_t *out_index)
{
	BTreeNode *node = GET_ROOT(tree);

	while (!IS_LEAF(node))
	{
		uint32_t child_idx = binary_search(tree, node, key);
		node = GET_CHILD(tree, node, child_idx);
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

	while (!IS_LEAF(current))
	{
		if (left)
		{
			current = GET_CHILD(*cursor->tree, current, 0);
		}
		else
		{
			uint32_t child_pos = current->num_keys;
			current = GET_CHILD(*cursor->tree, current, child_pos);
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

	BTreeNode *root = GET_ROOT(*cursor->tree);
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

	BTreeNode *node = GET_NODE(cursor->leaf_page);
	if (!node || cursor->leaf_index >= node->num_keys)
	{
		return nullptr;
	}

	return GET_KEY_AT(*cursor->tree, node, cursor->leaf_index);
}

uint8_t *
bplustree_cursor_record(BPtCursor *cursor)
{
	if (cursor->state != BPT_CURSOR_VALID)
	{
		return nullptr;
	}

	BTreeNode *node = GET_NODE(cursor->leaf_page);
	if (!node || cursor->leaf_index >= node->num_keys)
	{
		return nullptr;
	}

	return GET_RECORD_AT(*cursor->tree, node, cursor->leaf_index);
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
				 type_equals(cursor->tree->node_key_type, GET_KEY_AT(*cursor->tree, leaf, index), (uint8_t *)key);

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

	auto	 node = GET_NODE(cursor->leaf_page);
	uint32_t index = node->index;

	do_delete(*cursor->tree, node, key, cursor->leaf_index);

	// node will survive
	node = GET_NODE(cursor->leaf_page);
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

	BTreeNode *node = GET_NODE(cursor->leaf_page);
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
			BTreeNode *next_node = GET_NEXT(node);
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

	BTreeNode *node = GET_NODE(cursor->leaf_page);
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
		BTreeNode *prev_node = GET_PREV(node);
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

	BTreeNode *root = GET_ROOT(*tree);
	ASSERT_PRINT(root != nullptr, tree);

	// Root specific checks
	ASSERT_PRINT(IS_ROOT(root), tree); // Root has no parent
	ASSERT_PRINT(root->index == tree->root_page_index, tree);

	// Track visited nodes to detect cycles
	std::unordered_set<uint32_t> visited;

	// Validate tree recursively
	ValidationResult result = validate_node_recursive(*tree, root, 0, nullptr, nullptr, visited);

	// If tree has data, verify leaf chain integrity
	if (IS_LEAF(root) && root->num_keys > 0)
	{
		// Single leaf root should have no siblings
		ASSERT_PRINT(root->next == 0, tree);
		ASSERT_PRINT(root->previous == 0, tree);
	}
	else if (IS_INTERNAL(root))
	{
		// Verify complete leaf chain by walking it
		BTreeNode					*first_leaf = result.leftmost_leaf;
		BTreeNode					*current = first_leaf;
		std::unordered_set<uint32_t> leaf_visited;

		ASSERT_PRINT(current->previous == 0, tree); // First leaf has no previous

		while (current)
		{
			ASSERT_PRINT(IS_LEAF(current), tree);
			ASSERT_PRINT(leaf_visited.find(current->index) == leaf_visited.end(), tree); // No cycles in leaf chain
			leaf_visited.insert(current->index);

			if (current->next != 0)
			{
				BTreeNode *next = GET_NEXT(current);
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
	uint32_t max_keys = GET_MAX_KEYS(tree, node);
	uint32_t min_keys = GET_MIN_KEYS(tree, node);

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
			ASSERT_PRINT(IS_LEAF(node), &tree); // Only leaf root can be empty during deletion
		}
	}

	// Validate key ordering and bounds
	uint8_t *prev_key = nullptr;
	uint8_t *first_key = nullptr;
	uint8_t *last_key = nullptr;

	for (uint32_t i = 0; i < node->num_keys; i++)
	{
		uint8_t *current_key = GET_KEY_AT(tree, node, i);

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

	if (IS_LEAF(node))
	{
		result.depth = 0;
		result.leftmost_leaf = node;
		result.rightmost_leaf = node;

		// Validate leaf data exists
		uint8_t *records = GET_RECORD_DATA(tree, node);
		ASSERT_PRINT(records != nullptr, &tree);

		// Verify leaf chain pointers are valid page indices or 0
		if (node->next != 0)
		{
			ASSERT_PRINT(node->next != node->index, &tree); // No self-reference
			BTreeNode *next = GET_NEXT(node);
			ASSERT_PRINT(next != nullptr, &tree);
			ASSERT_PRINT(IS_LEAF(next), &tree);
		}
		if (node->previous != 0)
		{
			ASSERT_PRINT(node->previous != node->index, &tree); // No self-reference
			BTreeNode *prev = GET_PREV(node);
			ASSERT_PRINT(prev != nullptr, &tree);
			ASSERT_PRINT(IS_LEAF(prev), &tree);
		}
	}
	else
	{
		// Internal node validation
		uint32_t *children = GET_CHILDREN(tree, node);
		ASSERT_PRINT(children != nullptr, &tree);

		uint32_t   child_depth = UINT32_MAX;
		BTreeNode *leftmost_leaf = nullptr;
		BTreeNode *rightmost_leaf = nullptr;

		// Internal nodes have num_keys + 1 children
		for (uint32_t i = 0; i <= node->num_keys; i++)
		{
			ASSERT_PRINT(children[i] != 0, &tree);			 // No null children
			ASSERT_PRINT(children[i] != node->index, &tree); // No self-reference

			BTreeNode *child = GET_CHILD(tree, node, i);
			ASSERT_PRINT(child != nullptr, &tree);

			// Determine bounds for this child
			uint8_t *child_min = (i == 0) ? parent_min_bound : GET_KEY_AT(tree, node, i - 1);
			uint8_t *child_max = (i == node->num_keys) ? parent_max_bound : GET_KEY_AT(tree, node, i);

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
				uint8_t *separator = GET_KEY_AT(tree, node, i - 1);
				ASSERT_PRINT(type_greater_equal(tree.node_key_type, child_result.min_key, separator), &tree);
			}
			if (child_result.max_key && i < node->num_keys)
			{
				// Last key in child < separator key after it
				uint8_t *separator = GET_KEY_AT(tree, node, i);
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

			BTreeNode *node = GET_NODE(page_index);
			if (!node)
			{
				printf("  ERROR: Cannot read page %u\n", page_index);
				continue;
			}

			// Print node header
			printf("  Node[page_%u]:\n", node->index);
			printf("    Type: %s\n", IS_LEAF(node) ? "LEAF" : "INTERNAL");
			printf("    Parent: %s\n", IS_ROOT(node) ? "ROOT" : ("page_" + std::to_string(node->parent)).c_str());
			printf("    Keys(%u): [", node->num_keys);

			// Print keys
			for (uint32_t i = 0; i < node->num_keys; i++)
			{
				if (i > 0)
					printf(", ");
				print_key(*tree, GET_KEY_AT(*tree, node, i));
			}
			printf("]\n");

			// Print children for internal nodes
			if (IS_INTERNAL(node))
			{
				uint32_t *children = GET_CHILDREN(*tree, node);
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
	BTreeNode *current = GET_ROOT(*tree);
	while (IS_INTERNAL(current))
	{
		current = GET_CHILD(*tree, current, 0);
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

		current = GET_NEXT(current);
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

		BTreeNode *node = GET_NODE(page_index);
		if (!node)
			continue;

		// Print: page_index:type:num_keys:parent
		printf("[%u:%c:%u:%u] ", node->index, IS_LEAF(node) ? 'L' : 'I', node->num_keys, node->parent);

		// Add children to queue
		if (IS_INTERNAL(node))
		{
			uint32_t *children = GET_CHILDREN(*tree, node);
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
