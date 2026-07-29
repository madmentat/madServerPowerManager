#include "madspm/server_client.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <memory>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <chrono>
#include <thread>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace madspm {

ServerClient::ServerClient(ServerConfig config) : config_(std::move(config)) {}

bool ServerClient::reachable() const {
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* raw = nullptr;
    const std::string port = std::to_string(config_.port);
    if (::getaddrinfo(config_.host.c_str(), port.c_str(), &hints, &raw) != 0) return false;
    std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> addresses(raw, ::freeaddrinfo);
    for (auto* current = raw; current; current = current->ai_next) {
        const int fd = ::socket(current->ai_family, SOCK_STREAM | SOCK_NONBLOCK,
                                current->ai_protocol);
        if (fd < 0) continue;
        const int result = ::connect(fd, current->ai_addr, current->ai_addrlen);
        if (result == 0) { ::close(fd); return true; }
        pollfd descriptor {fd, POLLOUT, 0};
        const int ready = ::poll(&descriptor, 1,
            static_cast<int>(config_.connect_timeout_seconds * 1000));
        int error = 1;
        socklen_t length = sizeof(error);
        if (ready > 0) ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length);
        ::close(fd);
        if (ready > 0 && error == 0) return true;
    }
    return false;
}

bool ServerClient::run_ssh(const std::string& command, std::string& error) const {
    const pid_t pid = ::fork();
    if (pid < 0) { error = std::strerror(errno); return false; }
    if (pid == 0) {
        if (::setsid() < 0) _exit(126);
        const std::string destination = config_.user + "@" + config_.host;
        const std::string port = std::to_string(config_.port);
        const std::string timeout = "ConnectTimeout=" +
                                    std::to_string(config_.connect_timeout_seconds);
        ::execl("/usr/bin/ssh", "ssh",
                "-o", "BatchMode=yes",
                "-o", "StrictHostKeyChecking=yes",
                "-o", "PasswordAuthentication=no",
                "-o", timeout.c_str(),
                "-o", "ForwardAgent=no",
                "-o", "ClearAllForwardings=yes",
                "-o", "RequestTTY=no",
                "-p", port.c_str(),
                "-i", config_.private_key.c_str(),
                "-o", ("UserKnownHostsFile=" + config_.known_hosts.string()).c_str(),
                destination.c_str(), command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(config_.command_timeout_seconds);
    for (;;) {
        const pid_t result = ::waitpid(pid, &status, WNOHANG);
        if (result == pid) break;
        if (result < 0) {
            if (errno == EINTR) continue;
            error = std::strerror(errno);
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            ::kill(-pid, SIGTERM);
            const auto terminate_deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(1);
            while (std::chrono::steady_clock::now() < terminate_deadline) {
                const pid_t terminated = ::waitpid(pid, &status, WNOHANG);
                if (terminated == pid) {
                    error = "ssh timeout after " +
                            std::to_string(config_.command_timeout_seconds) + "s";
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            ::kill(-pid, SIGKILL);
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            error = "ssh timeout after " +
                    std::to_string(config_.command_timeout_seconds) + "s";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
    error = WIFEXITED(status) ? "ssh exit=" + std::to_string(WEXITSTATUS(status))
                             : "ssh завершён сигналом";
    return false;
}

bool ServerClient::request_shutdown(std::string& error) const {
    return run_ssh(config_.forced_command, error);
}

bool ServerClient::test_ssh(std::string& error) const {
    return run_ssh("status", error);
}

} // namespace madspm
