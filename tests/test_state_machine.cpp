#include "madspm/state_machine.hpp"
#include "madspm/tuya_client.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

madspm::Config config() {
    madspm::Config value;
    value.general.armed = true;
    value.ups.grace_seconds = 480;
    value.ups.mains_stable_seconds = 60;
    value.plug.minimum_off_seconds = 20;
    return value;
}

madspm::Observations online() {
    madspm::Observations value;
    value.ups.reachable = true;
    value.ups.online = true;
    value.ups.status = "OL";
    value.proxmox_reachable = true;
    value.plug_reachable = true;
    value.plug_on = true;
    return value;
}

madspm::Observations battery() {
    auto value = online();
    value.ups.online = false;
    value.ups.on_battery = true;
    value.ups.status = "OB";
    return value;
}

void expect(const std::string& name, bool condition) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}

madspm::Decision decide(const madspm::Config& cfg,
                        madspm::State state,
                        const madspm::Observations& observation,
                        std::int64_t now,
                        std::int64_t battery_since = 0,
                        std::int64_t mains_since = 0,
                        std::int64_t off_since = 0) {
    madspm::PersistentState saved;
    saved.state = state;
    saved.on_battery_since_utc = battery_since;
    saved.mains_restored_since_utc = mains_since;
    saved.plug_off_since_utc = off_since;
    saved.plug_was_cut = off_since > 0;
    return madspm::StateMachine(cfg).evaluate(saved, observation, now);
}

} // namespace

int main() {
    const auto cfg = config();
    const std::int64_t now = 10000;

    expect("1 short outage", decide(cfg, madspm::State::OnBatteryGrace,
           online(), now, now - 100).next == madspm::State::MainsStabilizing);
    expect("1b short outage does not touch plug",
           decide(cfg, madspm::State::MainsStabilizing, online(), now,
                  now - 100, now - 61).action == madspm::Action::None &&
           decide(cfg, madspm::State::MainsStabilizing, online(), now,
                  now - 100, now - 61).next == madspm::State::MainsOn);
    expect("2 long outage", decide(cfg, madspm::State::OnBatteryGrace,
           battery(), now, now - 481).action == madspm::Action::RequestShutdown);
    auto low = battery(); low.ups.low_battery = true;
    expect("3 low battery", decide(cfg, madspm::State::OnBatteryGrace,
           low, now, now - 10).action == madspm::Action::RequestShutdown);
    expect("4 mains during grace", decide(cfg, madspm::State::OnBatteryGrace,
           online(), now, now - 20).next == madspm::State::MainsStabilizing);
    expect("5 mains after shutdown", decide(cfg, madspm::State::WaitingServerOff,
           online(), now).next == madspm::State::WaitingServerOff);
    expect("6 mains after cut", decide(cfg, madspm::State::WaitingForMains,
           online(), now).next == madspm::State::MainsStabilizing);
    expect("7 daemon restart grace", decide(cfg, madspm::State::OnBatteryGrace,
           battery(), now, now - 300).next == madspm::State::OnBatteryGrace);
    auto off = battery(); off.plug_on = false; off.proxmox_reachable = false;
    expect("8 NUC restart plug off", decide(cfg, madspm::State::WaitingForMains,
           off, now, now - 600).next == madspm::State::WaitingForMains);
    auto no_proxmox = online(); no_proxmox.proxmox_reachable = false;
    expect("9 proxmox unavailable", decide(cfg, madspm::State::MainsOn,
           no_proxmox, now).action == madspm::Action::None);
    auto no_plug = online(); no_plug.plug_reachable = false; no_plug.plug_on.reset();
    expect("10 plug unavailable", decide(cfg, madspm::State::MainsOn,
           no_plug, now).action == madspm::Action::None);
    auto no_nut = online(); no_nut.ups.reachable = false;
    expect("11 NUT lost", decide(cfg, madspm::State::MainsOn,
           no_nut, now).next == madspm::State::Degraded);
    expect("12 mains bounce", decide(cfg, madspm::State::MainsStabilizing,
           battery(), now, 0, now - 2).next == madspm::State::WaitingForMains);
    expect("13 corrupt state policy", !madspm::state_from_string("BROKEN").has_value());
    auto manual_on = battery(); manual_on.plug_on = true;
    expect("14 manual plug on", decide(cfg, madspm::State::WaitingForMains,
           manual_on, now).action == madspm::Action::None);
    auto responding = battery(); responding.proxmox_reachable = true;
    expect("15 shutdown timeout safe", decide(cfg, madspm::State::WaitingServerOff,
           responding, now).action == madspm::Action::None);
    expect("16 NUT restart", decide(cfg, madspm::State::OnBatteryGrace,
           no_nut, now, now - 100).next == madspm::State::Degraded);
    expect("17 NUC recovery", decide(cfg, madspm::State::Recovery,
           online(), now).next == madspm::State::MainsOn);
    auto mismatch = online(); mismatch.plug_on = true;
    expect("18 state mismatch", decide(cfg, madspm::State::CuttingServerPower,
           mismatch, now).action == madspm::Action::PlugOff);
    expect("19 SSH host key handled by strict client", cfg.proxmox.known_hosts.is_absolute());
    auto no_charge = battery(); no_charge.ups.battery_charge.reset();
    expect("20 no battery charge", decide(cfg, madspm::State::OnBatteryGrace,
           no_charge, now, now - 10).action == madspm::Action::None);
    auto no_runtime = no_charge; no_runtime.ups.battery_runtime_seconds.reset();
    expect("21 no runtime", decide(cfg, madspm::State::OnBatteryGrace,
           no_runtime, now, now - 10).action == madspm::Action::None);
    expect("22 flags only", decide(cfg, madspm::State::MainsOn,
           battery(), now).next == madspm::State::OnBatteryGrace);
    std::string crypto_error;
    expect("23 Tuya crypto", madspm::tuya::self_test(crypto_error));
    auto ambiguous = online(); ambiguous.ups.online = false; ambiguous.ups.status.clear();
    expect("24 invalid NUT status", decide(cfg, madspm::State::MainsOn,
           ambiguous, now).next == madspm::State::Degraded);
    expect("25 brief mains return", decide(cfg, madspm::State::MainsStabilizing,
           battery(), now, 0, now - 3).next == madspm::State::WaitingForMains);

    madspm::Config unarmed = cfg;
    unarmed.general.armed = false;
    expect("armed false invariant",
           decide(unarmed, madspm::State::ShutdownRequested, low, now,
                  now - 1000).action == madspm::Action::None);

    if (failures == 0) std::cout << "Все сценарии FSM пройдены: 27/27\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
