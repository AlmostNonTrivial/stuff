// parser.cpp - Simplified SQL Parser Implementation
#include "parser.hpp"
#include "arena.hpp"
#include "containers.hpp"
#include "common.hpp"
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>

//=============================================================================
// ERROR MESSAGE FORMATTING
//=============================================================================

static string_view
format_error(Parser *parser, const char *fmt, ...)
{
	char   *buffer = (char *)arena<query_arena>::alloc(256);
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, 256, fmt, args);
	va_end(args);

	parser->error_msg = string_view(buffer, strlen(buffer));
	parser->error_line = parser->lexer.line;
	parser->error_column = parser->lexer.column;

	return parser->error_msg;
}

//=============================================================================
// STRING UTILITIES
//=============================================================================

static bool
str_eq_ci(const char *a, uint32_t a_len, const char *b)
{
	size_t b_len = strlen(b);
	if (a_len != b_len)
		return false;

	for (uint32_t i = 0; i < a_len; i++)
	{
		if (toupper(a[i]) != toupper(b[i]))
			return false;
	}
	return true;
}

//=============================================================================
// KEYWORD LIST
//=============================================================================

static const char *sql_keywords[] = {"SELECT",	 "FROM",   "WHERE",	 "INSERT", "INTO", "VALUES", "UPDATE",
									 "SET",		 "DELETE", "CREATE", "TABLE",  "DROP", "BEGIN",	 "COMMIT",
									 "ROLLBACK", "AND",	   "OR",	 "NOT",	   "NULL", "ORDER",	 "BY",
									 "ASC",		 "DESC",   "INT",	 "TEXT",   nullptr};

static bool
is_keyword(const char *text, uint32_t length)
{
	for (int i = 0; sql_keywords[i]; i++)
	{
		if (str_eq_ci(text, length, sql_keywords[i]))
		{
			return true;
		}
	}
	return false;
}

//=============================================================================
// LEXER IMPLEMENTATION
//=============================================================================

void
lexer_init(Lexer *lex, const char *input)
{
	lex->input = input;
	lex->current = input;
	lex->line = 1;
	lex->column = 1;
	lex->current_token = {TOKEN_EOF, nullptr, 0, 0, 0};
}

static void
skip_whitespace(Lexer *lex)
{
	while (*lex->current)
	{
		if (*lex->current == ' ' || *lex->current == '\t' || *lex->current == '\r')
		{
			lex->column++;
			lex->current++;
		}
		else if (*lex->current == '\n')
		{
			lex->line++;
			lex->column = 1;
			lex->current++;
		}
		else if (lex->current[0] == '-' && lex->current[1] == '-')
		{
			// SQL comment
			while (*lex->current && *lex->current != '\n')
			{
				lex->current++;
			}
		}
		else
		{
			break;
		}
	}
}

Token
lexer_next_token(Lexer *lex)
{
	skip_whitespace(lex);

	Token token;
	token.line = lex->line;
	token.column = lex->column;
	token.text = lex->current;

	if (*lex->current == '\0')
	{
		token.type = TOKEN_EOF;
		token.length = 0;
		lex->current_token = token;
		return token;
	}

	// Single character tokens
	char c = *lex->current;
	switch (c)
	{
	case '(':
		token.type = TOKEN_LPAREN;
		token.length = 1;
		break;
	case ')':
		token.type = TOKEN_RPAREN;
		token.length = 1;
		break;
	case ',':
		token.type = TOKEN_COMMA;
		token.length = 1;
		break;
	case ';':
		token.type = TOKEN_SEMICOLON;
		token.length = 1;
		break;
	case '*':
		token.type = TOKEN_STAR;
		token.length = 1;
		break;
	case '=':
		token.type = TOKEN_OPERATOR;
		token.length = 1;
		break;
	case '!':
		if (lex->current[1] == '=')
		{
			token.type = TOKEN_OPERATOR;
			token.length = 2;
		}
		else
		{
			token.type = TOKEN_OPERATOR;
			token.length = 1;
		}
		break;
	case '<':
		if (lex->current[1] == '=' || lex->current[1] == '>')
		{
			token.type = TOKEN_OPERATOR;
			token.length = 2;
		}
		else
		{
			token.type = TOKEN_OPERATOR;
			token.length = 1;
		}
		break;
	case '>':
		if (lex->current[1] == '=')
		{
			token.type = TOKEN_OPERATOR;
			token.length = 2;
		}
		else
		{
			token.type = TOKEN_OPERATOR;
			token.length = 1;
		}
		break;
	default:
		token.length = 0;
		break;
	}

	if (token.length > 0)
	{
		lex->current += token.length;
		lex->column += token.length;
		lex->current_token = token;
		return token;
	}

	// String literals
	if (c == '\'' || c == '"')
	{
		char quote = c;
		lex->current++;
		lex->column++;

		const char *start = lex->current;
		while (*lex->current && *lex->current != quote)
		{
			if (*lex->current == '\\' && lex->current[1])
			{
				lex->current += 2;
				lex->column += 2;
			}
			else
			{
				lex->current++;
				lex->column++;
			}
		}

		token.type = TOKEN_STRING;
		token.text = start;
		token.length = lex->current - start;

		if (*lex->current == quote)
		{
			lex->current++;
			lex->column++;
		}

		lex->current_token = token;
		return token;
	}

	// Numbers
	if (isdigit(c))
	{
		const char *start = lex->current;

		while (isdigit(*lex->current))
		{
			lex->current++;
			lex->column++;
		}

		token.type = TOKEN_NUMBER;
		token.text = start;
		token.length = lex->current - start;
		lex->current_token = token;
		return token;
	}

	// Identifiers and keywords
	if (isalpha(c) || c == '_')
	{
		const char *start = lex->current;

		while (isalnum(*lex->current) || *lex->current == '_')
		{
			lex->current++;
			lex->column++;
		}

		token.text = start;
		token.length = lex->current - start;
		token.type = is_keyword(start, token.length) ? TOKEN_KEYWORD : TOKEN_IDENTIFIER;

		lex->current_token = token;
		return token;
	}

	// Unknown character
	token.type = TOKEN_EOF;
	token.length = 1;
	lex->current++;
	lex->column++;
	lex->current_token = token;
	return token;
}

Token
lexer_peek_token(Lexer *lex)
{
	const char *saved_current = lex->current;
	uint32_t	saved_line = lex->line;
	uint32_t	saved_column = lex->column;
	Token		saved_token = lex->current_token;

	Token token = lexer_next_token(lex);

	lex->current = saved_current;
	lex->line = saved_line;
	lex->column = saved_column;
	lex->current_token = saved_token;

	return token;
}

//=============================================================================
// PARSER HELPERS
//=============================================================================

bool
consume_token(Parser *parser, TokenType type)
{
	Token token = lexer_peek_token(&parser->lexer);
	if (token.type == type)
	{
		lexer_next_token(&parser->lexer);
		return true;
	}
	return false;
}

bool
consume_keyword(Parser *parser, const char *keyword)
{
	Token token = lexer_peek_token(&parser->lexer);
	if (token.type == TOKEN_KEYWORD && str_eq_ci(token.text, token.length, keyword))
	{
		lexer_next_token(&parser->lexer);
		return true;
	}
	return false;
}

bool
peek_keyword(Parser *parser, const char *keyword)
{
	Token token = lexer_peek_token(&parser->lexer);
	return token.type == TOKEN_KEYWORD && str_eq_ci(token.text, token.length, keyword);
}

static bool
consume_operator(Parser *parser, const char *op)
{
	Token token = lexer_peek_token(&parser->lexer);
	if (token.type == TOKEN_OPERATOR && token.length == strlen(op) && memcmp(token.text, op, token.length) == 0)
	{
		lexer_next_token(&parser->lexer);
		return true;
	}
	return false;
}

DataType
parse_data_type(Parser *parser)
{
	if (consume_keyword(parser, "INT"))
	{
		return TYPE_U32;
	}
	if (consume_keyword(parser, "TEXT"))
	{
		return TYPE_CHAR32;
	}

	format_error(parser, "Expected data type (INT or TEXT)");
	return TYPE_NULL;
}

//=============================================================================
// EXPRESSION PARSING
//=============================================================================

// Forward declarations
Expr *
parse_or_expr(Parser *parser);
Expr *
parse_and_expr(Parser *parser);
Expr *
parse_comparison_expr(Parser *parser);
Expr *
parse_unary_expr(Parser *parser);
Expr *
parse_primary_expr(Parser *parser);

Expr *
parse_expression(Parser *parser)
{
	return parse_or_expr(parser);
}

Expr *
parse_or_expr(Parser *parser)
{
	Expr *left = parse_and_expr(parser);
	if (!left)
		return nullptr;

	while (consume_keyword(parser, "OR"))
	{
		Expr *right = parse_and_expr(parser);
		if (!right)
		{
			format_error(parser, "Expected expression after OR");
			return nullptr;
		}

		Expr *expr = (Expr *)arena<query_arena>::alloc(sizeof(Expr));
		expr->type = EXPR_BINARY_OP;
		expr->op = OP_OR;
		expr->left = left;
		expr->right = right;
		left = expr;
	}

	return left;
}

Expr *
parse_and_expr(Parser *parser)
{
	Expr *left = parse_comparison_expr(parser);
	if (!left)
		return nullptr;

	while (consume_keyword(parser, "AND"))
	{
		Expr *right = parse_comparison_expr(parser);
		if (!right)
		{
			format_error(parser, "Expected expression after AND");
			return nullptr;
		}

		Expr *expr = (Expr *)arena<query_arena>::alloc(sizeof(Expr));
		expr->type = EXPR_BINARY_OP;
		expr->op = OP_AND;
		expr->left = left;
		expr->right = right;
		left = expr;
	}

	return left;
}

Expr *
parse_comparison_expr(Parser *parser)
{
	Expr *left = parse_unary_expr(parser);
	if (!left)
		return nullptr;

	Token token = lexer_peek_token(&parser->lexer);
	if (token.type == TOKEN_OPERATOR)
	{
		BinaryOp op;

		if (consume_operator(parser, "="))
		{
			op = OP_EQ;
		}
		else if (consume_operator(parser, "!=") || consume_operator(parser, "<>"))
		{
			op = OP_NE;
		}
		else if (consume_operator(parser, "<="))
		{
			op = OP_LE;
		}
		else if (consume_operator(parser, ">="))
		{
			op = OP_GE;
		}
		else if (consume_operator(parser, "<"))
		{
			op = OP_LT;
		}
		else if (consume_operator(parser, ">"))
		{
			op = OP_GT;
		}
		else
		{
			return left; // Not a comparison operator
		}

		Expr *right = parse_unary_expr(parser);
		if (!right)
		{
			format_error(parser, "Expected expression after comparison operator");
			return nullptr;
		}

		Expr *expr = (Expr *)arena<query_arena>::alloc(sizeof(Expr));
		expr->type = EXPR_BINARY_OP;
		expr->op = op;
		expr->left = left;
		expr->right = right;
		return expr;
	}

	return left;
}

Expr *
parse_unary_expr(Parser *parser)
{
	if (consume_keyword(parser, "NOT"))
	{
		Expr *operand = parse_unary_expr(parser);
		if (!operand)
		{
			format_error(parser, "Expected expression after NOT");
			return nullptr;
		}

		Expr *expr = (Expr *)arena<query_arena>::alloc(sizeof(Expr));
		expr->type = EXPR_UNARY_OP;
		expr->unary_op = OP_NOT;
		expr->operand = operand;
		return expr;
	}

	return parse_primary_expr(parser);
}

Expr *
parse_primary_expr(Parser *parser)
{
	Token token = lexer_peek_token(&parser->lexer);

	// NULL literal
	if (consume_keyword(parser, "NULL"))
	{
		Expr *expr = (Expr *)arena<query_arena>::alloc(sizeof(Expr));
		expr->type = EXPR_NULL;
		return expr;
	}

	// Number literal
	if (token.type == TOKEN_NUMBER)
	{
		lexer_next_token(&parser->lexer);
		Expr *expr = (Expr *)arena<query_arena>::alloc(sizeof(Expr));

		expr->type = EXPR_LITERAL;
		expr->lit_type = TYPE_U32;

		char *num_str = (char *)arena<query_arena>::alloc(token.length + 1);
		memcpy(num_str, token.text, token.length);
		num_str[token.length] = '\0';

		expr->int_val = (uint32_t)atol(num_str);
		return expr;
	}

	// String literal
	if (token.type == TOKEN_STRING)
	{
		lexer_next_token(&parser->lexer);
		Expr *expr = (Expr *)arena<query_arena>::alloc(sizeof(Expr));
		expr->type = EXPR_LITERAL;
		expr->lit_type = TYPE_CHAR32;
		expr->str_val = string_view(token.text, token.length);
		return expr;
	}

	// Parenthesized expression
	if (consume_token(parser, TOKEN_LPAREN))
	{
		Expr *expr = parse_expression(parser);
		if (!expr)
		{
			format_error(parser, "Expected expression after '('");
			return nullptr;
		}

		if (!consume_token(parser, TOKEN_RPAREN))
		{
			format_error(parser, "Expected ')' after expression");
			return nullptr;
		}

		return expr;
	}

	// Column reference
	if (token.type == TOKEN_IDENTIFIER)
	{
		lexer_next_token(&parser->lexer);
		Expr *expr = (Expr *)arena<query_arena>::alloc(sizeof(Expr));
		expr->type = EXPR_COLUMN;
		expr->column_name = string_view(token.text, token.length);
		return expr;
	}

	format_error(parser, "Expected expression but found '%.*s'", token.length, token.text);
	return nullptr;
}

Expr *
parse_where_clause(Parser *parser)
{
	if (!consume_keyword(parser, "WHERE"))
	{
		return nullptr; // No WHERE clause
	}

	Expr *expr = parse_expression(parser);
	if (!expr)
	{
		format_error(parser, "Expected expression after WHERE");
	}

	return expr;
}

//=============================================================================
// SELECT STATEMENT PARSING
//=============================================================================

void
parse_select(Parser *parser, SelectStmt *stmt)
{
	memset(stmt, 0, sizeof(SelectStmt));

	if (!consume_keyword(parser, "SELECT"))
	{
		format_error(parser, "Expected SELECT");
		return;
	}

	// Check for SELECT *
	if (consume_token(parser, TOKEN_STAR))
	{
		stmt->is_star = true;
	}
	else
	{
		// Parse column list
		stmt->is_star = false;

		do
		{
			Token token = lexer_next_token(&parser->lexer);
			if (token.type != TOKEN_IDENTIFIER)
			{
				format_error(parser, "Expected column name in SELECT list");
				return;
			}

			string_view col_name = string_view(token.text, token.length);
			stmt->columns.push(col_name);
		} while (consume_token(parser, TOKEN_COMMA));
	}

	// FROM clause
	if (!consume_keyword(parser, "FROM"))
	{
		format_error(parser, "Expected FROM after SELECT list");
		return;
	}

	Token token = lexer_next_token(&parser->lexer);
	if (token.type != TOKEN_IDENTIFIER)
	{
		format_error(parser, "Expected table name after FROM");
		return;
	}

	stmt->table_name = string_view(token.text, token.length);

	// Optional WHERE clause
	stmt->where_clause = parse_where_clause(parser);

	// Optional ORDER BY clause
	if (consume_keyword(parser, "ORDER"))
	{
		if (!consume_keyword(parser, "BY"))
		{
			format_error(parser, "Expected BY after ORDER");
			return;
		}

		token = lexer_next_token(&parser->lexer);
		if (token.type != TOKEN_IDENTIFIER)
		{
			format_error(parser, "Expected column name after ORDER BY");
			return;
		}

		stmt->order_by_column = string_view(token.text, token.length);

		// Optional ASC/DESC
		if (consume_keyword(parser, "DESC"))
		{
			stmt->order_desc = true;
		}
		else
		{
			consume_keyword(parser, "ASC"); // Optional, ASC is default
			stmt->order_desc = false;
		}
	}
}

//=============================================================================
// INSERT STATEMENT PARSING
//=============================================================================

void
parse_insert(Parser *parser, InsertStmt *stmt)
{
	memset(stmt, 0, sizeof(InsertStmt));

	if (!consume_keyword(parser, "INSERT"))
	{
		format_error(parser, "Expected INSERT");
		return;
	}

	if (!consume_keyword(parser, "INTO"))
	{
		format_error(parser, "Expected INTO after INSERT");
		return;
	}

	Token token = lexer_next_token(&parser->lexer);
	if (token.type != TOKEN_IDENTIFIER)
	{
		format_error(parser, "Expected table name after INSERT INTO");
		return;
	}

	stmt->table_name = string_view(token.text, token.length);

	// Optional column list
	if (consume_token(parser, TOKEN_LPAREN))
	{
		do
		{
			token = lexer_next_token(&parser->lexer);
			if (token.type != TOKEN_IDENTIFIER)
			{
				format_error(parser, "Expected column name in INSERT column list");
				return;
			}

			string_view col_name = string_view(token.text, token.length);
			stmt->columns.push(col_name);
		} while (consume_token(parser, TOKEN_COMMA));

		if (!consume_token(parser, TOKEN_RPAREN))
		{
			format_error(parser, "Expected ')' after column list");
			return;
		}
	}

	if (!consume_keyword(parser, "VALUES"))
	{
		format_error(parser, "Expected VALUES after table name");
		return;
	}

	if (!consume_token(parser, TOKEN_LPAREN))
	{
		format_error(parser, "Expected '(' after VALUES");
		return;
	}

	// Parse value list
	do
	{
		Expr *expr = parse_expression(parser);
		if (!expr)
		{
			format_error(parser, "Expected value expression in VALUES list");
			return;
		}
		stmt->values.push(expr);
	} while (consume_token(parser, TOKEN_COMMA));

	if (!consume_token(parser, TOKEN_RPAREN))
	{
		format_error(parser, "Expected ')' after VALUES list");
		return;
	}
}

//=============================================================================
// UPDATE STATEMENT PARSING
//=============================================================================

void
parse_update(Parser *parser, UpdateStmt *stmt)
{
	memset(stmt, 0, sizeof(UpdateStmt));

	if (!consume_keyword(parser, "UPDATE"))
	{
		format_error(parser, "Expected UPDATE");
		return;
	}

	Token token = lexer_next_token(&parser->lexer);
	if (token.type != TOKEN_IDENTIFIER)
	{
		format_error(parser, "Expected table name after UPDATE");
		return;
	}

	stmt->table_name = string_view(token.text, token.length);

	if (!consume_keyword(parser, "SET"))
	{
		format_error(parser, "Expected SET after table name");
		return;
	}

	// Parse SET assignments
	do
	{
		token = lexer_next_token(&parser->lexer);
		if (token.type != TOKEN_IDENTIFIER)
		{
			format_error(parser, "Expected column name in SET clause");
			return;
		}

		string_view col_name = string_view(token.text, token.length);
		stmt->columns.push(col_name);

		if (!consume_operator(parser, "="))
		{
			format_error(parser, "Expected '=' after column name");
			return;
		}

		Expr *expr = parse_expression(parser);
		if (!expr)
		{
			format_error(parser, "Expected value expression after '='");
			return;
		}
		stmt->values.push(expr);
	} while (consume_token(parser, TOKEN_COMMA));

	// Optional WHERE clause
	stmt->where_clause = parse_where_clause(parser);
}

//=============================================================================
// DELETE STATEMENT PARSING
//=============================================================================

void
parse_delete(Parser *parser, DeleteStmt *stmt)
{
	memset(stmt, 0, sizeof(DeleteStmt));

	if (!consume_keyword(parser, "DELETE"))
	{
		format_error(parser, "Expected DELETE");
		return;
	}

	if (!consume_keyword(parser, "FROM"))
	{
		format_error(parser, "Expected FROM after DELETE");
		return;
	}

	Token token = lexer_next_token(&parser->lexer);
	if (token.type != TOKEN_IDENTIFIER)
	{
		format_error(parser, "Expected table name after DELETE FROM");
		return;
	}

	stmt->table_name = string_view(token.text, token.length);

	// Optional WHERE clause
	stmt->where_clause = parse_where_clause(parser);
}

//=============================================================================
// CREATE TABLE STATEMENT PARSING
//=============================================================================

void
parse_create_table(Parser *parser, CreateTableStmt *stmt)
{
	memset(stmt, 0, sizeof(CreateTableStmt));

	if (!consume_keyword(parser, "CREATE"))
	{
		format_error(parser, "Expected CREATE");
		return;
	}

	if (!consume_keyword(parser, "TABLE"))
	{
		format_error(parser, "Expected TABLE after CREATE");
		return;
	}

	Token token = lexer_next_token(&parser->lexer);
	if (token.type != TOKEN_IDENTIFIER)
	{
		format_error(parser, "Expected table name after CREATE TABLE");
		return;
	}

	stmt->table_name = string_view(token.text, token.length);

	if (!consume_token(parser, TOKEN_LPAREN))
	{
		format_error(parser, "Expected '(' after table name");
		return;
	}

	// Parse column definitions
	do
	{
		token = lexer_next_token(&parser->lexer);
		if (token.type != TOKEN_IDENTIFIER)
		{
			format_error(parser, "Expected column name in CREATE TABLE");
			return;
		}

		ColumnDef col;
		memset(&col, 0, sizeof(ColumnDef));

		col.name = string_view(token.text, token.length);

		col.type = parse_data_type(parser);
		if (col.type == TYPE_NULL)
		{
			return; // Error already set by parse_data_type
		}

		// First column is implicitly primary key
		if (stmt->columns.size() == 0)
		{
			col.sem.is_primary_key = true;
		}

		stmt->columns.push(col);
	} while (consume_token(parser, TOKEN_COMMA));

	if (!consume_token(parser, TOKEN_RPAREN))
	{
		format_error(parser, "Expected ')' after column definitions");
		return;
	}

	if (stmt->columns.size() == 0)
	{
		format_error(parser, "Table must have at least one column");
		return;
	}
}

//=============================================================================
// DROP TABLE STATEMENT PARSING
//=============================================================================

void
parse_drop_table(Parser *parser, DropTableStmt *stmt)
{
	memset(stmt, 0, sizeof(DropTableStmt));

	if (!consume_keyword(parser, "DROP"))
	{
		format_error(parser, "Expected DROP");
		return;
	}

	if (!consume_keyword(parser, "TABLE"))
	{
		format_error(parser, "Expected TABLE after DROP");
		return;
	}

	Token token = lexer_next_token(&parser->lexer);
	if (token.type != TOKEN_IDENTIFIER)
	{
		format_error(parser, "Expected table name after DROP TABLE");
		return;
	}

	stmt->table_name = string_view(token.text, token.length);
}

//=============================================================================
// TRANSACTION STATEMENT PARSING
//=============================================================================

void
parse_begin(Parser *parser, BeginStmt *stmt)
{
	if (!consume_keyword(parser, "BEGIN"))
	{
		format_error(parser, "Expected BEGIN");
		return;
	}
}

void
parse_commit(Parser *parser, CommitStmt *stmt)
{
	if (!consume_keyword(parser, "COMMIT"))
	{
		format_error(parser, "Expected COMMIT");
		return;
	}
}

void
parse_rollback(Parser *parser, RollbackStmt *stmt)
{
	if (!consume_keyword(parser, "ROLLBACK"))
	{
		format_error(parser, "Expected ROLLBACK");
		return;
	}
}

//=============================================================================
// MAIN STATEMENT PARSING
//=============================================================================

Statement *
parse_statement(Parser *parser)
{
	Statement *stmt = (Statement *)arena<query_arena>::alloc(sizeof(Statement));
	memset(stmt, 0, sizeof(Statement));

	Token token = lexer_peek_token(&parser->lexer);

	// Identify statement type by first keyword
	if (peek_keyword(parser, "SELECT"))
	{
		stmt->type = STMT_SELECT;
		parse_select(parser, &stmt->select_stmt);
	}
	else if (peek_keyword(parser, "INSERT"))
	{
		stmt->type = STMT_INSERT;
		parse_insert(parser, &stmt->insert_stmt);
	}
	else if (peek_keyword(parser, "UPDATE"))
	{
		stmt->type = STMT_UPDATE;
		parse_update(parser, &stmt->update_stmt);
	}
	else if (peek_keyword(parser, "DELETE"))
	{
		stmt->type = STMT_DELETE;
		parse_delete(parser, &stmt->delete_stmt);
	}
	else if (peek_keyword(parser, "CREATE"))
	{
		stmt->type = STMT_CREATE_TABLE;
		parse_create_table(parser, &stmt->create_table_stmt);
	}
	else if (peek_keyword(parser, "DROP"))
	{
		stmt->type = STMT_DROP_TABLE;
		parse_drop_table(parser, &stmt->drop_table_stmt);
	}
	else if (peek_keyword(parser, "BEGIN"))
	{
		stmt->type = STMT_BEGIN;
		parse_begin(parser, &stmt->begin_stmt);
	}
	else if (peek_keyword(parser, "COMMIT"))
	{
		stmt->type = STMT_COMMIT;
		parse_commit(parser, &stmt->commit_stmt);
	}
	else if (peek_keyword(parser, "ROLLBACK"))
	{
		stmt->type = STMT_ROLLBACK;
		parse_rollback(parser, &stmt->rollback_stmt);
	}
	else
	{
		if (token.type == TOKEN_EOF)
		{
			format_error(parser, "Unexpected end of input");
		}
		else
		{
			format_error(parser, "Unexpected token '%.*s' - expected SQL statement", token.length, token.text);
		}
		return nullptr;
	}

	// Check if parsing failed
	if (!parser->error_msg.empty())
	{
		return nullptr;
	}

	// Consume optional semicolon
	consume_token(parser, TOKEN_SEMICOLON);

	return stmt;
}

array<Statement *, query_arena>
parse_statements(Parser *parser)
{
	array<Statement *, query_arena> statements;
	statements.reset();

	while (true)
	{
		// Skip whitespace and check for EOF
		skip_whitespace(&parser->lexer);
		if (lexer_peek_token(&parser->lexer).type == TOKEN_EOF)
		{
			break;
		}

		Statement *stmt = parse_statement(parser);
		if (!stmt)
		{
			// Return what we've parsed so far with error info
			return statements;
		}

		statements.push(stmt);
	}

	return statements;
}

//=============================================================================
// PUBLIC API
//=============================================================================

ParseResult
parse_sql(const char *sql)
{
	ParseResult result;
	Parser		parser;

	arena<query_arena>::init();

	lexer_init(&parser.lexer, sql);

	parser.error_msg = string_view{};
	parser.error_line = -1;
	parser.error_column = -1;

	result.statements = parse_statements(&parser);

	if (!parser.error_msg.empty())
	{
		result.success = false;
		result.error = parser.error_msg.data();
		result.error_line = parser.error_line;
		result.error_column = parser.error_column;
		result.failed_statement_index = result.statements.size();
	}
	else
	{
		result.success = true;
		result.error = {};
		result.error_line = -1;
		result.error_column = -1;
		result.failed_statement_index = -1;
	}

	return result;
}

//=============================================================================
// DEBUG UTILITIES
//=============================================================================

const char *
token_type_to_string(TokenType type)
{
	switch (type)
	{
	case TOKEN_EOF:
		return "EOF";
	case TOKEN_IDENTIFIER:
		return "IDENTIFIER";
	case TOKEN_NUMBER:
		return "NUMBER";
	case TOKEN_STRING:
		return "STRING";
	case TOKEN_KEYWORD:
		return "KEYWORD";
	case TOKEN_OPERATOR:
		return "OPERATOR";
	case TOKEN_LPAREN:
		return "LPAREN";
	case TOKEN_RPAREN:
		return "RPAREN";
	case TOKEN_COMMA:
		return "COMMA";
	case TOKEN_SEMICOLON:
		return "SEMICOLON";
	case TOKEN_STAR:
		return "STAR";
	default:
		return "UNKNOWN";
	}
}

const char *
stmt_type_to_string(StmtType type)
{
	switch (type)
	{
	case STMT_SELECT:
		return "SELECT";
	case STMT_INSERT:
		return "INSERT";
	case STMT_UPDATE:
		return "UPDATE";
	case STMT_DELETE:
		return "DELETE";
	case STMT_CREATE_TABLE:
		return "CREATE_TABLE";
	case STMT_DROP_TABLE:
		return "DROP_TABLE";
	case STMT_BEGIN:
		return "BEGIN";
	case STMT_COMMIT:
		return "COMMIT";
	case STMT_ROLLBACK:
		return "ROLLBACK";
	default:
		return "UNKNOWN";
	}
}

static void
print_expr(Expr *expr, int indent)
{
	if (!expr)
	{
		printf("%*s<null>\n", indent, "");
		return;
	}

	switch (expr->type)
	{
	case EXPR_LITERAL:
		if (expr->lit_type == TYPE_U32)
		{
			printf("%*sLiteral(INT): %u\n", indent, "", expr->int_val);
		}
		else
		{
			printf("%*sLiteral(TEXT): '%.*s'\n", indent, "", (int)expr->str_val.size(), expr->str_val.data());
		}
		break;

	case EXPR_COLUMN:
		printf("%*sColumn: %.*s\n", indent, "", (int)expr->column_name.size(), expr->column_name.data());
		break;

	case EXPR_BINARY_OP: {
		const char *op_str = "";
		switch (expr->op)
		{
		case OP_EQ:
			op_str = "=";
			break;
		case OP_NE:
			op_str = "!=";
			break;
		case OP_LT:
			op_str = "<";
			break;
		case OP_LE:
			op_str = "<=";
			break;
		case OP_GT:
			op_str = ">";
			break;
		case OP_GE:
			op_str = ">=";
			break;
		case OP_AND:
			op_str = "AND";
			break;
		case OP_OR:
			op_str = "OR";
			break;
		}
		printf("%*sBinaryOp: %s\n", indent, "", op_str);
		print_expr(expr->left, indent + 2);
		print_expr(expr->right, indent + 2);
		break;
	}

	case EXPR_UNARY_OP:
		printf("%*sUnaryOp: %s\n", indent, "", expr->unary_op == OP_NOT ? "NOT" : "NEG");
		print_expr(expr->operand, indent + 2);
		break;

	case EXPR_NULL:
		printf("%*sNULL\n", indent, "");
		break;
	}
}

void
print_ast(Statement *stmt)
{
	if (!stmt)
	{
		printf("Null statement\n");
		return;
	}

	printf("Statement type: %s\n", stmt_type_to_string(stmt->type));

	switch (stmt->type)
	{
	case STMT_SELECT: {
		SelectStmt *s = &stmt->select_stmt;
		printf("  Table: %.*s\n", (int)s->table_name.size(), s->table_name.data());
		if (s->is_star)
		{
			printf("  Columns: *\n");
		}
		else
		{
			printf("  Columns: ");
			for (uint32_t i = 0; i < s->columns.size(); i++)
			{
				if (i > 0)
					printf(", ");
				printf("%.*s", (int)s->columns[i].size(), s->columns[i].data());
			}
			printf("\n");
		}
		if (s->where_clause)
		{
			printf("  WHERE:\n");
			print_expr(s->where_clause, 4);
		}
		if (!s->order_by_column.empty())
		{
			printf("  ORDER BY: %.*s %s\n", (int)s->order_by_column.size(), s->order_by_column.data(),
				   s->order_desc ? "DESC" : "ASC");
		}
		break;
	}

	case STMT_INSERT: {
		InsertStmt *s = &stmt->insert_stmt;
		printf("  Table: %.*s\n", (int)s->table_name.size(), s->table_name.data());
		if (s->columns.size() > 0)
		{
			printf("  Columns: ");
			for (uint32_t i = 0; i < s->columns.size(); i++)
			{
				if (i > 0)
					printf(", ");
				printf("%.*s", (int)s->columns[i].size(), s->columns[i].data());
			}
			printf("\n");
		}
		printf("  Values:\n");
		for (uint32_t i = 0; i < s->values.size(); i++)
		{
			print_expr(s->values[i], 4);
		}
		break;
	}

	case STMT_UPDATE: {
		UpdateStmt *s = &stmt->update_stmt;
		printf("  Table: %.*s\n", (int)s->table_name.size(), s->table_name.data());
		printf("  SET:\n");
		for (uint32_t i = 0; i < s->columns.size(); i++)
		{
			printf("    %.*s = ", (int)s->columns[i].size(), s->columns[i].data());
			print_expr(s->values[i], 0);
		}
		if (s->where_clause)
		{
			printf("  WHERE:\n");
			print_expr(s->where_clause, 4);
		}
		break;
	}

	case STMT_DELETE: {
		DeleteStmt *s = &stmt->delete_stmt;
		printf("  Table: %.*s\n", (int)s->table_name.size(), s->table_name.data());
		if (s->where_clause)
		{
			printf("  WHERE:\n");
			print_expr(s->where_clause, 4);
		}
		break;
	}

	case STMT_CREATE_TABLE: {
		CreateTableStmt *s = &stmt->create_table_stmt;
		printf("  Table: %.*s\n", (int)s->table_name.size(), s->table_name.data());
		printf("  Columns:\n");
		for (uint32_t i = 0; i < s->columns.size(); i++)
		{
			ColumnDef *col = &s->columns[i];
			printf("    %.*s %s%s\n", (int)col->name.size(), col->name.data(), col->type == TYPE_U32 ? "INT" : "TEXT",
				   col->sem.is_primary_key ? " (PRIMARY KEY)" : "");
		}
		break;
	}

	case STMT_DROP_TABLE: {
		DropTableStmt *s = &stmt->drop_table_stmt;
		printf("  Table: %.*s\n", (int)s->table_name.size(), s->table_name.data());
		break;
	}

	case STMT_BEGIN:
	case STMT_COMMIT:
	case STMT_ROLLBACK:
		// No additional info needed
		break;
	}
}
