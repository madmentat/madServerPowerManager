#pragma once

#include "madspm/config.hpp"
#include "madspm/types.hpp"

#include <cstdint>

namespace madspm {

class StateMachine {
public:
    explicit StateMachine(const Config& config);
    Decision evaluate(const PersistentState& state,
                      const Observations& observations,
                      std::int64_t now_utc) const;

private:
    const Config& config_;
    bool critical_battery(const UpsTelemetry& ups) const;
};

} // namespace madspm
