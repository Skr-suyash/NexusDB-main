#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <cstdio>
#include "mosaicdb/common.h"
#include "mosaicdb/bloom_filter.h"

namespace mosaicdb {

struct IndexEntry {
    std::string key;
    uint64_t offset;
};

struct DataEntry {
    std::string key;
    std::optional<std::string> value;
};

class SSTableWriter {
public:
    static Status build(const std::string& path,
                        const std::vector<std::pair<std::string, std::optional<std::string>>>& entries);
};

class SSTableReader {
public:
    explicit SSTableReader(const std::string& path);
    ~SSTableReader();

    SSTableReader(const SSTableReader&) = delete;
    SSTableReader& operator=(const SSTableReader&) = delete;

    struct LookupResult {
        bool found;
        bool is_tombstone;
        std::string value;
    };

    LookupResult get(const std::string& key) const;
    std::vector<DataEntry> scan(const std::string& start_key, const std::string& end_key) const;
    std::vector<DataEntry> read_all_entries() const;

    const std::string& file_path() const { return path_; }
    const std::string& min_key() const { return min_key_; }
    const std::string& max_key() const { return max_key_; }
    uint32_t entry_count() const { return num_entries_; }

    static bool validate(const std::string& path);

private:
    std::string path_;
    FILE* file_;
    std::vector<IndexEntry> index_;
    std::unique_ptr<BloomFilter> bloom_;
    uint32_t num_entries_;
    uint64_t bloom_offset_;
    uint32_t bloom_size_;
    uint64_t index_offset_;
    std::string min_key_;
    std::string max_key_;

    bool read_footer();
    bool read_index();
    bool read_bloom();
    LookupResult scan_from(uint64_t offset, uint64_t end_offset, const std::string& key) const;
};

}
