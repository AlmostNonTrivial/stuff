#include "catalog.hpp"
#include "arena.hpp"
#include "containers.hpp"
#include "compile.hpp"
#include "pager.hpp"
#include "parser.hpp"
#include "types.hpp"
#include <cassert>
#include <cstdint>

hash_map<string<catalog_arena>, Structure, catalog_arena> catalog;

Layout
Structure::to_layout()
{
	array<DataType> column_types;
	column_types.reserve(columns.size());
	for (size_t i = 0; i < columns.size(); i++)
	{
		column_types.push(columns[i].type);
	}
	return Layout::create(column_types);
}

Layout
Layout::create(array<DataType> &cols)
{
	Layout layout;
	layout.layout = cols;
	layout.offsets.reserve(cols.size());

	int offset = 0;
	layout.offsets.push(0);
	for (int i = 1; i < cols.size(); i++)
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
	structure.columns.set(cols);
	structure.name = name;
	return structure;
}

void
bootstrap_master(bool create)
{
	array<Column> cols;
	cols.reset();

	// type: "table" or "index"
	cols.push(Column{MC_ID, TYPE_U32});

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

	catalog.insert(MASTER_CATALOG, structure);
}

// In catalog.cpp or a new bootstrap file

// Result callback for catalog bootstrap
void
catalog_bootstrap_callback(TypedValue *result, size_t count)
{
	if (count != 5)
		return;

	const uint32_t key = result[0].as_u32();
	const char	  *name = result[1].as_char();
	const char	  *tbl_name = result[2].as_char();
	uint32_t	   rootpage = result[3].as_u32();
	const char	  *sql = result[4].as_char();

	if (strcmp(name, MASTER_CATALOG) == 0)
		return;

	auto master = catalog.get(MASTER_CATALOG);
	if (master->next_key <= key)
	{
		master->next_key = key + 1;
	}

	Parser parser;
	parser_init(&parser, sql);
	Statement *stmt = parser_parse_statement(&parser);

	array<Column> columns;

	if (strcmp(tbl_name, name) == 0)
	{
		// It's a table
		CreateTableStmt &create_stmt = stmt->create_table_stmt;
		columns.reserve(create_stmt.columns.size());

		for (uint32_t i = 0; i < create_stmt.columns.size(); i++)
		{
			ColumnDef &col_def = create_stmt.columns[i];
			Column	   col = {col_def.name.c_str(), col_def.type};
			columns.push(col);
		}
	}

	// Create the structure
	Structure structure = Structure::from(name, columns);

	// Set up the btree with existing root page
	structure.storage.btree = btree_create(structure.to_layout().key_type(), structure.to_layout().record_size, false);
	structure.storage.btree.root_page_index = rootpage;

	catalog.insert(name, structure);
}

void
load_catalog_from_master()
{
	// Set the callback
	vm_set_result_callback(catalog_bootstrap_callback);

	// Run your existing master table scan
	ProgramBuilder prog = {};
	auto		   cctx = from_structure(*catalog.get(MASTER_CATALOG));
	int			   cursor = prog.open_cursor(cctx);
	int			   is_at_end = prog.rewind(cursor, false);
	auto		   while_context = prog.begin_while(is_at_end);
	int			   dest_reg = prog.get_columns(cursor, 0, cctx->layout.count());
	prog.result(dest_reg, cctx->layout.count());
	prog.next(cursor, is_at_end);
	prog.end_while(while_context);
	prog.close_cursor(cursor);
	prog.halt();
	prog.resolve_labels();

	vm_execute(prog.instructions.front(), prog.instructions.size());
}

void
reload_catalog()
{
	arena<catalog_arena>::init();
	catalog.clear();
	bootstrap_master(false);
	load_catalog_from_master();
}
