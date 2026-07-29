#include "madspm/manager.hpp"

#include "madspm/nut_client.hpp"
#include "madspm/server_client.hpp"
#include "madspm/state_machine.hpp"
#include "madspm/tuya_client.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>

namespace madspm {
namespace {

std::string optional_number(const std::optional<double>& value) {
    if (!value) return "null";
    std::ostringstream out;
    out << *value;
    return out.str();
}

tuya::ClientConfig plug_config(const Config& config) {
    return {config.secrets.tuya_device_id, config.plug.host,
            config.secrets.tuya_local_key, config.plug.port,
            config.plug.switch_dp,
            static_cast<int>(config.plug.command_timeout_seconds * 1000)};
}

} // namespace

PowerManager::PowerManager(Config config)
    : config_(std::move(config)),
      store_(config_.general.state_file),
      started_utc_(utc_now_seconds()) {
    try {
        state_ = store_.load();
    } catch (const std::exception& error) {
        const auto damaged = store_.path().string() + ".corrupt." +
                             std::to_string(utc_now_seconds());
        if (std::filesystem::exists(store_.path()))
            std::filesystem::rename(store_.path(), damaged);
        state_.state = config_.general.armed ? State::Error : State::MonitorOnly;
        state_.last_error = error.what();
        record_event("error", "state_store", "load", "corrupt", "STATE_CORRUPT");
    }
}

Observations PowerManager::observe() {
    Observations result;
    result.ups = NutClient(config_.ups).read();
    ServerClient server(config_.server);
    result.server_reachable = server.reachable();
    try {
        if (!config_.secrets.tuya_device_id.empty() &&
            !config_.secrets.tuya_local_key.empty()) {
            result.plug_on = tuya::status(plug_config(config_));
            result.plug_reachable = result.plug_on.has_value();
        }
    } catch (const std::exception& error) {
        record_event("warning", "plug", "status", error.what(), "PLUG_UNREACHABLE");
    }
    {
        std::lock_guard lock(mutex_);
        observations_ = result;
    }
    return result;
}

bool PowerManager::apply_decision(const Decision& decision, bool dry_run) {
    const State previous = state_.state;
    if (decision.next != previous) {
        state_.state = decision.next;
        const auto now = utc_now_seconds();
        if (decision.next == State::MonitorOnly) {
            state_ = {};
            state_.state = State::MonitorOnly;
        }
        if (decision.next == State::OnBatteryGrace && state_.on_battery_since_utc == 0) {
            state_.on_battery_since_utc = now;
            state_.emergency_cycle_id = std::to_string(now);
        }
        if (decision.next == State::MainsStabilizing &&
            state_.mains_restored_since_utc == 0)
            state_.mains_restored_since_utc = now;
        if (decision.next == State::MainsOn &&
             (previous == State::Recovery ||
             previous == State::Degraded ||
             (previous == State::MainsStabilizing && !state_.plug_was_cut) ||
             (previous == State::WaitingServerOff && !state_.plug_was_cut))) {
            state_ = {};
            state_.state = State::MainsOn;
        }
        record_event("info", "fsm", "transition",
                     to_string(previous) + "->" + to_string(decision.next) +
                         ": " + decision.reason);
    }
    if (decision.action == Action::None)
        return state_.state != State::Degraded && state_.state != State::Error &&
               state_.last_error.empty();
    if (!config_.general.armed || dry_run) {
        record_event("info", "fsm", to_string(decision.action),
                     dry_run ? "dry-run" : "blocked: armed=false");
        return dry_run;
    }

    try {
        if (decision.action == Action::RequestShutdown) {
            std::string error;
            ++state_.shutdown_attempts;
            state_.shutdown_last_attempt_utc = utc_now_seconds();
            if (ServerClient(config_.server).request_shutdown(error)) {
                state_.shutdown_sent = true;
                if (state_.shutdown_sent_utc == 0)
                    state_.shutdown_sent_utc = state_.shutdown_last_attempt_utc;
                state_.server_unreachable_since_utc = 0;
                state_.server_confirmed_off = false;
                state_.state = State::WaitingServerOff;
                state_.last_error.clear();
                record_event("warning", "server", "shutdown", "sent");
                return true;
            } else {
                state_.last_error = error;
                record_event("error", "server", "shutdown", error, "SSH_FAILED");
                return false;
            }
        } else if (decision.action == Action::PlugOff) {
            if (!observations_.server_confirmed_off &&
                !config_.server.force_cut_after_shutdown_timeout) {
                state_.state = State::Degraded;
                state_.last_error =
                    "Нет достаточного подтверждения выключения сервера";
                record_event("error", "safety", "plug_off", "blocked",
                             "SERVER_OFF_UNCONFIRMED");
                return false;
            }
            ++state_.plug_attempts;
            state_.plug_last_attempt_utc = utc_now_seconds();
            if (tuya::set_power(plug_config(config_), false)) {
                state_.plug_was_cut = true;
                state_.plug_off_since_utc = utc_now_seconds();
                state_.state = State::WaitingForMains;
                state_.last_error.clear();
                record_event("warning", "plug", "off", "confirmed");
                return true;
            }
            state_.last_error = "Розетка не подтвердила команду выключения";
            record_event("error", "plug", "off", state_.last_error,
                         "PLUG_COMMAND_REJECTED");
            return false;
        } else if (decision.action == Action::PlugOn) {
            if (!observations_.ups.online) {
                state_.state = State::Degraded;
                state_.last_error = "Включение заблокировано: ИБП не подтверждает OL";
                record_event("error", "safety", "plug_on", "blocked", "UPS_NOT_ONLINE");
                return false;
            }
            ++state_.plug_attempts;
            state_.plug_last_attempt_utc = utc_now_seconds();
            if (tuya::set_power(plug_config(config_), true)) {
                state_.state = State::Recovery;
                state_.last_error.clear();
                record_event("warning", "plug", "on", "confirmed");
                return true;
            }
            state_.last_error = "Розетка не подтвердила команду включения";
            record_event("error", "plug", "on", state_.last_error,
                         "PLUG_COMMAND_REJECTED");
            return false;
        }
    } catch (const std::exception& error) {
        state_.last_error = error.what();
        if (decision.action != Action::PlugOff &&
            decision.action != Action::PlugOn)
            state_.state = State::Degraded;
        record_event("error", "action", to_string(decision.action), error.what(),
                     "ACTION_FAILED");
        return false;
    }
    return false;
}

int PowerManager::run_once(bool dry_run) {
    Observations current = observe();
    const auto now = utc_now_seconds();
    StateMachine state_machine(config_);
    {
        std::lock_guard lock(mutex_);
        state_machine.update_on_battery_detection(state_, current, now);
        state_machine.update_server_off_confirmation(state_, current, now);
        observations_ = current;
    }
    const Decision decision = state_machine.evaluate(state_, current, now);
    {
        std::lock_guard lock(mutex_);
        const bool succeeded = apply_decision(decision, dry_run);
        if (succeeded && state_.state != State::Degraded &&
            state_.state != State::Error)
            state_.last_success_utc = utc_now_seconds();
        store_.save(state_);
    }
    return current.ups.reachable ? 0 : 1;
}

int PowerManager::run() {
    record_event("info", "daemon", "start",
                 config_.general.armed ? "armed=true" : "armed=false");
    while (!stop_) {
        try {
            run_once(false);
        } catch (const std::exception& error) {
            std::lock_guard lock(mutex_);
            state_.state = State::Error;
            state_.last_error = error.what();
            record_event("error", "daemon", "tick", error.what(), "TICK_FAILED");
        }
        for (unsigned i = 0; i < config_.general.poll_interval_seconds * 10 && !stop_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    record_event("info", "daemon", "stop", "clean");
    return 0;
}

void PowerManager::record_event(const std::string& severity,
                                const std::string& component,
                                const std::string& action,
                                const std::string& result,
                                const std::string& code) {
    std::ostringstream event;
    event << "{\"utc\":" << utc_now_seconds()
          << ",\"severity\":\"" << json_escape(severity)
          << "\",\"component\":\"" << json_escape(component)
          << "\",\"state\":\"" << to_string(state_.state)
          << "\",\"cycle_id\":\"" << json_escape(state_.emergency_cycle_id)
          << "\",\"action\":\"" << json_escape(action)
          << "\",\"result\":\"" << json_escape(result)
          << "\",\"code\":\"" << json_escape(code) << "\"}";
    if (events_.size() >= 100) events_.erase(events_.begin());
    events_.push_back(event.str());
    std::clog << event.str() << '\n';
}

std::string PowerManager::status_json() const {
    std::lock_guard lock(mutex_);
    std::ostringstream out;
    out << "{\"version\":\"1.0.0\",\"uptime_seconds\":"
        << (utc_now_seconds() - started_utc_)
        << ",\"armed\":" << (config_.general.armed ? "true" : "false")
        << ",\"state\":\"" << to_string(state_.state)
        << "\",\"ups_reachable\":" << (observations_.ups.reachable ? "true" : "false")
        << ",\"ups_status\":\"" << json_escape(observations_.ups.status)
        << "\",\"battery_charge\":" << optional_number(observations_.ups.battery_charge)
        << ",\"battery_runtime_seconds\":"
        << optional_number(observations_.ups.battery_runtime_seconds)
        << ",\"load_percent\":" << optional_number(observations_.ups.load_percent)
        << ",\"input_voltage\":" << optional_number(observations_.ups.input_voltage)
        << ",\"server_reachable\":"
        << (observations_.server_reachable ? "true" : "false")
        << ",\"server_confirmed_off\":"
        << (observations_.server_confirmed_off ? "true" : "false")
        << ",\"plug_reachable\":" << (observations_.plug_reachable ? "true" : "false")
        << ",\"plug_on\":";
    if (observations_.plug_on) out << (*observations_.plug_on ? "true" : "false");
    else out << "null";
    out << ",\"last_error\":\"" << json_escape(state_.last_error)
        << "\",\"last_success_utc\":" << state_.last_success_utc
        << ",\"cycle_id\":\"" << json_escape(state_.emergency_cycle_id) << "\"}";
    return out.str();
}

std::string PowerManager::ups_json() const {
    std::lock_guard lock(mutex_);
    std::ostringstream out;
    out << "{\"reachable\":" << (observations_.ups.reachable ? "true" : "false")
        << ",\"status\":\"" << json_escape(observations_.ups.status) << "\",\"variables\":{";
    bool first = true;
    for (const auto& [key, value] : observations_.ups.variables) {
        if (!first) out << ',';
        first = false;
        out << '"' << json_escape(key) << "\":\"" << json_escape(value) << '"';
    }
    out << "}}";
    return out.str();
}

std::string PowerManager::server_json() const {
    std::lock_guard lock(mutex_);
    return std::string("{\"host\":\"") + json_escape(config_.server.host) +
           "\",\"reachable\":" +
           (observations_.server_reachable ? "true}" : "false}");
}

std::string PowerManager::plug_json() const {
    std::lock_guard lock(mutex_);
    std::string state = "null";
    if (observations_.plug_on) state = *observations_.plug_on ? "true" : "false";
    return std::string("{\"host\":\"") + json_escape(config_.plug.host) +
           "\",\"reachable\":" + (observations_.plug_reachable ? "true" : "false") +
           ",\"on\":" + state + "}";
}

std::string PowerManager::config_json() const {
    std::ostringstream out;
    out << "{\"armed\":" << (config_.general.armed ? "true" : "false")
        << ",\"poll_interval_seconds\":" << config_.general.poll_interval_seconds
        << ",\"nut_host\":\"" << json_escape(config_.ups.host)
        << "\",\"nut_name\":\"" << json_escape(config_.ups.name)
        << "\",\"server_host\":\"" << json_escape(config_.server.host)
        << "\",\"plug_host\":\"" << json_escape(config_.plug.host)
        << "\",\"api_listen\":\"" << json_escape(config_.api.listen_address)
        << "\",\"api_port\":" << config_.api.port
        << ",\"allow_control\":" << (config_.api.allow_control ? "true" : "false")
        << "}";
    return out.str();
}

std::string PowerManager::events_json() const {
    std::lock_guard lock(mutex_);
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < events_.size(); ++i) {
        if (i) out << ',';
        out << events_[i];
    }
    out << ']';
    return out.str();
}

std::string PowerManager::health_json() const {
    std::lock_guard lock(mutex_);
    const bool healthy = observations_.ups.reachable &&
                         state_.state != State::Error;
    return std::string("{\"ok\":") + (healthy ? "true" : "false") +
           ",\"state\":\"" + to_string(state_.state) + "\"}";
}

const Config& PowerManager::config() const { return config_; }
void PowerManager::request_stop() { stop_ = true; }

} // namespace madspm
