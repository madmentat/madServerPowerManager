#include "madspm/state_store.hpp"

#include <cctype>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace madspm {
namespace {

unsigned hex_digit(char value) {
    if (value >= '0' && value <= '9') return static_cast<unsigned>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<unsigned>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<unsigned>(value - 'A' + 10);
    throw std::runtime_error("state-файл содержит некорректную Unicode escape-последовательность");
}

std::uint32_t unicode_escape(const std::string& json, std::size_t offset) {
    if (offset + 4 > json.size())
        throw std::runtime_error("state-файл содержит обрезанную Unicode escape-последовательность");
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value = (value << 4U) | hex_digit(json[offset + i]);
    return value;
}

void append_utf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7fU) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0x10ffffU) {
        output.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else {
        throw std::runtime_error("state-файл содержит недопустимый Unicode codepoint");
    }
}

std::string json_string(const std::string& json, std::size_t& position) {
    if (position >= json.size() || json[position] != '"')
        throw std::runtime_error("Ожидалась JSON-строка");
    ++position;
    std::string result;
    while (position < json.size()) {
        const unsigned char current =
            static_cast<unsigned char>(json[position++]);
        if (current == '"') return result;
        if (current < 0x20U)
            throw std::runtime_error("Неэкранированный управляющий символ в state-файле");
        if (current != '\\') {
            result.push_back(static_cast<char>(current));
            continue;
        }
        if (position >= json.size())
            throw std::runtime_error("Обрезанная escape-последовательность в state-файле");
        const char escaped = json[position++];
        switch (escaped) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case '/': result.push_back('/'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        case 'u': {
            std::uint32_t codepoint = unicode_escape(json, position);
            position += 4;
            if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
                if (position + 6 > json.size() || json[position] != '\\' ||
                    json[position + 1] != 'u')
                    throw std::runtime_error("Обрезанная surrogate pair в state-файле");
                const std::uint32_t low = unicode_escape(json, position + 2);
                if (low < 0xdc00U || low > 0xdfffU)
                    throw std::runtime_error("Некорректная surrogate pair в state-файле");
                position += 6;
                codepoint = 0x10000U + ((codepoint - 0xd800U) << 10U) +
                            (low - 0xdc00U);
            } else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
                throw std::runtime_error("Одиночный low surrogate в state-файле");
            }
            append_utf8(result, codepoint);
            break;
        }
        default:
            throw std::runtime_error("Неизвестная escape-последовательность в state-файле");
        }
    }
    throw std::runtime_error("Незакрытая JSON-строка в state-файле");
}

std::string extract(const std::string& json, const std::string& key) {
    std::size_t position = 0;
    while (position < json.size() &&
           std::isspace(static_cast<unsigned char>(json[position])))
        ++position;
    if (position >= json.size() || json[position++] != '{')
        throw std::runtime_error("state-файл не содержит JSON-объект");
    for (;;) {
        while (position < json.size() &&
               (std::isspace(static_cast<unsigned char>(json[position])) ||
                json[position] == ','))
            ++position;
        if (position >= json.size() || json[position] == '}') return {};
        const std::string current_key = json_string(json, position);
        while (position < json.size() &&
               std::isspace(static_cast<unsigned char>(json[position])))
            ++position;
        if (position >= json.size() || json[position++] != ':')
            throw std::runtime_error("state-файл содержит поле без двоеточия");
        while (position < json.size() &&
               std::isspace(static_cast<unsigned char>(json[position])))
            ++position;
        std::string value;
        if (position < json.size() && json[position] == '"') {
            value = json_string(json, position);
        } else {
            const std::size_t begin = position;
            while (position < json.size() && json[position] != ',' &&
                   json[position] != '}')
                ++position;
            std::size_t end = position;
            while (end > begin &&
                   std::isspace(static_cast<unsigned char>(json[end - 1])))
                --end;
            value = json.substr(begin, end - begin);
        }
        if (current_key == key) return value;
    }
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
        << ",\"on_battery_detected_since_utc\":"
        << state.on_battery_detected_since_utc
        << ",\"mains_restored_since_utc\":" << state.mains_restored_since_utc
        << ",\"plug_off_since_utc\":" << state.plug_off_since_utc
        << ",\"shutdown_sent\":" << (state.shutdown_sent ? "true" : "false")
        << ",\"shutdown_attempts\":" << state.shutdown_attempts
        << ",\"shutdown_sent_utc\":" << state.shutdown_sent_utc
        << ",\"shutdown_last_attempt_utc\":" << state.shutdown_last_attempt_utc
        << ",\"server_unreachable_since_utc\":"
        << state.server_unreachable_since_utc
        << ",\"server_confirmed_off\":" << (state.server_confirmed_off ? "true" : "false")
        << ",\"plug_last_attempt_utc\":" << state.plug_last_attempt_utc
        << ",\"plug_attempts\":" << state.plug_attempts
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
    const std::string expected_suffix =
        ",\"checksum\":\"" + expected + "\"}";
    if (json.compare(payload_end, expected_suffix.size(), expected_suffix) != 0)
        throw std::runtime_error("state-файл повреждён: некорректное окончание");
    for (std::size_t i = payload_end + expected_suffix.size(); i < json.size(); ++i) {
        if (!std::isspace(static_cast<unsigned char>(json[i])))
            throw std::runtime_error("state-файл содержит данные после JSON-объекта");
    }
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
    result.on_battery_detected_since_utc =
        integer(extract(json, "on_battery_detected_since_utc"));
    result.mains_restored_since_utc = integer(extract(json, "mains_restored_since_utc"));
    result.plug_off_since_utc = integer(extract(json, "plug_off_since_utc"));
    result.shutdown_sent = truth(extract(json, "shutdown_sent"));
    result.shutdown_attempts = static_cast<unsigned>(integer(extract(json, "shutdown_attempts")));
    result.shutdown_sent_utc = integer(extract(json, "shutdown_sent_utc"));
    result.shutdown_last_attempt_utc = integer(extract(json, "shutdown_last_attempt_utc"));
    std::string server_unreachable =
        extract(json, "server_unreachable_since_utc");
    if (server_unreachable.empty())
        server_unreachable = extract(json, "proxmox_unreachable_since_utc");
    result.server_unreachable_since_utc = integer(server_unreachable);
    result.server_confirmed_off = truth(extract(json, "server_confirmed_off"));
    result.plug_last_attempt_utc = integer(extract(json, "plug_last_attempt_utc"));
    result.plug_attempts =
        static_cast<unsigned>(integer(extract(json, "plug_attempts")));
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
