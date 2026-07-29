#include "madspm/config.hpp"
#include "madspm/http_api.hpp"
#include "madspm/manager.hpp"
#include "madspm/nut_client.hpp"
#include "madspm/proxmox.hpp"
#include "madspm/state_machine.hpp"
#include "madspm/state_store.hpp"
#include "madspm/tuya_client.hpp"

#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

madspm::PowerManager* active_manager = nullptr;

void stop_handler(int) {
    if (active_manager) active_manager->request_stop();
}

void usage(const char* name) {
    std::cout
        << "madServerPowerManager 1.0.0\n"
        << "Использование: " << name << " [--config PATH] [команда]\n"
        << "  --init --doctor --status --print-ups --test-nut --test-ssh\n"
        << "  --test-plug --dry-run --simulate-on-battery --simulate-online\n"
        << "  --validate-config --show-state --reset-state --yes\n"
        << "Без команды запускается демон. По умолчанию armed=false.\n";
}

std::string argument_value(int& index, int argc, char** argv) {
    if (++index >= argc) throw std::runtime_error("Не указано значение аргумента");
    return argv[index];
}

int simulate(const madspm::Config& source, bool on_battery) {
    madspm::Config config = source;
    config.general.armed = true;
    madspm::PersistentState state;
    state.state = madspm::State::MainsOn;
    madspm::Observations observation;
    observation.ups.reachable = true;
    observation.ups.online = !on_battery;
    observation.ups.on_battery = on_battery;
    const auto decision =
        madspm::StateMachine(config).evaluate(state, observation, madspm::utc_now_seconds());
    std::cout << "{\"from\":\"" << madspm::to_string(state.state)
              << "\",\"to\":\"" << madspm::to_string(decision.next)
              << "\",\"action\":\"" << madspm::to_string(decision.action)
              << "\",\"reason\":\"" << madspm::json_escape(decision.reason) << "\"}\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    std::filesystem::path config_path =
        "/etc/mad-server-power-manager/madServerPowerManager.ini";
    std::string command;
    bool yes = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--config") config_path = argument_value(i, argc, argv);
        else if (argument == "--yes") yes = true;
        else if (argument == "--help" || argument == "-h") { usage(argv[0]); return 0; }
        else if (argument == "--version") { std::cout << "1.0.0\n"; return 0; }
        else if (command.empty()) command = argument;
        else throw std::runtime_error("Лишний аргумент: " + argument);
    }
    try {
        const bool require_secrets = command != "--init";
        madspm::Config config = madspm::ConfigLoader::load(config_path, require_secrets);
        for (const auto& warning : config.warnings) std::cerr << "Предупреждение: " << warning << '\n';
        if (command == "--validate-config") {
            std::cout << "Конфигурация корректна; armed="
                      << (config.general.armed ? "true" : "false") << '\n';
            return 0;
        }
        if (command == "--init") {
            std::cout << "Безопасный мастер не изменяет удалённые узлы.\n"
                      << "1. Установите основной конфиг из config/madServerPowerManager.ini.example.\n"
                      << "2. Создайте secrets.ini с правами 0600.\n"
                      << "3. Оставьте armed=false и выполните --doctor.\n"
                      << "4. SSH-ключ и forced command на Proxmox настраиваются отдельным этапом.\n";
            return 0;
        }
        madspm::PowerManager manager(config);
        if (command == "--status") {
            manager.observe();
            std::cout << manager.status_json() << '\n';
            return 0;
        }
        if (command == "--print-ups" || command == "--test-nut") {
            const auto ups = madspm::NutClient(config.ups).read();
            if (!ups.reachable) throw std::runtime_error(ups.error);
            for (const auto& [key, value] : ups.variables)
                std::cout << key << ": " << value << '\n';
            return 0;
        }
        if (command == "--test-plug") {
            const madspm::tuya::ClientConfig plug {
                config.secrets.tuya_device_id, config.plug.host,
                config.secrets.tuya_local_key, config.plug.port,
                config.plug.switch_dp,
                static_cast<int>(config.plug.command_timeout_seconds * 1000)};
            const auto state = madspm::tuya::status(plug, false);
            if (!state) throw std::runtime_error("DPS розетки не найден");
            std::cout << "Розетка " << (*state ? "включена" : "выключена")
                      << " (команды питания не отправлялись)\n";
            return 0;
        }
        if (command == "--test-ssh") {
            std::string error;
            if (!madspm::ProxmoxClient(config.proxmox).test_ssh(error))
                throw std::runtime_error(error);
            std::cout << "Безопасная SSH-команда выполнена\n";
            return 0;
        }
        if (command == "--doctor") {
            manager.observe();
            std::cout << manager.status_json() << '\n'
                      << "doctor: конфиг=ok, секреты=ok, действия питания=не выполнялись\n";
            return manager.config().general.armed ? 2 : 0;
        }
        if (command == "--dry-run") return manager.run_once(true);
        if (command == "--simulate-on-battery") return simulate(config, true);
        if (command == "--simulate-online") return simulate(config, false);
        if (command == "--show-state") {
            std::cout << madspm::state_to_json(
                madspm::StateStore(config.general.state_file).load());
            return 0;
        }
        if (command == "--reset-state") {
            if (!yes) throw std::runtime_error("--reset-state требует --yes");
            madspm::StateStore(config.general.state_file).reset();
            std::cout << "State-файл перемещён в резервную копию\n";
            return 0;
        }
        if (!command.empty()) throw std::runtime_error("Неизвестная команда: " + command);

        madspm::HttpApi api(manager);
        active_manager = &manager;
        std::signal(SIGTERM, stop_handler);
        std::signal(SIGINT, stop_handler);
        api.start();
        const int result = manager.run();
        api.stop();
        active_manager = nullptr;
        return result;
    } catch (const std::exception& error) {
        std::cerr << "Ошибка: " << error.what() << '\n';
        return 1;
    }
}
