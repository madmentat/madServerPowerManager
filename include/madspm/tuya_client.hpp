#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace madspm::tuya {

struct ClientConfig {
    std::string device_id;
    std::string device_ip;
    std::string local_key;
    std::uint16_t port = 6668;
    int switch_dp = 1;
    int timeout_ms = 5000;
};

std::optional<bool> status(const ClientConfig& config, bool debug = false);
bool set_power(const ClientConfig& config, bool desired, bool debug = false);
bool self_test(std::string& error);

} // namespace madspm::tuya
