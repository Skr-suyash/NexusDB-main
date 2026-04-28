#include "mosaicdb/sql_tokenizer.h"
#include <algorithm>
#include <stdexcept>
#include <cctype>

namespace mosaicdb {

SQLTokenizer::SQLTokenizer(const std::string& input) : input_(input), pos_(0) {}

char SQLTokenizer::peek() const {
    if (pos_ >= input_.size()) return '\0';
    return input_[pos_];
}

char SQLTokenizer::advance() {
    if (pos_ >= input_.size()) return '\0';
    return input_[pos_++];
}

void SQLTokenizer::skip_whitespace() {
    while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_])))
        ++pos_;
}

TokenType SQLTokenizer::keyword_type(const std::string& word) const {
    std::string upper = word;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    if (upper == "CREATE")   return TokenType::KW_CREATE;
    if (upper == "TABLE")    return TokenType::KW_TABLE;
    if (upper == "DROP")     return TokenType::KW_DROP;
    if (upper == "INSERT")   return TokenType::KW_INSERT;
    if (upper == "INTO")     return TokenType::KW_INTO;
    if (upper == "VALUES")   return TokenType::KW_VALUES;
    if (upper == "SELECT")   return TokenType::KW_SELECT;
    if (upper == "FROM")     return TokenType::KW_FROM;
    if (upper == "WHERE")    return TokenType::KW_WHERE;
    if (upper == "UPDATE")   return TokenType::KW_UPDATE;
    if (upper == "SET")      return TokenType::KW_SET;
    if (upper == "DELETE")   return TokenType::KW_DELETE;
    if (upper == "AND")      return TokenType::KW_AND;
    if (upper == "OR")       return TokenType::KW_OR;
    if (upper == "NOT")      return TokenType::KW_NOT;
    if (upper == "NULL")     return TokenType::KW_NULL;
    if (upper == "PRIMARY")  return TokenType::KW_PRIMARY;
    if (upper == "KEY")      return TokenType::KW_KEY;
    if (upper == "INT")      return TokenType::KW_INT;
    if (upper == "INTEGER")  return TokenType::KW_INTEGER;
    if (upper == "FLOAT")    return TokenType::KW_FLOAT;
    if (upper == "REAL")     return TokenType::KW_REAL;
    if (upper == "DOUBLE")   return TokenType::KW_DOUBLE;
    if (upper == "TEXT")     return TokenType::KW_TEXT;
    if (upper == "STRING")   return TokenType::KW_STRING;
    if (upper == "VARCHAR")  return TokenType::KW_VARCHAR;
    if (upper == "BOOL")     return TokenType::KW_BOOL;
    if (upper == "BOOLEAN")  return TokenType::KW_BOOLEAN;
    if (upper == "TRUE")     return TokenType::KW_TRUE;
    if (upper == "FALSE")    return TokenType::KW_FALSE;
    if (upper == "IF")       return TokenType::KW_IF;
    if (upper == "EXISTS")   return TokenType::KW_EXISTS;
    if (upper == "ORDER")    return TokenType::KW_ORDER;
    if (upper == "BY")       return TokenType::KW_BY;
    if (upper == "ASC")      return TokenType::KW_ASC;
    if (upper == "DESC")     return TokenType::KW_DESC;
    if (upper == "LIMIT")    return TokenType::KW_LIMIT;
    if (upper == "SHOW")     return TokenType::KW_SHOW;
    if (upper == "TABLES")   return TokenType::KW_TABLES;
    if (upper == "DESCRIBE") return TokenType::KW_DESCRIBE;

    return TokenType::IDENTIFIER;
}

Token SQLTokenizer::read_number() {
    size_t start = pos_;
    bool has_dot = false;

    // Handle negative numbers
    if (peek() == '-') advance();

    while (pos_ < input_.size()) {
        char c = peek();
        if (c == '.') {
            if (has_dot) break;
            has_dot = true;
            advance();
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            advance();
        } else {
            break;
        }
    }

    std::string val = input_.substr(start, pos_ - start);
    return {has_dot ? TokenType::FLOAT_LIT : TokenType::INTEGER_LIT, val, start};
}

Token SQLTokenizer::read_string() {
    size_t start = pos_;
    char quote = advance(); // consume opening quote
    std::string val;

    while (pos_ < input_.size()) {
        char c = advance();
        if (c == quote) {
            // Check for escaped quote ('')
            if (peek() == quote) {
                val.push_back(quote);
                advance();
            } else {
                break;
            }
        } else {
            val.push_back(c);
        }
    }

    return {TokenType::STRING_LIT, val, start};
}

Token SQLTokenizer::read_identifier_or_keyword() {
    size_t start = pos_;
    while (pos_ < input_.size()) {
        char c = peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            advance();
        } else {
            break;
        }
    }

    std::string word = input_.substr(start, pos_ - start);
    TokenType type = keyword_type(word);

    // Store the original case for identifiers, uppercase for keywords
    if (type == TokenType::IDENTIFIER) {
        return {type, word, start};
    }
    std::string upper = word;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    return {type, upper, start};
}

std::vector<Token> SQLTokenizer::tokenize() {
    std::vector<Token> tokens;

    while (pos_ < input_.size()) {
        skip_whitespace();
        if (pos_ >= input_.size()) break;

        char c = peek();
        size_t start = pos_;

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            tokens.push_back(read_identifier_or_keyword());
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            tokens.push_back(read_number());
        } else if (c == '-' && pos_ + 1 < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[pos_ + 1]))) {
            tokens.push_back(read_number());
        } else if (c == '\'' || c == '"') {
            tokens.push_back(read_string());
        } else if (c == '(') {
            advance();
            tokens.push_back({TokenType::LPAREN, "(", start});
        } else if (c == ')') {
            advance();
            tokens.push_back({TokenType::RPAREN, ")", start});
        } else if (c == ',') {
            advance();
            tokens.push_back({TokenType::COMMA, ",", start});
        } else if (c == ';') {
            advance();
            tokens.push_back({TokenType::SEMICOLON, ";", start});
        } else if (c == '*') {
            advance();
            tokens.push_back({TokenType::STAR, "*", start});
        } else if (c == '.') {
            advance();
            tokens.push_back({TokenType::DOT, ".", start});
        } else if (c == '=') {
            advance();
            tokens.push_back({TokenType::OP_EQ, "=", start});
        } else if (c == '<') {
            advance();
            if (peek() == '=') {
                advance();
                tokens.push_back({TokenType::OP_LE, "<=", start});
            } else if (peek() == '>') {
                advance();
                tokens.push_back({TokenType::OP_NE, "<>", start});
            } else {
                tokens.push_back({TokenType::OP_LT, "<", start});
            }
        } else if (c == '>') {
            advance();
            if (peek() == '=') {
                advance();
                tokens.push_back({TokenType::OP_GE, ">=", start});
            } else {
                tokens.push_back({TokenType::OP_GT, ">", start});
            }
        } else if (c == '!') {
            advance();
            if (peek() == '=') {
                advance();
                tokens.push_back({TokenType::OP_NE, "!=", start});
            } else {
                throw std::runtime_error("Unexpected character '!' at position " + std::to_string(start));
            }
        } else {
            throw std::runtime_error("Unexpected character '" + std::string(1, c) + "' at position " + std::to_string(start));
        }
    }

    tokens.push_back({TokenType::END_OF_INPUT, "", pos_});
    return tokens;
}

}
