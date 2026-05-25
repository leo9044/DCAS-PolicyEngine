#pragma once

#include <cstdint>
#include <string>

namespace dcas {

enum class DriverState {
    OK,
    WARNING,
    ESCALATION,
    ABSENT,
};

enum class Reason {
    NONE,
    PHONE,
    DROWSY,
    UNRESPONSIVE,
    INTOXICATED,
    UNKNOWN,
    BLOCKED_CAMERA,
};

enum class HmiAction {
    INFO,
    EOR,
    DCA,
    MRM,
};

enum class LkasMode {
    OFF,
    ON_INACTIVE,
    ON_ACTIVE,
};

enum class LkasSwitchEvent {
    NONE,
    ON,
    OFF,
};

enum class ManoeuvreType {
    NONE,
    CURVE_FOLLOW,
    LANE_CHANGE,
    TURN,
    MRM,
};

enum class SpeedBand {
    LOW,
    MID,
    HIGH,
};

struct PerceptionInput {
    bool is_attentive{true};
    std::int64_t is_attentive_ts_ms{0};
    Reason reason{Reason::NONE};
    std::int64_t reason_ts_ms{0};
    bool camera_blocked{false};
    std::int64_t camera_blocked_ts_ms{0};
};

struct NormalizedSnapshot {
    bool snapshot_valid{false};
    bool is_attentive{true};
    Reason reason{Reason::NONE};
    std::int64_t input_snapshot_ts_ms{0};
};

struct Thresholds {
    double t_warn_eff_s{3.0};
    double t_esc_eff_s{6.0};
    double t_absent_eff_s{10.0};
    double t_recover_hold_s{3.0};
};

struct StepBStateStore {
    DriverState current_state{DriverState::OK};
    double inattentive_elapsed_s{0.0};
    double recover_elapsed_s{0.0};
    bool absent_latched_run_cycle{false};
    Reason latest_reason{Reason::NONE};
    std::int64_t latest_reason_ts_ms{0};
};

struct DashboardState {
    DriverState driver_state{DriverState::OK};
    Reason reason{Reason::NONE};
    LkasMode lkas_mode{LkasMode::OFF};
    ManoeuvreType current_manoeuvre_type{ManoeuvreType::NONE};
};

struct StepCLatchedState {
    bool driver_override_lock{false};
    int mrm_activation_count_run_cycle{0};
    std::uint64_t run_cycle_id{0};
};

inline bool is_critical_reason(Reason reason) {
    return reason == Reason::UNRESPONSIVE || reason == Reason::INTOXICATED || reason == Reason::BLOCKED_CAMERA;
}

inline const char* to_string(DriverState value) {
    switch (value) {
        case DriverState::OK: return "OK";
        case DriverState::WARNING: return "WARNING";
        case DriverState::ESCALATION: return "ESCALATION";
        case DriverState::ABSENT: return "ABSENT";
    }
    return "UNKNOWN_STATE";
}

inline const char* to_string(Reason value) {
    switch (value) {
        case Reason::NONE: return "none";
        case Reason::PHONE: return "phone";
        case Reason::DROWSY: return "drowsy";
        case Reason::UNRESPONSIVE: return "unresponsive";
        case Reason::INTOXICATED: return "intoxicated";
        case Reason::UNKNOWN: return "unknown";
        case Reason::BLOCKED_CAMERA: return "blocked_camera";
    }
    return "unknown";
}

inline const char* to_string(HmiAction value) {
    switch (value) {
        case HmiAction::INFO: return "INFO";
        case HmiAction::EOR: return "EOR";
        case HmiAction::DCA: return "DCA";
        case HmiAction::MRM: return "MRM";
    }
    return "INFO";
}

inline const char* to_string(LkasMode value) {
    switch (value) {
        case LkasMode::OFF: return "OFF";
        case LkasMode::ON_INACTIVE: return "ON_INACTIVE";
        case LkasMode::ON_ACTIVE: return "ON_ACTIVE";
    }
    return "OFF";
}

}  // namespace dcas
