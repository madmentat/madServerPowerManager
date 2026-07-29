#include "madspm/server_client.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {
int failures = 0;
void expect(const char* name, bool condition) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}
} // namespace

int main() {
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return EXIT_FAILURE;
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listener, 1) != 0) {
        ::close(listener);
        return EXIT_FAILURE;
    }
    socklen_t address_size = sizeof(address);
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &address_size);

    madspm::ServerConfig config;
    config.host = "127.0.0.1";
    config.port = ntohs(address.sin_port);
    config.connect_timeout_seconds = 1;
    const madspm::ServerClient client(config);
    expect("detects reachable TCP endpoint", client.reachable());
    ::close(listener);
    expect("detects closed TCP endpoint", !client.reachable());

    const int hanging_listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (hanging_listener < 0) return EXIT_FAILURE;
    sockaddr_in hanging_address {};
    hanging_address.sin_family = AF_INET;
    hanging_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    hanging_address.sin_port = 0;
    if (::bind(hanging_listener,
               reinterpret_cast<sockaddr*>(&hanging_address),
               sizeof(hanging_address)) != 0 ||
        ::listen(hanging_listener, 1) != 0) {
        ::close(hanging_listener);
        return EXIT_FAILURE;
    }
    socklen_t hanging_size = sizeof(hanging_address);
    ::getsockname(hanging_listener,
                  reinterpret_cast<sockaddr*>(&hanging_address),
                  &hanging_size);
    std::thread hanging_server([hanging_listener] {
        const int connection = ::accept(hanging_listener, nullptr, nullptr);
        if (connection >= 0) {
            char byte = 0;
            while (::recv(connection, &byte, 1, 0) > 0) {}
            ::close(connection);
        }
    });
    madspm::ServerConfig timeout_config;
    timeout_config.host = "127.0.0.1";
    timeout_config.port = ntohs(hanging_address.sin_port);
    timeout_config.connect_timeout_seconds = 10;
    timeout_config.command_timeout_seconds = 1;
    std::string timeout_error;
    const auto timeout_started = std::chrono::steady_clock::now();
    const bool ssh_result =
        madspm::ServerClient(timeout_config).test_ssh(timeout_error);
    const auto timeout_elapsed = std::chrono::steady_clock::now() -
                                 timeout_started;
    hanging_server.join();
    ::close(hanging_listener);
    expect("hung SSH command is terminated", !ssh_result);
    expect("SSH timeout is reported",
           timeout_error.find("ssh timeout") != std::string::npos);
    expect("SSH timeout is bounded",
           timeout_elapsed < std::chrono::seconds(3));
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
