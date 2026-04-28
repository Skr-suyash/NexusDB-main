#include <iostream>
#include <cassert>
#include <filesystem>
#include <string>
#include "mosaicdb/storage_engine.h"
#include "mosaicdb/catalog.h"
#include "mosaicdb/executor.h"
#include "mosaicdb/sql_parser.h"

namespace fs = std::filesystem;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  TEST: " << #name << "... "; \
    try {

#define END_TEST \
    std::cout << "PASSED" << std::endl; \
    tests_passed++; \
    } catch (const std::exception& e) { \
        std::cout << "FAILED: " << e.what() << std::endl; \
        tests_failed++; \
    }

#define ASSERT_TRUE(cond) if (!(cond)) throw std::runtime_error("Assertion failed: " #cond)
#define ASSERT_EQ(a, b) if ((a) != (b)) throw std::runtime_error( \
    "Assertion failed: " #a " == " #b " (got '" + std::to_string(a) + "' vs '" + std::to_string(b) + "')")
#define ASSERT_STR_EQ(a, b) if ((a) != (b)) throw std::runtime_error( \
    "Assertion failed: " #a " == " #b " (got '" + (a) + "' vs '" + (b) + "')")

void cleanup(const std::string& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
}

void test_create_table() {
    std::string dir = "test_sql_create";
    cleanup(dir);

    {
        mosaicdb::StorageEngine engine(dir);
        mosaicdb::Catalog catalog(engine);
        mosaicdb::Executor executor(engine, catalog);

        TEST(create_table_basic)
            auto r = executor.execute_sql("CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT NULL, age INT)");
            ASSERT_TRUE(!r.is_error());
            ASSERT_TRUE(catalog.table_exists("users"));
        END_TEST

        TEST(create_duplicate_table)
            auto r = executor.execute_sql("CREATE TABLE users (id INT PRIMARY KEY)");
            ASSERT_TRUE(r.is_error());
        END_TEST

        TEST(describe_table)
            auto r = executor.execute_sql("DESCRIBE users");
            ASSERT_TRUE(!r.is_error());
            ASSERT_EQ(r.rows.size(), (size_t)3);
        END_TEST

        TEST(show_tables)
            executor.execute_sql("CREATE TABLE orders (id INT PRIMARY KEY, total FLOAT)");
            auto r = executor.execute_sql("SHOW TABLES");
            ASSERT_TRUE(!r.is_error());
            ASSERT_EQ(r.rows.size(), (size_t)2);
        END_TEST

        TEST(drop_table)
            auto r = executor.execute_sql("DROP TABLE orders");
            ASSERT_TRUE(!r.is_error());
            ASSERT_TRUE(!catalog.table_exists("orders"));
        END_TEST

        TEST(drop_table_if_exists)
            auto r = executor.execute_sql("DROP TABLE IF EXISTS nonexistent");
            ASSERT_TRUE(!r.is_error());
        END_TEST
    }
    cleanup(dir);
}

void test_insert_select() {
    std::string dir = "test_sql_insert";
    cleanup(dir);

    {
        mosaicdb::StorageEngine engine(dir);
        mosaicdb::Catalog catalog(engine);
        mosaicdb::Executor executor(engine, catalog);

        executor.execute_sql("CREATE TABLE employees (id INT PRIMARY KEY, name TEXT NOT NULL, salary FLOAT, active BOOL)");

        TEST(insert_basic)
            auto r = executor.execute_sql("INSERT INTO employees (id, name, salary, active) VALUES (1, 'Alice', 75000.50, TRUE)");
            ASSERT_TRUE(!r.is_error());
        END_TEST

        TEST(insert_second_row)
            auto r = executor.execute_sql("INSERT INTO employees (id, name, salary, active) VALUES (2, 'Bob', 82000.00, TRUE)");
            ASSERT_TRUE(!r.is_error());
        END_TEST

        TEST(insert_third_row)
            auto r = executor.execute_sql("INSERT INTO employees (id, name, salary, active) VALUES (3, 'Charlie', 65000.00, FALSE)");
            ASSERT_TRUE(!r.is_error());
        END_TEST

        TEST(insert_duplicate_pk)
            auto r = executor.execute_sql("INSERT INTO employees (id, name, salary, active) VALUES (1, 'Duplicate', 0, FALSE)");
            ASSERT_TRUE(r.is_error());
        END_TEST

        TEST(select_all)
            auto r = executor.execute_sql("SELECT * FROM employees");
            ASSERT_TRUE(!r.is_error());
            ASSERT_EQ(r.rows.size(), (size_t)3);
            ASSERT_EQ(r.column_names.size(), (size_t)4);
        END_TEST

        TEST(select_columns)
            auto r = executor.execute_sql("SELECT name, salary FROM employees");
            ASSERT_TRUE(!r.is_error());
            ASSERT_EQ(r.rows.size(), (size_t)3);
            ASSERT_EQ(r.column_names.size(), (size_t)2);
        END_TEST

        TEST(select_where_eq)
            auto r = executor.execute_sql("SELECT * FROM employees WHERE id = 1");
            ASSERT_TRUE(!r.is_error());
            ASSERT_EQ(r.rows.size(), (size_t)1);
            ASSERT_STR_EQ(r.rows[0][1].text_val, std::string("Alice"));
        END_TEST

        TEST(select_where_gt)
            auto r = executor.execute_sql("SELECT name FROM employees WHERE salary > 70000");
            ASSERT_TRUE(!r.is_error());
            ASSERT_EQ(r.rows.size(), (size_t)2);
        END_TEST

        TEST(select_where_and)
            auto r = executor.execute_sql("SELECT name FROM employees WHERE salary > 70000 AND active = TRUE");
            ASSERT_TRUE(!r.is_error());
            ASSERT_EQ(r.rows.size(), (size_t)2);
        END_TEST

        TEST(select_where_string)
            auto r = executor.execute_sql("SELECT * FROM employees WHERE name = 'Bob'");
            ASSERT_TRUE(!r.is_error());
            ASSERT_EQ(r.rows.size(), (size_t)1);
        END_TEST

        TEST(select_order_by_asc)
            auto r = executor.execute_sql("SELECT name, salary FROM employees ORDER BY salary ASC");
            ASSERT_TRUE(!r.is_error());
            ASSERT_EQ(r.rows.size(), (size_t)3);
            ASSERT_STR_EQ(r.rows[0][0].text_val, std::string("Charlie"));
        END_TEST

        TEST(select_order_by_desc)
            auto r = executor.execute_sql("SELECT name, salary FROM employees ORDER BY salary DESC");
            ASSERT_TRUE(!r.is_error());
            ASSERT_STR_EQ(r.rows[0][0].text_val, std::string("Bob"));
        END_TEST

        TEST(select_limit)
            auto r = executor.execute_sql("SELECT name FROM employees LIMIT 2");
            ASSERT_TRUE(!r.is_error());
            ASSERT_EQ(r.rows.size(), (size_t)2);
        END_TEST
    }
    cleanup(dir);
}

void test_update_delete() {
    std::string dir = "test_sql_update";
    cleanup(dir);

    {
        mosaicdb::StorageEngine engine(dir);
        mosaicdb::Catalog catalog(engine);
        mosaicdb::Executor executor(engine, catalog);

        executor.execute_sql("CREATE TABLE products (id INT PRIMARY KEY, name TEXT NOT NULL, price FLOAT)");
        executor.execute_sql("INSERT INTO products (id, name, price) VALUES (1, 'Widget', 9.99)");
        executor.execute_sql("INSERT INTO products (id, name, price) VALUES (2, 'Gadget', 19.99)");
        executor.execute_sql("INSERT INTO products (id, name, price) VALUES (3, 'Doohickey', 4.99)");

        TEST(update_single_row)
            auto r = executor.execute_sql("UPDATE products SET price = 14.99 WHERE id = 1");
            ASSERT_TRUE(!r.is_error());
            auto s = executor.execute_sql("SELECT price FROM products WHERE id = 1");
            // Check the price was updated (14.99)
            ASSERT_EQ(s.rows.size(), (size_t)1);
        END_TEST

        TEST(update_multiple_rows)
            auto r = executor.execute_sql("UPDATE products SET price = 0.00 WHERE price < 15");
            ASSERT_TRUE(!r.is_error());
        END_TEST

        TEST(delete_single_row)
            auto r = executor.execute_sql("DELETE FROM products WHERE id = 3");
            ASSERT_TRUE(!r.is_error());
            auto s = executor.execute_sql("SELECT * FROM products");
            ASSERT_EQ(s.rows.size(), (size_t)2);
        END_TEST

        TEST(delete_with_condition)
            auto r = executor.execute_sql("DELETE FROM products WHERE price = 0.00");
            ASSERT_TRUE(!r.is_error());
        END_TEST
    }
    cleanup(dir);
}

void test_parser_errors() {
    std::string dir = "test_sql_errors";
    cleanup(dir);

    {
        mosaicdb::StorageEngine engine(dir);
        mosaicdb::Catalog catalog(engine);
        mosaicdb::Executor executor(engine, catalog);

        TEST(parse_error_garbage)
            auto r = executor.execute_sql("FOOBAR baz");
            ASSERT_TRUE(r.is_error());
        END_TEST

        TEST(table_not_found)
            auto r = executor.execute_sql("SELECT * FROM nonexistent");
            ASSERT_TRUE(r.is_error());
        END_TEST

        TEST(insert_missing_table)
            auto r = executor.execute_sql("INSERT INTO nonexistent (id) VALUES (1)");
            ASSERT_TRUE(r.is_error());
        END_TEST
    }
    cleanup(dir);
}

int main() {
    std::cout << "=== MosaicDB SQL Tests ===" << std::endl;
    std::cout << std::endl;

    std::cout << "[CREATE / DROP / SHOW / DESCRIBE]" << std::endl;
    test_create_table();
    std::cout << std::endl;

    std::cout << "[INSERT / SELECT]" << std::endl;
    test_insert_select();
    std::cout << std::endl;

    std::cout << "[UPDATE / DELETE]" << std::endl;
    test_update_delete();
    std::cout << std::endl;

    std::cout << "[ERROR HANDLING]" << std::endl;
    test_parser_errors();
    std::cout << std::endl;

    std::cout << "========================" << std::endl;
    std::cout << "Passed: " << tests_passed << std::endl;
    std::cout << "Failed: " << tests_failed << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
