#include "madspm/nut_client.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
int failures = 0;
void expect(const char* name, bool condition) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}
} // namespace

int main() {
    const std::string response =
        "BEGIN LIST VAR ups\r\n"
        "VAR ups ups.status \"OB LB\"\r\n"
        "VAR ups battery.charge \"12.5\"\r\n"
        "VAR ups battery.runtime \"540\"\r\n"
        "VAR ups device.note \"quoted \\\"value\\\"\"\r\n"
        "END LIST VAR ups\r\n";
    const auto values =
        madspm::NutClient::parse_variables_response(response, "ups");
    expect("parses variables", values.size() == 4);
    expect("unquotes escaped value", values.at("device.note") == "quoted \"value\"");
    const auto telemetry = madspm::NutClient::telemetry_from_variables(values);
    expect("detects on battery", telemetry.on_battery && !telemetry.online);
    expect("detects low battery", telemetry.low_battery);
    expect("parses charge", telemetry.battery_charge &&
                            *telemetry.battery_charge == 12.5);
    bool error_seen = false;
    try {
        madspm::NutClient::parse_variables_response("ERR UNKNOWN-UPS\n", "ups");
    } catch (const std::runtime_error&) {
        error_seen = true;
    }
    expect("propagates NUT error", error_seen);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
