#pragma once

#include <string>
#include <vector>
#include <optional>
#include "mosaicdb/types.h"
#include "mosaicdb/common.h"

namespace mosaicdb {

struct ColumnDef {
    std::string name;
    ColumnType type;
    bool not_null = false;
    bool is_primary_key = false;
};

struct TableSchema {
    std::string table_name;
    std::vector<ColumnDef> columns;
    std::string primary_key;  // name of the PK column

    int find_column(const std::string& name) const {
        for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
            if (columns[i].name == name) return i;
        }
        return -1;
    }

    // Serialize schema to a byte string for KV storage
    std::string serialize() const {
        std::string out;
        // table_name_len(4) + table_name + pk_len(4) + pk + col_count(4)
        uint8_t buf[4];

        encode_u32_le(buf, static_cast<uint32_t>(table_name.size()));
        out.append(reinterpret_cast<char*>(buf), 4);
        out.append(table_name);

        encode_u32_le(buf, static_cast<uint32_t>(primary_key.size()));
        out.append(reinterpret_cast<char*>(buf), 4);
        out.append(primary_key);

        encode_u32_le(buf, static_cast<uint32_t>(columns.size()));
        out.append(reinterpret_cast<char*>(buf), 4);

        for (const auto& col : columns) {
            encode_u32_le(buf, static_cast<uint32_t>(col.name.size()));
            out.append(reinterpret_cast<char*>(buf), 4);
            out.append(col.name);
            out.push_back(static_cast<char>(col.type));
            out.push_back(col.not_null ? 1 : 0);
            out.push_back(col.is_primary_key ? 1 : 0);
        }

        return out;
    }

    // Deserialize schema from a byte string
    static TableSchema deserialize(const std::string& data) {
        TableSchema schema;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
        size_t pos = 0;

        auto read_u32 = [&]() -> uint32_t {
            uint32_t v = decode_u32_le(p + pos);
            pos += 4;
            return v;
        };
        auto read_string = [&](uint32_t len) -> std::string {
            std::string s(reinterpret_cast<const char*>(p + pos), len);
            pos += len;
            return s;
        };

        uint32_t tname_len = read_u32();
        schema.table_name = read_string(tname_len);

        uint32_t pk_len = read_u32();
        schema.primary_key = read_string(pk_len);

        uint32_t col_count = read_u32();
        schema.columns.resize(col_count);

        for (uint32_t i = 0; i < col_count; ++i) {
            uint32_t cname_len = read_u32();
            schema.columns[i].name = read_string(cname_len);
            schema.columns[i].type = static_cast<ColumnType>(p[pos++]);
            schema.columns[i].not_null = (p[pos++] != 0);
            schema.columns[i].is_primary_key = (p[pos++] != 0);
        }

        return schema;
    }
};

}
