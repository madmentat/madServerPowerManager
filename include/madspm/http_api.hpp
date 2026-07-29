#pragma once

#include "madspm/manager.hpp"

#include <atomic>
#include <thread>

namespace madspm {

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
