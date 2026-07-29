#include "madspm/manager.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
int failures = 0;
void expect(const char* name, bool condition) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}

class FakeNut {
public:
    explicit FakeNut(std::vector<std::string> responses)
        : responses_(std::move(responses)) {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ < 0) throw std::runtime_error("socket failed");
        int reuse = 1;
        ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(listener_, reinterpret_cast<sockaddr*>(&address),
                   sizeof(address)) != 0 ||
            ::listen(listener_, 4) != 0)
            throw std::runtime_error("bind/listen failed");
        socklen_t size = sizeof(address);
        ::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size);
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this] { serve(); });
    }

    ~FakeNut() {
        if (worker_.joinable()) worker_.join();
        if (listener_ >= 0) ::close(listener_);
    }

    std::uint16_t port() const { return port_; }

private:
    void serve() {
        for (const auto& response : responses_) {
            const int client = ::accept(listener_, nullptr, nullptr);
            if (client < 0) return;
            char request[256];
            static_cast<void>(::recv(client, request, sizeof(request), 0));
            std::size_t offset = 0;
            while (offset < response.size()) {
                const ssize_t sent =
                    ::send(client, response.data() + offset,
                           response.size() - offset, MSG_NOSIGNAL);
                if (sent <= 0) break;
                offset += static_cast<std::size_t>(sent);
            }
            ::close(client);
        }
    }

    int listener_ = -1;
    std::uint16_t port_ = 0;
    std::vector<std::string> responses_;
    std::thread worker_;
};

std::string nut_response(const std::string& status) {
    return "BEGIN LIST VAR ups\nVAR ups ups.status \"" + status +
           "\"\nVAR ups battery.charge \"80\"\nEND LIST VAR ups\n";
}

madspm::Config base_config(const std::filesystem::path& state_file) {
    madspm::Config config;
    config.general.armed = true;
    config.general.state_file = state_file;
    config.ups.host = "127.0.0.1";
    config.server.host = "127.0.0.1";
    config.server.port = 1;
    config.server.connect_timeout_seconds = 1;
    config.plug.host = "127.0.0.1";
    config.plug.port = 1;
    config.plug.command_timeout_seconds = 1;
    return config;
}
} // namespace

int main() {
    const auto directory = std::filesystem::path("/tmp") /
        ("madspm-manager-test-" + std::to_string(::getpid()));
    std::filesystem::create_directories(directory);

    const auto healthy_state = directory / "healthy.json";
    madspm::Config healthy = base_config(healthy_state);
    {
        FakeNut server({nut_response("OL")});
        healthy.ups.port = server.port();
        madspm::PowerManager manager(healthy);
        expect("healthy tick succeeds", manager.run_once(false) == 0);
    }
    const auto first_success = madspm::StateStore(healthy_state).load().last_success_utc;
    expect("healthy tick updates last_success", first_success > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    {
        madspm::PowerManager manager(healthy);
        expect("unreachable NUT fails tick", manager.run_once(false) == 1);
    }
    expect("failed tick preserves last_success",
           madspm::StateStore(healthy_state).load().last_success_utc ==
               first_success);

    const auto retry_state_path = directory / "retry.json";
    madspm::PersistentState retry_state;
    retry_state.state = madspm::State::CuttingServerPower;
    retry_state.last_success_utc = 321;
    madspm::StateStore(retry_state_path).save(retry_state);
    madspm::Config retry = base_config(retry_state_path);
    retry.secrets.tuya_device_id = "test-device";
    retry.secrets.tuya_local_key = "1234567890abcdef";
    retry.plug.retry_seconds = 30;
    retry.server.force_cut_after_shutdown_timeout = true;
    {
        FakeNut server({nut_response("OB"), nut_response("OB")});
        retry.ups.port = server.port();
        madspm::PowerManager manager(retry);
        expect("failed plug command keeps daemon tick alive",
               manager.run_once(false) == 0);
        auto after_failure = madspm::StateStore(retry_state_path).load();
        expect("failed plug command remains retryable",
               after_failure.state == madspm::State::CuttingServerPower &&
                   after_failure.plug_attempts == 1 &&
                   !after_failure.last_error.empty());
        expect("failed plug command is logged",
               manager.events_json().find("ACTION_FAILED") != std::string::npos);
        expect("retry delay tick remains alive", manager.run_once(false) == 0);
    }
    const auto after_delay = madspm::StateStore(retry_state_path).load();
    expect("retry delay prevents immediate second command",
           after_delay.plug_attempts == 1);
    expect("failed action does not advance last_success",
           after_delay.last_success_utc == 321);

    std::filesystem::remove_all(directory);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
