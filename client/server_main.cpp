#include <iostream>
#include <string>
#include <csignal>
#include "mosaicdb/storage_engine.h"
#include "mosaicdb/server.h"

static mosaicdb::Server* g_server = nullptr;

void signal_handler(int) {
    if (g_server) {
        std::cerr << "\n[MosaicDB Server] Shutting down..." << std::endl;
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    std::string data_dir = "mosaicdb_data";
    uint16_t port = mosaicdb::DEFAULT_PORT;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--data" && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: mosaicdb_server [--port PORT] [--data DIR]" << std::endl;
            std::cout << "  --port PORT   TCP port (default: 7690)" << std::endl;
            std::cout << "  --data DIR    Data directory (default: mosaicdb_data)" << std::endl;
            return 0;
        }
    }

    try {
        mosaicdb::StorageEngine engine(data_dir);
        mosaicdb::Server server(engine, port);
        g_server = &server;

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        server.start();

        std::cout << "MosaicDB Server v0.1.0" << std::endl;
        std::cout << "Port: " << port << " | Data: " << data_dir << std::endl;
        std::cout << "Press Ctrl+C to stop." << std::endl;

        while (server.is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        g_server = nullptr;
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
