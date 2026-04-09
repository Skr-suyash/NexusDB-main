#include "mosaicdb/memtable.h"

namespace mosaicdb {

MemTable::MemTable(size_t flush_threshold)
    : current_size_(0), flush_threshold_(flush_threshold) {}

void MemTable::put(const std::string& key, const std::string& value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = data_.find(key);
    if (it != data_.end()) {
        current_size_ -= key.size();
        if (it->second.has_value())
            current_size_ -= it->second->size();
        it->second = value;
    } else {
        data_[key] = value;
    }
    current_size_ += key.size() + value.size();
}

void MemTable::remove(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = data_.find(key);
    if (it != data_.end()) {
        current_size_ -= key.size();
        if (it->second.has_value())
            current_size_ -= it->second->size();
        it->second = std::nullopt;
        current_size_ += key.size();
    } else {
        data_[key] = std::nullopt;
        current_size_ += key.size();
    }
}

MemTable::LookupResult MemTable::get(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = data_.find(key);
    if (it == data_.end())
        return {false, false, ""};
    if (!it->second.has_value())
        return {true, true, ""};
    return {true, false, it->second.value()};
}

std::vector<std::pair<std::string, std::optional<std::string>>> MemTable::scan(const std::string& start_key, const std::string& end_key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::pair<std::string, std::optional<std::string>>> result;
    
    auto it_start = data_.lower_bound(start_key);
    auto it_end = data_.upper_bound(end_key);
    
    for (auto it = it_start; it != it_end; ++it) {
        result.push_back(*it);
    }
    
    return result;
}

bool MemTable::contains(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return data_.find(key) != data_.end();
}

bool MemTable::should_flush() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return current_size_ >= flush_threshold_;
}

std::vector<std::pair<std::string, std::optional<std::string>>> MemTable::dump_sorted() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return {data_.begin(), data_.end()};
}

void MemTable::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    data_.clear();
    current_size_ = 0;
}

size_t MemTable::size_bytes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return current_size_;
}

size_t MemTable::entry_count() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return data_.size();
}

}
