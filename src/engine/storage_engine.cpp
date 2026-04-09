#include "mosaicdb/storage_engine.h"
#include "mosaicdb/compaction.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <regex>

namespace mosaicdb {

namespace fs = std::filesystem;

StorageEngine::StorageEngine(const std::string& data_dir, size_t memtable_threshold)
    : data_dir_(data_dir), next_seq_(0) {
    fs::create_directories(data_dir_);

    load_sstables();

    std::string wal_path = (fs::path(data_dir_) / "wal.log").string();
    wal_ = std::make_unique<WAL>(wal_path);
    memtable_ = std::make_unique<MemTable>(memtable_threshold);

    recover();
}

StorageEngine::~StorageEngine() = default;

std::string StorageEngine::sstable_path(uint64_t seq) const {
    char buf[64];
    snprintf(buf, sizeof(buf), "sst_%06llu.db", static_cast<unsigned long long>(seq));
    return (fs::path(data_dir_) / buf).string();
}

void StorageEngine::load_sstables() {
    std::vector<std::pair<uint64_t, std::string>> found;

    if (!fs::exists(data_dir_)) return;

    for (const auto& entry : fs::directory_iterator(data_dir_)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.size() > 4 && name.substr(0, 4) == "sst_" &&
            name.substr(name.size() - 3) == ".db") {
            std::string num_str = name.substr(4, name.size() - 7);
            try {
                uint64_t seq = std::stoull(num_str);
                if (SSTableReader::validate(entry.path().string()))
                    found.push_back({seq, entry.path().string()});
                else {
                    std::cerr << "[MosaicDB] Removing corrupt SSTable: " << name << std::endl;
                    fs::remove(entry.path());
                }
            } catch (...) {}
        }
    }

    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    for (const auto& [seq, path] : found) {
        try {
            sstables_.push_back(std::make_unique<SSTableReader>(path));
            if (seq >= next_seq_) next_seq_ = seq + 1;
        } catch (const std::exception& e) {
            std::cerr << "[MosaicDB] Failed to load SSTable: " << e.what() << std::endl;
        }
    }

    if (!sstables_.empty())
        std::cerr << "[MosaicDB] Loaded " << sstables_.size() << " SSTables" << std::endl;
}

Status StorageEngine::recover() {
    auto records = wal_->recover();

    for (const auto& rec : records) {
        switch (rec.type) {
            case OpType::PUT:
                memtable_->put(rec.key, rec.value);
                break;
            case OpType::DELETE:
                memtable_->remove(rec.key);
                break;
        }
    }

    if (!records.empty())
        std::cerr << "[MosaicDB] Recovered " << records.size() << " records from WAL" << std::endl;

    if (memtable_->should_flush()) {
        flush_memtable();
    }

    return Status::OK;
}

Status StorageEngine::flush_memtable() {
    auto entries = memtable_->dump_sorted();
    if (entries.empty()) return Status::OK;

    std::string path = sstable_path(next_seq_);
    Status s = SSTableWriter::build(path, entries);
    if (s != Status::OK) return s;

    try {
        auto reader = std::make_unique<SSTableReader>(path);
        sstables_.insert(sstables_.begin(), std::move(reader));
    } catch (const std::exception& e) {
        std::cerr << "[MosaicDB] Failed to open flushed SSTable: " << e.what() << std::endl;
        return Status::IO_ERROR;
    }

    next_seq_++;
    memtable_->clear();
    wal_->clear();

    std::cerr << "[MosaicDB] Flushed MemTable to " << path
              << " (" << entries.size() << " entries)" << std::endl;

    return Status::OK;
}

Status StorageEngine::put(const std::string& key, const std::string& value) {
    if (key.empty()) return Status::INVALID_ARGUMENT;

    std::lock_guard<std::mutex> lock(write_mutex_);

    Status s = wal_->append(OpType::PUT, key, value);
    if (s != Status::OK) return s;

    memtable_->put(key, value);

    if (memtable_->should_flush()) {
        Status fs = flush_memtable();
        if (fs != Status::OK)
            std::cerr << "[MosaicDB] Flush failed: " << status_to_string(fs) << std::endl;
        else
            maybe_compact();
    }

    return Status::OK;
}

std::pair<Status, std::string> StorageEngine::get(const std::string& key) {
    if (key.empty()) return {Status::INVALID_ARGUMENT, ""};

    auto result = memtable_->get(key);

    if (result.found) {
        if (result.is_tombstone)
            return {Status::NOT_FOUND, ""};
        return {Status::OK, result.value};
    }

    for (const auto& sst : sstables_) {
        auto sr = sst->get(key);
        if (sr.found) {
            if (sr.is_tombstone)
                return {Status::NOT_FOUND, ""};
            return {Status::OK, sr.value};
        }
    }

    return {Status::NOT_FOUND, ""};
}

std::vector<std::pair<std::string, std::string>> StorageEngine::scan(const std::string& start_key, const std::string& end_key) {
    if (start_key > end_key) return {};

    std::map<std::string, std::optional<std::string>> merged;

    for (int i = static_cast<int>(sstables_.size()) - 1; i >= 0; --i) {
        auto entries = sstables_[i]->scan(start_key, end_key);
        for (auto& e : entries) {
            merged[std::move(e.key)] = std::move(e.value);
        }
    }

    auto mem_entries = memtable_->scan(start_key, end_key);
    for (auto& e : mem_entries) {
        merged[std::move(e.first)] = std::move(e.second);
    }

    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(merged.size());
    for (auto& [k, v] : merged) {
        if (v.has_value()) {
            result.push_back({k, std::move(v.value())});
        }
    }

    return result;
}

Status StorageEngine::remove(const std::string& key) {
    if (key.empty()) return Status::INVALID_ARGUMENT;

    std::lock_guard<std::mutex> lock(write_mutex_);

    Status s = wal_->append(OpType::DELETE, key, "");
    if (s != Status::OK) return s;

    memtable_->remove(key);

    if (memtable_->should_flush()) {
        Status fs = flush_memtable();
        if (fs != Status::OK)
            std::cerr << "[MosaicDB] Flush failed: " << status_to_string(fs) << std::endl;
    }

    return Status::OK;
}

size_t StorageEngine::sstable_count() const {
    return sstables_.size();
}

Status StorageEngine::maybe_compact() {
    if (sstables_.size() < COMPACTION_THRESHOLD) return Status::OK;

    std::string out_path = sstable_path(next_seq_);

    Status s = CompactionEngine::compact(sstables_, out_path);
    if (s != Status::OK) {
        std::cerr << "[MosaicDB] Compaction failed: " << status_to_string(s) << std::endl;
        return s;
    }

    std::vector<std::string> old_paths;
    for (const auto& sst : sstables_)
        old_paths.push_back(sst->file_path());

    sstables_.clear();

    try {
        sstables_.push_back(std::make_unique<SSTableReader>(out_path));
    } catch (const std::exception& e) {
        std::cerr << "[MosaicDB] Failed to open compacted SSTable: " << e.what() << std::endl;
        return Status::IO_ERROR;
    }

    next_seq_++;

    for (const auto& p : old_paths) {
        std::error_code ec;
        fs::remove(p, ec);
    }

    std::cerr << "[MosaicDB] Compacted " << old_paths.size()
              << " SSTables into " << out_path << std::endl;

    return Status::OK;
}

}
