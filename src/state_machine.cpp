#include "madspm/state_machine.hpp"

namespace madspm {

StateMachine::StateMachine(const Config& config) : config_(config) {}

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
        if (observed.ups.on_battery)
            return {State::OnBatteryGrace, Action::None, "Запуск на батарее"};
        return {State::MainsOn, Action::None, "Внешнее питание доступно"};

    case State::MainsOn:
        if (observed.ups.on_battery)
            return {State::OnBatteryGrace, Action::None, "Подтверждён переход на батарею"};
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
        return {State::ShutdownRequested, Action::RequestShutdown, "Повтор shutdown"};

    case State::WaitingServerOff:
        if (observed.proxmox_confirmed_off)
            return {State::CuttingServerPower, Action::PlugOff, "Proxmox выключен"};
        return {State::WaitingServerOff, Action::None, "Ожидание выключения Proxmox"};

    case State::CuttingServerPower:
        if (observed.plug_on && !*observed.plug_on)
            return {State::WaitingForMains, Action::None, "Розетка выключена"};
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
        return {State::RestoringServerPower, Action::PlugOn, "Подтверждаем включение розетки"};

    case State::Recovery:
        if (observed.proxmox_reachable)
            return {State::MainsOn, Action::None, "Proxmox загрузился"};
        return {State::Recovery, Action::None, "Ожидание загрузки Proxmox"};

    case State::Error:
        return {State::Error, Action::None, "Требуется ручное вмешательство"};
    }
    return {State::Error, Action::None, "Неизвестное состояние"};
}

} // namespace madspm
