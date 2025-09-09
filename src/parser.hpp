#pragma once
#include "common.hpp"
#include "containers.hpp"
#include "types.hpp"



/*
BEGIN
COMMIT
ROLLBACK
-- CREATE
CREATE TABLE table_name (column_name INT|TEXT, ...)
-- DROP
DROP TABLE table_name
-- INSERT (single row only)
INSERT INTO table_name VALUES (value, ...)
INSERT INTO table_name (column, ...) VALUES (value, ...)
-- DELETE
DELETE FROM table_name
DELETE FROM table_name WHERE expression
-- UPDATE (all matching rows)
UPDATE table_name SET column = value, ...
UPDATE table_name SET column = value, ... WHERE expression
-- SELECT
SELECT * FROM table_name
SELECT * FROM table_name WHERE expression
SELECT * FROM table_name ORDER BY column
SELECT * FROM table_name WHERE expression ORDER BY column
SELECT column, ... FROM table_name
SELECT column, ... FROM table_name WHERE expression
SELECT column, ... FROM table_name ORDER BY column
SELECT column, ... FROM table_name WHERE expression ORDER BY column
 */



struct Relation;


//=============================================================================
// EXPRESSION AST NODES
//=============================================================================

enum ExprType : uint8_t
{
	EXPR_LITERAL = 0, // Number or string literal
	EXPR_COLUMN,	  // Column reference
	EXPR_BINARY_OP,	  // Binary operation (comparison or logical)
	EXPR_UNARY_OP,	  // Unary operation (NOT, -)
	EXPR_NULL		  // NULL literal
};

enum BinaryOp : uint8_t
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

struct Expr
{
	ExprType type;

	struct
	{
		DataType   resolved_type = TYPE_NULL;
		int32_t	   column_index = -1;
		Relation *table = nullptr;

	} sem;

	union {
		// EXPR_LITERAL
		struct
		{
			DataType lit_type;
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
			BinaryOp op;
			Expr	*left;
			Expr	*right;
		};


		struct
		{
			UnaryOp unary_op;
			Expr   *operand;
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


struct ColumnDef
{
	string_view name;
	DataType	type;

	struct
	{
		bool is_primary_key = false; // First column is implicitly PK
	} sem;
};


struct SelectStmt
{
	bool							 is_star;		  // SELECT *
	array<string_view, query_arena> columns;		  // Column names (if not *)
	string_view						 table_name;	  // FROM table
	Expr							*where_clause;	  // Optional WHERE
	string_view						 order_by_column; // Optional ORDER BY column
	bool							 order_desc;	  // DESC if true, ASC if false


	struct
	{
		Relation					 *table = nullptr;
		array<int32_t, query_arena>  column_indices;	   // Indices of selected columns
		array<DataType, query_arena> column_types;		   // Types of selected columns
		int32_t						  order_by_index = -1; // Index of ORDER BY column
	} sem;
};


struct InsertStmt
{
	string_view						 table_name;
	array<string_view, query_arena> columns;
	array<Expr *, query_arena>		 values;


	struct
	{
		Relation					*table = nullptr;
		array<int32_t, query_arena> column_indices;

	} sem;
};


struct UpdateStmt
{
	string_view						 table_name;
	array<string_view, query_arena> columns;
	array<Expr *, query_arena>		 values;
	Expr							*where_clause;


	struct
	{
		Relation					*table = nullptr;
		array<int32_t, query_arena> column_indices;

	} sem;
};


struct DeleteStmt
{
	string_view table_name;
	Expr	   *where_clause;


	struct
	{
		Relation *table = nullptr;

	} sem;
};


struct CreateTableStmt
{
	string_view					   table_name;
	array<ColumnDef, query_arena> columns;


	struct
	{
		string_view created_structure;

	} sem;
};


struct DropTableStmt
{
	string_view table_name;


	struct
	{
		Relation *table = nullptr;

	} sem;
};


struct BeginStmt
{
};
struct CommitStmt
{
};
struct RollbackStmt
{
};


struct Statement
{
	StmtType type;


	struct
	{
		bool has_errors = false;
	} sem;

	union {
		SelectStmt		select_stmt;
		InsertStmt		insert_stmt;
		UpdateStmt		update_stmt;
		DeleteStmt		delete_stmt;
		CreateTableStmt create_table_stmt;
		DropTableStmt	drop_table_stmt;
		BeginStmt		begin_stmt;
		CommitStmt		commit_stmt;
		RollbackStmt	rollback_stmt;
	};
};



struct parser_result
{
	bool							 success;
	string_view						 error;					 // Error message (nullptr if success)
	array<Statement *, query_arena> statements;			 // Array by value, not pointer
	int								 error_line;			 // -1 if no error
	int								 error_column;			 // -1 if no error
	int								 failed_statement_index; // Which statement failed (-1 if none)
};


parser_result
parse_sql(const char *sql);


void
print_ast(Statement *stmt);
