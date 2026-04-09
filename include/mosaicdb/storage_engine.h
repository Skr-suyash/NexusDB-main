#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "mosaicdb/common.h"
#include "mosaicdb/wal.h"
#include "mosaicdb/memtable.h"
#include "mosaicdb/sstable.h"
#include "mosaicdb/compaction.h"

namespace mosaicdb {

constexpr size_t COMPACTION_THRESHOLD = 4;

class StorageEngine {
public:
    explicit StorageEngine(const std::string& data_dir,
                           size_t memtable_threshold = DEFAULT_MEMTABLE_THRESHOLD);
    ~StorageEngine();

    StorageEngine(const StorageEngine&) = delete;
    StorageEngine& operator=(const StorageEngine&) = delete;

    Status put(const std::string& key, const std::string& value);
    std::pair<Status, std::string> get(const std::string& key);
    std::vector<std::pair<std::string, std::string>> scan(const std::string& start_key, const std::string& end_key);
    Status remove(const std::string& key);
    size_t sstable_count() const;

private:
    std::string data_dir_;
    std::unique_ptr<WAL> wal_;
    std::unique_ptr<MemTable> memtable_;
    std::vector<std::unique_ptr<SSTableReader>> sstables_;
    std::mutex write_mutex_;
    uint64_t next_seq_;

    Status recover();
    Status flush_memtable();
    Status maybe_compact();
    void load_sstables();
    std::string sstable_path(uint64_t seq) const;
};

}
