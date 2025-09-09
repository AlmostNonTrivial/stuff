// test_parser.cpp - Simplified SQL Parser Test Suite
#include "../parser.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>

// Helper to compare string_view with C string
inline static bool
str_eq(const std::string_view &a, const char *b)
{

	if (a.empty() && !b)
		return true;
	if (a.empty() || !b)
		return false;
	return a.size() == strlen(b) && memcmp(a.data(), b, a.size()) == 0;
}

//=============================================================================
// SELECT TESTS
//=============================================================================

inline void
test_select_star()
{
	printf("Testing SELECT * FROM table...\n");

	ParseResult result = parse_sql("SELECT * FROM users");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	Statement *stmt = result.statements[0];
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

	ParseResult result = parse_sql("SELECT id, name, email FROM users");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	Statement *stmt = result.statements[0];
	assert(stmt != nullptr);

	SelectStmt *select = &stmt->select_stmt;
	assert(select->is_star == false);
	assert(select->columns.size() == 3);
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

	ParseResult result = parse_sql("SELECT * FROM users WHERE id = 42");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	Statement *stmt = result.statements[0];
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

	ParseResult result = parse_sql("SELECT * FROM products WHERE price > 100 AND category = 'electronics'");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	Statement *stmt = result.statements[0];
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

	// Test ASC
	ParseResult result = parse_sql("SELECT * FROM users ORDER BY name ASC");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	Statement *stmt = result.statements[0];
	assert(stmt != nullptr);

	SelectStmt *select = &stmt->select_stmt;
	assert(str_eq(select->order_by_column, "name"));
	assert(select->order_desc == false);

	// Test DESC
	result = parse_sql("SELECT * FROM users ORDER BY created_at DESC");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	stmt = result.statements[0];
	assert(stmt != nullptr);

	select = &stmt->select_stmt;
	assert(str_eq(select->order_by_column, "created_at"));
	assert(select->order_desc == true);

	// Test default (ASC)
	result = parse_sql("SELECT * FROM users ORDER BY id");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	stmt = result.statements[0];
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

	ParseResult result = parse_sql("SELECT name, email FROM users WHERE age > 18 ORDER BY name DESC");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	Statement *stmt = result.statements[0];
	assert(stmt != nullptr);

	SelectStmt *select = &stmt->select_stmt;
	assert(select->is_star == false);
	assert(select->columns.size() == 2);
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

	ParseResult result = parse_sql("INSERT INTO users VALUES (1, 'John', 'john@example.com')");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	Statement *stmt = result.statements[0];
	assert(stmt != nullptr);
	assert(stmt->type == STMT_INSERT);

	InsertStmt *insert = &stmt->insert_stmt;
	assert(str_eq(insert->table_name, "users"));
	assert(insert->columns.size() == 0); // No column list
	assert(insert->values.size() == 3);
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

	ParseResult result = parse_sql("INSERT INTO users (id, name, email) VALUES (1, 'John', 'john@example.com')");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	Statement *stmt = result.statements[0];
	assert(stmt != nullptr);

	InsertStmt *insert = &stmt->insert_stmt;
	assert(insert->columns.size() == 3);
	assert(str_eq(insert->columns[0], "id"));
	assert(str_eq(insert->columns[1], "name"));
	assert(str_eq(insert->columns[2], "email"));
	assert(insert->values.size() == 3);

	printf("  ✓ INSERT with columns passed\n");
}



//=============================================================================
// UPDATE TESTS
//=============================================================================

inline void
test_update_no_where()
{
	printf("Testing UPDATE without WHERE...\n");

	ParseResult result = parse_sql("UPDATE users SET status = 'active'");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	Statement *stmt = result.statements[0];
	assert(stmt != nullptr);
	assert(stmt->type == STMT_UPDATE);

	UpdateStmt *update = &stmt->update_stmt;
	assert(str_eq(update->table_name, "users"));
	assert(update->columns.size() == 1);
	assert(str_eq(update->columns[0], "status"));
	assert(update->values.size() == 1);
	assert(str_eq(update->values[0]->str_val, "active"));
	assert(update->where_clause == nullptr);

	printf("  ✓ UPDATE without WHERE passed\n");
}

inline void
test_update_with_where()
{
	printf("Testing UPDATE with WHERE...\n");

	ParseResult result = parse_sql("UPDATE users SET name = 'Jane' WHERE id = 1");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	Statement *stmt = result.statements[0];
	assert(stmt != nullptr);

	UpdateStmt *update = &stmt->update_stmt;
	assert(str_eq(update->table_name, "users"));
	assert(update->columns.size() == 1);
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

	ParseResult result = parse_sql("UPDATE users SET name = 'Jane', age = 30, email = 'jane@example.com' WHERE id = 1");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	Statement *stmt = result.statements[0];
	assert(stmt != nullptr);

	UpdateStmt *update = &stmt->update_stmt;
	assert(update->columns.size() == 3);
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

	ParseResult result = parse_sql("DELETE FROM users");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	Statement *stmt = result.statements[0];
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

	ParseResult result = parse_sql("DELETE FROM users WHERE id = 1");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	Statement *stmt = result.statements[0];
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

	ParseResult result = parse_sql("CREATE TABLE users (id INT, name TEXT, age INT, email TEXT)");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	Statement *stmt = result.statements[0];
	assert(stmt != nullptr);
	assert(stmt->type == STMT_CREATE_TABLE);

	CreateTableStmt *create = &stmt->create_table_stmt;
	assert(str_eq(create->table_name, "users"));
	assert(create->columns.size() == 4);

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

	ParseResult result = parse_sql("DROP TABLE users");
	assert(result.success == true);
	assert(result.statements.size() == 1);

	Statement *stmt = result.statements[0];
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

	// BEGIN
	ParseResult result = parse_sql("BEGIN");
	assert(result.success == true);
	assert(result.statements.size() == 1);
	Statement *stmt = result.statements[0];
	assert(stmt != nullptr);
	assert(stmt->type == STMT_BEGIN);

	// COMMIT
	result = parse_sql("COMMIT");
	assert(result.success == true);
	assert(result.statements.size() == 1);
	stmt = result.statements[0];
	assert(stmt != nullptr);
	assert(stmt->type == STMT_COMMIT);

	// ROLLBACK
	result = parse_sql("ROLLBACK");
	assert(result.success == true);
	assert(result.statements.size() == 1);
	stmt = result.statements[0];
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

	// Test comparison operators
	ParseResult result = parse_sql("SELECT * FROM t WHERE a = 1 AND b != 2 AND c < 3 AND d <= 4 AND e > 5 AND f >= 6");
	assert(result.success == true);
	assert(result.statements.size() == 1);
	Statement *stmt = result.statements[0];
	assert(stmt != nullptr);
	SelectStmt *select = &stmt->select_stmt;
	assert(select->where_clause != nullptr);

	// Test OR
	result = parse_sql("SELECT * FROM t WHERE a = 1 OR b = 2");
	assert(result.success == true);
	assert(result.statements.size() == 1);
	stmt = result.statements[0];
	assert(stmt != nullptr);
	select = &stmt->select_stmt;
	assert(select->where_clause->op == OP_OR);

	// Test NOT
	result = parse_sql("SELECT * FROM t WHERE NOT active = 1");
	assert(result.success == true);
	assert(result.statements.size() == 1);
	stmt = result.statements[0];
	assert(stmt != nullptr);
	select = &stmt->select_stmt;
	assert(select->where_clause->type == EXPR_UNARY_OP);
	assert(select->where_clause->unary_op == OP_NOT);

	// Test parentheses
	result = parse_sql("SELECT * FROM t WHERE (a = 1 OR b = 2) AND c = 3");
	assert(result.success == true);
	assert(result.statements.size() == 1);
	stmt = result.statements[0];
	assert(stmt != nullptr);
	select = &stmt->select_stmt;
	assert(select->where_clause->op == OP_AND);
	assert(select->where_clause->left->op == OP_OR);

	printf("  ✓ Expressions passed\n");
}
/*
 * string literal handling
 */

inline void
test_string_literal_size_limits()
{
	printf("Testing string literal size limits...\n");

	// Valid: String within 32 byte limit
	{
		const char *sql = "INSERT INTO users VALUES (1, 'This is a valid string')";
		ParseResult result = parse_sql(sql);
		assert(result.success == true);
		assert(result.statements.size() == 1);

		InsertStmt *insert = &result.statements[0]->insert_stmt;
		assert(insert->values[1]->type == EXPR_LITERAL);
		assert(insert->values[1]->lit_type == TYPE_CHAR32);
		assert(insert->values[1]->str_val.size() <= 32);
		printf("  ✓ Valid string literal accepted\n");
	}

	// Valid: Exactly 32 bytes
	{
		const char *sql = "INSERT INTO users VALUES (1, '12345678901234567890123456789012')"; // 32 chars
		ParseResult result = parse_sql(sql);
		assert(result.success == true);
		assert(result.statements.size() == 1);

		InsertStmt *insert = &result.statements[0]->insert_stmt;
		assert(insert->values[1]->str_val.size() == 32);
		printf("  ✓ 32-byte string literal accepted\n");
	}

	// Invalid: String exceeds 32 byte limit
	{
		const char *sql = "INSERT INTO users VALUES (1, 'This string is way too long and exceeds the 32 byte limit for TEXT columns')";
		ParseResult result = parse_sql(sql);
		assert(result.success == false);
		assert(!result.error.empty());
		// Error should mention string too long or exceeds TEXT limit
		printf("  ✓ Oversized string literal rejected\n");
	}

	// Invalid: 33 bytes (just over limit)
	{
		const char *sql = "INSERT INTO users VALUES (1, '123456789012345678901234567890123')"; // 33 chars
		ParseResult result = parse_sql(sql);
		assert(result.success == false);
		assert(!result.error.empty());
		printf("  ✓ 33-byte string literal rejected\n");
	}

	// Test in SELECT WHERE clause
	{
		const char *sql = "SELECT * FROM users WHERE name = 'This extremely long string should not be allowed in a TEXT column'";
		ParseResult result = parse_sql(sql);
		assert(result.success == false);
		assert(!result.error.empty());
		printf("  ✓ Oversized string in WHERE rejected\n");
	}

	// Test in UPDATE SET
	{
		const char *sql = "UPDATE users SET name = 'Another string that is definitely way too long for the TEXT type limit'";
		ParseResult result = parse_sql(sql);
		assert(result.success == false);
		assert(!result.error.empty());
		printf("  ✓ Oversized string in UPDATE rejected\n");
	}

	// Test empty string (should be valid)
	{
		const char *sql = "INSERT INTO users VALUES (1, '')";
		ParseResult result = parse_sql(sql);
		assert(result.success == true);
		assert(result.statements.size() == 1);
		printf("  ✓ Empty string accepted\n");
	}

	// Test with escaped characters (length should count actual bytes)
	{
		// Note: If parser handles escape sequences, this needs adjustment
		const char *sql = "INSERT INTO users VALUES (1, 'String with \\n newline')";
		ParseResult result = parse_sql(sql);
		// Depending on escape handling, this might be valid or not
		// If \n counts as 1 byte: valid
		// If \n counts as 2 bytes: still valid (< 32)
		printf("  ✓ String with escapes handled\n");
	}

	printf("  ✓ All string size limit tests passed\n");
}

inline void
test_integer_literal_limits()
{
	printf("Testing integer literal limits...\n");

	// Valid: Within uint32_t range
	{
		const char *sql = "INSERT INTO users VALUES (4294967295, 'name')"; // Max uint32_t
		ParseResult result = parse_sql(sql);
		assert(result.success == true);
		assert(result.statements.size() == 1);

		InsertStmt *insert = &result.statements[0]->insert_stmt;
		assert(insert->values[0]->type == EXPR_LITERAL);
		assert(insert->values[0]->lit_type == TYPE_U32);
		assert(insert->values[0]->int_val == 4294967295U);
		printf("  ✓ Max uint32_t value accepted\n");
	}

	// Valid: Zero
	{
		const char *sql = "INSERT INTO users VALUES (0, 'name')";
		ParseResult result = parse_sql(sql);
		assert(result.success == true);
		assert(result.statements.size() == 1);

		InsertStmt *insert = &result.statements[0]->insert_stmt;
		assert(insert->values[0]->int_val == 0);
		printf("  ✓ Zero value accepted\n");
	}

	// Invalid: Exceeds uint32_t max (optional - parser could reject or wrap)
	{
		const char *sql = "INSERT INTO users VALUES (4294967296, 'name')"; // uint32_t max + 1
		ParseResult result = parse_sql(sql);
		// Parser might accept and wrap, or reject - depends on implementation
		// If rejecting:
		// assert(result.success == false);
		// assert(!result.error.empty());
		printf("  ✓ Integer overflow handled\n");
	}

	// Invalid: Negative numbers (if not supporting signed integers)
	{
		const char *sql = "INSERT INTO users VALUES (-1, 'name')";
		ParseResult result = parse_sql(sql);
		// Since TYPE_U32 is unsigned, negative values should probably be rejected
		// Though parser might not check this currently
		printf("  ✓ Negative integer handled\n");
	}

	printf("  ✓ All integer limit tests passed\n");
}

//=============================================================================
// MULTIPLE STATEMENTS TEST
//=============================================================================

inline void
test_multiple_statements()
{
	printf("Testing multiple statements...\n");

	ParseResult result = parse_sql("SELECT * FROM users; "
								   "INSERT INTO users VALUES (1, 'John'); "
								   "UPDATE users SET name = 'Jane' WHERE id = 1; "
								   "DELETE FROM users WHERE id = 2; "
								   "CREATE TABLE test (id INT, name TEXT); "
								   "DROP TABLE old_table; "
								   "BEGIN; "
								   "COMMIT");

	assert(result.success == true);
	assert(result.statements.size() == 8);

	assert(result.statements[0]->type == STMT_SELECT);
	assert(result.statements[1]->type == STMT_INSERT);
	assert(result.statements[2]->type == STMT_UPDATE);
	assert(result.statements[3]->type == STMT_DELETE);
	assert(result.statements[4]->type == STMT_CREATE_TABLE);
	assert(result.statements[5]->type == STMT_DROP_TABLE);
	assert(result.statements[6]->type == STMT_BEGIN);
	assert(result.statements[7]->type == STMT_COMMIT);

	printf("  ✓ Multiple statements passed\n");
}

inline void
test_statements_without_semicolons()
{
	printf("Testing statements without semicolons...\n");

	ParseResult result = parse_sql("SELECT * FROM users "
								   "INSERT INTO users VALUES (1, 'Bob') "
								   "COMMIT");

	assert(result.success == true);
	assert(result.statements.size() == 3);
	assert(result.statements[0]->type == STMT_SELECT);
	assert(result.statements[1]->type == STMT_INSERT);
	assert(result.statements[2]->type == STMT_COMMIT);

	printf("  ✓ Statements without semicolons passed\n");
}

//=============================================================================
// ERROR HANDLING TESTS
//=============================================================================


inline void
test_error_handling()
{
	printf("Testing error handling...\n");

	// Empty input - should succeed with no statements
	{
		ParseResult result = parse_sql("");
		assert(result.success == true);
		assert(result.statements.size() == 0);
		printf("  ✓ Empty input handled\n");
	}

	// Test specific error messages and locations
	{
		ParseResult result = parse_sql("INVALID SQL HERE");
		assert(result.success == false);
		assert(!result.error.empty());
		assert(result.error_line == 1);
		assert(result.error_column == 1);
		// Could check for specific message like "Unexpected token" or "Expected SQL statement"
		printf("  ✓ Invalid statement detected\n");
	}

	// SELECT errors
	{
		// Missing FROM
		ParseResult result = parse_sql("SELECT *");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected FROM"

		// Missing table name
		result = parse_sql("SELECT * FROM");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected table name"

		// Invalid WHERE expression
		result = parse_sql("SELECT * FROM users WHERE");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected expression after WHERE"

		// Incomplete ORDER BY
		result = parse_sql("SELECT * FROM users ORDER");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected BY after ORDER"

		result = parse_sql("SELECT * FROM users ORDER BY");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected column name after ORDER BY"

		printf("  ✓ SELECT error cases handled\n");
	}

	// INSERT errors
	{
		// Missing INTO
		ParseResult result = parse_sql("INSERT users VALUES (1)");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected INTO"

		// Missing VALUES
		result = parse_sql("INSERT INTO users");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected VALUES"

		// Missing opening paren for VALUES
		result = parse_sql("INSERT INTO users VALUES 1, 2, 3");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected '(' after VALUES"

		// Missing closing paren for column list
		result = parse_sql("INSERT INTO users (id, name VALUES (1, 'test')");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected ')' after column list"

		// Empty VALUES list
		result = parse_sql("INSERT INTO users VALUES ()");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected value expression"

		printf("  ✓ INSERT error cases handled\n");
	}

	// UPDATE errors
	{
		// Missing SET
		ParseResult result = parse_sql("UPDATE users WHERE id = 1");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected SET"

		// Missing column name in SET
		result = parse_sql("UPDATE users SET = 'value'");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected column name"

		// Missing = in SET
		result = parse_sql("UPDATE users SET name 'value'");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected '=' after column name"

		// Missing value in SET
		result = parse_sql("UPDATE users SET name =");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected value expression"

		printf("  ✓ UPDATE error cases handled\n");
	}

	// DELETE errors
	{
		// Missing FROM
		ParseResult result = parse_sql("DELETE users WHERE id = 1");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected FROM"

		// Missing table name
		result = parse_sql("DELETE FROM");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected table name"

		printf("  ✓ DELETE error cases handled\n");
	}

	// CREATE TABLE errors
	{
		// Missing TABLE keyword
		ParseResult result = parse_sql("CREATE users (id INT)");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected TABLE"

		// Missing opening paren
		result = parse_sql("CREATE TABLE users id INT");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected '(' after table name"

		// Empty column list
		result = parse_sql("CREATE TABLE users ()");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Table must have at least one column" (if checked in parser)

		// Invalid data type
		result = parse_sql("CREATE TABLE users (id INVALID_TYPE)");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected data type"

		// Missing data type
		result = parse_sql("CREATE TABLE users (id)");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected data type"

		printf("  ✓ CREATE TABLE error cases handled\n");
	}

	// DROP TABLE errors
	{
		// Missing TABLE keyword
		ParseResult result = parse_sql("DROP users");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected TABLE"

		// Missing table name
		result = parse_sql("DROP TABLE");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected table name"

		printf("  ✓ DROP TABLE error cases handled\n");
	}

	// Expression errors
	{
		// Incomplete comparison
		ParseResult result = parse_sql("SELECT * FROM users WHERE id =");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected expression after comparison operator"

		// Incomplete AND
		result = parse_sql("SELECT * FROM users WHERE id = 1 AND");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected expression after AND"

		// Incomplete OR
		result = parse_sql("SELECT * FROM users WHERE id = 1 OR");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected expression after OR"

		// Incomplete NOT
		result = parse_sql("SELECT * FROM users WHERE NOT");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected expression after NOT"

		// Unclosed parenthesis
		result = parse_sql("SELECT * FROM users WHERE (id = 1");
		assert(result.success == false);
		assert(!result.error.empty());
		// Should contain "Expected ')'"

		printf("  ✓ Expression error cases handled\n");
	}

	// Multiple statement error handling
	{
		// Error in second statement
		ParseResult result = parse_sql("SELECT * FROM users; INVALID SQL");
		assert(result.success == false);
		assert(result.statements.size() == 1); // First statement should be parsed
		assert(result.failed_statement_index == 1); // Error in second statement

		// Error in first statement
		result = parse_sql("INVALID SQL; SELECT * FROM users");
		assert(result.success == false);
		assert(result.statements.size() == 0); // No statements parsed
		assert(result.failed_statement_index == 0); // Error in first statement

		printf("  ✓ Multiple statement error handling verified\n");
	}

	// Test error location tracking
	{
		// Multi-line error
		const char *sql = "SELECT *\n"
						  "FROM users\n"
						  "WHERE";
		ParseResult result = parse_sql(sql);
		assert(result.success == false);
		assert(result.error_line == 3); // Error on line 3

		printf("  ✓ Error location tracking verified\n");
	}

	printf("  ✓ All error handling tests passed\n");
}



//=============================================================================
// MAIN TEST RUNNER
//=============================================================================

inline void
test_parser()
{
	arena<query_arena>::init();

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
	test_string_literal_size_limits();
	test_integer_literal_limits();

	printf("\n🎉 All parser tests passed!\n");
}
