#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace madspm {

enum class State {
    Starting,
    MonitorOnly,
    MainsOn,
    OnBatteryGrace,
    ShutdownRequested,
    WaitingServerOff,
    CuttingServerPower,
    WaitingForMains,
    MainsStabilizing,
    RestoringServerPower,
    Recovery,
    Degraded,
    Error,
};

enum class Action {
    None,
    RequestShutdown,
    PlugOff,
    PlugOn,
};

struct UpsTelemetry {
    bool reachable = false;
    bool online = false;
    bool on_battery = false;
    bool low_battery = false;
    std::optional<double> battery_charge;
    std::optional<double> battery_runtime_seconds;
    std::optional<double> battery_voltage;
    std::optional<double> load_percent;
    std::optional<double> input_voltage;
    std::optional<double> input_frequency;
    std::optional<double> output_voltage;
    std::optional<double> temperature;
    std::string status;
    std::map<std::string, std::string> variables;
    std::string error;
};

struct Observations {
    UpsTelemetry ups;
    bool server_reachable = false;
    bool server_confirmed_off = false;
    bool plug_reachable = false;
    std::optional<bool> plug_on;
};

struct PersistentState {
    std::uint32_t format_version = 1;
    State state = State::Starting;
    std::string emergency_cycle_id;
    std::int64_t on_battery_since_utc = 0;
    std::int64_t on_battery_detected_since_utc = 0;
    std::int64_t mains_restored_since_utc = 0;
    std::int64_t plug_off_since_utc = 0;
    bool shutdown_sent = false;
    unsigned shutdown_attempts = 0;
    std::int64_t shutdown_sent_utc = 0;
    std::int64_t shutdown_last_attempt_utc = 0;
    std::int64_t server_unreachable_since_utc = 0;
    bool server_confirmed_off = false;
    std::int64_t plug_last_attempt_utc = 0;
    unsigned plug_attempts = 0;
    bool plug_was_cut = false;
    std::string last_error;
    std::int64_t last_success_utc = 0;
};

struct Decision {
    State next = State::Starting;
    Action action = Action::None;
    std::string reason;
};

std::string to_string(State state);
std::string to_string(Action action);
std::optional<State> state_from_string(const std::string& value);
std::int64_t utc_now_seconds();
std::string json_escape(const std::string& value);

} // namespace madspm
