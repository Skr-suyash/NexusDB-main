#pragma once

#include <string>
#include <vector>
#include <mutex>
#include "mosaicdb/common.h"

namespace mosaicdb {

class WAL {
public:
    explicit WAL(const std::string& path);
    ~WAL();

    WAL(const WAL&) = delete;
    WAL& operator=(const WAL&) = delete;

    Status append(OpType type, const std::string& key, const std::string& value);
    std::vector<Record> recover();
    Status clear();
    Status sync();

private:
    std::string path_;
    int fd_;
    std::mutex mutex_;

    bool write_all(const uint8_t* data, size_t length);
    bool read_all(uint8_t* data, size_t length);
};

}
