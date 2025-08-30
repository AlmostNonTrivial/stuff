#pragma once
#include "parser.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>

// Helper to compare strings
inline static   bool str_eq(const char* a, const char* b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}


inline static   void test_in_operator() {
    printf("Testing IN operator...\n");

    Parser parser;

    // Test basic IN with numbers
    parser_init(&parser, "SELECT * FROM users WHERE id IN (1, 2, 3)");
    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_SELECT);

    SelectStmt* select = stmt->select_stmt;
    assert(select->where_clause != nullptr);
    assert(select->where_clause->type == EXPR_BINARY_OP);
    assert(select->where_clause->op == OP_IN);

    // Check left side is column
    assert(select->where_clause->left->type == EXPR_COLUMN);
    assert(str_eq(select->where_clause->left->column_name, "id"));

    // Check right side is list
    assert(select->where_clause->right->type == EXPR_LIST);
    assert(select->where_clause->right->list_items->size == 3);
    assert(select->where_clause->right->list_items->data[0]->type == EXPR_LITERAL);
    assert(select->where_clause->right->list_items->data[0]->int_val == 1);
    assert(select->where_clause->right->list_items->data[1]->int_val == 2);
    assert(select->where_clause->right->list_items->data[2]->int_val == 3);

    parser_reset(&parser);

    // Test IN with strings
    parser_init(&parser, "SELECT * FROM users WHERE status IN ('active', 'pending', 'blocked')");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    select = stmt->select_stmt;
    assert(select->where_clause->type == EXPR_BINARY_OP);
    assert(select->where_clause->op == OP_IN);
    assert(select->where_clause->right->type == EXPR_LIST);
    assert(select->where_clause->right->list_items->size == 3);
    assert(str_eq(select->where_clause->right->list_items->data[0]->str_val, "active"));
    assert(str_eq(select->where_clause->right->list_items->data[1]->str_val, "pending"));
    assert(str_eq(select->where_clause->right->list_items->data[2]->str_val, "blocked"));

    parser_reset(&parser);

    // Test IN with single value
    parser_init(&parser, "SELECT * FROM orders WHERE product_id IN (42)");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    select = stmt->select_stmt;
    assert(select->where_clause->op == OP_IN);
    assert(select->where_clause->right->list_items->size == 1);
    assert(select->where_clause->right->list_items->data[0]->int_val == 42);

    parser_reset(&parser);

    // Test IN combined with other operators
    parser_init(&parser, "SELECT * FROM users WHERE active = 1 AND id IN (10, 20, 30)");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    select = stmt->select_stmt;
    assert(select->where_clause->type == EXPR_BINARY_OP);
    assert(select->where_clause->op == OP_AND);

    // Right side should be the IN expression
    Expr* in_expr = select->where_clause->right;
    assert(in_expr->type == EXPR_BINARY_OP);
    assert(in_expr->op == OP_IN);
    assert(in_expr->right->type == EXPR_LIST);
    assert(in_expr->right->list_items->size == 3);

    parser_reset(&parser);

    printf("  ✓ IN operator passed\n");
}

// Test basic SELECT
inline static   void test_select_basic() {
    printf("Testing basic SELECT...\n");

    Parser parser;
    parser_init(&parser, "SELECT * FROM users");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_SELECT);

    SelectStmt* select = stmt->select_stmt;
    assert(select != nullptr);
    assert(select->select_list->size == 1);
    assert(select->select_list->data[0]->type == EXPR_STAR);
    assert(str_eq(select->from_table->table_name, "users"));
    assert(select->from_table->alias == nullptr);
    assert(select->where_clause == nullptr);
    assert(!select->is_distinct);

    parser_reset(&parser);
    printf("  ✓ Basic SELECT passed\n");
}

// Test SELECT with columns
inline static   void test_select_columns() {
    printf("Testing SELECT with columns...\n");

    Parser parser;
    parser_init(&parser, "SELECT id, name, email FROM users");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_SELECT);

    SelectStmt* select = stmt->select_stmt;
    assert(select->select_list->size == 3);

    assert(select->select_list->data[0]->type == EXPR_COLUMN);
    assert(str_eq(select->select_list->data[0]->column_name, "id"));

    assert(select->select_list->data[1]->type == EXPR_COLUMN);
    assert(str_eq(select->select_list->data[1]->column_name, "name"));

    assert(select->select_list->data[2]->type == EXPR_COLUMN);
    assert(str_eq(select->select_list->data[2]->column_name, "email"));

    parser_reset(&parser);
    printf("  ✓ SELECT with columns passed\n");
}

// Test SELECT with WHERE
inline static   void test_select_where() {
    printf("Testing SELECT with WHERE...\n");

    Parser parser;
    parser_init(&parser, "SELECT * FROM users WHERE id = 42 AND active = 1");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    SelectStmt* select = stmt->select_stmt;
    assert(select->where_clause != nullptr);
    assert(select->where_clause->type == EXPR_BINARY_OP);
    assert(select->where_clause->op == OP_AND);

    // Check left side: id = 42
    Expr* left = select->where_clause->left;
    assert(left->type == EXPR_BINARY_OP);
    assert(left->op == OP_EQ);
    assert(left->left->type == EXPR_COLUMN);
    assert(str_eq(left->left->column_name, "id"));
    assert(left->right->type == EXPR_LITERAL);
    assert(left->right->int_val == 42);

    // Check right side: active = 1
    Expr* right = select->where_clause->right;
    assert(right->type == EXPR_BINARY_OP);
    assert(right->op == OP_EQ);
    assert(right->left->type == EXPR_COLUMN);
    assert(str_eq(right->left->column_name, "active"));
    assert(right->right->type == EXPR_LITERAL);
    assert(right->right->int_val == 1);

    parser_reset(&parser);
    printf("  ✓ SELECT with WHERE passed\n");
}

// Test SELECT with complex WHERE
inline static   void test_select_complex_where() {
    printf("Testing SELECT with complex WHERE...\n");

    Parser parser;
    parser_init(&parser,
        "SELECT * FROM products WHERE price > 100 AND (category = 'electronics' OR category = 'computers')");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    SelectStmt* select = stmt->select_stmt;
    assert(select->where_clause != nullptr);
    assert(select->where_clause->type == EXPR_BINARY_OP);
    assert(select->where_clause->op == OP_AND);

    // Check price > 100
    Expr* price_check = select->where_clause->left;
    assert(price_check->type == EXPR_BINARY_OP);
    assert(price_check->op == OP_GT);
    assert(str_eq(price_check->left->column_name, "price"));
    assert(price_check->right->int_val == 100);

    // Check OR expression
    Expr* or_expr = select->where_clause->right;
    assert(or_expr->type == EXPR_BINARY_OP);
    assert(or_expr->op == OP_OR);

    parser_reset(&parser);
    printf("  ✓ SELECT with complex WHERE passed\n");
}

// Test SELECT with JOIN
inline static   void test_select_join() {
    printf("Testing SELECT with JOIN...\n");

    Parser parser;
    parser_init(&parser,
        "SELECT u.name, o.total FROM users u INNER JOIN orders o ON u.id = o.user_id");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    SelectStmt* select = stmt->select_stmt;

    // Check select list
    assert(select->select_list->size == 2);
    assert(select->select_list->data[0]->type == EXPR_COLUMN);
    assert(str_eq(select->select_list->data[0]->table_name, "u"));
    assert(str_eq(select->select_list->data[0]->column_name, "name"));

    // Check FROM table with alias
    assert(str_eq(select->from_table->table_name, "users"));
    assert(str_eq(select->from_table->alias, "u"));

    // Check JOIN
    assert(select->joins != nullptr);
    assert(select->joins->size == 1);
    assert(select->joins->data[0]->type == JOIN_INNER);
    assert(str_eq(select->joins->data[0]->table->table_name, "orders"));
    assert(str_eq(select->joins->data[0]->table->alias, "o"));

    // Check JOIN condition
    Expr* join_cond = select->joins->data[0]->condition;
    assert(join_cond != nullptr);
    assert(join_cond->type == EXPR_BINARY_OP);
    assert(join_cond->op == OP_EQ);

    parser_reset(&parser);
    printf("  ✓ SELECT with JOIN passed\n");
}

// Test SELECT with multiple JOINs
inline static   void test_select_multiple_joins() {
    printf("Testing SELECT with multiple JOINs...\n");

    Parser parser;
    parser_init(&parser,
        "SELECT * FROM users "
        "LEFT JOIN orders ON users.id = orders.user_id "
        "RIGHT JOIN products ON orders.product_id = products.id");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    SelectStmt* select = stmt->select_stmt;
    assert(select->joins->size == 2);
    assert(select->joins->data[0]->type == JOIN_LEFT);
    assert(select->joins->data[1]->type == JOIN_RIGHT);

    parser_reset(&parser);
    printf("  ✓ SELECT with multiple JOINs passed\n");
}

// Test SELECT with ORDER BY
inline static   void test_select_order_by() {
    printf("Testing SELECT with ORDER BY...\n");

    Parser parser;
    parser_init(&parser, "SELECT * FROM users ORDER BY name ASC, created_at DESC");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    SelectStmt* select = stmt->select_stmt;
    assert(select->order_by != nullptr);
    assert(select->order_by->size == 2);

    assert(select->order_by->data[0]->expr->type == EXPR_COLUMN);
    assert(str_eq(select->order_by->data[0]->expr->column_name, "name"));
    assert(select->order_by->data[0]->dir == ORDER_ASC);

    assert(select->order_by->data[1]->expr->type == EXPR_COLUMN);
    assert(str_eq(select->order_by->data[1]->expr->column_name, "created_at"));
    assert(select->order_by->data[1]->dir == ORDER_DESC);

    parser_reset(&parser);
    printf("  ✓ SELECT with ORDER BY passed\n");
}

// Test SELECT with GROUP BY and HAVING
inline static   void test_select_group_by() {
    printf("Testing SELECT with GROUP BY and HAVING...\n");

    Parser parser;
    parser_init(&parser,
        "SELECT category, COUNT(*) FROM products GROUP BY category HAVING COUNT(*) > 5");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    SelectStmt* select = stmt->select_stmt;

    // Check select list has function
    assert(select->select_list->size == 2);
    assert(select->select_list->data[1]->type == EXPR_FUNCTION);
    assert(str_eq(select->select_list->data[1]->func_name, "COUNT"));
    assert(select->select_list->data[1]->args->size == 1);
    assert(select->select_list->data[1]->args->data[0]->type == EXPR_STAR);

    // Check GROUP BY
    assert(select->group_by != nullptr);
    assert(select->group_by->size == 1);
    assert(select->group_by->data[0]->type == EXPR_COLUMN);
    assert(str_eq(select->group_by->data[0]->column_name, "category"));

    // Check HAVING
    assert(select->having_clause != nullptr);
    assert(select->having_clause->type == EXPR_BINARY_OP);
    assert(select->having_clause->op == OP_GT);

    parser_reset(&parser);
    printf("  ✓ SELECT with GROUP BY and HAVING passed\n");
}

// Test SELECT with LIMIT and OFFSET
inline static   void test_select_limit_offset() {
    printf("Testing SELECT with LIMIT and OFFSET...\n");

    Parser parser;
    parser_init(&parser, "SELECT * FROM users LIMIT 10 OFFSET 20");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    SelectStmt* select = stmt->select_stmt;
    assert(select->limit == 10);
    assert(select->offset == 20);

    parser_reset(&parser);
    printf("  ✓ SELECT with LIMIT and OFFSET passed\n");
}

// Test SELECT DISTINCT
inline static   void test_select_distinct() {
    printf("Testing SELECT DISTINCT...\n");

    Parser parser;
    parser_init(&parser, "SELECT DISTINCT category FROM products");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    SelectStmt* select = stmt->select_stmt;
    assert(select->is_distinct);

    parser_reset(&parser);
    printf("  ✓ SELECT DISTINCT passed\n");
}

// Test INSERT basic
inline static   void test_insert_basic() {
    printf("Testing basic INSERT...\n");

    Parser parser;
    parser_init(&parser, "INSERT INTO users VALUES (1, 'John', 'john@example.com')");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_INSERT);

    InsertStmt* insert = stmt->insert_stmt;
    assert(str_eq(insert->table_name, "users"));
    assert(insert->columns == nullptr);  // No column list specified
    assert(insert->values->size == 1);
    assert(insert->values->data[0]->size == 3);

    // Check values
    assert(insert->values->data[0]->data[0]->type == EXPR_LITERAL);
    assert(insert->values->data[0]->data[0]->int_val == 1);

    assert(insert->values->data[0]->data[1]->type == EXPR_LITERAL);
    assert(str_eq(insert->values->data[0]->data[1]->str_val, "John"));

    assert(insert->values->data[0]->data[2]->type == EXPR_LITERAL);
    assert(str_eq(insert->values->data[0]->data[2]->str_val, "john@example.com"));

    parser_reset(&parser);
    printf("  ✓ Basic INSERT passed\n");
}

// Test INSERT with columns
inline static   void test_insert_with_columns() {
    printf("Testing INSERT with columns...\n");

    Parser parser;
    parser_init(&parser,
        "INSERT INTO users (id, name, email) VALUES (1, 'John', 'john@example.com')");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    InsertStmt* insert = stmt->insert_stmt;
    assert(insert->columns != nullptr);
    assert(insert->columns->size == 3);
    assert(str_eq(insert->columns->data[0], "id"));
    assert(str_eq(insert->columns->data[1], "name"));
    assert(str_eq(insert->columns->data[2], "email"));

    parser_reset(&parser);
    printf("  ✓ INSERT with columns passed\n");
}

// Test INSERT with multiple rows
inline static   void test_insert_multiple_rows() {
    printf("Testing INSERT with multiple rows...\n");

    Parser parser;
    parser_init(&parser,
        "INSERT INTO users VALUES (1, 'John'), (2, 'Jane'), (3, 'Bob')");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    InsertStmt* insert = stmt->insert_stmt;
    assert(insert->values->size == 3);

    // Check first row
    assert(insert->values->data[0]->data[0]->int_val == 1);
    assert(str_eq(insert->values->data[0]->data[1]->str_val, "John"));

    // Check second row
    assert(insert->values->data[1]->data[0]->int_val == 2);
    assert(str_eq(insert->values->data[1]->data[1]->str_val, "Jane"));

    // Check third row
    assert(insert->values->data[2]->data[0]->int_val == 3);
    assert(str_eq(insert->values->data[2]->data[1]->str_val, "Bob"));

    parser_reset(&parser);
    printf("  ✓ INSERT with multiple rows passed\n");
}

// Test UPDATE basic
inline static   void test_update_basic() {
    printf("Testing basic UPDATE...\n");

    Parser parser;
    parser_init(&parser, "UPDATE users SET name = 'Jane' WHERE id = 1");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_UPDATE);

    UpdateStmt* update = stmt->update_stmt;
    assert(str_eq(update->table_name, "users"));
    assert(update->columns->size == 1);
    assert(str_eq(update->columns->data[0], "name"));
    assert(update->values->size == 1);
    assert(update->values->data[0]->type == EXPR_LITERAL);
    assert(str_eq(update->values->data[0]->str_val, "Jane"));

    // Check WHERE
    assert(update->where_clause != nullptr);
    assert(update->where_clause->type == EXPR_BINARY_OP);
    assert(update->where_clause->op == OP_EQ);

    parser_reset(&parser);
    printf("  ✓ Basic UPDATE passed\n");
}

// Test UPDATE multiple columns
inline static   void test_update_multiple_columns() {
    printf("Testing UPDATE with multiple columns...\n");

    Parser parser;
    parser_init(&parser,
        "UPDATE users SET name = 'Jane', email = 'jane@example.com', age = 30 WHERE id = 1");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    UpdateStmt* update = stmt->update_stmt;
    assert(update->columns->size == 3);
    assert(str_eq(update->columns->data[0], "name"));
    assert(str_eq(update->columns->data[1], "email"));
    assert(str_eq(update->columns->data[2], "age"));

    assert(update->values->size == 3);
    assert(str_eq(update->values->data[0]->str_val, "Jane"));
    assert(str_eq(update->values->data[1]->str_val, "jane@example.com"));
    assert(update->values->data[2]->int_val == 30);

    parser_reset(&parser);
    printf("  ✓ UPDATE with multiple columns passed\n");
}

// Test DELETE basic
inline static   void test_delete_basic() {
    printf("Testing basic DELETE...\n");

    Parser parser;
    parser_init(&parser, "DELETE FROM users WHERE id = 1");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_DELETE);

    DeleteStmt* del = stmt->delete_stmt;
    assert(str_eq(del->table_name, "users"));
    assert(del->where_clause != nullptr);
    assert(del->where_clause->type == EXPR_BINARY_OP);
    assert(del->where_clause->op == OP_EQ);

    parser_reset(&parser);
    printf("  ✓ Basic DELETE passed\n");
}

// Test DELETE without WHERE
inline static   void test_delete_all() {
    printf("Testing DELETE without WHERE...\n");

    Parser parser;
    parser_init(&parser, "DELETE FROM users");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    DeleteStmt* del = stmt->delete_stmt;
    assert(str_eq(del->table_name, "users"));
    assert(del->where_clause == nullptr);

    parser_reset(&parser);
    printf("  ✓ DELETE without WHERE passed\n");
}

// Test CREATE TABLE
inline static   void test_create_table() {
    printf("Testing CREATE TABLE...\n");

    Parser parser;
    parser_init(&parser,
        "CREATE TABLE users ("
        "  id BIGINT PRIMARY KEY,"
        "  name VARCHAR(100) NOT NULL,"
        "  email VARCHAR(255),"
        "  age INT"
        ")");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_CREATE_TABLE);

    CreateTableStmt* create = stmt->create_table_stmt;
    assert(str_eq(create->table_name, "users"));
    assert(create->columns->size == 4);

    // Check id column
    assert(str_eq(create->columns->data[0]->name, "id"));
    assert(create->columns->data[0]->type == TYPE_8);
    assert(create->columns->data[0]->is_primary_key);
    assert(create->columns->data[0]->is_not_null);

    // Check name column
    assert(str_eq(create->columns->data[1]->name, "name"));
    assert(create->columns->data[1]->type == TYPE_256);
    assert(!create->columns->data[1]->is_primary_key);
    assert(create->columns->data[1]->is_not_null);

    // Check email column
    assert(str_eq(create->columns->data[2]->name, "email"));
    assert(create->columns->data[2]->type == TYPE_256);
    assert(!create->columns->data[2]->is_primary_key);
    assert(!create->columns->data[2]->is_not_null);

    // Check age column
    assert(str_eq(create->columns->data[3]->name, "age"));
    assert(create->columns->data[3]->type == TYPE_4);

    parser_reset(&parser);
    printf("  ✓ CREATE TABLE passed\n");
}

// Test CREATE TABLE IF NOT EXISTS
inline static   void test_create_table_if_not_exists() {
    printf("Testing CREATE TABLE IF NOT EXISTS...\n");

    Parser parser;
    parser_init(&parser,
        "CREATE TABLE IF NOT EXISTS users (id INT PRIMARY KEY)");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    CreateTableStmt* create = stmt->create_table_stmt;
    assert(create->if_not_exists);

    parser_reset(&parser);
    printf("  ✓ CREATE TABLE IF NOT EXISTS passed\n");
}

// Test DROP TABLE
inline static   void test_drop_table() {
    printf("Testing DROP TABLE...\n");

    Parser parser;
    parser_init(&parser, "DROP TABLE users");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_DROP_TABLE);

    DropTableStmt* drop = stmt->drop_table_stmt;
    assert(str_eq(drop->table_name, "users"));
    assert(!drop->if_exists);

    parser_reset(&parser);
    printf("  ✓ DROP TABLE passed\n");
}

// Test DROP TABLE IF EXISTS
inline static   void test_drop_table_if_exists() {
    printf("Testing DROP TABLE IF EXISTS...\n");

    Parser parser;
    parser_init(&parser, "DROP TABLE IF EXISTS users");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    DropTableStmt* drop = stmt->drop_table_stmt;
    assert(str_eq(drop->table_name, "users"));
    assert(drop->if_exists);

    parser_reset(&parser);
    printf("  ✓ DROP TABLE IF EXISTS passed\n");
}

// Test transaction statements
inline static   void test_transactions() {
    printf("Testing transaction statements...\n");

    Parser parser;

    // Test BEGIN
    parser_init(&parser, "BEGIN");
    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_BEGIN);
    parser_reset(&parser);

    // Test COMMIT
    parser_init(&parser, "COMMIT");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_COMMIT);
    parser_reset(&parser);

    // Test ROLLBACK
    parser_init(&parser, "ROLLBACK");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_ROLLBACK);
    parser_reset(&parser);

    printf("  ✓ Transaction statements passed\n");
}

// Test expressions
inline static   void test_expressions() {
    printf("Testing complex expressions...\n");

    Parser parser;

    // Test arithmetic expressions
    parser_init(&parser, "SELECT price * quantity + tax - discount FROM orders");
    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    SelectStmt* select = stmt->select_stmt;
    Expr* expr = select->select_list->data[0];

    // Should be: ((price * quantity) + tax) - discount
    assert(expr->type == EXPR_BINARY_OP);
    assert(expr->op == OP_SUB);

    parser_reset(&parser);

    // Test comparison with arithmetic
    parser_init(&parser, "SELECT * FROM products WHERE price * 1.1 > 100");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    select = stmt->select_stmt;
    assert(select->where_clause != nullptr);
    assert(select->where_clause->type == EXPR_BINARY_OP);
    assert(select->where_clause->op == OP_GT);

    parser_reset(&parser);

    // Test NOT expression
    parser_init(&parser, "SELECT * FROM users WHERE NOT active = 1");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    select = stmt->select_stmt;
    assert(select->where_clause != nullptr);
    assert(select->where_clause->type == EXPR_UNARY_OP);
    assert(select->where_clause->unary_op == OP_NOT);

    parser_reset(&parser);

    printf("  ✓ Complex expressions passed\n");
}

// Test function calls
inline static   void test_functions() {
    printf("Testing function calls...\n");

    Parser parser;
    parser_init(&parser,
        "SELECT COUNT(*), SUM(amount), AVG(price), MAX(score), MIN(age) FROM stats");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    SelectStmt* select = stmt->select_stmt;
    assert(select->select_list->size == 5);

    // Test COUNT(*)
    assert(select->select_list->data[0]->type == EXPR_FUNCTION);
    assert(str_eq(select->select_list->data[0]->func_name, "COUNT"));
    assert(select->select_list->data[0]->args->size == 1);
    assert(select->select_list->data[0]->args->data[0]->type == EXPR_STAR);

    // Test SUM(amount)
    assert(select->select_list->data[1]->type == EXPR_FUNCTION);
    assert(str_eq(select->select_list->data[1]->func_name, "SUM"));
    assert(select->select_list->data[1]->args->size == 1);
    assert(select->select_list->data[1]->args->data[0]->type == EXPR_COLUMN);

    parser_reset(&parser);
    printf("  ✓ Function calls passed\n");
}

// Test LIKE operator
inline static   void test_like_operator() {
    printf("Testing LIKE operator...\n");

    Parser parser;
    parser_init(&parser, "SELECT * FROM users WHERE name LIKE 'John%'");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    SelectStmt* select = stmt->select_stmt;
    assert(select->where_clause != nullptr);
    assert(select->where_clause->type == EXPR_BINARY_OP);
    assert(select->where_clause->op == OP_LIKE);
    assert(select->where_clause->left->type == EXPR_COLUMN);
    assert(str_eq(select->where_clause->left->column_name, "name"));
    assert(select->where_clause->right->type == EXPR_LITERAL);
    assert(str_eq(select->where_clause->right->str_val, "John%"));

    parser_reset(&parser);
    printf("  ✓ LIKE operator passed\n");
}

// Test NULL handling
inline static   void test_null_handling() {
    printf("Testing NULL handling...\n");

    Parser parser;

    // Test NULL in INSERT
    parser_init(&parser, "INSERT INTO users VALUES (1, NULL, 'test@example.com')");
    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    InsertStmt* insert = stmt->insert_stmt;
    assert(insert->values->data[0]->data[1]->type == EXPR_NULL);

    parser_reset(&parser);

    // Test NULL in WHERE
    parser_init(&parser, "SELECT * FROM users WHERE email = NULL");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    SelectStmt* select = stmt->select_stmt;
    assert(select->where_clause->right->type == EXPR_NULL);

    parser_reset(&parser);

    printf("  ✓ NULL handling passed\n");
}

// Test semicolon handling
inline static   void test_semicolon_handling() {
    printf("Testing semicolon handling...\n");

    Parser parser;

    // With semicolon
    parser_init(&parser, "SELECT * FROM users;");
    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    parser_reset(&parser);

    // Without semicolon
    parser_init(&parser, "SELECT * FROM users");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    parser_reset(&parser);

    printf("  ✓ Semicolon handling passed\n");
}

// Test case insensitivity
inline static   void test_case_insensitivity() {
    printf("Testing case insensitivity...\n");

    Parser parser;
    parser_init(&parser, "SeLeCt * FrOm users WhErE id = 1 OrDeR bY name");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_SELECT);

    SelectStmt* select = stmt->select_stmt;
    assert(str_eq(select->from_table->table_name, "users"));
    assert(select->where_clause != nullptr);
    assert(select->order_by != nullptr);

    parser_reset(&parser);
    printf("  ✓ Case insensitivity passed\n");
}

// Test edge cases
inline static   void test_edge_cases() {
    printf("Testing edge cases...\n");

    Parser parser;

    // Empty input
    parser_init(&parser, "");
    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt == nullptr);
    parser_reset(&parser);

    // Invalid syntax
    parser_init(&parser, "SELECT FROM");
    stmt = parser_parse_statement(&parser);
    assert(stmt == nullptr || stmt->select_stmt == nullptr);
    parser_reset(&parser);

    // Nested parentheses
    parser_init(&parser, "SELECT * FROM users WHERE ((id = 1))");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    parser_reset(&parser);

    printf("  ✓ Edge cases passed\n");
}

inline static   void test_subqueries() {
    printf("Testing subqueries...\n");

    Parser parser;

    // Test scalar subquery in WHERE
    parser_init(&parser, "SELECT * FROM products WHERE price > (SELECT AVG(price) FROM products)");
    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_SELECT);

    SelectStmt* select = stmt->select_stmt;
    assert(select->where_clause != nullptr);
    assert(select->where_clause->type == EXPR_BINARY_OP);
    assert(select->where_clause->op == OP_GT);

    // Right side should be subquery
    assert(select->where_clause->right->type == EXPR_SUBQUERY);
    assert(select->where_clause->right->subquery != nullptr);

    // Check the subquery structure
    SelectStmt* subq = select->where_clause->right->subquery;
    assert(subq->select_list->size == 1);
    assert(subq->select_list->data[0]->type == EXPR_FUNCTION);
    assert(str_eq(subq->select_list->data[0]->func_name, "AVG"));
    assert(str_eq(subq->from_table->table_name, "products"));

    parser_reset(&parser);

    // Test IN with subquery
    parser_init(&parser, "SELECT * FROM users WHERE id IN (SELECT user_id FROM orders WHERE total > 100)");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    select = stmt->select_stmt;
    assert(select->where_clause->type == EXPR_BINARY_OP);
    assert(select->where_clause->op == OP_IN);

    // Right side should be subquery, not list
    assert(select->where_clause->right->type == EXPR_SUBQUERY);
    subq = select->where_clause->right->subquery;
    assert(subq != nullptr);
    assert(str_eq(subq->from_table->table_name, "orders"));
    assert(subq->where_clause != nullptr);
    assert(subq->where_clause->op == OP_GT);

    parser_reset(&parser);

    // Test nested subqueries
    parser_init(&parser,
        "SELECT * FROM products WHERE price > (SELECT MAX(price) FROM products WHERE category_id IN (SELECT id FROM categories WHERE name = 'Electronics'))");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    select = stmt->select_stmt;
    assert(select->where_clause->right->type == EXPR_SUBQUERY);

    // First level subquery
    subq = select->where_clause->right->subquery;
    assert(subq->where_clause != nullptr);
    assert(subq->where_clause->op == OP_IN);

    // Nested subquery in IN clause
    assert(subq->where_clause->right->type == EXPR_SUBQUERY);
    SelectStmt* nested = subq->where_clause->right->subquery;
    assert(nested != nullptr);
    assert(str_eq(nested->from_table->table_name, "categories"));

    parser_reset(&parser);

    // Test subquery in SELECT list
    parser_init(&parser,
        "SELECT name, (SELECT COUNT(*) FROM orders WHERE orders.user_id = users.id) FROM users");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    select = stmt->select_stmt;
    assert(select->select_list->size == 2);
    assert(select->select_list->data[0]->type == EXPR_COLUMN);
    assert(select->select_list->data[1]->type == EXPR_SUBQUERY);

    subq = select->select_list->data[1]->subquery;
    assert(subq->select_list->data[0]->type == EXPR_FUNCTION);
    assert(str_eq(subq->select_list->data[0]->func_name, "COUNT"));

    parser_reset(&parser);

    // Test arithmetic with subquery
    parser_init(&parser,
        "SELECT * FROM products WHERE price * 1.1 > (SELECT AVG(price) FROM products) + 10");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    select = stmt->select_stmt;
    assert(select->where_clause->op == OP_GT);

    // Right side should be addition with subquery as left operand
    Expr* right_expr = select->where_clause->right;
    assert(right_expr->type == EXPR_BINARY_OP);
    assert(right_expr->op == OP_ADD);
    assert(right_expr->left->type == EXPR_SUBQUERY);
    assert(right_expr->right->type == EXPR_LITERAL);
    assert(right_expr->right->int_val == 10);

    parser_reset(&parser);

    printf("  ✓ Subqueries passed\n");
}

// Main test runner
inline void test_parser() {
    printf("\n========================================\n");
    printf("    PARSER TEST SUITE\n");
    printf("========================================\n\n");

    // SELECT tests
    test_select_basic();
    test_select_columns();
    test_select_where();
    test_subqueries();
    test_select_complex_where();
    test_select_join();
    test_select_multiple_joins();
    test_select_order_by();
    test_select_group_by();
    test_select_limit_offset();
    test_select_distinct();

    // INSERT tests
    test_insert_basic();
    test_insert_with_columns();
    test_insert_multiple_rows();

    // UPDATE tests
    test_update_basic();
    test_update_multiple_columns();

    // DELETE tests
    test_delete_basic();
    test_delete_all();

    // DDL tests
    test_create_table();
    test_create_table_if_not_exists();
    test_drop_table();
    test_drop_table_if_exists();

    // Transaction tests
    test_transactions();

    // Expression tests
    test_expressions();
    test_functions();
    test_like_operator();
    test_in_operator();
    test_null_handling();

    // Misc tests
    test_semicolon_handling();
    test_case_insensitivity();
    test_edge_cases();

    printf("\n========================================\n");
    printf("    ALL TESTS PASSED! ✓\n");
    printf("========================================\n\n");

    // Clean up arena
    Arena<ParserArena>::shutdown();
}
