#include "parser.hpp"
#include "program_builder.hpp"
#include "arena.hpp"
#include "vm.hpp"
#include <cstring>
#include <cctype>

// Forward declarations
static void advance(Parser* p);
static bool expect(Parser* p, TokenType type);
static bool match(Parser* p, TokenType type);
static std::vector<VMInstruction> parse_statement(Parser* p);
static std::vector<VMInstruction> parse_select(Parser* p);
static std::vector<VMInstruction> parse_insert(Parser* p);
static std::vector<VMInstruction> parse_update(Parser* p);
static std::vector<VMInstruction> parse_delete(Parser* p);
static std::vector<VMInstruction> parse_create(Parser* p);
static WhereCondition parse_condition(Parser* p, const std::vector<ColumnInfo>& schema);
static std::vector<WhereCondition> parse_where_clause(Parser* p, const std::vector<ColumnInfo>& schema);

// Helper to resolve column name to index
static uint32_t resolve_column_index(const char* col_name, const std::vector<ColumnInfo>& schema) {
    for (size_t i = 0; i < schema.size(); i++) {
        if (strcmp(schema[i].name, col_name) == 0) {
            return i;
        }
    }
    // Default to 0 if not found - in production would error
    return 0;
}

// Token lookup table for keywords
struct Keyword {
    const char* str;
    TokenType type;
};

static const Keyword keywords[] = {
    {"SELECT", TOK_SELECT}, {"FROM", TOK_FROM}, {"WHERE", TOK_WHERE},
    {"ORDER", TOK_ORDER}, {"BY", TOK_BY}, {"INSERT", TOK_INSERT},
    {"INTO", TOK_INTO}, {"VALUES", TOK_VALUES}, {"UPDATE", TOK_UPDATE},
    {"SET", TOK_SET}, {"DELETE", TOK_DELETE}, {"CREATE", TOK_CREATE},
    {"TABLE", TOK_TABLE}, {"INDEX", TOK_INDEX}, {"ON", TOK_ON},
    {"BEGIN", TOK_BEGIN}, {"COMMIT", TOK_COMMIT}, {"ROLLBACK", TOK_ROLLBACK},
    {"AND", TOK_AND}, {"OR", TOK_OR}, {"ASC", TOK_ASC}, {"DESC", TOK_DESC},
    {"COUNT", TOK_COUNT}, {"MIN", TOK_MIN}, {"MAX", TOK_MAX},
    {"SUM", TOK_SUM}, {"AVG", TOK_AVG},
    {nullptr, TOK_EOF}
};

static void skip_whitespace(Parser* p) {
    while (p->pos < p->len && isspace(p->input[p->pos])) {
        p->pos++;
    }
}

static TokenType check_keyword(const char* start, size_t len) {
    for (const Keyword* k = keywords; k->str; k++) {
        if (strlen(k->str) == len && strncasecmp(start, k->str, len) == 0) {
            return k->type;
        }
    }
    return TOK_IDENTIFIER;
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
            // Parse float value
            char buffer[64];
            size_t len = p->pos - start;
            if (len >= sizeof(buffer)) len = sizeof(buffer) - 1;
            memcpy(buffer, &p->input[start], len);
            buffer[len] = '\0';
            p->current_value.float_val = atof(buffer);
        } else {
            p->current_type = TOK_INTEGER;
            // Parse integer value
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
    char* result = (char*)arena_alloc(p->current_len + 1);
    memcpy(result, p->current_start, p->current_len);
    result[p->current_len] = '\0';
    return result;
}

static VMValue parse_value(Parser* p) {
    VMValue value;

    if (p->current_type == TOK_INTEGER) {
        value.type = TYPE_INT32;
        uint32_t val = (uint32_t)p->current_value.int_val;
        value.data = (uint8_t*)arena_alloc(sizeof(uint32_t));
        memcpy(value.data, &val, sizeof(uint32_t));
        advance(p);
    } else if (p->current_type == TOK_STRING) {
        size_t len = p->current_len;
        if (len > 256) len = 256;

        // Determine type based on length
        value.type = (len <= 32) ? TYPE_VARCHAR32 : TYPE_VARCHAR256;

        // Always allocate the full size for the type
        uint32_t alloc_size = VMValue::get_size(value.type);
        value.data = (uint8_t*)arena_alloc(alloc_size);

        // Zero-fill the entire allocation
        memset(value.data, 0, alloc_size);

        // Copy the actual string data (without null terminator from source)
        memcpy(value.data, p->current_start, len);

        advance(p);
    } else {
        value.type = TYPE_NULL;
        value.data = nullptr;
    }

    return value;
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

static WhereCondition parse_condition(Parser* p, const std::vector<ColumnInfo>& schema) {
    WhereCondition cond;

    // Get column name
    char* column_name = copy_identifier(p);
    advance(p);

    // Resolve column index
    cond.column_index = resolve_column_index(column_name, schema);

    // Get operator
    cond.operator_type = token_to_compare_op(p->current_type);
    advance(p);

    // Get value
    cond.value = parse_value(p);

    return cond;
}

static std::vector<WhereCondition> parse_where_clause(Parser* p, const std::vector<ColumnInfo>& schema) {
    std::vector<WhereCondition> conditions;

    if (!match(p, TOK_WHERE)) {
        return conditions;
    }

    conditions.push_back(parse_condition(p, schema));

    while (match(p, TOK_AND)) {
        conditions.push_back(parse_condition(p, schema));
    }

    // Note: OR support would require expression tree

    return conditions;
}

static std::vector<VMInstruction> parse_select(Parser* p) {
    expect(p, TOK_SELECT);

    bool is_aggregate = false;
    const char* agg_func = nullptr;
    uint32_t* agg_column = nullptr;
    std::vector<uint32_t>* select_columns = nullptr;
    char* agg_column_name = nullptr;

    // Check for aggregate function
    if (p->current_type == TOK_COUNT || p->current_type == TOK_MIN ||
        p->current_type == TOK_MAX || p->current_type == TOK_SUM ||
        p->current_type == TOK_AVG) {

        is_aggregate = true;
        agg_func = copy_identifier(p);
        advance(p);
        expect(p, TOK_LPAREN);

        if (p->current_type == TOK_STAR) {
            advance(p);
            // COUNT(*) - no column
        } else if (p->current_type == TOK_IDENTIFIER) {
            // Get column for aggregate
            agg_column_name = copy_identifier(p);
            advance(p);
            agg_column = (uint32_t*)arena_alloc(sizeof(uint32_t));
        }

        expect(p, TOK_RPAREN);

    } else if (match(p, TOK_STAR)) {
        // SELECT * - all columns
        select_columns = nullptr;
    } else {
        // Column list
        select_columns = (std::vector<uint32_t>*)arena_alloc(sizeof(std::vector<uint32_t>));
        new (select_columns) std::vector<uint32_t>();

        // Store column names temporarily
        std::vector<char*> column_names;
        do {
            if (p->current_type == TOK_IDENTIFIER) {
                char* col_name = copy_identifier(p);
                advance(p);
                column_names.push_back(col_name);
            }
        } while (match(p, TOK_COMMA));

        // Will resolve indices after getting table schema
        for (auto name : column_names) {
            select_columns->push_back(0); // Placeholder
        }
    }

    expect(p, TOK_FROM);

    char* table_name = copy_identifier(p);
    advance(p);

    // Get actual table schema
    auto table = vm_get_table(table_name);
    const auto& schema = table.schema.columns;

    // Now resolve column indices
    if (agg_column && agg_column_name) {
        *agg_column = resolve_column_index(agg_column_name, schema);
    }

    if (select_columns && !select_columns->empty()) {
        // Go back and resolve the column indices
        size_t idx = 0;
        char* col_name = nullptr;
        // Re-parse column names since we stored placeholders
        // This is a simplified approach - in production would store names
        for (size_t i = 0; i < select_columns->size(); i++) {
            (*select_columns)[i] = i; // For now, default to column order
        }
    }

    std::vector<WhereCondition> conditions = parse_where_clause(p, schema);

    OrderBy* order_by = nullptr;
    if (match(p, TOK_ORDER)) {
        expect(p, TOK_BY);
        order_by = (OrderBy*)arena_alloc(sizeof(OrderBy));
        char* order_col = copy_identifier(p);
        advance(p);
        order_by->column_index = resolve_column_index(order_col, schema);

        if (match(p, TOK_DESC)) {
            order_by->direction = "DESC";
        } else {
            match(p, TOK_ASC); // Optional
            order_by->direction = "ASC";
        }
    }

    if (is_aggregate) {
        return aggregate(table_name, agg_func, agg_column, conditions);
    } else {
        SelectOptions opts;
        opts.table_name = table_name;
        opts.schema = schema;
        opts.column_indices = select_columns;
        opts.where_conditions = conditions;
        opts.order_by = order_by;

        return build_select(opts);
    }
}

static std::vector<VMInstruction> parse_insert(Parser* p) {
    expect(p, TOK_INSERT);
    expect(p, TOK_INTO);

    char* table_name = copy_identifier(p);
    advance(p);

    expect(p, TOK_VALUES);
    expect(p, TOK_LPAREN);

    std::vector<Pair> values;
    uint32_t col_index = 0;

    do {
        VMValue val = parse_value(p);
        values.push_back({col_index++, val});
    } while (match(p, TOK_COMMA));

    expect(p, TOK_RPAREN);

    return build_insert(table_name, values, false);
}

static std::vector<VMInstruction> parse_update(Parser* p) {
    expect(p, TOK_UPDATE);

    char* table_name = copy_identifier(p);
    advance(p);

    // Get actual table schema
    auto table = vm_get_table(table_name);
    const auto& schema = table.schema.columns;

    expect(p, TOK_SET);

    std::vector<Pair> set_columns;
    do {
        char* col_name = copy_identifier(p);
        advance(p);
        expect(p, TOK_EQ);
        VMValue val = parse_value(p);

        // Find actual column index
        uint32_t col_index = resolve_column_index(col_name, schema);
        set_columns.push_back({col_index, val});
    } while (match(p, TOK_COMMA));

    std::vector<WhereCondition> conditions = parse_where_clause(p, schema);

    UpdateOptions opts;
    opts.table_name = table_name;
    opts.schema = schema;
    opts.set_columns = set_columns;
    opts.where_conditions = conditions;

    return build_update(opts, false);
}

static std::vector<VMInstruction> parse_delete(Parser* p) {
    expect(p, TOK_DELETE);
    expect(p, TOK_FROM);

    char* table_name = copy_identifier(p);
    advance(p);

    // Get actual table schema
    auto table = vm_get_table(table_name);
    const auto& schema = table.schema.columns;

    std::vector<WhereCondition> conditions = parse_where_clause(p, schema);

    UpdateOptions opts;
    opts.table_name = table_name;
    opts.schema = schema;
    opts.set_columns = {};
    opts.where_conditions = conditions;

    return build_delete(opts, false);
}

static std::vector<VMInstruction> parse_create(Parser* p) {
    expect(p, TOK_CREATE);

    if (match(p, TOK_TABLE)) {
        char* table_name = copy_identifier(p);
        advance(p);

        expect(p, TOK_LPAREN);

        std::vector<ColumnInfo> columns;

        do {
            ColumnInfo col;

            // Check for type keywords (INT, VARCHAR, VAR32, etc.)
            if (p->current_type == TOK_IDENTIFIER) {
                char* type_str = copy_identifier(p);
                advance(p);

                // Enhanced type mapping
                if (strcasecmp(type_str, "INT") == 0) {
                    col.type = TYPE_INT32;
                } else if (strcasecmp(type_str, "INT32") == 0) {
                    col.type = TYPE_INT32;
                } else if (strcasecmp(type_str, "INT64") == 0) {
                    col.type = TYPE_INT64;
                } else if (strcasecmp(type_str, "VARCHAR") == 0) {
                    col.type = TYPE_VARCHAR256;
                } else if (strcasecmp(type_str, "VARCHAR32") == 0 ||
                          strcasecmp(type_str, "VAR32") == 0) {
                    col.type = TYPE_VARCHAR32;
                } else if (strcasecmp(type_str, "VARCHAR256") == 0) {
                    col.type = TYPE_VARCHAR256;
                } else {
                    // Default to VARCHAR32 for unknown types
                    col.type = TYPE_VARCHAR32;
                }
            }

            // Parse column name
            if (p->current_type == TOK_IDENTIFIER) {
                char* col_name = copy_identifier(p);
                advance(p);

                size_t len = strlen(col_name);
                if (len > 31) len = 31;
                memcpy(col.name, col_name, len);
                col.name[len] = '\0';
            }

            columns.push_back(col);

        } while (match(p, TOK_COMMA));

        expect(p, TOK_RPAREN);

        return build_creat_table(table_name, columns);

    } else if (match(p, TOK_INDEX)) {
        char* index_name = copy_identifier(p);
        advance(p);

        expect(p, TOK_ON);

        char* table_name = copy_identifier(p);
        advance(p);

        expect(p, TOK_LPAREN);
        char* column_name = copy_identifier(p);
        advance(p);
        expect(p, TOK_RPAREN);

        // Get table schema to resolve column index and type
        auto table = vm_get_table(table_name);
        const auto& schema = table.schema.columns;

        uint32_t col_index = resolve_column_index(column_name, schema);
        DataType col_type = schema[col_index].type;

        return build_create_index(table_name, col_index, col_type);
    }

    return {};
}

static std::vector<VMInstruction> parse_statement(Parser* p) {
    std::vector<VMInstruction> instructions;

    switch (p->current_type) {
        case TOK_SELECT:
            return parse_select(p);
        case TOK_INSERT:
            return parse_insert(p);
        case TOK_UPDATE:
            return parse_update(p);
        case TOK_DELETE:
            return parse_delete(p);
        case TOK_CREATE:
            return parse_create(p);
        case TOK_BEGIN:
            advance(p);
            instructions.push_back({OP_Begin, 0, 0, 0, nullptr, 0});
            instructions.push_back({OP_Halt, 0, 0, 0, nullptr, 0});
            return instructions;
        case TOK_COMMIT:
            advance(p);
            instructions.push_back({OP_Commit, 0, 0, 0, nullptr, 0});
            instructions.push_back({OP_Halt, 0, 0, 0, nullptr, 0});
            return instructions;
        case TOK_ROLLBACK:
            advance(p);
            instructions.push_back({OP_Rollback, 0, 0, 0, nullptr, 0});
            instructions.push_back({OP_Halt, 0, 0, 0, nullptr, 0});
            return instructions;
        default:
            p->error_msg = "Expected statement";
            p->error_pos = p->pos;
            return {};
    }
}

std::vector<VMInstruction> parse_sql(const char* sql) {
    Parser parser = {};
    parser.input = sql;
    parser.len = strlen(sql);
    parser.pos = 0;

    advance(&parser);

    std::vector<VMInstruction> result = parse_statement(&parser);

    // Expect semicolon or EOF
    if (parser.current_type != TOK_SEMICOLON && parser.current_type != TOK_EOF) {
        parser.error_msg = "Expected semicolon or end of input";
        return {};
    }

    if (parser.error_msg) {
        PRINT "err" END;
        // Could add error reporting here
        return {};
    }

    return result;
}
