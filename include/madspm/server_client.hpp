#pragma once

#include "madspm/config.hpp"

#include <string>

namespace madspm {

class ServerClient {
public:
    explicit ServerClient(ServerConfig config);
    bool reachable() const;
    bool request_shutdown(std::string& error) const;
    bool test_ssh(std::string& error) const;

private:
    ServerConfig config_;
    bool run_ssh(const std::string& command, std::string& error) const;
};

} // namespace madspm
