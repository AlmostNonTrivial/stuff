#include "catalog.hpp"
#include "arena.hpp"
#include "common.hpp"
#include "containers.hpp"
#include "pager.hpp"
#include "types.hpp"
#include <cassert>
#include <cstring>
#include <string_view>
#include "compile.hpp"

hash_map<string_view, relation, catalog_arena> catalog;

/**
 * Creates a format descriptor for tuples with the given column types.
 * The first column is treated as the key and stored separately in the
 * btree, so offsets begin from the second column.
 */
tuple_format
tuple_format_from_types(array<data_type, query_arena> &columns)
{
	tuple_format format = {};

	// First column is always the key
	format.key_type = columns[0];
	format.columns.copy_from(columns);

	// Calculate offsets for record portion (excluding key)
	// The key is stored separately in the btree node
	int offset = 0;
	format.offsets.push(0); // First record column starts at offset 0

	// Start from 1 because column 0 is the key
	for (int i = 1; i < columns.size(); i++)
	{
		offset += type_size(columns[i]);
		format.offsets.push(offset);
	}

	format.record_size = offset;
	return format;
}

tuple_format
tuple_format_from_relation(relation &schema)
{
	array<data_type, query_arena> column_types;

	for (auto col : schema.columns)
	{
		column_types.push(col.type);
	}

	return tuple_format_from_types(column_types);
}

relation
create_relation(string_view name, array<attribute, query_arena> columns)
{
	relation rel = {};

	rel.columns.copy_from(columns);

	sv_to_cstr(name, rel.name, RELATION_NAME_MAX_SIZE);
	return rel;
}

void
bootstrap_master(bool is_new_database)
{

	array<attribute, query_arena> master_columns = {{MC_ID, TYPE_U32},
													{MC_NAME, TYPE_CHAR32},
													{MC_TBL_NAME, TYPE_CHAR32},
													{MC_ROOTPAGE, TYPE_U32},
													{MC_SQL, TYPE_CHAR256}};

	relation master_table = create_relation(MASTER_CATALOG, master_columns);
	master_table.next_key.type = TYPE_U32;
	master_table.next_key.data = arena<catalog_arena>::alloc(type_size(TYPE_U32));
	type_zero(master_table.next_key.type, master_table.next_key.data);

	tuple_format layout = tuple_format_from_relation(master_table);

	if (is_new_database)
	{
		// Create new master catalog
		pager_begin_transaction();
		master_table.storage.btree = bt_create(layout.key_type, layout.record_size, is_new_database);

		// Master catalog MUST be at page 1
		assert(1 == master_table.storage.btree.root_page_index);

		pager_commit();
	}
	else
	{
		master_table.storage.btree = bt_create(layout.key_type, layout.record_size, is_new_database);
		master_table.storage.btree.root_page_index = 1;
	}

	catalog.insert(MASTER_CATALOG, master_table);
}

void
catalog_reload()
{
	catalog.reset();
	arena<catalog_arena>::reset_and_decommit();

	bootstrap_master(false);

	load_catalog_from_master();
}
