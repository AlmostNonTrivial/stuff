// test_parser.cpp - Simplified SQL Parser Test Suite
#include "../parser.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>

// Helper to compare strings
inline static bool
str_eq(const string<parser_arena> &a, const char *b)
{
	if (a.empty() && !b)
		return true;
	if (a.empty() || !b)
		return false;
	return strcmp(a.c_str(), b) == 0;
}

//=============================================================================
// SELECT TESTS
//=============================================================================

inline void
test_select_star()
{
	printf("Testing SELECT * FROM table...\n");

	Parser parser;
	parser_init(&parser, "SELECT * FROM users");

	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);
	assert(stmt->type == STMT_SELECT);

	SelectStmt *select = &stmt->select_stmt;
	assert(select->is_star == true);
	assert(str_eq(select->table_name, "users"));
	assert(select->where_clause == nullptr);
	assert(select->order_by_column.empty());

	printf("  ✓ SELECT * passed\n");
}

inline void
test_select_columns()
{
	printf("Testing SELECT columns FROM table...\n");

	Parser parser;
	parser_init(&parser, "SELECT id, name, email FROM users");

	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);

	SelectStmt *select = &stmt->select_stmt;
	assert(select->is_star == false);
	assert(select->columns.size == 3);
	assert(str_eq(select->columns[0], "id"));
	assert(str_eq(select->columns[1], "name"));
	assert(str_eq(select->columns[2], "email"));
	assert(str_eq(select->table_name, "users"));

	printf("  ✓ SELECT columns passed\n");
}

inline void
test_select_where()
{
	printf("Testing SELECT with WHERE...\n");

	Parser parser;
	parser_init(&parser, "SELECT * FROM users WHERE id = 42");

	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);

	SelectStmt *select = &stmt->select_stmt;
	assert(select->where_clause != nullptr);
	assert(select->where_clause->type == EXPR_BINARY_OP);
	assert(select->where_clause->op == OP_EQ);
	assert(str_eq(select->where_clause->left->column_name, "id"));
	assert(select->where_clause->right->int_val == 42);

	printf("  ✓ SELECT with WHERE passed\n");
}

inline void
test_select_where_complex()
{
	printf("Testing SELECT with complex WHERE...\n");

	Parser parser;
	parser_init(&parser, "SELECT * FROM products WHERE price > 100 AND category = 'electronics'");

	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);

	SelectStmt *select = &stmt->select_stmt;
	assert(select->where_clause != nullptr);
	assert(select->where_clause->op == OP_AND);

	// Left: price > 100
	assert(select->where_clause->left->op == OP_GT);
	assert(str_eq(select->where_clause->left->left->column_name, "price"));
	assert(select->where_clause->left->right->int_val == 100);

	// Right: category = 'electronics'
	assert(select->where_clause->right->op == OP_EQ);
	assert(str_eq(select->where_clause->right->left->column_name, "category"));
	assert(str_eq(select->where_clause->right->right->str_val, "electronics"));

	printf("  ✓ Complex WHERE passed\n");
}

inline void
test_select_order_by()
{
	printf("Testing SELECT with ORDER BY...\n");

	Parser parser;

	// Test ASC
	parser_init(&parser, "SELECT * FROM users ORDER BY name ASC");
	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);

	SelectStmt *select = &stmt->select_stmt;
	assert(str_eq(select->order_by_column, "name"));
	assert(select->order_desc == false);

	// Test DESC
	parser_init(&parser, "SELECT * FROM users ORDER BY created_at DESC");
	stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);

	select = &stmt->select_stmt;
	assert(str_eq(select->order_by_column, "created_at"));
	assert(select->order_desc == true);

	// Test default (ASC)
	parser_init(&parser, "SELECT * FROM users ORDER BY id");
	stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);

	select = &stmt->select_stmt;
	assert(str_eq(select->order_by_column, "id"));
	assert(select->order_desc == false);

	printf("  ✓ ORDER BY passed\n");
}

inline void
test_select_full()
{
	printf("Testing full SELECT statement...\n");

	Parser parser;
	parser_init(&parser, "SELECT name, email FROM users WHERE age > 18 ORDER BY name DESC");

	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);

	SelectStmt *select = &stmt->select_stmt;
	assert(select->is_star == false);
	assert(select->columns.size == 2);
	assert(str_eq(select->columns[0], "name"));
	assert(str_eq(select->columns[1], "email"));
	assert(select->where_clause != nullptr);
	assert(select->where_clause->op == OP_GT);
	assert(str_eq(select->order_by_column, "name"));
	assert(select->order_desc == true);

	printf("  ✓ Full SELECT passed\n");
}

//=============================================================================
// INSERT TESTS
//=============================================================================

inline void
test_insert_values_only()
{
	printf("Testing INSERT VALUES...\n");

	Parser parser;
	parser_init(&parser, "INSERT INTO users VALUES (1, 'John', 'john@example.com')");

	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);
	assert(stmt->type == STMT_INSERT);

	InsertStmt *insert = &stmt->insert_stmt;
	assert(str_eq(insert->table_name, "users"));
	assert(insert->columns.size == 0); // No column list
	assert(insert->values.size == 3);
	assert(insert->values[0]->type == EXPR_LITERAL);
	assert(insert->values[0]->int_val == 1);
	assert(insert->values[1]->type == EXPR_LITERAL);
	assert(str_eq(insert->values[1]->str_val, "John"));
	assert(insert->values[2]->type == EXPR_LITERAL);
	assert(str_eq(insert->values[2]->str_val, "john@example.com"));

	printf("  ✓ INSERT VALUES passed\n");
}

inline void
test_insert_with_columns()
{
	printf("Testing INSERT with column list...\n");

	Parser parser;
	parser_init(&parser, "INSERT INTO users (id, name, email) VALUES (1, 'John', 'john@example.com')");

	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);

	InsertStmt *insert = &stmt->insert_stmt;
	assert(insert->columns.size == 3);
	assert(str_eq(insert->columns[0], "id"));
	assert(str_eq(insert->columns[1], "name"));
	assert(str_eq(insert->columns[2], "email"));
	assert(insert->values.size == 3);

	printf("  ✓ INSERT with columns passed\n");
}

inline void
test_insert_with_null()
{
	printf("Testing INSERT with NULL...\n");

	Parser parser;
	parser_init(&parser, "INSERT INTO users VALUES (1, NULL, 'test@example.com')");

	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);

	InsertStmt *insert = &stmt->insert_stmt;
	assert(insert->values.size == 3);
	assert(insert->values[0]->type == EXPR_LITERAL);
	assert(insert->values[1]->type == EXPR_NULL);
	assert(insert->values[2]->type == EXPR_LITERAL);

	printf("  ✓ INSERT with NULL passed\n");
}

//=============================================================================
// UPDATE TESTS
//=============================================================================

inline void
test_update_no_where()
{
	printf("Testing UPDATE without WHERE...\n");

	Parser parser;
	parser_init(&parser, "UPDATE users SET status = 'active'");

	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);
	assert(stmt->type == STMT_UPDATE);

	UpdateStmt *update = &stmt->update_stmt;
	assert(str_eq(update->table_name, "users"));
	assert(update->columns.size == 1);
	assert(str_eq(update->columns[0], "status"));
	assert(update->values.size == 1);
	assert(str_eq(update->values[0]->str_val, "active"));
	assert(update->where_clause == nullptr);

	printf("  ✓ UPDATE without WHERE passed\n");
}

inline void
test_update_with_where()
{
	printf("Testing UPDATE with WHERE...\n");

	Parser parser;
	parser_init(&parser, "UPDATE users SET name = 'Jane' WHERE id = 1");

	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);

	UpdateStmt *update = &stmt->update_stmt;
	assert(str_eq(update->table_name, "users"));
	assert(update->columns.size == 1);
	assert(str_eq(update->columns[0], "name"));
	assert(str_eq(update->values[0]->str_val, "Jane"));
	assert(update->where_clause != nullptr);
	assert(update->where_clause->op == OP_EQ);

	printf("  ✓ UPDATE with WHERE passed\n");
}

inline void
test_update_multiple_columns()
{
	printf("Testing UPDATE multiple columns...\n");

	Parser parser;
	parser_init(&parser, "UPDATE users SET name = 'Jane', age = 30, email = 'jane@example.com' WHERE id = 1");

	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);

	UpdateStmt *update = &stmt->update_stmt;
	assert(update->columns.size == 3);
	assert(str_eq(update->columns[0], "name"));
	assert(str_eq(update->columns[1], "age"));
	assert(str_eq(update->columns[2], "email"));
	assert(str_eq(update->values[0]->str_val, "Jane"));
	assert(update->values[1]->int_val == 30);
	assert(str_eq(update->values[2]->str_val, "jane@example.com"));

	printf("  ✓ UPDATE multiple columns passed\n");
}

//=============================================================================
// DELETE TESTS
//=============================================================================

inline void
test_delete_all()
{
	printf("Testing DELETE without WHERE...\n");

	Parser parser;
	parser_init(&parser, "DELETE FROM users");

	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);
	assert(stmt->type == STMT_DELETE);

	DeleteStmt *del = &stmt->delete_stmt;
	assert(str_eq(del->table_name, "users"));
	assert(del->where_clause == nullptr);

	printf("  ✓ DELETE all passed\n");
}

inline void
test_delete_where()
{
	printf("Testing DELETE with WHERE...\n");

	Parser parser;
	parser_init(&parser, "DELETE FROM users WHERE id = 1");

	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);

	DeleteStmt *del = &stmt->delete_stmt;
	assert(str_eq(del->table_name, "users"));
	assert(del->where_clause != nullptr);
	assert(del->where_clause->op == OP_EQ);
	assert(str_eq(del->where_clause->left->column_name, "id"));
	assert(del->where_clause->right->int_val == 1);

	printf("  ✓ DELETE with WHERE passed\n");
}

//=============================================================================
// DDL TESTS
//=============================================================================

inline void
test_create_table()
{
	printf("Testing CREATE TABLE...\n");

	Parser parser;
	parser_init(&parser, "CREATE TABLE users (id INT, name TEXT, age INT, email TEXT)");

	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);
	assert(stmt->type == STMT_CREATE_TABLE);

	CreateTableStmt *create = &stmt->create_table_stmt;
	assert(str_eq(create->table_name, "users"));
	assert(create->columns.size == 4);

	// Check columns
	assert(str_eq(create->columns[0].name, "id"));
	assert(create->columns[0].type == TYPE_U32);
	assert(create->columns[0].sem.is_primary_key == true); // First column is PK

	assert(str_eq(create->columns[1].name, "name"));
	assert(create->columns[1].type == TYPE_CHAR32);
	assert(create->columns[1].sem.is_primary_key == false);

	assert(str_eq(create->columns[2].name, "age"));
	assert(create->columns[2].type == TYPE_U32);

	assert(str_eq(create->columns[3].name, "email"));
	assert(create->columns[3].type == TYPE_CHAR32);

	printf("  ✓ CREATE TABLE passed\n");
}

inline void
test_drop_table()
{
	printf("Testing DROP TABLE...\n");

	Parser parser;
	parser_init(&parser, "DROP TABLE users");

	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);
	assert(stmt->type == STMT_DROP_TABLE);

	DropTableStmt *drop = &stmt->drop_table_stmt;
	assert(str_eq(drop->table_name, "users"));

	printf("  ✓ DROP TABLE passed\n");
}

//=============================================================================
// TRANSACTION TESTS
//=============================================================================

inline void
test_transactions()
{
	printf("Testing transaction statements...\n");

	Parser parser;

	// BEGIN
	parser_init(&parser, "BEGIN");
	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);
	assert(stmt->type == STMT_BEGIN);

	// COMMIT
	parser_init(&parser, "COMMIT");
	stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);
	assert(stmt->type == STMT_COMMIT);

	// ROLLBACK
	parser_init(&parser, "ROLLBACK");
	stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);
	assert(stmt->type == STMT_ROLLBACK);

	printf("  ✓ Transaction statements passed\n");
}

//=============================================================================
// EXPRESSION TESTS
//=============================================================================

inline void
test_expressions()
{
	printf("Testing expressions...\n");

	Parser parser;

	// Test comparison operators
	parser_init(&parser, "SELECT * FROM t WHERE a = 1 AND b != 2 AND c < 3 AND d <= 4 AND e > 5 AND f >= 6");
	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);
	SelectStmt *select = &stmt->select_stmt;
	assert(select->where_clause != nullptr);

	// Test OR
	parser_init(&parser, "SELECT * FROM t WHERE a = 1 OR b = 2");
	stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);
	select = &stmt->select_stmt;
	assert(select->where_clause->op == OP_OR);

	// Test NOT
	parser_init(&parser, "SELECT * FROM t WHERE NOT active = 1");
	stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);
	select = &stmt->select_stmt;
	assert(select->where_clause->type == EXPR_UNARY_OP);
	assert(select->where_clause->unary_op == OP_NOT);

	// Test parentheses
	parser_init(&parser, "SELECT * FROM t WHERE (a = 1 OR b = 2) AND c = 3");
	stmt = parser_parse_statement(&parser);
	assert(stmt != nullptr);
	select = &stmt->select_stmt;
	assert(select->where_clause->op == OP_AND);
	assert(select->where_clause->left->op == OP_OR);

	printf("  ✓ Expressions passed\n");
}

//=============================================================================
// MULTIPLE STATEMENTS TEST
//=============================================================================

inline void
test_multiple_statements()
{
	printf("Testing multiple statements...\n");

	Parser parser;
	parser_init(&parser, "SELECT * FROM users; "
						 "INSERT INTO users VALUES (1, 'John'); "
						 "UPDATE users SET name = 'Jane' WHERE id = 1; "
						 "DELETE FROM users WHERE id = 2; "
						 "CREATE TABLE test (id INT, name TEXT); "
						 "DROP TABLE old_table; "
						 "BEGIN; "
						 "COMMIT");

	array<Statement *, parser_arena> statements = parser_parse_statements(&parser);
	assert(statements.size == 8);

	assert(statements[0]->type == STMT_SELECT);
	assert(statements[1]->type == STMT_INSERT);
	assert(statements[2]->type == STMT_UPDATE);
	assert(statements[3]->type == STMT_DELETE);
	assert(statements[4]->type == STMT_CREATE_TABLE);
	assert(statements[5]->type == STMT_DROP_TABLE);
	assert(statements[6]->type == STMT_BEGIN);
	assert(statements[7]->type == STMT_COMMIT);

	printf("  ✓ Multiple statements passed\n");
}

inline void
test_statements_without_semicolons()
{
	printf("Testing statements without semicolons...\n");

	Parser parser;
	parser_init(&parser, "SELECT * FROM users "
						 "INSERT INTO users VALUES (1, 'Bob') "
						 "COMMIT");

	array<Statement *, parser_arena> statements = parser_parse_statements(&parser);
	assert(statements.size == 3);
	assert(statements[0]->type == STMT_SELECT);
	assert(statements[1]->type == STMT_INSERT);
	assert(statements[2]->type == STMT_COMMIT);

	printf("  ✓ Statements without semicolons passed\n");
}

//=============================================================================
// ERROR HANDLING TESTS
//=============================================================================

inline void
test_error_handling()
{
	printf("Testing error handling...\n");

	Parser parser;

	// Empty input
	parser_init(&parser, "");
	array<Statement *, parser_arena> statements = parser_parse_statements(&parser);
	assert(statements.size == 0);

	// Invalid statement
	parser_init(&parser, "INVALID SQL HERE");
	Statement *stmt = parser_parse_statement(&parser);
	assert(stmt == nullptr);
	assert(parser.error_msg != nullptr);

	// Incomplete statement
	parser_init(&parser, "SELECT * FROM");
	stmt = parser_parse_statement(&parser);
	assert(stmt == nullptr);

	// Missing closing paren
	parser_init(&parser, "INSERT INTO users (id, name VALUES (1, 'test')");
	stmt = parser_parse_statement(&parser);
	assert(stmt == nullptr);

	printf("  ✓ Error handling passed\n");
}

//=============================================================================
// MAIN TEST RUNNER
//=============================================================================

inline void
test_parser()
{
	arena::init<parser_arena>();

	// SELECT tests
	test_select_star();
	test_select_columns();
	test_select_where();
	test_select_where_complex();
	test_select_order_by();
	test_select_full();

	// INSERT tests
	test_insert_values_only();
	test_insert_with_columns();
	test_insert_with_null();

	// UPDATE tests
	test_update_no_where();
	test_update_with_where();
	test_update_multiple_columns();

	// DELETE tests
	test_delete_all();
	test_delete_where();

	// DDL tests
	test_create_table();
	test_drop_table();

	// Transaction tests
	test_transactions();

	// Expression tests
	test_expressions();

	// Multiple statements
	test_multiple_statements();
	test_statements_without_semicolons();

	// Error handling
	test_error_handling();
}
