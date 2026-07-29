#include "madspm/state_machine.hpp"

namespace madspm {

StateMachine::StateMachine(const Config& config) : config_(config) {}

void StateMachine::update_on_battery_detection(
    PersistentState& state,
    const Observations& observations,
    std::int64_t now_utc) const {
    if (!observations.ups.reachable || !observations.ups.on_battery) {
        state.on_battery_detected_since_utc = 0;
        return;
    }
    if (state.on_battery_detected_since_utc == 0)
        state.on_battery_detected_since_utc = now_utc;
}

void StateMachine::update_server_off_confirmation(
    PersistentState& state,
    Observations& observations,
    std::int64_t now_utc) const {
    observations.server_confirmed_off = false;
    if (!state.shutdown_sent) {
        state.server_unreachable_since_utc = 0;
        state.server_confirmed_off = false;
        return;
    }
    // State-файлы старой версии не содержат timestamp отправки shutdown.
    // Для них начинаем безопасное окно подтверждения заново.
    if (state.shutdown_sent_utc == 0)
        state.shutdown_sent_utc = now_utc;
    if (observations.server_reachable) {
        state.server_unreachable_since_utc = 0;
        state.server_confirmed_off = false;
        return;
    }
    if (state.server_unreachable_since_utc == 0)
        state.server_unreachable_since_utc = now_utc;
    const bool confirmed =
        now_utc - state.server_unreachable_since_utc >=
        static_cast<std::int64_t>(config_.server.server_off_confirm_seconds);
    state.server_confirmed_off = confirmed;
    observations.server_confirmed_off = confirmed;
}

bool StateMachine::critical_battery(const UpsTelemetry& ups) const {
    if (config_.ups.use_low_battery_flag && ups.low_battery) return true;
    if (ups.battery_charge &&
        *ups.battery_charge <= config_.ups.critical_battery_charge) return true;
    return ups.battery_runtime_seconds &&
           *ups.battery_runtime_seconds <= config_.ups.critical_runtime_seconds;
}

Decision StateMachine::evaluate(const PersistentState& current,
                                const Observations& observed,
                                std::int64_t now) const {
    if (!config_.general.armed)
        return {State::MonitorOnly, Action::None, "armed=false"};
    if (!observed.ups.reachable)
        return {State::Degraded, Action::None, "Нет достоверной связи с NUT"};
    if (!observed.ups.online && !observed.ups.on_battery)
        return {State::Degraded, Action::None, "Неоднозначный ups.status"};

    switch (current.state) {
    case State::Starting:
    case State::MonitorOnly:
    case State::Degraded:
        if (observed.ups.on_battery &&
            current.on_battery_detected_since_utc > 0 &&
            now - current.on_battery_detected_since_utc >=
                static_cast<std::int64_t>(
                    config_.ups.on_battery_confirm_seconds))
            return {State::OnBatteryGrace, Action::None, "Запуск на батарее"};
        if (observed.ups.on_battery)
            return {current.state, Action::None,
                    "Подтверждение перехода на батарею"};
        return {State::MainsOn, Action::None, "Внешнее питание доступно"};

    case State::MainsOn:
        if (observed.ups.on_battery &&
            current.on_battery_detected_since_utc > 0 &&
            now - current.on_battery_detected_since_utc >=
                static_cast<std::int64_t>(
                    config_.ups.on_battery_confirm_seconds))
            return {State::OnBatteryGrace, Action::None, "Подтверждён переход на батарею"};
        if (observed.ups.on_battery)
            return {State::MainsOn, Action::None,
                    "Подтверждение перехода на батарею"};
        return {State::MainsOn, Action::None, "Питание стабильно"};

    case State::OnBatteryGrace:
        if (observed.ups.online)
            return {State::MainsStabilizing, Action::None, "Питание вернулось в grace period"};
        if (critical_battery(observed.ups) ||
            (current.on_battery_since_utc > 0 &&
             now - current.on_battery_since_utc >=
                 static_cast<std::int64_t>(config_.ups.grace_seconds)))
            return {State::ShutdownRequested, Action::RequestShutdown,
                    critical_battery(observed.ups) ? "Критическая батарея" : "Grace period истёк"};
        return {State::OnBatteryGrace, Action::None, "Ожидание grace period"};

    case State::ShutdownRequested:
        if (current.shutdown_sent)
            return {State::WaitingServerOff, Action::None, "Shutdown отправлен"};
        if (current.shutdown_attempts >= config_.server.shutdown_max_attempts)
            return {State::Degraded, Action::None,
                    "Исчерпаны попытки отправки shutdown"};
        if (current.shutdown_last_attempt_utc > 0 &&
            now - current.shutdown_last_attempt_utc <
                static_cast<std::int64_t>(config_.server.shutdown_retry_seconds))
            return {State::ShutdownRequested, Action::None,
                    "Ожидание перед повтором shutdown"};
        return {State::ShutdownRequested, Action::RequestShutdown, "Повтор shutdown"};

    case State::WaitingServerOff:
        if (observed.server_confirmed_off)
            return {State::CuttingServerPower, Action::PlugOff, "Сервер выключен"};
        if (current.shutdown_sent_utc > 0 &&
            now - current.shutdown_sent_utc >=
                static_cast<std::int64_t>(config_.server.shutdown_timeout_seconds)) {
            if (config_.server.force_cut_after_shutdown_timeout)
                return {State::CuttingServerPower, Action::PlugOff,
                        "Тайм-аут shutdown; разрешено принудительное снятие питания"};
            if (observed.ups.online && observed.server_reachable)
                return {State::MainsOn, Action::None,
                        "Сеть восстановлена, сервер доступен: аварийный цикл отменён"};
            return {State::Degraded, Action::None,
                    "Тайм-аут shutdown без подтверждения выключения сервера"};
        }
        return {State::WaitingServerOff, Action::None, "Ожидание выключения сервера"};

    case State::CuttingServerPower:
        if (observed.plug_on && !*observed.plug_on)
            return {State::WaitingForMains, Action::None, "Розетка выключена"};
        if (current.plug_last_attempt_utc > 0 &&
            now - current.plug_last_attempt_utc <
                static_cast<std::int64_t>(config_.plug.retry_seconds))
            return {State::CuttingServerPower, Action::None,
                    "Ожидание повтора выключения розетки"};
        return {State::CuttingServerPower, Action::PlugOff, "Подтверждаем выключение розетки"};

    case State::WaitingForMains:
        if (observed.ups.online)
            return {State::MainsStabilizing, Action::None, "Внешнее питание восстановлено"};
        return {State::WaitingForMains, Action::None, "Ожидание внешнего питания"};

    case State::MainsStabilizing:
        if (observed.ups.on_battery)
            return {State::WaitingForMains, Action::None, "Питание снова пропало"};
        if (current.mains_restored_since_utc > 0 &&
            now - current.mains_restored_since_utc >=
                static_cast<std::int64_t>(config_.ups.mains_stable_seconds)) {
            if (!current.plug_was_cut)
                return {State::MainsOn, Action::None,
                        "Краткое отключение завершено, розетка не переключалась"};
            if (current.plug_off_since_utc > 0 &&
                now - current.plug_off_since_utc >=
                    static_cast<std::int64_t>(config_.plug.minimum_off_seconds))
                return {State::RestoringServerPower, Action::PlugOn, "Питание стабильно"};
        }
        return {State::MainsStabilizing, Action::None, "Проверка стабильности сети"};

    case State::RestoringServerPower:
        if (observed.plug_on && *observed.plug_on)
            return {State::Recovery, Action::None, "Розетка включена"};
        if (current.plug_last_attempt_utc > 0 &&
            now - current.plug_last_attempt_utc <
                static_cast<std::int64_t>(config_.plug.retry_seconds))
            return {State::RestoringServerPower, Action::None,
                    "Ожидание повтора включения розетки"};
        return {State::RestoringServerPower, Action::PlugOn, "Подтверждаем включение розетки"};

    case State::Recovery:
        if (observed.server_reachable)
            return {State::MainsOn, Action::None, "Сервер загрузился"};
        return {State::Recovery, Action::None, "Ожидание загрузки сервера"};

    case State::Error:
        return {State::Error, Action::None, "Требуется ручное вмешательство"};
    }
    return {State::Error, Action::None, "Неизвестное состояние"};
}

} // namespace madspm
