
// ============================================================================
// catalog.cpp - Implementation
// ============================================================================

#include "catalog.hpp"
#include "arena.hpp"
#include "common.hpp"
#include "containers.hpp"
#include "pager.hpp"
#include "parser.hpp"
#include "types.hpp"
#include <cassert>
#include <cstring>
#include <string_view>
#include "compile.hpp"

// ============================================================================
// Global Catalog Instance
// ============================================================================

hash_map<string_view, Relation, catalog_arena> catalog;

// ============================================================================
// Catalog Management
// ============================================================================


// ============================================================================
// Tuple Format Construction
// ============================================================================

/**
 * Build a TupleFormat from column types
 *
 * Creates a format descriptor for tuples with the given column types.
 * The first column is treated as the key and stored separately in the
 * btree, so offsets begin from the second column.
 *
 * @param columns Array of column types, first element is the key type
 * @return TupleFormat describing the physical tuple layout
 */
TupleFormat tuple_format_from_types(array<DataType, query_arena>& columns) {
    TupleFormat format;

    // First column is always the key
    format.key_type = columns[0];
    format.columns.copy_from(columns);

    // Calculate offsets for record portion (excluding key)
    // The key is stored separately in the btree node
    int offset = 0;
    format.offsets.push(0);  // First record column starts at offset 0

    // Start from 1 because column 0 is the key
    for (int i = 1; i < columns.size(); i++) {
        offset += type_size(columns[i]);
        format.offsets.push(offset);
    }

    format.record_size = offset;
    return format;
}

/**
 * Extract TupleFormat from a Relation's schema
 *
 * Converts the logical schema (Attributes) into a physical layout
 * descriptor (TupleFormat) for tuple processing.
 */
TupleFormat tuple_format_from_relation(Relation& schema) {
    array<DataType, query_arena> column_types;

    for (auto col : schema.columns) {
        column_types.push(col.type);
    }

    return tuple_format_from_types(column_types);
}

// ============================================================================
// Relation Construction
// ============================================================================

/**
 * Create a new Relation with the given schema
 *
 * Performs cross-arena copying: the input columns are typically in
 * query_arena (temporary), but the Relation stores them in catalog_arena
 * (persistent) to ensure they survive beyond the current query.
 */
Relation create_relation(string_view name, array<Attribute, query_arena> columns) {
    Relation rel;

    // Cross-arena copy from query to catalog arena
    rel.columns.copy_from(columns);

    to_str(name, rel.name, RELATION_NAME_MAX_SIZE);
    return rel;
}

// ============================================================================
// Master Catalog Bootstrap
// ============================================================================

/**
 * Bootstrap the master catalog table
 *
 * The master catalog is the meta-table that stores information about
 * all other tables. It must be at btree root page 1 for consistency.
 *
 * Two modes:
 * 1. is_new_database=true:  Create new master catalog at page 1
 * 2. is_new_database=false: Load existing master catalog from page 1
 *
 * The master catalog schema is compatible with SQLite for familiarity.
 */
void bootstrap_master(bool is_new_database) {
    // Define the master catalog schema
    array<Attribute, query_arena> master_columns = {
        {MC_ID, TYPE_U32},          // Auto-increment ID
        {MC_NAME, TYPE_CHAR32},     // Object name
        {MC_TBL_NAME, TYPE_CHAR32}, // Parent table name
        {MC_ROOTPAGE, TYPE_U32},    // Root page in btree
        {MC_SQL, TYPE_CHAR256}      // CREATE statement
    };

    Relation master_table = create_relation(MASTER_CATALOG, master_columns);
    TupleFormat layout = tuple_format_from_relation(master_table);

    if (is_new_database) {
        // Create new master catalog
        pager_begin_transaction();
        master_table.storage.btree = btree_create(layout.key_type, layout.record_size, is_new_database);

        // Master catalog MUST be at page 1
        assert(1 == master_table.storage.btree.root_page_index);

        pager_commit();
    } else {
        // Load existing master catalog from page 1
        master_table.storage.btree = btree_create(layout.key_type, layout.record_size, is_new_database);
        master_table.storage.btree.root_page_index = 1;
    }

    catalog.insert(MASTER_CATALOG, master_table);
}

// ============================================================================
// Catalog Lifecycle
// ============================================================================

/**
 * Reload the entire catalog from disk
 *
 * This is called:
 * 1. On database open to load the schema
 * 2. After a rollback to reset to the committed state
 *
 * Process:
 * 1. Clear and reset the catalog arena
 * 2. Bootstrap the master catalog from page 1
 * 3. Scan the master catalog to load all other relations
 */
void catalog_reload() {
    // Ensure catalog arena is initialized
    arena<catalog_arena>::init();

    // Clear all catalog memory and return to initial state
    catalog.reset();
    arena<catalog_arena>::reset_and_decommit();

    // Load master catalog from page 1
    bootstrap_master(false);

    // Load all other relations from master
    // (Implementation in separate file)
    load_catalog_from_master();
}
