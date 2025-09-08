// ============================================================================
// catalog.hpp - Schema Management and Metadata Storage
// ============================================================================
//
// The catalog is the heart of the database's metadata system. It maintains
// the schema information for all relations (tables) and provides the mapping
// between logical structure and physical storage.
//
// Memory Model:
// - Catalog data lives in catalog_arena which persists across queries
// - On rollback, the entire catalog is reloaded from the master table
// - String names are interned in the catalog arena for deduplication
//
// The master catalog (sqlite_master compatible) is always at btree root page 1
// and bootstraps the loading of all other relations.
//
// ============================================================================

#pragma once
#include "btree.hpp"
#include "common.hpp"
#include "types.hpp"
#include "containers.hpp"
#include <cstdint>



// ============================================================================
// Master Catalog Schema Constants
// ============================================================================

// The master catalog table name and column names follow SQLite conventions
// for compatibility and familiarity
#define MASTER_CATALOG "sqlite_master"
#define MC_ID          "id"        // Auto-increment primary key
#define MC_NAME        "name"      // Object name (table/index)
#define MC_TBL_NAME    "tbl_name"  // Table this object belongs to
#define MC_ROOTPAGE    "rootpage"  // Root page in btree
#define MC_SQL         "sql"       // Original CREATE statement



#define ATTRIBUTE_NAME_MAX_SIZE 32
#define RELATION_NAME_MAX_SIZE 32
// ============================================================================
// Memory Arenas
// ============================================================================

/**
 * catalog_arena - Persistent storage for schema metadata
 *
 * This arena holds all catalog data that must survive across queries.
 * It is only reset when the catalog is reloaded (e.g., after rollback).
 */
struct catalog_arena {};

// ============================================================================
// Core Types
// ============================================================================

/**
 * Attribute - Column definition within a relation
 *
 * Represents a single column's metadata. The name is interned in the
 * catalog arena to ensure it persists and to enable pointer comparison.
 */
struct Attribute {
    char name[ATTRIBUTE_NAME_MAX_SIZE + 1];  // Interned string in catalog arena
    DataType type;     // Column data type
};

/**
 * Relation - Schema definition for a table
 *
 * Holds the complete metadata for a table including its schema and
 * a handle to its btree storage. The relation itself lives in the
 * catalog arena and persists across queries.
 *
 * Note: In relational algebra, "relation" is the formal term for what
 * SQL calls a "table".
 */
struct Relation {
    char name[RELATION_NAME_MAX_SIZE + 1];      // Interned table name
    TypedValue next_key;   // Next auto-increment value (supports various key types)

    // Physical storage handle
    union {
        btree btree;       // B+tree storage backend
        // Future: could add other storage backends
    } storage;

    // Schema definition
    array<Attribute, catalog_arena> columns;
};

/**
 * TupleFormat - Runtime layout descriptor for tuple processing
 *
 * Created per-query to describe the physical layout of tuples being
 * processed. Can be derived from a Relation's schema or created
 * independently for intermediate results (e.g., ORDER BY temp storage).
 *
 * Layout example for (id:u32, email:char32, name:char16, age:u16):
 *   columns: [TYPE_U32, TYPE_CHAR32, TYPE_CHAR16, TYPE_U16]
 *   offsets: [0, 32, 48]  // Offsets for email, name, age (id is the key)
 *   record_size: 50       // Total size of record portion
 *   key_type: TYPE_U32    // Type of the key (id)
 *
 * Note: The key is stored separately in the btree, so offsets start
 * from the first non-key column.
 */
struct TupleFormat {
    array<DataType, query_arena> columns;  // All column types including key
    array<uint32_t, query_arena> offsets;  // Byte offsets (excluding key)
    uint32_t record_size;                  // Size of record (excluding key)
    DataType key_type;                     // Type of the key column
};

// ============================================================================
// Global Catalog
// ============================================================================

/**
 * Global catalog map - Central registry of all relations
 *
 * Maps table names to their Relation metadata. Uses string_view as keys
 * with the assumption that the strings are interned in catalog_arena.
 */
extern hash_map<string_view, Relation, catalog_arena> catalog;

// ============================================================================
// Public API
// ============================================================================

/**
 * Reload the entire catalog from disk
 *
 * Clears the in-memory catalog and rebuilds it from the master catalog
 * stored on disk. Called after database open or rollback.
 */
void catalog_reload();

/**
 * Add a relation to the catalog
 *
 * Interns the relation name and inserts it into the global catalog map.
 */
void catalog_add_relation(Relation& relation);

/**
 * Remove a relation from the catalog
 *
 * Removes the relation and reclaims the interned name string.
 */
void catalog_delete_relation(string_view key);

/**
 * Create a tuple format from raw column types
 *
 * Builds a TupleFormat from an array of data types. The first type
 * is assumed to be the key. Offsets are calculated for the record
 * portion (excluding the key which is stored separately).
 */
TupleFormat tuple_format_from_types(array<DataType, query_arena>& columns);

/**
 * Extract tuple format from a relation's schema
 *
 * Creates a TupleFormat that describes the physical layout of tuples
 * for the given relation.
 */
TupleFormat tuple_format_from_relation(Relation& schema);

/**
 * Create a new relation with the given schema
 *
 * Note: Performs cross-arena copying from query_arena to catalog_arena
 * to ensure the schema persists beyond the current query.
 */
Relation create_relation(string_view name, array<Attribute, query_arena> columns);
