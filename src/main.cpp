#include <cstdlib>
#include <iostream>
#include <string>

#include "dcas_policy_engine/policy_runtime.hpp"

namespace {

bool ParseBool(const std::string& value) {
    return value == "1" || value == "true" || value == "yes" || value == "y";
}

dcas::Reason ParseReason(const std::string& value) {
    if (value == "phone") return dcas::Reason::PHONE;
    if (value == "drowsy") return dcas::Reason::DROWSY;
    if (value == "unresponsive") return dcas::Reason::UNRESPONSIVE;
    if (value == "intoxicated") return dcas::Reason::INTOXICATED;
    if (value == "none") return dcas::Reason::NONE;
    return dcas::Reason::UNKNOWN;
}

dcas::LkasMode ParseLkasMode(const std::string& value) {
    if (value == "ON_INACTIVE" || value == "on_inactive") return dcas::LkasMode::ON_INACTIVE;
    if (value == "ON_ACTIVE" || value == "on_active") return dcas::LkasMode::ON_ACTIVE;
    return dcas::LkasMode::OFF;
}

dcas::LkasSwitchEvent ParseSwitchEvent(const std::string& value) {
    if (value == "ON" || value == "on") return dcas::LkasSwitchEvent::ON;
    if (value == "OFF" || value == "off") return dcas::LkasSwitchEvent::OFF;
    return dcas::LkasSwitchEvent::NONE;
}

void PrintUsage(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " [options]\n"
        << "\n"
        << "Mock input options:\n"
        << "  --attentive true|false       Driver attention signal (default: false)\n"
        << "  --reason NAME                none|phone|drowsy|unresponsive|intoxicated|unknown (default: drowsy)\n"
        << "  --ts MS                      Same timestamp for is_attentive/reason (default: 1000)\n"
        << "  --reason-ts MS               Override reason timestamp to test mismatch\n"
        << "  --dt SEC                     Tick delta seconds (default: 2.5)\n"
        << "  --ticks N                    Repeat the same mock tick N times (default: 1)\n"
        << "  --jetracer VALUE             JetRacer input 0.0..0.4 (default: 0.2)\n"
        << "  --lkas-throttle VALUE        Raw LKAS throttle request (default: 0.5)\n"
        << "  --lkas-mode MODE             OFF|ON_INACTIVE|ON_ACTIVE (default: ON_ACTIVE)\n"
        << "  --switch EVENT               NONE|ON|OFF (default: NONE)\n"
        << "  --override true|false        Driver override input (default: false)\n"
        << "  --notebook-alive true|false  Perception notebook alive (default: true)\n"
        << "  --camera-blocked true|false  Camera blocked signal (default: false)\n"
        << "  --camera-blocked-ts MS       Timestamp when blockage was first detected (default: 0)\n"
        << "  --help                       Show this help\n";
}

}  // namespace

int main(int argc, char** argv) {
    bool is_attentive = false;
    dcas::Reason reason = dcas::Reason::DROWSY;
    std::int64_t is_attentive_ts_ms = 1000;
    std::int64_t reason_ts_ms = 1000;
    bool reason_ts_overridden = false;
    double delta_s = 2.5;
    int ticks = 1;
    double jetracer_input_0_4 = 0.2;
    double lkas_throttle = 0.5;
    dcas::LkasMode lkas_mode = dcas::LkasMode::ON_ACTIVE;
    dcas::LkasSwitchEvent switch_event = dcas::LkasSwitchEvent::NONE;
    bool driver_override = false;
    bool notebook_alive = true;
    bool camera_blocked = false;
    std::int64_t camera_blocked_ts_ms = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto require_value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        } else if (arg == "--attentive") {
            is_attentive = ParseBool(require_value("--attentive"));
        } else if (arg == "--reason") {
            reason = ParseReason(require_value("--reason"));
        } else if (arg == "--ts") {
            is_attentive_ts_ms = std::stoll(require_value("--ts"));
        } else if (arg == "--reason-ts") {
            reason_ts_ms = std::stoll(require_value("--reason-ts"));
            reason_ts_overridden = true;
        } else if (arg == "--dt") {
            delta_s = std::stod(require_value("--dt"));
        } else if (arg == "--ticks") {
            ticks = std::stoi(require_value("--ticks"));
        } else if (arg == "--jetracer") {
            jetracer_input_0_4 = std::stod(require_value("--jetracer"));
        } else if (arg == "--lkas-throttle") {
            lkas_throttle = std::stod(require_value("--lkas-throttle"));
        } else if (arg == "--lkas-mode") {
            lkas_mode = ParseLkasMode(require_value("--lkas-mode"));
        } else if (arg == "--switch") {
            switch_event = ParseSwitchEvent(require_value("--switch"));
        } else if (arg == "--override") {
            driver_override = ParseBool(require_value("--override"));
        } else if (arg == "--notebook-alive") {
            notebook_alive = ParseBool(require_value("--notebook-alive"));
        } else if (arg == "--camera-blocked") {
            camera_blocked = ParseBool(require_value("--camera-blocked"));
        } else if (arg == "--camera-blocked-ts") {
            camera_blocked_ts_ms = std::stoll(require_value("--camera-blocked-ts"));
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage(argv[0]);
            return 2;
        }
    }

    if (!reason_ts_overridden) {
        reason_ts_ms = is_attentive_ts_ms;
    }

    dcas::PolicyRuntime runtime{};

    for (int i = 0; i < ticks; ++i) {
        dcas::RuntimeTickInput tick{};
        const std::int64_t tick_ts = is_attentive_ts_ms + i;
        tick.step_b.perception.is_attentive = is_attentive;
        tick.step_b.perception.is_attentive_ts_ms = tick_ts;
        tick.step_b.perception.reason = reason;
        tick.step_b.perception.reason_ts_ms = reason_ts_overridden ? reason_ts_ms : tick_ts;
        tick.step_b.perception.camera_blocked = camera_blocked;
        tick.step_b.perception.camera_blocked_ts_ms = camera_blocked_ts_ms;
        tick.step_b.jetracer_input_0_4 = jetracer_input_0_4;
        tick.step_b.delta_s = delta_s;

        tick.step_c.previous_lkas_mode = lkas_mode;
        tick.step_c.lkas_switch_event = switch_event;
        tick.step_c.notebook_input_alive = notebook_alive;
        tick.step_c.driver_override = driver_override;
        tick.step_c.lkas_throttle = lkas_throttle;

        const dcas::RuntimeTickOutput output = runtime.Tick(tick);
        lkas_mode = output.step_c.next_lkas_mode;
        switch_event = dcas::LkasSwitchEvent::NONE;

        std::cout
            << "{"
            << "\"tick\":" << i
            << ",\"step_b_next_state\":\"" << dcas::to_string(output.step_b.next_state) << "\""
            << ",\"reason\":\"" << dcas::to_string(output.step_b.reason) << "\""
            << ",\"absent_latched\":" << (output.step_b.absent_latched_run_cycle ? "true" : "false")
            << ",\"reengagement_confirmed_200ms\":"
            << (output.step_b.reengagement_confirmed_200ms ? "true" : "false")
            << ",\"hmi_action\":\"" << dcas::to_string(output.step_c.hmi_action) << "\""
            << ",\"next_lkas_mode\":\"" << dcas::to_string(output.step_c.next_lkas_mode) << "\""
            << ",\"throttle_limit\":" << output.step_c.throttle_limit
            << ",\"mrm_active\":" << (output.step_c.mrm_active ? "true" : "false")
            << ",\"driver_override_lock\":"
            << (output.step_c.next_latched_state.driver_override_lock ? "true" : "false")
            << "}\n";
    }

    return 0;
}
