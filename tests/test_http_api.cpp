#include "madspm/http_api.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
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
    madspm::Config config;
    config.general.state_file =
        std::filesystem::path("/tmp") /
        ("madspm-api-test-" + std::to_string(::getpid()) + ".json");
    madspm::PowerManager manager(config);

    const auto status =
        madspm::route_http_request(manager, "GET", "/api/v1/status");
    expect("status endpoint", status.status == "200 OK");
    expect("status is JSON", status.body.find("\"state\"") != std::string::npos);

    const auto missing =
        madspm::route_http_request(manager, "GET", "/api/v1/missing");
    expect("unknown endpoint", missing.status == "404 Not Found");
    expect("not-found JSON", missing.body == "{\"error\":\"not found\"}");

    const auto server =
        madspm::route_http_request(manager, "GET", "/api/v1/server");
    const auto legacy_server =
        madspm::route_http_request(manager, "GET", "/api/v1/proxmox");
    expect("server endpoint", server.status == "200 OK");
    expect("legacy server endpoint remains compatible",
           legacy_server.body == server.body);

    const auto control =
        madspm::route_http_request(manager, "POST", "/api/v1/status");
    expect("non-GET rejected", control.status == "405 Method Not Allowed");

    std::filesystem::remove(config.general.state_file);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
