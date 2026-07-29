#pragma once

#include "madspm/manager.hpp"

#include <atomic>
#include <thread>

namespace madspm {

struct HttpResponse {
    std::string status;
    std::string body;
};

HttpResponse route_http_request(PowerManager& manager,
                                const std::string& method,
                                const std::string& path);

class HttpApi {
public:
    explicit HttpApi(PowerManager& manager);
    ~HttpApi();
    void start();
    void stop();

private:
    void serve();
    PowerManager& manager_;
    std::atomic<bool> stopping_{false};
    int listen_fd_ = -1;
    std::thread thread_;
};

} // namespace madspm
