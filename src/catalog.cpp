#include "catalog.hpp"
#include "arena.hpp"
#include "pager.hpp"
#include "types.hpp"
#include <cassert>

hash_map<string<catalog_arena>, Structure, catalog_arena> catalog;

Layout
Structure::to_layout()
{
	array<DataType> column_types;
	column_types.reserve(columns.size);
	for (size_t i = 0; i < columns.size; i++)
	{
		column_types[i] = columns[i].type;
	}
	return Layout::create(column_types);
}

Layout
Layout::create(array<DataType> &cols)
{
	Layout layout;
	layout.layout = cols;
	layout.offsets.reserve(cols.size);

	int offset = 0;
	layout.offsets.push(0);
	for (int i = 1; i < cols.size; i++)
	{
		offset += type_size(cols[i]);
		layout.offsets.push(offset);
	}
	layout.record_size = offset;
	return layout;
}

Structure
Structure::from(const char *name, array<Column> cols)
{
	Structure structure;
	structure.columns = cols;
	structure.name = name;
	return structure;
}

void
bootstrap_master(bool create)
{
	array<Column> cols;
	cols.data = nullptr;
	cols.size = 0;
	cols.capacity = 0;

	// type: "table" or "index"
	cols.push(Column{MC_TYPE, TYPE_CHAR16});

	// name: object name
	cols.push(Column{MC_NAME, TYPE_CHAR32});

	// tbl_name: table that this object belongs to
	// (same as name for tables, parent table for indexes)
	cols.push(Column{MC_TBL_NAME, TYPE_CHAR32});

	// rootpage: root page number in the btree
	cols.push(Column{MC_ROOTPAGE, TYPE_U32});

	// sql: CREATE statement (using largest fixed char type)
	cols.push(Column{MC_SQL, TYPE_CHAR256});

	Structure structure = Structure::from(MASTER_CATALOG, cols);
	Layout	  layout = structure.to_layout();
	if (create)
	{
		pager_begin_transaction();
		structure.storage.btree = btree_create(layout.key_type(), layout.record_size, create);
		assert(1 == structure.storage.btree.root_page_index);
		// validate
		pager_commit();
	}
	else
	{
		structure.storage.btree = btree_create(layout.key_type(), layout.record_size, create);
		structure.storage.btree.root_page_index = 1;
	}

	string<catalog_arena> s;
	s.set(MASTER_CATALOG);
	catalog[s] = structure;
}
