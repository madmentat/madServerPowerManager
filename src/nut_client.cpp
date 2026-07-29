#include "madspm/nut_client.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <memory>
#include <netdb.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace madspm {
namespace {

class Fd {
public:
    explicit Fd(int value = -1) : value_(value) {}
    ~Fd() { if (value_ >= 0) ::close(value_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const { return value_; }
private:
    int value_;
};

std::string unquote(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    std::string result;
    bool escaped = false;
    for (char ch : value) {
        if (escaped) {
            result.push_back(ch);
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

std::optional<double> numeric(const std::map<std::string, std::string>& values,
                              const std::string& name) {
    const auto it = values.find(name);
    if (it == values.end()) return std::nullopt;
    try { return std::stod(it->second); } catch (...) { return std::nullopt; }
}

} // namespace

NutClient::NutClient(UpsConfig config) : config_(std::move(config)) {}

std::map<std::string, std::string> NutClient::query_variables() const {
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* raw = nullptr;
    const std::string port = std::to_string(config_.port);
    if (::getaddrinfo(config_.host.c_str(), port.c_str(), &hints, &raw) != 0)
        throw std::runtime_error("NUT: не удалось разрешить адрес");
    std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> addresses(raw, ::freeaddrinfo);
    int socket_fd = -1;
    for (auto* current = raw; current; current = current->ai_next) {
        socket_fd = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (socket_fd < 0) continue;
        timeval timeout {5, 0};
        ::setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (::connect(socket_fd, current->ai_addr, current->ai_addrlen) == 0) break;
        ::close(socket_fd);
        socket_fd = -1;
    }
    if (socket_fd < 0) throw std::runtime_error("NUT: соединение не установлено");
    Fd connection(socket_fd);
    const std::string request = "LIST VAR " + config_.name + "\n";
    if (::send(connection.get(), request.data(), request.size(), MSG_NOSIGNAL) < 0)
        throw std::runtime_error("NUT: ошибка отправки запроса");
    std::string buffer;
    char chunk[2048];
    while (true) {
        const ssize_t received = ::recv(connection.get(), chunk, sizeof(chunk), 0);
        if (received < 0) throw std::runtime_error("NUT: ошибка чтения ответа");
        if (received == 0) break;
        buffer.append(chunk, static_cast<std::size_t>(received));
        if (buffer.find("END LIST VAR ") != std::string::npos ||
            buffer.find("ERR ") != std::string::npos) break;
        if (buffer.size() > 1024 * 1024) throw std::runtime_error("NUT: слишком большой ответ");
    }
    if (buffer.rfind("ERR ", 0) == 0) throw std::runtime_error("NUT: " + buffer);
    std::map<std::string, std::string> result;
    std::size_t offset = 0;
    while (offset < buffer.size()) {
        const auto newline = buffer.find('\n', offset);
        const std::string line = buffer.substr(offset, newline - offset);
        offset = newline == std::string::npos ? buffer.size() : newline + 1;
        const std::string prefix = "VAR " + config_.name + " ";
        if (line.rfind(prefix, 0) != 0) continue;
        const std::size_t name_end = line.find(' ', prefix.size());
        if (name_end == std::string::npos) continue;
        result[line.substr(prefix.size(), name_end - prefix.size())] =
            unquote(line.substr(name_end + 1));
    }
    if (result.empty()) throw std::runtime_error("NUT: список переменных пуст");
    return result;
}

UpsTelemetry NutClient::read() const {
    UpsTelemetry result;
    try {
        result.variables = query_variables();
        result.reachable = true;
        const auto it = result.variables.find("ups.status");
        if (it == result.variables.end())
            throw std::runtime_error("NUT не вернул ups.status");
        result.status = it->second;
        result.online = result.status.find("OL") != std::string::npos;
        result.on_battery = result.status.find("OB") != std::string::npos;
        result.low_battery = result.status.find("LB") != std::string::npos;
        result.battery_charge = numeric(result.variables, "battery.charge");
        result.battery_runtime_seconds = numeric(result.variables, "battery.runtime");
        result.battery_voltage = numeric(result.variables, "battery.voltage");
        result.load_percent = numeric(result.variables, "ups.load");
        result.input_voltage = numeric(result.variables, "input.voltage");
        result.input_frequency = numeric(result.variables, "input.frequency");
        result.output_voltage = numeric(result.variables, "output.voltage");
        result.temperature = numeric(result.variables, "ups.temperature");
    } catch (const std::exception& error) {
        result.error = error.what();
    }
    return result;
}

} // namespace madspm
