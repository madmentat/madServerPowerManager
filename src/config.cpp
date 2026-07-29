#include "madspm/config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>

namespace madspm {
namespace {

std::string trim(std::string value) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool boolean(const std::map<std::string, std::string>& values,
             const std::string& key,
             bool fallback) {
    const auto it = values.find(key);
    if (it == values.end()) return fallback;
    if (it->second == "true" || it->second == "yes" || it->second == "1") return true;
    if (it->second == "false" || it->second == "no" || it->second == "0") return false;
    throw std::runtime_error("Некорректное логическое значение: " + key);
}

std::string text(const std::map<std::string, std::string>& values,
                 const std::string& key,
                 const std::string& fallback) {
    const auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
}

unsigned number(const std::map<std::string, std::string>& values,
                const std::string& key,
                unsigned fallback) {
    const auto it = values.find(key);
    if (it == values.end()) return fallback;
    std::size_t consumed = 0;
    const auto result = std::stoul(it->second, &consumed);
    if (consumed != it->second.size()) throw std::runtime_error("Некорректное число: " + key);
    return static_cast<unsigned>(result);
}

double decimal(const std::map<std::string, std::string>& values,
               const std::string& key,
               double fallback) {
    const auto it = values.find(key);
    if (it == values.end()) return fallback;
    std::size_t consumed = 0;
    const double result = std::stod(it->second, &consumed);
    if (consumed != it->second.size()) throw std::runtime_error("Некорректное число: " + key);
    return result;
}

void verify_secret_permissions(const std::filesystem::path& path) {
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0) {
        throw std::runtime_error("Не найден секретный файл: " + path.string());
    }
    if ((info.st_mode & 077) != 0) {
        throw std::runtime_error("Секретный файл должен иметь права 0600 или строже: " +
                                 path.string());
    }
}

} // namespace

std::map<std::string, std::string> ConfigLoader::parse_ini(
    const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Не удалось открыть конфиг: " + path.string());
    std::map<std::string, std::string> values;
    std::string section;
    std::string line;
    unsigned line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            if (section.empty()) throw std::runtime_error("Пустая секция INI");
            continue;
        }
        const auto equal = line.find('=');
        if (equal == std::string::npos || section.empty()) {
            throw std::runtime_error("Ошибка INI, строка " + std::to_string(line_number));
        }
        const std::string key = section + "." + trim(line.substr(0, equal));
        if (!values.emplace(key, trim(line.substr(equal + 1))).second) {
            throw std::runtime_error("Повторяющийся параметр: " + key);
        }
    }
    return values;
}

Config ConfigLoader::load(const std::filesystem::path& path, bool require_secrets) {
    const auto values = parse_ini(path);
    Config config;
    config.source_path = path;
    config.general.armed = boolean(values, "general.armed", false);
    config.general.poll_interval_seconds =
        number(values, "general.poll_interval_seconds", 5);
    config.general.log_level = text(values, "general.log_level", "info");
    config.general.state_file =
        text(values, "general.state_file", config.general.state_file.string());
    config.general.secrets_file =
        text(values, "general.secrets_file", config.general.secrets_file.string());

    config.ups.host = text(values, "ups.nut_host", config.ups.host);
    config.ups.port = static_cast<std::uint16_t>(number(values, "ups.nut_port", config.ups.port));
    config.ups.name = text(values, "ups.nut_name", config.ups.name);
    config.ups.on_battery_confirm_seconds =
        number(values, "ups.on_battery_confirm_seconds", 10);
    config.ups.grace_seconds = number(values, "ups.grace_seconds", 480);
    config.ups.mains_stable_seconds = number(values, "ups.mains_stable_seconds", 60);
    config.ups.critical_battery_charge =
        decimal(values, "ups.critical_battery_charge", 15.0);
    config.ups.critical_runtime_seconds =
        decimal(values, "ups.critical_runtime_seconds", 600.0);
    config.ups.use_low_battery_flag =
        boolean(values, "ups.use_low_battery_flag", true);

    config.proxmox.host = text(values, "proxmox.host", config.proxmox.host);
    config.proxmox.port =
        static_cast<std::uint16_t>(number(values, "proxmox.port", config.proxmox.port));
    config.proxmox.user = text(values, "proxmox.user", config.proxmox.user);
    config.proxmox.private_key =
        text(values, "proxmox.private_key", config.proxmox.private_key.string());
    config.proxmox.known_hosts =
        text(values, "proxmox.known_hosts", config.proxmox.known_hosts.string());
    config.proxmox.connect_timeout_seconds =
        number(values, "proxmox.connect_timeout_seconds", 5);
    config.proxmox.shutdown_grace_seconds =
        number(values, "proxmox.shutdown_grace_seconds", 480);
    config.proxmox.shutdown_timeout_seconds =
        number(values, "proxmox.shutdown_timeout_seconds", 360);
    config.proxmox.shutdown_retry_seconds =
        number(values, "proxmox.shutdown_retry_seconds", 20);
    config.proxmox.shutdown_max_attempts =
        number(values, "proxmox.shutdown_max_attempts", 5);
    config.proxmox.force_cut_after_shutdown_timeout =
        boolean(values, "proxmox.force_cut_after_shutdown_timeout", false);
    config.proxmox.forced_command =
        text(values, "proxmox.forced_command", config.proxmox.forced_command);

    config.plug.host = text(values, "plug.host", config.plug.host);
    config.plug.port =
        static_cast<std::uint16_t>(number(values, "plug.port", config.plug.port));
    config.plug.protocol_version =
        text(values, "plug.protocol_version", config.plug.protocol_version);
    config.plug.switch_dp = static_cast<int>(number(values, "plug.switch_dp", 1));
    config.plug.command_timeout_seconds =
        number(values, "plug.command_timeout_seconds", 5);
    config.plug.retry_seconds = number(values, "plug.retry_seconds", 10);
    config.plug.minimum_off_seconds = number(values, "plug.minimum_off_seconds", 20);

    config.api.enabled = boolean(values, "api.enabled", true);
    config.api.listen_address = text(values, "api.listen_address", "127.0.0.1");
    config.api.port = static_cast<std::uint16_t>(number(values, "api.port", 9187));
    config.api.allow_control = boolean(values, "api.allow_control", false);

    const std::set<std::string> known = {
        "general.armed", "general.poll_interval_seconds", "general.log_level",
        "general.state_file", "general.secrets_file", "ups.nut_host", "ups.nut_port",
        "ups.nut_name", "ups.on_battery_confirm_seconds", "ups.grace_seconds",
        "ups.mains_stable_seconds", "ups.critical_battery_charge",
        "ups.critical_runtime_seconds", "ups.use_low_battery_flag", "proxmox.host",
        "proxmox.port", "proxmox.user", "proxmox.private_key",
        "proxmox.known_hosts", "proxmox.connect_timeout_seconds",
        "proxmox.shutdown_grace_seconds", "proxmox.shutdown_timeout_seconds",
        "proxmox.shutdown_retry_seconds", "proxmox.shutdown_max_attempts",
        "proxmox.force_cut_after_shutdown_timeout", "proxmox.forced_command",
        "plug.host", "plug.port", "plug.protocol_version", "plug.switch_dp",
        "plug.command_timeout_seconds", "plug.retry_seconds", "plug.minimum_off_seconds",
        "api.enabled", "api.listen_address", "api.port", "api.allow_control"};
    for (const auto& [key, unused] : values) {
        if (!known.contains(key)) config.warnings.push_back("Неизвестный параметр: " + key);
    }

    if (require_secrets) {
        verify_secret_permissions(config.general.secrets_file);
        const auto secret_values = parse_ini(config.general.secrets_file);
        config.secrets.tuya_device_id =
            text(secret_values, "tuya.device_id", "");
        config.secrets.tuya_local_key =
            text(secret_values, "tuya.local_key", "");
        config.secrets.api_token =
            text(secret_values, "api.token", "");
    }
    validate(config, config.general.armed);
    return config;
}

void ConfigLoader::validate(const Config& config, bool for_armed_mode) {
    if (config.general.poll_interval_seconds == 0)
        throw std::runtime_error("poll_interval_seconds должен быть больше нуля");
    if (config.ups.grace_seconds < config.ups.on_battery_confirm_seconds)
        throw std::runtime_error("grace_seconds меньше времени подтверждения батареи");
    if (config.proxmox.shutdown_timeout_seconds < 360)
        throw std::runtime_error("shutdown_timeout_seconds должен быть не менее 360");
    if (config.plug.minimum_off_seconds < 20)
        throw std::runtime_error("minimum_off_seconds должен быть не менее 20");
    if (config.api.allow_control && !config.api.enabled)
        throw std::runtime_error("allow_control требует включённый API");
    if (config.plug.protocol_version != "3.5")
        throw std::runtime_error("Поддерживается только локальный протокол Tuya 3.5");
    if (!config.secrets.tuya_local_key.empty() &&
        config.secrets.tuya_local_key.size() != 16)
        throw std::runtime_error("Tuya local_key должен содержать ровно 16 байт");
    if (for_armed_mode) {
        if (config.secrets.tuya_device_id.empty() || config.secrets.tuya_local_key.empty())
            throw std::runtime_error("armed=true запрещён без секретов Tuya");
        if (!std::filesystem::exists(config.proxmox.private_key) ||
            !std::filesystem::exists(config.proxmox.known_hosts))
            throw std::runtime_error("armed=true запрещён без SSH-ключа и known_hosts");
    }
}

} // namespace madspm
