/*
** parser.hpp - SQL Parser Interface
**
** OVERVIEW
**
** This parser implements a subset of SQL suitable for educational purposes.
** It produces an Abstract Syntax Tree (AST) that can be analyzed and compiled
** into VM bytecode.
**
** SUPPORTED SQL GRAMMAR
**
** Data Definition Language (DDL):
**   CREATE TABLE table_name (column_name INT|TEXT, ...)
**   DROP TABLE table_name
**
** Data Manipulation Language (DML):
**   SELECT * FROM table_name [WHERE expr] [ORDER BY column [ASC|DESC]]
**   SELECT col1, col2, ... FROM table_name [WHERE expr] [ORDER BY column [ASC|DESC]]
**   INSERT INTO table_name VALUES (val1, val2, ...)
**   INSERT INTO table_name (col1, col2, ...) VALUES (val1, val2, ...)
**   UPDATE table_name SET col1 = val1, col2 = val2, ... [WHERE expr]
**   DELETE FROM table_name [WHERE expr]
**
** Transaction Control:
**   BEGIN
**   COMMIT
**   ROLLBACK
**
** Expression Grammar:
**   Literals: 42, 'string'
**   Columns: column_name
**   Comparisons: =, !=, <>, <, <=, >, >=
**   Logical: AND, OR, NOT
**   Grouping: (expression)
**
** LIMITATIONS
**
** - Single table operations only (no JOINs)
** - INSERT supports single row only
** - Data types limited to INT (stored as u32) and TEXT (max 32 bytes)
** - No aggregates (COUNT, SUM, etc.)
** - No GROUP BY or HAVING
** - No subqueries
** - No NULL values in expressions (though NULL type exists internally)
** - String literals must be under 32 bytes
**
** AST STRUCTURE
**
** The parser produces a two-level AST:
**   1. Statement level - represents complete SQL statements
**   2. Expression level - represents WHERE clauses and values
**
** Each AST node contains:
**   - Parsed data from the SQL text
**   - A 'sem' struct for semantic analysis results (filled by semantic.cpp)
**
** The AST uses arena allocation, so individual nodes don't need cleanup.
*/

#pragma once
#include "common.hpp"
#include "containers.hpp"
#include "types.hpp"

struct relation;
//=============================================================================
// EXPRESSION AST NODES
//=============================================================================

enum EXPR_TYPE : uint8_t
{
	EXPR_LITERAL = 0, // Number or string literal
	EXPR_COLUMN,	  // Column reference
	EXPR_BINARY_OP,	  // Binary operation (comparison or logical)
	EXPR_UNARY_OP,	  // Unary operation (NOT, -)
	EXPR_NULL		  // NULL literal
};

enum BINARY_OP : uint8_t
{

	OP_EQ = 0,
	OP_NE,
	OP_LT,
	OP_LE,
	OP_GT,
	OP_GE,


	OP_AND,
	OP_OR
};

enum UnaryOp : uint8_t
{
	OP_NOT = 0,
	OP_NEG
};

struct expr_node
{
	EXPR_TYPE type;

	struct
	{
		data_type resolved_type = TYPE_NULL;
		int32_t	   column_index = -1;
		relation *table = nullptr;

	} sem;

	union {
		// EXPR_LITERAL
		struct
		{
			data_type lit_type;
			union {
				uint32_t	int_val;
				string_view str_val;
			};
		};


		struct
		{
			string_view column_name;
		};


		struct
		{
			BINARY_OP op;
			expr_node	*left;
			expr_node	*right;
		};


		struct
		{
			UnaryOp unary_op;
			expr_node   *operand;
		};


	};
};


enum StmtType : uint8_t
{
	STMT_SELECT = 0,
	STMT_INSERT,
	STMT_UPDATE,
	STMT_DELETE,
	STMT_CREATE_TABLE,
	STMT_DROP_TABLE,
	STMT_BEGIN,
	STMT_COMMIT,
	STMT_ROLLBACK
};


struct attribute_node
{
	string_view name;
	data_type	type;

	struct
	{
		bool is_primary_key = false; // First column is implicitly PK
	} sem;
};


struct select_stmt_node
{
	bool							 is_star;		  // SELECT *
	array<string_view, query_arena> columns;		  // Column names (if not *)
	string_view						 table_name;	  // FROM table
	expr_node						*where_clause;	  // Optional WHERE
	string_view						 order_by_column; // Optional ORDER BY column
	bool							 order_desc;	  // DESC if true, ASC if false


	struct
	{
		relation					 *table = nullptr;
		array<int32_t, query_arena>  column_indices;	   // Indices of selected columns
		array<data_type, query_arena> column_types;		   // Types of selected columns
		int32_t						  order_by_index = -1; // Index of ORDER BY column
	} sem;
};


struct insert_stmt_node
{
	string_view						 table_name;
	array<string_view, query_arena> columns;
	array<expr_node *, query_arena>		 values;


	struct
	{
		relation					*table = nullptr;
		array<int32_t, query_arena> column_indices;

	} sem;
};


struct update_stmt_node
{
	string_view						 table_name;
	array<string_view, query_arena> columns;
	array<expr_node *, query_arena>		 values;
	expr_node							*where_clause;


	struct
	{
		relation					*table = nullptr;
		array<int32_t, query_arena> column_indices;

	} sem;
};


struct delete_stmt_node
{
	string_view table_name;
	expr_node	   *where_clause;


	struct
	{
		relation *table = nullptr;

	} sem;
};


struct create_table_stmt_node
{
	string_view					   table_name;
	array<attribute_node, query_arena> columns;


	struct
	{
		string_view created_structure;

	} sem;
};


struct drop_table_stmt_node
{
	string_view table_name;


	struct
	{
		relation *table = nullptr;

	} sem;
};


struct begin_stmt_node
{
};
struct commit_stmt_node
{
};
struct rollback_stmt_node
{
};


struct stmt_node
{
	StmtType type;


	struct
	{
		bool has_errors = false;
	} sem;

	union {
		select_stmt_node		select_stmt;
		insert_stmt_node		insert_stmt;
		update_stmt_node		update_stmt;
		delete_stmt_node		delete_stmt;
		create_table_stmt_node create_table_stmt;
		drop_table_stmt_node	drop_table_stmt;
		begin_stmt_node		begin_stmt;
		commit_stmt_node		commit_stmt;
		rollback_stmt_node	rollback_stmt;
	};
};



struct parser_result
{
	bool							 success;
	string_view						 error;					 // Error message (nullptr if success)
	array<stmt_node *, query_arena> statements;			 // Array by value, not pointer
	int								 error_line;			 // -1 if no error
	int								 error_column;			 // -1 if no error
	int								 failed_statement_index; // Which statement failed (-1 if none)
};


parser_result
parse_sql(const char *sql);


void
print_ast(stmt_node *stmt);
