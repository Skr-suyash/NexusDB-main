#include <iostream>
#include <cassert>
#include <string>
#include "mosaicdb/memtable.h"

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

void test_put_get() {
    mosaicdb::MemTable mt;
    mt.put("hello", "world");

    auto result = mt.get("hello");
    ASSERT_TRUE(result.found);
    ASSERT_FALSE(result.is_tombstone);
    ASSERT_EQ(result.value, "world");
}

void test_get_missing() {
    mosaicdb::MemTable mt;
    auto result = mt.get("missing");
    ASSERT_FALSE(result.found);
}

void test_overwrite() {
    mosaicdb::MemTable mt;
    mt.put("key", "val1");
    mt.put("key", "val2");

    auto result = mt.get("key");
    ASSERT_TRUE(result.found);
    ASSERT_EQ(result.value, "val2");
}

void test_delete() {
    mosaicdb::MemTable mt;
    mt.put("key", "value");
    mt.remove("key");

    auto result = mt.get("key");
    ASSERT_TRUE(result.found);
    ASSERT_TRUE(result.is_tombstone);
}

void test_delete_nonexistent() {
    mosaicdb::MemTable mt;
    mt.remove("ghost");

    auto result = mt.get("ghost");
    ASSERT_TRUE(result.found);
    ASSERT_TRUE(result.is_tombstone);
}

void test_dump_sorted() {
    mosaicdb::MemTable mt;
    mt.put("cherry", "3");
    mt.put("apple", "1");
    mt.put("banana", "2");

    auto entries = mt.dump_sorted();
    ASSERT_EQ(entries.size(), 3u);
    ASSERT_EQ(entries[0].first, "apple");
    ASSERT_EQ(entries[1].first, "banana");
    ASSERT_EQ(entries[2].first, "cherry");
}

void test_should_flush() {
    mosaicdb::MemTable mt(100);

    ASSERT_FALSE(mt.should_flush());

    mt.put("key", std::string(50, 'x'));
    ASSERT_FALSE(mt.should_flush());

    mt.put("key2", std::string(50, 'y'));
    ASSERT_TRUE(mt.should_flush());
}

void test_clear() {
    mosaicdb::MemTable mt;
    mt.put("a", "1");
    mt.put("b", "2");
    mt.clear();

    ASSERT_EQ(mt.entry_count(), 0u);
    ASSERT_EQ(mt.size_bytes(), 0u);

    auto result = mt.get("a");
    ASSERT_FALSE(result.found);
}

void test_size_tracking() {
    mosaicdb::MemTable mt;

    ASSERT_EQ(mt.size_bytes(), 0u);

    mt.put("abc", "defgh");
    ASSERT_EQ(mt.size_bytes(), 8u);

    mt.put("abc", "xy");
    ASSERT_EQ(mt.size_bytes(), 5u);
}

int main() {
    std::cout << "=== MemTable Tests ===" << std::endl;

    TEST(test_put_get);
    TEST(test_get_missing);
    TEST(test_overwrite);
    TEST(test_delete);
    TEST(test_delete_nonexistent);
    TEST(test_dump_sorted);
    TEST(test_should_flush);
    TEST(test_clear);
    TEST(test_size_tracking);

    std::cout << std::endl;
    std::cout << tests_passed << "/" << tests_run << " tests passed." << std::endl;

    return (tests_passed == tests_run) ? 0 : 1;
}
