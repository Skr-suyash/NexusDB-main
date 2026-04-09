#include "mosaicdb/sstable.h"
#include <algorithm>

namespace mosaicdb {

Status SSTableWriter::build(
    const std::string& path,
    const std::vector<std::pair<std::string, std::optional<std::string>>>& entries) {

    if (entries.empty()) return Status::INVALID_ARGUMENT;

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return Status::IO_ERROR;

    std::vector<IndexEntry> index;
    BloomFilter bloom = BloomFilter::create_for_entries(static_cast<uint32_t>(entries.size()));
    uint64_t offset = 0;

    for (size_t i = 0; i < entries.size(); ++i) {
        if (i % SPARSE_INDEX_INTERVAL == 0)
            index.push_back({entries[i].first, offset});

        bloom.add(entries[i].first);

        const auto& key = entries[i].first;
        const auto& val = entries[i].second;

        uint32_t key_size = static_cast<uint32_t>(key.size());
        uint32_t val_size = val.has_value() ? static_cast<uint32_t>(val->size()) : TOMBSTONE_VAL_SIZE;

        uint8_t hdr[8];
        encode_u32_le(hdr, key_size);
        encode_u32_le(hdr + 4, val_size);
        fwrite(hdr, 1, 8, f);
        fwrite(key.data(), 1, key_size, f);

        if (val.has_value() && !val->empty())
            fwrite(val->data(), 1, val->size(), f);

        offset += 8 + key_size + (val.has_value() ? val->size() : 0);
    }

    uint64_t bloom_offset = offset;
    uint8_t bloom_hdr[8];
    encode_u32_le(bloom_hdr, bloom.num_bits());
    encode_u32_le(bloom_hdr + 4, bloom.num_hashes());
    fwrite(bloom_hdr, 1, 8, f);
    fwrite(bloom.data(), 1, bloom.data_size(), f);
    uint32_t bloom_total_size = static_cast<uint32_t>(8 + bloom.data_size());

    uint64_t index_offset = bloom_offset + bloom_total_size;

    for (const auto& ie : index) {
        uint32_t ksz = static_cast<uint32_t>(ie.key.size());
        uint8_t buf[12];
        encode_u32_le(buf, ksz);
        encode_u64_le(buf + 4, ie.offset);
        fwrite(buf, 1, 12, f);
        fwrite(ie.key.data(), 1, ksz, f);
    }

    uint8_t footer[SSTABLE_FOOTER_SIZE];
    encode_u64_le(footer, bloom_offset);
    encode_u32_le(footer + 8, bloom_total_size);
    encode_u64_le(footer + 12, index_offset);
    encode_u32_le(footer + 20, static_cast<uint32_t>(index.size()));
    encode_u32_le(footer + 24, static_cast<uint32_t>(entries.size()));
    encode_u32_le(footer + 28, SSTABLE_MAGIC);
    fwrite(footer, 1, SSTABLE_FOOTER_SIZE, f);

    fflush(f);
    fclose(f);
    return Status::OK;
}

SSTableReader::SSTableReader(const std::string& path)
    : path_(path), file_(nullptr), num_entries_(0),
      bloom_offset_(0), bloom_size_(0), index_offset_(0) {

    file_ = fopen(path.c_str(), "rb");
    if (!file_)
        throw std::runtime_error("Failed to open SSTable: " + path);

    if (!read_footer() || !read_bloom() || !read_index()) {
        fclose(file_);
        file_ = nullptr;
        throw std::runtime_error("Corrupt SSTable: " + path);
    }

    if (!index_.empty()) {
        min_key_ = index_.front().key;
        fseek(file_, 0, SEEK_SET);
        uint64_t pos = 0;
        std::string last_key;
        while (pos < bloom_offset_) {
            uint8_t hdr[8];
            if (fread(hdr, 1, 8, file_) != 8) break;
            uint32_t ksz = decode_u32_le(hdr);
            uint32_t vsz = decode_u32_le(hdr + 4);

            last_key.resize(ksz);
            if (ksz > 0 && fread(&last_key[0], 1, ksz, file_) != ksz) break;

            uint64_t skip = (vsz == TOMBSTONE_VAL_SIZE) ? 0 : vsz;
            if (skip > 0) fseek(file_, static_cast<long>(skip), SEEK_CUR);
            pos += 8 + ksz + skip;
        }
        max_key_ = last_key;
    }
}

SSTableReader::~SSTableReader() {
    if (file_) { fclose(file_); file_ = nullptr; }
}

bool SSTableReader::read_footer() {
    if (fseek(file_, -static_cast<long>(SSTABLE_FOOTER_SIZE), SEEK_END) != 0)
        return false;
    uint8_t footer[SSTABLE_FOOTER_SIZE];
    if (fread(footer, 1, SSTABLE_FOOTER_SIZE, file_) != SSTABLE_FOOTER_SIZE)
        return false;
    if (decode_u32_le(footer + 28) != SSTABLE_MAGIC) return false;

    bloom_offset_ = decode_u64_le(footer);
    bloom_size_ = decode_u32_le(footer + 8);
    index_offset_ = decode_u64_le(footer + 12);
    uint32_t num_idx = decode_u32_le(footer + 20);
    num_entries_ = decode_u32_le(footer + 24);
    index_.reserve(num_idx);
    return true;
}

bool SSTableReader::read_bloom() {
    if (bloom_size_ == 0) return true;
    if (fseek(file_, static_cast<long>(bloom_offset_), SEEK_SET) != 0)
        return false;
    uint8_t bhdr[8];
    if (fread(bhdr, 1, 8, file_) != 8) return false;
    uint32_t nb = decode_u32_le(bhdr);
    uint32_t nh = decode_u32_le(bhdr + 4);
    size_t dsz = bloom_size_ - 8;
    std::vector<uint8_t> bdata(dsz);
    if (fread(bdata.data(), 1, dsz, file_) != dsz) return false;
    bloom_ = std::make_unique<BloomFilter>(bdata.data(), dsz, nb, nh);
    return true;
}

bool SSTableReader::read_index() {
    if (fseek(file_, static_cast<long>(index_offset_), SEEK_SET) != 0)
        return false;
    size_t cap = index_.capacity();
    for (size_t i = 0; i < cap; ++i) {
        uint8_t buf[12];
        if (fread(buf, 1, 12, file_) != 12) return false;
        uint32_t ksz = decode_u32_le(buf);
        uint64_t off = decode_u64_le(buf + 4);
        std::string key(ksz, '\0');
        if (ksz > 0 && fread(&key[0], 1, ksz, file_) != ksz) return false;
        index_.push_back({std::move(key), off});
    }
    return true;
}

SSTableReader::LookupResult SSTableReader::get(const std::string& key) const {
    if (!file_ || index_.empty()) return {false, false, ""};
    if (key < min_key_ || key > max_key_) return {false, false, ""};

    if (bloom_ && !bloom_->possibly_contains(key))
        return {false, false, ""};

    size_t lo = 0, hi = index_.size() - 1, idx = 0;
    while (lo <= hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (index_[mid].key <= key) {
            idx = mid;
            if (mid == hi) break;
            lo = mid + 1;
        } else {
            if (mid == 0) break;
            hi = mid - 1;
        }
    }

    uint64_t start = index_[idx].offset;
    uint64_t end = (idx + 1 < index_.size()) ? index_[idx + 1].offset : bloom_offset_;
    return scan_from(start, end, key);
}

SSTableReader::LookupResult SSTableReader::scan_from(
    uint64_t offset, uint64_t end_offset, const std::string& key) const {
    fseek(file_, static_cast<long>(offset), SEEK_SET);
    uint64_t pos = offset;
    while (pos < end_offset) {
        uint8_t hdr[8];
        if (fread(hdr, 1, 8, file_) != 8) break;
        uint32_t ksz = decode_u32_le(hdr);
        uint32_t vsz = decode_u32_le(hdr + 4);
        bool is_tomb = (vsz == TOMBSTONE_VAL_SIZE);
        uint32_t actual_vsz = is_tomb ? 0 : vsz;

        std::string entry_key(ksz, '\0');
        if (ksz > 0 && fread(&entry_key[0], 1, ksz, file_) != ksz) break;

        if (entry_key == key) {
            if (is_tomb) return {true, true, ""};
            std::string val(actual_vsz, '\0');
            if (actual_vsz > 0 && fread(&val[0], 1, actual_vsz, file_) != actual_vsz) break;
            return {true, false, val};
        }
        if (entry_key > key) return {false, false, ""};
        if (actual_vsz > 0) fseek(file_, static_cast<long>(actual_vsz), SEEK_CUR);
        pos += 8 + ksz + actual_vsz;
    }
    return {false, false, ""};
}

std::vector<DataEntry> SSTableReader::scan(const std::string& start_key, const std::string& end_key) const {
    std::vector<DataEntry> result;
    if (!file_ || index_.empty()) return result;
    if (min_key_ > end_key || max_key_ < start_key) return result;

    size_t lo = 0, hi = index_.size() - 1, idx = 0;
    while (lo <= hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (index_[mid].key <= start_key) {
            idx = mid;
            if (mid == hi) break;
            lo = mid + 1;
        } else {
            if (mid == 0) break;
            hi = mid - 1;
        }
    }

    uint64_t offset = index_[idx].offset;
    fseek(file_, static_cast<long>(offset), SEEK_SET);
    uint64_t pos = offset;
    while (pos < bloom_offset_) {
        uint8_t hdr[8];
        if (fread(hdr, 1, 8, file_) != 8) break;
        uint32_t ksz = decode_u32_le(hdr);
        uint32_t vsz = decode_u32_le(hdr + 4);
        bool is_tomb = (vsz == TOMBSTONE_VAL_SIZE);
        uint32_t actual_vsz = is_tomb ? 0 : vsz;

        std::string entry_key(ksz, '\0');
        if (ksz > 0 && fread(&entry_key[0], 1, ksz, file_) != ksz) break;

        if (entry_key > end_key) {
            break;
        }

        if (entry_key >= start_key) {
            std::optional<std::string> val;
            if (!is_tomb) {
                std::string v(actual_vsz, '\0');
                if (actual_vsz > 0 && fread(&v[0], 1, actual_vsz, file_) != actual_vsz) break;
                val = std::move(v);
            } else if (actual_vsz > 0) {
                fseek(file_, static_cast<long>(actual_vsz), SEEK_CUR);
            }
            result.push_back({std::move(entry_key), std::move(val)});
        } else if (actual_vsz > 0) {
            fseek(file_, static_cast<long>(actual_vsz), SEEK_CUR);
        }

        pos += 8 + ksz + actual_vsz;
    }

    return result;
}

std::vector<DataEntry> SSTableReader::read_all_entries() const {
    std::vector<DataEntry> result;
    if (!file_) return result;
    result.reserve(num_entries_);

    fseek(file_, 0, SEEK_SET);
    uint64_t pos = 0;
    while (pos < bloom_offset_) {
        uint8_t hdr[8];
        if (fread(hdr, 1, 8, file_) != 8) break;
        uint32_t ksz = decode_u32_le(hdr);
        uint32_t vsz = decode_u32_le(hdr + 4);
        bool is_tomb = (vsz == TOMBSTONE_VAL_SIZE);
        uint32_t actual_vsz = is_tomb ? 0 : vsz;

        std::string key(ksz, '\0');
        if (ksz > 0 && fread(&key[0], 1, ksz, file_) != ksz) break;

        std::optional<std::string> val;
        if (!is_tomb) {
            std::string v(actual_vsz, '\0');
            if (actual_vsz > 0 && fread(&v[0], 1, actual_vsz, file_) != actual_vsz) break;
            val = std::move(v);
        }

        result.push_back({std::move(key), std::move(val)});
        pos += 8 + ksz + actual_vsz;
    }
    return result;
}

bool SSTableReader::validate(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    if (fseek(f, -static_cast<long>(SSTABLE_FOOTER_SIZE), SEEK_END) != 0) {
        fclose(f); return false;
    }
    uint8_t footer[SSTABLE_FOOTER_SIZE];
    if (fread(footer, 1, SSTABLE_FOOTER_SIZE, f) != SSTABLE_FOOTER_SIZE) {
        fclose(f); return false;
    }
    fclose(f);
    return decode_u32_le(footer + 28) == SSTABLE_MAGIC;
}

}
