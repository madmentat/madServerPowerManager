#pragma once

#include "madspm/config.hpp"
#include "madspm/state_store.hpp"
#include "madspm/types.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace madspm {

class PowerManager {
public:
    explicit PowerManager(Config config);
    int run();
    int run_once(bool dry_run);
    Observations observe();
    std::string status_json() const;
    std::string ups_json() const;
    std::string server_json() const;
    std::string plug_json() const;
    std::string config_json() const;
    std::string events_json() const;
    std::string health_json() const;
    const Config& config() const;
    void request_stop();

private:
    bool apply_decision(const Decision& decision, bool dry_run);
    void record_event(const std::string& severity,
                      const std::string& component,
                      const std::string& action,
                      const std::string& result,
                      const std::string& code = "");
    Config config_;
    StateStore store_;
    mutable std::mutex mutex_;
    PersistentState state_;
    Observations observations_;
    std::vector<std::string> events_;
    std::atomic<bool> stop_{false};
    std::int64_t started_utc_ = 0;
};

} // namespace madspm
