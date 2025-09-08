#pragma once
#include "arena.hpp"
#include "containers.hpp"
#include "types.hpp"

using std::string_view;

// Forward declarations
struct Relation;

//=============================================================================
// TOKEN TYPES
//=============================================================================

enum TokenType : uint8_t
{
	TOKEN_EOF = 0,
	TOKEN_IDENTIFIER,
	TOKEN_NUMBER,
	TOKEN_STRING,
	TOKEN_KEYWORD,
	TOKEN_OPERATOR,
	TOKEN_LPAREN,
	TOKEN_RPAREN,
	TOKEN_COMMA,
	TOKEN_SEMICOLON,
	TOKEN_STAR
};

struct Token
{
	TokenType	type;
	const char *text; // Points into original input
	uint32_t	length;
	uint32_t	line;
	uint32_t	column;
};

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
	// Comparison operators
	OP_EQ = 0, // =
	OP_NE,	   // != or <>
	OP_LT,	   //
	OP_LE,	   // <=
	OP_GT,	   // >
	OP_GE,	   // >=

	// Logical operators
	OP_AND, // AND
	OP_OR	// OR
};

enum UnaryOp : uint8_t
{
	OP_NOT = 0, // NOT
	OP_NEG		// - (unary minus)
};

struct Expr
{
	ExprType type;

	// Semantic resolution info (populated during semantic pass)
	struct
	{
		DataType   resolved_type = TYPE_NULL;
		int32_t	   column_index = -1; // For EXPR_COLUMN
		Relation *table = nullptr;	  // For EXPR_COLUMN
		bool	   is_resolved = false;
	} sem;

	union {
		// EXPR_LITERAL
		struct
		{
			DataType lit_type; // TYPE_U32 or TYPE_CHAR32 only
			union {
				uint32_t	int_val; // For INT (TYPE_U32)
				string_view str_val; // For TEXT (TYPE_CHAR32)
			};
		};

		// EXPR_COLUMN
		struct
		{
			string_view column_name;
		};

		// EXPR_BINARY_OP
		struct
		{
			BinaryOp op;
			Expr	*left; // Still need pointers for recursive structures
			Expr	*right;
		};

		// EXPR_UNARY_OP
		struct
		{
			UnaryOp unary_op;
			Expr   *operand;
		};

		// EXPR_NULL has no data
	};
};

//=============================================================================
// STATEMENT AST NODES
//=============================================================================

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

// Column definition for CREATE TABLE
struct ColumnDef
{
	string_view name;
	DataType	type; // TYPE_U32 for INT, TYPE_CHAR32 for TEXT (only options)

	// Semantic info
	struct
	{
		bool is_primary_key = false; // First column is implicitly PK
	} sem;
};

// SELECT statement - simplified
struct SelectStmt
{
	bool							 is_star;		  // SELECT *
	array<string_view, query_arena> columns;		  // Column names (if not *)
	string_view						 table_name;	  // FROM table
	Expr							*where_clause;	  // Optional WHERE
	string_view						 order_by_column; // Optional ORDER BY column
	bool							 order_desc;	  // DESC if true, ASC if false

	// Semantic resolution
	struct
	{
		Relation					 *table = nullptr;
		array<int32_t, query_arena>  column_indices;	   // Indices of selected columns
		array<DataType, query_arena> column_types;		   // Types of selected columns
		int32_t						  order_by_index = -1; // Index of ORDER BY column
		bool						  is_resolved = false;
	} sem;
};

// INSERT statement - single row only
struct InsertStmt
{
	string_view						 table_name;
	array<string_view, query_arena> columns; // Optional column list
	array<Expr *, query_arena>		 values;  // Value expressions

	// Semantic resolution
	struct
	{
		Relation					*table = nullptr;
		array<int32_t, query_arena> column_indices; // Target column indices
		bool						 is_resolved = false;
	} sem;
};

// UPDATE statement
struct UpdateStmt
{
	string_view						 table_name;
	array<string_view, query_arena> columns;	   // SET columns
	array<Expr *, query_arena>		 values;	   // SET values
	Expr							*where_clause; // Optional WHERE

	// Semantic resolution
	struct
	{
		Relation					*table = nullptr;
		array<int32_t, query_arena> column_indices;
		bool						 is_resolved = false;
	} sem;
};

// DELETE statement
struct DeleteStmt
{
	string_view table_name;
	Expr	   *where_clause; // Optional WHERE

	// Semantic resolution
	struct
	{
		Relation *table = nullptr;
		bool	   is_resolved = false;
	} sem;
};

// CREATE TABLE statement
struct CreateTableStmt
{
	string_view					   table_name;
	array<ColumnDef, query_arena> columns; // Direct storage, not pointers

	// Semantic resolution
	struct
	{
		Relation *created_structure = nullptr; // Built structure
		bool	   is_resolved = false;
	} sem;
};

// DROP TABLE statement
struct DropTableStmt
{
	string_view table_name;

	// Semantic resolution
	struct
	{
		Relation *table = nullptr;
		bool	   is_resolved = false;
	} sem;
};

// Transaction statements (empty structs)
struct BeginStmt
{
};
struct CommitStmt
{
};
struct RollbackStmt
{
};

// Main statement - still needs to be allocated because of union
struct Statement
{
	StmtType type;

	// Semantic resolution
	struct
	{
		bool is_resolved = false;
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

//=============================================================================
// PARSER RESULT STRUCTURE
//=============================================================================

struct ParseResult
{
	bool							 success;
	string_view						 error;					 // Error message (nullptr if success)
	array<Statement *, query_arena> statements;			 // Array by value, not pointer
	int								 error_line;			 // -1 if no error
	int								 error_column;			 // -1 if no error
	int								 failed_statement_index; // Which statement failed (-1 if none)
};

//=============================================================================
// LEXER STATE
//=============================================================================

struct Lexer
{
	const char *input;
	const char *current;
	uint32_t	line;
	uint32_t	column;
	Token		current_token;
};

//=============================================================================
// PARSER STATE
//=============================================================================

struct Parser
{
	Lexer		lexer; // Embed directly, not a pointer
	string_view error_msg;
	int			error_line;
	int			error_column;
};

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

// Main entry point - returns result by value
ParseResult
parse_sql(const char *sql);

// Debug/utility
void
print_ast(Statement *stmt);
const char *
token_type_to_string(TokenType type);
const char *
stmt_type_to_string(StmtType type);
