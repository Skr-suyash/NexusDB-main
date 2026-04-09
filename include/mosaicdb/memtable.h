#pragma once

#include <map>
#include <string>
#include <optional>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include "mosaicdb/common.h"

namespace mosaicdb {

class MemTable {
public:
    explicit MemTable(size_t flush_threshold = DEFAULT_MEMTABLE_THRESHOLD);

    void put(const std::string& key, const std::string& value);
    void remove(const std::string& key);

    struct LookupResult {
        bool found;
        bool is_tombstone;
        std::string value;
    };

    LookupResult get(const std::string& key) const;
    std::vector<std::pair<std::string, std::optional<std::string>>> scan(const std::string& start_key, const std::string& end_key) const;
    bool contains(const std::string& key) const;
    bool should_flush() const;
    std::vector<std::pair<std::string, std::optional<std::string>>> dump_sorted() const;
    void clear();
    size_t size_bytes() const;
    size_t entry_count() const;

private:
    std::map<std::string, std::optional<std::string>> data_;
    size_t current_size_;
    size_t flush_threshold_;
    mutable std::shared_mutex mutex_;
};

}
