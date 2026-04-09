#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace mosaicdb {

class BloomFilter {
public:
    BloomFilter(uint32_t num_bits, uint32_t num_hashes);
    BloomFilter(const uint8_t* data, size_t data_size, uint32_t num_bits, uint32_t num_hashes);

    void add(const std::string& key);
    bool possibly_contains(const std::string& key) const;

    const uint8_t* data() const { return bits_.data(); }
    size_t data_size() const { return bits_.size(); }
    uint32_t num_bits() const { return num_bits_; }
    uint32_t num_hashes() const { return num_hashes_; }

    static BloomFilter create_for_entries(uint32_t num_entries);

private:
    std::vector<uint8_t> bits_;
    uint32_t num_bits_;
    uint32_t num_hashes_;

    void set_bit(uint32_t pos);
    bool get_bit(uint32_t pos) const;
    static uint32_t fnv1a(const std::string& key);
    static uint32_t murmur_mix(const std::string& key);
};

}
