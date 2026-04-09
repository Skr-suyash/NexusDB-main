#include "mosaicdb/bloom_filter.h"
#include <cstring>

namespace mosaicdb {

BloomFilter::BloomFilter(uint32_t num_bits, uint32_t num_hashes)
    : bits_((num_bits + 7) / 8, 0), num_bits_(num_bits), num_hashes_(num_hashes) {}

BloomFilter::BloomFilter(const uint8_t* data, size_t data_size, uint32_t num_bits, uint32_t num_hashes)
    : bits_(data, data + data_size), num_bits_(num_bits), num_hashes_(num_hashes) {}

void BloomFilter::set_bit(uint32_t pos) {
    bits_[pos / 8] |= (1 << (pos % 8));
}

bool BloomFilter::get_bit(uint32_t pos) const {
    return (bits_[pos / 8] & (1 << (pos % 8))) != 0;
}

uint32_t BloomFilter::fnv1a(const std::string& key) {
    uint32_t hash = 2166136261u;
    for (char c : key) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

uint32_t BloomFilter::murmur_mix(const std::string& key) {
    uint32_t hash = 0x5bd1e995u;
    for (char c : key) {
        uint32_t k = static_cast<uint8_t>(c);
        k *= 0x5bd1e995u;
        k ^= k >> 24;
        k *= 0x5bd1e995u;
        hash *= 0x5bd1e995u;
        hash ^= k;
    }
    hash ^= hash >> 13;
    hash *= 0x5bd1e995u;
    hash ^= hash >> 15;
    return hash;
}

void BloomFilter::add(const std::string& key) {
    uint32_t h1 = fnv1a(key);
    uint32_t h2 = murmur_mix(key);
    for (uint32_t i = 0; i < num_hashes_; ++i) {
        uint32_t pos = (h1 + i * h2) % num_bits_;
        set_bit(pos);
    }
}

bool BloomFilter::possibly_contains(const std::string& key) const {
    uint32_t h1 = fnv1a(key);
    uint32_t h2 = murmur_mix(key);
    for (uint32_t i = 0; i < num_hashes_; ++i) {
        uint32_t pos = (h1 + i * h2) % num_bits_;
        if (!get_bit(pos)) return false;
    }
    return true;
}

BloomFilter BloomFilter::create_for_entries(uint32_t num_entries) {
    uint32_t bits_per_key = 10;
    uint32_t num_bits = num_entries * bits_per_key;
    if (num_bits < 64) num_bits = 64;
    uint32_t num_hashes = 7;
    return BloomFilter(num_bits, num_hashes);
}

}
