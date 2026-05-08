#include <chrono>
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
    double delta_s = 0.02;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--attentive" && i + 1 < argc) {
            const std::string value = argv[++i];
            is_attentive = (value == "1" || value == "true" || value == "yes");
        } else if (arg == "--reason" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (value == "phone") reason = dcas::Reason::PHONE;
            else if (value == "drowsy") reason = dcas::Reason::DROWSY;
            else if (value == "unresponsive") reason = dcas::Reason::UNRESPONSIVE;
            else if (value == "intoxicated") reason = dcas::Reason::INTOXICATED;
            else if (value == "none") reason = dcas::Reason::NONE;
            else reason = dcas::Reason::UNKNOWN;
        } else if (arg == "--dt" && i + 1 < argc) {
            delta_s = std::stod(argv[++i]);
        }
    }

    rt_ipc::RtControlShm shm;
    if (!shm.create_or_open(false)) {
        std::cerr << "Failed to open rt_control_shm" << std::endl;
        return 1;
    }

    dcas::PolicyRuntime runtime{};
    dcas::LkasMode lkas_mode = dcas::LkasMode::ON_ACTIVE;

    while (true) {
        rt_ipc::ActuatorToDcasSample speed_sample{};
        rt_ipc::LkasToDcasSample lkas_sample{};

        if (!shm.read_actuator_to_dcas(speed_sample)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        if (!shm.read_lkas_to_dcas(lkas_sample)) {
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
        tick.step_c.notebook_input_alive = true;
        tick.step_c.driver_override = false;
        tick.step_c.lkas_throttle = lkas_sample.lkas_throttle;

        const dcas::RuntimeTickOutput output = runtime.Tick(tick);
        lkas_mode = output.step_c.next_lkas_mode;

        rt_ipc::DcasToActuatorSample out{};
        out.timestamp_us = now_us();
        out.final_throttle = static_cast<float>(output.step_c.throttle_limit);
        out.is_valid = 1;
        shm.write_dcas_to_actuator(out);

        if (output.step_c.mrm_active) {
            send_mrm_event(true, static_cast<std::uint8_t>(output.step_c.next_lkas_mode));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
