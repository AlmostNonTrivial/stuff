#include "parser.hpp"
#include "arena.hpp"
#include "vm.hpp"
#include <algorithm>
#include <cstring>
#include <cctype>
#include <unordered_map>


// TODO, what should I do about this?
// Global keyword map - initialized once
static std::unordered_map<std::string, TokenType> keyword_map = {
        {"SELECT", TOK_SELECT}, {"FROM", TOK_FROM}, {"WHERE", TOK_WHERE},
        {"ORDER", TOK_ORDER}, {"BY", TOK_BY}, {"INSERT", TOK_INSERT},
        {"INTO", TOK_INTO}, {"VALUES", TOK_VALUES}, {"UPDATE", TOK_UPDATE},
        {"SET", TOK_SET}, {"DELETE", TOK_DELETE}, {"CREATE", TOK_CREATE},
        {"TABLE", TOK_TABLE}, {"INDEX", TOK_INDEX}, {"ON", TOK_ON},
        {"BEGIN", TOK_BEGIN}, {"COMMIT", TOK_COMMIT}, {"ROLLBACK", TOK_ROLLBACK},
        {"AND", TOK_AND}, {"OR", TOK_OR}, {"ASC", TOK_ASC}, {"DESC", TOK_DESC},
        {"COUNT", TOK_COUNT}, {"MIN", TOK_MIN}, {"MAX", TOK_MAX},
        {"SUM", TOK_SUM}, {"AVG", TOK_AVG},

        // Type keywords
        {"INT", TOK_INT}, {"INT32", TOK_INT32}, {"INT64", TOK_INT64},
        {"VARCHAR", TOK_VARCHAR}, {"VARCHAR32", TOK_VARCHAR32},
        {"VARCHAR256", TOK_VARCHAR256}, {"VAR32", TOK_VAR32}
    };




// Check if identifier is a keyword
static TokenType check_keyword(const char* start, size_t len) {
    // Create temporary string for comparison
    std::string keyword(start, len);

    // Convert to uppercase for case-insensitive comparison
    for (char& c : keyword) {
        c = toupper(c);
    }

    auto it = keyword_map.find(keyword);
    if (it != keyword_map.end()) {
        return it->second;
    }
    return TOK_IDENTIFIER;
}

// Lexer functions
static void skip_whitespace(Parser* p) {
    while (p->pos < p->len && isspace(p->input[p->pos])) {
        p->pos++;
    }
}

static void scan_token(Parser* p) {
    skip_whitespace(p);

    p->current_start = &p->input[p->pos];

    if (p->pos >= p->len) {
        p->current_type = TOK_EOF;
        p->current_len = 0;
        return;
    }

    char c = p->input[p->pos];

    // Single character tokens
    switch (c) {
        case '(': p->current_type = TOK_LPAREN; p->current_len = 1; p->pos++; return;
        case ')': p->current_type = TOK_RPAREN; p->current_len = 1; p->pos++; return;
        case ',': p->current_type = TOK_COMMA; p->current_len = 1; p->pos++; return;
        case ';': p->current_type = TOK_SEMICOLON; p->current_len = 1; p->pos++; return;
        case '*': p->current_type = TOK_STAR; p->current_len = 1; p->pos++; return;
    }

    // Comparison operators
    if (c == '=') {
        if (p->pos + 1 < p->len && p->input[p->pos + 1] == '=') {
            p->current_type = TOK_EQ;
            p->current_len = 2;
            p->pos += 2;
            return;
        }
        p->current_type = TOK_EQ;
        p->current_len = 1;
        p->pos++;
        return;
    }

    if (c == '!') {
        if (p->pos + 1 < p->len && p->input[p->pos + 1] == '=') {
            p->current_type = TOK_NE;
            p->current_len = 2;
            p->pos += 2;
            return;
        }
    }

    if (c == '<') {
        if (p->pos + 1 < p->len && p->input[p->pos + 1] == '=') {
            p->current_type = TOK_LE;
            p->current_len = 2;
            p->pos += 2;
            return;
        }
        if (p->pos + 1 < p->len && p->input[p->pos + 1] == '>') {
            p->current_type = TOK_NE;
            p->current_len = 2;
            p->pos += 2;
            return;
        }
        p->current_type = TOK_LT;
        p->current_len = 1;
        p->pos++;
        return;
    }

    if (c == '>') {
        if (p->pos + 1 < p->len && p->input[p->pos + 1] == '=') {
            p->current_type = TOK_GE;
            p->current_len = 2;
            p->pos += 2;
            return;
        }
        p->current_type = TOK_GT;
        p->current_len = 1;
        p->pos++;
        return;
    }

    // String literals
    if (c == '\'' || c == '"') {
        char quote = c;
        p->pos++;
        size_t start = p->pos;
        while (p->pos < p->len && p->input[p->pos] != quote) {
            if (p->input[p->pos] == '\\' && p->pos + 1 < p->len) {
                p->pos += 2;
            } else {
                p->pos++;
            }
        }
        p->current_type = TOK_STRING;
        p->current_start = &p->input[start];
        p->current_len = p->pos - start;
        if (p->pos < p->len) p->pos++; // Skip closing quote
        return;
    }

    // Numbers
    if (isdigit(c) || (c == '-' && p->pos + 1 < p->len && isdigit(p->input[p->pos + 1]))) {
        size_t start = p->pos;
        if (c == '-') p->pos++;

        while (p->pos < p->len && isdigit(p->input[p->pos])) {
            p->pos++;
        }

        if (p->pos < p->len && p->input[p->pos] == '.') {
            p->pos++;
            while (p->pos < p->len && isdigit(p->input[p->pos])) {
                p->pos++;
            }
            p->current_type = TOK_FLOAT;
            char buffer[64];
            size_t len = p->pos - start;
            if (len >= sizeof(buffer)) len = sizeof(buffer) - 1;
            memcpy(buffer, &p->input[start], len);
            buffer[len] = '\0';
            p->current_value.float_val = atof(buffer);
        } else {
            p->current_type = TOK_INTEGER;
            p->current_value.int_val = 0;
            size_t i = start;
            int sign = 1;
            if (p->input[i] == '-') {
                sign = -1;
                i++;
            }
            while (i < p->pos) {
                p->current_value.int_val = p->current_value.int_val * 10 + (p->input[i] - '0');
                i++;
            }
            p->current_value.int_val *= sign;
        }
        p->current_len = p->pos - start;
        return;
    }

    // Identifiers and keywords
    if (isalpha(c) || c == '_') {
        size_t start = p->pos;
        while (p->pos < p->len && (isalnum(p->input[p->pos]) || p->input[p->pos] == '_')) {
            p->pos++;
        }
        p->current_len = p->pos - start;
        p->current_type = check_keyword(p->current_start, p->current_len);
        return;
    }

    // Unknown token
    p->error_msg = "Unexpected character";
    p->error_pos = p->pos;
    p->current_type = TOK_EOF;
}

static void advance(Parser* p) {
    scan_token(p);
}

static bool match(Parser* p, TokenType type) {
    if (p->current_type == type) {
        advance(p);
        return true;
    }
    return false;
}

static bool expect(Parser* p, TokenType type) {
    if (p->current_type != type) {
        p->error_msg = "Unexpected token";
        p->error_pos = p->pos;
        return false;
    }
    advance(p);
    return true;
}

static char* copy_identifier(Parser* p) {
    char* result = (char*)arena::alloc<QueryArena>(p->current_len + 1);
    memcpy(result, p->current_start, p->current_len);
    result[p->current_len] = '\0';
    return result;
}

static CompareOp token_to_compare_op(TokenType type) {
    switch (type) {
        case TOK_EQ: return EQ;
        case TOK_NE: return NE;
        case TOK_LT: return LT;
        case TOK_LE: return LE;
        case TOK_GT: return GT;
        case TOK_GE: return GE;
        default: return EQ;
    }
}

// Convert type token to DataType
static DataType token_to_data_type(TokenType type) {
    switch (type) {
        case TOK_INT:
        case TOK_INT32:
            return TYPE_UINT32;
        case TOK_INT64:
            return TYPE_UINT64;
        case TOK_VARCHAR:
        case TOK_VARCHAR256:
            return TYPE_VARCHAR256;
        case TOK_VARCHAR32:
        case TOK_VAR32:
            return TYPE_VARCHAR32;
        default:
            return TYPE_VARCHAR32;  // Default type
    }
}

// Forward declarations
static ASTNode* parse_expression(Parser* p);
static ASTNode* parse_comparison(Parser* p);
static ASTNode* parse_and_expression(Parser* p);

// Parse a literal value
static ASTNode* parse_literal(Parser* p) {
    LiteralNode* node = (LiteralNode*)arena::alloc<QueryArena>(sizeof(LiteralNode));
    node->type = AST_LITERAL;

    if (p->current_type == TOK_INTEGER) {
        node->value.type = TYPE_UINT32;
        uint32_t val = (uint32_t)p->current_value.int_val;
        node->value.data = (uint8_t*)arena::alloc<QueryArena>(sizeof(uint32_t));
        memcpy(node->value.data, &val, sizeof(uint32_t));
        advance(p);
    } else if (p->current_type == TOK_STRING) {
        size_t len = p->current_len;
        if (len > 256) len = 256;

        node->value.type = (len <= 32) ? TYPE_VARCHAR32 : TYPE_VARCHAR256;
        uint32_t alloc_size = VMValue::get_size(node->value.type);
        node->value.data = (uint8_t*)arena::alloc<QueryArena>(alloc_size);
        memset(node->value.data, 0, alloc_size);
        memcpy(node->value.data, p->current_start, len);

        advance(p);
    } else {
        node->value.type = TYPE_NULL;
        node->value.data = nullptr;
    }

    return (ASTNode*)node;
}

// Parse a column reference
static ASTNode* parse_column_ref(Parser* p) {
    ColumnRefNode* node = (ColumnRefNode*)arena::alloc<QueryArena>(sizeof(ColumnRefNode));
    node->type = AST_COLUMN_REF;
    node->name = copy_identifier(p);
    node->index = 0;  // Will be resolved later
    advance(p);
    return (ASTNode*)node;
}

// Parse a primary expression (column or literal)
static ASTNode* parse_primary(Parser* p) {
    if (p->current_type == TOK_IDENTIFIER) {
        return parse_column_ref(p);
    } else if (p->current_type == TOK_INTEGER || p->current_type == TOK_STRING) {
        return parse_literal(p);
    }

    p->error_msg = "Expected column or value";
    return nullptr;
}

// Parse a comparison expression
static ASTNode* parse_comparison(Parser* p) {
    ASTNode* left = parse_primary(p);
    if (!left) return nullptr;

    if (p->current_type >= TOK_EQ && p->current_type <= TOK_GE) {
        BinaryOpNode* node = (BinaryOpNode*)arena::alloc<QueryArena>(sizeof(BinaryOpNode));
        node->type = AST_BINARY_OP;
        node->op = token_to_compare_op(p->current_type);
        node->left = left;
        node->is_and = false;
        advance(p);

        node->right = parse_primary(p);
        if (!node->right) return nullptr;

        return (ASTNode*)node;
    }

    return left;
}

// Parse AND expression (only AND support for now)
static ASTNode* parse_and_expression(Parser* p) {
    ASTNode* left = parse_comparison(p);
    if (!left) return nullptr;

    while (match(p, TOK_AND)) {
        BinaryOpNode* node = (BinaryOpNode*)arena::alloc<QueryArena>(sizeof(BinaryOpNode));
        node->type = AST_BINARY_OP;
        node->is_and = true;
        node->left = left;
        node->right = parse_comparison(p);
        if (!node->right) return nullptr;
        left = (ASTNode*)node;
    }

    return left;
}

// Parse WHERE clause
static WhereNode* parse_where_clause(Parser* p) {
    if (!match(p, TOK_WHERE)) {
        return nullptr;
    }

    WhereNode* node = (WhereNode*)arena::alloc<QueryArena>(sizeof(WhereNode));
    node->type = AST_WHERE;
    node->condition = parse_and_expression(p);

    return node;
}

// Parse ORDER BY clause
static OrderByNode* parse_order_by_clause(Parser* p) {
    if (!match(p, TOK_ORDER)) {
        return nullptr;
    }

    expect(p, TOK_BY);

    OrderByNode* node = (OrderByNode*)arena::alloc<QueryArena>(sizeof(OrderByNode));
    node->type = AST_ORDER_BY;
    node->column = copy_identifier(p);
    node->column_index = 0;  // Will be resolved later
    advance(p);

    if (match(p, TOK_DESC)) {
        node->ascending = false;
    } else {
        match(p, TOK_ASC);  // Optional
        node->ascending = true;
    }

    return node;
}

// Parse SELECT statement
static ASTNode* parse_select(Parser* p) {
    expect(p, TOK_SELECT);

    SelectNode* node = (SelectNode*)arena::alloc<QueryArena>(sizeof(SelectNode));
    node->type = AST_SELECT;
    node->aggregate = nullptr;

    // Parse column list or aggregate
    if (p->current_type >= TOK_COUNT && p->current_type <= TOK_AVG) {
        // Aggregate function
        AggregateNode* agg = (AggregateNode*)arena::alloc<QueryArena>(sizeof(AggregateNode));
        agg->type = AST_AGGREGATE;
        agg->function = copy_identifier(p);
        advance(p);

        expect(p, TOK_LPAREN);
        if (match(p, TOK_STAR)) {
            agg->arg = nullptr;  // COUNT(*)
        } else {
            agg->arg = parse_column_ref(p);
        }
        expect(p, TOK_RPAREN);

        node->aggregate = agg;

    } else if (match(p, TOK_STAR)) {
        // SELECT * - leave columns empty

    } else {
        // Column list
        do {
            if (p->current_type == TOK_IDENTIFIER) {
                node->columns.push_back(parse_column_ref(p));
            }
        } while (match(p, TOK_COMMA));
    }

    expect(p, TOK_FROM);
    node->table = copy_identifier(p);
    advance(p);

    node->where = parse_where_clause(p);
    node->order_by = parse_order_by_clause(p);

    return (ASTNode*)node;
}

// Parse INSERT statement
static ASTNode* parse_insert(Parser* p) {
    expect(p, TOK_INSERT);
    expect(p, TOK_INTO);

    InsertNode* node = (InsertNode*)arena::alloc<QueryArena>(sizeof(InsertNode));
    node->type = AST_INSERT;
    node->table = copy_identifier(p);
    advance(p);

    expect(p, TOK_VALUES);
    expect(p, TOK_LPAREN);

    do {
        node->values.push_back(parse_literal(p));
    } while (match(p, TOK_COMMA));

    expect(p, TOK_RPAREN);

    return (ASTNode*)node;
}

// Parse UPDATE statement
static ASTNode* parse_update(Parser* p) {
    expect(p, TOK_UPDATE);

    UpdateNode* node = (UpdateNode*)arena::alloc<QueryArena>(sizeof(UpdateNode));
    node->type = AST_UPDATE;
    node->table = copy_identifier(p);
    advance(p);

    expect(p, TOK_SET);

    do {
        SetClauseNode* set = (SetClauseNode*)arena::alloc<QueryArena>(sizeof(SetClauseNode));
        set->type = AST_SET_CLAUSE;
        set->column = copy_identifier(p);
        set->column_index = 0;  // Will be resolved later
        advance(p);

        expect(p, TOK_EQ);
        set->value = parse_literal(p);

        node->set_clauses.push_back(set);
    } while (match(p, TOK_COMMA));

    node->where = parse_where_clause(p);

    return (ASTNode*)node;
}

// Parse DELETE statement
static ASTNode* parse_delete(Parser* p) {
    expect(p, TOK_DELETE);
    expect(p, TOK_FROM);

    DeleteNode* node = (DeleteNode*)arena::alloc<QueryArena>(sizeof(DeleteNode));
    node->type = AST_DELETE;
    node->table = copy_identifier(p);
    advance(p);

    node->where = parse_where_clause(p);

    return (ASTNode*)node;
}

// Parse CREATE statement
static ASTNode* parse_create(Parser* p) {
    expect(p, TOK_CREATE);

    if (match(p, TOK_TABLE)) {
        CreateTableNode* node = (CreateTableNode*)arena::alloc<QueryArena>(sizeof(CreateTableNode));
        node->type = AST_CREATE_TABLE;
        node->table = copy_identifier(p);
        advance(p);

        expect(p, TOK_LPAREN);

        do {
            ColumnInfo col;

            // Parse type - now using proper type tokens
            if (p->current_type >= TOK_INT && p->current_type <= TOK_VAR32) {
                col.type = token_to_data_type(p->current_type);
                advance(p);
            } else if (p->current_type == TOK_IDENTIFIER) {
                // Fallback for unrecognized types
                advance(p);
                col.type = TYPE_VARCHAR32;
            }

            // Parse column name
            if (p->current_type == TOK_IDENTIFIER) {
                char* col_name = copy_identifier(p);
                advance(p);

                size_t len = strlen(col_name);
                if (len > 31) len = 31;
                col.name.assign(col_name);
                // col.name[len] = '\0';
            }

            node->columns.push_back(col);

        } while (match(p, TOK_COMMA));

        expect(p, TOK_RPAREN);

        return (ASTNode*)node;

    } else if (match(p, TOK_INDEX)) {
        CreateIndexNode* node = (CreateIndexNode*)arena::alloc<QueryArena>(sizeof(CreateIndexNode));
        node->type = AST_CREATE_INDEX;
        node->index_name = copy_identifier(p);
        advance(p);

        expect(p, TOK_ON);
        node->table = copy_identifier(p);
        advance(p);

        expect(p, TOK_LPAREN);
        node->column = copy_identifier(p);
        advance(p);
        expect(p, TOK_RPAREN);

        return (ASTNode*)node;
    }

    return nullptr;
}

// Parse transaction statements
static ASTNode* parse_transaction(Parser* p) {
    if (match(p, TOK_BEGIN)) {
        BeginNode* node = (BeginNode*)arena::alloc<QueryArena>(sizeof(BeginNode));
        node->type = AST_BEGIN;
        return (ASTNode*)node;
    } else if (match(p, TOK_COMMIT)) {
        CommitNode* node = (CommitNode*)arena::alloc<QueryArena>(sizeof(CommitNode));
        node->type = AST_COMMIT;
        return (ASTNode*)node;
    } else if (match(p, TOK_ROLLBACK)) {
        RollbackNode* node = (RollbackNode*)arena::alloc<QueryArena>(sizeof(RollbackNode));
        node->type = AST_ROLLBACK;
        return (ASTNode*)node;
    }
    return nullptr;
}

// Main parse function - returns AST
ASTNode* parse_statement(const char* sql) {
    Parser parser = {};
    parser.input = sql;
    parser.len = strlen(sql);
    parser.pos = 0;

    advance(&parser);

    ASTNode* ast = nullptr;

    switch (parser.current_type) {
        case TOK_SELECT:
            ast = parse_select(&parser);
            break;
        case TOK_INSERT:
            ast = parse_insert(&parser);
            break;
        case TOK_UPDATE:
            ast = parse_update(&parser);
            break;
        case TOK_DELETE:
            ast = parse_delete(&parser);
            break;
        case TOK_CREATE:
            ast = parse_create(&parser);
            break;
        case TOK_BEGIN:
        case TOK_COMMIT:
        case TOK_ROLLBACK:
            ast = parse_transaction(&parser);
            break;
        default:
            parser.error_msg = "Expected statement";
            parser.error_pos = parser.pos;
            return nullptr;
    }

    // Expect semicolon or EOF
    if (parser.current_type != TOK_SEMICOLON && parser.current_type != TOK_EOF) {
        parser.error_msg = "Expected semicolon or end of input";
        return nullptr;
    }

    if (parser.error_msg) {
        // Could add error reporting here
        return nullptr;
    }

    return ast;
}
ArenaVector<ASTNode*, QueryArena> parse_sql(const char* sql) {
    ArenaVector<ASTNode*, QueryArena> statements;

    // Simple multi-statement parsing
    const char* current = sql;
    while (*current) {
        // Skip whitespace
        while (*current && isspace(*current)) current++;
        if (!*current) break;

        // Find end of statement (semicolon or end of string)
        const char* end = current;
        while (*end && *end != ';') end++;

        // Parse single statement
        size_t len = end - current;
        char* single_sql = (char*)arena::alloc<QueryArena>(len + 1);
        memcpy(single_sql, current, len);
        single_sql[len] = '\0';

        ASTNode* ast = parse_statement(single_sql);
        if (ast) {
            statements.push_back(ast);
        }

        current = (*end == ';') ? end + 1 : end;
    }

    return statements;
}
