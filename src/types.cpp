#include "madspm/types.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace madspm {

std::string to_string(State state) {
    switch (state) {
    case State::Starting: return "STARTING";
    case State::MonitorOnly: return "MONITOR_ONLY";
    case State::MainsOn: return "MAINS_ON";
    case State::OnBatteryGrace: return "ON_BATTERY_GRACE";
    case State::ShutdownRequested: return "SHUTDOWN_REQUESTED";
    case State::WaitingServerOff: return "WAITING_SERVER_OFF";
    case State::CuttingServerPower: return "CUTTING_SERVER_POWER";
    case State::WaitingForMains: return "WAITING_FOR_MAINS";
    case State::MainsStabilizing: return "MAINS_STABILIZING";
    case State::RestoringServerPower: return "RESTORING_SERVER_POWER";
    case State::Recovery: return "RECOVERY";
    case State::Degraded: return "DEGRADED";
    case State::Error: return "ERROR";
    }
    return "ERROR";
}

std::string to_string(Action action) {
    switch (action) {
    case Action::None: return "NONE";
    case Action::RequestShutdown: return "REQUEST_SHUTDOWN";
    case Action::PlugOff: return "PLUG_OFF";
    case Action::PlugOn: return "PLUG_ON";
    }
    return "NONE";
}

std::optional<State> state_from_string(const std::string& value) {
    for (State state : {State::Starting, State::MonitorOnly, State::MainsOn,
                        State::OnBatteryGrace, State::ShutdownRequested,
                        State::WaitingServerOff, State::CuttingServerPower,
                        State::WaitingForMains, State::MainsStabilizing,
                        State::RestoringServerPower, State::Recovery,
                        State::Degraded, State::Error}) {
        if (to_string(state) == value) return state;
    }
    return std::nullopt;
}

std::int64_t utc_now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string json_escape(const std::string& value) {
    std::ostringstream output;
    for (unsigned char ch : value) {
        switch (ch) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (ch < 0x20) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(ch);
            } else {
                output << static_cast<char>(ch);
            }
        }
    }
    return output.str();
}

} // namespace madspm
