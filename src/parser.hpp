#pragma once
#include "arena.hpp"
#include "types.hpp"
#include <cstdint>
#include <cstring>

// Forward declaration for semantic resolution
struct Structure;

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
	const char *text; // Points into original input - keep as const char* since it points to lexer input
	uint32_t	length;
	uint32_t	line;
	uint32_t	column;
};

// Lexer state
struct Lexer
{
	const char *input;   // Keep as const char* - points to original input string
	const char *current; // Keep as const char* - points into input
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
	string<ParserArena> name;
	DataType			type;
	bool				is_primary_key;
	bool				is_not_null;
	struct
	{
		bool is_blob_ref = false;
	} sem;
};

// Expression node
struct Expr
{
	ExprType type;

	// Semantic resolution fields
	struct
	{
		DataType   resolved_type = TYPE_NULL;
		int32_t	   column_index = -1;	 // For EXPR_COLUMN
		Structure *table = nullptr;		 // For EXPR_COLUMN
		bool	   is_aggregate = false; // For aggregate functions
		bool	   is_resolved = false;	 // Has semantic pass run
	} sem;

	union {
		// EXPR_LITERAL
		struct
		{
			DataType lit_type;
			union {
				int64_t				int_val;
				double				float_val;
				string<ParserArena> str_val;
			};
		};
		struct
		{
			SelectStmt *subquery;
		};

		// EXPR_COLUMN
		struct
		{
			string<ParserArena> table_name; // Can be empty
			string<ParserArena> column_name;
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
			string<ParserArena>			  func_name;
			array<Expr *, ParserArena>	  args;
		};
		struct
		{
			array<Expr *, ParserArena> list_items;
		};
	};
};

// Table reference with optional alias
struct TableRef
{
	string<ParserArena> table_name;
	string<ParserArena> alias;

	// Semantic resolution
	struct
	{
		Structure *resolved = nullptr; // Points to catalog/shadow catalog entry
	} sem;
};

// Join clause
struct JoinClause
{
	JoinType  type;
	TableRef *table;
	Expr	 *condition;

	// Semantic resolution
	struct
	{
		bool is_resolved = false;
	} sem;
};

// Order by clause
struct OrderByClause
{
	Expr	*expr;
	OrderDir dir;

	// Semantic resolution
	struct
	{
		bool is_resolved = false;
	} sem;
};

// SELECT statement
struct SelectStmt
{
	array<Expr *, ParserArena>			select_list;
	TableRef							*from_table;
	array<JoinClause *, ParserArena>	joins;
	Expr								*where_clause;
	array<Expr *, ParserArena>			group_by;
	Expr								*having_clause;
	array<OrderByClause *, ParserArena> order_by;
	int64_t								limit;
	int64_t								offset;
	bool								is_distinct;

	// Semantic resolution
	struct
	{
		array<DataType, ParserArena>		 output_types;
		array<string<ParserArena>, ParserArena> output_names;
		bool								 has_aggregates = false;
		bool								 is_resolved = false;
	} sem;
};

// INSERT statement
struct InsertStmt
{
	string<ParserArena>									 table_name;
	array<string<ParserArena>, ParserArena>				 columns;
	array<array<Expr *, ParserArena> *, ParserArena>	 values;

	// Semantic resolution
	struct
	{
		Structure				   *table = nullptr;
		array<int32_t, ParserArena> column_indices;
		bool						is_resolved = false;
	} sem;
};

// UPDATE statement
struct UpdateStmt
{
	string<ParserArena>						 table_name;
	array<string<ParserArena>, ParserArena>	 columns;
	array<Expr *, ParserArena>				 values;
	Expr									 *where_clause;

	// Semantic resolution
	struct
	{
		Structure				   *table = nullptr;
		array<int32_t, ParserArena> column_indices;
		bool						is_resolved = false;
	} sem;
};

// DELETE statement
struct DeleteStmt
{
	string<ParserArena> table_name;
	Expr			   *where_clause;

	// Semantic resolution
	struct
	{
		Structure *table = nullptr;
		bool	   is_resolved = false;
	} sem;
};

// CREATE TABLE statement
struct CreateTableStmt
{
	string<ParserArena>				table_name;
	array<ColumnDef *, ParserArena> columns;
	bool							if_not_exists;

	// Semantic resolution
	struct
	{
		Structure *created_structure = nullptr; // Built structure for shadow catalog
		bool	   is_resolved = false;
	} sem;
};

struct CreateIndexStmt
{
	string<ParserArena>						 index_name;
	string<ParserArena>						 table_name;
	array<string<ParserArena>, ParserArena>	 columns;
	bool									 is_unique;
	bool									 if_not_exists;

	// Semantic resolution
	struct
	{
		Structure				   *table = nullptr;
		array<int32_t, ParserArena> column_indices;
		bool						is_resolved = false;
	} sem;
};

// DROP TABLE statement
struct DropTableStmt
{
	string<ParserArena> table_name;
	bool				if_exists;

	// Semantic resolution
	struct
	{
		Structure *table = nullptr; // Existing table being dropped
		bool	   is_resolved = false;
	} sem;
};

struct DropIndexStmt
{
	string<ParserArena> index_name;
	string<ParserArena> table_name; // Optional - some SQL dialects support ON table_name
	bool				if_exists;

	// Semantic resolution
	struct
	{
		Structure *table = nullptr; // If table_name specified
		bool	   is_resolved = false;
	} sem;
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

	// Semantic resolution
	struct
	{
		bool is_resolved = false;
		bool has_errors = false;
	} sem;

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
	Lexer								  *lexer;
	hash_map<string<ParserArena>, bool, ParserArena> keywords; // Keyword lookup
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

DataType
parse_data_type(Parser *parser);
array<Statement *, ParserArena> *
parser_parse_statements(Parser *parser);
array<Statement *, ParserArena> *
parse_sql(const char *sql);

void
print_ast(Statement *stmt);
