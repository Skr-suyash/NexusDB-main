#include "mosaicdb/row.h"
#include "mosaicdb/common.h"
#include <cstring>

namespace mosaicdb {

std::string RowSerializer::serialize(const TableSchema& schema, const std::vector<Value>& values) {
    std::string out;
    uint8_t buf[8];

    // Column count
    encode_u32_le(buf, static_cast<uint32_t>(values.size()));
    out.append(reinterpret_cast<char*>(buf), 4);

    for (size_t i = 0; i < values.size(); ++i) {
        const auto& val = values[i];

        // Type byte
        out.push_back(static_cast<char>(val.type));
        // Null flag
        out.push_back(val.is_null ? 1 : 0);

        if (val.is_null) {
            // No value bytes for NULL
            encode_u32_le(buf, 0);
            out.append(reinterpret_cast<char*>(buf), 4);
            continue;
        }

        switch (val.type) {
            case ColumnType::INT: {
                encode_u32_le(buf, 8);
                out.append(reinterpret_cast<char*>(buf), 4);
                encode_u64_le(buf, static_cast<uint64_t>(val.int_val));
                out.append(reinterpret_cast<char*>(buf), 8);
                break;
            }
            case ColumnType::FLOAT: {
                encode_u32_le(buf, 8);
                out.append(reinterpret_cast<char*>(buf), 4);
                uint64_t raw;
                std::memcpy(&raw, &val.float_val, 8);
                encode_u64_le(buf, raw);
                out.append(reinterpret_cast<char*>(buf), 8);
                break;
            }
            case ColumnType::TEXT: {
                encode_u32_le(buf, static_cast<uint32_t>(val.text_val.size()));
                out.append(reinterpret_cast<char*>(buf), 4);
                out.append(val.text_val);
                break;
            }
            case ColumnType::BOOL: {
                encode_u32_le(buf, 1);
                out.append(reinterpret_cast<char*>(buf), 4);
                out.push_back(val.bool_val ? 1 : 0);
                break;
            }
        }
    }

    return out;
}

std::vector<Value> RowSerializer::deserialize(const TableSchema& schema, const std::string& data) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
    size_t pos = 0;

    uint32_t col_count = decode_u32_le(p + pos);
    pos += 4;

    std::vector<Value> values;
    values.reserve(col_count);

    for (uint32_t i = 0; i < col_count; ++i) {
        ColumnType type = static_cast<ColumnType>(p[pos++]);
        bool is_null = (p[pos++] != 0);
        uint32_t val_len = decode_u32_le(p + pos);
        pos += 4;

        if (is_null) {
            values.push_back(Value::make_null(type));
            continue;
        }

        switch (type) {
            case ColumnType::INT: {
                int64_t v = static_cast<int64_t>(decode_u64_le(p + pos));
                pos += 8;
                values.push_back(Value::make_int(v));
                break;
            }
            case ColumnType::FLOAT: {
                uint64_t raw = decode_u64_le(p + pos);
                pos += 8;
                double v;
                std::memcpy(&v, &raw, 8);
                values.push_back(Value::make_float(v));
                break;
            }
            case ColumnType::TEXT: {
                std::string v(reinterpret_cast<const char*>(p + pos), val_len);
                pos += val_len;
                values.push_back(Value::make_text(v));
                break;
            }
            case ColumnType::BOOL: {
                bool v = (p[pos] != 0);
                pos += 1;
                values.push_back(Value::make_bool(v));
                break;
            }
        }
    }

    return values;
}

}
