#include "dcas_policy_engine/step_b.hpp"

namespace dcas {

StepBTransitionEngine::StepBTransitionEngine(
    PerceptionAdapter adapter,
    SpeedBandEstimator speed_band_estimator,
    ThresholdScheduler threshold_scheduler)
    : adapter_(adapter),
      speed_band_estimator_(speed_band_estimator),
      threshold_scheduler_(threshold_scheduler) {}

StepBOutput StepBTransitionEngine::Evaluate(const StepBInput& input, StateTimerStore& state_store) const {
    const NormalizedSnapshot snapshot = adapter_.Normalize(input.perception);
    StepBStateStore state = state_store.Get();
    const Thresholds thresholds = threshold_scheduler_.Schedule(
        speed_band_estimator_.Estimate(input.jetracer_input_0_4));

    if (!snapshot.snapshot_valid) {
        return StepBOutput{
            state.current_state,
            Reason::NONE,
            state.absent_latched_run_cycle,
            0,
            false,
        };
    }

    if (state.absent_latched_run_cycle) {
        state.current_state = DriverState::ABSENT;
        state_store.Replace(state);
        return StepBOutput{
            DriverState::ABSENT,
            snapshot.reason,
            true,
            snapshot.input_snapshot_ts_ms,
            false,
        };
    }

    if (!snapshot.is_attentive) {
        state_store.AdvanceInattentive(input.delta_s);
    } else {
        state_store.AdvanceRecover(input.delta_s);
    }
    state = state_store.Get();

    DriverState next_state = state.current_state;

    // Critical reason is evaluated only after is_attentive=yes -> reason=none normalization.
    if (is_critical_reason(snapshot.reason)) {
        state_store.LatchAbsentRunCycle();
        return StepBOutput{
            DriverState::ABSENT,
            snapshot.reason,
            true,
            snapshot.input_snapshot_ts_ms,
            false,
        };
    }

    if (snapshot.is_attentive) {
        if (state.current_state != DriverState::OK &&
            state.recover_elapsed_s >= thresholds.t_recover_hold_s) {
            next_state = DriverState::OK;
        }
    } else if (state.inattentive_elapsed_s >= thresholds.t_absent_eff_s) {
        next_state = DriverState::ABSENT;
    } else if (state.inattentive_elapsed_s >= thresholds.t_esc_eff_s) {
        next_state = DriverState::ESCALATION;
    } else if (state.inattentive_elapsed_s >= thresholds.t_warn_eff_s) {
        next_state = DriverState::WARNING;
    }

    state.current_state = next_state;
    if (next_state == DriverState::ABSENT) {
        state.absent_latched_run_cycle = true;
    }
    state_store.Replace(state);

    return StepBOutput{
        next_state,
        snapshot.reason,
        state.absent_latched_run_cycle,
        snapshot.input_snapshot_ts_ms,
        next_state == DriverState::WARNING && snapshot.is_attentive && state.recover_elapsed_s >= 0.2,
    };
}

}  // namespace dcas
