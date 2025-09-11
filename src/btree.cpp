
/*
================================================================================
					B+TREE LEAF NODE SHIFT OPERATIONS
================================================================================

LEAF NODE MEMORY LAYOUT
-----------------------
┌────────────────────────────────────────────────────────────────────────┐
│ Header (24 bytes) │        Keys Area         │      Records Area       │
├───────────────────┼──────────────────────────┼─────────────────────────┤
│ index  (4)        │ key[0] │ key[1] │ key[2] │ rec[0] │ rec[1] │ rec[2]│
│ parent (4)        │        │        │        │        │        │       │
│ next   (4)        │  Keys stored             │  Records stored         │
│ prev   (4)        │  contiguously            │  contiguously           │
│ num_keys (4)      │                          │                         │
│ is_leaf (4)       │                          │                         │
└────────────────────────────────────────────────────────────────────────┘
					↑                          ↑
					data[0]                    data + (max_keys * key_size)


================================================================================
1. SHIFT_KEYS_RIGHT - Making space for insertion at index 1
================================================================================

BEFORE: (num_keys = 3, inserting at index 1)
────────────────────────────────────────────
Keys:    [10] [20] [30] [  ] [  ]
		 ↑    ↑    ↑
		 0    1    2

Records: [A]  [B]  [C]  [ ]  [ ]
		 ↑    ↑    ↑
		 0    1    2

OPERATION: SHIFT_KEYS_RIGHT(node, from_idx=1, count=2)
──────────────────────────────────────────────────────
memcpy(GET_KEY_AT(node, 2),    // destination: key[2]
	   GET_KEY_AT(node, 1),    // source: key[1]
	   2 * key_size)           // copy key[1] and key[2]

Visual:
	   from_idx
		  ↓
Keys:    [10] [20] [30] [  ] [  ]
			  └─────┴────→ copy 2 keys
Keys:    [10] [20] [20] [30] [  ]
			  gap  └─────┴─── shifted

AFTER: (ready to insert at index 1)
─────────────────────────────────
Keys:    [10] [??] [20] [30] [  ]
			  ↑
			  ready for new key

Records: [A]  [??] [B]  [C]  [ ]
			  ↑
			  ready for new record
			  (after SHIFT_RECORDS_RIGHT)


================================================================================
2. SHIFT_RECORDS_RIGHT - Corresponding record shift
================================================================================

OPERATION: SHIFT_RECORDS_RIGHT(node, from_idx=1, count=2)
──────────────────────────────────────────────────────────
uint8_t *base = GET_RECORD_DATA(node);
memcpy(base + (2 * record_size),    // destination: rec[2]
	   base + (1 * record_size),    // source: rec[1]
	   2 * record_size)             // copy rec[1] and rec[2]

Visual:
		 from_idx
			↓
Records: [A]  [B]  [C]  [ ]  [ ]
			  └────┴─────→ copy 2 records
Records: [A]  [B]  [B]  [C]  [ ]
			  gap  └────┴─── shifted


================================================================================
3. SHIFT_KEYS_LEFT - Removing entry at index 1
================================================================================

BEFORE: (num_keys = 4, deleting at index 1)
───────────────────────────────────────────
Keys:    [10] [15] [20] [30] [  ]
		 ↑    ↑    ↑    ↑
		 0    1    2    3
			  DEL

Records: [A]  [X]  [B]  [C]  [ ]
		 ↑    ↑    ↑    ↑
		 0    1    2    3
			  DEL

OPERATION: SHIFT_KEYS_LEFT(node, from_idx=1, count=2)
─────────────────────────────────────────────────────
memcpy(GET_KEY_AT(node, 1),    // destination: key[1]
	   GET_KEY_AT(node, 2),    // source: key[2]
	   2 * key_size)           // copy key[2] and key[3]

Visual:
			  from_idx
				 ↓
Keys:    [10] [15] [20] [30] [  ]
			  ←────└────┴─── copy 2 keys
Keys:    [10] [20] [30] [30] [  ]
			  └────┴─── shifted
						stale (will be ignored)

AFTER: (num_keys decremented to 3)
───────────────────────────────────
Keys:    [10] [20] [30] [××] [  ]
		 ↑    ↑    ↑
		 0    1    2    (ignored)

Records: [A]  [B]  [C]  [××] [ ]
		 ↑    ↑    ↑
		 0    1    2    (ignored)


================================================================================
4. COMPLETE INSERT EXAMPLE
================================================================================

Initial state: num_keys = 3
─────────────────────────────
Keys:    [10] [20] [30]
Records: [A]  [B]  [C]

Want to insert: key=15, record=X at position 1

Step 1: Find insertion point (binary_search returns 1)
Step 2: SHIFT_KEYS_RIGHT(node, 1, 2)
		Keys:    [10] [20] [20] [30]
Step 3: SHIFT_RECORDS_RIGHT(node, 1, 2)
		Records: [A]  [B]  [B]  [C]
Step 4: COPY_KEY(GET_KEY_AT(node, 1), 15)
		Keys:    [10] [15] [20] [30]
Step 5: COPY_RECORD(GET_RECORD_AT(node, 1), X)
		Records: [A]  [X]  [B]  [C]
Step 6: node->num_keys++
		num_keys = 4

Final state:
────────────
Keys:    [10] [15] [20] [30]
Records: [A]  [X]  [B]  [C]


================================================================================
5. COMPLETE DELETE EXAMPLE
================================================================================

Initial state: num_keys = 4
─────────────────────────────
Keys:    [10] [15] [20] [30]
Records: [A]  [X]  [B]  [C]

Want to delete: key=15 at position 1

Step 1: Find deletion point (binary_search returns 1)
Step 2: Calculate entries_to_shift = 4 - 1 - 1 = 2
Step 3: SHIFT_KEYS_LEFT(node, 1, 2)
		Keys:    [10] [20] [30] [30]
Step 4: SHIFT_RECORDS_LEFT(node, 1, 2)
		Records: [A]  [B]  [C]  [C]
Step 5: node->num_keys--
		num_keys = 3

Final state:
────────────
Keys:    [10] [20] [30] [××]  (last entry ignored)
Records: [A]  [B]  [C]  [××]  (last entry ignored)


================================================================================
							  KEY OBSERVATIONS
================================================================================

1. CONTIGUOUS STORAGE
   - Keys are packed together at the start of data area
   - Records are packed together after all keys
   - This maximizes cache efficiency during binary search

2. SHIFT DIRECTION
   - RIGHT: Creates gap for insertion (copy from lower to higher index)
   - LEFT: Closes gap after deletion (copy from higher to lower index)

3. MEMCPY VS MEMMOVE
   - These operations use memcpy because source and destination don't overlap
   - from_idx+1 to from_idx+1+count doesn't overlap with from_idx to from_idx+count

4. RECORD ALIGNMENT
   - Records follow the same shift pattern as keys
   - The base pointer calculation ensures proper offset into record area

5. EFFICIENCY
   - Bulk memory operations are much faster than element-by-element
   - Single memcpy for multiple contiguous elements
*/

#include <cstdio>
#include <string>

/*
** 2024 SQL-FromScratch
**
** OVERVIEW
**
** This file implements a B+tree data structure optimized for database
** storage systems. B+trees store all data
** in leaf nodes, with internal nodes serving purely as a navigation
** index.
**
** KEY CONCEPTS
**
** Node Types: The B+tree contains two distinct node types - internal nodes
** that store only keys and child pointers, and leaf nodes that store
** key-value pairs. All nodes fit exactly within a single page for atomic
** disk operations.
**
** Leaf Chain: Leaf nodes are linked in a doubly-linked list, enabling
** efficient range queries and sequential scans without tree traversal.
**
** Key Distribution: The tree maintains balance through controlled key
** distribution. Each node (except root) must contain between MIN_KEYS
** and MAX_KEYS entries. The split point is chosen to optimize space
** utilization while maintaining balance.
**
** Overflow/Underflow: When nodes exceed capacity (overflow) they split,
** potentially propagating splits up to the root. When nodes fall below
** minimum capacity (underflow) they attempt to borrow from siblings or
** merge, potentially propagating merges up to the root.
**
** IMPLEMENTATION NOTES
**
** Memory Management: All nodes are allocated through the pager, which
** handles caching, persistence, and transaction support. Node pointers
** are page indices rather than memory addresses.
**
** Error Handling: The implementation uses assertions for invariant
** checking during development. Production builds rely on the pager's
** transaction mechanism for consistency.
**
*/

#include "btree.hpp"
#include "containers.hpp"
#include "common.hpp"
#include "types.hpp"
#include "pager.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <sys/types.h>
#include <utility>



/*
** CONSTANTS AND CONFIGURATION
**
** These values control the B+tree geometry and must be chosen carefully
** to balance between tree height (affecting search performance) and
** node utilization (affecting space efficiency).
*/

/*
** NODE_HEADER_SIZE - Space reserved for node metadata
**
** The header contains:
**   - index (4 bytes): Page index for self-identification
**   - parent (4 bytes): Parent page index (0 for root)
**   - next (4 bytes): Next leaf in chain (leaf nodes only)
**   - previous (4 bytes): Previous leaf in chain (leaf nodes only)
**   - num_keys (4 bytes): Current number of keys in node
**   - is_leaf (4 bytes): Node type flag and padding
**
** Total: 24 bytes aligned for efficient access
*/
#define NODE_HEADER_SIZE 24

/*
** NODE_DATA_SIZE - Usable space for keys and values
**
** After reserving header space, the remainder of the page is available
** for storing keys, child pointers (internal nodes), or records (leaf nodes).
*/
#define NODE_DATA_SIZE (PAGE_SIZE - NODE_HEADER_SIZE)

/*
** MIN_ENTRY_COUNT - Minimum viable node capacity
**
** This ensures nodes can perform splits and merges correctly.
** A value of 3 allows for proper redistribution during rebalancing:
** a node with 3 entries can split into two nodes with at least 1 entry each,
** and two minimal nodes can merge without exceeding maximum capacity.
*/
#define MIN_ENTRY_COUNT 3

/*
** B+TREE NODE STRUCTURE
**
** Fixed-size structure that fits exactly in one page. The node header
** provides metadata for navigation and maintenance, while the data area
** stores the actual keys and associated values or pointers.
**
** Memory Layout for Internal Nodes:
**   [Header (24B)][Key0][Key1]...[KeyN][Child0][Child1]...[ChildN+1]
**
**   Keys are stored contiguously from the start of the data area.
**   Child pointers follow immediately after all keys.
**   Note: Internal nodes have N keys but N+1 children.
**
** Memory Layout for Leaf Nodes:
**   [Header (24B)][Key0][Key1]...[KeyN][Record0][Record1]...[RecordN]
**
**   Keys are stored contiguously from the start of the data area.
**   Records follow immediately after all keys.
**   Each key has exactly one corresponding record.
**
** The contiguous storage of keys enables efficient binary search and
** maximizes CPU cache utilization during traversal operations.
*/
struct btree_node
{
	/* Node identification and navigation (24 bytes total) */
	uint32_t index;	   /* This node's page index in the database file */
	uint32_t parent;   /* Parent node's page index (0 if this is root) */
	uint32_t next;	   /* Next sibling in leaf chain (leaf nodes only) */
	uint32_t previous; /* Previous sibling in leaf chain (leaf nodes only) */
	uint32_t num_keys; /* Number of valid keys currently in this node */
	uint32_t is_leaf;  /* Node type: 1 for leaf, 0 for internal */

	/* Flexible data area for keys and values/pointers */
	uint8_t data[NODE_DATA_SIZE];
};

/*
** Compile-time verification that node structure matches page size exactly.
** This is critical for atomic I/O operations and proper alignment.
*/
static_assert(sizeof(btree_node) == PAGE_SIZE, "btree_node must be exactly PAGE_SIZE");

/*
** ACCESSOR MACROS
**
** These macros provide zero-cost abstractions for accessing node data.
** Using macros instead of functions ensures:
**   1. No function call overhead in critical paths
**   2. Compile-time type checking with proper casts
**   3. Consistent memory access patterns for the optimizer
**
** The macros are organized into logical groups for clarity.
*/

// ============================================================================
// NODE TYPE AND RELATIONSHIP PREDICATES
// ============================================================================
/*NOCOVER_START*/
/*
** Node type checks - branch prediction hints could be added here
** since leaves are accessed more frequently than internals in most workloads
*/
#define IS_LEAF(node)	  ((node)->is_leaf)
#define IS_INTERNAL(node) (!(node)->is_leaf)
#define IS_ROOT(node)	  ((node)->parent == 0)

/*
** Node capacity accessors - these vary by node type since internal
** nodes need space for an extra child pointer (N keys, N+1 children)
*/
#define GET_MAX_KEYS(node)	  ((node)->is_leaf ? tree->leaf_max_keys : tree->internal_max_keys)
#define GET_MIN_KEYS(node)	  ((node)->is_leaf ? tree->leaf_min_keys : tree->internal_min_keys)
#define GET_SPLIT_INDEX(node) ((node)->is_leaf ? tree->leaf_split_index : tree->internal_split_index)

// ============================================================================
// NODE RETRIEVAL AND NAVIGATION
// ============================================================================

/*
** Page-to-node conversion macros
**
** GET_NODE converts a page index to a node pointer through the pager.
** The cast is safe because btree_node is designed to overlay exactly
** onto a page structure.
*/
#define GET_NODE(index)	 (reinterpret_cast<btree_node *>(pager_get(index)))
#define GET_ROOT()		 GET_NODE(tree->root_page_index)
#define GET_PARENT(node) GET_NODE((node)->parent)

/*
** Leaf chain navigation
**
** Only valid for leaf nodes. The chain enables efficient range scans
** without tree traversal. Invalid for internal nodes (should be 0).
*/
#define GET_NEXT(node) GET_NODE((node)->next)
#define GET_PREV(node) GET_NODE((node)->previous)

// ============================================================================
// DATA LAYOUT ACCESSORS
// ============================================================================

/*
** Key access
**
** Keys are stored contiguously from the start of the data area.
** This layout maximizes cache efficiency during binary search.
*/
#define GET_KEY_AT(node, idx) ((node)->data + idx * tree->node_key_size)

/*
** Internal node children array
**
** Children pointers follow immediately after keys. For N keys, there
** are N+1 children. Child[i] contains keys < Key[i], Child[i+1] contains
** keys >= Key[i].
*/
#define GET_CHILDREN(node)	 (reinterpret_cast<uint32_t *>((node)->data + tree->internal_max_keys * tree->node_key_size))
#define GET_CHILD(node, idx) GET_NODE(GET_CHILDREN(node)[idx])

/*
** Leaf node record storage
**
** Records are packed after keys. Each key[i] maps to record[i].
** The separation of keys and records improves search performance
** by keeping keys dense in memory.
*/
#define GET_RECORD_DATA(node)	 ((node)->data + tree->leaf_max_keys * tree->node_key_size)
#define GET_RECORD_AT(node, idx) (GET_RECORD_DATA(node) + (idx) * tree->record_size)

// ============================================================================
// NODE STATE PREDICATES
// ============================================================================

/*
** Capacity checks for split/merge decisions
**
** These predicates encapsulate the B+tree invariants and make the
** split/merge logic more readable.
*/
#define NODE_IS_FULL(node)	  ((node)->num_keys >= GET_MAX_KEYS(node))
#define NODE_IS_MINIMAL(node) ((node)->num_keys <= GET_MIN_KEYS(node))
#define IS_UNDERFLOWING(node) ((node->num_keys < GET_MIN_KEYS(node)))
#define NODE_CAN_SPARE(node)  ((node)->num_keys > GET_MIN_KEYS(node))

/*
** Transaction integration
**
** Marks a node's page as dirty in the pager's transaction log.
** Must be called before modifying node content.
*/
#define MARK_DIRTY(node) pager_mark_dirty((node)->index)

// ============================================================================
// MEMORY OPERATION MACROS
// ============================================================================

/*
** Bulk memory operations for node modifications
**
** These macros encapsulate the low-level memory operations required
** for insertions, deletions, splits, and merges. Using memmove/memcpy
** is significantly faster than element-by-element copying for large
** keys or records.
**
** The RIGHT macros create gaps for insertion, LEFT macros close gaps
** after deletion.
*/

/* Key array manipulation */
#define SHIFT_KEYS_RIGHT(node, from_idx, count)                                                                        \
	memcpy(GET_KEY_AT(node, (from_idx) + 1), GET_KEY_AT(node, from_idx), (count) * tree->node_key_size)

#define SHIFT_KEYS_LEFT(node, from_idx, count)                                                                         \
	memcpy(GET_KEY_AT(node, from_idx), GET_KEY_AT(node, (from_idx) + 1), (count) * tree->node_key_size)

/*
** Record array manipulation (leaf nodes only)
**
** The do-while(0) idiom allows these macros to be used as statements
** with proper semicolon termination and scope.
*/
#define SHIFT_RECORDS_RIGHT(node, from_idx, count)                                                                     \
	do                                                                                                                 \
	{                                                                                                                  \
		uint8_t *_base = GET_RECORD_DATA(node);                                                                        \
		memcpy(_base + ((from_idx) + 1) * tree->record_size, _base + (from_idx) * tree->record_size,                   \
			   (count) * tree->record_size);                                                                           \
	} while (0)

#define SHIFT_RECORDS_LEFT(node, from_idx, count)                                                                      \
	do                                                                                                                 \
	{                                                                                                                  \
		uint8_t *_base = GET_RECORD_DATA(node);                                                                        \
		memcpy(_base + (from_idx) * tree->record_size, _base + ((from_idx) + 1) * tree->record_size,                   \
			   (count) * tree->record_size);                                                                           \
	} while (0)

/* Children array manipulation (internal nodes only) */
#define SHIFT_CHILDREN_RIGHT(node, from_idx, count)                                                                    \
	do                                                                                                                 \
	{                                                                                                                  \
		uint32_t *_children = GET_CHILDREN(node);                                                                      \
		memcpy(&_children[(from_idx) + 1], &_children[from_idx], (count) * sizeof(uint32_t));                          \
	} while (0)

#define SHIFT_CHILDREN_LEFT(node, from_idx, count)                                                                     \
	do                                                                                                                 \
	{                                                                                                                  \
		uint32_t *_children = GET_CHILDREN(node);                                                                      \
		memcpy(&_children[from_idx], &_children[(from_idx) + 1], (count) * sizeof(uint32_t));                          \
	} while (0)

/*
** Inter-node copy operations
**
** Used during splits and merges to move data between nodes.
** These preserve the source node's data (unlike shifts).
*/
#define COPY_KEYS(src, src_idx, dst, dst_idx, count)                                                                   \
	memcpy(GET_KEY_AT(dst, dst_idx), GET_KEY_AT(src, src_idx), (count) * tree->node_key_size)

#define COPY_RECORDS(src, src_idx, dst, dst_idx, count)                                                                \
	do                                                                                                                 \
	{                                                                                                                  \
		uint8_t *_src_records = GET_RECORD_DATA(src);                                                                  \
		uint8_t *_dst_records = GET_RECORD_DATA(dst);                                                                  \
		memcpy(_dst_records + (dst_idx) * tree->record_size, _src_records + (src_idx) * tree->record_size,             \
			   (count) * tree->record_size);                                                                           \
	} while (0)

/* Single element operations */
#define COPY_KEY(dst, src)	  memcpy(dst, src, tree->node_key_size)
#define COPY_RECORD(dst, src) memcpy(dst, src, tree->record_size)

/*NOCOVER_END*/
// ============================================================================
// NODE RELATIONSHIP FUNCTIONS
// ============================================================================

/*
** Find a child's position within its parent's children array.
**
** This cannot use binary search because child pointers are not sorted
** by their page indices. The children are positionally significant:
** child[i] contains keys less than key[i], child[i+1] contains keys
** greater than or equal to key[i].
**
** Returns: Index in the children array (0 to parent->num_keys)
** Asserts: Child must actually be a child of parent
*/
static uint32_t
find_child_index(btree *tree, btree_node *parent, btree_node *child)
{
	// cannot be converted to binary search, because the page indicies are not sorted
	uint32_t *children = GET_CHILDREN(parent);
	for (uint32_t i = 0; i <= parent->num_keys; i++)
	{
		if (children[i] == child->index)
		{
			return i;
		}
	}
	assert(false);
	return 0;
}

/*
** Binary search within a node to find key position.
**
** Returns the index where the key either exists or should be inserted.
** For leaf nodes, returns exact match position or insertion point.
** For internal nodes, returns the child pointer to follow.
**
** The search handles three cases:
**   1. Key found: Leaf returns exact index, internal returns right child
**   2. Key < all keys: Returns 0 (leftmost position/child)
**   3. Key > all keys: Returns num_keys (rightmost position/child)
**
** Special handling for exact matches in internal nodes:
** Returns mid+1 to follow the right child (contains keys >= separator).
*/
static uint32_t
binary_search(btree *tree, btree_node *node, void *key)
{
	uint32_t left = 0;
	uint32_t right = node->num_keys;

	while (left < right)
	{
		uint32_t mid = left + (right - left) / 2;
		void	*mid_key = GET_KEY_AT(node, mid);

		if (type_less_than(tree->node_key_type, mid_key, key))
		{
			left = mid + 1;
		}
		else if (type_equals(tree->node_key_type, mid_key, key))
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

/*
** Navigate from root to the appropriate leaf for a given key.
**
** Performs standard B+tree traversal, using binary search at each
** internal node to determine which child to follow. Since all data
** is stored in leaves, this always descends to a leaf node.
**
** Returns: The leaf node where this key belongs (for search or insertion)
*/
static btree_node *
find_leaf_for_key(btree *tree, void *key)
{
	btree_node *node = GET_ROOT();

	while (IS_INTERNAL(node))
	{
		uint32_t idx = binary_search(tree, node, key);
		node = GET_CHILD(node, idx);
	}

	return node;
}

/*
** Update the doubly-linked chain between leaf nodes.
**
** The leaf chain enables efficient range queries without tree traversal.
** This function maintains bidirectional consistency - each node points
** to its neighbors and neighbors point back.
**
** Parameters:
**   left  - Previous node in chain (may be null for first leaf)
**   right - Next node in chain (may be null for last leaf)
**
** Note: Both nodes are marked dirty since their pointers change.
*/
static void
link_leaf_nodes(btree_node *left, btree_node *right)
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

/*
** Remove a leaf node from the doubly-linked chain.
**
** When deleting a node, we must maintain chain integrity by connecting
** its previous and next neighbors directly. This is a no-op for internal
** nodes since they don't participate in the leaf chain.
**
** The node being removed is not modified - it will be deleted after this.
*/
static void
unlink_leaf_node(btree_node *node)
{
	if (!IS_LEAF(node))
	{
		return;
	}

	btree_node *prev_node = nullptr;
	btree_node *next_node = nullptr;

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

/*
** Update a child pointer and maintain parent back-reference.
**
** This function ensures bidirectional consistency between parent and child.
** When a child is assigned to a parent slot, the child's parent pointer
** must be updated to maintain the invariant that every non-root node
** knows its parent.
**
** Parameters:
**   node        - Parent node (must be internal)
**   child_index - Position in children array (0 to num_keys)
**   node_index  - Page index of new child (0 means null/deleted)
**
** Both nodes are marked dirty since the relationship change must persist.
*/
static void
set_child(btree *tree, btree_node *node, uint32_t child_index, uint32_t node_index)
{
	assert(IS_INTERNAL(node));

	MARK_DIRTY(node);
	uint32_t *children = GET_CHILDREN(node);
	children[child_index] = node_index;

	if (node_index != 0)
	{
		btree_node *child_node = GET_NODE(node_index);
		if (child_node)
		{

			MARK_DIRTY(child_node);
			child_node->parent = node->index;
		}
	}
}

/*
** Allocate and initialize a new B+tree node.
**
** Creates a node through the pager (ensuring persistence) and
** initializes all fields to valid empty state. The node starts with
** no keys and no parent (caller must set parent if needed).
**
** The node is immediately marked dirty to ensure the initialization
** is written to disk within the current transaction.
*/
static btree_node *
create_node(btree *tree, bool is_leaf)
{
	uint32_t page_index = pager_new();
	assert(page_index != PAGE_INVALID);
	btree_node *node = GET_NODE(page_index);

	node->index = page_index;
	node->parent = 0;
	node->next = 0;
	node->previous = 0;
	node->num_keys = 0;
	node->is_leaf = is_leaf ? 1 : 0;

	MARK_DIRTY(node);
	return node;
}

// ============================================================================
// CORE B+TREE ALGORITHMS
// ============================================================================
/*
** Swap the contents of the root node with another node.
**
** This operation is critical for maintaining the invariant that the root
** always resides at a fixed page index. When the root needs to be split
** or replaced (e.g., creating a new root above the current one), we swap
** contents rather than changing the root page index.
**
** The swap preserves each node's original page index while exchanging
** all other data. After swapping, all children of the new root must have
** their parent pointers updated to reference the root page index.
**
** This technique avoids updating the root page reference throughout the
** tree and in external structures.
*/
static void
swap_with_root(btree *tree, btree_node *root, btree_node *other)
{
	MARK_DIRTY(root);
	MARK_DIRTY(other);
	// Verify root is actually the root
	assert(root->index == tree->root_page_index);

	// Swap everything EXCEPT the index field
	uint32_t saved_root_index = root->index;
	uint32_t saved_other_index = other->index;

	// Swap all content
	btree_node temp;
	memcpy(&temp, other, sizeof(btree_node));
	memcpy(other, root, sizeof(btree_node));
	memcpy(root, &temp, sizeof(btree_node));

	// Restore the original indices
	root->index = saved_root_index;
	other->index = saved_other_index;

	// Fix parent pointers for root's new children
	root->parent = 0; // Root has no parent
	if (IS_INTERNAL(root))
	{
		uint32_t *children = GET_CHILDREN(root);
		for (uint32_t i = 0; i <= root->num_keys; i++)
		{
			if (children[i])
			{
				btree_node *child = GET_NODE(children[i]);
				MARK_DIRTY(child);
				child->parent = tree->root_page_index;
			}
		}
	}
	if (IS_INTERNAL(other))
	{
		uint32_t *children = GET_CHILDREN(other);
		for (uint32_t i = 0; i <= other->num_keys; i++)
		{
			if (children[i])
			{
				btree_node *child = GET_NODE(children[i]);
				MARK_DIRTY(child);
				child->parent = other->index; // ← Update to other's index, not root
			}
		}
	}
}

/*
** Split a full node into two nodes, promoting a key to the parent.
**
** This is the core operation for maintaining B+tree balance during insertion.
** The split creates a new right sibling, redistributes entries between the
** original (left) and new (right) nodes, and promotes a separator key to
** the parent.
**
** Algorithm:
**   1. Create new right sibling node
**   2. Choose split point (typically mid-point for balance)
**   3. For leaves: Copy right half to new node, promote copy of middle key
**   4. For internals: Move right half to new node, promote middle key
**   5. Insert promoted key and new child into parent
**   6. Handle parent overflow if necessary
**
** Special case: Splitting the root
**   When the root splits, we need a new root above it. Rather than
**   changing the root page index, we:
**   1. Create a new node for the current root's data
**   2. Swap contents so root becomes empty parent
**   3. Continue with normal split logic
**
** Returns: Parent node (which may now be full and need splitting)
*/

/*
====================================
B+Tree Structure Before: Single Full Leaf Root
====================================
Root: page_2
Key type: U32, Record size: 100 bytes
Internal: max_keys=124, min_keys=61
Leaf: max_keys=9, min_keys=4
------------------------------------

LEVEL 0:
--------
  Node[page_2]:
	Type: LEAF
	Parent: ROOT
	Keys(9): [1, 2, 3, 4, 5, 6, 7, 8, 9]
	Leaf chain: prev=NULL, next=NULL

====================================
Leaf Chain Traversal:
------------------------------------
  page_2
  Total leaves: 1
====================================

====================================
B+Tree Structure After Split: Internal Root + 2 leafs
====================================
Root: page_2
Key type: U32, Record size: 100 bytes
Internal: max_keys=124, min_keys=61
Leaf: max_keys=9, min_keys=4
------------------------------------

LEVEL 0:
--------
  Node[page_2]:
	Type: INTERNAL
	Parent: ROOT
	Keys(1): [5]
	Children(2): [page_6, page_5]

LEVEL 1:
--------
  Node[page_6]:
	Type: LEAF
	Parent: page_2
	Keys(4): [1, 2, 3, 4]
	Leaf chain: prev=NULL, next=page_5

  Node[page_5]:
	Type: LEAF
	Parent: page_2
	Keys(5): [5, 6, 7, 8, 9]
	Leaf chain: prev=page_6, next=NULL

====================================
Leaf Chain Traversal:
------------------------------------
  page_6 -> page_5
  Total leaves: 2
====================================
 */

static btree_node *
split(btree *tree, btree_node *node)
{
	// === PREPARATION ===
	uint32_t	split_point = GET_SPLIT_INDEX(node);
	btree_node *new_right = create_node(tree, IS_LEAF(node));

	// Save the key that will be promoted to parent
	// (for leaves, this is a copy; for internals, it moves up)
	uint8_t promoted_key[256];

	COPY_KEY(promoted_key, GET_KEY_AT(node, split_point));

	MARK_DIRTY(node);
	MARK_DIRTY(new_right);

	// === ENSURE PARENT EXISTS ===
	btree_node *parent = GET_PARENT(node);
	uint32_t	position_in_parent = 0;

	if (!parent)
	{
		// Special case: splitting root requires creating new root above it
		// 1. Create new node for the internal
		btree_node *new_node = create_node(tree, false);
		// 2. Get current root
		btree_node *root = GET_NODE(tree->root_page_index);
		// 3. Swap contents: root becomes empty, new_node gets old root data
		swap_with_root(tree, root, new_node);
		// 4. Make the old root data (now in new_node) a child of the new root
		set_child(tree, root, 0, new_node->index);

		parent = root;			// New root is the parent
		node = new_node;		// Continue with relocated node
		position_in_parent = 0; // It's child 0 of the new root
	}
	else
	{
		MARK_DIRTY(parent);
		// Normal case: find our position in existing parent
		position_in_parent = find_child_index(tree, parent, node);

		// Make room for new key and child in parent
		SHIFT_CHILDREN_RIGHT(parent, position_in_parent + 1, parent->num_keys - position_in_parent);
		SHIFT_KEYS_RIGHT(parent, position_in_parent, parent->num_keys - position_in_parent);
	}

	// === INSERT PROMOTED KEY INTO PARENT ===

	COPY_KEY(GET_KEY_AT(parent, position_in_parent), promoted_key);
	set_child(tree, parent, position_in_parent + 1, new_right->index);
	parent->num_keys++;

	// === SPLIT NODE'S DATA ===
	if (IS_LEAF(node))
	{
		// Leaf: Split keys and records evenly
		// Right gets everything from split_point onwards
		new_right->num_keys = node->num_keys - split_point;
		COPY_KEYS(node, split_point, new_right, 0, new_right->num_keys);
		COPY_RECORDS(node, split_point, new_right, 0, new_right->num_keys);

		// Maintain leaf chain
		link_leaf_nodes(new_right, GET_NEXT(node));
		link_leaf_nodes(node, new_right);

		// Left keeps first split_point entries
		node->num_keys = split_point;
	}
	else
	{
		// Internal: The promoted key goes up, doesn't stay in either node
		// Right gets keys after the promoted key
		new_right->num_keys = node->num_keys - split_point - 1;
		COPY_KEYS(node, split_point + 1, new_right, 0, new_right->num_keys);

		// Move children [split_point+1..end] to right node
		uint32_t *left_children = GET_CHILDREN(node);
		for (uint32_t i = 0; i <= new_right->num_keys; i++)
		{
			uint32_t child = left_children[split_point + 1 + i];
			if (child)
			{
				set_child(tree, new_right, i, child);
				left_children[split_point + 1 + i] = 0; // Clear from left
			}
		}

		// Left keeps first split_point keys
		node->num_keys = split_point;
	}

	return parent; // Parent might now be full and need splitting
}

/*
** Insert a key-value pair into the B+tree.
**
** Handles three cases:
**   1. Empty tree: Direct insert into root
**   2. Space available: Simple insert at correct position
**   3. Node full: Split and retry
**
** The splitting logic is iterative rather than recursive to avoid
** stack overflow with deep trees. If a split propagates to the root,
** the tree height increases by one.
**
** After splits, we re-search for the leaf because the key's proper
** location may have changed during the restructuring.
*/
static void
insert_element(btree *tree, void *key, void *data)
{
	btree_node *root = GET_ROOT();

	if (root->num_keys == 0)
	{
		// Direct insert into empty root
		MARK_DIRTY(root);
		COPY_KEY(GET_KEY_AT(root, 0), (void *)key);
		COPY_RECORD(GET_RECORD_AT(root, 0), (void *)data);
		root->num_keys = 1;
		return;
	}

	// Find leaf
	btree_node *leaf = find_leaf_for_key(tree, (void *)key);

	// Make room if needed
	if (NODE_IS_FULL(leaf))
	{

		btree_node *node = leaf;
		while (node && NODE_IS_FULL(node))
		{
			// split might propagate to parent,
			// stop when split doesn't return new parent of split node
			// and research

			node = split(tree, node);
		}

		// Re-find because splits may have moved our key
		leaf = find_leaf_for_key(tree, (void *)key);
	}

	// Now insert
	uint32_t pos = binary_search(tree, leaf, (void *)key);
	MARK_DIRTY(leaf);

	SHIFT_KEYS_RIGHT(leaf, pos, leaf->num_keys - pos);
	SHIFT_RECORDS_RIGHT(leaf, pos, leaf->num_keys - pos);

	COPY_KEY(GET_KEY_AT(leaf, pos), (void *)key);
	COPY_RECORD(GET_RECORD_AT(leaf, pos), (void *)data);
	leaf->num_keys++;
}

// ============================================================================
// DELETE AND REPAIR OPERATIONS - Clear strategy and flow
// ============================================================================

/*
** Delete a node and clean up its relationships.
**
** Handles both leaf chain maintenance (for leaf nodes) and returns
** the page to the pager's free list for reuse.
*/
static void
destroy_node(btree_node *node)
{
	unlink_leaf_node(node);
	pager_delete(node->index);
}
/*
** Handle the special case where the root becomes empty after deletion.
**
** This only occurs for internal roots with a single child remaining.
** Rather than having an empty root pointing to one child, we promote
** the child to become the new root, reducing tree height by one.
**
** Uses swap_with_root to maintain the invariant that the root stays
** at the same page index.
*/
static void
collapse_empty_root(btree *tree, btree_node *root)
{
	assert(root->num_keys == 0);
	assert(IS_INTERNAL(root));

	// The root has only one child - make it the new root
	btree_node *only_child = GET_CHILD(root, 0);

	// Swap contents: only_child becomes root, root gets child's data
	swap_with_root(tree, root, only_child);

	// Delete the old root (now at only_child's position)
	destroy_node(only_child);
}

// === BORROWING OPERATIONS (non-destructive repair) ===

/*
** Borrow an entry from the left sibling to fix underflow.
**
** For leaf nodes:
**   - Move rightmost entry from left sibling to leftmost of current node
**   - Update parent separator to reflect new boundary
**
** For internal nodes:
**   - Rotate through parent: parent separator moves down, left's last key moves up
**   - Transfer corresponding child pointer
**
** This maintains the B+tree property that parent separators correctly
** partition the key space between siblings.
*/
static void
borrow_from_left_sibling(btree *tree, btree_node *node, btree_node *left_sibling, uint32_t separator_index)
{
	btree_node *parent = GET_PARENT(node);

	MARK_DIRTY(node);
	MARK_DIRTY(left_sibling);
	MARK_DIRTY(parent);

	// Make room at the beginning of node
	SHIFT_KEYS_RIGHT(node, 0, node->num_keys);

	if (IS_LEAF(node))
	{
		// For leaves: move last entry from left to first of node
		SHIFT_RECORDS_RIGHT(node, 0, node->num_keys);

		// Copy entry from left's end to node's beginning
		COPY_KEY(GET_KEY_AT(node, 0), GET_KEY_AT(left_sibling, left_sibling->num_keys - 1));
		COPY_RECORD(GET_RECORD_AT(node, 0), GET_RECORD_AT(left_sibling, left_sibling->num_keys - 1));

		// Update parent separator to be the new first key of node
		COPY_KEY(GET_KEY_AT(parent, separator_index), GET_KEY_AT(node, 0));
	}
	else
	{
		// For internals: rotate through parent
		// Parent separator moves down to node
		COPY_KEY(GET_KEY_AT(node, 0), GET_KEY_AT(parent, separator_index));

		// Left's last key moves up to parent
		COPY_KEY(GET_KEY_AT(parent, separator_index), GET_KEY_AT(left_sibling, left_sibling->num_keys - 1));

		// Move corresponding child pointer
		uint32_t *node_children = GET_CHILDREN(node);
		uint32_t *left_children = GET_CHILDREN(left_sibling);

		// Shift node's children right and add left's last child as first
		for (uint32_t i = node->num_keys + 1; i > 0; i--)
		{
			set_child(tree, node, i, node_children[i - 1]);
		}
		set_child(tree, node, 0, left_children[left_sibling->num_keys]);
	}

	node->num_keys++;
	left_sibling->num_keys--;
}

/*
** Borrow an entry from the right sibling to fix underflow.
**
** Mirror operation of borrow_from_left_sibling, moving entries
** in the opposite direction.
*/
static void
borrow_from_right_sibling(btree *tree, btree_node *node, btree_node *right_sibling, uint32_t separator_index)
{
	btree_node *parent = GET_PARENT(node);

	MARK_DIRTY(node);
	MARK_DIRTY(right_sibling);
	MARK_DIRTY(parent);

	if (IS_LEAF(node))
	{
		// For leaves: move first entry from right to end of node
		COPY_KEY(GET_KEY_AT(node, node->num_keys), GET_KEY_AT(right_sibling, 0));
		COPY_RECORD(GET_RECORD_AT(node, node->num_keys), GET_RECORD_AT(right_sibling, 0));

		// Shift right sibling's entries left
		SHIFT_KEYS_LEFT(right_sibling, 0, right_sibling->num_keys - 1);
		SHIFT_RECORDS_LEFT(right_sibling, 0, right_sibling->num_keys - 1);

		// Update parent separator to be the new first key of right
		COPY_KEY(GET_KEY_AT(parent, separator_index), GET_KEY_AT(right_sibling, 0));
	}
	else
	{
		// For internals: rotate through parent
		// Parent separator moves down to node
		COPY_KEY(GET_KEY_AT(node, node->num_keys), GET_KEY_AT(parent, separator_index));

		// Right's first key moves up to parent
		COPY_KEY(GET_KEY_AT(parent, separator_index), GET_KEY_AT(right_sibling, 0));

		// Move corresponding child pointer
		uint32_t *right_children = GET_CHILDREN(right_sibling);
		set_child(tree, node, node->num_keys + 1, right_children[0]);

		// Shift right's keys and children left
		SHIFT_KEYS_LEFT(right_sibling, 0, right_sibling->num_keys - 1);
		for (uint32_t i = 0; i < right_sibling->num_keys; i++)
		{
			set_child(tree, right_sibling, i, right_children[i + 1]);
		}
	}

	node->num_keys++;
	right_sibling->num_keys--;
}

/*
** Attempt to borrow from either sibling to fix underflow.
**
** Tries left sibling first for consistency. Borrowing is preferred
** over merging because it's non-destructive and maintains the same
** number of nodes in the tree.
**
** Returns: true if borrowing succeeded, false if both siblings are minimal
*/
static bool
try_borrow_from_siblings(btree *tree, btree_node *node)
{
	btree_node *parent = GET_PARENT(node);
	uint32_t	child_index = find_child_index(tree, parent, node);

	// Try left sibling first (consistent strategy)
	if (child_index > 0)
	{
		btree_node *left = GET_CHILD(parent, child_index - 1);
		if (NODE_CAN_SPARE(left))
		{
			borrow_from_left_sibling(tree, node, left, child_index - 1);
			return true;
		}
	}

	// Try right sibling
	if (child_index < parent->num_keys)
	{
		btree_node *right = GET_CHILD(parent, child_index + 1);
		if (NODE_CAN_SPARE(right))
		{
			borrow_from_right_sibling(tree, node, right, child_index);
			return true;
		}
	}

	return false;
}

// === MERGE OPERATION (destructive repair) ===

/*
** Merge an underflowing node with a sibling.
**
** When borrowing isn't possible (both siblings are minimal), we must
** merge the underflowing node with a sibling. This reduces the number
** of nodes and may cause the parent to underflow.
**
** For leaves: Concatenate all entries into the left node
** For internals: Pull down separator from parent and concatenate
**
** Returns: Parent node (which may now underflow and need repair)
*/
static btree_node *
perform_merge_with_sibling(btree *tree, btree_node *node)
{
	btree_node *parent = GET_PARENT(node);
	uint32_t	child_index = find_child_index(tree, parent, node);

	btree_node *left, *right;
	uint32_t	separator_index;

	// Decide which sibling to merge with
	// Prefer merging with right sibling (consistent strategy)
	if (child_index < parent->num_keys)
	{
		// Merge with right sibling
		left = node;
		right = GET_CHILD(parent, child_index + 1);
		separator_index = child_index;
	}
	else
	{

		assert(child_index > 0);
		// We're the rightmost child, merge with left sibling
		left = GET_CHILD(parent, child_index - 1);
		right = node;
		separator_index = child_index - 1;
	}

	// === MERGE LOGIC ===
	assert(left->index == GET_CHILDREN(parent)[separator_index]);
	assert(right->index == GET_CHILDREN(parent)[separator_index + 1]);

	MARK_DIRTY(left);
	MARK_DIRTY(parent);

	if (IS_LEAF(left))
	{
		// For leaves: concatenate all entries
		COPY_KEYS(right, 0, left, left->num_keys, right->num_keys);
		COPY_RECORDS(right, 0, left, left->num_keys, right->num_keys);
		left->num_keys += right->num_keys;

		// Update leaf chain
		link_leaf_nodes(left, GET_NEXT(right));
	}
	else
	{
		// For internals: bring down separator and concatenate
		// Copy separator from parent into left
		COPY_KEY(GET_KEY_AT(left, left->num_keys), GET_KEY_AT(parent, separator_index));

		// Copy all keys from right
		COPY_KEYS(right, 0, left, left->num_keys + 1, right->num_keys);

		// Move all children from right
		uint32_t *right_children = GET_CHILDREN(right);
		for (uint32_t i = 0; i <= right->num_keys; i++)
		{
			set_child(tree, left, left->num_keys + 1 + i, right_children[i]);
		}

		left->num_keys += 1 + right->num_keys;
	}

	// Remove separator and right child from parent
	SHIFT_KEYS_LEFT(parent, separator_index, parent->num_keys - separator_index - 1);
	SHIFT_CHILDREN_LEFT(parent, separator_index + 1, parent->num_keys - separator_index - 1);
	parent->num_keys--;

	// Delete the now-empty right node
	destroy_node(right);

	return parent;
}

// === MAIN REPAIR FUNCTION ===

/*
** Fix an underflowing node after deletion.
**
** Uses a two-phase strategy:
**   1. Try borrowing from siblings (non-destructive)
**   2. If that fails, merge with a sibling (destructive)
**
** The repair may cascade up the tree if merging causes the parent
** to underflow. The root is special-cased: it can have fewer than
** the minimum keys, but if it becomes completely empty (internal
** with no keys), we collapse it.
*/
static void
repair_underflow(btree *tree, btree_node *node)
{
	// Step 1: Check if repair is needed
	if (!IS_UNDERFLOWING(node))
	{
		return;
	}

	// Root is allowed to have fewer keys
	if (IS_ROOT(node))
	{
		return;
	}

	// Step 2: Try non-destructive fix (borrow from sibling)
	if (try_borrow_from_siblings(tree, node))
	{
		return;
	}

	// Step 3: Destructive fix (merge with sibling)
	btree_node *parent = perform_merge_with_sibling(tree, node);

	// Step 4: Check if parent needs repair (cascade)
	if (parent && IS_UNDERFLOWING(parent))
	{
		if (IS_ROOT(parent) && parent->num_keys == 0)
		{
			collapse_empty_root(tree, parent);
		}
		else
		{
			repair_underflow(tree, parent); // Recursive
		}
	}
}

/*
** Delete an entry from a leaf node.
**
** Removes the key-value pair at the specified index and handles
** any resulting underflow. The deletion only occurs in leaf nodes
** since B+trees store all data in leaves.
**
** Special case: Root leaf with one entry becomes empty but remains valid.
*/
static void
delete_element(btree *tree, btree_node *node, void *key, uint32_t index)
{
	assert(IS_LEAF(node));

	// Special case: deleting last entry from root leaf
	if (IS_ROOT(node) && node->num_keys == 1)
	{
		MARK_DIRTY(node);
		node->num_keys = 0;
		return;
	}

	MARK_DIRTY(node);

	// Remove the entry by shifting remaining entries left
	uint32_t entries_to_shift = node->num_keys - index - 1;
	SHIFT_KEYS_LEFT(node, index, entries_to_shift);
	SHIFT_RECORDS_LEFT(node, index, entries_to_shift);
	node->num_keys--;

	// Fix underflow if necessary
	repair_underflow(tree, node);
}
/*
** Recursively delete all nodes in the tree.
**
** Post-order traversal ensures children are deleted before their parents,
** preventing dangling references. Each node's page is returned to the
** pager's free list for reuse.
*/
void
clear_recurse(btree *tree, btree_node *node)
{
	if (IS_LEAF(node))
	{
		pager_delete(node->index);
		return;
	}

	uint32_t	i = 0;
	btree_node *child = GET_CHILD(node, i);
	while (child != nullptr)
	{
		clear_recurse(tree, child);
		child = GET_CHILD(node, i++);
	}

	pager_delete(node->index);
}

/*
** Clear all data from the B+tree.
**
** Deallocates all nodes and resets the tree to empty state.
** The tree structure itself remains valid for future use.
*/
bool
bt_clear(btree *tree)
{
	if (0 == tree->root_page_index)
	{
		// unitialised table
		return true;
	}

	clear_recurse(tree, GET_NODE(tree->root_page_index));
	return true;
}

/*
** Initialize a new B+tree structure.
**
** Calculates optimal node capacities based on key and record sizes,
** ensuring efficient space utilization while maintaining B+tree invariants.
**
** The calculations account for:
**   - Node header overhead (24 bytes)
**   - Different layouts for leaf vs internal nodes
**   - Minimum entry count for proper splits/merges
**
** Internal nodes require special handling:
**   - Even max_keys: min = (max/2 - 1) to ensure balanced splits
**   - Odd max_keys: min = max/2 (integer division)
**
** Parameters:
**   key         - Data type of keys
**   record_size - Size of each record in bytes
**   init        - If true, create an empty root node
**
** Returns: Initialized BTree structure (zero-initialized on failure)
*/
btree
bt_create(data_type key, uint32_t record_size, bool init = false)
{
	btree tree = {0};

	tree.node_key_type = key;
	tree.node_key_size = type_size(key);

	tree.record_size = record_size;

	constexpr uint32_t USABLE_SPACE = PAGE_SIZE - NODE_HEADER_SIZE;

	if (record_size * MIN_ENTRY_COUNT > USABLE_SPACE)
	{
		// return invalid tree
		return tree;
	}

	uint32_t leaf_entry_size = tree.node_key_size + record_size;
	uint32_t leaf_max_entries = USABLE_SPACE / leaf_entry_size;

	tree.leaf_max_keys = MIN_ENTRY_COUNT > leaf_max_entries ? MIN_ENTRY_COUNT : leaf_max_entries;
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

	tree.internal_max_keys = MIN_ENTRY_COUNT > internal_max_entries ? MIN_ENTRY_COUNT : internal_max_entries;
	tree.internal_split_index = tree.internal_max_keys / 2;

	if (init)
	{
		btree_node *root = create_node(&tree, true);
		tree.root_page_index = root->index;
	}
	return tree;
}

// ============================================================================
// CURSOR IMPLEMENTATION
// ============================================================================

/*
** The cursor provides a stateful position within the B+tree, enabling
** efficient iteration and positioned updates. Cursors operate exclusively
** at the leaf level since all data resides in leaf nodes.
**
** Cursor state consists of:
**   - leaf_page: Current leaf node's page index
**   - leaf_index: Position within that leaf (0 to num_keys-1)
**   - state: Valid or invalid flag
*/

/*
** Reset cursor to invalid state.
**
** Used internally when cursor operations fail or when initializing
** a seek operation.
*/
static void
cursor_clear(bt_cursor *cursor)
{
	cursor->leaf_page = 0;
	cursor->leaf_index = 0;
	cursor->state = BT_CURSOR_INVALID;
}

/*
** Move cursor to first or last entry in the tree.
**
** Helper for bt_cursorfirst/last. Handles empty tree case.
*/
static bool
cursor_move_end(bt_cursor *cursor, bool left)
{

	btree *tree = cursor->tree;
	cursor_clear(cursor);

	btree_node *root = GET_ROOT();
	if (!root || root->num_keys == 0)
	{
		cursor->state = BT_CURSOR_INVALID;
		return false;
	}

	btree_node *current = root;

	while (!IS_LEAF(current))
	{
		if (left)
		{
			current = GET_CHILD(current, 0);
		}
		else
		{
			uint32_t child_pos = current->num_keys;

			current = GET_CHILD(current, child_pos);
			assert(current);
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
	cursor->state = BT_CURSOR_VALID;
	return true;
}

/*
** Position cursor based on key and comparison operator.
**
** Supports multiple comparison modes:
**   EQ: Exact match only
**   GE/GT: First key >= or > target
**   LE/LT: Last key <= or < target
**
** For range operations (non-EQ), the cursor iterates from the initial
** position until finding a key satisfying the condition.
**
** Returns: true if a matching key was found, false otherwise
*/
bool
bt_cursorseek(bt_cursor *cursor, void *key, comparison_op op)
{
	btree *tree = cursor->tree;
	cursor_clear(cursor);

	if (!cursor->tree->root_page_index)
	{
		cursor->state = BT_CURSOR_INVALID;
		return false;
	}

	// Find the leaf and position
	btree_node *leaf = find_leaf_for_key(cursor->tree, key);
	uint32_t	index = binary_search(cursor->tree, leaf, key);

	cursor->leaf_page = leaf->index;

	// Check for exact match
	bool exact = index < leaf->num_keys && type_equals(cursor->tree->node_key_type, GET_KEY_AT(leaf, index), key);

	// Handle EQ case immediately
	if (op == EQ)
	{
		if (exact)
		{
			cursor->leaf_index = index;
			cursor->state = BT_CURSOR_VALID;
			return true;
		}
		cursor->state = BT_CURSOR_INVALID;
		return false;
	}

	// Position cursor for iteration
	if (index >= leaf->num_keys)
	{
		cursor->leaf_index = leaf->num_keys - 1;
	}
	else
	{
		cursor->leaf_index = index;
	}
	cursor->state = BT_CURSOR_VALID;

	// For exact match cases with GE/LE
	bool exact_match_ok = (op == GE || op == LE);
	if (exact && exact_match_ok)
	{
		return true;
	}

	// Need to find the right position for GT/LT/GE/LE
	bool forward = (op == GE || op == GT);

	do
	{
		void *current_key = bt_cursorkey(cursor);
		if (!current_key)
		{
			continue;
		}

		bool satisfied = (op == GE && type_greater_equal(cursor->tree->node_key_type, current_key, key)) ||
						 (op == GT && type_greater_than(cursor->tree->node_key_type, current_key, key)) ||
						 (op == LE && type_less_equal(cursor->tree->node_key_type, current_key, key)) ||
						 (op == LT && type_less_than(cursor->tree->node_key_type, current_key, key));

		if (satisfied)
		{
			return true;
		}

	} while (forward ? bt_cursornext(cursor) : bt_cursorprevious(cursor));

	cursor->state = BT_CURSOR_INVALID;
	return false;
}

// ============================================================================
// PUBLIC CURSOR INTERFACE
// ============================================================================

/*
** Check if cursor points to a valid position.
*/
bool
bt_cursoris_valid(bt_cursor *cursor)
{
	return cursor->state == BT_CURSOR_VALID;
}

/*
** Get pointer to key at current cursor position.
**
** Returns: Pointer to key data, or nullptr if cursor invalid
** Note: Pointer becomes invalid after any tree modification
*/
void *
bt_cursorkey(bt_cursor *cursor)
{

	btree *tree = cursor->tree;
	if (cursor->state != BT_CURSOR_VALID)
	{
		return nullptr;
	}

	btree_node *node = GET_NODE(cursor->leaf_page);
	if (!node || cursor->leaf_index >= node->num_keys)
	{
		return nullptr;
	}

	return GET_KEY_AT(node, cursor->leaf_index);
}

/*
** Get pointer to record at current cursor position.
**
** Returns: Pointer to record data, or nullptr if cursor invalid
** Note: Pointer becomes invalid after any tree modification
*/
void *
bt_cursorrecord(bt_cursor *cursor)
{

	btree *tree = cursor->tree;
	if (cursor->state != BT_CURSOR_VALID)
	{
		return nullptr;
	}

	btree_node *node = GET_NODE(cursor->leaf_page);
	if (!node || cursor->leaf_index >= node->num_keys)
	{
		return nullptr;
	}

	return GET_RECORD_AT(node, cursor->leaf_index);
}

/*
** Delete entry at current cursor position.
**
** After deletion, cursor moves to the previous entry in the node
** if at the last position, or stays at the same index (which now
** contains the next entry due to shifting).
*/
bool
bt_cursordelete(bt_cursor *cursor)
{
	if (cursor->state != BT_CURSOR_VALID)
	{
		return false;
	}

	void *key = bt_cursorkey(cursor);
	if (!key)
	{
		return false;
	}

	auto	 node = GET_NODE(cursor->leaf_page);
	uint32_t index = node->index;

	delete_element(cursor->tree, node, key, cursor->leaf_index);

	// node will survive
	node = GET_NODE(cursor->leaf_page);
	assert(node);

	if (cursor->leaf_index >= node->num_keys)
	{
		if (node->num_keys > 0)
		{
			cursor->leaf_index = node->num_keys - 1;
		}
		else
		{
			cursor->state = BT_CURSOR_INVALID;
		}
	}

	return true;
}

/*
** Insert a new key-value pair.
**
** Returns false if key already exists (no duplicates allowed).
** Cursor position becomes undefined after insertion.
*/
bool
bt_cursorinsert(bt_cursor *cursor, void *key, void *record)
{

	if (bt_cursorseek(cursor, key))
	{
		return false;
	}

	insert_element(cursor->tree, key, record);
	return true;
}

/*
** Update record at current cursor position.
**
** Only modifies the record data, not the key. The cursor
** position remains valid after update.
*/
bool
bt_cursorupdate(bt_cursor *cursor, void *record)
{
	if (cursor->state != BT_CURSOR_VALID)
	{
		return false;
	}
	pager_mark_dirty(cursor->leaf_page);
	void *data = bt_cursorrecord(cursor);
	memcpy(data, record, cursor->tree->record_size);
	return true;
}

/*
** Move cursor to first entry in tree.
*/
bool
bt_cursorfirst(bt_cursor *cursor)
{
	return cursor_move_end(cursor, true);
}

/*
** Move cursor to last entry in tree.
*/
bool
bt_cursorlast(bt_cursor *cursor)
{
	return cursor_move_end(cursor, false);
}

/*
** Advance cursor to next entry.
**
** Uses the leaf chain for efficient traversal without ascending
** to parent nodes. Returns false at end of tree.
*/
bool
bt_cursornext(bt_cursor *cursor)
{
	if (cursor->state != BT_CURSOR_VALID)
	{
		return false;
	}

	btree_node *node = GET_NODE(cursor->leaf_page);
	if (!node)
	{
		cursor->state = BT_CURSOR_INVALID;
		return false;
	}

	cursor->leaf_index++;

	if (cursor->leaf_index >= node->num_keys)
	{
		if (node->next != 0)
		{
			btree_node *next_node = GET_NEXT(node);
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

/*
** Move cursor to previous entry.
**
** Uses the leaf chain for efficient backward traversal.
** Returns false at beginning of tree.
*/
bool
bt_cursorprevious(bt_cursor *cursor)
{
	if (cursor->state != BT_CURSOR_VALID)
	{
		return false;
	}

	btree_node *node = GET_NODE(cursor->leaf_page);
	if (!node)
	{
		cursor->state = BT_CURSOR_INVALID;
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
		btree_node *prev_node = GET_PREV(node);
		if (prev_node && prev_node->num_keys > 0)
		{
			cursor->leaf_page = prev_node->index;
			cursor->leaf_index = prev_node->num_keys - 1;
			return true;
		}
	}

	return false;
}

/*
** Check if cursor can move forward without changing position.
*/
bool
bt_cursorhas_next(bt_cursor *cursor)
{
	if (bt_cursornext(cursor))
	{
		bt_cursorprevious(cursor);
		return true;
	}
	return false;
}

/*
** Check if cursor can move backward without changing position.
*/
bool
bt_cursorhas_previous(bt_cursor *cursor)
{
	if (bt_cursorprevious(cursor))
	{
		bt_cursornext(cursor);
		return true;
	}
	return false;
}

/*NOCOVER_START*/

#include <unordered_set>

#define ASSERT_PRINT(condition, tree)                                                                                  \
	if (!(condition))                                                                                                  \
	{                                                                                                                  \
		btree_print(tree);                                                                                             \
		assert(condition);                                                                                             \
	}

// Validation result structure to pass information up the recursion
struct validation_result
{
	uint32_t	depth;
	uint8_t	   *min_key;
	uint8_t	   *max_key;
	btree_node *leftmost_leaf;
	btree_node *rightmost_leaf;
};

// Forward declaration
static validation_result
validate_node_recursive(btree *tree, btree_node *node, uint32_t expected_parent, void *parent_min_bound,
						void *parent_max_bound, std::unordered_set<uint32_t> &visited);

// Main validation function
void
bt_validate(btree *tree_ptr)
{

	btree *tree = tree_ptr;
	ASSERT_PRINT(tree_ptr != nullptr, tree_ptr);

	// Empty tree is valid
	if (tree_ptr->root_page_index == 0)
	{
		return;
	}

	btree_node *root = GET_ROOT();
	ASSERT_PRINT(root != nullptr, tree_ptr);

	// Root specific checks
	ASSERT_PRINT(IS_ROOT(root), tree_ptr); // Root has no parent
	ASSERT_PRINT(root->index == tree_ptr->root_page_index, tree_ptr);

	// Track visited nodes to detect cycles
	std::unordered_set<uint32_t> visited;

	// Validate tree recursively
	validation_result result = validate_node_recursive(tree, root, 0, nullptr, nullptr, visited);

	// If tree has data, verify leaf chain integrity
	if (IS_LEAF(root) && root->num_keys > 0)
	{
		// Single leaf root should have no siblings
		ASSERT_PRINT(root->next == 0, tree_ptr);
		ASSERT_PRINT(root->previous == 0, tree_ptr);
	}
	else if (IS_INTERNAL(root))
	{
		// Verify complete leaf chain by walking it
		btree_node					*first_leaf = result.leftmost_leaf;
		btree_node					*current = first_leaf;
		std::unordered_set<uint32_t> leaf_visited;

		ASSERT_PRINT(current->previous == 0, tree_ptr); // First leaf has no previous

		while (current)
		{
			ASSERT_PRINT(IS_LEAF(current), tree_ptr);
			ASSERT_PRINT(leaf_visited.find(current->index) == leaf_visited.end(), tree_ptr); // No cycles in leaf chain
			leaf_visited.insert(current->index);

			if (current->next != 0)
			{
				btree_node *next = GET_NEXT(current);
				ASSERT_PRINT(next != nullptr, tree_ptr);
				ASSERT_PRINT(next->previous == current->index, tree_ptr); // Bidirectional link integrity
				current = next;
			}
			else
			{
				ASSERT_PRINT(current == result.rightmost_leaf, tree_ptr); // Last leaf matches rightmost
				break;
			}
		}
	}
}
static validation_result
validate_node_recursive(btree *tree, btree_node *node, uint32_t expected_parent, void *parent_min_bound,
						void *parent_max_bound, std::unordered_set<uint32_t> &visited)
{
	ASSERT_PRINT(node != nullptr, tree);

	// Check for cycles
	ASSERT_PRINT(visited.find(node->index) == visited.end(), tree);
	visited.insert(node->index);

	// Verify parent pointer
	ASSERT_PRINT(node->parent == expected_parent, tree);

	// Check key count constraints
	uint32_t max_keys = GET_MAX_KEYS(node);
	uint32_t min_keys = GET_MIN_KEYS(node);

	ASSERT_PRINT(node->num_keys <= max_keys, tree);

	// Non-root nodes must meet minimum
	if (expected_parent != 0)
	{
		ASSERT_PRINT(node->num_keys >= min_keys, tree);
	}
	else
	{
		// Root can have fewer, but not zero (unless tree is being cleared)
		if (node->num_keys == 0)
		{
			ASSERT_PRINT(IS_LEAF(node), tree); // Only leaf root can be empty during deletion
		}
	}

	// Validate key ordering and bounds
	void *prev_key = nullptr;
	void *first_key = nullptr;
	void *last_key = nullptr;

	for (uint32_t i = 0; i < node->num_keys; i++)
	{
		void *current_key = GET_KEY_AT(node, i);

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
			ASSERT_PRINT(type_less_than(tree->node_key_type, prev_key, current_key), tree); // prev < current
		}

		// Check bounds from parent
		if (parent_min_bound)
		{
			ASSERT_PRINT(type_greater_equal(tree->node_key_type, current_key, parent_min_bound), tree);
		}
		if (parent_max_bound)
		{
			ASSERT_PRINT(type_less_than(tree->node_key_type, current_key, parent_max_bound), tree);
		}
		prev_key = current_key;
	}

	validation_result result;
	result.min_key = (uint8_t *)first_key;
	result.max_key = (uint8_t *)last_key;

	if (IS_LEAF(node))
	{
		result.depth = 0;
		result.leftmost_leaf = node;
		result.rightmost_leaf = node;

		// Validate leaf data exists
		void *records = GET_RECORD_DATA(node);
		ASSERT_PRINT(records != nullptr, tree);

		// Verify leaf chain pointers are valid page indices or 0
		if (node->next != 0)
		{
			ASSERT_PRINT(node->next != node->index, tree); // No self-reference
			btree_node *next = GET_NEXT(node);
			ASSERT_PRINT(next != nullptr, tree);
			ASSERT_PRINT(IS_LEAF(next), tree);
		}
		if (node->previous != 0)
		{
			ASSERT_PRINT(node->previous != node->index, tree); // No self-reference
			btree_node *prev = GET_PREV(node);
			ASSERT_PRINT(prev != nullptr, tree);
			ASSERT_PRINT(IS_LEAF(prev), tree);
		}
	}
	else
	{
		// Internal node validation
		uint32_t *children = GET_CHILDREN(node);
		ASSERT_PRINT(children != nullptr, tree);

		uint32_t	child_depth = UINT32_MAX;
		btree_node *leftmost_leaf = nullptr;
		btree_node *rightmost_leaf = nullptr;

		// Internal nodes have num_keys + 1 children
		for (uint32_t i = 0; i <= node->num_keys; i++)
		{
			ASSERT_PRINT(children[i] != 0, tree);			// No null children
			ASSERT_PRINT(children[i] != node->index, tree); // No self-reference

			btree_node *child = GET_CHILD(node, i);
			ASSERT_PRINT(child != nullptr, tree);

			// Determine bounds for this child
			void *child_min = (i == 0) ? parent_min_bound : GET_KEY_AT(node, i - 1);
			void *child_max = (i == node->num_keys) ? parent_max_bound : GET_KEY_AT(node, i);

			validation_result child_result =
				validate_node_recursive(tree, child, node->index, child_min, child_max, visited);

			// All children must have same depth
			if (child_depth == UINT32_MAX)
			{
				child_depth = child_result.depth;
				leftmost_leaf = child_result.leftmost_leaf;
			}
			else
			{
				ASSERT_PRINT(child_depth == child_result.depth, tree);
			}

			// Track rightmost leaf
			rightmost_leaf = child_result.rightmost_leaf;

			// Verify key bounds match child contents
			if (child_result.min_key && i > 0)
			{
				// First key in child >= separator key before it
				void *separator = GET_KEY_AT(node, i - 1);
				ASSERT_PRINT(type_greater_equal(tree->node_key_type, child_result.min_key, separator), tree);
			}
			if (child_result.max_key && i < node->num_keys)
			{
				// Last key in child < separator key after it
				void *separator = GET_KEY_AT(node, i);
				ASSERT_PRINT(type_less_equal(tree->node_key_type, child_result.max_key, separator), tree);
			}
		}

		result.depth = child_depth + 1;
		result.leftmost_leaf = leftmost_leaf;
		result.rightmost_leaf = rightmost_leaf;

		// Internal nodes should not have leaf chain pointers
		ASSERT_PRINT(node->next == 0, tree);
		ASSERT_PRINT(node->previous == 0, tree);
	}

	return result;
}

// Add this to bplustree.cpp

// Helper to print a single key based on type
static void
print_key(btree *tree, void *key)
{
	if (!key)
	{
		printf("NULL");
		return;
	}
	type_print(tree->node_key_type, key);
}

// Main B+Tree print function
void
btree_print(btree *tree_ptr)
{
	btree *tree = tree_ptr;
	if (!tree_ptr || tree_ptr->root_page_index == 0)
	{
		printf("B+Tree: EMPTY\n");
		return;
	}

	printf("====================================\n");
	printf("B+Tree Structure (BFS)\n");
	printf("====================================\n");
	printf("Root: page_%u\n", tree_ptr->root_page_index);
	printf("Key type: %s, Record size: %u bytes\n", type_name(tree_ptr->node_key_type), tree_ptr->record_size);
	printf("Internal: max_keys=%u, min_keys=%u\n", tree_ptr->internal_max_keys, tree_ptr->internal_min_keys);
	printf("Leaf: max_keys=%u, min_keys=%u\n", tree_ptr->leaf_max_keys, tree_ptr->leaf_min_keys);
	printf("------------------------------------\n\n");

	// BFS traversal using two queues (current level and next level)
	queue<uint32_t, query_arena> current_level;
	queue<uint32_t, query_arena> next_level;

	current_level.push(tree_ptr->root_page_index);
	uint32_t depth = 0;

	while (!current_level.empty())
	{
		printf("LEVEL %u:\n", depth);
		printf("--------\n");

		while (!current_level.empty())
		{
			uint32_t page_index = *current_level.front();
			current_level.pop();

			btree_node *node = GET_NODE(page_index);
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
				print_key(tree_ptr, GET_KEY_AT(node, i));
			}
			printf("]\n");

			// Print children for internal nodes
			if (IS_INTERNAL(node))
			{
				uint32_t *children = GET_CHILDREN(node);
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
	btree_node *current = GET_ROOT();
	while (IS_INTERNAL(current))
	{
		current = GET_CHILD(current, 0);
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
btree_print_compact(btree *tree_ptr)
{
	btree *tree = tree_ptr;
	if (!tree_ptr || tree_ptr->root_page_index == 0)
	{
		printf("B+Tree: EMPTY\n");
		return;
	}

	printf("B+Tree (page:type:keys:parent):\n");

	struct temp_arena
	{
	};
	queue<uint32_t, temp_arena> q;
	queue<uint32_t, temp_arena> levels;

	q.push(tree_ptr->root_page_index);
	levels.push(0);

	uint32_t current_level = 0;

	while (!q.empty())
	{
		uint32_t page_index = *q.front();
		uint32_t level = *levels.front();
		q.pop();
		levels.pop();

		if (level != current_level)
		{
			printf("\n");
			current_level = level;
		}

		btree_node *node = GET_NODE(page_index);
		if (!node)
			continue;

		// Print: page_index:type:num_keys:parent
		printf("[%u:%c:%u:%u] ", node->index, IS_LEAF(node) ? 'L' : 'I', node->num_keys, node->parent);

		// Add children to queue
		if (IS_INTERNAL(node))
		{
			uint32_t *children = GET_CHILDREN(node);
			for (uint32_t i = 0; i <= node->num_keys; i++)
			{
				q.push(children[i]);
				levels.push(level + 1);
			}
		}
	}
	printf("\n");
}
/*NOCOVER_END*/
