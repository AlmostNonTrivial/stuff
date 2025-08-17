// src/parser.cpp
#include "parser.hpp"
#include "arena.hpp"
#include <cstring>
#include <cctype>

static Token next_token(Parser* p);
static void advance(Parser* p);
static bool expect(Parser* p, TokenType type);
static SelectStmt* parse_select(Parser* p);
static InsertStmt* parse_insert(Parser* p);
static UpdateStmt* parse_update(Parser* p);
static DeleteStmt* parse_delete(Parser* p);
static CreateTableStmt* parse_create_table(Parser* p);
static Expr* parse_expression(Parser* p);
static Expr* parse_or_expr(Parser* p);
static Expr* parse_and_expr(Parser* p);
static Expr* parse_comparison(Parser* p);
static Expr* parse_primary(Parser* p);

void parser_init(Parser* p, const char* sql) {
    p->input = sql;
    p->pos = 0;
    p->len = strlen(sql);
    p->current = next_token(p);
    p->lookahead = next_token(p);
}

ParsedSQL* parse_sql(Parser* p) {
    ParsedSQL* result = ARENA_ALLOC(ParsedSQL);

    switch (p->current.type) {
    case TOK_SELECT:
        result->type = STMT_SELECT;
        result->stmt = parse_select(p);
        break;
    case TOK_INSERT:
        result->type = STMT_INSERT;
        result->stmt = parse_insert(p);
        break;
    case TOK_UPDATE:
        result->type = STMT_UPDATE;
        result->stmt = parse_update(p);
        break;
    case TOK_DELETE:
        result->type = STMT_DELETE;
        result->stmt = parse_delete(p);
        break;
    case TOK_CREATE:
        advance(p);
        if (p->current.type == TOK_TABLE) {
            result->type = STMT_CREATE_TABLE;
            result->stmt = parse_create_table(p);
        } else if (p->current.type == TOK_INDEX) {
            result->type = STMT_CREATE_INDEX;
            result->stmt = parse_create_index(p);
        }
        break;
    case TOK_BEGIN:
        result->type = STMT_BEGIN;
        result->stmt = nullptr;
        advance(p);
        break;
    case TOK_COMMIT:
        result->type = STMT_COMMIT;
        result->stmt = nullptr;
        advance(p);
        break;
    case TOK_ROLLBACK:
        result->type = STMT_ROLLBACK;
        result->stmt = nullptr;
        advance(p);
        break;
    }

    expect(p, TOK_SEMICOLON);
    return result;
}

static SelectStmt* parse_select(Parser* p) {
    SelectStmt* stmt = ARENA_ALLOC(SelectStmt);
    memset(stmt, 0, sizeof(SelectStmt));

    expect(p, TOK_SELECT);

    // Check for aggregate or columns
    if (is_aggregate(p->current.type)) {
        stmt->aggregate = p->current.start;
        advance(p);
        expect(p, TOK_LPAREN);
        expect(p, TOK_STAR);
        expect(p, TOK_RPAREN);
    } else if (p->current.type == TOK_STAR) {
        advance(p);
        stmt->columns = nullptr;  // SELECT *
    } else {
        // Parse column list
        uint32_t capacity = 8;
        stmt->columns = ARENA_ALLOC_ARRAY(ColumnRef, capacity);
        stmt->column_count = 0;

        do {
            if (stmt->column_count >= capacity) {
                // In real code, would need to handle reallocation
                break;
            }

            if (p->current.type != TOK_IDENTIFIER) break;

            stmt->columns[stmt->column_count].name = p->current.start;
            stmt->columns[stmt->column_count].name_len = p->current.length;
            stmt->column_count++;
            advance(p);

            if (p->current.type == TOK_COMMA) {
                advance(p);
            } else {
                break;
            }
        } while (true);
    }

    expect(p, TOK_FROM);

    if (p->current.type != TOK_IDENTIFIER) {
        return nullptr;  // Error
    }

    stmt->table = p->current.start;
    stmt->table_len = p->current.length;
    advance(p);

    // Optional WHERE
    if (p->current.type == TOK_WHERE) {
        advance(p);
        stmt->where = parse_expression(p);
    }

    // Optional ORDER BY
    if (p->current.type == TOK_ORDER) {
        advance(p);
        expect(p, TOK_BY);

        if (p->current.type != TOK_IDENTIFIER) {
            return nullptr;
        }

        stmt->order_by_column = p->current.start;
        stmt->order_by_len = p->current.length;
        advance(p);

        stmt->order_asc = true;
        if (p->current.type == TOK_DESC) {
            stmt->order_asc = false;
            advance(p);
        } else if (p->current.type == TOK_ASC) {
            advance(p);
        }
    }

    return stmt;
}

static Expr* parse_primary(Parser* p) {
    Expr* expr = ARENA_ALLOC(Expr);

    switch (p->current.type) {
    case TOK_IDENTIFIER:
        expr->type = EXPR_COLUMN;
        expr->column.name = p->current.start;
        expr->column.name_len = p->current.length;
        advance(p);
        break;

    case TOK_INTEGER:
        expr->type = EXPR_LITERAL_INT;
        expr->int_lit.value = p->current.int_value;
        advance(p);
        break;

    case TOK_STRING:
        expr->type = EXPR_LITERAL_STRING;
        expr->str_lit.value = p->current.start;
        expr->str_lit.len = p->current.length;
        advance(p);
        break;

    case TOK_LPAREN:
        advance(p);
        expr = parse_expression(p);
        expect(p, TOK_RPAREN);
        break;

    default:
        return nullptr;
    }

    return expr;
}

static Expr* parse_comparison(Parser* p) {
    Expr* left = parse_primary(p);
    if (!left) return nullptr;

    BinaryOp op;
    bool is_comparison = true;

    switch (p->current.type) {
    case TOK_EQ: op = OP_EQ; break;
    case TOK_NE: op = OP_NE; break;
    case TOK_LT: op = OP_LT; break;
    case TOK_LE: op = OP_LE; break;
    case TOK_GT: op = OP_GT; break;
    case TOK_GE: op = OP_GE; break;
    default:
        is_comparison = false;
    }

    if (!is_comparison) {
        return left;
    }

    advance(p);
    Expr* right = parse_primary(p);
    if (!right) return nullptr;

    Expr* result = ARENA_ALLOC(Expr);
    result->type = EXPR_BINARY_OP;
    result->binary.op = op;
    result->binary.left = left;
    result->binary.right = right;

    return result;
}

// Helper to skip whitespace
static void skip_whitespace(Parser* p) {
    while (p->pos < p->len && isspace(p->input[p->pos])) {
        p->pos++;
    }
}

static Token next_token(Parser* p) {
    skip_whitespace(p);

    if (p->pos >= p->len) {
        return {TOK_EOF, nullptr, 0, 0};
    }

    Token tok;
    tok.start = &p->input[p->pos];
    tok.int_value = 0;

    // Handle operators and single chars
    char ch = p->input[p->pos];
    char next_ch = (p->pos + 1 < p->len) ? p->input[p->pos + 1] : '\0';

    if (ch == '=' && next_ch == '=') {
        tok.type = TOK_EQ;
        tok.length = 2;
        p->pos += 2;
        return tok;
    }

    // ... handle other operators ...

    // Handle identifiers and keywords
    if (isalpha(ch) || ch == '_') {
        uint32_t start = p->pos;
        while (p->pos < p->len && (isalnum(p->input[p->pos]) || p->input[p->pos] == '_')) {
            p->pos++;
        }

        tok.length = p->pos - start;

        // Check keywords (would use a table in real code)
        if (strncasecmp(tok.start, "SELECT", tok.length) == 0) {
            tok.type = TOK_SELECT;
        } else if (strncasecmp(tok.start, "FROM", tok.length) == 0) {
            tok.type = TOK_FROM;
        } else {
            tok.type = TOK_IDENTIFIER;
        }

        return tok;
    }

    // Handle numbers
    if (isdigit(ch)) {
        int64_t value = 0;
        uint32_t start = p->pos;
        while (p->pos < p->len && isdigit(p->input[p->pos])) {
            value = value * 10 + (p->input[p->pos] - '0');
            p->pos++;
        }
        tok.type = TOK_INTEGER;
        tok.length = p->pos - start;
        tok.int_value = value;
        return tok;
    }

    // ... handle strings ...

    return tok;
}
