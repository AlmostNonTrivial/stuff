#include "parser.hpp"
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// SQL Keywords
static const char* sql_keywords[] = {
    "SELECT", "FROM", "WHERE", "INSERT", "INTO", "VALUES", "UPDATE", "SET",
    "DELETE", "CREATE", "TABLE", "DROP", "BEGIN", "COMMIT", "ROLLBACK",
    "JOIN", "INNER", "LEFT", "RIGHT", "CROSS", "ON", "AND", "OR", "NOT",
    "NULL", "DISTINCT", "AS", "ORDER", "BY", "GROUP", "HAVING", "LIMIT",
    "OFFSET", "ASC", "DESC", "IF", "EXISTS", "PRIMARY", "KEY", "INT",
    "BIGINT", "VARCHAR", "TEXT", "LIKE", "IN", "BETWEEN", "IS", "TRUE",
    "FALSE", "COUNT", "SUM", "AVG", "MIN", "MAX"
};

// Initialize keywords map
static void init_keywords(string_map<bool, ParserArena>* keywords) {
    for (const char* kw : sql_keywords) {
        stringmap_insert(keywords, kw, true);
    }
}

// String interning using arena
const char* intern_string(const char* str, uint32_t length) {
    char* interned = (char*)Arena<ParserArena>::alloc(length + 1);
    memcpy(interned, str, length);
    interned[length] = '\0';
    return interned;
}

// Case-insensitive string comparison for keywords
static bool str_eq_ci(const char* a, uint32_t a_len, const char* b) {
    uint32_t b_len = strlen(b);
    if (a_len != b_len) return false;

    for (uint32_t i = 0; i < a_len; i++) {
        if (toupper(a[i]) != toupper(b[i])) {
            return false;
        }
    }
    return true;
}

// Lexer implementation
void lexer_init(Lexer* lex, const char* input) {
    lex->input = input;
    lex->current = input;
    lex->line = 1;
    lex->column = 1;
    lex->current_token = {TOKEN_EOF, nullptr, 0, 0, 0};
}

static void skip_whitespace(Lexer* lex) {
    while (*lex->current) {
        if (*lex->current == ' ' || *lex->current == '\t' || *lex->current == '\r') {
            lex->column++;
            lex->current++;
        } else if (*lex->current == '\n') {
            lex->line++;
            lex->column = 1;
            lex->current++;
        } else if (lex->current[0] == '-' && lex->current[1] == '-') {
            // Skip SQL comment
            while (*lex->current && *lex->current != '\n') {
                lex->current++;
            }
        } else {
            break;
        }
    }
}

Token lexer_next_token(Lexer* lex) {
    skip_whitespace(lex);

    Token token;
    token.line = lex->line;
    token.column = lex->column;
    token.text = lex->current;

    if (*lex->current == '\0') {
        token.type = TOKEN_EOF;
        token.length = 0;
        lex->current_token = token;
        return token;
    }

    // Single character tokens
    switch (*lex->current) {
        case '(':
            token.type = TOKEN_LPAREN;
            token.length = 1;
            lex->current++;
            lex->column++;
            lex->current_token = token;
            return token;

        case ')':
            token.type = TOKEN_RPAREN;
            token.length = 1;
            lex->current++;
            lex->column++;
            lex->current_token = token;
            return token;

        case ',':
            token.type = TOKEN_COMMA;
            token.length = 1;
            lex->current++;
            lex->column++;
            lex->current_token = token;
            return token;

        case ';':
            token.type = TOKEN_SEMICOLON;
            token.length = 1;
            lex->current++;
            lex->column++;
            lex->current_token = token;
            return token;

        case '.':
            token.type = TOKEN_DOT;
            token.length = 1;
            lex->current++;
            lex->column++;
            lex->current_token = token;
            return token;

        case '*':
            token.type = TOKEN_STAR;
            token.length = 1;
            lex->current++;
            lex->column++;
            lex->current_token = token;
            return token;
    }

    // Operators
    if (strchr("+-/%<>=!", *lex->current)) {
        token.type = TOKEN_OPERATOR;
        token.length = 1;

        // Check for two-character operators
        if ((lex->current[0] == '<' && lex->current[1] == '=') ||
            (lex->current[0] == '>' && lex->current[1] == '=') ||
            (lex->current[0] == '!' && lex->current[1] == '=') ||
            (lex->current[0] == '<' && lex->current[1] == '>')) {
            token.length = 2;
        }

        lex->current += token.length;
        lex->column += token.length;
        lex->current_token = token;
        return token;
    }

    // String literals
    if (*lex->current == '\'' || *lex->current == '"') {
        char quote = *lex->current;
        lex->current++;
        lex->column++;

        const char* start = lex->current;
        while (*lex->current && *lex->current != quote) {
            if (*lex->current == '\\' && lex->current[1]) {
                lex->current += 2;
                lex->column += 2;
            } else {
                lex->current++;
                lex->column++;
            }
        }

        token.type = TOKEN_STRING;
        token.text = start;
        token.length = lex->current - start;

        if (*lex->current == quote) {
            lex->current++;
            lex->column++;
        }

        lex->current_token = token;
        return token;
    }

    // Numbers
    if (isdigit(*lex->current)) {
        const char* start = lex->current;

        while (isdigit(*lex->current)) {
            lex->current++;
            lex->column++;
        }

        // Check for decimal point
        if (*lex->current == '.' && isdigit(lex->current[1])) {
            lex->current++;
            lex->column++;
            while (isdigit(*lex->current)) {
                lex->current++;
                lex->column++;
            }
        }

        token.type = TOKEN_NUMBER;
        token.text = start;
        token.length = lex->current - start;
        lex->current_token = token;
        return token;
    }

    // Identifiers and keywords
    if (isalpha(*lex->current) || *lex->current == '_') {
        const char* start = lex->current;

        while (isalnum(*lex->current) || *lex->current == '_') {
            lex->current++;
            lex->column++;
        }

        token.text = start;
        token.length = lex->current - start;

        // Check if it's a keyword
        token.type = TOKEN_IDENTIFIER;
        for (const char* kw : sql_keywords) {
            if (str_eq_ci(start, token.length, kw)) {
                token.type = TOKEN_KEYWORD;
                break;
            }
        }

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

Token lexer_peek_token(Lexer* lex) {
    // Save current state
    const char* saved_current = lex->current;
    uint32_t saved_line = lex->line;
    uint32_t saved_column = lex->column;

    // Get next token
    Token token = lexer_next_token(lex);

    // Restore state
    lex->current = saved_current;
    lex->line = saved_line;
    lex->column = saved_column;

    return token;
}

// Parser implementation
void parser_init(Parser* parser, const char* input) {
    // Initialize arena
    Arena<ParserArena>::init(64 * 1024, 1024 * 1024);  // 64KB initial, 1MB max

    // Create and initialize lexer
    parser->lexer = (Lexer*)Arena<ParserArena>::alloc(sizeof(Lexer));
    lexer_init(parser->lexer, input);

    // Initialize keywords map
    parser->keywords = (string_map<bool, ParserArena>*)Arena<ParserArena>::alloc(sizeof(string_map<bool, ParserArena>));
    stringmap_init(parser->keywords);
    init_keywords(parser->keywords);
}

void parser_reset(Parser* parser) {
    Arena<ParserArena>::reset();
}

bool consume_token(Parser* parser, TokenType type) {
    Token token = lexer_peek_token(parser->lexer);
    if (token.type == type) {
        lexer_next_token(parser->lexer);
        return true;
    }
    return false;
}

bool consume_keyword(Parser* parser, const char* keyword) {
    Token token = lexer_peek_token(parser->lexer);
    if (token.type == TOKEN_KEYWORD && str_eq_ci(token.text, token.length, keyword)) {
        lexer_next_token(parser->lexer);
        return true;
    }
    return false;
}

bool peek_keyword(Parser* parser, const char* keyword) {
    Token token = lexer_peek_token(parser->lexer);
    return token.type == TOKEN_KEYWORD && str_eq_ci(token.text, token.length, keyword);
}

// Expression parsing (recursive descent)
Expr* parse_primary_expr(Parser* parser) {
    Token token = lexer_peek_token(parser->lexer);

    if (token.type == TOKEN_NUMBER) {
        lexer_next_token(parser->lexer);
        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_LITERAL;
        expr->lit_type = TYPE_8;  // Default to BIGINT

        // Simple number parsing (could be improved)
        char* num_str = (char*)Arena<ParserArena>::alloc(token.length + 1);
        memcpy(num_str, token.text, token.length);
        num_str[token.length] = '\0';
        expr->int_val = atoll(num_str);

        return expr;
    }

    if (token.type == TOKEN_STRING) {
        lexer_next_token(parser->lexer);
        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_LITERAL;
        expr->lit_type = TYPE_256;  // VARCHAR
        expr->str_val = intern_string(token.text, token.length);
        return expr;
    }

    if (token.type == TOKEN_STAR) {
        lexer_next_token(parser->lexer);
        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_STAR;
        return expr;
    }

    if (consume_keyword(parser, "NULL")) {
        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_NULL;
        return expr;
    }

    if (consume_token(parser, TOKEN_LPAREN)) {
        Expr* expr = parse_expression(parser);
        if (!consume_token(parser, TOKEN_RPAREN)) {
            // Error: expected closing paren
            return nullptr;
        }
        return expr;
    }

    if (token.type == TOKEN_IDENTIFIER) {
        lexer_next_token(parser->lexer);

        // Check for function call
        if (consume_token(parser, TOKEN_LPAREN)) {
            Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
            expr->type = EXPR_FUNCTION;
            expr->func_name = intern_string(token.text, token.length);
            expr->args = array_create<Expr*, ParserArena>();

            // Parse arguments
            if (!consume_token(parser, TOKEN_RPAREN)) {
                do {
                    Expr* arg = parse_expression(parser);
                    if (!arg) return nullptr;
                    array_push(expr->args, arg);
                } while (consume_token(parser, TOKEN_COMMA));

                if (!consume_token(parser, TOKEN_RPAREN)) {
                    return nullptr;
                }
            }

            return expr;
        }

        // Column reference
        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_COLUMN;

        // Check for table.column
        const char* first_name = intern_string(token.text, token.length);
        if (consume_token(parser, TOKEN_DOT)) {
            token = lexer_next_token(parser->lexer);
            if (token.type != TOKEN_IDENTIFIER) {
                return nullptr;
            }
            expr->table_name = first_name;
            expr->column_name = intern_string(token.text, token.length);
        } else {
            expr->table_name = nullptr;
            expr->column_name = first_name;
        }

        return expr;
    }

    return nullptr;
}

Expr* parse_unary_expr(Parser* parser) {
    if (consume_keyword(parser, "NOT")) {
        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_UNARY_OP;
        expr->unary_op = OP_NOT;
        expr->operand = parse_unary_expr(parser);
        return expr;
    }

    Token token = lexer_peek_token(parser->lexer);
    if (token.type == TOKEN_OPERATOR && token.length == 1 && *token.text == '-') {
        lexer_next_token(parser->lexer);
        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_UNARY_OP;
        expr->unary_op = OP_NEG;
        expr->operand = parse_unary_expr(parser);
        return expr;
    }

    return parse_primary_expr(parser);
}

Expr* parse_multiplicative_expr(Parser* parser) {
    Expr* left = parse_unary_expr(parser);
    if (!left) return nullptr;

    while (true) {
        Token token = lexer_peek_token(parser->lexer);
        if (token.type != TOKEN_OPERATOR) break;

        BinaryOp op;
        if (token.length == 1 && *token.text == '*') {
            op = OP_MUL;
        } else if (token.length == 1 && *token.text == '/') {
            op = OP_DIV;
        } else if (token.length == 1 && *token.text == '%') {
            op = OP_MOD;
        } else {
            break;
        }

        lexer_next_token(parser->lexer);

        Expr* right = parse_unary_expr(parser);
        if (!right) return nullptr;

        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_BINARY_OP;
        expr->op = op;
        expr->left = left;
        expr->right = right;
        left = expr;
    }

    return left;
}

Expr* parse_additive_expr(Parser* parser) {
    Expr* left = parse_multiplicative_expr(parser);
    if (!left) return nullptr;

    while (true) {
        Token token = lexer_peek_token(parser->lexer);
        if (token.type != TOKEN_OPERATOR) break;

        BinaryOp op;
        if (token.length == 1 && *token.text == '+') {
            op = OP_ADD;
        } else if (token.length == 1 && *token.text == '-') {
            op = OP_SUB;
        } else {
            break;
        }

        lexer_next_token(parser->lexer);

        Expr* right = parse_multiplicative_expr(parser);
        if (!right) return nullptr;

        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_BINARY_OP;
        expr->op = op;
        expr->left = left;
        expr->right = right;
        left = expr;
    }

    return left;
}

Expr* parse_comparison_expr(Parser* parser) {
    Expr* left = parse_additive_expr(parser);
    if (!left) return nullptr;

    Token token = lexer_peek_token(parser->lexer);

    // Check for LIKE operator
    if (peek_keyword(parser, "LIKE")) {
        consume_keyword(parser, "LIKE");

        Expr* right = parse_additive_expr(parser);
        if (!right) return nullptr;

        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_BINARY_OP;
        expr->op = OP_LIKE;
        expr->left = left;
        expr->right = right;
        return expr;
    }

    if (token.type == TOKEN_OPERATOR) {
        BinaryOp op;

        if (token.length == 1 && *token.text == '=') {
            op = OP_EQ;
        } else if (token.length == 2 && token.text[0] == '!' && token.text[1] == '=') {
            op = OP_NE;
        } else if (token.length == 2 && token.text[0] == '<' && token.text[1] == '>') {
            op = OP_NE;
        } else if (token.length == 1 && *token.text == '<') {
            op = OP_LT;
        } else if (token.length == 2 && token.text[0] == '<' && token.text[1] == '=') {
            op = OP_LE;
        } else if (token.length == 1 && *token.text == '>') {
            op = OP_GT;
        } else if (token.length == 2 && token.text[0] == '>' && token.text[1] == '=') {
            op = OP_GE;
        } else {
            return left;
        }

        lexer_next_token(parser->lexer);

        Expr* right = parse_additive_expr(parser);
        if (!right) return nullptr;

        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_BINARY_OP;
        expr->op = op;
        expr->left = left;
        expr->right = right;
        return expr;
    }

    return left;
}

Expr* parse_and_expr(Parser* parser) {
    Expr* left = parse_comparison_expr(parser);
    if (!left) return nullptr;

    while (consume_keyword(parser, "AND")) {
        Expr* right = parse_comparison_expr(parser);
        if (!right) return nullptr;

        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_BINARY_OP;
        expr->op = OP_AND;
        expr->left = left;
        expr->right = right;
        left = expr;
    }

    return left;
}

Expr* parse_or_expr(Parser* parser) {
    Expr* left = parse_and_expr(parser);
    if (!left) return nullptr;

    while (consume_keyword(parser, "OR")) {
        Expr* right = parse_and_expr(parser);
        if (!right) return nullptr;

        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_BINARY_OP;
        expr->op = OP_OR;
        expr->left = left;
        expr->right = right;
        left = expr;
    }

    return left;
}

Expr* parse_expression(Parser* parser) {
    return parse_or_expr(parser);
}

// Parse table reference
TableRef* parse_table_ref(Parser* parser) {
    Token token = lexer_next_token(parser->lexer);
    if (token.type != TOKEN_IDENTIFIER) {
        return nullptr;
    }

    TableRef* table = (TableRef*)Arena<ParserArena>::alloc(sizeof(TableRef));
    table->table_name = intern_string(token.text, token.length);
    table->alias = nullptr;

    // Check for alias
    if (consume_keyword(parser, "AS")) {
        token = lexer_next_token(parser->lexer);
        if (token.type != TOKEN_IDENTIFIER) {
            return nullptr;
        }
        table->alias = intern_string(token.text, token.length);
    } else {
        token = lexer_peek_token(parser->lexer);
        if (token.type == TOKEN_IDENTIFIER) {
            lexer_next_token(parser->lexer);
            table->alias = intern_string(token.text, token.length);
        }
    }

    return table;
}

// Parse SELECT statement
SelectStmt* parse_select(Parser* parser) {
    if (!consume_keyword(parser, "SELECT")) {
        return nullptr;
    }

    SelectStmt* stmt = (SelectStmt*)Arena<ParserArena>::alloc(sizeof(SelectStmt));
    memset(stmt, 0, sizeof(SelectStmt));

    // Check for DISTINCT
    stmt->is_distinct = consume_keyword(parser, "DISTINCT");

    // Parse select list
    stmt->select_list = array_create<Expr*, ParserArena>();

    do {
        Expr* expr = parse_expression(parser);
        if (!expr) return nullptr;
        array_push(stmt->select_list, expr);
    } while (consume_token(parser, TOKEN_COMMA));

    // Parse FROM clause
    if (consume_keyword(parser, "FROM")) {
        stmt->from_table = parse_table_ref(parser);
        if (!stmt->from_table) return nullptr;

        // Parse JOINs
        stmt->joins = array_create<JoinClause*, ParserArena>();

        while (true) {
            JoinType join_type;

            if (consume_keyword(parser, "INNER")) {
                consume_keyword(parser, "JOIN");
                join_type = JOIN_INNER;
            } else if (consume_keyword(parser, "LEFT")) {
                consume_keyword(parser, "JOIN");
                join_type = JOIN_LEFT;
            } else if (consume_keyword(parser, "RIGHT")) {
                consume_keyword(parser, "JOIN");
                join_type = JOIN_RIGHT;
            } else if (consume_keyword(parser, "CROSS")) {
                consume_keyword(parser, "JOIN");
                join_type = JOIN_CROSS;
            } else if (consume_keyword(parser, "JOIN")) {
                join_type = JOIN_INNER;
            } else {
                break;
            }

            JoinClause* join = (JoinClause*)Arena<ParserArena>::alloc(sizeof(JoinClause));
            join->type = join_type;
            join->table = parse_table_ref(parser);
            if (!join->table) return nullptr;

            if (consume_keyword(parser, "ON")) {
                join->condition = parse_expression(parser);
                if (!join->condition) return nullptr;
            } else {
                join->condition = nullptr;
            }

            array_push(stmt->joins, join);
        }
    }

    // Parse WHERE clause
    if (consume_keyword(parser, "WHERE")) {
        stmt->where_clause = parse_expression(parser);
        if (!stmt->where_clause) return nullptr;
    }

    // Parse GROUP BY
    if (consume_keyword(parser, "GROUP")) {
        if (!consume_keyword(parser, "BY")) return nullptr;

        stmt->group_by = array_create<Expr*, ParserArena>();
        do {
            Expr* expr = parse_expression(parser);
            if (!expr) return nullptr;
            array_push(stmt->group_by, expr);
        } while (consume_token(parser, TOKEN_COMMA));

        // Parse HAVING
        if (consume_keyword(parser, "HAVING")) {
            stmt->having_clause = parse_expression(parser);
            if (!stmt->having_clause) return nullptr;
        }
    }

    // Parse ORDER BY
    if (consume_keyword(parser, "ORDER")) {
        if (!consume_keyword(parser, "BY")) return nullptr;

        stmt->order_by = array_create<OrderByClause*, ParserArena>();
        do {
            OrderByClause* order = (OrderByClause*)Arena<ParserArena>::alloc(sizeof(OrderByClause));
            order->expr = parse_expression(parser);
            if (!order->expr) return nullptr;

            if (consume_keyword(parser, "DESC")) {
                order->dir = ORDER_DESC;
            } else {
                consume_keyword(parser, "ASC");
                order->dir = ORDER_ASC;
            }

            array_push(stmt->order_by, order);
        } while (consume_token(parser, TOKEN_COMMA));
    }

    // Parse LIMIT
    stmt->limit = -1;
    if (consume_keyword(parser, "LIMIT")) {
        Token token = lexer_next_token(parser->lexer);
        if (token.type != TOKEN_NUMBER) return nullptr;

        char* num_str = (char*)Arena<ParserArena>::alloc(token.length + 1);
        memcpy(num_str, token.text, token.length);
        num_str[token.length] = '\0';
        stmt->limit = atoll(num_str);
    }

    // Parse OFFSET
    stmt->offset = 0;
    if (consume_keyword(parser, "OFFSET")) {
        Token token = lexer_next_token(parser->lexer);
        if (token.type != TOKEN_NUMBER) return nullptr;

        char* num_str = (char*)Arena<ParserArena>::alloc(token.length + 1);
        memcpy(num_str, token.text, token.length);
        num_str[token.length] = '\0';
        stmt->offset = atoll(num_str);
    }

    return stmt;
}

// Parse INSERT statement
InsertStmt* parse_insert(Parser* parser) {
    if (!consume_keyword(parser, "INSERT")) {
        return nullptr;
    }

    if (!consume_keyword(parser, "INTO")) {
        return nullptr;
    }

    Token token = lexer_next_token(parser->lexer);
    if (token.type != TOKEN_IDENTIFIER) {
        return nullptr;
    }

    InsertStmt* stmt = (InsertStmt*)Arena<ParserArena>::alloc(sizeof(InsertStmt));
    stmt->table_name = intern_string(token.text, token.length);

    // Parse column list (optional)
    stmt->columns = nullptr;
    if (consume_token(parser, TOKEN_LPAREN)) {
        stmt->columns = array_create<const char*, ParserArena>();

        do {
            token = lexer_next_token(parser->lexer);
            if (token.type != TOKEN_IDENTIFIER) return nullptr;
            array_push(stmt->columns, intern_string(token.text, token.length));
        } while (consume_token(parser, TOKEN_COMMA));

        if (!consume_token(parser, TOKEN_RPAREN)) {
            return nullptr;
        }
    }

    if (!consume_keyword(parser, "VALUES")) {
        return nullptr;
    }

    // Parse value lists
    stmt->values = array_create<array<Expr*, ParserArena>*, ParserArena>();

    do {
        if (!consume_token(parser, TOKEN_LPAREN)) {
            return nullptr;
        }

        auto* value_list = array_create<Expr*, ParserArena>();

        do {
            Expr* expr = parse_expression(parser);
            if (!expr) return nullptr;
            array_push(value_list, expr);
        } while (consume_token(parser, TOKEN_COMMA));

        if (!consume_token(parser, TOKEN_RPAREN)) {
            return nullptr;
        }

        array_push(stmt->values, value_list);
    } while (consume_token(parser, TOKEN_COMMA));

    return stmt;
}

// Parse UPDATE statement
UpdateStmt* parse_update(Parser* parser) {
    if (!consume_keyword(parser, "UPDATE")) {
        return nullptr;
    }

    Token token = lexer_next_token(parser->lexer);
    if (token.type != TOKEN_IDENTIFIER) {
        return nullptr;
    }

    UpdateStmt* stmt = (UpdateStmt*)Arena<ParserArena>::alloc(sizeof(UpdateStmt));
    stmt->table_name = intern_string(token.text, token.length);

    if (!consume_keyword(parser, "SET")) {
        return nullptr;
    }

    // Parse assignments
    stmt->columns = array_create<const char*, ParserArena>();
    stmt->values = array_create<Expr*, ParserArena>();

    do {
        token = lexer_next_token(parser->lexer);
        if (token.type != TOKEN_IDENTIFIER) return nullptr;
        array_push(stmt->columns, intern_string(token.text, token.length));

        if (!consume_token(parser, TOKEN_OPERATOR) ||
            parser->lexer->current_token.length != 1 ||
            *parser->lexer->current_token.text != '=') {
            return nullptr;
        }

        Expr* expr = parse_expression(parser);
        if (!expr) return nullptr;
        array_push(stmt->values, expr);
    } while (consume_token(parser, TOKEN_COMMA));

    // Parse WHERE clause
    stmt->where_clause = nullptr;
    if (consume_keyword(parser, "WHERE")) {
        stmt->where_clause = parse_expression(parser);
        if (!stmt->where_clause) return nullptr;
    }

    return stmt;
}

// Parse DELETE statement
DeleteStmt* parse_delete(Parser* parser) {
    if (!consume_keyword(parser, "DELETE")) {
        return nullptr;
    }

    if (!consume_keyword(parser, "FROM")) {
        return nullptr;
    }

    Token token = lexer_next_token(parser->lexer);
    if (token.type != TOKEN_IDENTIFIER) {
        return nullptr;
    }

    DeleteStmt* stmt = (DeleteStmt*)Arena<ParserArena>::alloc(sizeof(DeleteStmt));
    stmt->table_name = intern_string(token.text, token.length);

    // Parse WHERE clause
    stmt->where_clause = nullptr;
    if (consume_keyword(parser, "WHERE")) {
        stmt->where_clause = parse_expression(parser);
        if (!stmt->where_clause) return nullptr;
    }

    return stmt;
}

// Parse data type
DataType parse_data_type(Parser* parser) {
    if (consume_keyword(parser, "INT")) {
        return TYPE_4;
    }
    if (consume_keyword(parser, "BIGINT")) {
        return TYPE_8;
    }
    if (consume_keyword(parser, "VARCHAR")) {
        // Could parse length in parens here
        consume_token(parser, TOKEN_LPAREN);
        Token token = lexer_peek_token(parser->lexer);
        if (token.type == TOKEN_NUMBER) {
            lexer_next_token(parser->lexer);
            // Parse the number to determine size
            char* num_str = (char*)Arena<ParserArena>::alloc(token.length + 1);
            memcpy(num_str, token.text, token.length);
            num_str[token.length] = '\0';
            int len = atoi(num_str);
            consume_token(parser, TOKEN_RPAREN);

            if (len <= 32) return TYPE_32;
            else return TYPE_256;
        }
        consume_token(parser, TOKEN_RPAREN);
        return TYPE_256;
    }
    if (consume_keyword(parser, "TEXT")) {
        return TYPE_256;
    }

    return TYPE_256;  // Default
}

// Parse CREATE TABLE statement
CreateTableStmt* parse_create_table(Parser* parser) {
    if (!consume_keyword(parser, "CREATE")) {
        return nullptr;
    }

    if (!consume_keyword(parser, "TABLE")) {
        return nullptr;
    }

    CreateTableStmt* stmt = (CreateTableStmt*)Arena<ParserArena>::alloc(sizeof(CreateTableStmt));
    stmt->if_not_exists = false;

    // Check for IF NOT EXISTS
    if (consume_keyword(parser, "IF")) {
        if (consume_keyword(parser, "NOT") && consume_keyword(parser, "EXISTS")) {
            stmt->if_not_exists = true;
        }
    }

    Token token = lexer_next_token(parser->lexer);
    if (token.type != TOKEN_IDENTIFIER) {
        return nullptr;
    }

    stmt->table_name = intern_string(token.text, token.length);

    if (!consume_token(parser, TOKEN_LPAREN)) {
        return nullptr;
    }

    // Parse column definitions
    stmt->columns = array_create<ColumnDef*, ParserArena>();

    do {
        token = lexer_next_token(parser->lexer);
        if (token.type != TOKEN_IDENTIFIER) return nullptr;

        ColumnDef* col = (ColumnDef*)Arena<ParserArena>::alloc(sizeof(ColumnDef));
        col->name = intern_string(token.text, token.length);
        col->type = parse_data_type(parser);
        col->is_primary_key = false;
        col->is_not_null = false;

        // Parse column constraints
        while (true) {
            if (consume_keyword(parser, "PRIMARY")) {
                if (consume_keyword(parser, "KEY")) {
                    col->is_primary_key = true;
                    col->is_not_null = true;
                }
            } else if (consume_keyword(parser, "NOT")) {
                if (consume_keyword(parser, "NULL")) {
                    col->is_not_null = true;
                }
            } else {
                break;
            }
        }

        array_push(stmt->columns, col);
    } while (consume_token(parser, TOKEN_COMMA));

    if (!consume_token(parser, TOKEN_RPAREN)) {
        return nullptr;
    }

    return stmt;
}

// Parse DROP TABLE statement
DropTableStmt* parse_drop_table(Parser* parser) {
    if (!consume_keyword(parser, "DROP")) {
        return nullptr;
    }

    if (!consume_keyword(parser, "TABLE")) {
        return nullptr;
    }

    DropTableStmt* stmt = (DropTableStmt*)Arena<ParserArena>::alloc(sizeof(DropTableStmt));
    stmt->if_exists = false;

    // Check for IF EXISTS
    if (consume_keyword(parser, "IF")) {
        if (consume_keyword(parser, "EXISTS")) {
            stmt->if_exists = true;
        }
    }

    Token token = lexer_next_token(parser->lexer);
    if (token.type != TOKEN_IDENTIFIER) {
        return nullptr;
    }

    stmt->table_name = intern_string(token.text, token.length);

    return stmt;
}

// Parse transaction statements
BeginStmt* parse_begin(Parser* parser) {
    if (!consume_keyword(parser, "BEGIN")) {
        return nullptr;
    }
    return (BeginStmt*)Arena<ParserArena>::alloc(sizeof(BeginStmt));
}

CommitStmt* parse_commit(Parser* parser) {
    if (!consume_keyword(parser, "COMMIT")) {
        return nullptr;
    }
    return (CommitStmt*)Arena<ParserArena>::alloc(sizeof(CommitStmt));
}

RollbackStmt* parse_rollback(Parser* parser) {
    if (!consume_keyword(parser, "ROLLBACK")) {
        return nullptr;
    }
    return (RollbackStmt*)Arena<ParserArena>::alloc(sizeof(RollbackStmt));
}

// Main parser entry point
Statement* parser_parse_statement(Parser* parser) {
    Statement* stmt = (Statement*)Arena<ParserArena>::alloc(sizeof(Statement));

    // Try each statement type
    if (peek_keyword(parser, "SELECT")) {
        stmt->type = STMT_SELECT;
        stmt->select_stmt = parse_select(parser);
        if (!stmt->select_stmt) return nullptr;
    } else if (peek_keyword(parser, "INSERT")) {
        stmt->type = STMT_INSERT;
        stmt->insert_stmt = parse_insert(parser);
        if (!stmt->insert_stmt) return nullptr;
    } else if (peek_keyword(parser, "UPDATE")) {
        stmt->type = STMT_UPDATE;
        stmt->update_stmt = parse_update(parser);
        if (!stmt->update_stmt) return nullptr;
    } else if (peek_keyword(parser, "DELETE")) {
        stmt->type = STMT_DELETE;
        stmt->delete_stmt = parse_delete(parser);
        if (!stmt->delete_stmt) return nullptr;
    } else if (peek_keyword(parser, "CREATE")) {
        stmt->type = STMT_CREATE_TABLE;
        stmt->create_table_stmt = parse_create_table(parser);
        if (!stmt->create_table_stmt) return nullptr;
    } else if (peek_keyword(parser, "DROP")) {
        stmt->type = STMT_DROP_TABLE;
        stmt->drop_table_stmt = parse_drop_table(parser);
        if (!stmt->drop_table_stmt) return nullptr;
    } else if (peek_keyword(parser, "BEGIN")) {
        stmt->type = STMT_BEGIN;
        stmt->begin_stmt = parse_begin(parser);
        if (!stmt->begin_stmt) return nullptr;
    } else if (peek_keyword(parser, "COMMIT")) {
        stmt->type = STMT_COMMIT;
        stmt->commit_stmt = parse_commit(parser);
        if (!stmt->commit_stmt) return nullptr;
    } else if (peek_keyword(parser, "ROLLBACK")) {
        stmt->type = STMT_ROLLBACK;
        stmt->rollback_stmt = parse_rollback(parser);
        if (!stmt->rollback_stmt) return nullptr;
    } else {
        return nullptr;
    }

    // Consume optional semicolon
    consume_token(parser, TOKEN_SEMICOLON);

    return stmt;
}
