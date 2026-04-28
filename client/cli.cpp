#include <iostream>
#include <string>
#include <iomanip>
#include <algorithm>
#include <vector>
#include "mosaicdb/storage_engine.h"
#include "mosaicdb/catalog.h"
#include "mosaicdb/executor.h"
#include "mosaicdb/sql_parser.h"

// Format a table with borders for SELECT results
void print_table(const std::vector<std::string>& headers,
                 const std::vector<std::vector<mosaicdb::Value>>& rows) {
    if (headers.empty()) return;

    // Calculate column widths
    std::vector<size_t> widths(headers.size());
    for (size_t i = 0; i < headers.size(); ++i) {
        widths[i] = headers[i].size();
    }
    for (const auto& row : rows) {
        for (size_t i = 0; i < row.size() && i < widths.size(); ++i) {
            widths[i] = std::max(widths[i], row[i].to_string().size());
        }
    }

    // Print separator line
    auto print_sep = [&]() {
        std::cout << "+";
        for (size_t i = 0; i < widths.size(); ++i) {
            std::cout << std::string(widths[i] + 2, '-') << "+";
        }
        std::cout << std::endl;
    };

    // Print header
    print_sep();
    std::cout << "|";
    for (size_t i = 0; i < headers.size(); ++i) {
        std::cout << " " << std::left << std::setw(static_cast<int>(widths[i])) << headers[i] << " |";
    }
    std::cout << std::endl;
    print_sep();

    // Print rows
    for (const auto& row : rows) {
        std::cout << "|";
        for (size_t i = 0; i < headers.size(); ++i) {
            std::string val = (i < row.size()) ? row[i].to_string() : "NULL";
            std::cout << " " << std::left << std::setw(static_cast<int>(widths[i])) << val << " |";
        }
        std::cout << std::endl;
    }
    print_sep();
}

int main(int argc, char* argv[]) {
    std::string data_dir = "mosaicdb_data";
    if (argc > 1)
        data_dir = argv[1];

    try {
        mosaicdb::StorageEngine engine(data_dir);
        mosaicdb::Catalog catalog(engine);
        mosaicdb::Executor executor(engine, catalog);

        std::string line;

        std::cout << "MosaicDB v0.2.0 - SQL Mode" << std::endl;
        std::cout << "Supported: CREATE TABLE, INSERT, SELECT, UPDATE, DELETE, DROP TABLE" << std::endl;
        std::cout << "Meta:      SHOW TABLES, DESCRIBE <table>" << std::endl;
        std::cout << "Type 'quit' or 'exit' to exit." << std::endl;
        std::cout << std::endl;

        while (true) {
            std::cout << "MosaicSQL> ";
            std::cout.flush();

            if (!std::getline(std::cin, line)) break;
            if (line.empty()) continue;
            if (line == "quit" || line == "exit") break;

            auto result = executor.execute_sql(line);

            if (result.is_error()) {
                std::cout << "ERROR: " << result.message << std::endl;
            } else if (!result.column_names.empty() && !result.rows.empty()) {
                // SELECT-style result with table output
                print_table(result.column_names, result.rows);
                std::cout << result.message << std::endl;
            } else if (!result.column_names.empty() && result.rows.empty()) {
                // SELECT with no results
                print_table(result.column_names, {});
                std::cout << "0 row(s) returned." << std::endl;
            } else {
                std::cout << result.message << std::endl;
            }
        }

        std::cout << "Goodbye." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
