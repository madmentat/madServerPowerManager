#include "madspm/state_store.hpp"

#include <cctype>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace madspm {
namespace {

std::string extract(const std::string& json, const std::string& key) {
    const std::string marker = "\"" + key + "\":";
    auto pos = json.find(marker);
    if (pos == std::string::npos) return {};
    pos += marker.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos < json.size() && json[pos] == '"') {
        const auto end = json.find('"', pos + 1);
        return end == std::string::npos ? std::string{} : json.substr(pos + 1, end - pos - 1);
    }
    const auto end = json.find_first_of(",}\n", pos);
    return json.substr(pos, end - pos);
}

bool truth(const std::string& value) { return value == "true"; }

std::int64_t integer(const std::string& value) {
    return value.empty() ? 0 : std::stoll(value);
}

std::uint64_t checksum(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string payload_json(const PersistentState& state) {
    std::ostringstream out;
    out << "\"format_version\":" << state.format_version
        << ",\"state\":\"" << to_string(state.state) << '"'
        << ",\"emergency_cycle_id\":\"" << json_escape(state.emergency_cycle_id) << '"'
        << ",\"on_battery_since_utc\":" << state.on_battery_since_utc
        << ",\"mains_restored_since_utc\":" << state.mains_restored_since_utc
        << ",\"plug_off_since_utc\":" << state.plug_off_since_utc
        << ",\"shutdown_sent\":" << (state.shutdown_sent ? "true" : "false")
        << ",\"shutdown_attempts\":" << state.shutdown_attempts
        << ",\"server_confirmed_off\":" << (state.server_confirmed_off ? "true" : "false")
        << ",\"plug_was_cut\":" << (state.plug_was_cut ? "true" : "false")
        << ",\"last_error\":\"" << json_escape(state.last_error) << '"'
        << ",\"last_success_utc\":" << state.last_success_utc;
    return out.str();
}

} // namespace

StateStore::StateStore(std::filesystem::path path) : path_(std::move(path)) {}

PersistentState StateStore::load() const {
    if (!std::filesystem::exists(path_)) return {};
    std::ifstream input(path_);
    std::ostringstream text;
    text << input.rdbuf();
    const std::string json = text.str();
    const auto expected = extract(json, "checksum");
    const auto payload_end = json.rfind(",\"checksum\"");
    if (payload_end == std::string::npos || expected.empty())
        throw std::runtime_error("state-файл повреждён: отсутствует checksum");
    const std::string payload = json.substr(1, payload_end - 1);
    if (std::to_string(checksum(payload)) != expected)
        throw std::runtime_error("state-файл повреждён: checksum не совпадает");
    PersistentState result;
    result.format_version = static_cast<std::uint32_t>(integer(extract(json, "format_version")));
    const auto state = state_from_string(extract(json, "state"));
    if (!state) throw std::runtime_error("state-файл содержит неизвестное состояние");
    result.state = *state;
    result.emergency_cycle_id = extract(json, "emergency_cycle_id");
    result.on_battery_since_utc = integer(extract(json, "on_battery_since_utc"));
    result.mains_restored_since_utc = integer(extract(json, "mains_restored_since_utc"));
    result.plug_off_since_utc = integer(extract(json, "plug_off_since_utc"));
    result.shutdown_sent = truth(extract(json, "shutdown_sent"));
    result.shutdown_attempts = static_cast<unsigned>(integer(extract(json, "shutdown_attempts")));
    result.server_confirmed_off = truth(extract(json, "server_confirmed_off"));
    result.plug_was_cut = truth(extract(json, "plug_was_cut"));
    result.last_error = extract(json, "last_error");
    result.last_success_utc = integer(extract(json, "last_success_utc"));
    return result;
}

std::string state_to_json(const PersistentState& state) {
    const std::string payload = payload_json(state);
    return "{" + payload + ",\"checksum\":\"" + std::to_string(checksum(payload)) + "\"}\n";
}

void StateStore::save(const PersistentState& state) const {
    std::filesystem::create_directories(path_.parent_path());
    const auto temporary = path_.string() + ".tmp." + std::to_string(::getpid());
    const std::string content = state_to_json(state);
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) throw std::runtime_error("Не удалось создать временный state-файл");
    std::size_t offset = 0;
    while (offset < content.size()) {
        const ssize_t written = ::write(fd, content.data() + offset, content.size() - offset);
        if (written <= 0) {
            ::close(fd);
            throw std::runtime_error("Ошибка записи state-файла");
        }
        offset += static_cast<std::size_t>(written);
    }
    if (::fsync(fd) != 0 || ::close(fd) != 0) {
        throw std::runtime_error("Ошибка синхронизации state-файла");
    }
    std::filesystem::rename(temporary, path_);
    const int directory = ::open(path_.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
    if (directory >= 0) {
        ::fsync(directory);
        ::close(directory);
    }
}

void StateStore::reset() const {
    if (!std::filesystem::exists(path_)) return;
    const auto backup = path_.string() + ".reset." + std::to_string(utc_now_seconds());
    std::filesystem::rename(path_, backup);
}

const std::filesystem::path& StateStore::path() const { return path_; }

} // namespace madspm
