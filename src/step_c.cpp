#include "dcas_policy_engine/step_c.hpp"

#include <algorithm>

namespace dcas {
namespace {

LkasMode ApplySwitchEvent(LkasMode current_mode, LkasSwitchEvent event) {
    switch (event) {
        case LkasSwitchEvent::ON:
            return current_mode == LkasMode::OFF ? LkasMode::ON_INACTIVE : current_mode;
        case LkasSwitchEvent::OFF:
            return LkasMode::OFF;
        case LkasSwitchEvent::NONE:
            return current_mode;
    }
    return current_mode;
}

double BaseThrottleGain(DriverState state) {
    switch (state) {
        case DriverState::OK: return 1.0;
        case DriverState::WARNING: return 0.6;
        case DriverState::ESCALATION: return 0.2;
        case DriverState::ABSENT: return 0.0;
    }
    return 0.0;
}

double ReasonOverlayGain(DriverState state, Reason reason) {
    if ((state == DriverState::WARNING || state == DriverState::ESCALATION) &&
        reason == Reason::DROWSY) {
        return 0.9;
    }
    return 1.0;
}

}  // namespace

StepCOutput StepCPolicyEngine::Evaluate(const StepCInput& input) const {
    StepCOutput output{};
    output.next_latched_state = input.latched_state;

    output.next_lkas_mode = ApplySwitchEvent(input.previous_lkas_mode, input.lkas_switch_event);

    if (input.lkas_switch_event == LkasSwitchEvent::ON &&
        input.driver_state == DriverState::OK &&
        input.notebook_input_alive) {
        output.next_lkas_mode = LkasMode::ON_ACTIVE;
    }

    if (output.next_lkas_mode == LkasMode::ON_ACTIVE &&
        (input.driver_state != DriverState::OK || !input.notebook_input_alive)) {
        output.next_lkas_mode = LkasMode::ON_INACTIVE;
    }

    if (input.driver_override && !output.next_latched_state.driver_override_lock) {
        output.next_lkas_mode = LkasMode::OFF;
        output.throttle_limit = 0.0;
        output.hmi_action = HmiAction::INFO;
        output.mrm_active = false;
    } else {
        output.throttle_limit = std::max(
            0.0,
            input.lkas_throttle * BaseThrottleGain(input.driver_state) *
                ReasonOverlayGain(input.driver_state, input.reason));
        switch (input.driver_state) {
            case DriverState::OK:
                output.hmi_action = HmiAction::INFO;
                break;
            case DriverState::WARNING:
                output.hmi_action = input.reengagement_confirmed_200ms ? HmiAction::INFO : HmiAction::EOR;
                break;
            case DriverState::ESCALATION:
                output.hmi_action = input.reengagement_confirmed_200ms ? HmiAction::INFO : HmiAction::DCA;
                break;
            case DriverState::ABSENT:
                output.hmi_action = HmiAction::MRM;
                output.mrm_active = true;
                output.throttle_limit = 0.0;
                ++output.next_latched_state.mrm_activation_count_run_cycle;
                break;
        }
    }

    if (output.next_latched_state.mrm_activation_count_run_cycle >= 2) {
        output.next_latched_state.driver_override_lock = true;
        output.next_lkas_mode = LkasMode::OFF;
    }

    output.dashboard_state.driver_state = input.driver_state;
    output.dashboard_state.reason = input.reason;
    output.dashboard_state.lkas_mode = output.next_lkas_mode;
    output.dashboard_state.current_manoeuvre_type = input.current_manoeuvre_type;
    return output;
}

}  // namespace dcas
