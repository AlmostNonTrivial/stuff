#include "parser.hpp"
#include "arena.hpp"
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// SQL Keywords - keep it simple
static const char* sql_keywords[] = {
    "SELECT", "FROM", "WHERE", "INSERT", "INTO", "VALUES", "UPDATE", "SET",
    "DELETE", "CREATE", "TABLE", "DROP", "BEGIN", "COMMIT", "ROLLBACK",
    "JOIN", "INNER", "LEFT", "RIGHT", "CROSS", "ON", "AND", "OR", "NOT",
    "NULL", "DISTINCT", "AS", "ORDER", "BY", "GROUP", "HAVING", "LIMIT",
    "OFFSET", "ASC", "DESC", "IF", "EXISTS", "PRIMARY", "KEY", "INT",
    "BIGINT", "VARCHAR", "TEXT", "LIKE", "IN", "BETWEEN", "IS", "TRUE",
    "FALSE", "COUNT", "SUM", "AVG", "MIN", "MAX", "NOT"
};

// String interning
const char* intern_string(const char* str, uint32_t length) {
    char* interned = (char*)Arena<ParserArena>::alloc(length + 1);
    memcpy(interned, str, length);
    interned[length] = '\0';
    return interned;
}

// Case-insensitive comparison
static bool str_eq_ci(const char* a, uint32_t a_len, const char* b) {
    size_t b_len = strlen(b);
    if (a_len != b_len) return false;

    for (uint32_t i = 0; i < a_len; i++) {
        if (toupper(a[i]) != toupper(b[i])) return false;
    }
    return true;
}

// Check if identifier is a keyword
static bool is_keyword(const char* text, uint32_t length) {
    for (const char* kw : sql_keywords) {
        if (str_eq_ci(text, length, kw)) return true;
    }
    return false;
}

//=============================================================================
// LEXER - Keep it simple and correct
//=============================================================================

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
            // SQL comment
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
    char c = *lex->current;
    switch (c) {
        case '(': token.type = TOKEN_LPAREN; token.length = 1; break;
        case ')': token.type = TOKEN_RPAREN; token.length = 1; break;
        case ',': token.type = TOKEN_COMMA; token.length = 1; break;
        case ';': token.type = TOKEN_SEMICOLON; token.length = 1; break;
        case '.': token.type = TOKEN_DOT; token.length = 1; break;
        case '*': case '+': case '-': case '/': case '%':
            token.type = TOKEN_OPERATOR;
            token.length = 1;
            break;
        case '=':
            token.type = TOKEN_OPERATOR;
            token.length = 1;
            break;
        case '!':
            if (lex->current[1] == '=') {
                token.type = TOKEN_OPERATOR;
                token.length = 2;
            } else {
                token.type = TOKEN_OPERATOR;
                token.length = 1;
            }
            break;
        case '<':
            if (lex->current[1] == '=' || lex->current[1] == '>') {
                token.type = TOKEN_OPERATOR;
                token.length = 2;
            } else {
                token.type = TOKEN_OPERATOR;
                token.length = 1;
            }
            break;
        case '>':
            if (lex->current[1] == '=') {
                token.type = TOKEN_OPERATOR;
                token.length = 2;
            } else {
                token.type = TOKEN_OPERATOR;
                token.length = 1;
            }
            break;
        default:
            // Not a single-char token, continue below
            token.length = 0;
            break;
    }

    if (token.length > 0) {
        lex->current += token.length;
        lex->column += token.length;
        lex->current_token = token;
        return token;
    }

    // String literals
    if (c == '\'' || c == '"') {
        char quote = c;
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
    if (isdigit(c) || (c == '.' && isdigit(lex->current[1]))) {
        const char* start = lex->current;

        while (isdigit(*lex->current)) {
            lex->current++;
            lex->column++;
        }

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
    if (isalpha(c) || c == '_') {
        const char* start = lex->current;

        while (isalnum(*lex->current) || *lex->current == '_') {
            lex->current++;
            lex->column++;
        }

        token.text = start;
        token.length = lex->current - start;
        token.type = is_keyword(start, token.length) ? TOKEN_KEYWORD : TOKEN_IDENTIFIER;

        lex->current_token = token;
        return token;
    }

    // Unknown
    token.type = TOKEN_EOF;
    token.length = 1;
    lex->current++;
    lex->column++;
    lex->current_token = token;
    return token;
}

Token lexer_peek_token(Lexer* lex) {
    const char* saved_current = lex->current;
    uint32_t saved_line = lex->line;
    uint32_t saved_column = lex->column;
    Token saved_token = lex->current_token;

    Token token = lexer_next_token(lex);

    lex->current = saved_current;
    lex->line = saved_line;
    lex->column = saved_column;
    lex->current_token = saved_token;

    return token;
}

//=============================================================================
// PARSER
//=============================================================================

void parser_init(Parser* parser, const char* input) {
    Arena<ParserArena>::init(64 * 1024, 1024 * 1024);

    parser->lexer = (Lexer*)Arena<ParserArena>::alloc(sizeof(Lexer));
    lexer_init(parser->lexer, input);

    parser->keywords = nullptr; // Not really needed with our simpler approach
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

bool consume_operator(Parser* parser, const char* op) {
    Token token = lexer_peek_token(parser->lexer);
    if (token.type == TOKEN_OPERATOR &&
        token.length == strlen(op) &&
        memcmp(token.text, op, token.length) == 0) {
        lexer_next_token(parser->lexer);
        return true;
    }
    return false;
}

bool peek_operator(Parser* parser) {
    Token token = lexer_peek_token(parser->lexer);
    return token.type == TOKEN_OPERATOR;
}

//=============================================================================
// EXPRESSION PARSING - Simple precedence climbing
//=============================================================================

// Forward declarations
Expr* parse_expression(Parser* parser);
Expr* parse_binary_expr(Parser* parser, Expr* left, int min_prec);

// Get operator precedence (higher = tighter binding)
static int get_precedence(BinaryOp op) {
    switch (op) {
        case OP_OR:  return 1;
        case OP_AND: return 2;
        case OP_EQ: case OP_NE: case OP_LT:
        case OP_LE: case OP_GT: case OP_GE:
        case OP_LIKE: case OP_IN: return 3;
        case OP_ADD: case OP_SUB: return 4;
        case OP_MUL: case OP_DIV: case OP_MOD: return 5;
        default: return 0;
    }
}

// Peek at binary operator without consuming
static BinaryOp peek_binary_op(Parser* parser) {
    Token token = lexer_peek_token(parser->lexer);

    // Check keywords first
    if (token.type == TOKEN_KEYWORD) {
        if (str_eq_ci(token.text, token.length, "AND")) return OP_AND;
        if (str_eq_ci(token.text, token.length, "OR")) return OP_OR;
        if (str_eq_ci(token.text, token.length, "LIKE")) return OP_LIKE;
        if (str_eq_ci(token.text, token.length, "IN")) return OP_IN;
        return (BinaryOp)-1;
    }

    if (token.type == TOKEN_OPERATOR) {
        if (token.length == 1) {
            switch (*token.text) {
                case '=': return OP_EQ;
                case '<': return OP_LT;
                case '>': return OP_GT;
                case '+': return OP_ADD;
                case '-': return OP_SUB;
                case '*': return OP_MUL;
                case '/': return OP_DIV;
                case '%': return OP_MOD;
            }
        } else if (token.length == 2) {
            if (memcmp(token.text, "!=", 2) == 0) return OP_NE;
            if (memcmp(token.text, "<>", 2) == 0) return OP_NE;
            if (memcmp(token.text, "<=", 2) == 0) return OP_LE;
            if (memcmp(token.text, ">=", 2) == 0) return OP_GE;
        }
    }

    return (BinaryOp)-1;
}

// Consume binary operator token
static void consume_binary_op(Parser* parser, BinaryOp op) {
    Token token = lexer_peek_token(parser->lexer);

    if (token.type == TOKEN_KEYWORD) {
        if ((op == OP_AND && str_eq_ci(token.text, token.length, "AND")) ||
            (op == OP_OR && str_eq_ci(token.text, token.length, "OR")) ||
            (op == OP_LIKE && str_eq_ci(token.text, token.length, "LIKE")) ||
            (op == OP_IN && str_eq_ci(token.text, token.length, "IN"))) {
            lexer_next_token(parser->lexer);
        }
    } else if (token.type == TOKEN_OPERATOR) {
        lexer_next_token(parser->lexer);
    }
}

// Parse primary expression (literals, identifiers, parentheses)
// Special handling: * is EXPR_STAR only when standalone in select list or COUNT(*)
Expr* parse_primary_expr(Parser* parser) {
    Token token = lexer_peek_token(parser->lexer);

    // NULL literal
    if (consume_keyword(parser, "NULL")) {
        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_NULL;
        return expr;
    }

    // Number literal
    if (token.type == TOKEN_NUMBER) {
        lexer_next_token(parser->lexer);
        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_LITERAL;
        expr->lit_type = TYPE_8;

        char* num_str = (char*)Arena<ParserArena>::alloc(token.length + 1);
        memcpy(num_str, token.text, token.length);
        num_str[token.length] = '\0';

        // Check for decimal point
        if (strchr(num_str, '.')) {
            expr->float_val = atof(num_str);
        } else {
            expr->int_val = atoll(num_str);
        }

        return expr;
    }

    // String literal
    if (token.type == TOKEN_STRING) {
        lexer_next_token(parser->lexer);
        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_LITERAL;
        expr->lit_type = TYPE_256;
        expr->str_val = intern_string(token.text, token.length);
        return expr;
    }

    // Parenthesized expression
    // In parse_primary_expr, modify the parentheses case:
    if (consume_token(parser, TOKEN_LPAREN)) {
        // Peek ahead to see if this is a subquery
        if (peek_keyword(parser, "SELECT")) {
            Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
            expr->type = EXPR_SUBQUERY;
            expr->subquery = parse_select(parser);
            if (!expr->subquery) return nullptr;

            if (!consume_token(parser, TOKEN_RPAREN)) {
                return nullptr; // Error: missing closing paren
            }
            return expr;
        }

        // Otherwise normal parenthesized expression
        Expr* expr = parse_expression(parser);
        if (!consume_token(parser, TOKEN_RPAREN)) {
            return nullptr;
        }
        return expr;
    }

    // NOT operator - fixed to handle comparison expressions properly
    if (consume_keyword(parser, "NOT")) {
        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_UNARY_OP;
        expr->unary_op = OP_NOT;

        // Parse the operand - should be able to include comparisons but not AND/OR
        // Start with a primary expression
        Expr* operand = parse_primary_expr(parser);
        if (!operand) return nullptr;

        // Continue parsing binary operators with precedence > AND (i.e., >= 3)
        // This allows NOT to capture comparison expressions but not AND/OR
        operand = parse_binary_expr(parser, operand, 3);

        expr->operand = operand;
        return expr;
    }

    // Unary minus
    if (token.type == TOKEN_OPERATOR && token.length == 1 && *token.text == '-') {
        lexer_next_token(parser->lexer);
        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_UNARY_OP;
        expr->unary_op = OP_NEG;
        expr->operand = parse_primary_expr(parser);
        return expr;
    }

    // Check for aggregate function keywords that can be used as functions
    bool is_function_keyword = false;
    if (token.type == TOKEN_KEYWORD) {
        if (str_eq_ci(token.text, token.length, "COUNT") ||
            str_eq_ci(token.text, token.length, "SUM") ||
            str_eq_ci(token.text, token.length, "AVG") ||
            str_eq_ci(token.text, token.length, "MIN") ||
            str_eq_ci(token.text, token.length, "MAX")) {
            is_function_keyword = true;
        }
    }

    // Identifier or function keyword (column reference or function call)
    if (token.type == TOKEN_IDENTIFIER || is_function_keyword) {
        lexer_next_token(parser->lexer);
        const char* first_name = intern_string(token.text, token.length);

        // Check for function call
        if (consume_token(parser, TOKEN_LPAREN)) {
            Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
            expr->type = EXPR_FUNCTION;
            expr->func_name = first_name;
            expr->args = array_create<Expr*, ParserArena>();

            if (!consume_token(parser, TOKEN_RPAREN)) {
                do {
                    // Special case for * in COUNT(*)
                    Token next = lexer_peek_token(parser->lexer);
                    if (next.type == TOKEN_OPERATOR && next.length == 1 && *next.text == '*') {
                        lexer_next_token(parser->lexer);
                        Expr* star = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
                        star->type = EXPR_STAR;
                        array_push(expr->args, star);
                    } else {
                        Expr* arg = parse_expression(parser);
                        if (!arg) return nullptr;
                        array_push(expr->args, arg);
                    }
                } while (consume_token(parser, TOKEN_COMMA));

                if (!consume_token(parser, TOKEN_RPAREN)) {
                    return nullptr;
                }
            }

            return expr;
        }

        // Column reference (possibly with table prefix)
        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_COLUMN;

        if (consume_token(parser, TOKEN_DOT)) {
            token = lexer_next_token(parser->lexer);
            if (token.type == TOKEN_IDENTIFIER) {
                expr->table_name = first_name;
                expr->column_name = intern_string(token.text, token.length);
            } else if (token.type == TOKEN_OPERATOR && token.length == 1 && *token.text == '*') {
                // Handle table.*
                expr->table_name = first_name;
                expr->column_name = intern_string("*", 1);
            } else {
                return nullptr;
            }
        } else {
            expr->table_name = nullptr;
            expr->column_name = first_name;
        }

        return expr;
    }

    // Special case: standalone * (for SELECT * or in special contexts)
    // This should only happen when * is not being used as multiplication
    if (token.type == TOKEN_OPERATOR && token.length == 1 && *token.text == '*') {
        lexer_next_token(parser->lexer);
        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_STAR;
        return expr;
    }

    return nullptr;
}

// Parse binary expression with precedence climbing
Expr* parse_binary_expr(Parser* parser, Expr* left, int min_prec) {
    while (true) {
        // Peek at next operator
        BinaryOp op = peek_binary_op(parser);
        if (op == (BinaryOp)-1) break;

        int prec = get_precedence(op);
        if (prec < min_prec) break;

        // Consume the operator
        consume_binary_op(parser, op);

        Expr* right = nullptr;

        // Special handling for IN - expect a parenthesized list
        // In parse_binary_expr, modify the IN handling:
        if (op == OP_IN) {
            if (!consume_token(parser, TOKEN_LPAREN)) {
                return nullptr;
            }

            // Check if it's a subquery
            if (peek_keyword(parser, "SELECT")) {
                right = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
                right->type = EXPR_SUBQUERY;
                right->subquery = parse_select(parser);
                if (!right->subquery) return nullptr;
            } else {
                // Existing list handling
                right = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
                right->type = EXPR_LIST;
                right->list_items = array_create<Expr*, ParserArena>();

                do {
                    Expr* item = parse_expression(parser);
                    if (!item) return nullptr;
                    array_push(right->list_items, item);
                } while (consume_token(parser, TOKEN_COMMA));
            }

            if (!consume_token(parser, TOKEN_RPAREN)) {
                return nullptr;

        }
        } else {
            // Normal binary operator handling
            right = parse_primary_expr(parser);
            if (!right) return nullptr;

            // While next operator has higher precedence, accumulate it into right side
            while (true) {
                BinaryOp next_op = peek_binary_op(parser);
                if (next_op == (BinaryOp)-1) break;

                int next_prec = get_precedence(next_op);

                if (next_prec > prec) {
                    right = parse_binary_expr(parser, right, next_prec);
                    if (!right) return nullptr;
                } else {
                    break;
                }
            }
        }

        // Create binary op node
        Expr* expr = (Expr*)Arena<ParserArena>::alloc(sizeof(Expr));
        expr->type = EXPR_BINARY_OP;
        expr->op = op;
        expr->left = left;
        expr->right = right;
        left = expr;
    }

    return left;
}

// Main expression parser entry point
Expr* parse_expression(Parser* parser) {
    Expr* left = parse_primary_expr(parser);
    if (!left) return nullptr;

    return parse_binary_expr(parser, left, 0);
}

//=============================================================================
// STATEMENT PARSING
//=============================================================================

TableRef* parse_table_ref(Parser* parser) {
    Token token = lexer_next_token(parser->lexer);
    if (token.type != TOKEN_IDENTIFIER) {
        return nullptr;
    }

    TableRef* table = (TableRef*)Arena<ParserArena>::alloc(sizeof(TableRef));
    table->table_name = intern_string(token.text, token.length);
    table->alias = nullptr;

    // Check for alias (AS keyword is optional)
    if (consume_keyword(parser, "AS")) {
        token = lexer_next_token(parser->lexer);
        if (token.type != TOKEN_IDENTIFIER) {
            return nullptr;
        }
        table->alias = intern_string(token.text, token.length);
    } else {
        token = lexer_peek_token(parser->lexer);
        if (token.type == TOKEN_IDENTIFIER &&
            !is_keyword(token.text, token.length)) {
            lexer_next_token(parser->lexer);
            table->alias = intern_string(token.text, token.length);
        }
    }

    return table;
}

SelectStmt* parse_select(Parser* parser) {
    if (!consume_keyword(parser, "SELECT")) {
        return nullptr;
    }

    SelectStmt* stmt = (SelectStmt*)Arena<ParserArena>::alloc(sizeof(SelectStmt));
    memset(stmt, 0, sizeof(SelectStmt));

    // DISTINCT
    stmt->is_distinct = consume_keyword(parser, "DISTINCT");

    // Select list
    stmt->select_list = array_create<Expr*, ParserArena>();
    do {
        Expr* expr = parse_expression(parser);
        if (!expr) return nullptr;
        array_push(stmt->select_list, expr);
    } while (consume_token(parser, TOKEN_COMMA));

    // FROM clause
    if (consume_keyword(parser, "FROM")) {
        stmt->from_table = parse_table_ref(parser);
        if (!stmt->from_table) return nullptr;

        // JOINs
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

    // WHERE clause
    if (consume_keyword(parser, "WHERE")) {
        stmt->where_clause = parse_expression(parser);
        if (!stmt->where_clause) return nullptr;
    }

    // GROUP BY
    if (consume_keyword(parser, "GROUP")) {
        if (!consume_keyword(parser, "BY")) return nullptr;

        stmt->group_by = array_create<Expr*, ParserArena>();
        do {
            Expr* expr = parse_expression(parser);
            if (!expr) return nullptr;
            array_push(stmt->group_by, expr);
        } while (consume_token(parser, TOKEN_COMMA));

        // HAVING
        if (consume_keyword(parser, "HAVING")) {
            stmt->having_clause = parse_expression(parser);
            if (!stmt->having_clause) return nullptr;
        }
    }

    // ORDER BY
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

    // LIMIT
    stmt->limit = -1;
    if (consume_keyword(parser, "LIMIT")) {
        Token token = lexer_next_token(parser->lexer);
        if (token.type != TOKEN_NUMBER) return nullptr;

        char* num_str = (char*)Arena<ParserArena>::alloc(token.length + 1);
        memcpy(num_str, token.text, token.length);
        num_str[token.length] = '\0';
        stmt->limit = atoll(num_str);
    }

    // OFFSET
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

    // Optional column list
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

    // Value lists
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

    // Parse SET assignments
    stmt->columns = array_create<const char*, ParserArena>();
    stmt->values = array_create<Expr*, ParserArena>();

    do {
        token = lexer_next_token(parser->lexer);
        if (token.type != TOKEN_IDENTIFIER) return nullptr;
        array_push(stmt->columns, intern_string(token.text, token.length));

        // Must have '=' operator
        if (!consume_operator(parser, "=")) {
            return nullptr;
        }

        Expr* expr = parse_expression(parser);
        if (!expr) return nullptr;
        array_push(stmt->values, expr);
    } while (consume_token(parser, TOKEN_COMMA));

    // WHERE clause
    stmt->where_clause = nullptr;
    if (consume_keyword(parser, "WHERE")) {
        stmt->where_clause = parse_expression(parser);
        if (!stmt->where_clause) return nullptr;
    }

    return stmt;
}

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

    // WHERE clause
    stmt->where_clause = nullptr;
    if (consume_keyword(parser, "WHERE")) {
        stmt->where_clause = parse_expression(parser);
        if (!stmt->where_clause) return nullptr;
    }

    return stmt;
}

DataType parse_data_type(Parser* parser) {
    if (consume_keyword(parser, "INT")) {
        return TYPE_4;
    }
    if (consume_keyword(parser, "BIGINT")) {
        return TYPE_8;
    }
    if (consume_keyword(parser, "VARCHAR")) {
        if (consume_token(parser, TOKEN_LPAREN)) {
            Token token = lexer_peek_token(parser->lexer);
            if (token.type == TOKEN_NUMBER) {
                lexer_next_token(parser->lexer);
                char* num_str = (char*)Arena<ParserArena>::alloc(token.length + 1);
                memcpy(num_str, token.text, token.length);
                num_str[token.length] = '\0';
                int len = atoi(num_str);
                consume_token(parser, TOKEN_RPAREN);

                if (len <= 32) return TYPE_32;
                else return TYPE_256;
            }
            consume_token(parser, TOKEN_RPAREN);
        }
        return TYPE_256;
    }
    if (consume_keyword(parser, "TEXT")) {
        return TYPE_256;
    }

    return TYPE_256;
}
CreateIndexStmt* parse_create_index(Parser* parser) {
    if (!consume_keyword(parser, "CREATE")) {
        return nullptr;
    }

    CreateIndexStmt* stmt = (CreateIndexStmt*)Arena<ParserArena>::alloc(sizeof(CreateIndexStmt));
    stmt->is_unique = false;
    stmt->if_not_exists = false;

    // Check for UNIQUE
    if (consume_keyword(parser, "UNIQUE")) {
        stmt->is_unique = true;
    }

    if (!consume_keyword(parser, "INDEX")) {
        return nullptr;
    }

    // Check for IF NOT EXISTS
    if (consume_keyword(parser, "IF")) {
        if (consume_keyword(parser, "NOT") && consume_keyword(parser, "EXISTS")) {
            stmt->if_not_exists = true;
        }
    }

    // Index name
    Token token = lexer_next_token(parser->lexer);
    if (token.type != TOKEN_IDENTIFIER) {
        return nullptr;
    }
    stmt->index_name = intern_string(token.text, token.length);

    // ON keyword
    if (!consume_keyword(parser, "ON")) {
        return nullptr;
    }

    // Table name
    token = lexer_next_token(parser->lexer);
    if (token.type != TOKEN_IDENTIFIER) {
        return nullptr;
    }
    stmt->table_name = intern_string(token.text, token.length);

    // Column list in parentheses
    if (!consume_token(parser, TOKEN_LPAREN)) {
        return nullptr;
    }

    stmt->columns = array_create<const char*, ParserArena>();
    do {
        token = lexer_next_token(parser->lexer);
        if (token.type != TOKEN_IDENTIFIER) {
            return nullptr;
        }
        array_push(stmt->columns, intern_string(token.text, token.length));
    } while (consume_token(parser, TOKEN_COMMA));

    if (!consume_token(parser, TOKEN_RPAREN)) {
        return nullptr;
    }

    return stmt;
}



CreateTableStmt* parse_create_table(Parser* parser) {
    if (!consume_keyword(parser, "CREATE")) {
        return nullptr;
    }

    if (!consume_keyword(parser, "TABLE")) {
        return nullptr;
    }

    CreateTableStmt* stmt = (CreateTableStmt*)Arena<ParserArena>::alloc(sizeof(CreateTableStmt));
    stmt->if_not_exists = false;

    // IF NOT EXISTS
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

    // Column definitions
    stmt->columns = array_create<ColumnDef*, ParserArena>();

    do {
        token = lexer_next_token(parser->lexer);
        if (token.type != TOKEN_IDENTIFIER) return nullptr;

        ColumnDef* col = (ColumnDef*)Arena<ParserArena>::alloc(sizeof(ColumnDef));
        col->name = intern_string(token.text, token.length);
        col->type = parse_data_type(parser);
        col->is_primary_key = false;
        col->is_not_null = false;

        // Column constraints
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


DropIndexStmt* parse_drop_index(Parser* parser) {
    if (!consume_keyword(parser, "DROP")) {
        return nullptr;
    }

    if (!consume_keyword(parser, "INDEX")) {
        return nullptr;
    }

    DropIndexStmt* stmt = (DropIndexStmt*)Arena<ParserArena>::alloc(sizeof(DropIndexStmt));
    stmt->if_exists = false;
    stmt->table_name = nullptr;

    // Check for IF EXISTS
    if (consume_keyword(parser, "IF")) {
        if (consume_keyword(parser, "EXISTS")) {
            stmt->if_exists = true;
        }
    }

    // Index name
    Token token = lexer_next_token(parser->lexer);
    if (token.type != TOKEN_IDENTIFIER) {
        return nullptr;
    }
    stmt->index_name = intern_string(token.text, token.length);

    // Optional ON table_name (some SQL dialects support this)
    if (consume_keyword(parser, "ON")) {
        token = lexer_next_token(parser->lexer);
        if (token.type != TOKEN_IDENTIFIER) {
            return nullptr;
        }
        stmt->table_name = intern_string(token.text, token.length);
    }

    return stmt;
}


DropTableStmt* parse_drop_table(Parser* parser) {
    if (!consume_keyword(parser, "DROP")) {
        return nullptr;
    }

    if (!consume_keyword(parser, "TABLE")) {
        return nullptr;
    }

    DropTableStmt* stmt = (DropTableStmt*)Arena<ParserArena>::alloc(sizeof(DropTableStmt));
    stmt->if_exists = false;

    // IF EXISTS
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

// Transaction statements
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
    }  else if (peek_keyword(parser, "CREATE")) {
           // Need to look ahead to see if it's TABLE or INDEX
           Token saved_current = parser->lexer->current_token;
           const char* saved_pos = parser->lexer->current;
           uint32_t saved_line = parser->lexer->line;
           uint32_t saved_col = parser->lexer->column;

           consume_keyword(parser, "CREATE");

           // Check for UNIQUE INDEX
           bool is_index = false;
           if (peek_keyword(parser, "UNIQUE")) {
               consume_keyword(parser, "UNIQUE");
               if (peek_keyword(parser, "INDEX")) {
                   is_index = true;
               }
           } else if (peek_keyword(parser, "INDEX")) {
               is_index = true;
           }

           // Restore position
           parser->lexer->current_token = saved_current;
           parser->lexer->current = saved_pos;
           parser->lexer->line = saved_line;
           parser->lexer->column = saved_col;

           if (is_index) {
               stmt->type = STMT_CREATE_INDEX;
               stmt->create_index_stmt = parse_create_index(parser);
               if (!stmt->create_index_stmt) return nullptr;
           } else {
               stmt->type = STMT_CREATE_TABLE;
               stmt->create_table_stmt = parse_create_table(parser);
               if (!stmt->create_table_stmt) return nullptr;
           }
       }  else if (peek_keyword(parser, "CREATE")) {
              // Need to look ahead to see if it's TABLE or INDEX
              Token saved_current = parser->lexer->current_token;
              const char* saved_pos = parser->lexer->current;
              uint32_t saved_line = parser->lexer->line;
              uint32_t saved_col = parser->lexer->column;
              
              consume_keyword(parser, "CREATE");
              
              // Check for UNIQUE INDEX
              bool is_index = false;
              if (peek_keyword(parser, "UNIQUE")) {
                  consume_keyword(parser, "UNIQUE");
                  if (peek_keyword(parser, "INDEX")) {
                      is_index = true;
                  }
              } else if (peek_keyword(parser, "INDEX")) {
                  is_index = true;
              }
              
              // Restore position
              parser->lexer->current_token = saved_current;
              parser->lexer->current = saved_pos;
              parser->lexer->line = saved_line;
              parser->lexer->column = saved_col;
              
              if (is_index) {
                  stmt->type = STMT_CREATE_INDEX;
                  stmt->create_index_stmt = parse_create_index(parser);
                  if (!stmt->create_index_stmt) return nullptr;
              } else {
                  stmt->type = STMT_CREATE_TABLE;
                  stmt->create_table_stmt = parse_create_table(parser);
                  if (!stmt->create_table_stmt) return nullptr;
              }
          }    else if (peek_keyword(parser, "DROP")) {
        // Look ahead to see if it's TABLE or INDEX
        Token saved_current = parser->lexer->current_token;
        const char* saved_pos = parser->lexer->current;
        uint32_t saved_line = parser->lexer->line;
        uint32_t saved_col = parser->lexer->column;

        consume_keyword(parser, "DROP");

        bool is_index = false;
        if (peek_keyword(parser, "INDEX")) {
            is_index = true;
        }

        // Restore position
        parser->lexer->current_token = saved_current;
        parser->lexer->current = saved_pos;
        parser->lexer->line = saved_line;
        parser->lexer->column = saved_col;

        if (is_index) {
            stmt->type = STMT_DROP_INDEX;
            stmt->drop_index_stmt = parse_drop_index(parser);
            if (!stmt->drop_index_stmt) return nullptr;
        } else {
            stmt->type = STMT_DROP_TABLE;
            stmt->drop_table_stmt = parse_drop_table(parser);
            if (!stmt->drop_table_stmt) return nullptr;
        }
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

array<Statement*, ParserArena>* parser_parse_statements(Parser* parser) {
    auto* statements = array_create<Statement*, ParserArena>();
    while (true) {
        // Skip whitespace and check for EOF
        skip_whitespace(parser->lexer);
        if (lexer_peek_token(parser->lexer).type == TOKEN_EOF) {
            break;
        }

        Statement* stmt = parser_parse_statement(parser);
        if (!stmt) {
            // Optionally handle error (e.g., return nullptr or empty array)
            array_clear(statements);
            return statements;
        }
        array_push(statements, stmt);
    }
    return statements;
}
