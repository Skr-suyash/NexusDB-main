#include <iostream>
#include <cassert>
#include <filesystem>
#include <cstdio>
#include <string>
#include "mosaicdb/wal.h"

namespace fs = std::filesystem;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        std::cout << "  [RUN ] " << #name << std::endl; \
        try { name(); tests_passed++; std::cout << "  [PASS] " << #name << std::endl; } \
        catch (const std::exception& e) { std::cout << "  [FAIL] " << #name << ": " << e.what() << std::endl; } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) throw std::runtime_error("Assertion failed: " #expr); } while(0)

static const std::string TEST_DIR = "test_wal_data";
void cleanup() { fs::remove_all(TEST_DIR); }
void setup() { cleanup(); fs::create_directories(TEST_DIR); }

void test_append_and_recover() {
    setup();
    std::string path = (fs::path(TEST_DIR) / "test.wal").string();

    {
        mosaicdb::WAL wal(path);
        ASSERT_EQ(wal.append(mosaicdb::OpType::PUT, "key1", "value1"), mosaicdb::Status::OK);
        ASSERT_EQ(wal.append(mosaicdb::OpType::PUT, "key2", "value2"), mosaicdb::Status::OK);
        ASSERT_EQ(wal.append(mosaicdb::OpType::DELETE, "key1", ""), mosaicdb::Status::OK);
    }

    {
        mosaicdb::WAL wal(path);
        auto records = wal.recover();
        ASSERT_EQ(records.size(), 3u);
        ASSERT_EQ(records[0].type, mosaicdb::OpType::PUT);
        ASSERT_EQ(records[0].key, "key1");
        ASSERT_EQ(records[0].value, "value1");
        ASSERT_EQ(records[1].type, mosaicdb::OpType::PUT);
        ASSERT_EQ(records[1].key, "key2");
        ASSERT_EQ(records[1].value, "value2");
        ASSERT_EQ(records[2].type, mosaicdb::OpType::DELETE);
        ASSERT_EQ(records[2].key, "key1");
    }

    cleanup();
}

void test_empty_recovery() {
    setup();
    std::string path = (fs::path(TEST_DIR) / "empty.wal").string();

    {
        mosaicdb::WAL wal(path);
        auto records = wal.recover();
        ASSERT_EQ(records.size(), 0u);
    }

    cleanup();
}

void test_clear() {
    setup();
    std::string path = (fs::path(TEST_DIR) / "clear.wal").string();

    {
        mosaicdb::WAL wal(path);
        wal.append(mosaicdb::OpType::PUT, "a", "b");
        wal.append(mosaicdb::OpType::PUT, "c", "d");
        wal.clear();
    }

    {
        mosaicdb::WAL wal(path);
        auto records = wal.recover();
        ASSERT_EQ(records.size(), 0u);
    }

    cleanup();
}

void test_large_values() {
    setup();
    std::string path = (fs::path(TEST_DIR) / "large.wal").string();

    std::string large_key(1000, 'K');
    std::string large_value(100000, 'V');

    {
        mosaicdb::WAL wal(path);
        ASSERT_EQ(wal.append(mosaicdb::OpType::PUT, large_key, large_value), mosaicdb::Status::OK);
    }

    {
        mosaicdb::WAL wal(path);
        auto records = wal.recover();
        ASSERT_EQ(records.size(), 1u);
        ASSERT_EQ(records[0].key, large_key);
        ASSERT_EQ(records[0].value, large_value);
    }

    cleanup();
}

void test_many_records() {
    setup();
    std::string path = (fs::path(TEST_DIR) / "many.wal").string();

    const int N = 10000;

    {
        mosaicdb::WAL wal(path);
        for (int i = 0; i < N; ++i) {
            std::string key = "key_" + std::to_string(i);
            std::string val = "val_" + std::to_string(i);
            ASSERT_EQ(wal.append(mosaicdb::OpType::PUT, key, val), mosaicdb::Status::OK);
        }
    }

    {
        mosaicdb::WAL wal(path);
        auto records = wal.recover();
        ASSERT_EQ(records.size(), static_cast<size_t>(N));
        for (int i = 0; i < N; ++i) {
            ASSERT_EQ(records[i].key, "key_" + std::to_string(i));
            ASSERT_EQ(records[i].value, "val_" + std::to_string(i));
        }
    }

    cleanup();
}

void test_corruption_detection() {
    setup();
    std::string path = (fs::path(TEST_DIR) / "corrupt.wal").string();

    {
        mosaicdb::WAL wal(path);
        wal.append(mosaicdb::OpType::PUT, "good1", "val1");
        wal.append(mosaicdb::OpType::PUT, "good2", "val2");
    }

    {
        FILE* f = fopen(path.c_str(), "ab");
        ASSERT_TRUE(f != nullptr);
        uint8_t garbage[] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8, 0xF7, 0xF6, 0xF5, 0xF4, 0xF3};
        fwrite(garbage, 1, sizeof(garbage), f);
        fclose(f);
    }

    {
        mosaicdb::WAL wal(path);
        auto records = wal.recover();
        ASSERT_EQ(records.size(), 2u);
        ASSERT_EQ(records[0].key, "good1");
        ASSERT_EQ(records[1].key, "good2");
    }

    cleanup();
}

int main() {
    std::cout << "=== WAL Tests ===" << std::endl;

    TEST(test_append_and_recover);
    TEST(test_empty_recovery);
    TEST(test_clear);
    TEST(test_large_values);
    TEST(test_many_records);
    TEST(test_corruption_detection);

    std::cout << std::endl;
    std::cout << tests_passed << "/" << tests_run << " tests passed." << std::endl;

    return (tests_passed == tests_run) ? 0 : 1;
}
