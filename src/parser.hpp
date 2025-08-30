#pragma once
#include "arena.hpp"
#include "defs.hpp"
#include "btree.hpp"
#include <cstdint>
#include <cstring>

// Parser arena tag
struct ParserArena
{
};

// Token types
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
	TOKEN_DOT,
	TOKEN_STAR
};

// Token structure
struct Token
{
	TokenType	type;
	const char *text; // Points into original input
	uint32_t	length;
	uint32_t	line;
	uint32_t	column;
};

// Lexer state
struct Lexer
{
	const char *input;
	const char *current;
	uint32_t	line;
	uint32_t	column;
	Token		current_token;
};

// Forward declarations for AST nodes
struct Expr;
struct SelectStmt;
struct InsertStmt;
struct UpdateStmt;
struct DeleteStmt;
struct CreateTableStmt;
struct DropTableStmt;
struct BeginStmt;
struct CommitStmt;
struct RollbackStmt;

// Expression types
enum ExprType : uint8_t
{
	EXPR_LITERAL = 0,
	EXPR_COLUMN,
	EXPR_BINARY_OP,
	EXPR_UNARY_OP,
	EXPR_FUNCTION,
	EXPR_STAR,
	EXPR_LIST,
	EXPR_SUBQUERY,
	EXPR_NULL
};

// Binary operators
enum BinaryOp : uint8_t
{
	OP_ADD = 0,
	OP_SUB,
	OP_MUL,
	OP_DIV,
	OP_MOD,
	OP_EQ,
	OP_NE,
	OP_LT,
	OP_LE,
	OP_GT,
	OP_GE,
	OP_AND,
	OP_OR,
	OP_LIKE,
	OP_IN
};

// Unary operators
enum UnaryOp : uint8_t
{
	OP_NOT = 0,
	OP_NEG
};

// Join types
enum JoinType : uint8_t
{
	JOIN_INNER = 0,
	JOIN_LEFT,
	JOIN_RIGHT,
	JOIN_CROSS
};

// Order direction
enum OrderDir : uint8_t
{
	ORDER_ASC = 0,
	ORDER_DESC
};

// Statement types
enum StmtType : uint8_t
{
	STMT_SELECT = 0,
	STMT_INSERT,
	STMT_UPDATE,
	STMT_DELETE,
	STMT_CREATE_TABLE,
	STMT_CREATE_INDEX,
	STMT_DROP_TABLE,
	STMT_DROP_INDEX,
	STMT_BEGIN,
	STMT_COMMIT,
	STMT_ROLLBACK
};

// Column definition for CREATE TABLE
struct ColumnDef
{
	const char *name;
	DataType	type;
	bool		is_primary_key;
	bool		is_not_null;
};

// Expression node
struct Expr
{
	ExprType type;

	union {
		// EXPR_LITERAL
		struct
		{
			DataType lit_type;
			union {
				int64_t		int_val;
				double		float_val;
				const char *str_val;
			};
		};
		struct
		{
			SelectStmt *subquery;
		};

		// EXPR_COLUMN
		struct
		{
			const char *table_name; // Can be null
			const char *column_name;
		};

		// EXPR_BINARY_OP
		struct
		{
			BinaryOp op;
			Expr	*left;
			Expr	*right;
		};

		// EXPR_UNARY_OP
		struct
		{
			UnaryOp unary_op;
			Expr   *operand;
		};

		// EXPR_FUNCTION
		struct
		{
			const char				   *func_name;
			array<Expr *, ParserArena> *args;
		};
		struct
		{
			array<Expr *, ParserArena> *list_items;
		};
	};
};

// Table reference with optional alias
struct TableRef
{
	const char *table_name;
	const char *alias;
};

// Join clause
struct JoinClause
{
	JoinType  type;
	TableRef *table;
	Expr	 *condition;
};

// Order by clause
struct OrderByClause
{
	Expr	*expr;
	OrderDir dir;
};

// SELECT statement
struct SelectStmt
{
	array<Expr *, ParserArena>			*select_list;
	TableRef							*from_table;
	array<JoinClause *, ParserArena>	*joins;
	Expr								*where_clause;
	array<Expr *, ParserArena>			*group_by;
	Expr								*having_clause;
	array<OrderByClause *, ParserArena> *order_by;
	int64_t								 limit;
	int64_t								 offset;
	bool								 is_distinct;
};

// INSERT statement
struct InsertStmt
{
	const char										 *table_name;
	array<const char *, ParserArena>				 *columns;
	array<array<Expr *, ParserArena> *, ParserArena> *values;
};

// UPDATE statement
struct UpdateStmt
{
	const char						 *table_name;
	array<const char *, ParserArena> *columns;
	array<Expr *, ParserArena>		 *values;
	Expr							 *where_clause;
};

// DELETE statement
struct DeleteStmt
{
	const char *table_name;
	Expr	   *where_clause;
};

// CREATE TABLE statement
struct CreateTableStmt
{
	const char						*table_name;
	array<ColumnDef *, ParserArena> *columns;
	bool							 if_not_exists;
};

struct CreateIndexStmt
{
	const char						 *index_name;
	const char						 *table_name;
	array<const char *, ParserArena> *columns;
	bool							  is_unique;
	bool							  if_not_exists;
};

// DROP TABLE statement
struct DropTableStmt
{
	const char *table_name;
	bool		if_exists;
};
struct DropIndexStmt
{
	const char *index_name;
	const char *table_name; // Optional - some SQL dialects support ON table_name
	bool		if_exists;
};

// Transaction statements
struct BeginStmt
{
};
struct CommitStmt
{
};
struct RollbackStmt
{
};

// Generic statement
struct Statement
{
	StmtType type;
	union {
		SelectStmt		*select_stmt;
		InsertStmt		*insert_stmt;
		UpdateStmt		*update_stmt;
		DeleteStmt		*delete_stmt;
		CreateTableStmt *create_table_stmt;
		CreateIndexStmt *create_index_stmt;
		DropTableStmt	*drop_table_stmt;
		DropIndexStmt	*drop_index_stmt;
		BeginStmt		*begin_stmt;
		CommitStmt		*commit_stmt;
		RollbackStmt	*rollback_stmt;
	};
};

// Parser state
struct Parser
{
	Lexer						  *lexer;
	string_map<bool, ParserArena> *keywords; // Keyword lookup
};

// Lexer functions
void
lexer_init(Lexer *lex, const char *input);
Token
lexer_next_token(Lexer *lex);
Token
lexer_peek_token(Lexer *lex);
bool
lexer_is_keyword(const char *text, uint32_t length);

// Parser functions
void
parser_init(Parser *parser, const char *input);
Statement *
parser_parse_statement(Parser *parser);
void
parser_reset(Parser *parser);

// Parse specific statements
SelectStmt *
parse_select(Parser *parser);
InsertStmt *
parse_insert(Parser *parser);
UpdateStmt *
parse_update(Parser *parser);
DeleteStmt *
parse_delete(Parser *parser);
CreateTableStmt *
parse_create_table(Parser *parser);
CreateIndexStmt *
parse_create_index(Parser *parser);
DropTableStmt *
parse_drop_table(Parser *parser);
BeginStmt *
parse_begin(Parser *parser);
CommitStmt *
parse_commit(Parser *parser);
RollbackStmt *
parse_rollback(Parser *parser);

// Expression parsing
Expr *
parse_expression(Parser *parser);
Expr *
parse_or_expr(Parser *parser);
Expr *
parse_and_expr(Parser *parser);
Expr *
parse_comparison_expr(Parser *parser);
Expr *
parse_additive_expr(Parser *parser);
Expr *
parse_multiplicative_expr(Parser *parser);
Expr *
parse_unary_expr(Parser *parser);
Expr *
parse_primary_expr(Parser *parser);

// Helper functions
bool
consume_token(Parser *parser, TokenType type);
bool
consume_keyword(Parser *parser, const char *keyword);
bool
peek_keyword(Parser *parser, const char *keyword);
const char *
token_type_to_string(TokenType type);
const char *
intern_string(const char *str, uint32_t length);
DataType
parse_data_type(Parser *parser);
array<Statement *, ParserArena> *
parser_parse_statements(Parser *parser);
array<Statement *, ParserArena> *
parse_sql(const char *sql);
