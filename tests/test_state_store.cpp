#include "madspm/state_store.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

namespace {
int failures = 0;
void expect(const char* name, bool condition) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}

std::uint64_t checksum(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}
} // namespace

int main() {
    const auto directory = std::filesystem::path("/tmp") /
        ("madspm-state-test-" + std::to_string(::getpid()));
    std::filesystem::create_directories(directory);
    const auto path = directory / "state.json";
    const madspm::StateStore store(path);

    madspm::PersistentState source;
    source.state = madspm::State::Degraded;
    source.emergency_cycle_id = "cycle-\"quoted\"";
    source.on_battery_detected_since_utc = 101;
    source.plug_last_attempt_utc = 202;
    source.server_unreachable_since_utc = 303;
    source.plug_attempts = 3;
    source.last_error =
        std::string("quote=\" checksum-marker=,\"checksum\":\"fake\""
                    " slash=\\ newline=\n tab=\t control=") +
        static_cast<char>(1) + " utf8=ошибка";
    store.save(source);

    const auto loaded = store.load();
    expect("round-trips escaped last_error", loaded.last_error == source.last_error);
    expect("round-trips escaped cycle id",
           loaded.emergency_cycle_id == source.emergency_cycle_id);
    expect("round-trips new timers",
               loaded.on_battery_detected_since_utc == 101 &&
               loaded.plug_last_attempt_utc == 202 &&
               loaded.plug_attempts == 3 &&
               loaded.server_unreachable_since_utc == 303);

    std::ifstream input(path);
    std::ostringstream serialized;
    serialized << input.rdbuf();
    expect("serializes quote escape",
           serialized.str().find("\\\"") != std::string::npos);
    expect("serializes backslash escape",
           serialized.str().find("\\\\") != std::string::npos);
    expect("serializes newline escape",
           serialized.str().find("\\n") != std::string::npos);
    expect("serializes control character as unicode",
           serialized.str().find("\\u0001") != std::string::npos);

    std::string legacy = serialized.str();
    const std::string new_key = "server_unreachable_since_utc";
    const auto new_key_position = legacy.find(new_key);
    if (new_key_position != std::string::npos)
        legacy.replace(new_key_position, new_key.size(),
                       "proxmox_unreachable_since_utc");
    const auto checksum_position = legacy.rfind(",\"checksum\"");
    const std::string payload = legacy.substr(1, checksum_position - 1);
    legacy.replace(
        checksum_position, std::string::npos,
        ",\"checksum\":\"" + std::to_string(checksum(payload)) + "\"}\n");
    {
        std::ofstream legacy_output(path, std::ios::trunc);
        legacy_output << legacy;
    }
    expect("loads legacy Proxmox state field",
           store.load().server_unreachable_since_utc == 303);

    std::string damaged = legacy;
    const auto state_name = damaged.find("DEGRADED");
    if (state_name != std::string::npos)
        damaged.replace(state_name, std::string("DEGRADED").size(), "ERROR   ");
    {
        std::ofstream corrupt(path, std::ios::trunc);
        corrupt << damaged;
    }
    bool corruption_detected = false;
    try {
        store.load();
    } catch (const std::exception&) {
        corruption_detected = true;
    }
    expect("rejects corrupt state file", corruption_detected);

    std::filesystem::remove_all(directory);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
