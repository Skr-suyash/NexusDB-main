#pragma once

#include <string>
#include <vector>
#include <variant>
#include <optional>
#include "mosaicdb/types.h"
#include "mosaicdb/schema.h"
#include "mosaicdb/sql_tokenizer.h"

namespace mosaicdb {

// WHERE clause condition
enum class CompOp { EQ, NE, LT, GT, LE, GE };
enum class LogicOp { AND, OR };

struct Condition {
    std::string column;
    CompOp op;
    Value value;
};

struct WhereClause {
    std::vector<Condition> conditions;
    std::vector<LogicOp> logic_ops; // between conditions: conditions[0] logic_ops[0] conditions[1] ...
    bool empty() const { return conditions.empty(); }
};

struct Assignment {
    std::string column;
    Value value;
};

// AST node types
struct CreateTableStmt {
    std::string table_name;
    std::vector<ColumnDef> columns;
    std::string primary_key;
};

struct DropTableStmt {
    std::string table_name;
    bool if_exists = false;
};

struct InsertStmt {
    std::string table_name;
    std::vector<std::string> columns;
    std::vector<Value> values;
};

struct SelectStmt {
    std::string table_name;
    std::vector<std::string> columns; // empty = SELECT *
    bool select_all = false;
    WhereClause where;
    std::string order_by;
    bool order_desc = false;
    int limit = -1;
};

struct UpdateStmt {
    std::string table_name;
    std::vector<Assignment> assignments;
    WhereClause where;
};

struct DeleteStmt {
    std::string table_name;
    WhereClause where;
};

struct ShowTablesStmt {};
struct DescribeStmt { std::string table_name; };

using Statement = std::variant<
    CreateTableStmt, DropTableStmt, InsertStmt,
    SelectStmt, UpdateStmt, DeleteStmt,
    ShowTablesStmt, DescribeStmt
>;

class SQLParser {
public:
    static Statement parse(const std::string& sql);

private:
    explicit SQLParser(const std::vector<Token>& tokens);

    const Token& current() const;
    const Token& peek_next() const;
    bool match(TokenType type) const;
    const Token& expect(TokenType type);
    void advance();
    bool at_end() const;

    Statement parse_statement();
    CreateTableStmt parse_create_table();
    DropTableStmt parse_drop_table();
    InsertStmt parse_insert();
    SelectStmt parse_select();
    UpdateStmt parse_update();
    DeleteStmt parse_delete();
    ShowTablesStmt parse_show_tables();
    DescribeStmt parse_describe();
    WhereClause parse_where();
    Condition parse_condition();
    Value parse_value();
    ColumnType parse_column_type();

    std::vector<Token> tokens_;
    size_t pos_;
};

}
