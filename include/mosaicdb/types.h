#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace mosaicdb {

enum class ColumnType : uint8_t {
    INT   = 0x01,
    FLOAT = 0x02,
    TEXT  = 0x03,
    BOOL  = 0x04
};

inline std::string column_type_to_string(ColumnType t) {
    switch (t) {
        case ColumnType::INT:   return "INT";
        case ColumnType::FLOAT: return "FLOAT";
        case ColumnType::TEXT:  return "TEXT";
        case ColumnType::BOOL:  return "BOOL";
    }
    return "UNKNOWN";
}

inline ColumnType string_to_column_type(const std::string& s) {
    if (s == "INT" || s == "INTEGER") return ColumnType::INT;
    if (s == "FLOAT" || s == "DOUBLE" || s == "REAL") return ColumnType::FLOAT;
    if (s == "TEXT" || s == "STRING" || s == "VARCHAR") return ColumnType::TEXT;
    if (s == "BOOL" || s == "BOOLEAN") return ColumnType::BOOL;
    throw std::runtime_error("Unknown column type: " + s);
}

struct Value {
    ColumnType type;
    bool is_null = false;
    int64_t int_val = 0;
    double float_val = 0.0;
    std::string text_val;
    bool bool_val = false;

    static Value make_int(int64_t v) {
        Value val; val.type = ColumnType::INT; val.int_val = v; return val;
    }
    static Value make_float(double v) {
        Value val; val.type = ColumnType::FLOAT; val.float_val = v; return val;
    }
    static Value make_text(const std::string& v) {
        Value val; val.type = ColumnType::TEXT; val.text_val = v; return val;
    }
    static Value make_bool(bool v) {
        Value val; val.type = ColumnType::BOOL; val.bool_val = v; return val;
    }
    static Value make_null(ColumnType t) {
        Value val; val.type = t; val.is_null = true; return val;
    }

    std::string to_string() const {
        if (is_null) return "NULL";
        switch (type) {
            case ColumnType::INT:   return std::to_string(int_val);
            case ColumnType::FLOAT: {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2) << float_val;
                return oss.str();
            }
            case ColumnType::TEXT:  return text_val;
            case ColumnType::BOOL:  return bool_val ? "TRUE" : "FALSE";
        }
        return "?";
    }

    bool compare_eq(const Value& other) const {
        if (is_null || other.is_null) return false;
        switch (type) {
            case ColumnType::INT:   return int_val == other.int_val;
            case ColumnType::FLOAT: return float_val == other.float_val;
            case ColumnType::TEXT:  return text_val == other.text_val;
            case ColumnType::BOOL:  return bool_val == other.bool_val;
        }
        return false;
    }

    bool compare_lt(const Value& other) const {
        if (is_null || other.is_null) return false;
        switch (type) {
            case ColumnType::INT:   return int_val < other.int_val;
            case ColumnType::FLOAT: return float_val < other.float_val;
            case ColumnType::TEXT:  return text_val < other.text_val;
            case ColumnType::BOOL:  return false;
        }
        return false;
    }

    bool compare_gt(const Value& other) const {
        if (is_null || other.is_null) return false;
        switch (type) {
            case ColumnType::INT:   return int_val > other.int_val;
            case ColumnType::FLOAT: return float_val > other.float_val;
            case ColumnType::TEXT:  return text_val > other.text_val;
            case ColumnType::BOOL:  return false;
        }
        return false;
    }
};

}
