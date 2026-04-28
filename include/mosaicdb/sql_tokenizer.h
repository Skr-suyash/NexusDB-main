#pragma once

#include <string>
#include <vector>

namespace mosaicdb {

enum class TokenType {
    // Keywords
    KW_CREATE, KW_TABLE, KW_DROP, KW_INSERT, KW_INTO, KW_VALUES,
    KW_SELECT, KW_FROM, KW_WHERE, KW_UPDATE, KW_SET, KW_DELETE,
    KW_AND, KW_OR, KW_NOT, KW_NULL, KW_PRIMARY, KW_KEY,
    KW_INT, KW_FLOAT, KW_TEXT, KW_BOOL, KW_INTEGER, KW_REAL,
    KW_VARCHAR, KW_BOOLEAN, KW_STRING, KW_DOUBLE,
    KW_NOT_NULL, KW_TRUE, KW_FALSE, KW_IF, KW_EXISTS,
    KW_ORDER, KW_BY, KW_ASC, KW_DESC, KW_LIMIT,
    KW_SHOW, KW_TABLES, KW_DESCRIBE,

    // Literals
    INTEGER_LIT, FLOAT_LIT, STRING_LIT,

    // Identifiers
    IDENTIFIER,

    // Operators
    OP_EQ,     // =
    OP_NE,     // != or <>
    OP_LT,     // <
    OP_GT,     // >
    OP_LE,     // <=
    OP_GE,     // >=

    // Punctuation
    COMMA, LPAREN, RPAREN, SEMICOLON, STAR, DOT,

    // End
    END_OF_INPUT
};

struct Token {
    TokenType type;
    std::string value;
    size_t position;
};

class SQLTokenizer {
public:
    explicit SQLTokenizer(const std::string& input);
    std::vector<Token> tokenize();

private:
    std::string input_;
    size_t pos_;

    char peek() const;
    char advance();
    void skip_whitespace();
    Token read_number();
    Token read_string();
    Token read_identifier_or_keyword();
    TokenType keyword_type(const std::string& word) const;
};

}
