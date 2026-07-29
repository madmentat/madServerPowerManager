#include "crypto.hpp"
#include "madspm/tuya_client.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kPrefix6699 = 0x00006699U;
constexpr std::uint32_t kSuffix6699 = 0x00009966U;
constexpr std::uint32_t kCmdSessionStart = 0x03U;
constexpr std::uint32_t kCmdSessionResponse = 0x04U;
constexpr std::uint32_t kCmdSessionFinish = 0x05U;
constexpr std::uint32_t kCmdControlNew = 0x0dU;
constexpr std::uint32_t kCmdDpQueryNew = 0x10U;
constexpr std::size_t kHeaderSize = 18;
constexpr std::size_t kIvSize = 12;
constexpr std::size_t kTagSize = 16;
constexpr std::size_t kFooterSize = 4;
constexpr std::size_t kMaxFrameSize = 1024 * 1024;

struct Config {
    std::string device_id;
    std::string device_ip;
    std::string local_key;
    int port = 6668;
    int switch_dp = 1;
    int timeout_ms = 5000;
};

enum class Action { On, Off, Status, SelfTest, Help };

struct Options {
    Action action = Action::Help;
    std::optional<fs::path> config_path;
    bool debug = false;
};

class Socket {
public:
    Socket() = default;
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket() { close(); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    int get() const { return fd_; }
    explicit operator bool() const { return fd_ >= 0; }
    void close() {
        if (fd_ >= 0) {
            ::shutdown(fd_, SHUT_RDWR);
            ::close(fd_);
            fd_ = -1;
        }
    }
private:
    int fd_ = -1;
};

struct Frame {
    std::uint32_t sequence = 0;
    std::uint32_t command = 0;
    std::uint32_t retcode = 0;
    std::vector<std::uint8_t> payload;
};

std::uint32_t read_be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24U) |
           (static_cast<std::uint32_t>(p[1]) << 16U) |
           (static_cast<std::uint32_t>(p[2]) << 8U) |
            static_cast<std::uint32_t>(p[3]);
}

void append_be16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 8U));
    out.push_back(static_cast<std::uint8_t>(v));
}

void append_be32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24U));
    out.push_back(static_cast<std::uint8_t>(v >> 16U));
    out.push_back(static_cast<std::uint8_t>(v >> 8U));
    out.push_back(static_cast<std::uint8_t>(v));
}

std::string errno_message(const std::string& prefix) {
    return prefix + ": " + std::strerror(errno);
}

void fill_random(std::uint8_t* data, std::size_t size) {
    std::size_t done = 0;
    while (done < size) {
        const ssize_t n = ::getrandom(data + done, size - done, 0);
        if (n > 0) {
            done += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    if (done == size) return;

    const int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error(errno_message("Не удалось открыть /dev/urandom"));
    while (done < size) {
        const ssize_t n = ::read(fd, data + done, size - done);
        if (n > 0) {
            done += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        const int saved = errno;
        ::close(fd);
        errno = saved;
        throw std::runtime_error(errno_message("Не удалось получить случайные байты"));
    }
    ::close(fd);
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Не удалось открыть конфиг: " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!in.good() && !in.eof()) throw std::runtime_error("Ошибка чтения конфига: " + path.string());
    return buffer.str();
}

void skip_ws(const std::string& text, std::size_t& pos) {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n')) ++pos;
}

std::optional<std::size_t> find_json_value(const std::string& text, const std::string& name) {
    const std::string needle = "\"" + name + "\"";
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        pos += needle.size();
        skip_ws(text, pos);
        if (pos < text.size() && text[pos] == ':') {
            ++pos;
            skip_ws(text, pos);
            return pos;
        }
    }
    return std::nullopt;
}

std::string parse_json_string_at(const std::string& text, std::size_t pos) {
    if (pos >= text.size() || text[pos] != '"') throw std::runtime_error("В конфиге ожидалась строка JSON");
    ++pos;
    std::string out;
    while (pos < text.size()) {
        const char c = text[pos++];
        if (c == '"') return out;
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (pos >= text.size()) break;
        const char esc = text[pos++];
        switch (esc) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default: throw std::runtime_error("Неподдерживаемая escape-последовательность в конфиге");
        }
    }
    throw std::runtime_error("Незакрытая строка JSON в конфиге");
}

std::string get_json_string(const std::string& text, const std::string& name, bool required) {
    const auto pos = find_json_value(text, name);
    if (!pos) {
        if (required) throw std::runtime_error("В конфиге отсутствует поле \"" + name + "\"");
        return {};
    }
    return parse_json_string_at(text, *pos);
}

int get_json_int(const std::string& text, const std::string& name, int default_value) {
    const auto value_pos = find_json_value(text, name);
    if (!value_pos) return default_value;
    std::size_t pos = *value_pos;
    bool negative = false;
    if (pos < text.size() && text[pos] == '-') {
        negative = true;
        ++pos;
    }
    if (pos >= text.size() || text[pos] < '0' || text[pos] > '9') {
        throw std::runtime_error("Поле \"" + name + "\" должно быть целым числом");
    }
    long long value = 0;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        value = value * 10 + (text[pos] - '0');
        if (value > 1000000000LL) throw std::runtime_error("Слишком большое значение поля \"" + name + "\"");
        ++pos;
    }
    return static_cast<int>(negative ? -value : value);
}

fs::path executable_directory() {
    std::vector<char> buffer(4096, '\0');
    const ssize_t n = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (n > 0) return fs::path(std::string(buffer.data(), static_cast<std::size_t>(n))).parent_path();
    return fs::current_path();
}

std::vector<fs::path> config_candidates(const Options& options) {
    std::vector<fs::path> paths;
    if (options.config_path) {
        paths.push_back(*options.config_path);
        return paths;
    }
    if (const char* env = std::getenv("PLUGCTL_CONFIG")) paths.emplace_back(env);
    paths.push_back(executable_directory() / "plug_config.json");
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        paths.emplace_back(fs::path(xdg) / "plugctl" / "config.json");
    }
    if (const char* home = std::getenv("HOME")) {
        paths.emplace_back(fs::path(home) / ".config" / "plugctl" / "config.json");
    }
    paths.emplace_back("/etc/plugctl.json");
    return paths;
}

std::pair<Config, fs::path> load_config(const Options& options) {
    const auto candidates = config_candidates(options);
    fs::path selected;
    for (const auto& path : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(path, ec)) {
            selected = path;
            break;
        }
    }
    if (selected.empty()) {
        std::ostringstream message;
        message << "Не найден конфигурационный файл. Проверены пути:";
        for (const auto& path : candidates) message << "\n  " << path.string();
        message << "\nМожно указать его явно: plugctl --config /путь/config.json --status";
        throw std::runtime_error(message.str());
    }

    const std::string text = read_file(selected);
    Config config;
    config.device_id = get_json_string(text, "device_id", false);
    config.device_ip = get_json_string(text, "device_ip", true);
    config.local_key = get_json_string(text, "local_key", true);
    config.port = get_json_int(text, "port", 6668);
    config.switch_dp = get_json_int(text, "switch_dp", 1);
    config.timeout_ms = get_json_int(text, "timeout_ms", 5000);

    if (config.local_key.size() != 16) {
        throw std::runtime_error("local_key должен содержать ровно 16 байт; сейчас: " + std::to_string(config.local_key.size()));
    }
    if (config.device_ip.empty()) throw std::runtime_error("device_ip не может быть пустым");
    if (config.port < 1 || config.port > 65535) throw std::runtime_error("port должен быть от 1 до 65535");
    if (config.switch_dp < 1 || config.switch_dp > 9999) throw std::runtime_error("switch_dp выглядит некорректно");
    if (config.timeout_ms < 100 || config.timeout_ms > 120000) throw std::runtime_error("timeout_ms должен быть от 100 до 120000");
    return {config, selected};
}

void print_usage(const char* name) {
    std::cout
        << "Нативная утилита управления розеткой Tuya 3.5 (Python не нужен).\n\n"
        << "Использование:\n"
        << "  " << name << " --on [--config ПУТЬ] [--debug]\n"
        << "  " << name << " --off [--config ПУТЬ] [--debug]\n"
        << "  " << name << " --status [--config ПУТЬ] [--debug]\n"
        << "  " << name << " --self-test\n\n"
        << "Конфиг ищется рядом с программой, затем в ~/.config/plugctl/config.json\n"
        << "и /etc/plugctl.json. Путь можно задать переменной PLUGCTL_CONFIG.\n";
}

Options parse_options(int argc, char** argv) {
    Options options;
    bool action_seen = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto set_action = [&](Action action) {
            if (action_seen) throw std::runtime_error("Нужно указать только одно действие: --on, --off или --status");
            options.action = action;
            action_seen = true;
        };
        if (arg == "--on" || arg == "on") set_action(Action::On);
        else if (arg == "--off" || arg == "off") set_action(Action::Off);
        else if (arg == "--status" || arg == "status") set_action(Action::Status);
        else if (arg == "--self-test") set_action(Action::SelfTest);
        else if (arg == "--help" || arg == "-h") set_action(Action::Help);
        else if (arg == "--debug") options.debug = true;
        else if (arg == "--config") {
            if (++i >= argc) throw std::runtime_error("После --config нужен путь к файлу");
            options.config_path = fs::path(argv[i]);
        } else if (arg.rfind("--config=", 0) == 0) {
            options.config_path = fs::path(arg.substr(9));
        } else {
            throw std::runtime_error("Неизвестный параметр: " + arg);
        }
    }
    if (!action_seen) options.action = Action::Help;
    return options;
}

Socket connect_device(const Config& config) {
    sockaddr_storage address{};
    socklen_t address_size = 0;
    int family = AF_UNSPEC;

    auto* ipv4 = reinterpret_cast<sockaddr_in*>(&address);
    if (::inet_pton(AF_INET, config.device_ip.c_str(), &ipv4->sin_addr) == 1) {
        family = AF_INET;
        ipv4->sin_family = AF_INET;
        ipv4->sin_port = htons(static_cast<std::uint16_t>(config.port));
        address_size = sizeof(sockaddr_in);
    } else {
        auto* ipv6 = reinterpret_cast<sockaddr_in6*>(&address);
        if (::inet_pton(AF_INET6, config.device_ip.c_str(), &ipv6->sin6_addr) == 1) {
            family = AF_INET6;
            ipv6->sin6_family = AF_INET6;
            ipv6->sin6_port = htons(static_cast<std::uint16_t>(config.port));
            address_size = sizeof(sockaddr_in6);
        }
    }
    if (family == AF_UNSPEC) {
        throw std::runtime_error("device_ip должен быть числовым IPv4/IPv6-адресом: " + config.device_ip);
    }

    Socket socket(::socket(family, SOCK_STREAM, IPPROTO_TCP));
    if (!socket) throw std::runtime_error(errno_message("Не удалось создать TCP-сокет"));

    const timeval tv{config.timeout_ms / 1000, (config.timeout_ms % 1000) * 1000};
    ::setsockopt(socket.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(socket.get(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    const int yes = 1;
    ::setsockopt(socket.get(), IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

    if (::connect(socket.get(), reinterpret_cast<const sockaddr*>(&address), address_size) != 0) {
        throw std::runtime_error("Не удалось подключиться к " + config.device_ip + ":" +
                                 std::to_string(config.port) + ": " + std::strerror(errno));
    }
    return socket;
}

void send_all(int fd, const std::vector<std::uint8_t>& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        throw std::runtime_error(errno_message("Ошибка отправки в розетку"));
    }
}

void recv_exact(int fd, std::uint8_t* data, std::size_t size) {
    std::size_t received = 0;
    while (received < size) {
        const ssize_t n = ::recv(fd, data + received, size - received, 0);
        if (n > 0) {
            received += static_cast<std::size_t>(n);
            continue;
        }
        if (n == 0) throw std::runtime_error("Розетка закрыла TCP-соединение");
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) throw std::runtime_error("Истёк тайм-аут ожидания ответа розетки");
        throw std::runtime_error(errno_message("Ошибка чтения ответа розетки"));
    }
}

std::array<std::uint8_t, kHeaderSize> receive_header(int fd) {
    constexpr std::array<std::uint8_t, 4> prefix = {0x00, 0x00, 0x66, 0x99};
    std::array<std::uint8_t, kHeaderSize> header{};
    std::size_t matched = 0;
    for (;;) {
        std::uint8_t byte = 0;
        recv_exact(fd, &byte, 1);
        if (byte == prefix[matched]) {
            header[matched++] = byte;
            if (matched == prefix.size()) break;
        } else {
            matched = (byte == prefix[0]) ? 1 : 0;
            if (matched == 1) header[0] = byte;
        }
    }
    recv_exact(fd, header.data() + 4, header.size() - 4);
    return header;
}

std::vector<std::uint8_t> pack_frame(std::uint32_t sequence,
                                     std::uint32_t command,
                                     const std::vector<std::uint8_t>& plaintext,
                                     const crypto::Block16& key,
                                     bool debug) {
    crypto::Iv12 iv{};
    fill_random(iv.data(), iv.size());
    const std::uint32_t length = static_cast<std::uint32_t>(kIvSize + plaintext.size() + kTagSize);

    std::vector<std::uint8_t> header;
    header.reserve(kHeaderSize);
    append_be32(header, kPrefix6699);
    append_be16(header, 0);
    append_be32(header, sequence);
    append_be32(header, command);
    append_be32(header, length);

    const crypto::GcmResult encrypted = crypto::aes128_gcm_encrypt(
        key, iv, header.data() + 4, header.size() - 4,
        plaintext.data(), plaintext.size());

    std::vector<std::uint8_t> frame;
    frame.reserve(header.size() + length + kFooterSize);
    frame.insert(frame.end(), header.begin(), header.end());
    frame.insert(frame.end(), iv.begin(), iv.end());
    frame.insert(frame.end(), encrypted.ciphertext.begin(), encrypted.ciphertext.end());
    frame.insert(frame.end(), encrypted.tag.begin(), encrypted.tag.end());
    append_be32(frame, kSuffix6699);

    if (debug) {
        std::cerr << "TX cmd=0x" << std::hex << command << std::dec
                  << " seq=" << sequence << " bytes=" << frame.size() << "\n";
    }
    return frame;
}

Frame receive_frame(int fd, const crypto::Block16& key, bool debug) {
    const auto header = receive_header(fd);
    const std::uint32_t sequence = read_be32(header.data() + 6);
    const std::uint32_t command = read_be32(header.data() + 10);
    const std::uint32_t length = read_be32(header.data() + 14);
    if (length < kIvSize + kTagSize || length > kMaxFrameSize) {
        throw std::runtime_error("Розетка прислала некорректную длину кадра: " + std::to_string(length));
    }

    std::vector<std::uint8_t> body(static_cast<std::size_t>(length) + kFooterSize);
    recv_exact(fd, body.data(), body.size());
    const std::uint32_t suffix = read_be32(body.data() + length);
    if (suffix != kSuffix6699) throw std::runtime_error("Некорректный конец кадра Tuya 3.5");

    crypto::Iv12 iv{};
    std::copy_n(body.begin(), kIvSize, iv.begin());
    const std::size_t cipher_size = static_cast<std::size_t>(length) - kIvSize - kTagSize;
    crypto::Tag16 tag{};
    std::copy_n(body.begin() + static_cast<std::ptrdiff_t>(kIvSize + cipher_size), kTagSize, tag.begin());

    std::vector<std::uint8_t> plaintext;
    if (!crypto::aes128_gcm_decrypt(key, iv,
                                    header.data() + 4, header.size() - 4,
                                    body.data() + kIvSize, cipher_size,
                                    tag, plaintext)) {
        throw std::runtime_error("AES-GCM: подпись ответа не совпала (неверный ключ или потерян сеанс)");
    }

    Frame frame;
    frame.sequence = sequence;
    frame.command = command;
    if (plaintext.size() >= 4) {
        frame.retcode = read_be32(plaintext.data());
        frame.payload.assign(plaintext.begin() + 4, plaintext.end());
    } else {
        frame.payload = std::move(plaintext);
    }
    if (debug) {
        std::cerr << "RX cmd=0x" << std::hex << command << std::dec
                  << " seq=" << sequence << " ret=" << frame.retcode
                  << " payload=" << frame.payload.size() << "\n";
        if (!frame.payload.empty()) {
            std::cerr << "RX payload hex: " << crypto::hex_encode(frame.payload.data(), frame.payload.size()) << "\n";
        }
    }
    return frame;
}

crypto::Block16 block_from_key(const std::string& key) {
    crypto::Block16 out{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(key.data()), out.size(), out.begin());
    return out;
}

crypto::Block16 negotiate_session_key(int fd,
                                      const crypto::Block16& real_key,
                                      std::uint32_t& sequence,
                                      bool debug) {
    crypto::Block16 client_nonce{};
    fill_random(client_nonce.data(), client_nonce.size());
    std::vector<std::uint8_t> start(client_nonce.begin(), client_nonce.end());
    send_all(fd, pack_frame(sequence++, kCmdSessionStart, start, real_key, debug));

    Frame response;
    bool found = false;
    for (int i = 0; i < 4; ++i) {
        response = receive_frame(fd, real_key, debug);
        if (response.command == kCmdSessionResponse) {
            found = true;
            break;
        }
    }
    if (!found) throw std::runtime_error("Розетка не прислала ответ согласования сеансового ключа");
    if (response.retcode != 0) throw std::runtime_error("Розетка отклонила согласование ключа, retcode=" + std::to_string(response.retcode));
    if (response.payload.size() < 48) throw std::runtime_error("Слишком короткий ответ согласования ключа");

    crypto::Block16 device_nonce{};
    std::copy_n(response.payload.begin(), device_nonce.size(), device_nonce.begin());
    const crypto::Digest32 expected = crypto::hmac_sha256(
        real_key.data(), real_key.size(), client_nonce.data(), client_nonce.size());
    if (!crypto::constant_time_equal(expected.data(), response.payload.data() + 16, expected.size())) {
        throw std::runtime_error("Розетка не прошла проверку HMAC: скорее всего, local_key неверен");
    }

    const crypto::Digest32 finish_hmac = crypto::hmac_sha256(
        real_key.data(), real_key.size(), device_nonce.data(), device_nonce.size());
    std::vector<std::uint8_t> finish(finish_hmac.begin(), finish_hmac.end());
    send_all(fd, pack_frame(sequence++, kCmdSessionFinish, finish, real_key, debug));

    crypto::Block16 xored{};
    for (std::size_t i = 0; i < xored.size(); ++i) xored[i] = client_nonce[i] ^ device_nonce[i];
    crypto::Iv12 derivation_iv{};
    std::copy_n(client_nonce.begin(), derivation_iv.size(), derivation_iv.begin());
    const crypto::GcmResult derived = crypto::aes128_gcm_encrypt(
        real_key, derivation_iv, nullptr, 0, xored.data(), xored.size());
    if (derived.ciphertext.size() != 16) throw std::runtime_error("Внутренняя ошибка вывода сеансового ключа");

    crypto::Block16 session_key{};
    std::copy_n(derived.ciphertext.begin(), session_key.size(), session_key.begin());
    if (session_key[0] == 0) {
        throw std::runtime_error("Tuya сформировала недопустимый сеансовый ключ; требуется повторное соединение");
    }
    if (debug) std::cerr << "Сеансовый ключ согласован.\n";
    return session_key;
}

std::vector<std::uint8_t> make_control_payload(int dps, bool state) {
    const std::time_t now = std::time(nullptr);
    std::ostringstream json;
    json << "{\"protocol\":5,\"t\":" << static_cast<long long>(now)
         << ",\"data\":{\"dps\":{\"" << dps << "\":"
         << (state ? "true" : "false") << "}}}";
    const std::string json_text = json.str();
    std::vector<std::uint8_t> payload;
    payload.reserve(15 + json_text.size());
    payload.push_back('3');
    payload.push_back('.');
    payload.push_back('5');
    payload.insert(payload.end(), 12, 0);
    payload.insert(payload.end(), json_text.begin(), json_text.end());
    return payload;
}

std::vector<std::uint8_t> make_status_payload() {
    return {'{', '}'};
}

std::string decode_json_payload(std::vector<std::uint8_t> payload) {
    if (payload.size() >= 15 && payload[0] == '3' && payload[1] == '.' && payload[2] == '5') {
        payload.erase(payload.begin(), payload.begin() + 15);
    }
    while (!payload.empty() && payload.front() == 0) payload.erase(payload.begin());
    return std::string(payload.begin(), payload.end());
}

std::optional<bool> parse_dps_boolean(const std::string& json, int dps) {
    std::size_t dps_name = json.find("\"dps\"");
    while (dps_name != std::string::npos) {
        std::size_t colon = json.find(':', dps_name + 5);
        if (colon == std::string::npos) return std::nullopt;
        std::size_t begin = json.find('{', colon + 1);
        if (begin == std::string::npos) return std::nullopt;
        std::size_t depth = 0;
        bool in_string = false;
        bool escaped = false;
        std::size_t end = std::string::npos;
        for (std::size_t i = begin; i < json.size(); ++i) {
            const char c = json[i];
            if (in_string) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') in_string = false;
                continue;
            }
            if (c == '"') in_string = true;
            else if (c == '{') ++depth;
            else if (c == '}') {
                if (--depth == 0) {
                    end = i;
                    break;
                }
            }
        }
        if (end == std::string::npos) return std::nullopt;
        const std::string key = "\"" + std::to_string(dps) + "\"";
        std::size_t key_pos = json.find(key, begin + 1);
        if (key_pos != std::string::npos && key_pos < end) {
            std::size_t value = json.find(':', key_pos + key.size());
            if (value != std::string::npos && value < end) {
                ++value;
                skip_ws(json, value);
                if (json.compare(value, 4, "true") == 0) return true;
                if (json.compare(value, 5, "false") == 0) return false;
            }
        }
        dps_name = json.find("\"dps\"", end + 1);
    }
    return std::nullopt;
}

std::optional<bool> request_status(int fd,
                                   const crypto::Block16& session_key,
                                   std::uint32_t& sequence,
                                   int dps,
                                   bool debug,
                                   std::string& last_json) {
    send_all(fd, pack_frame(sequence++, kCmdDpQueryNew, make_status_payload(), session_key, debug));
    for (int i = 0; i < 5; ++i) {
        Frame frame = receive_frame(fd, session_key, debug);
        if (frame.retcode != 0) {
            throw std::runtime_error("Розетка вернула ошибку retcode=" + std::to_string(frame.retcode));
        }
        if (frame.payload.empty()) continue;
        last_json = decode_json_payload(std::move(frame.payload));
        if (debug) std::cerr << "JSON: " << last_json << "\n";
        if (auto state = parse_dps_boolean(last_json, dps)) return state;
    }
    return std::nullopt;
}

bool send_switch_command(int fd,
                         const crypto::Block16& session_key,
                         std::uint32_t& sequence,
                         int dps,
                         bool state,
                         bool debug) {
    send_all(fd, pack_frame(sequence++, kCmdControlNew, make_control_payload(dps, state), session_key, debug));
    for (int i = 0; i < 3; ++i) {
        Frame frame = receive_frame(fd, session_key, debug);
        if (frame.retcode != 0) {
            throw std::runtime_error("Розетка отклонила команду, retcode=" + std::to_string(frame.retcode));
        }
        // Даже пустой GCM-кадр является валидным ACK. Дополнительный статус проверим отдельной командой.
        return true;
    }
    return false;
}

int run_device_action(const Config& config, Action action, bool debug) {
    const crypto::Block16 real_key = block_from_key(config.local_key);
    std::string last_error;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        try {
            if (debug) std::cerr << "Подключение, попытка " << attempt << "/3...\n";
            Socket socket = connect_device(config);
            std::uint32_t sequence = 1;
            const crypto::Block16 session_key = negotiate_session_key(socket.get(), real_key, sequence, debug);

            if (action == Action::Status) {
                std::string json;
                const auto state = request_status(socket.get(), session_key, sequence, config.switch_dp, debug, json);
                if (!state) {
                    std::cerr << "Ответ получен, но DPS " << config.switch_dp << " не найден.\n";
                    if (!json.empty()) std::cerr << "Ответ розетки: " << json << "\n";
                    return 8;
                }
                std::cout << "Розетка " << (*state ? "включена" : "выключена") << "\n";
                return 0;
            }

            const bool desired = action == Action::On;
            send_switch_command(socket.get(), session_key, sequence, config.switch_dp, desired, debug);

            std::string json;
            try {
                const auto actual = request_status(socket.get(), session_key, sequence, config.switch_dp, debug, json);
                if (actual) {
                    if (*actual == desired) {
                        std::cout << "Розетка " << (desired ? "включена" : "выключена") << "\n";
                        return 0;
                    }
                    std::cerr << "Команда подтверждена, но фактическое состояние не изменилось.\n";
                    return 9;
                }
            } catch (const std::exception& e) {
                if (debug) std::cerr << "Проверка состояния после команды не удалась: " << e.what() << "\n";
            }
            std::cout << "Команда " << (desired ? "включения" : "выключения") << " принята розеткой\n";
            return 0;
        } catch (const std::exception& e) {
            last_error = e.what();
            if (debug) std::cerr << "Попытка не удалась: " << last_error << "\n";
            if (attempt < 3) std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    throw std::runtime_error(last_error.empty() ? "Неизвестная ошибка связи с розеткой" : last_error);
}

} // namespace

#ifdef MADSPM_TUYA_LIBRARY
namespace madspm::tuya {

namespace {

Config native_config(const ClientConfig& source) {
    Config result;
    result.device_id = source.device_id;
    result.device_ip = source.device_ip;
    result.local_key = source.local_key;
    result.port = source.port;
    result.switch_dp = source.switch_dp;
    result.timeout_ms = source.timeout_ms;
    return result;
}

} // namespace

std::optional<bool> status(const ClientConfig& source, bool debug) {
    const Config config = native_config(source);
    const crypto::Block16 real_key = block_from_key(config.local_key);
    std::string last_error;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        try {
            Socket socket = connect_device(config);
            std::uint32_t sequence = 1;
            const crypto::Block16 session_key =
                negotiate_session_key(socket.get(), real_key, sequence, debug);
            std::string json;
            return request_status(socket.get(), session_key, sequence,
                                  config.switch_dp, debug, json);
        } catch (const std::exception& error) {
            last_error = error.what();
            if (attempt < 3) std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    throw std::runtime_error(last_error.empty() ? "Ошибка чтения Tuya DPS" : last_error);
}

bool set_power(const ClientConfig& source, bool desired, bool debug) {
    const Config config = native_config(source);
    return run_device_action(config, desired ? Action::On : Action::Off, debug) == 0;
}

bool self_test(std::string& error) {
    return crypto::run_self_tests(error);
}

} // namespace madspm::tuya
#else
int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    try {
        const Options options = parse_options(argc, argv);
        if (options.action == Action::Help) {
            print_usage(argv[0]);
            return 0;
        }
        std::string crypto_error;
        if (!crypto::run_self_tests(crypto_error)) {
            std::cerr << "Криптографическая самопроверка не пройдена: " << crypto_error << "\n";
            return 10;
        }
        if (options.action == Action::SelfTest) {
            std::cout << "Самопроверка SHA-256, HMAC-SHA256, AES-128 и AES-GCM пройдена.\n";
            return 0;
        }

        const auto [config, config_path] = load_config(options);
        if (options.debug) {
            std::cerr << "Конфиг: " << config_path << "\n"
                      << "Устройство: " << config.device_ip << ':' << config.port
                      << ", DPS=" << config.switch_dp << "\n";
        }
        return run_device_action(config, options.action, options.debug);
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << "\n";
        return 1;
    }
}
#endif
