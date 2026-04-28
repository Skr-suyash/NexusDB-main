#include "mosaicdb/executor.h"
#include "mosaicdb/row.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdio>

namespace mosaicdb {

Executor::Executor(StorageEngine& engine, Catalog& catalog)
    : engine_(engine), catalog_(catalog) {}

std::string Executor::format_pk(const Value& val) {
    // Pad integer PKs to 10 digits for proper lexicographic ordering in KV store
    if (val.type == ColumnType::INT) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%010lld", static_cast<long long>(val.int_val));
        return buf;
    }
    return val.to_string();
}

Value Executor::coerce_value(const Value& val, ColumnType target_type) {
    if (val.is_null) return Value::make_null(target_type);
    if (val.type == target_type) return val;

    // INT -> FLOAT
    if (val.type == ColumnType::INT && target_type == ColumnType::FLOAT) {
        return Value::make_float(static_cast<double>(val.int_val));
    }
    // FLOAT -> INT
    if (val.type == ColumnType::FLOAT && target_type == ColumnType::INT) {
        return Value::make_int(static_cast<int64_t>(val.float_val));
    }
    // TEXT -> INT
    if (val.type == ColumnType::TEXT && target_type == ColumnType::INT) {
        try { return Value::make_int(std::stoll(val.text_val)); }
        catch (...) { return val; }
    }
    // TEXT -> FLOAT
    if (val.type == ColumnType::TEXT && target_type == ColumnType::FLOAT) {
        try { return Value::make_float(std::stod(val.text_val)); }
        catch (...) { return val; }
    }
    // anything -> TEXT
    if (target_type == ColumnType::TEXT) {
        return Value::make_text(val.to_string());
    }

    return val;
}

QueryResult Executor::execute_sql(const std::string& sql) {
    try {
        Statement stmt = SQLParser::parse(sql);
        return execute(stmt);
    } catch (const std::exception& e) {
        return QueryResult::error(std::string("Parse error: ") + e.what());
    }
}

QueryResult Executor::execute(const Statement& stmt) {
    try {
        if (auto* s = std::get_if<CreateTableStmt>(&stmt)) return exec_create_table(*s);
        if (auto* s = std::get_if<DropTableStmt>(&stmt))   return exec_drop_table(*s);
        if (auto* s = std::get_if<InsertStmt>(&stmt))      return exec_insert(*s);
        if (auto* s = std::get_if<SelectStmt>(&stmt))      return exec_select(*s);
        if (auto* s = std::get_if<UpdateStmt>(&stmt))      return exec_update(*s);
        if (auto* s = std::get_if<DeleteStmt>(&stmt))      return exec_delete(*s);
        if (auto* s = std::get_if<ShowTablesStmt>(&stmt))  return exec_show_tables(*s);
        if (auto* s = std::get_if<DescribeStmt>(&stmt))    return exec_describe(*s);
        return QueryResult::error("Unknown statement type");
    } catch (const std::exception& e) {
        return QueryResult::error(std::string("Execution error: ") + e.what());
    }
}

QueryResult Executor::exec_create_table(const CreateTableStmt& stmt) {
    if (stmt.table_name.empty())
        return QueryResult::error("Table name cannot be empty");
    if (stmt.columns.empty())
        return QueryResult::error("Table must have at least one column");
    if (stmt.primary_key.empty())
        return QueryResult::error("Table must have a PRIMARY KEY");

    TableSchema schema;
    schema.table_name = stmt.table_name;
    schema.columns = stmt.columns;
    schema.primary_key = stmt.primary_key;

    Status s = catalog_.create_table(schema);
    if (s == Status::INVALID_ARGUMENT)
        return QueryResult::error("Table '" + stmt.table_name + "' already exists");
    if (s != Status::OK)
        return QueryResult::error("Failed to create table: " + status_to_string(s));

    return QueryResult::ok("Table '" + stmt.table_name + "' created.");
}

QueryResult Executor::exec_drop_table(const DropTableStmt& stmt) {
    if (!catalog_.table_exists(stmt.table_name)) {
        if (stmt.if_exists) return QueryResult::ok("OK");
        return QueryResult::error("Table '" + stmt.table_name + "' does not exist");
    }

    Status s = catalog_.drop_table(stmt.table_name);
    if (s != Status::OK)
        return QueryResult::error("Failed to drop table: " + status_to_string(s));

    return QueryResult::ok("Table '" + stmt.table_name + "' dropped.");
}

QueryResult Executor::exec_insert(const InsertStmt& stmt) {
    auto schema_opt = catalog_.get_schema(stmt.table_name);
    if (!schema_opt)
        return QueryResult::error("Table '" + stmt.table_name + "' does not exist");

    const auto& schema = *schema_opt;

    // Determine column order
    std::vector<std::string> cols = stmt.columns;
    if (cols.empty()) {
        for (const auto& c : schema.columns) cols.push_back(c.name);
    }

    if (cols.size() != stmt.values.size())
        return QueryResult::error("Column count (" + std::to_string(cols.size()) +
                                  ") doesn't match value count (" +
                                  std::to_string(stmt.values.size()) + ")");

    // Build the row values in schema column order
    std::vector<Value> row_values(schema.columns.size());
    for (size_t i = 0; i < schema.columns.size(); ++i) {
        row_values[i] = Value::make_null(schema.columns[i].type);
    }

    Value pk_value;
    bool has_pk = false;

    for (size_t i = 0; i < cols.size(); ++i) {
        int col_idx = schema.find_column(cols[i]);
        if (col_idx < 0)
            return QueryResult::error("Unknown column '" + cols[i] + "' in table '" + stmt.table_name + "'");

        Value coerced = coerce_value(stmt.values[i], schema.columns[col_idx].type);
        row_values[col_idx] = coerced;

        if (cols[i] == schema.primary_key) {
            pk_value = coerced;
            has_pk = true;
        }
    }

    // Check NOT NULL constraints
    for (size_t i = 0; i < schema.columns.size(); ++i) {
        if (schema.columns[i].not_null && row_values[i].is_null)
            return QueryResult::error("Column '" + schema.columns[i].name + "' cannot be NULL");
    }

    // Auto-generate PK if not provided
    if (!has_pk) {
        int pk_idx = schema.find_column(schema.primary_key);
        if (pk_idx >= 0 && schema.columns[pk_idx].type == ColumnType::INT) {
            uint64_t id = catalog_.next_row_id(stmt.table_name);
            pk_value = Value::make_int(static_cast<int64_t>(id));
            row_values[pk_idx] = pk_value;
            has_pk = true;
        } else {
            return QueryResult::error("PRIMARY KEY '" + schema.primary_key + "' value required");
        }
    }

    // Check for duplicate PK
    std::string kv_key = Catalog::row_key(stmt.table_name, format_pk(pk_value));
    auto [gs, gv] = engine_.get(kv_key);
    if (gs == Status::OK)
        return QueryResult::error("Duplicate PRIMARY KEY value: " + pk_value.to_string());

    // Serialize and store
    std::string row_data = RowSerializer::serialize(schema, row_values);
    Status s = engine_.put(kv_key, row_data);
    if (s != Status::OK)
        return QueryResult::error("Failed to insert row: " + status_to_string(s));

    return QueryResult::ok("1 row inserted.");
}

QueryResult Executor::exec_select(const SelectStmt& stmt) {
    auto schema_opt = catalog_.get_schema(stmt.table_name);
    if (!schema_opt)
        return QueryResult::error("Table '" + stmt.table_name + "' does not exist");

    const auto& schema = *schema_opt;

    // Determine which columns to return
    std::vector<int> col_indices;
    std::vector<std::string> col_names;

    if (stmt.select_all || stmt.columns.empty()) {
        for (size_t i = 0; i < schema.columns.size(); ++i) {
            col_indices.push_back(static_cast<int>(i));
            col_names.push_back(schema.columns[i].name);
        }
    } else {
        for (const auto& name : stmt.columns) {
            int idx = schema.find_column(name);
            if (idx < 0)
                return QueryResult::error("Unknown column '" + name + "'");
            col_indices.push_back(idx);
            col_names.push_back(name);
        }
    }

    // Scan all rows for this table
    auto kv_results = engine_.scan(
        Catalog::row_key_prefix(stmt.table_name),
        Catalog::row_key_prefix_end(stmt.table_name)
    );

    std::vector<std::vector<Value>> result_rows;

    for (const auto& [key, value] : kv_results) {
        // Skip internal keys
        if (key.find(":row:") == std::string::npos) continue;

        auto row = RowSerializer::deserialize(schema, value);

        // Apply WHERE filter
        if (!stmt.where.empty() && !evaluate_where(stmt.where, schema, row))
            continue;

        // Project selected columns
        std::vector<Value> projected;
        for (int idx : col_indices) {
            if (idx < static_cast<int>(row.size())) {
                projected.push_back(row[idx]);
            } else {
                projected.push_back(Value::make_null(ColumnType::TEXT));
            }
        }
        result_rows.push_back(std::move(projected));
    }

    // ORDER BY
    if (!stmt.order_by.empty()) {
        int order_idx = -1;
        for (size_t i = 0; i < col_names.size(); ++i) {
            if (col_names[i] == stmt.order_by) {
                order_idx = static_cast<int>(i);
                break;
            }
        }
        // Also check full schema if not in projection
        if (order_idx < 0) {
            // Need to re-scan with full rows for ordering
            // For simplicity, only support ordering by selected columns
            return QueryResult::error("ORDER BY column '" + stmt.order_by + "' must be in SELECT list");
        }

        bool desc = stmt.order_desc;
        std::sort(result_rows.begin(), result_rows.end(),
            [order_idx, desc](const std::vector<Value>& a, const std::vector<Value>& b) {
                if (desc) return a[order_idx].compare_gt(b[order_idx]);
                return a[order_idx].compare_lt(b[order_idx]);
            });
    }

    // LIMIT
    if (stmt.limit >= 0 && static_cast<size_t>(stmt.limit) < result_rows.size()) {
        result_rows.resize(stmt.limit);
    }

    QueryResult result;
    result.status = Status::OK;
    result.message = std::to_string(result_rows.size()) + " row(s) returned.";
    result.column_names = col_names;
    result.rows = std::move(result_rows);
    return result;
}

QueryResult Executor::exec_update(const UpdateStmt& stmt) {
    auto schema_opt = catalog_.get_schema(stmt.table_name);
    if (!schema_opt)
        return QueryResult::error("Table '" + stmt.table_name + "' does not exist");

    const auto& schema = *schema_opt;

    auto kv_results = engine_.scan(
        Catalog::row_key_prefix(stmt.table_name),
        Catalog::row_key_prefix_end(stmt.table_name)
    );

    int updated_count = 0;

    for (const auto& [key, value] : kv_results) {
        if (key.find(":row:") == std::string::npos) continue;

        auto row = RowSerializer::deserialize(schema, value);

        if (!stmt.where.empty() && !evaluate_where(stmt.where, schema, row))
            continue;

        // Apply assignments
        for (const auto& assign : stmt.assignments) {
            int col_idx = schema.find_column(assign.column);
            if (col_idx < 0)
                return QueryResult::error("Unknown column '" + assign.column + "'");
            row[col_idx] = coerce_value(assign.value, schema.columns[col_idx].type);
        }

        // Serialize and update
        std::string new_data = RowSerializer::serialize(schema, row);
        engine_.put(key, new_data);
        ++updated_count;
    }

    return QueryResult::ok(std::to_string(updated_count) + " row(s) updated.");
}

QueryResult Executor::exec_delete(const DeleteStmt& stmt) {
    auto schema_opt = catalog_.get_schema(stmt.table_name);
    if (!schema_opt)
        return QueryResult::error("Table '" + stmt.table_name + "' does not exist");

    const auto& schema = *schema_opt;

    auto kv_results = engine_.scan(
        Catalog::row_key_prefix(stmt.table_name),
        Catalog::row_key_prefix_end(stmt.table_name)
    );

    std::vector<std::string> keys_to_delete;

    for (const auto& [key, value] : kv_results) {
        if (key.find(":row:") == std::string::npos) continue;

        auto row = RowSerializer::deserialize(schema, value);

        if (!stmt.where.empty() && !evaluate_where(stmt.where, schema, row))
            continue;

        keys_to_delete.push_back(key);
    }

    for (const auto& key : keys_to_delete) {
        engine_.remove(key);
    }

    return QueryResult::ok(std::to_string(keys_to_delete.size()) + " row(s) deleted.");
}

QueryResult Executor::exec_show_tables(const ShowTablesStmt&) {
    auto tables = catalog_.list_tables();
    std::sort(tables.begin(), tables.end());

    QueryResult result;
    result.status = Status::OK;
    result.column_names = {"table_name"};
    for (const auto& t : tables) {
        result.rows.push_back({Value::make_text(t)});
    }
    result.message = std::to_string(tables.size()) + " table(s).";
    return result;
}

QueryResult Executor::exec_describe(const DescribeStmt& stmt) {
    auto schema_opt = catalog_.get_schema(stmt.table_name);
    if (!schema_opt)
        return QueryResult::error("Table '" + stmt.table_name + "' does not exist");

    const auto& schema = *schema_opt;

    QueryResult result;
    result.status = Status::OK;
    result.column_names = {"column_name", "type", "not_null", "primary_key"};

    for (const auto& col : schema.columns) {
        std::vector<Value> row;
        row.push_back(Value::make_text(col.name));
        row.push_back(Value::make_text(column_type_to_string(col.type)));
        row.push_back(Value::make_text(col.not_null ? "YES" : "NO"));
        row.push_back(Value::make_text(col.is_primary_key ? "YES" : "NO"));
        result.rows.push_back(std::move(row));
    }

    result.message = std::to_string(schema.columns.size()) + " column(s).";
    return result;
}

bool Executor::evaluate_where(const WhereClause& where, const TableSchema& schema,
                               const std::vector<Value>& row) {
    if (where.empty()) return true;

    bool result = evaluate_condition(where.conditions[0], schema, row);

    for (size_t i = 0; i < where.logic_ops.size(); ++i) {
        bool next = evaluate_condition(where.conditions[i + 1], schema, row);
        if (where.logic_ops[i] == LogicOp::AND) {
            result = result && next;
        } else {
            result = result || next;
        }
    }

    return result;
}

bool Executor::evaluate_condition(const Condition& cond, const TableSchema& schema,
                                   const std::vector<Value>& row) {
    int col_idx = schema.find_column(cond.column);
    if (col_idx < 0 || col_idx >= static_cast<int>(row.size())) return false;

    const Value& cell = row[col_idx];
    Value compare_val = coerce_value(cond.value, cell.type);

    switch (cond.op) {
        case CompOp::EQ: return cell.compare_eq(compare_val);
        case CompOp::NE: return !cell.compare_eq(compare_val);
        case CompOp::LT: return cell.compare_lt(compare_val);
        case CompOp::GT: return cell.compare_gt(compare_val);
        case CompOp::LE: return cell.compare_eq(compare_val) || cell.compare_lt(compare_val);
        case CompOp::GE: return cell.compare_eq(compare_val) || cell.compare_gt(compare_val);
    }
    return false;
}

}
