#include "madspm/config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>

namespace {
int failures = 0;
void expect(const char* name, bool condition) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}

template <typename Function>
bool throws(Function function) {
    try {
        function();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}
} // namespace

int main() {
    char directory_template[] = "/tmp/madspm-config-test-XXXXXX";
    const char* raw_directory = ::mkdtemp(directory_template);
    if (!raw_directory) return EXIT_FAILURE;
    const std::filesystem::path directory(raw_directory);
    const auto secrets = directory / "secrets.ini";
    const auto config = directory / "manager.ini";
    {
        std::ofstream output(secrets);
        output << "[tuya]\ndevice_id=test-device\nlocal_key=1234567890abcdef\n"
                  "[api]\ntoken=test-token\n";
    }
    ::chmod(secrets.c_str(), 0600);
    {
        std::ofstream output(config);
        output << "[general]\narmed=false\npoll_interval_seconds=7\n"
               << "state_file=" << (directory / "state.json").string() << '\n'
               << "secrets_file=" << secrets.string() << '\n'
               << "unknown_option=warn\n"
                  "[ups]\nnut_host=localhost\non_battery_confirm_seconds=10\n"
                  "grace_seconds=480\n"
                  "[server]\nhost=server.example.test\n"
                  "command_timeout_seconds=3\nshutdown_timeout_seconds=360\n"
                  "[plug]\nhost=127.0.0.2\nminimum_off_seconds=20\n"
                  "[api]\nenabled=true\nallow_control=false\n";
    }

    const madspm::Config loaded = madspm::ConfigLoader::load(config);
    expect("loads numeric value", loaded.general.poll_interval_seconds == 7);
    expect("loads secret", loaded.secrets.tuya_local_key == "1234567890abcdef");
    expect("reports unknown key", loaded.warnings.size() == 1);

    const auto duplicate = directory / "duplicate.ini";
    {
        std::ofstream output(duplicate);
        output << "[general]\narmed=false\narmed=true\n";
    }
    expect("rejects duplicate key", throws([&] {
        madspm::ConfigLoader::parse_ini(duplicate);
    }));

    ::chmod(secrets.c_str(), 0644);
    expect("rejects loose secret permissions", throws([&] {
        madspm::ConfigLoader::load(config);
    }));

    madspm::Config armed = loaded;
    armed.general.armed = true;
    armed.server.host = "YOUR_SERVER_HOST";
    expect("rejects placeholders when armed", throws([&] {
        madspm::ConfigLoader::validate(armed, true);
    }));
    const auto legacy_config = directory / "legacy.ini";
    {
        std::ofstream output(legacy_config);
        output << "[general]\narmed=false\n"
               << "secrets_file=" << secrets.string() << '\n'
               << "[ups]\nnut_host=127.0.0.1\n"
                  "[proxmox]\nhost=legacy-server.example.test\n"
                  "[plug]\nhost=127.0.0.2\n";
    }
    ::chmod(secrets.c_str(), 0600);
    const auto legacy = madspm::ConfigLoader::load(legacy_config);
    expect("legacy proxmox section maps to server",
           legacy.server.host == "legacy-server.example.test");
    expect("legacy proxmox section emits migration warning",
           !legacy.warnings.empty() &&
               legacy.warnings.front().find("[proxmox]") != std::string::npos);
    madspm::Config invalid_plug = loaded;
    invalid_plug.plug.host = "неверный-адрес";
    expect("rejects non-IP plug host", throws([&] {
        madspm::ConfigLoader::validate(invalid_plug, false);
    }));

    std::filesystem::remove_all(directory);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
