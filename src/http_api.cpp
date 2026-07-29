#include "madspm/http_api.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace madspm {

HttpApi::HttpApi(PowerManager& manager) : manager_(manager) {}
HttpApi::~HttpApi() { stop(); }

void HttpApi::start() {
    if (!manager_.config().api.enabled || thread_.joinable()) return;
    stopping_ = false;
    thread_ = std::thread([this] { serve(); });
}

void HttpApi::stop() {
    stopping_ = true;
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (thread_.joinable()) thread_.join();
}

void HttpApi::serve() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return;
    int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(manager_.config().api.port);
    if (::inet_pton(AF_INET, manager_.config().api.listen_address.c_str(),
                    &address.sin_addr) != 1 ||
        ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listen_fd_, 16) != 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }
    while (!stopping_) {
        const int client = ::accept(listen_fd_, nullptr, nullptr);
        if (client < 0) continue;
        char buffer[8192];
        const ssize_t received = ::recv(client, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) { ::close(client); continue; }
        buffer[received] = '\0';
        const std::string request(buffer);
        const auto first_space = request.find(' ');
        const auto second_space = request.find(' ', first_space + 1);
        const std::string method = request.substr(0, first_space);
        const std::string path =
            first_space == std::string::npos || second_space == std::string::npos
                ? ""
                : request.substr(first_space + 1, second_space - first_space - 1);
        std::string body;
        std::string status = "200 OK";
        if (method != "GET") {
            status = "405 Method Not Allowed";
            body = "{\"error\":\"control API disabled\"}";
        } else if (path == "/api/v1/status") body = manager_.status_json();
        else if (path == "/api/v1/ups") body = manager_.ups_json();
        else if (path == "/api/v1/proxmox") body = manager_.proxmox_json();
        else if (path == "/api/v1/plug") body = manager_.plug_json();
        else if (path == "/api/v1/events") body = manager_.events_json();
        else if (path == "/api/v1/health") body = manager_.health_json();
        else if (path == "/api/v1/config") body = manager_.config_json();
        else {
            status = "404 Not Found";
            body = "{\"error\":\"not found\"}";
        }
        const std::string response =
            "HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\n"
            "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\n\r\n" + body;
        ::send(client, response.data(), response.size(), MSG_NOSIGNAL);
        ::close(client);
    }
}

} // namespace madspm
