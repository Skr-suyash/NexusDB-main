#include <iostream>
#include <string>
#include "mosaicdb/bloom_filter.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        std::cout << "  [RUN ] " << #name << std::endl; \
        try { name(); tests_passed++; std::cout << "  [PASS] " << #name << std::endl; } \
        catch (const std::exception& e) { std::cout << "  [FAIL] " << #name << ": " << e.what() << std::endl; } \
    } while(0)

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) throw std::runtime_error("Assertion failed: " #expr); } while(0)

#define ASSERT_FALSE(expr) \
    do { if ((expr)) throw std::runtime_error("Assertion failed: NOT " #expr); } while(0)

void test_add_and_check() {
    auto bf = mosaicdb::BloomFilter::create_for_entries(100);
    bf.add("hello");
    bf.add("world");
    bf.add("mosaicdb");

    ASSERT_TRUE(bf.possibly_contains("hello"));
    ASSERT_TRUE(bf.possibly_contains("world"));
    ASSERT_TRUE(bf.possibly_contains("mosaicdb"));
}

void test_negative_lookup() {
    auto bf = mosaicdb::BloomFilter::create_for_entries(1000);
    for (int i = 0; i < 100; ++i)
        bf.add("key_" + std::to_string(i));

    for (int i = 0; i < 100; ++i)
        ASSERT_TRUE(bf.possibly_contains("key_" + std::to_string(i)));

    int false_positives = 0;
    for (int i = 1000; i < 2000; ++i) {
        if (bf.possibly_contains("miss_" + std::to_string(i)))
            false_positives++;
    }

    double fpr = static_cast<double>(false_positives) / 1000.0;
    std::cout << "    FPR: " << (fpr * 100.0) << "% (" << false_positives << "/1000)" << std::endl;
    ASSERT_TRUE(fpr < 0.05);
}

void test_serialization() {
    auto bf1 = mosaicdb::BloomFilter::create_for_entries(500);
    for (int i = 0; i < 500; ++i)
        bf1.add("item_" + std::to_string(i));

    mosaicdb::BloomFilter bf2(bf1.data(), bf1.data_size(), bf1.num_bits(), bf1.num_hashes());

    for (int i = 0; i < 500; ++i)
        ASSERT_TRUE(bf2.possibly_contains("item_" + std::to_string(i)));
}

void test_empty_filter() {
    mosaicdb::BloomFilter bf(64, 7);
    ASSERT_FALSE(bf.possibly_contains("anything"));
    ASSERT_FALSE(bf.possibly_contains(""));
}

int main() {
    std::cout << "=== Bloom Filter Tests ===" << std::endl;

    TEST(test_add_and_check);
    TEST(test_negative_lookup);
    TEST(test_serialization);
    TEST(test_empty_filter);

    std::cout << std::endl;
    std::cout << tests_passed << "/" << tests_run << " tests passed." << std::endl;

    return (tests_passed == tests_run) ? 0 : 1;
}
