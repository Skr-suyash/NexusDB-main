#pragma once

#include <string>
#include <vector>
#include "mosaicdb/types.h"
#include "mosaicdb/schema.h"
#include "mosaicdb/sql_parser.h"
#include "mosaicdb/catalog.h"
#include "mosaicdb/storage_engine.h"

namespace mosaicdb {

struct QueryResult {
    Status status = Status::OK;
    std::string message;
    std::vector<std::string> column_names;
    std::vector<std::vector<Value>> rows;

    bool is_error() const { return status != Status::OK; }

    static QueryResult ok(const std::string& msg) {
        return {Status::OK, msg, {}, {}};
    }
    static QueryResult error(const std::string& msg) {
        return {Status::INVALID_ARGUMENT, msg, {}, {}};
    }
};

class Executor {
public:
    Executor(StorageEngine& engine, Catalog& catalog);

    QueryResult execute(const Statement& stmt);
    QueryResult execute_sql(const std::string& sql);

private:
    StorageEngine& engine_;
    Catalog& catalog_;

    QueryResult exec_create_table(const CreateTableStmt& stmt);
    QueryResult exec_drop_table(const DropTableStmt& stmt);
    QueryResult exec_insert(const InsertStmt& stmt);
    QueryResult exec_select(const SelectStmt& stmt);
    QueryResult exec_update(const UpdateStmt& stmt);
    QueryResult exec_delete(const DeleteStmt& stmt);
    QueryResult exec_show_tables(const ShowTablesStmt& stmt);
    QueryResult exec_describe(const DescribeStmt& stmt);

    bool evaluate_where(const WhereClause& where, const TableSchema& schema,
                        const std::vector<Value>& row);
    bool evaluate_condition(const Condition& cond, const TableSchema& schema,
                            const std::vector<Value>& row);

    // Coerce a parsed Value to match a column's type
    Value coerce_value(const Value& val, ColumnType target_type);

    // Format primary key as a padded string for KV key ordering
    std::string format_pk(const Value& val);
};

}
