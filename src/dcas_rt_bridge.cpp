#include <chrono>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include <mqueue.h>

#include "dcas_policy_engine/policy_runtime.hpp"
#include "rt_control_shm.hpp"

namespace {

constexpr const char* kMrmQueueName = "/mrm_event_queue";

std::uint64_t now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

float speed_to_jetracer_input(float speed_kmh) {
    const float max_kmh = 40.0f;
    float scaled = speed_kmh * 0.4f / max_kmh;
    if (scaled < 0.0f) {
        return 0.0f;
    }
    if (scaled > 0.4f) {
        return 0.4f;
    }
    return scaled;
}

bool parse_bool(const std::string& value) {
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

dcas::Reason parse_reason(const std::string& value) {
    if (value == "phone") return dcas::Reason::PHONE;
    if (value == "drowsy") return dcas::Reason::DROWSY;
    if (value == "unresponsive") return dcas::Reason::UNRESPONSIVE;
    if (value == "intoxicated") return dcas::Reason::INTOXICATED;
    if (value == "none") return dcas::Reason::NONE;
    return dcas::Reason::UNKNOWN;
}

dcas::LkasMode parse_lkas_mode(const std::string& value) {
    if (value == "OFF") return dcas::LkasMode::OFF;
    if (value == "ON_INACTIVE") return dcas::LkasMode::ON_INACTIVE;
    if (value == "ON_ACTIVE") return dcas::LkasMode::ON_ACTIVE;
    return dcas::LkasMode::ON_ACTIVE;
}

struct MrmEvent {
    bool mrm_request;
    std::uint8_t target_mode;
};

void send_mrm_event(bool mrm_request, std::uint8_t target_mode) {
    mqd_t mq = mq_open(kMrmQueueName, O_WRONLY | O_NONBLOCK | O_CREAT, 0666, nullptr);
    if (mq == static_cast<mqd_t>(-1)) {
        return;
    }
    MrmEvent event{mrm_request, target_mode};
    mq_send(mq, reinterpret_cast<const char*>(&event), sizeof(event), 0);
    mq_close(mq);
}

}  // namespace

int main(int argc, char** argv) {
    bool is_attentive = true;
    dcas::Reason reason = dcas::Reason::NONE;
    bool notebook_input_alive = true;
    bool driver_override = false;
    double delta_s = 0.02;
    int period_ms = 10;
    int iterations = -1;
    bool verbose = false;
    dcas::LkasMode lkas_mode = dcas::LkasMode::ON_ACTIVE;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--attentive" && i + 1 < argc) {
            is_attentive = parse_bool(argv[++i]);
        } else if (arg == "--reason" && i + 1 < argc) {
            reason = parse_reason(argv[++i]);
        } else if (arg == "--dt" && i + 1 < argc) {
            delta_s = std::stod(argv[++i]);
        } else if (arg == "--period-ms" && i + 1 < argc) {
            period_ms = std::stoi(argv[++i]);
        } else if (arg == "--iterations" && i + 1 < argc) {
            iterations = std::stoi(argv[++i]);
        } else if (arg == "--once") {
            iterations = 1;
        } else if (arg == "--notebook-alive" && i + 1 < argc) {
            notebook_input_alive = parse_bool(argv[++i]);
        } else if (arg == "--driver-override" && i + 1 < argc) {
            driver_override = parse_bool(argv[++i]);
        } else if (arg == "--lkas-mode" && i + 1 < argc) {
            lkas_mode = parse_lkas_mode(argv[++i]);
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--help") {
            std::cout
                << "Usage: dcas_rt_bridge [--attentive true|false] [--reason name] [--dt seconds]\n"
                << "                      [--period-ms n] [--iterations n|--once] [--verbose]\n"
                << "                      [--notebook-alive true|false] [--driver-override true|false]\n"
                << "                      [--lkas-mode OFF|ON_INACTIVE|ON_ACTIVE]\n";
            return 0;
        }
    }

    if (period_ms < 1) {
        period_ms = 1;
    }

    rt_ipc::RtControlShm shm;
    if (!shm.create_or_open(false) && !shm.create_or_open(true)) {
        std::cerr << "Failed to open rt_control_shm" << std::endl;
        return 1;
    }

    dcas::PolicyRuntime runtime{};

    rt_ipc::ActuatorToDcasSample speed_sample{};
    rt_ipc::LkasToDcasSample lkas_sample{};
    bool have_speed = false;
    bool have_lkas = false;

    while (iterations != 0) {
        if (shm.read_actuator_to_dcas(speed_sample)) {
            have_speed = true;
        }

        if (shm.read_lkas_to_dcas(lkas_sample)) {
            have_lkas = true;
        }

        if (!have_speed || !have_lkas) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        dcas::RuntimeTickInput tick{};
        const std::int64_t tick_ts = static_cast<std::int64_t>(now_us() / 1000);

        tick.step_b.perception.is_attentive = is_attentive;
        tick.step_b.perception.is_attentive_ts_ms = tick_ts;
        tick.step_b.perception.reason = reason;
        tick.step_b.perception.reason_ts_ms = tick_ts;
        tick.step_b.jetracer_input_0_4 = speed_to_jetracer_input(speed_sample.current_speed_kmh);
        tick.step_b.delta_s = delta_s;

        tick.step_c.previous_lkas_mode = lkas_mode;
        tick.step_c.lkas_switch_event = dcas::LkasSwitchEvent::NONE;
        tick.step_c.notebook_input_alive = notebook_input_alive;
        tick.step_c.driver_override = driver_override;
        tick.step_c.lkas_throttle = lkas_sample.lkas_throttle;

        const dcas::RuntimeTickOutput output = runtime.Tick(tick);
        lkas_mode = output.step_c.next_lkas_mode;

        rt_ipc::DcasToActuatorSample out{};
        out.timestamp_us = now_us();
        const double raw_throttle = static_cast<double>(lkas_sample.lkas_throttle);
        const double limit = output.step_c.throttle_limit;
        const double final_throttle = speed_sample.hardware_fault
                                          ? 0.0
                                          : std::clamp(raw_throttle, 0.0, limit);
        out.final_throttle = static_cast<float>(final_throttle);
        out.final_steering = lkas_sample.lkas_steering;
        out.is_valid = speed_sample.hardware_fault ? 0U : 1U;
        shm.write_dcas_to_actuator(out);

        if (verbose) {
            std::cout << "speed_kmh=" << speed_sample.current_speed_kmh
                      << " lkas_throttle=" << lkas_sample.lkas_throttle
                      << " throttle_limit=" << limit
                      << " final_throttle=" << out.final_throttle
                      << " final_steering=" << out.final_steering
                      << " state=" << dcas::to_string(output.step_b.next_state)
                      << " hmi=" << dcas::to_string(output.step_c.hmi_action)
                      << " valid=" << out.is_valid
                      << std::endl;
        }

        if (output.step_c.mrm_active) {
            send_mrm_event(true, static_cast<std::uint8_t>(output.step_c.next_lkas_mode));
        }

        if (iterations > 0) {
            --iterations;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(period_ms));
    }

    return 0;
}
