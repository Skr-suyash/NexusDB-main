#include "mosaicdb/server.h"
#include <iostream>
#include <cstring>

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#endif

namespace mosaicdb {

Server::Server(StorageEngine& engine, uint16_t port)
    : engine_(engine), port_(port), listen_sock_(INVALID_SOCK), running_(false) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

Server::~Server() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

void Server::close_socket(socket_t sock) {
    if (sock == INVALID_SOCK) return;
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

bool Server::send_all(socket_t sock, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = send(sock, reinterpret_cast<const char*>(data + sent),
                     static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool Server::recv_all(socket_t sock, uint8_t* data, size_t len) {
    size_t total = 0;
    while (total < len) {
        int n = recv(sock, reinterpret_cast<char*>(data + total),
                     static_cast<int>(len - total), 0);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

void Server::start() {
    listen_sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock_ == INVALID_SOCK) {
        std::cerr << "[MosaicDB Server] Failed to create socket" << std::endl;
        return;
    }

    int opt = 1;
    setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listen_sock_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[MosaicDB Server] Failed to bind port " << port_ << std::endl;
        close_socket(listen_sock_);
        listen_sock_ = INVALID_SOCK;
        return;
    }

    if (listen(listen_sock_, 16) != 0) {
        std::cerr << "[MosaicDB Server] Failed to listen" << std::endl;
        close_socket(listen_sock_);
        listen_sock_ = INVALID_SOCK;
        return;
    }

    running_ = true;
    std::cerr << "[MosaicDB Server] Listening on port " << port_ << std::endl;

    accept_thread_ = std::thread(&Server::accept_loop, this);
}

void Server::stop() {
    running_ = false;

    if (listen_sock_ != INVALID_SOCK) {
        close_socket(listen_sock_);
        listen_sock_ = INVALID_SOCK;
    }

    if (accept_thread_.joinable())
        accept_thread_.join();

    std::lock_guard<std::mutex> lock(threads_mutex_);
    for (auto& t : client_threads_) {
        if (t.joinable()) t.join();
    }
    client_threads_.clear();
}

void Server::accept_loop() {
    while (running_) {
        struct sockaddr_in client_addr{};
        int addr_len = sizeof(client_addr);
        socket_t client = accept(listen_sock_,
                                 reinterpret_cast<struct sockaddr*>(&client_addr),
#ifdef _WIN32
                                 &addr_len);
#else
                                 reinterpret_cast<socklen_t*>(&addr_len));
#endif

        if (client == INVALID_SOCK) {
            if (!running_) break;
            continue;
        }

        std::lock_guard<std::mutex> lock(threads_mutex_);
        client_threads_.emplace_back(&Server::handle_client, this, client);
    }
}

void Server::handle_client(socket_t client_sock) {
    while (running_) {
        uint8_t header[9];
        if (!recv_all(client_sock, header, 9)) break;

        uint8_t type = header[0];
        uint32_t key_size = decode_u32_le(header + 1);
        uint32_t val_size = decode_u32_le(header + 5);

        if (key_size > 10 * 1024 * 1024 || val_size > 100 * 1024 * 1024) break;

        std::string key(key_size, '\0');
        if (key_size > 0 && !recv_all(client_sock, reinterpret_cast<uint8_t*>(&key[0]), key_size))
            break;

        std::string value(val_size, '\0');
        if (val_size > 0 && !recv_all(client_sock, reinterpret_cast<uint8_t*>(&value[0]), val_size))
            break;

        uint8_t resp_status = RESP_ERROR;
        std::string resp_value;

        switch (type) {
            case PROTO_PUT: {
                Status s = engine_.put(key, value);
                resp_status = (s == Status::OK) ? RESP_OK : RESP_ERROR;
                break;
            }
            case PROTO_GET: {
                auto [s, v] = engine_.get(key);
                if (s == Status::OK) {
                    resp_status = RESP_OK;
                    resp_value = v;
                } else if (s == Status::NOT_FOUND) {
                    resp_status = RESP_NOT_FOUND;
                } else {
                    resp_status = RESP_ERROR;
                }
                break;
            }
            case PROTO_DELETE: {
                Status s = engine_.remove(key);
                resp_status = (s == Status::OK) ? RESP_OK : RESP_ERROR;
                break;
            }
            case PROTO_SCAN: {
                auto entries = engine_.scan(key, value);
                resp_status = RESP_OK;
                uint32_t count = static_cast<uint32_t>(entries.size());
                uint8_t count_buf[4];
                encode_u32_le(count_buf, count);
                resp_value.append(reinterpret_cast<const char*>(count_buf), 4);
                for (const auto& [k, v] : entries) {
                    uint32_t ks = static_cast<uint32_t>(k.size());
                    uint32_t vs = static_cast<uint32_t>(v.size());
                    uint8_t size_buf[8];
                    encode_u32_le(size_buf, ks);
                    encode_u32_le(size_buf + 4, vs);
                    resp_value.append(reinterpret_cast<const char*>(size_buf), 8);
                    resp_value.append(k);
                    resp_value.append(v);
                }
                break;
            }
            default:
                resp_status = RESP_ERROR;
                break;
        }

        uint8_t resp_header[5];
        resp_header[0] = resp_status;
        encode_u32_le(resp_header + 1, static_cast<uint32_t>(resp_value.size()));

        if (!send_all(client_sock, resp_header, 5)) break;
        if (!resp_value.empty()) {
            if (!send_all(client_sock, reinterpret_cast<const uint8_t*>(resp_value.data()),
                          resp_value.size())) break;
        }
    }

    close_socket(client_sock);
}

}
