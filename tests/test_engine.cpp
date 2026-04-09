#include <iostream>
#include <cassert>
#include <filesystem>
#include <string>
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

static const std::string TEST_DIR = "test_engine_data";
void cleanup() { fs::remove_all(TEST_DIR); }
void setup() { cleanup(); }

void test_put_get() {
    setup();
    {
        mosaicdb::StorageEngine engine(TEST_DIR);
        ASSERT_EQ(engine.put("name", "MosaicDB"), mosaicdb::Status::OK);

        auto [status, value] = engine.get("name");
        ASSERT_EQ(status, mosaicdb::Status::OK);
        ASSERT_EQ(value, "MosaicDB");
    }
    cleanup();
}

void test_get_missing() {
    setup();
    {
        mosaicdb::StorageEngine engine(TEST_DIR);
        auto [status, value] = engine.get("nonexistent");
        ASSERT_EQ(status, mosaicdb::Status::NOT_FOUND);
    }
    cleanup();
}

void test_delete() {
    setup();
    {
        mosaicdb::StorageEngine engine(TEST_DIR);
        engine.put("key", "value");
        engine.remove("key");

        auto [status, value] = engine.get("key");
        ASSERT_EQ(status, mosaicdb::Status::NOT_FOUND);
    }
    cleanup();
}

void test_overwrite() {
    setup();
    {
        mosaicdb::StorageEngine engine(TEST_DIR);
        engine.put("key", "v1");
        engine.put("key", "v2");
        engine.put("key", "v3");

        auto [status, value] = engine.get("key");
        ASSERT_EQ(status, mosaicdb::Status::OK);
        ASSERT_EQ(value, "v3");
    }
    cleanup();
}

void test_crash_recovery() {
    setup();

    {
        mosaicdb::StorageEngine engine(TEST_DIR);
        engine.put("alpha", "1");
        engine.put("beta", "2");
        engine.put("gamma", "3");
        engine.remove("beta");
    }

    {
        mosaicdb::StorageEngine engine(TEST_DIR);

        auto [s1, v1] = engine.get("alpha");
        ASSERT_EQ(s1, mosaicdb::Status::OK);
        ASSERT_EQ(v1, "1");

        auto [s2, v2] = engine.get("beta");
        ASSERT_EQ(s2, mosaicdb::Status::NOT_FOUND);

        auto [s3, v3] = engine.get("gamma");
        ASSERT_EQ(s3, mosaicdb::Status::OK);
        ASSERT_EQ(v3, "3");
    }

    cleanup();
}

void test_recovery_with_overwrites() {
    setup();

    {
        mosaicdb::StorageEngine engine(TEST_DIR);
        engine.put("x", "old");
        engine.put("x", "new");
    }

    {
        mosaicdb::StorageEngine engine(TEST_DIR);
        auto [status, value] = engine.get("x");
        ASSERT_EQ(status, mosaicdb::Status::OK);
        ASSERT_EQ(value, "new");
    }

    cleanup();
}

void test_many_keys() {
    setup();
    const int N = 5000;

    {
        mosaicdb::StorageEngine engine(TEST_DIR);
        for (int i = 0; i < N; ++i)
            engine.put("k" + std::to_string(i), "v" + std::to_string(i));
    }

    {
        mosaicdb::StorageEngine engine(TEST_DIR);
        for (int i = 0; i < N; ++i) {
            auto [status, value] = engine.get("k" + std::to_string(i));
            ASSERT_EQ(status, mosaicdb::Status::OK);
            ASSERT_EQ(value, "v" + std::to_string(i));
        }
    }

    cleanup();
}

void test_flush_creates_sstable() {
    setup();
    {
        mosaicdb::StorageEngine engine(TEST_DIR, 512);
        for (int i = 0; i < 100; ++i)
            engine.put("key_" + std::to_string(i), std::string(20, 'v'));
        ASSERT_TRUE(engine.sstable_count() > 0);
    }
    cleanup();
}

void test_read_from_sstable_after_restart() {
    setup();
    {
        mosaicdb::StorageEngine engine(TEST_DIR, 256);
        for (int i = 0; i < 50; ++i)
            engine.put("rk_" + std::to_string(i), "rv_" + std::to_string(i));
    }
    {
        mosaicdb::StorageEngine engine(TEST_DIR, 256);
        for (int i = 0; i < 50; ++i) {
            auto [s, v] = engine.get("rk_" + std::to_string(i));
            ASSERT_EQ(s, mosaicdb::Status::OK);
            ASSERT_EQ(v, "rv_" + std::to_string(i));
        }
    }
    cleanup();
}

void test_delete_across_flush() {
    setup();
    {
        mosaicdb::StorageEngine engine(TEST_DIR, 256);
        for (int i = 0; i < 30; ++i)
            engine.put("dk_" + std::to_string(i), "dv_" + std::to_string(i));
        for (int i = 0; i < 15; ++i)
            engine.remove("dk_" + std::to_string(i));
    }
    {
        mosaicdb::StorageEngine engine(TEST_DIR, 256);
        for (int i = 0; i < 15; ++i) {
            auto [s, v] = engine.get("dk_" + std::to_string(i));
            ASSERT_EQ(s, mosaicdb::Status::NOT_FOUND);
        }
        for (int i = 15; i < 30; ++i) {
            auto [s, v] = engine.get("dk_" + std::to_string(i));
            ASSERT_EQ(s, mosaicdb::Status::OK);
        }
    }
    cleanup();
}

void test_scan() {
    setup();
    {
        mosaicdb::StorageEngine engine(TEST_DIR, 128);
        engine.put("b", "B");
        engine.put("d", "D");
        engine.put("c", "C");
        engine.put("a", "A");
        engine.put("e", "E");
        engine.put("c1", "C1");

        auto res = engine.scan("b", "d");
        ASSERT_EQ(res.size(), 4);
        ASSERT_EQ(res[0].first, "b");
        ASSERT_EQ(res[1].first, "c");
        ASSERT_EQ(res[2].first, "c1");
        ASSERT_EQ(res[3].first, "d");

        engine.put("c", "C_new");
        engine.remove("c1");
        auto res2 = engine.scan("b", "d");
        ASSERT_EQ(res2.size(), 3);
        ASSERT_EQ(res2[0].first, "b");
        ASSERT_EQ(res2[1].first, "c");
        ASSERT_EQ(res2[1].second, "C_new");
        ASSERT_EQ(res2[2].first, "d");
    }
    cleanup();
}

int main() {
    std::cout << "=== Storage Engine Tests ===" << std::endl;

    TEST(test_put_get);
    TEST(test_get_missing);
    TEST(test_delete);
    TEST(test_overwrite);
    TEST(test_crash_recovery);
    TEST(test_recovery_with_overwrites);
    TEST(test_many_keys);
    TEST(test_flush_creates_sstable);
    TEST(test_read_from_sstable_after_restart);
    TEST(test_delete_across_flush);
    TEST(test_scan);

    std::cout << std::endl;
    std::cout << tests_passed << "/" << tests_run << " tests passed." << std::endl;

    return (tests_passed == tests_run) ? 0 : 1;
}
