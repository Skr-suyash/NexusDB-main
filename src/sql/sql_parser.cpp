#include "mosaicdb/sql_parser.h"
#include <stdexcept>
#include <algorithm>

namespace mosaicdb {

SQLParser::SQLParser(const std::vector<Token>& tokens) : tokens_(tokens), pos_(0) {}

const Token& SQLParser::current() const {
    return tokens_[pos_];
}

const Token& SQLParser::peek_next() const {
    if (pos_ + 1 >= tokens_.size()) return tokens_.back();
    return tokens_[pos_ + 1];
}

bool SQLParser::match(TokenType type) const {
    return current().type == type;
}

const Token& SQLParser::expect(TokenType type) {
    if (!match(type)) {
        throw std::runtime_error("SQL parse error: expected token type " +
            std::to_string(static_cast<int>(type)) + " but got '" + current().value +
            "' at position " + std::to_string(current().position));
    }
    const Token& tok = current();
    advance();
    return tok;
}

void SQLParser::advance() {
    if (pos_ < tokens_.size() - 1) ++pos_;
}

bool SQLParser::at_end() const {
    return match(TokenType::END_OF_INPUT) || match(TokenType::SEMICOLON);
}

Statement SQLParser::parse(const std::string& sql) {
    SQLTokenizer tokenizer(sql);
    auto tokens = tokenizer.tokenize();
    SQLParser parser(tokens);
    return parser.parse_statement();
}

Statement SQLParser::parse_statement() {
    if (match(TokenType::KW_CREATE)) return parse_create_table();
    if (match(TokenType::KW_DROP))   return parse_drop_table();
    if (match(TokenType::KW_INSERT)) return parse_insert();
    if (match(TokenType::KW_SELECT)) return parse_select();
    if (match(TokenType::KW_UPDATE)) return parse_update();
    if (match(TokenType::KW_DELETE)) return parse_delete();
    if (match(TokenType::KW_SHOW))   return parse_show_tables();
    if (match(TokenType::KW_DESCRIBE)) return parse_describe();

    throw std::runtime_error("SQL parse error: unexpected token '" + current().value + "'");
}

ColumnType SQLParser::parse_column_type() {
    TokenType t = current().type;
    advance();
    switch (t) {
        case TokenType::KW_INT:
        case TokenType::KW_INTEGER:
            return ColumnType::INT;
        case TokenType::KW_FLOAT:
        case TokenType::KW_REAL:
        case TokenType::KW_DOUBLE:
            return ColumnType::FLOAT;
        case TokenType::KW_TEXT:
        case TokenType::KW_STRING:
        case TokenType::KW_VARCHAR:
            // Skip optional (N) for VARCHAR
            if (match(TokenType::LPAREN)) {
                advance(); // skip (
                advance(); // skip N
                expect(TokenType::RPAREN);
            }
            return ColumnType::TEXT;
        case TokenType::KW_BOOL:
        case TokenType::KW_BOOLEAN:
            return ColumnType::BOOL;
        default:
            throw std::runtime_error("SQL parse error: expected column type, got '" + tokens_[pos_ - 1].value + "'");
    }
}

CreateTableStmt SQLParser::parse_create_table() {
    CreateTableStmt stmt;
    expect(TokenType::KW_CREATE);
    expect(TokenType::KW_TABLE);

    // Table name
    const Token& name_tok = expect(TokenType::IDENTIFIER);
    stmt.table_name = name_tok.value;

    expect(TokenType::LPAREN);

    // Parse columns
    while (!match(TokenType::RPAREN) && !at_end()) {
        // Check for PRIMARY KEY (table-level)
        if (match(TokenType::KW_PRIMARY)) {
            advance(); // PRIMARY
            expect(TokenType::KW_KEY);
            expect(TokenType::LPAREN);
            const Token& pk_tok = expect(TokenType::IDENTIFIER);
            stmt.primary_key = pk_tok.value;
            expect(TokenType::RPAREN);
        } else {
            ColumnDef col;
            const Token& col_name = expect(TokenType::IDENTIFIER);
            col.name = col_name.value;
            col.type = parse_column_type();

            // Check for constraints
            while (!match(TokenType::COMMA) && !match(TokenType::RPAREN) && !at_end()) {
                if (match(TokenType::KW_PRIMARY)) {
                    advance(); // PRIMARY
                    expect(TokenType::KW_KEY);
                    col.is_primary_key = true;
                    stmt.primary_key = col.name;
                } else if (match(TokenType::KW_NOT)) {
                    advance(); // NOT
                    expect(TokenType::KW_NULL);
                    col.not_null = true;
                } else {
                    break;
                }
            }

            stmt.columns.push_back(col);
        }

        if (match(TokenType::COMMA)) {
            advance();
        }
    }

    expect(TokenType::RPAREN);

    // If primary key is set, mark the corresponding column
    if (!stmt.primary_key.empty()) {
        for (auto& col : stmt.columns) {
            if (col.name == stmt.primary_key) {
                col.is_primary_key = true;
                col.not_null = true;
            }
        }
    }

    return stmt;
}

DropTableStmt SQLParser::parse_drop_table() {
    DropTableStmt stmt;
    expect(TokenType::KW_DROP);
    expect(TokenType::KW_TABLE);

    if (match(TokenType::KW_IF)) {
        advance(); // IF
        expect(TokenType::KW_EXISTS);
        stmt.if_exists = true;
    }

    const Token& name_tok = expect(TokenType::IDENTIFIER);
    stmt.table_name = name_tok.value;
    return stmt;
}

Value SQLParser::parse_value() {
    if (match(TokenType::INTEGER_LIT)) {
        int64_t v = std::stoll(current().value);
        advance();
        return Value::make_int(v);
    }
    if (match(TokenType::FLOAT_LIT)) {
        double v = std::stod(current().value);
        advance();
        return Value::make_float(v);
    }
    if (match(TokenType::STRING_LIT)) {
        std::string v = current().value;
        advance();
        return Value::make_text(v);
    }
    if (match(TokenType::KW_TRUE)) {
        advance();
        return Value::make_bool(true);
    }
    if (match(TokenType::KW_FALSE)) {
        advance();
        return Value::make_bool(false);
    }
    if (match(TokenType::KW_NULL)) {
        advance();
        return Value::make_null(ColumnType::TEXT);
    }

    throw std::runtime_error("SQL parse error: expected value, got '" + current().value + "'");
}

InsertStmt SQLParser::parse_insert() {
    InsertStmt stmt;
    expect(TokenType::KW_INSERT);
    expect(TokenType::KW_INTO);

    const Token& name_tok = expect(TokenType::IDENTIFIER);
    stmt.table_name = name_tok.value;

    // Optional column list
    if (match(TokenType::LPAREN)) {
        advance(); // (
        while (!match(TokenType::RPAREN) && !at_end()) {
            const Token& col = expect(TokenType::IDENTIFIER);
            stmt.columns.push_back(col.value);
            if (match(TokenType::COMMA)) advance();
        }
        expect(TokenType::RPAREN);
    }

    expect(TokenType::KW_VALUES);
    expect(TokenType::LPAREN);

    while (!match(TokenType::RPAREN) && !at_end()) {
        stmt.values.push_back(parse_value());
        if (match(TokenType::COMMA)) advance();
    }
    expect(TokenType::RPAREN);

    return stmt;
}

SelectStmt SQLParser::parse_select() {
    SelectStmt stmt;
    expect(TokenType::KW_SELECT);

    // Columns or *
    if (match(TokenType::STAR)) {
        stmt.select_all = true;
        advance();
    } else {
        while (true) {
            const Token& col = expect(TokenType::IDENTIFIER);
            stmt.columns.push_back(col.value);
            if (!match(TokenType::COMMA)) break;
            advance();
        }
    }

    expect(TokenType::KW_FROM);
    const Token& name_tok = expect(TokenType::IDENTIFIER);
    stmt.table_name = name_tok.value;

    // Optional WHERE
    if (match(TokenType::KW_WHERE)) {
        stmt.where = parse_where();
    }

    // Optional ORDER BY
    if (match(TokenType::KW_ORDER)) {
        advance(); // ORDER
        expect(TokenType::KW_BY);
        const Token& order_col = expect(TokenType::IDENTIFIER);
        stmt.order_by = order_col.value;
        if (match(TokenType::KW_DESC)) {
            stmt.order_desc = true;
            advance();
        } else if (match(TokenType::KW_ASC)) {
            advance();
        }
    }

    // Optional LIMIT
    if (match(TokenType::KW_LIMIT)) {
        advance();
        stmt.limit = std::stoi(current().value);
        advance();
    }

    return stmt;
}

UpdateStmt SQLParser::parse_update() {
    UpdateStmt stmt;
    expect(TokenType::KW_UPDATE);

    const Token& name_tok = expect(TokenType::IDENTIFIER);
    stmt.table_name = name_tok.value;

    expect(TokenType::KW_SET);

    // Parse assignments: col = value [, col = value ...]
    while (true) {
        Assignment assign;
        const Token& col = expect(TokenType::IDENTIFIER);
        assign.column = col.value;
        expect(TokenType::OP_EQ);
        assign.value = parse_value();
        stmt.assignments.push_back(assign);
        if (!match(TokenType::COMMA)) break;
        advance();
    }

    // Optional WHERE
    if (match(TokenType::KW_WHERE)) {
        stmt.where = parse_where();
    }

    return stmt;
}

DeleteStmt SQLParser::parse_delete() {
    DeleteStmt stmt;
    expect(TokenType::KW_DELETE);
    expect(TokenType::KW_FROM);

    const Token& name_tok = expect(TokenType::IDENTIFIER);
    stmt.table_name = name_tok.value;

    // Optional WHERE
    if (match(TokenType::KW_WHERE)) {
        stmt.where = parse_where();
    }

    return stmt;
}

WhereClause SQLParser::parse_where() {
    WhereClause wc;
    expect(TokenType::KW_WHERE);

    wc.conditions.push_back(parse_condition());

    while (match(TokenType::KW_AND) || match(TokenType::KW_OR)) {
        if (match(TokenType::KW_AND)) {
            wc.logic_ops.push_back(LogicOp::AND);
        } else {
            wc.logic_ops.push_back(LogicOp::OR);
        }
        advance();
        wc.conditions.push_back(parse_condition());
    }

    return wc;
}

Condition SQLParser::parse_condition() {
    Condition cond;
    const Token& col = expect(TokenType::IDENTIFIER);
    cond.column = col.value;

    // Parse operator
    TokenType op_type = current().type;
    switch (op_type) {
        case TokenType::OP_EQ: cond.op = CompOp::EQ; break;
        case TokenType::OP_NE: cond.op = CompOp::NE; break;
        case TokenType::OP_LT: cond.op = CompOp::LT; break;
        case TokenType::OP_GT: cond.op = CompOp::GT; break;
        case TokenType::OP_LE: cond.op = CompOp::LE; break;
        case TokenType::OP_GE: cond.op = CompOp::GE; break;
        default:
            throw std::runtime_error("SQL parse error: expected comparison operator, got '" + current().value + "'");
    }
    advance();

    cond.value = parse_value();
    return cond;
}

ShowTablesStmt SQLParser::parse_show_tables() {
    expect(TokenType::KW_SHOW);
    expect(TokenType::KW_TABLES);
    return ShowTablesStmt{};
}

DescribeStmt SQLParser::parse_describe() {
    DescribeStmt stmt;
    expect(TokenType::KW_DESCRIBE);
    const Token& name_tok = expect(TokenType::IDENTIFIER);
    stmt.table_name = name_tok.value;
    return stmt;
}

}
