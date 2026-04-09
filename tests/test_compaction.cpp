#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <optional>
#include "mosaicdb/sstable.h"
#include "mosaicdb/compaction.h"
#include "mosaicdb/storage_engine.h"

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

static const std::string TEST_DIR = "test_compact_data";
void cleanup() { fs::remove_all(TEST_DIR); }
void setup() { cleanup(); fs::create_directories(TEST_DIR); }

void test_merge_two_sstables() {
    setup();

    std::vector<std::pair<std::string, std::optional<std::string>>> e1 = {
        {"a", "1"}, {"c", "3"}, {"e", "5"}
    };
    std::vector<std::pair<std::string, std::optional<std::string>>> e2 = {
        {"b", "2"}, {"d", "4"}, {"f", "6"}
    };

    std::string p1 = (fs::path(TEST_DIR) / "sst_000000.db").string();
    std::string p2 = (fs::path(TEST_DIR) / "sst_000001.db").string();
    std::string out = (fs::path(TEST_DIR) / "sst_000002.db").string();

    mosaicdb::SSTableWriter::build(p1, e1);
    mosaicdb::SSTableWriter::build(p2, e2);

    {
        std::vector<std::unique_ptr<mosaicdb::SSTableReader>> inputs;
        inputs.push_back(std::make_unique<mosaicdb::SSTableReader>(p2));
        inputs.push_back(std::make_unique<mosaicdb::SSTableReader>(p1));
        ASSERT_EQ(mosaicdb::CompactionEngine::compact(inputs, out), mosaicdb::Status::OK);
    }

    {
        mosaicdb::SSTableReader reader(out);
        ASSERT_EQ(reader.entry_count(), 6u);
        for (auto& k : {"a", "b", "c", "d", "e", "f"}) {
            auto r = reader.get(k);
            ASSERT_TRUE(r.found);
        }
    }

    cleanup();
}

void test_duplicate_keys_latest_wins() {
    setup();

    std::vector<std::pair<std::string, std::optional<std::string>>> e1 = {{"key", "old_value"}};
    std::vector<std::pair<std::string, std::optional<std::string>>> e2 = {{"key", "new_value"}};

    std::string p1 = (fs::path(TEST_DIR) / "sst_000000.db").string();
    std::string p2 = (fs::path(TEST_DIR) / "sst_000001.db").string();
    std::string out = (fs::path(TEST_DIR) / "sst_000002.db").string();

    mosaicdb::SSTableWriter::build(p1, e1);
    mosaicdb::SSTableWriter::build(p2, e2);

    {
        std::vector<std::unique_ptr<mosaicdb::SSTableReader>> inputs;
        inputs.push_back(std::make_unique<mosaicdb::SSTableReader>(p2));
        inputs.push_back(std::make_unique<mosaicdb::SSTableReader>(p1));
        mosaicdb::CompactionEngine::compact(inputs, out);
    }

    {
        mosaicdb::SSTableReader reader(out);
        auto r = reader.get("key");
        ASSERT_TRUE(r.found);
        ASSERT_EQ(r.value, "new_value");
    }

    cleanup();
}

void test_tombstones_removed() {
    setup();

    std::vector<std::pair<std::string, std::optional<std::string>>> e1 = {
        {"alive", "yes"}, {"dead", "old"}
    };
    std::vector<std::pair<std::string, std::optional<std::string>>> e2 = {
        {"dead", std::nullopt}
    };

    std::string p1 = (fs::path(TEST_DIR) / "sst_000000.db").string();
    std::string p2 = (fs::path(TEST_DIR) / "sst_000001.db").string();
    std::string out = (fs::path(TEST_DIR) / "sst_000002.db").string();

    mosaicdb::SSTableWriter::build(p1, e1);
    mosaicdb::SSTableWriter::build(p2, e2);

    {
        std::vector<std::unique_ptr<mosaicdb::SSTableReader>> inputs;
        inputs.push_back(std::make_unique<mosaicdb::SSTableReader>(p2));
        inputs.push_back(std::make_unique<mosaicdb::SSTableReader>(p1));
        mosaicdb::CompactionEngine::compact(inputs, out);
    }

    {
        mosaicdb::SSTableReader reader(out);
        auto r1 = reader.get("alive");
        ASSERT_TRUE(r1.found);
        ASSERT_EQ(r1.value, "yes");

        auto r2 = reader.get("dead");
        ASSERT_TRUE(!r2.found);
    }

    cleanup();
}

void test_engine_auto_compaction() {
    setup();
    {
        mosaicdb::StorageEngine engine(TEST_DIR, 128);

        for (int i = 0; i < 200; ++i)
            engine.put("ek_" + std::to_string(i), std::string(10, 'x'));

        ASSERT_TRUE(engine.sstable_count() < 4);

        for (int i = 0; i < 200; ++i) {
            auto [s, v] = engine.get("ek_" + std::to_string(i));
            ASSERT_EQ(s, mosaicdb::Status::OK);
        }
    }
    cleanup();
}

int main() {
    std::cout << "=== Compaction Tests ===" << std::endl;

    TEST(test_merge_two_sstables);
    TEST(test_duplicate_keys_latest_wins);
    TEST(test_tombstones_removed);
    TEST(test_engine_auto_compaction);

    std::cout << std::endl;
    std::cout << tests_passed << "/" << tests_run << " tests passed." << std::endl;

    return (tests_passed == tests_run) ? 0 : 1;
}
