#include <iostream>
#include <string>
#include "mosaicdb/storage_engine.h"
#include "mosaicdb/query.h"

int main(int argc, char* argv[]) {
    std::string data_dir = "mosaicdb_data";
    if (argc > 1)
        data_dir = argv[1];

    try {
        mosaicdb::StorageEngine engine(data_dir);
        std::string line;

        std::cout << "MosaicDB v0.1.0 - Phase 4" << std::endl;
        std::cout << "Commands: PUT key value | GET key | DELETE key | SCAN start_key end_key" << std::endl;
        std::cout << "Type 'quit' or 'exit' to exit." << std::endl;
        std::cout << std::endl;

        while (true) {
            std::cout << "MosaicDB> ";
            std::cout.flush();

            if (!std::getline(std::cin, line)) break;
            if (line.empty()) continue;
            if (line == "quit" || line == "exit") break;

            auto cmd = mosaicdb::QueryParser::parse(line);

            switch (cmd.type) {
                case mosaicdb::CommandType::PUT: {
                    if (cmd.key.empty() || cmd.value.empty()) {
                        std::cout << "Usage: PUT key value" << std::endl;
                        break;
                    }
                    auto status = engine.put(cmd.key, cmd.value);
                    std::cout << mosaicdb::status_to_string(status) << std::endl;
                    break;
                }
                case mosaicdb::CommandType::GET: {
                    if (cmd.key.empty()) {
                        std::cout << "Usage: GET key" << std::endl;
                        break;
                    }
                    auto [status, value] = engine.get(cmd.key);
                    if (status == mosaicdb::Status::OK)
                        std::cout << value << std::endl;
                    else
                        std::cout << mosaicdb::status_to_string(status) << std::endl;
                    break;
                }
                case mosaicdb::CommandType::DELETE_CMD: {
                    if (cmd.key.empty()) {
                        std::cout << "Usage: DELETE key" << std::endl;
                        break;
                    }
                    auto status = engine.remove(cmd.key);
                    std::cout << mosaicdb::status_to_string(status) << std::endl;
                    break;
                }
                case mosaicdb::CommandType::SCAN: {
                    if (cmd.key.empty() || cmd.end_key.empty()) {
                        std::cout << "Usage: SCAN start_key end_key" << std::endl;
                        break;
                    }
                    auto results = engine.scan(cmd.key, cmd.end_key);
                    for (const auto& [k, v] : results) {
                        std::cout << k << " => " << v << std::endl;
                    }
                    std::cout << "Scanned " << results.size() << " records." << std::endl;
                    break;
                }
                default: {
                    std::cout << "Unknown command. Use PUT, GET, DELETE, or SCAN." << std::endl;
                    break;
                }
            }
        }

        std::cout << "Goodbye." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
