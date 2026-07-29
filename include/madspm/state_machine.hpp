#pragma once

#include "madspm/config.hpp"
#include "madspm/types.hpp"

#include <cstdint>

namespace madspm {

class StateMachine {
public:
    explicit StateMachine(const Config& config);
    void update_on_battery_detection(PersistentState& state,
                                     const Observations& observations,
                                     std::int64_t now_utc) const;
    void update_server_off_confirmation(PersistentState& state,
                                        Observations& observations,
                                        std::int64_t now_utc) const;
    Decision evaluate(const PersistentState& state,
                      const Observations& observations,
                      std::int64_t now_utc) const;

private:
    const Config& config_;
    bool critical_battery(const UpsTelemetry& ups) const;
};

} // namespace madspm
