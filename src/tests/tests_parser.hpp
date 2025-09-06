#include "../parser.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>

// Helper to compare strings - updated for string<parser_arena>
inline static bool str_eq(const string<parser_arena>& a, const char* b) {
    if (a.empty() && !b) return true;
    if (a.empty() || !b) return false;
    return strcmp(a.c_str(), b) == 0;
}

inline static bool str_eq(const char* a, const char* b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

inline static void test_multiple_statements() {
    printf("Testing multiple statements parsing...\n");

    Parser parser;
    parser_init(&parser,
        "SELECT * FROM users WHERE id = 1; "
        "INSERT INTO users (id, name) VALUES (2, 'Jane'); "
        "UPDATE users SET name = 'John' WHERE id = 1; "
        "DELETE FROM users WHERE id = 3; "
        "CREATE TABLE test (id INT); "
        "DROP TABLE test;"
    );

    array<Statement*, parser_arena>* statements = parser_parse_statements(&parser);
    assert(statements != nullptr);
    assert(statements->size == 6);

    // Test 1: SELECT statement
    Statement* stmt1 = statements->data[0];
    assert(stmt1->type == STMT_SELECT);
    SelectStmt* select = stmt1->select_stmt;
    assert(select != nullptr);
    assert(select->select_list.size == 1);
    assert(select->select_list.data[0]->type == EXPR_STAR);
    assert(str_eq(select->from_table->table_name, "users"));
    assert(select->where_clause != nullptr);
    assert(select->where_clause->op == OP_EQ);
    assert(str_eq(select->where_clause->left->column_name, "id"));
    assert(select->where_clause->right->int_val == 1);

    // Test 2: INSERT statement
    Statement* stmt2 = statements->data[1];
    assert(stmt2->type == STMT_INSERT);
    InsertStmt* insert = stmt2->insert_stmt;
    assert(str_eq(insert->table_name, "users"));
    assert(insert->columns.size == 2);
    assert(str_eq(insert->columns.data[0], "id"));
    assert(str_eq(insert->columns.data[1], "name"));
    assert(insert->values.size == 1);
    assert(insert->values.data[0]->size == 2);
    assert(insert->values.data[0]->data[0]->int_val == 2);
    assert(str_eq(insert->values.data[0]->data[1]->str_val, "Jane"));

    // Test 3: UPDATE statement
    Statement* stmt3 = statements->data[2];
    assert(stmt3->type == STMT_UPDATE);
    UpdateStmt* update = stmt3->update_stmt;
    assert(str_eq(update->table_name, "users"));
    assert(update->columns.size == 1);
    assert(str_eq(update->columns.data[0], "name"));
    assert(str_eq(update->values.data[0]->str_val, "John"));
    assert(update->where_clause != nullptr);
    assert(update->where_clause->op == OP_EQ);
    assert(str_eq(update->where_clause->left->column_name, "id"));
    assert(update->where_clause->right->int_val == 1);

    // Test 4: DELETE statement
    Statement* stmt4 = statements->data[3];
    assert(stmt4->type == STMT_DELETE);
    DeleteStmt* del = stmt4->delete_stmt;
    assert(str_eq(del->table_name, "users"));
    assert(del->where_clause != nullptr);
    assert(del->where_clause->op == OP_EQ);
    assert(str_eq(del->where_clause->left->column_name, "id"));
    assert(del->where_clause->right->int_val == 3);

    // Test 5: CREATE TABLE statement
    Statement* stmt5 = statements->data[4];
    assert(stmt5->type == STMT_CREATE_TABLE);
    CreateTableStmt* create = stmt5->create_table_stmt;
    assert(str_eq(create->table_name, "test"));
    assert(create->columns.size == 1);
    assert(str_eq(create->columns.data[0]->name, "id"));
    assert(create->columns.data[0]->type == TYPE_U32);

    // Test 6: DROP TABLE statement
    Statement* stmt6 = statements->data[5];
    assert(stmt6->type == STMT_DROP_TABLE);
    DropTableStmt* drop = stmt6->drop_table_stmt;
    assert(str_eq(drop->table_name, "test"));
    assert(!drop->if_exists);

    parser_reset(&parser);
    printf("  ✓ Multiple statements parsing passed\n");

    // Test with missing semicolons
    parser_init(&parser,
        "SELECT * FROM users "
        "INSERT INTO users VALUES (1, 'Bob') "
        "COMMIT"
    );

    statements = parser_parse_statements(&parser);
    assert(statements != nullptr);
    assert(statements->size == 3);

    assert(statements->data[0]->type == STMT_SELECT);
    assert(statements->data[1]->type == STMT_INSERT);
    assert(statements->data[2]->type == STMT_COMMIT);

    parser_reset(&parser);
    printf("  ✓ Multiple statements without semicolons passed\n");

    // Test empty input
    parser_init(&parser, "");
    statements = parser_parse_statements(&parser);
    assert(statements != nullptr);
    assert(statements->size == 0);

    parser_reset(&parser);
    printf("  ✓ Empty input handling passed\n");

    // Test single statement
    parser_init(&parser, "SELECT * FROM users");
    statements = parser_parse_statements(&parser);
    assert(statements != nullptr);
    assert(statements->size == 1);
    assert(statements->data[0]->type == STMT_SELECT);

    parser_reset(&parser);
    printf("  ✓ Single statement parsing passed\n");

    // Test invalid statement in middle
    parser_init(&parser,
        "SELECT * FROM users; "
        "INVALID SYNTAX HERE; "
        "INSERT INTO users VALUES (1, 'Bob')"
    );

    statements = parser_parse_statements(&parser);
    assert(statements != nullptr);
    assert(statements->size == 1); // Should stop after first valid statement
    assert(statements->data[0]->type == STMT_SELECT);

    parser_reset(&parser);
    printf("  ✓ Invalid statement handling passed\n");
}

inline static void test_in_operator() {
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
    assert(select->where_clause->right->list_items.size == 3);
    assert(select->where_clause->right->list_items.data[0]->type == EXPR_LITERAL);
    assert(select->where_clause->right->list_items.data[0]->int_val == 1);
    assert(select->where_clause->right->list_items.data[1]->int_val == 2);
    assert(select->where_clause->right->list_items.data[2]->int_val == 3);

    parser_reset(&parser);

    // Test IN with strings
    parser_init(&parser, "SELECT * FROM users WHERE status IN ('active', 'pending', 'blocked')");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    select = stmt->select_stmt;
    assert(select->where_clause->type == EXPR_BINARY_OP);
    assert(select->where_clause->op == OP_IN);
    assert(select->where_clause->right->type == EXPR_LIST);
    assert(select->where_clause->right->list_items.size == 3);
    assert(str_eq(select->where_clause->right->list_items.data[0]->str_val, "active"));
    assert(str_eq(select->where_clause->right->list_items.data[1]->str_val, "pending"));
    assert(str_eq(select->where_clause->right->list_items.data[2]->str_val, "blocked"));

    parser_reset(&parser);

    // Test IN with single value
    parser_init(&parser, "SELECT * FROM orders WHERE product_id IN (42)");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    select = stmt->select_stmt;
    assert(select->where_clause->op == OP_IN);
    assert(select->where_clause->right->list_items.size == 1);
    assert(select->where_clause->right->list_items.data[0]->int_val == 42);

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
    assert(in_expr->right->list_items.size == 3);

    parser_reset(&parser);

    printf("  ✓ IN operator passed\n");
}

// Test basic SELECT
inline static void test_select_basic() {
    printf("Testing basic SELECT...\n");

    Parser parser;
    parser_init(&parser, "SELECT * FROM users");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_SELECT);

    SelectStmt* select = stmt->select_stmt;
    assert(select != nullptr);
    assert(select->select_list.size == 1);
    assert(select->select_list.data[0]->type == EXPR_STAR);
    assert(str_eq(select->from_table->table_name, "users"));
    assert(select->from_table->alias.empty());
    assert(select->where_clause == nullptr);
    assert(!select->is_distinct);

    parser_reset(&parser);
    printf("  ✓ Basic SELECT passed\n");
}

// Test SELECT with columns
inline static void test_select_columns() {
    printf("Testing SELECT with columns...\n");

    Parser parser;
    parser_init(&parser, "SELECT id, name, email FROM users");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_SELECT);

    SelectStmt* select = stmt->select_stmt;
    assert(select->select_list.size == 3);

    assert(select->select_list.data[0]->type == EXPR_COLUMN);
    assert(str_eq(select->select_list.data[0]->column_name, "id"));

    assert(select->select_list.data[1]->type == EXPR_COLUMN);
    assert(str_eq(select->select_list.data[1]->column_name, "name"));

    assert(select->select_list.data[2]->type == EXPR_COLUMN);
    assert(str_eq(select->select_list.data[2]->column_name, "email"));

    parser_reset(&parser);
    printf("  ✓ SELECT with columns passed\n");
}

// Test SELECT with WHERE
inline static void test_select_where() {
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
inline static void test_select_complex_where() {
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
inline static void test_select_join() {
    printf("Testing SELECT with JOIN...\n");

    Parser parser;
    parser_init(&parser,
        "SELECT u.name, o.total FROM users u INNER JOIN orders o ON u.id = o.user_id");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    SelectStmt* select = stmt->select_stmt;

    // Check select list
    assert(select->select_list.size == 2);
    assert(select->select_list.data[0]->type == EXPR_COLUMN);
    assert(str_eq(select->select_list.data[0]->table_name, "u"));
    assert(str_eq(select->select_list.data[0]->column_name, "name"));

    // Check FROM table with alias
    assert(str_eq(select->from_table->table_name, "users"));
    assert(str_eq(select->from_table->alias, "u"));

    // Check JOIN
    assert(select->joins.size == 1);
    assert(select->joins.data[0]->type == JOIN_INNER);
    assert(str_eq(select->joins.data[0]->table->table_name, "orders"));
    assert(str_eq(select->joins.data[0]->table->alias, "o"));

    // Check JOIN condition
    Expr* join_cond = select->joins.data[0]->condition;
    assert(join_cond != nullptr);
    assert(join_cond->type == EXPR_BINARY_OP);
    assert(join_cond->op == OP_EQ);

    parser_reset(&parser);
    printf("  ✓ SELECT with JOIN passed\n");
}

// Test SELECT with multiple JOINs
inline static void test_select_multiple_joins() {
    printf("Testing SELECT with multiple JOINs...\n");

    Parser parser;
    parser_init(&parser,
        "SELECT * FROM users "
        "LEFT JOIN orders ON users.id = orders.user_id "
        "RIGHT JOIN products ON orders.product_id = products.id");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    SelectStmt* select = stmt->select_stmt;
    assert(select->joins.size == 2);
    assert(select->joins.data[0]->type == JOIN_LEFT);
    assert(select->joins.data[1]->type == JOIN_RIGHT);

    parser_reset(&parser);
    printf("  ✓ SELECT with multiple JOINs passed\n");
}

// Test SELECT with ORDER BY
inline static void test_select_order_by() {
    printf("Testing SELECT with ORDER BY...\n");

    Parser parser;
    parser_init(&parser, "SELECT * FROM users ORDER BY name ASC, created_at DESC");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    SelectStmt* select = stmt->select_stmt;
    assert(select->order_by.size == 2);

    assert(select->order_by.data[0]->expr->type == EXPR_COLUMN);
    assert(str_eq(select->order_by.data[0]->expr->column_name, "name"));
    assert(select->order_by.data[0]->dir == ORDER_ASC);

    assert(select->order_by.data[1]->expr->type == EXPR_COLUMN);
    assert(str_eq(select->order_by.data[1]->expr->column_name, "created_at"));
    assert(select->order_by.data[1]->dir == ORDER_DESC);

    parser_reset(&parser);
    printf("  ✓ SELECT with ORDER BY passed\n");
}

// Test SELECT with GROUP BY and HAVING
inline static void test_select_group_by() {
    printf("Testing SELECT with GROUP BY and HAVING...\n");

    Parser parser;
    parser_init(&parser,
        "SELECT category, COUNT(*) FROM products GROUP BY category HAVING COUNT(*) > 5");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    SelectStmt* select = stmt->select_stmt;

    // Check select list has function
    assert(select->select_list.size == 2);
    assert(select->select_list.data[1]->type == EXPR_FUNCTION);
    assert(str_eq(select->select_list.data[1]->func_name, "COUNT"));
    assert(select->select_list.data[1]->args.size == 1);
    assert(select->select_list.data[1]->args.data[0]->type == EXPR_STAR);

    // Check GROUP BY
    assert(select->group_by.size == 1);
    assert(select->group_by.data[0]->type == EXPR_COLUMN);
    assert(str_eq(select->group_by.data[0]->column_name, "category"));

    // Check HAVING
    assert(select->having_clause != nullptr);
    assert(select->having_clause->type == EXPR_BINARY_OP);
    assert(select->having_clause->op == OP_GT);

    parser_reset(&parser);
    printf("  ✓ SELECT with GROUP BY and HAVING passed\n");
}

// Test SELECT with LIMIT and OFFSET
inline static void test_select_limit_offset() {
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
inline static void test_select_distinct() {
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
inline static void test_insert_basic() {
    printf("Testing basic INSERT...\n");

    Parser parser;
    parser_init(&parser, "INSERT INTO users VALUES (1, 'John', 'john@example.com')");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_INSERT);

    InsertStmt* insert = stmt->insert_stmt;
    assert(str_eq(insert->table_name, "users"));
    assert(insert->columns.size == 0);  // No column list specified
    assert(insert->values.size == 1);
    assert(insert->values.data[0]->size == 3);

    // Check values
    assert(insert->values.data[0]->data[0]->type == EXPR_LITERAL);
    assert(insert->values.data[0]->data[0]->int_val == 1);

    assert(insert->values.data[0]->data[1]->type == EXPR_LITERAL);
    assert(str_eq(insert->values.data[0]->data[1]->str_val, "John"));

    assert(insert->values.data[0]->data[2]->type == EXPR_LITERAL);
    assert(str_eq(insert->values.data[0]->data[2]->str_val, "john@example.com"));

    parser_reset(&parser);
    printf("  ✓ Basic INSERT passed\n");
}

// Test INSERT with columns
inline static void test_insert_with_columns() {
    printf("Testing INSERT with columns...\n");

    Parser parser;
    parser_init(&parser,
        "INSERT INTO users (id, name, email) VALUES (1, 'John', 'john@example.com')");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    InsertStmt* insert = stmt->insert_stmt;
    assert(insert->columns.size == 3);
    assert(str_eq(insert->columns.data[0], "id"));
    assert(str_eq(insert->columns.data[1], "name"));
    assert(str_eq(insert->columns.data[2], "email"));

    parser_reset(&parser);
    printf("  ✓ INSERT with columns passed\n");
}

// Test INSERT with multiple rows
inline static void test_insert_multiple_rows() {
    printf("Testing INSERT with multiple rows...\n");

    Parser parser;
    parser_init(&parser,
        "INSERT INTO users VALUES (1, 'John'), (2, 'Jane'), (3, 'Bob')");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    InsertStmt* insert = stmt->insert_stmt;
    assert(insert->values.size == 3);

    // Check first row
    assert(insert->values.data[0]->data[0]->int_val == 1);
    assert(str_eq(insert->values.data[0]->data[1]->str_val, "John"));

    // Check second row
    assert(insert->values.data[1]->data[0]->int_val == 2);
    assert(str_eq(insert->values.data[1]->data[1]->str_val, "Jane"));

    // Check third row
    assert(insert->values.data[2]->data[0]->int_val == 3);
    assert(str_eq(insert->values.data[2]->data[1]->str_val, "Bob"));

    parser_reset(&parser);
    printf("  ✓ INSERT with multiple rows passed\n");
}

// Test UPDATE basic
inline static void test_update_basic() {
    printf("Testing basic UPDATE...\n");

    Parser parser;
    parser_init(&parser, "UPDATE users SET name = 'Jane' WHERE id = 1");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_UPDATE);

    UpdateStmt* update = stmt->update_stmt;
    assert(str_eq(update->table_name, "users"));
    assert(update->columns.size == 1);
    assert(str_eq(update->columns.data[0], "name"));
    assert(update->values.size == 1);
    assert(update->values.data[0]->type == EXPR_LITERAL);
    assert(str_eq(update->values.data[0]->str_val, "Jane"));

    // Check WHERE
    assert(update->where_clause != nullptr);
    assert(update->where_clause->type == EXPR_BINARY_OP);
    assert(update->where_clause->op == OP_EQ);

    parser_reset(&parser);
    printf("  ✓ Basic UPDATE passed\n");
}

// Test UPDATE multiple columns
inline static void test_update_multiple_columns() {
    printf("Testing UPDATE with multiple columns...\n");

    Parser parser;
    parser_init(&parser,
        "UPDATE users SET name = 'Jane', email = 'jane@example.com', age = 30 WHERE id = 1");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    UpdateStmt* update = stmt->update_stmt;
    assert(update->columns.size == 3);
    assert(str_eq(update->columns.data[0], "name"));
    assert(str_eq(update->columns.data[1], "email"));
    assert(str_eq(update->columns.data[2], "age"));

    assert(update->values.size == 3);
    assert(str_eq(update->values.data[0]->str_val, "Jane"));
    assert(str_eq(update->values.data[1]->str_val, "jane@example.com"));
    assert(update->values.data[2]->int_val == 30);

    parser_reset(&parser);
    printf("  ✓ UPDATE with multiple columns passed\n");
}

// Test DELETE basic
inline static void test_delete_basic() {
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
inline static void test_delete_all() {
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
inline static void test_create_table() {
    printf("Testing CREATE TABLE...\n");

    Parser parser;
    parser_init(&parser,
        "CREATE TABLE users ("
        "  id U64 PRIMARY KEY,"
        "  name CHAR32 NOT NULL,"
        "  email CHAR32,"
        "  age U32"
        ")");

    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_CREATE_TABLE);

    CreateTableStmt* create = stmt->create_table_stmt;
    assert(str_eq(create->table_name, "users"));
    assert(create->columns.size == 4);

    // Check id column
    assert(str_eq(create->columns.data[0]->name, "id"));
    assert(create->columns.data[0]->type == TYPE_U64);
    assert(create->columns.data[0]->is_primary_key);
    assert(create->columns.data[0]->is_not_null);

    // Check name column
    assert(str_eq(create->columns.data[1]->name, "name"));
    assert(create->columns.data[1]->type == TYPE_CHAR32);
    assert(!create->columns.data[1]->is_primary_key);
    assert(create->columns.data[1]->is_not_null);

    // Check email column
    assert(str_eq(create->columns.data[2]->name, "email"));
    assert(create->columns.data[2]->type == TYPE_CHAR32);
    assert(!create->columns.data[2]->is_primary_key);
    assert(!create->columns.data[2]->is_not_null);

    // Check age column
    assert(str_eq(create->columns.data[3]->name, "age"));
    assert(create->columns.data[3]->type == TYPE_U32);

    parser_reset(&parser);
    printf("  ✓ CREATE TABLE passed\n");
}

// Test CREATE TABLE IF NOT EXISTS
inline static void test_create_table_if_not_exists() {
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
inline static void test_drop_table() {
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
inline static void test_drop_table_if_exists() {
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
inline static void test_transactions() {
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

// Continue with remaining tests...
// (Rest of test functions follow similar pattern of updates)

void test_create_index() {
    printf("Testing CREATE INDEX...\n");

    Parser parser;

    // Basic index
    parser_init(&parser, "CREATE INDEX idx_users_email ON users (email)");
    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_CREATE_INDEX);

    CreateIndexStmt* create = stmt->create_index_stmt;
    assert(str_eq(create->index_name, "idx_users_email"));
    assert(str_eq(create->table_name, "users"));
    assert(create->columns.size == 1);
    assert(str_eq(create->columns.data[0], "email"));
    assert(!create->is_unique);

    parser_reset(&parser);

    // Unique index with multiple columns
    parser_init(&parser, "CREATE UNIQUE INDEX idx_composite ON orders (user_id, product_id)");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    create = stmt->create_index_stmt;
    assert(create->is_unique);
    assert(create->columns.size == 2);
    assert(str_eq(create->columns.data[0], "user_id"));
    assert(str_eq(create->columns.data[1], "product_id"));

    parser_reset(&parser);

    // IF NOT EXISTS
    parser_init(&parser, "CREATE INDEX IF NOT EXISTS idx_test ON test (col1)");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    create = stmt->create_index_stmt;
    assert(create->if_not_exists);

    parser_reset(&parser);

    printf("  ✓ CREATE INDEX passed\n");
}

void test_drop_index() {
    printf("Testing DROP INDEX...\n");

    Parser parser;

    // Basic drop
    parser_init(&parser, "DROP INDEX idx_users_email");
    Statement* stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);
    assert(stmt->type == STMT_DROP_INDEX);

    DropIndexStmt* drop = stmt->drop_index_stmt;
    assert(str_eq(drop->index_name, "idx_users_email"));
    assert(drop->table_name.empty());
    assert(!drop->if_exists);

    parser_reset(&parser);

    // With ON table_name
    parser_init(&parser, "DROP INDEX idx_test ON users");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    drop = stmt->drop_index_stmt;
    assert(str_eq(drop->index_name, "idx_test"));
    assert(str_eq(drop->table_name, "users"));

    parser_reset(&parser);

    // IF EXISTS
    parser_init(&parser, "DROP INDEX IF EXISTS idx_old");
    stmt = parser_parse_statement(&parser);
    assert(stmt != nullptr);

    drop = stmt->drop_index_stmt;
    assert(drop->if_exists);

    parser_reset(&parser);

    printf("  ✓ DROP INDEX passed\n");
}

// Main test runner
int test_parser() {
    printf("\n========================================\n");
    printf("    PARSER TEST SUITE\n");
    printf("========================================\n\n");

    arena::init<parser_arena>();

    // SELECT tests
    test_select_basic();
    test_select_columns();
    test_select_where();
    test_select_complex_where();
    test_select_join();
    test_select_multiple_joins();
    test_select_order_by();
    test_select_group_by();
    test_select_limit_offset();
    test_select_distinct();
    test_create_index();
    test_drop_index();

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

    test_multiple_statements();
    // Transaction tests
    test_transactions();

    // Additional tests
    test_in_operator();

    printf("\n========================================\n");
    printf("    ALL TESTS PASSED! ✓\n");
    printf("========================================\n\n");

    // Clean up arena
    arena::shutdown<parser_arena>();

    return 0;
}
