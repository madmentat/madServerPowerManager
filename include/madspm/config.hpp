#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace madspm {

struct GeneralConfig {
    bool armed = false;
    unsigned poll_interval_seconds = 5;
    std::string log_level = "info";
    std::filesystem::path state_file = "/var/lib/mad-server-power-manager/state.json";
    std::filesystem::path secrets_file = "/etc/mad-server-power-manager/secrets.ini";
};

struct UpsConfig {
    std::string host = "127.0.0.1";
    std::uint16_t port = 3493;
    std::string name = "ups";
    unsigned on_battery_confirm_seconds = 10;
    unsigned grace_seconds = 480;
    unsigned mains_stable_seconds = 60;
    double critical_battery_charge = 15.0;
    double critical_runtime_seconds = 600.0;
    bool use_low_battery_flag = true;
};

struct ServerConfig {
    std::string host = "YOUR_SERVER_HOST";
    std::uint16_t port = 22;
    std::string user = "mad-power-manager";
    std::filesystem::path private_key = "/etc/mad-server-power-manager/id_ed25519";
    std::filesystem::path known_hosts = "/etc/mad-server-power-manager/known_hosts";
    unsigned connect_timeout_seconds = 5;
    unsigned command_timeout_seconds = 15;
    unsigned shutdown_grace_seconds = 480;
    unsigned shutdown_timeout_seconds = 300;
    unsigned server_off_confirm_seconds = 30;
    unsigned shutdown_retry_seconds = 20;
    unsigned shutdown_max_attempts = 5;
    bool force_cut_after_shutdown_timeout = false;
    std::string forced_command = "poweroff";
};

struct PlugConfig {
    std::string host = "YOUR_TUYA_PLUG_HOST";
    std::uint16_t port = 6668;
    std::string protocol_version = "3.5";
    int switch_dp = 1;
    unsigned command_timeout_seconds = 5;
    unsigned retry_seconds = 10;
    unsigned minimum_off_seconds = 20;
};

struct ApiConfig {
    bool enabled = true;
    std::string listen_address = "127.0.0.1";
    std::uint16_t port = 9187;
    bool allow_control = false;
};

struct Secrets {
    std::string tuya_device_id;
    std::string tuya_local_key;
    std::string api_token;
};

struct Config {
    GeneralConfig general;
    UpsConfig ups;
    ServerConfig server;
    PlugConfig plug;
    ApiConfig api;
    Secrets secrets;
    std::filesystem::path source_path;
    std::vector<std::string> warnings;
};

class ConfigLoader {
public:
    static Config load(const std::filesystem::path& path, bool require_secrets = true);
    static void validate(const Config& config, bool for_armed_mode);
    static std::map<std::string, std::string> parse_ini(const std::filesystem::path& path);
};

} // namespace madspm
