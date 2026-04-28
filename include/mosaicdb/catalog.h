#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include "mosaicdb/common.h"
#include "mosaicdb/schema.h"
#include "mosaicdb/storage_engine.h"

namespace mosaicdb {

class Catalog {
public:
    explicit Catalog(StorageEngine& engine);

    Status create_table(const TableSchema& schema);
    Status drop_table(const std::string& table_name);
    std::optional<TableSchema> get_schema(const std::string& table_name);
    std::vector<std::string> list_tables();
    bool table_exists(const std::string& table_name);

    // Row ID management
    uint64_t next_row_id(const std::string& table_name);

    // Key encoding helpers
    static std::string catalog_key(const std::string& table_name);
    static std::string row_key(const std::string& table_name, const std::string& pk_value);
    static std::string row_key_prefix(const std::string& table_name);
    static std::string row_key_prefix_end(const std::string& table_name);
    static std::string meta_key(const std::string& table_name, const std::string& field);

private:
    StorageEngine& engine_;
    std::unordered_map<std::string, TableSchema> cache_;

    void load_cache();
};

}
