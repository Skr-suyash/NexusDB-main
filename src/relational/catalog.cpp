#include "mosaicdb/catalog.h"
#include <iostream>

namespace mosaicdb {

static const std::string CATALOG_PREFIX = "__catalog:";
static const std::string TABLE_PREFIX   = "__table:";
static const std::string META_PREFIX    = "__meta:";

std::string Catalog::catalog_key(const std::string& table_name) {
    return CATALOG_PREFIX + table_name;
}

std::string Catalog::row_key(const std::string& table_name, const std::string& pk_value) {
    return TABLE_PREFIX + table_name + ":row:" + pk_value;
}

std::string Catalog::row_key_prefix(const std::string& table_name) {
    return TABLE_PREFIX + table_name + ":row:";
}

std::string Catalog::row_key_prefix_end(const std::string& table_name) {
    // Use '~' (0x7E) as an end marker since it's after all printable ASCII
    return TABLE_PREFIX + table_name + ":row:~";
}

std::string Catalog::meta_key(const std::string& table_name, const std::string& field) {
    return META_PREFIX + table_name + ":" + field;
}

Catalog::Catalog(StorageEngine& engine) : engine_(engine) {
    load_cache();
}

void Catalog::load_cache() {
    // Scan for all catalog entries
    auto results = engine_.scan(CATALOG_PREFIX, CATALOG_PREFIX + "~");
    for (const auto& [key, value] : results) {
        if (key.substr(0, CATALOG_PREFIX.size()) == CATALOG_PREFIX) {
            try {
                TableSchema schema = TableSchema::deserialize(value);
                cache_[schema.table_name] = schema;
            } catch (const std::exception& e) {
                std::cerr << "[MosaicDB] Failed to load schema from key: " << key
                          << " - " << e.what() << std::endl;
            }
        }
    }
    if (!cache_.empty()) {
        std::cerr << "[MosaicDB] Loaded " << cache_.size() << " table schemas" << std::endl;
    }
}

Status Catalog::create_table(const TableSchema& schema) {
    if (table_exists(schema.table_name)) {
        return Status::INVALID_ARGUMENT;
    }

    std::string key = catalog_key(schema.table_name);
    std::string value = schema.serialize();

    Status s = engine_.put(key, value);
    if (s != Status::OK) return s;

    // Initialize the auto-increment row ID counter
    s = engine_.put(meta_key(schema.table_name, "next_rowid"), "1");
    if (s != Status::OK) return s;

    cache_[schema.table_name] = schema;
    return Status::OK;
}

Status Catalog::drop_table(const std::string& table_name) {
    if (!table_exists(table_name)) {
        return Status::NOT_FOUND;
    }

    // Remove all rows
    auto rows = engine_.scan(row_key_prefix(table_name), row_key_prefix_end(table_name));
    for (const auto& [key, _] : rows) {
        engine_.remove(key);
    }

    // Remove metadata
    engine_.remove(meta_key(table_name, "next_rowid"));

    // Remove catalog entry
    engine_.remove(catalog_key(table_name));

    cache_.erase(table_name);
    return Status::OK;
}

std::optional<TableSchema> Catalog::get_schema(const std::string& table_name) {
    auto it = cache_.find(table_name);
    if (it != cache_.end()) {
        return it->second;
    }

    // Try loading from engine
    auto [status, value] = engine_.get(catalog_key(table_name));
    if (status != Status::OK) return std::nullopt;

    try {
        TableSchema schema = TableSchema::deserialize(value);
        cache_[table_name] = schema;
        return schema;
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<std::string> Catalog::list_tables() {
    std::vector<std::string> tables;
    for (const auto& [name, _] : cache_) {
        tables.push_back(name);
    }
    return tables;
}

bool Catalog::table_exists(const std::string& table_name) {
    return cache_.find(table_name) != cache_.end();
}

uint64_t Catalog::next_row_id(const std::string& table_name) {
    std::string mk = meta_key(table_name, "next_rowid");
    auto [status, value] = engine_.get(mk);
    uint64_t id = 1;
    if (status == Status::OK) {
        try { id = std::stoull(value); } catch (...) {}
    }
    engine_.put(mk, std::to_string(id + 1));
    return id;
}

}
