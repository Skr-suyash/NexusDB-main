#include <iostream>
#include <cassert>
#include <filesystem>
#include <string>
#include <vector>
#include <optional>
#include "mosaicdb/sstable.h"

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

#define ASSERT_FALSE(expr) \
    do { if ((expr)) throw std::runtime_error("Assertion failed: NOT " #expr); } while(0)

static const std::string TEST_DIR = "test_sst_data";
void cleanup() { fs::remove_all(TEST_DIR); }
void setup() { cleanup(); fs::create_directories(TEST_DIR); }

void test_build_and_read() {
    setup();
    std::string path = (fs::path(TEST_DIR) / "test.db").string();

    std::vector<std::pair<std::string, std::optional<std::string>>> entries = {
        {"apple", "red"},
        {"banana", "yellow"},
        {"cherry", "dark_red"},
        {"date", "brown"},
        {"elderberry", "purple"},
    };

    ASSERT_EQ(mosaicdb::SSTableWriter::build(path, entries), mosaicdb::Status::OK);

    {
        mosaicdb::SSTableReader reader(path);
        ASSERT_EQ(reader.entry_count(), 5u);
        ASSERT_EQ(reader.min_key(), "apple");
        ASSERT_EQ(reader.max_key(), "elderberry");

        auto r1 = reader.get("apple");
        ASSERT_TRUE(r1.found);
        ASSERT_FALSE(r1.is_tombstone);
        ASSERT_EQ(r1.value, "red");

        auto r2 = reader.get("cherry");
        ASSERT_TRUE(r2.found);
        ASSERT_EQ(r2.value, "dark_red");

        auto r3 = reader.get("grape");
        ASSERT_FALSE(r3.found);
    }

    cleanup();
}

void test_tombstones() {
    setup();
    std::string path = (fs::path(TEST_DIR) / "tomb.db").string();

    std::vector<std::pair<std::string, std::optional<std::string>>> entries = {
        {"alive", "yes"},
        {"dead", std::nullopt},
        {"zombie", "maybe"},
    };

    ASSERT_EQ(mosaicdb::SSTableWriter::build(path, entries), mosaicdb::Status::OK);

    {
        mosaicdb::SSTableReader reader(path);

        auto r1 = reader.get("alive");
        ASSERT_TRUE(r1.found);
        ASSERT_EQ(r1.value, "yes");

        auto r2 = reader.get("dead");
        ASSERT_TRUE(r2.found);
        ASSERT_TRUE(r2.is_tombstone);

        auto r3 = reader.get("zombie");
        ASSERT_TRUE(r3.found);
        ASSERT_EQ(r3.value, "maybe");
    }

    cleanup();
}

void test_large_sstable() {
    setup();
    std::string path = (fs::path(TEST_DIR) / "large.db").string();

    const int N = 1000;
    std::vector<std::pair<std::string, std::optional<std::string>>> entries;
    for (int i = 0; i < N; ++i) {
        char key[32], val[64];
        snprintf(key, sizeof(key), "key_%06d", i);
        snprintf(val, sizeof(val), "value_%06d", i);
        entries.push_back({key, val});
    }

    ASSERT_EQ(mosaicdb::SSTableWriter::build(path, entries), mosaicdb::Status::OK);

    {
        mosaicdb::SSTableReader reader(path);
        ASSERT_EQ(reader.entry_count(), static_cast<uint32_t>(N));

        for (int i = 0; i < N; i += 37) {
            char key[32], val[64];
            snprintf(key, sizeof(key), "key_%06d", i);
            snprintf(val, sizeof(val), "value_%06d", i);
            auto r = reader.get(key);
            ASSERT_TRUE(r.found);
            ASSERT_EQ(r.value, val);
        }

        auto miss = reader.get("zzz_not_here");
        ASSERT_FALSE(miss.found);
    }

    cleanup();
}

void test_validate() {
    setup();
    std::string good = (fs::path(TEST_DIR) / "good.db").string();
    std::string bad = (fs::path(TEST_DIR) / "bad.db").string();

    std::vector<std::pair<std::string, std::optional<std::string>>> entries = {{"a", "b"}};
    mosaicdb::SSTableWriter::build(good, entries);

    {
        FILE* f = fopen(bad.c_str(), "wb");
        uint8_t garbage[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        fwrite(garbage, 1, sizeof(garbage), f);
        fclose(f);
    }

    ASSERT_TRUE(mosaicdb::SSTableReader::validate(good));
    ASSERT_FALSE(mosaicdb::SSTableReader::validate(bad));

    cleanup();
}

void test_single_entry() {
    setup();
    std::string path = (fs::path(TEST_DIR) / "single.db").string();

    std::vector<std::pair<std::string, std::optional<std::string>>> entries = {
        {"only_key", "only_val"}
    };

    ASSERT_EQ(mosaicdb::SSTableWriter::build(path, entries), mosaicdb::Status::OK);

    {
        mosaicdb::SSTableReader reader(path);
        ASSERT_EQ(reader.entry_count(), 1u);

        auto r = reader.get("only_key");
        ASSERT_TRUE(r.found);
        ASSERT_EQ(r.value, "only_val");

        auto m = reader.get("other");
        ASSERT_FALSE(m.found);
    }

    cleanup();
}

int main() {
    std::cout << "=== SSTable Tests ===" << std::endl;

    TEST(test_build_and_read);
    TEST(test_tombstones);
    TEST(test_large_sstable);
    TEST(test_validate);
    TEST(test_single_entry);

    std::cout << std::endl;
    std::cout << tests_passed << "/" << tests_run << " tests passed." << std::endl;

    return (tests_passed == tests_run) ? 0 : 1;
}
