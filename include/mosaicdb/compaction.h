#pragma once

#include <string>
#include <vector>
#include <memory>
#include "mosaicdb/common.h"
#include "mosaicdb/sstable.h"

namespace mosaicdb {

class CompactionEngine {
public:
    static Status compact(
        const std::vector<std::unique_ptr<SSTableReader>>& inputs,
        const std::string& output_path);
};

}
