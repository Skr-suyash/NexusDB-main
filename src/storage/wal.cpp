#include "mosaicdb/wal.h"
#include <stdexcept>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#define mosaic_open(p, f, m)            _open(p, f, m)
#define mosaic_close(fd)                _close(fd)
#define mosaic_read(fd, buf, cnt)       _read(fd, buf, static_cast<unsigned int>(cnt))
#define mosaic_write(fd, buf, cnt)      _write(fd, buf, static_cast<unsigned int>(cnt))
#define mosaic_lseek(fd, off, w)        _lseek(fd, static_cast<long>(off), w)
#define mosaic_fsync(fd)                _commit(fd)
#define mosaic_ftruncate(fd, sz)        _chsize(fd, static_cast<long>(sz))
#define MOSAIC_O_FLAGS                  (_O_RDWR | _O_CREAT | _O_BINARY)
#define MOSAIC_PERM                     (_S_IREAD | _S_IWRITE)
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#define mosaic_open(p, f, m)            open(p, f, m)
#define mosaic_close(fd)                close(fd)
#define mosaic_read(fd, buf, cnt)       read(fd, buf, cnt)
#define mosaic_write(fd, buf, cnt)      write(fd, buf, cnt)
#define mosaic_lseek(fd, off, w)        lseek(fd, off, w)
#define mosaic_fsync(fd)                fsync(fd)
#define mosaic_ftruncate(fd, sz)        ftruncate(fd, sz)
#define MOSAIC_O_FLAGS                  (O_RDWR | O_CREAT)
#define MOSAIC_PERM                     (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#endif

namespace mosaicdb {

static constexpr size_t HEADER_SIZE = 4 + 1 + 4 + 4;

WAL::WAL(const std::string& path) : path_(path), fd_(-1) {
    fd_ = mosaic_open(path.c_str(), MOSAIC_O_FLAGS, MOSAIC_PERM);
    if (fd_ < 0)
        throw std::runtime_error("Failed to open WAL file: " + path);
}

WAL::~WAL() {
    if (fd_ >= 0) {
        mosaic_close(fd_);
        fd_ = -1;
    }
}

bool WAL::write_all(const uint8_t* data, size_t length) {
    size_t written = 0;
    while (written < length) {
        auto n = mosaic_write(fd_, data + written, length - written);
        if (n <= 0) return false;
        written += static_cast<size_t>(n);
    }
    return true;
}

bool WAL::read_all(uint8_t* data, size_t length) {
    size_t total = 0;
    while (total < length) {
        auto n = mosaic_read(fd_, data + total, length - total);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

Status WAL::append(OpType type, const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t key_size = static_cast<uint32_t>(key.size());
    uint32_t val_size = static_cast<uint32_t>(value.size());

    size_t payload_size = 1 + 4 + 4 + key_size + val_size;
    std::vector<uint8_t> payload(payload_size);

    payload[0] = static_cast<uint8_t>(type);
    encode_u32_le(&payload[1], key_size);
    encode_u32_le(&payload[5], val_size);
    if (key_size > 0)
        std::memcpy(&payload[9], key.data(), key_size);
    if (val_size > 0)
        std::memcpy(&payload[9 + key_size], value.data(), val_size);

    uint32_t crc = CRC32::compute(payload.data(), payload_size);

    mosaic_lseek(fd_, 0, SEEK_END);

    uint8_t crc_buf[4];
    encode_u32_le(crc_buf, crc);
    if (!write_all(crc_buf, 4)) return Status::IO_ERROR;
    if (!write_all(payload.data(), payload_size)) return Status::IO_ERROR;
    if (mosaic_fsync(fd_) != 0) return Status::IO_ERROR;

    return Status::OK;
}

std::vector<Record> WAL::recover() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Record> records;

    mosaic_lseek(fd_, 0, SEEK_SET);

    while (true) {
        uint8_t header[HEADER_SIZE];
        if (!read_all(header, HEADER_SIZE)) break;

        uint32_t stored_crc = decode_u32_le(header);
        uint8_t type_byte = header[4];
        uint32_t key_size = decode_u32_le(&header[5]);
        uint32_t val_size = decode_u32_le(&header[9]);

        if (key_size > 10 * 1024 * 1024 || val_size > 100 * 1024 * 1024) break;

        std::vector<uint8_t> kv_data(key_size + val_size);
        if (key_size + val_size > 0) {
            if (!read_all(kv_data.data(), key_size + val_size)) break;
        }

        size_t payload_size = 1 + 4 + 4 + key_size + val_size;
        std::vector<uint8_t> payload(payload_size);
        payload[0] = type_byte;
        encode_u32_le(&payload[1], key_size);
        encode_u32_le(&payload[5], val_size);
        if (key_size + val_size > 0)
            std::memcpy(&payload[9], kv_data.data(), key_size + val_size);

        uint32_t computed_crc = CRC32::compute(payload.data(), payload_size);
        if (computed_crc != stored_crc) break;

        Record rec;
        rec.type = static_cast<OpType>(type_byte);
        rec.key = std::string(reinterpret_cast<char*>(kv_data.data()), key_size);
        if (val_size > 0)
            rec.value = std::string(reinterpret_cast<char*>(kv_data.data() + key_size), val_size);

        records.push_back(std::move(rec));
    }

    return records;
}

Status WAL::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mosaic_ftruncate(fd_, 0) != 0) return Status::IO_ERROR;
    mosaic_lseek(fd_, 0, SEEK_SET);
    return Status::OK;
}

Status WAL::sync() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mosaic_fsync(fd_) != 0) return Status::IO_ERROR;
    return Status::OK;
}

}
