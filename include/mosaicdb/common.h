#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <stdexcept>

namespace mosaicdb {

enum class Status { OK, NOT_FOUND, CORRUPTION, IO_ERROR, INVALID_ARGUMENT };

enum class OpType : uint8_t { PUT = 0x01, DELETE = 0x02 };

struct Record {
    OpType type;
    std::string key;
    std::string value;
};

constexpr size_t DEFAULT_MEMTABLE_THRESHOLD = 4 * 1024 * 1024;
constexpr uint32_t SSTABLE_MAGIC = 0x4D534454;
constexpr uint32_t TOMBSTONE_VAL_SIZE = 0xFFFFFFFF;
constexpr int SPARSE_INDEX_INTERVAL = 16;
constexpr size_t SSTABLE_FOOTER_SIZE = 32;

inline std::string status_to_string(Status s) {
    switch (s) {
        case Status::OK:               return "OK";
        case Status::NOT_FOUND:        return "NOT_FOUND";
        case Status::CORRUPTION:       return "CORRUPTION";
        case Status::IO_ERROR:         return "IO_ERROR";
        case Status::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    }
    return "UNKNOWN";
}

inline void encode_u32_le(uint8_t* buf, uint32_t val) {
    buf[0] = static_cast<uint8_t>(val);
    buf[1] = static_cast<uint8_t>(val >> 8);
    buf[2] = static_cast<uint8_t>(val >> 16);
    buf[3] = static_cast<uint8_t>(val >> 24);
}

inline uint32_t decode_u32_le(const uint8_t* buf) {
    return static_cast<uint32_t>(buf[0])
         | (static_cast<uint32_t>(buf[1]) << 8)
         | (static_cast<uint32_t>(buf[2]) << 16)
         | (static_cast<uint32_t>(buf[3]) << 24);
}

inline void encode_u64_le(uint8_t* buf, uint64_t val) {
    for (int i = 0; i < 8; ++i)
        buf[i] = static_cast<uint8_t>(val >> (i * 8));
}

inline uint64_t decode_u64_le(const uint8_t* buf) {
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i)
        val |= static_cast<uint64_t>(buf[i]) << (i * 8);
    return val;
}

class CRC32 {
public:
    static uint32_t compute(const uint8_t* data, size_t length) {
        static const auto tbl = build_table();
        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < length; ++i)
            crc = (crc >> 8) ^ tbl[static_cast<uint8_t>(crc ^ data[i])];
        return crc ^ 0xFFFFFFFF;
    }

private:
    static std::array<uint32_t, 256> build_table() {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j)
                c = (c & 1) ? ((c >> 1) ^ 0xEDB88320u) : (c >> 1);
            t[i] = c;
        }
        return t;
    }
};

}
