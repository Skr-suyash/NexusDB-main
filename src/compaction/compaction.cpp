#include "mosaicdb/compaction.h"
#include <map>
#include <optional>

namespace mosaicdb {

Status CompactionEngine::compact(
    const std::vector<std::unique_ptr<SSTableReader>>& inputs,
    const std::string& output_path) {

    if (inputs.empty()) return Status::INVALID_ARGUMENT;

    std::map<std::string, std::optional<std::string>> merged;

    for (int i = static_cast<int>(inputs.size()) - 1; i >= 0; --i) {
        auto entries = inputs[i]->read_all_entries();
        for (auto& e : entries)
            merged[std::move(e.key)] = std::move(e.value);
    }

    std::vector<std::pair<std::string, std::optional<std::string>>> result;
    result.reserve(merged.size());
    for (auto& [k, v] : merged) {
        if (v.has_value())
            result.push_back({k, std::move(v)});
    }

    if (result.empty()) {
        result.push_back({"__empty__", ""});
    }

    return SSTableWriter::build(output_path, result);
}

}
