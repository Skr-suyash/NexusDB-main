#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include "mosaicdb/storage_engine.h"
#include "mosaicdb/catalog.h"
#include "mosaicdb/executor.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#define INVALID_SOCK INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
using socket_t = int;
#define INVALID_SOCK (-1)
#endif

namespace mosaicdb {

constexpr uint16_t DEFAULT_PORT = 7690;
constexpr uint8_t PROTO_PUT = 0x01;
constexpr uint8_t PROTO_GET = 0x02;
constexpr uint8_t PROTO_DELETE = 0x03;
constexpr uint8_t PROTO_SCAN = 0x04;
constexpr uint8_t PROTO_SQL  = 0x05;
constexpr uint8_t RESP_OK = 0x00;
constexpr uint8_t RESP_NOT_FOUND = 0x01;
constexpr uint8_t RESP_ERROR = 0x02;

class Server {
public:
    Server(StorageEngine& engine, uint16_t port = DEFAULT_PORT);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void start();
    void stop();
    bool is_running() const { return running_.load(); }

private:
    StorageEngine& engine_;
    Catalog catalog_;
    Executor executor_;
    uint16_t port_;
    socket_t listen_sock_;
    std::atomic<bool> running_;
    std::thread accept_thread_;
    std::vector<std::thread> client_threads_;
    std::mutex threads_mutex_;

    void accept_loop();
    void handle_client(socket_t client_sock);

    static bool send_all(socket_t sock, const uint8_t* data, size_t len);
    static bool recv_all(socket_t sock, uint8_t* data, size_t len);
    static void close_socket(socket_t sock);
};

}
