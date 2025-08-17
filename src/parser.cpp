#include "parser.hpp"
#include "arena.hpp"
#include "vm.hpp"
#include <algorithm>
#include <cstring>
#include <cctype>


enum TokenType {
    TOK_EOF,
    TOK_IDENTIFIER,
    TOK_INTEGER,
    TOK_FLOAT,
    TOK_STRING,

    // Keywords
    TOK_SELECT, TOK_FROM, TOK_WHERE, TOK_ORDER, TOK_BY,
    TOK_INSERT, TOK_INTO, TOK_VALUES,
    TOK_UPDATE, TOK_SET,
    TOK_DELETE,
    TOK_CREATE, TOK_TABLE, TOK_INDEX, TOK_ON,
    TOK_BEGIN, TOK_COMMIT, TOK_ROLLBACK,
    TOK_AND, TOK_OR,
    TOK_ASC, TOK_DESC,
    TOK_COUNT, TOK_MIN, TOK_MAX, TOK_SUM, TOK_AVG,

    // Operators
    TOK_LPAREN, TOK_RPAREN,
    TOK_COMMA, TOK_SEMICOLON,
    TOK_STAR,
    TOK_EQ, TOK_NE, TOK_LT, TOK_LE, TOK_GT, TOK_GE,
};


struct Parser {
    const char* input;
    size_t pos;
    size_t len;

    // Current token
    TokenType current_type;
    const char* current_start;
    size_t current_len;
    union {
        int64_t int_val;
        double float_val;
    } current_value;

    // Lookahead
    TokenType next_type;

    // Error state
    const char* error_msg;
    size_t error_pos;
};


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


std::vector<VMInstruction> build_direct_rowid_operation(
    const std::string &table_name, const std::vector<ColumnInfo> &schema,
    const std::vector<Pair> &set_columns,
    const WhereCondition &primary_condition,
    const std::vector<WhereCondition> &remaining_conditions,
    RegisterAllocator &regs, UnifiedOptions::Operation operation,
    bool implicit_begin, std::vector<uint32_t> *select_columns,
    const char *aggregate_func, uint32_t *aggregate_column);

std::vector<VMInstruction> build_index_scan_operation(
    const std::string &table_name, const std::vector<ColumnInfo> &schema,
    const std::vector<Pair> &set_columns, const WhereCondition &index_condition,
    const std::vector<WhereCondition> &remaining_conditions, uint32_t index_col,
    RegisterAllocator &regs, UnifiedOptions::Operation operation,
    bool implicit_begin, std::vector<uint32_t> *select_columns,
    const char *aggregate_func, uint32_t *aggregate_column);

std::vector<VMInstruction> build_full_table_scan_operation(
    const std::string &table_name, const std::vector<ColumnInfo> &schema,
    const std::vector<Pair> &set_columns,
    const std::vector<WhereCondition> &conditions, RegisterAllocator &regs,
    UnifiedOptions::Operation operation, bool implicit_begin,
    std::vector<uint32_t> *select_columns, const char *aggregate_func,
    uint32_t *aggregate_column);

std::vector<VMInstruction>
update_or_delete_or_select(const UnifiedOptions &options, bool implicit_begin);

// RegisterAllocator implementation
int RegisterAllocator::get(const std::string &name) {
  auto it = name_to_register.find(name);
  if (it == name_to_register.end()) {
    name_to_register[name] = next_register;
    return next_register++;
  }
  return it->second;
}

void RegisterAllocator::clear() {
  name_to_register.clear();
  next_register = 0;
}

// Helper functions
void resolve_labels(std::vector<VMInstruction> &program,
                    const std::unordered_map<std::string, int> &map) {
  for (auto &inst : program) {
    // Check p2 for label (stored as string in p4)
    if (inst.p4 && inst.p2 == -1) {
      auto it = map.find((const char *)inst.p4);
      if (it != map.end()) {
        inst.p2 = it->second;
        inst.p4 = nullptr;
      }
    }
    // Check p3 for label
    if (inst.p4 && inst.p3 == -1) {
      auto it = map.find((const char *)inst.p4);
      if (it != map.end()) {
        inst.p3 = it->second;
        inst.p4 = nullptr;
      }
    }
  }
}

Pair make_pair(uint32_t index, const VMValue &value) { return {index, value}; }

OpCode str_or_int(const VMValue &value) {
  return (value.type == TYPE_INT32 || value.type == TYPE_INT64) ? OP_Integer
                                                                : OP_String;
}

uint8_t set_p5(uint8_t current, uint8_t flag) { return current | flag; }

void add_begin(std::vector<VMInstruction> &instructions) {
  instructions.insert(instructions.begin(), make_begin());
}

void load_value(std::vector<VMInstruction> &instructions, const VMValue &value,
                int target_reg) {
  if (value.type == TYPE_INT32 || value.type == TYPE_INT64) {
    uint32_t val = *(uint32_t *)value.data;
    instructions.push_back(make_integer(target_reg, (int32_t)val));
  } else {
    instructions.push_back(make_string(target_reg, (int32_t)value.type, value.data));
  }
}

OpCode get_negated_opcode(CompareOp op) {
  switch (op) {
  case EQ:
    return OP_Ne;
  case NE:
    return OP_Eq;
  case LT:
    return OP_Ge;
  case LE:
    return OP_Gt;
  case GT:
    return OP_Le;
  case GE:
    return OP_Lt;
  }
  return OP_Eq;
}

OpCode to_seek(CompareOp op) {
  switch (op) {
  case EQ:
    return OP_SeekEQ;
  case GE:
    return OP_SeekGE;
  case GT:
    return OP_SeekGT;
  case LE:
    return OP_SeekLE;
  case LT:
    return OP_SeekLT;
  default:
    return OP_SeekEQ;
  }
}

OpCode to_opcode(CompareOp op) {
  switch (op) {
  case EQ:
    return OP_Eq;
  case NE:
    return OP_Ne;
  case LT:
    return OP_Lt;
  case LE:
    return OP_Le;
  case GT:
    return OP_Gt;
  case GE:
    return OP_Ge;
  }
  return OP_Eq;
}

bool ascending(CompareOp op) { return op == GE || op == GT || op == EQ; }

// Create table
std::vector<VMInstruction>
build_creat_table(const std::string &table_name,
             const std::vector<ColumnInfo> &columns) {
  TableSchema *schema = ARENA_ALLOC(TableSchema);
  schema->table_name = table_name;
  schema->columns = columns;
  schema->column_offsets.resize(columns.size());

  return {make_create_table(schema), make_halt()};
}

// Drop table
std::vector<VMInstruction> build_drop_table(const std::string &table_name) {
  char *name = (char *)arena_alloc(table_name.size() + 1);
  strcpy(name, table_name.c_str());

  return {make_drop_table(name), make_halt()};
}

// Drop index
std::vector<VMInstruction> build_drop_index(const std::string &index_name) {
  char *name = (char *)arena_alloc(index_name.size() + 1);
  strcpy(name, index_name.c_str());

  return {make_drop_index(0, name), make_halt()};
}

// Create index
std::vector<VMInstruction> build_create_index(const std::string &table_name,
                                        uint32_t column_index,
                                        DataType key_type) {
  RegisterAllocator regs;
  std::vector<VMInstruction> instructions;
  std::unordered_map<std::string, int> labels;

  const int table_cursor_id = 0;
  const int index_cursor_id = 1;

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  instructions.push_back(make_create_index(column_index, table_name_str));
  instructions.push_back(make_open_read(table_cursor_id, table_name_str));
  instructions.push_back(make_open_write(index_cursor_id, table_name_str, column_index));

  instructions.push_back(make_rewind_label(table_cursor_id, "end"));

  labels["loop_start"] = instructions.size();

  int rowid_reg = regs.get("rowid");
  instructions.push_back(make_key(table_cursor_id, rowid_reg));

  int column_reg = regs.get("column_value");
  instructions.push_back(make_column(table_cursor_id, (int32_t)column_index, column_reg));
  instructions.push_back(make_insert(index_cursor_id, column_reg, rowid_reg));
  instructions.push_back(make_next_label(table_cursor_id, "loop_start"));

  labels["end"] = instructions.size();

  instructions.push_back(make_close(table_cursor_id));
  instructions.push_back(make_close(index_cursor_id));
  instructions.push_back(make_halt());

  resolve_labels(instructions, labels);
  return instructions;
}

// Insert
std::vector<VMInstruction> build_insert(const std::string &table_name,
                                  const std::vector<Pair> &values,
                                  bool implicit_begin) {
  RegisterAllocator regs;
  std::vector<VMInstruction> instructions;

  if (implicit_begin) {
    add_begin(instructions);
  }

  const int table_cursor_id = 0;

  // Get indexes for this table
  auto table_indexes = vm_get_table_indexes(table_name);
  std::unordered_map<uint32_t, std::pair<Index, int>> indexes_to_insert;
  int cursor_id = 1;
  for (const auto &[col, index] : table_indexes) {
    indexes_to_insert[col] = {index, cursor_id++};
  }

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  instructions.push_back(make_open_write(table_cursor_id, table_name_str));

  // Open index cursors
  for (const auto &[col_idx, idx_pair] : indexes_to_insert) {
    instructions.push_back(make_open_write(idx_pair.second, table_name_str, col_idx));
  }

  // Load values
  std::vector<int> value_regs;
  for (size_t i = 0; i < values.size(); i++) {
    int reg = regs.get("value_" + std::to_string(i));
    value_regs.push_back(reg);

    load_value(instructions, values[i].value, reg);

    // Insert into indexes if needed
    if (indexes_to_insert.find(values[i].column_index) != indexes_to_insert.end()) {
      auto &idx_pair = indexes_to_insert[values[i].column_index];
      instructions.push_back(make_insert(idx_pair.second, reg, 0));
    }
  }

  int record_reg = regs.get("record");
  instructions.push_back(make_record(value_regs[0], (int32_t)values.size(), record_reg));
  instructions.push_back(make_insert(table_cursor_id, value_regs[0], record_reg));
  instructions.push_back(make_close(table_cursor_id));
  instructions.push_back(make_halt());

  return instructions;
}

// Build where checks helper
void build_where_checks(std::vector<VMInstruction> &instructions, int cursor_id,
                        const std::vector<WhereCondition> &conditions,
                        const std::string &skip_label,
                        RegisterAllocator &regs) {
  for (size_t i = 0; i < conditions.size(); i++) {
    int col_reg = regs.get("where_col_" + std::to_string(i));
    instructions.push_back(make_column(cursor_id, (int32_t)conditions[i].column_index, col_reg));

    int compare_reg = regs.get("compare_" + std::to_string(i));
    load_value(instructions, conditions[i].value, compare_reg);

    OpCode negated = get_negated_opcode(conditions[i].operator_type);

    char *label_str = (char *)arena_alloc(skip_label.size() + 1);
    strcpy(label_str, skip_label.c_str());

    // Build the appropriate comparison with label
    switch(negated) {
      case OP_Eq:
        instructions.push_back(make_eq_label(col_reg, compare_reg, label_str));
        break;
      case OP_Ne:
        instructions.push_back(make_ne_label(col_reg, compare_reg, label_str));
        break;
      case OP_Lt:
        instructions.push_back(make_lt_label(col_reg, compare_reg, label_str));
        break;
      case OP_Le:
        instructions.push_back(make_le_label(col_reg, compare_reg, label_str));
        break;
      case OP_Gt:
        instructions.push_back(make_gt_label(col_reg, compare_reg, label_str));
        break;
      case OP_Ge:
        instructions.push_back(make_ge_label(col_reg, compare_reg, label_str));
        break;
    }
  }
}

// Generate aggregate instructions
std::vector<VMInstruction>
aggregate(const std::string &table_name, const char *agg_func,
          uint32_t *column_index,
          const std::vector<WhereCondition> &where_conditions) {

  if (strcmp(agg_func, "COUNT") != 0 && column_index == nullptr) {
    return {};
  }

  if (where_conditions.size() > 0) {
    auto table = vm_get_table(table_name);

    UnifiedOptions options = {.table_name = table_name,
                              .schema = table.schema.columns,
                              .set_columns = {},
                              .where_conditions = where_conditions,
                              .operation = UnifiedOptions::AGGREGATE,
                              .select_columns = nullptr,
                              .order_by = nullptr,
                              .aggregate_func = agg_func,
                              .aggregate_column = column_index};

    return update_or_delete_or_select(options, false);
  }

  // Simple aggregate without WHERE
  RegisterAllocator regs;
  std::vector<VMInstruction> instructions;
  const int cursor_id = 0;
  int agg_reg = regs.get("agg");
  int output_reg = regs.get("output");

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  instructions.push_back(make_open_read(cursor_id, table_name_str));
  instructions.push_back(make_agg_reset(agg_func));

  int rewind_jump = 6 + (strcmp(agg_func, "COUNT") == 0 ? 0 : 1);
  instructions.push_back(make_rewind(cursor_id, rewind_jump));

  int loop_start = 3;
  if (strcmp(agg_func, "COUNT") != 0) {
    int value_reg = regs.get("value");
    instructions.push_back(make_column(cursor_id, (int32_t)*column_index, value_reg));
    loop_start = 4;
    instructions.push_back(make_agg_step(value_reg));
  } else {
    instructions.push_back(make_agg_step());
  }

  instructions.push_back(make_next(cursor_id, loop_start));
  instructions.push_back(make_agg_final(output_reg));
  instructions.push_back(make_result_row(output_reg, 1));
  instructions.push_back(make_close(cursor_id));
  instructions.push_back(make_flush());
  instructions.push_back(make_halt());

  return instructions;
}

// Optimization functions (unchanged)
double estimate_selectivity(const WhereCondition &condition,
                            const std::vector<ColumnInfo> &schema,
                            const std::string &table_name) {
  std::string column_name = schema[condition.column_index].name;
  std::string index_name = table_name + "." + column_name;

  auto indexes = vm_get_table_indexes(table_name);
  auto it = indexes.find(condition.column_index);
  bool is_indexed = (it != indexes.end());

  switch (condition.operator_type) {
  case EQ:
    if (condition.column_index == 0)
      return 0.001;
    return is_indexed ? 0.01 : 0.1;
  case NE:
    return 0.9;
  case LT:
  case LE:
  case GT:
  case GE:
    return is_indexed ? 0.2 : 0.3;
  default:
    return 0.5;
  }
}

std::vector<WhereCondition>
optimize_where_conditions(const std::vector<WhereCondition> &conditions,
                          const std::vector<ColumnInfo> &schema,
                          const std::string &table_name) {

  std::vector<WhereCondition> optimized = conditions;

  for (auto &cond : optimized) {
    if (cond.selectivity == 0.5) {
      cond.selectivity = estimate_selectivity(cond, schema, table_name);
    }
  }

  std::sort(optimized.begin(), optimized.end(),
            [&](const WhereCondition &a, const WhereCondition &b) {
              if (a.column_index == 0 && a.operator_type == EQ)
                return true;
              if (b.column_index == 0 && b.operator_type == EQ)
                return false;

              auto indexes = vm_get_table_indexes(table_name);
              bool a_indexed = indexes.find(a.column_index) != indexes.end() &&
                               a.operator_type == EQ;
              bool b_indexed = indexes.find(b.column_index) != indexes.end() &&
                               b.operator_type == EQ;

              if (a_indexed && !b_indexed)
                return true;
              if (b_indexed && !a_indexed)
                return false;

              return a.selectivity < b.selectivity;
            });

  return optimized;
}

AccessMethod choose_access_method(const std::vector<WhereCondition> &conditions,
                                  const std::vector<ColumnInfo> &schema,
                                  const std::string &table_name) {
  std::vector<WhereCondition> sorted_conditions = conditions;
  std::stable_partition(
      sorted_conditions.begin(), sorted_conditions.end(),
      [](const WhereCondition &c) { return c.operator_type == EQ; });

  for (auto &cond : sorted_conditions) {
    if (cond.operator_type == EQ && cond.column_index == 0) {
      return {.type = AccessMethod::DIRECT_ROWID,
              .primary_condition = const_cast<WhereCondition *>(&cond),
              .index_condition = nullptr,
              .index_col = cond.column_index};
    }
  }

  auto indexes = vm_get_table_indexes(table_name);
  for (auto &cond : sorted_conditions) {
    if (indexes.find(cond.column_index) != indexes.end()) {
      return {.type = AccessMethod::INDEX_SCAN,
              .primary_condition = nullptr,
              .index_condition = const_cast<WhereCondition *>(&cond),
              .index_col = cond.column_index};
    }
  }

  return {.type = AccessMethod::FULL_TABLE_SCAN,
          .primary_condition = nullptr,
          .index_condition = nullptr,
          .index_col = 0};
}

// Main unified function
std::vector<VMInstruction>
update_or_delete_or_select(const UnifiedOptions &options, bool implicit_begin) {
  RegisterAllocator regs;

  auto optimized_conditions = optimize_where_conditions(
      options.where_conditions, options.schema, options.table_name);

  auto access_method = choose_access_method(optimized_conditions,
                                            options.schema, options.table_name);

  std::vector<VMInstruction> instructions;

  switch (access_method.type) {
  case AccessMethod::DIRECT_ROWID:
    instructions = build_direct_rowid_operation(
        options.table_name, options.schema, options.set_columns,
        *access_method.primary_condition,
        [&]() {
          std::vector<WhereCondition> remaining;
          for (const auto &c : optimized_conditions) {
            if (&c != access_method.primary_condition) {
              remaining.push_back(c);
            }
          }
          return remaining;
        }(),
        regs, options.operation, implicit_begin, options.select_columns,
        options.aggregate_func, options.aggregate_column);
    break;

  case AccessMethod::INDEX_SCAN:
    instructions = build_index_scan_operation(
        options.table_name, options.schema, options.set_columns,
        *access_method.index_condition,
        [&]() {
          std::vector<WhereCondition> remaining;
          for (const auto &c : optimized_conditions) {
            if (&c != access_method.index_condition) {
              remaining.push_back(c);
            }
          }
          return remaining;
        }(),
        access_method.index_col, regs, options.operation, implicit_begin,
        options.select_columns, options.aggregate_func,
        options.aggregate_column);
    break;

  case AccessMethod::FULL_TABLE_SCAN:
  default:
    instructions = build_full_table_scan_operation(
        options.table_name, options.schema, options.set_columns,
        optimized_conditions, regs, options.operation, implicit_begin,
        options.select_columns, options.aggregate_func,
        options.aggregate_column);
  }

  if (options.operation == UnifiedOptions::SELECT && options.order_by) {
    instructions.push_back(make_sort(options.order_by->column_index,
                                     options.order_by->direction == "DESC"));
  }

  instructions.push_back(make_flush());
  instructions.push_back(make_halt());

  return instructions;
}

// Public wrapper functions
std::vector<VMInstruction> build_select(const SelectOptions &options) {
  UnifiedOptions unified = {.table_name = options.table_name,
                            .schema = options.schema,
                            .set_columns = {},
                            .where_conditions = options.where_conditions,
                            .operation = UnifiedOptions::SELECT,
                            .select_columns = options.column_indices,
                            .order_by = options.order_by,
                            .aggregate_func = nullptr,
                            .aggregate_column = nullptr};

  return update_or_delete_or_select(unified, false);
}

std::vector<VMInstruction> build_update(const UpdateOptions &options,
                                        bool implicit_begin) {
  UnifiedOptions unified = {.table_name = options.table_name,
                            .schema = options.schema,
                            .set_columns = options.set_columns,
                            .where_conditions = options.where_conditions,
                            .operation = UnifiedOptions::UPDATE,
                            .select_columns = nullptr,
                            .order_by = nullptr,
                            .aggregate_func = nullptr,
                            .aggregate_column = nullptr};

  return update_or_delete_or_select(unified, implicit_begin);
}

std::vector<VMInstruction> build_delete(const UpdateOptions &options,
                                        bool implicit_begin) {
  UnifiedOptions unified = {.table_name = options.table_name,
                            .schema = options.schema,
                            .set_columns = {},
                            .where_conditions = options.where_conditions,
                            .operation = UnifiedOptions::DELETE,
                            .select_columns = nullptr,
                            .order_by = nullptr,
                            .aggregate_func = nullptr,
                            .aggregate_column = nullptr};

  return update_or_delete_or_select(unified, implicit_begin);
}

// Build full table scan operation
std::vector<VMInstruction> build_full_table_scan_operation(
    const std::string &table_name,
    const std::vector<ColumnInfo> &schema,
    const std::vector<Pair> &set_columns,
    const std::vector<WhereCondition> &conditions, RegisterAllocator &regs,
    UnifiedOptions::Operation operation, bool implicit_begin,
    std::vector<uint32_t> *select_columns, const char *aggregate_func,
    uint32_t *aggregate_column) {

  std::vector<VMInstruction> instructions;
  std::unordered_map<std::string, int> labels;
  const int cursor_id = 0;

  if (implicit_begin) {
    add_begin(instructions);
  }

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  if (operation == UnifiedOptions::SELECT || operation == UnifiedOptions::AGGREGATE) {
    instructions.push_back(make_open_read(cursor_id, table_name_str));
  } else {
    instructions.push_back(make_open_write(cursor_id, table_name_str));
  }

  // Initialize aggregate if needed
  int agg_reg = -1;
  if (operation == UnifiedOptions::AGGREGATE) {
    agg_reg = regs.get("agg");
    instructions.push_back(make_agg_reset(aggregate_func));
  }

  // Load comparison values
  for (size_t i = 0; i < conditions.size(); i++) {
    int reg = regs.get("compare_" + std::to_string(i));
    load_value(instructions, conditions[i].value, reg);
  }

  instructions.push_back(make_rewind_label(cursor_id, "end"));

  labels["loop_start"] = instructions.size();

  build_where_checks(instructions, cursor_id, conditions, "next_record", regs);

  // Perform operation
  if (operation == UnifiedOptions::DELETE) {
    instructions.push_back(make_delete(cursor_id));
  } else if (operation == UnifiedOptions::AGGREGATE) {
    if (strcmp(aggregate_func, "COUNT") != 0 && aggregate_column != nullptr) {
      int value_reg = regs.get("agg_value");
      instructions.push_back(make_column(cursor_id, (int32_t)*aggregate_column, value_reg));
      instructions.push_back(make_agg_step(value_reg));
    } else {
      instructions.push_back(make_agg_step());
    }
  } else if (operation == UnifiedOptions::SELECT) {
    std::vector<int> output_regs;
    std::vector<uint32_t> columns_to_select;

    if (select_columns) {
      columns_to_select = *select_columns;
    } else {
      for (size_t i = 0; i < schema.size(); i++) {
        columns_to_select.push_back(i);
      }
    }

    for (size_t i = 0; i < columns_to_select.size(); i++) {
      int col_reg = regs.get("output_col_" + std::to_string(i));
      output_regs.push_back(col_reg);
      instructions.push_back(make_column(cursor_id, (int32_t)columns_to_select[i], col_reg));
    }

    instructions.push_back(make_result_row(output_regs[0], (int32_t)output_regs.size()));
  } else {
    // UPDATE
    std::vector<int> current_regs;
    for (size_t i = 0; i < schema.size(); i++) {
      int col_reg = regs.get("current_col_" + std::to_string(i));
      current_regs.push_back(col_reg);
      instructions.push_back(make_column(cursor_id, (int32_t)i, col_reg));
    }

    for (const auto &set_col : set_columns) {
      int reg = regs.get("update_col_" + std::to_string(set_col.column_index));
      load_value(instructions, set_col.value, reg);
      instructions.push_back(make_move(reg, current_regs[set_col.column_index]));
    }

    int record_reg = regs.get("record");
    instructions.push_back(make_record(current_regs[0], (int32_t)schema.size(), record_reg));

    int rowid_reg = regs.get("rowid");
    instructions.push_back(make_key(cursor_id, rowid_reg));
    instructions.push_back(make_insert(cursor_id, rowid_reg, record_reg));
  }

  labels["next_record"] = instructions.size();

  instructions.push_back(make_next_label(cursor_id, "loop_start"));

  labels["end"] = instructions.size();

  if (operation == UnifiedOptions::AGGREGATE) {
    int output_reg = regs.get("output");
    instructions.push_back(make_agg_final(output_reg));
    instructions.push_back(make_result_row(output_reg, 1));
  }

  instructions.push_back(make_close(cursor_id));

  resolve_labels(instructions, labels);
  return instructions;
}

// Build direct rowid operation
std::vector<VMInstruction> build_direct_rowid_operation(
    const std::string &table_name, const std::vector<ColumnInfo> &schema,
    const std::vector<Pair> &set_columns,
    const WhereCondition &primary_condition,
    const std::vector<WhereCondition> &remaining_conditions,
    RegisterAllocator &regs, UnifiedOptions::Operation operation,
    bool implicit_begin, std::vector<uint32_t> *select_columns,
    const char *aggregate_func, uint32_t *aggregate_column) {

  std::vector<VMInstruction> instructions;
  std::unordered_map<std::string, int> labels;
  const int cursor_id = 0;

  if (implicit_begin) {
    add_begin(instructions);
  }

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  if (operation == UnifiedOptions::SELECT || operation == UnifiedOptions::AGGREGATE) {
    instructions.push_back(make_open_read(cursor_id, table_name_str));
  } else {
    instructions.push_back(make_open_write(cursor_id, table_name_str));
  }

  int agg_reg = -1;
  if (operation == UnifiedOptions::AGGREGATE) {
    agg_reg = regs.get("agg");
    instructions.push_back(make_agg_reset(aggregate_func));
  }

  std::unordered_map<uint32_t, std::pair<Index, int>> indexes_to_update;
  if (operation == UnifiedOptions::UPDATE) {
    auto table_indexes = vm_get_table_indexes(table_name);
    int cursor_idx = 1;
    for (const auto &[column, index] : table_indexes) {
      indexes_to_update[column] = {index, cursor_idx++};
    }

    for (const auto &[ci, idx_pair] : indexes_to_update) {
      instructions.push_back(make_open_write(idx_pair.second, table_name_str, ci));
    }
  }

  int rowid_reg = regs.get("rowid_value");
  load_value(instructions, primary_condition.value, rowid_reg);

  instructions.push_back(make_seek_eq_label(cursor_id, rowid_reg, "end"));

  build_where_checks(instructions, cursor_id, remaining_conditions, "end", regs);

  // Perform operation
  if (operation == UnifiedOptions::DELETE) {
    instructions.push_back(make_delete(cursor_id));
  } else if (operation == UnifiedOptions::AGGREGATE) {
    if (strcmp(aggregate_func, "COUNT") != 0 && aggregate_column != nullptr) {
      int value_reg = regs.get("agg_value");
      instructions.push_back(make_column(cursor_id, (int32_t)*aggregate_column, value_reg));
      instructions.push_back(make_agg_step(value_reg));
    } else {
      instructions.push_back(make_agg_step());
    }
  } else if (operation == UnifiedOptions::SELECT) {
    std::vector<int> output_regs;
    std::vector<uint32_t> columns_to_select;

    if (select_columns) {
      columns_to_select = *select_columns;
    } else {
      for (size_t i = 0; i < schema.size(); i++) {
        columns_to_select.push_back(i);
      }
    }

    for (size_t i = 0; i < columns_to_select.size(); i++) {
      int col_reg = regs.get("output_col_" + std::to_string(i));
      output_regs.push_back(col_reg);
      instructions.push_back(make_column(cursor_id, (int32_t)columns_to_select[i], col_reg));
    }

    instructions.push_back(make_result_row(output_regs[0], (int32_t)output_regs.size()));
  } else {
    // UPDATE
    std::vector<int> current_regs;
    for (size_t i = 0; i < schema.size(); i++) {
      int col_reg = regs.get("current_col_" + std::to_string(i));
      current_regs.push_back(col_reg);
      instructions.push_back(make_column(cursor_id, (int32_t)i, col_reg));

      if (indexes_to_update.find(i) != indexes_to_update.end()) {
        const auto &idx_pair = indexes_to_update[i];
        instructions.push_back(make_seek_eq_label(idx_pair.second, col_reg, "end"));
      }
    }

    for (const auto &set_col : set_columns) {
      int reg = regs.get("update_col_" + std::to_string(set_col.column_index));
      load_value(instructions, set_col.value, reg);
      instructions.push_back(make_move(reg, current_regs[set_col.column_index]));

      if (indexes_to_update.find(set_col.column_index) != indexes_to_update.end()) {
        const auto &idx_pair = indexes_to_update[set_col.column_index];
        instructions.push_back(make_delete(idx_pair.second));
        instructions.push_back(make_insert(idx_pair.second, current_regs[set_col.column_index], rowid_reg));
      }
    }

    int record_reg = regs.get("record");
    instructions.push_back(make_record(current_regs[0], (int32_t)schema.size(), record_reg));
    instructions.push_back(make_insert(cursor_id, rowid_reg, record_reg));
  }

  labels["end"] = instructions.size();

  if (operation == UnifiedOptions::AGGREGATE) {
    int output_reg = regs.get("output");
    instructions.push_back(make_agg_final(output_reg));
    instructions.push_back(make_result_row(output_reg, 1));
  }

  instructions.push_back(make_close(cursor_id));

  if (operation == UnifiedOptions::UPDATE) {
    for (const auto &[ci, idx_pair] : indexes_to_update) {
      instructions.push_back(make_close(idx_pair.second));
    }
  }

  resolve_labels(instructions, labels);
  return instructions;
}

// Build index scan operation
std::vector<VMInstruction> build_index_scan_operation(
    const std::string &table_name, const std::vector<ColumnInfo> &schema,
    const std::vector<Pair> &set_columns, const WhereCondition &index_condition,
    const std::vector<WhereCondition> &remaining_conditions, uint32_t index_col,
    RegisterAllocator &regs, UnifiedOptions::Operation operation,
    bool implicit_begin, std::vector<uint32_t> *select_columns,
    const char *aggregate_func, uint32_t *aggregate_column) {

  std::vector<VMInstruction> instructions;
  std::unordered_map<std::string, int> labels;
  const int table_cursor_id = 1;
  const int index_cursor_id = 0;

  if (implicit_begin) {
    add_begin(instructions);
  }

  char *table_name_str = (char *)arena_alloc(table_name.size() + 1);
  strcpy(table_name_str, table_name.c_str());

  if (operation == UnifiedOptions::SELECT || operation == UnifiedOptions::AGGREGATE) {
    instructions.push_back(make_open_read(index_cursor_id, table_name_str));
    instructions.push_back(make_open_read(table_cursor_id, table_name_str));
  } else {
    instructions.push_back(make_open_write(index_cursor_id, table_name_str, index_condition.column_index));
    instructions.push_back(make_open_write(table_cursor_id, table_name_str));
  }

  int agg_reg = -1;
  if (operation == UnifiedOptions::AGGREGATE) {
    agg_reg = regs.get("agg");
    instructions.push_back(make_agg_reset(aggregate_func));
  }

  int ii = 2;
  std::unordered_map<uint32_t, std::pair<Index, int>> indexes_to_update;
  if (operation == UnifiedOptions::UPDATE) {
    auto table_indexes = vm_get_table_indexes(table_name);
    auto table = vm_get_table(table_name);
    for (const auto &[column, index] : table_indexes) {
      auto info = table.schema.columns[column];
      if (column != index_col) {
        indexes_to_update[column] = {index, ii++};
        instructions.push_back(make_open_write(indexes_to_update[column].second, table_name_str, column));
      }
    }
  }

  int index_key_reg = regs.get("index_key");
  load_value(instructions, index_condition.value, index_key_reg);

  // Build seek based on operator type
  switch(index_condition.operator_type) {
    case EQ:
      instructions.push_back(make_seek_eq_label(index_cursor_id, index_key_reg, "end"));
      break;
    case GE:
      instructions.push_back(make_seek_ge_label(index_cursor_id, index_key_reg, "end"));
      break;
    case GT:
      instructions.push_back(make_seek_gt_label(index_cursor_id, index_key_reg, "end"));
      break;
    case LE:
      instructions.push_back(make_seek_le_label(index_cursor_id, index_key_reg, "end"));
      break;
    case LT:
      instructions.push_back(make_seek_lt_label(index_cursor_id, index_key_reg, "end"));
      break;
    default:
      instructions.push_back(make_seek_eq_label(index_cursor_id, index_key_reg, "end"));
  }

  labels["loop_start"] = instructions.size();

  int current_key_reg = regs.get("current_key");
  instructions.push_back(make_key(index_cursor_id, current_key_reg));

  OpCode negated_op = get_negated_opcode(index_condition.operator_type);
  switch(negated_op) {
    case OP_Eq:
      instructions.push_back(make_eq_label(current_key_reg, index_key_reg, "end"));
      break;
    case OP_Ne:
      instructions.push_back(make_ne_label(current_key_reg, index_key_reg, "end"));
      break;
    case OP_Lt:
      instructions.push_back(make_lt_label(current_key_reg, index_key_reg, "end"));
      break;
    case OP_Le:
      instructions.push_back(make_le_label(current_key_reg, index_key_reg, "end"));
      break;
    case OP_Gt:
      instructions.push_back(make_gt_label(current_key_reg, index_key_reg, "end"));
      break;
    case OP_Ge:
      instructions.push_back(make_ge_label(current_key_reg, index_key_reg, "end"));
      break;
  }

  int rowid_reg = regs.get("rowid");
  instructions.push_back(make_column(index_cursor_id, 0, rowid_reg));

  instructions.push_back(make_seek_eq_label(table_cursor_id, rowid_reg, "next_iteration"));

  build_where_checks(instructions, table_cursor_id, remaining_conditions, "next_iteration", regs);

  // Perform operation
  if (operation == UnifiedOptions::DELETE) {
    instructions.push_back(make_delete(table_cursor_id));
  } else if (operation == UnifiedOptions::AGGREGATE) {
    if (strcmp(aggregate_func, "COUNT") != 0 && aggregate_column != nullptr) {
      int value_reg = regs.get("agg_value");
      instructions.push_back(make_column(table_cursor_id, (int32_t)*aggregate_column, value_reg));
      instructions.push_back(make_agg_step(value_reg));
    } else {
      instructions.push_back(make_agg_step());
    }
  } else if (operation == UnifiedOptions::SELECT) {
    std::vector<int> output_regs;
    std::vector<uint32_t> columns_to_select;

    if (select_columns) {
      columns_to_select = *select_columns;
    } else {
      for (size_t i = 0; i < schema.size(); i++) {
        columns_to_select.push_back(i);
      }
    }

    for (size_t i = 0; i < columns_to_select.size(); i++) {
      int col_reg = regs.get("output_col_" + std::to_string(i));
      output_regs.push_back(col_reg);
      instructions.push_back(make_column(table_cursor_id, (int32_t)columns_to_select[i], col_reg));
    }

    instructions.push_back(make_result_row(output_regs[0], (int32_t)output_regs.size()));
  } else {
    // UPDATE
    std::vector<int> current_regs;
    for (size_t i = 0; i < schema.size(); i++) {
      int col_reg = regs.get("current_col_" + std::to_string(i));
      current_regs.push_back(col_reg);
      instructions.push_back(make_column(table_cursor_id, (int32_t)i, col_reg));

      if (indexes_to_update.find(i) != indexes_to_update.end()) {
        const auto &idx_pair = indexes_to_update[i];
        if (idx_pair.second != index_cursor_id) {
          instructions.push_back(make_seek_eq_label(idx_pair.second, col_reg, "end"));
        }
      }
    }

    for (const auto &set_col : set_columns) {
      int reg = regs.get("update_col_" + std::to_string(set_col.column_index));
      load_value(instructions, set_col.value, reg);
      instructions.push_back(make_move(reg, current_regs[set_col.column_index]));

      if (indexes_to_update.find(set_col.column_index) != indexes_to_update.end()) {
        const auto &idx_pair = indexes_to_update[set_col.column_index];
        instructions.push_back(make_delete(idx_pair.second));
        instructions.push_back(make_insert(idx_pair.second, current_regs[set_col.column_index], rowid_reg));
      }
    }

    int record_reg = regs.get("record");
    instructions.push_back(make_record(current_regs[0], (int32_t)schema.size(), record_reg));
    instructions.push_back(make_insert(table_cursor_id, rowid_reg, record_reg));
  }

  labels["next_iteration"] = instructions.size();

  bool use_next = ascending(index_condition.operator_type);
  if (use_next) {
    instructions.push_back(make_next_label(index_cursor_id, "loop_start"));
  } else {
    instructions.push_back(make_prev_label(index_cursor_id, "loop_start"));
  }

  labels["end"] = instructions.size();

  if (operation == UnifiedOptions::AGGREGATE) {
    int output_reg = regs.get("output");
    instructions.push_back(make_agg_final(output_reg));
    instructions.push_back(make_result_row(output_reg, 1));
  }

  instructions.push_back(make_close(index_cursor_id));
  instructions.push_back(make_close(table_cursor_id));

  if (operation == UnifiedOptions::UPDATE) {
    for (const auto &[ci, idx_pair] : indexes_to_update) {
      instructions.push_back(make_close(idx_pair.second));
    }
  }

  resolve_labels(instructions, labels);
  return instructions;
}
