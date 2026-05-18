# DCAS PolicyEngine C++ Skeleton (2026-04-23)

본 문서는 `IMPLEMENTATION_QA_2026-04-23.md`의 5번 항목을 기준으로,
현재 `DOCS/state-machine-Baseline.md`, `DOCS/control-policy-Baseline.md`를 구현하기 위한
유지보수형 C++ 뼈대 구조를 정리한 초안이다.

목표:

- 인지팀 입력 계약을 단순화
- Step B 내부 계산과 Step C 정책 결정을 분리
- run-cycle 래치 상태를 runtime/state-store로 일원화
- 프로토타입 v0 구현 시 바로 옮길 수 있는 타입/함수 뼈대 제공

---

## 1) 권장 디렉터리 구조

```text
DCAS-PolicyEngine/
├─ include/dcas_policy/
│  ├─ types.hpp
│  ├─ perception_adapter.hpp
│  ├─ state_timer_store.hpp
│  ├─ speed_band_estimator.hpp
│  ├─ threshold_scheduler.hpp
│  ├─ step_b_transition_engine.hpp
│  ├─ step_c_policy_engine.hpp
│  └─ policy_runtime.hpp
├─ src/
│  ├─ types.cpp
│  ├─ perception_adapter.cpp
│  ├─ state_timer_store.cpp
│  ├─ speed_band_estimator.cpp
│  ├─ threshold_scheduler.cpp
│  ├─ step_b_transition_engine.cpp
│  ├─ step_c_policy_engine.cpp
│  └─ policy_runtime.cpp
├─ tests/
│  ├─ test_perception_adapter.cpp
│  ├─ test_step_b_transition.cpp
│  ├─ test_step_c_policy.cpp
│  └─ test_policy_runtime.cpp
└─ CMakeLists.txt
```

---

## 2) 핵심 설계 원칙

1. 인지팀 입력은 `is_attentive`, `reason`, 각 timestamp만 받는다.
2. `is_attentive_ts_ms == reason_ts_ms`인 동일 snapshot만 유효하다.
3. `is_attentive`가 authoritative input이다.
4. `is_attentive=yes`이면 `reason=NONE`으로 정규화한다.
5. Step B는 상태/타이머/전이를 계산한다.
6. Step C는 `StepBOutput`을 기반으로 throttle/HMI/MRM/LKAS mode를 계산한다.
7. run-cycle 래치 상태는 runtime/state-store가 저장하고, Step B/Step C는 갱신 규칙만 가진다.

---

## 3) 공통 타입 뼈대

```cpp
// include/dcas_policy/types.hpp
#pragma once

#include <cstdint>
#include <optional>
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
  UNKNOWN,
  PHONE,
  DROWSY,
  UNRESPONSIVE,
  INTOXICATED,
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

enum class ReasonContextSource {
  PERCEPTION_NOTEBOOK,
  STEP_B_BRIDGE,
};

enum class SpeedBand {
  LOW,
  MID,
  HIGH,
};

struct PerceptionInput {
  bool is_attentive;
  std::int64_t is_attentive_ts_ms;
  Reason reason;
  std::int64_t reason_ts_ms;
};

struct NormalizedSnapshot {
  bool valid_snapshot;
  bool is_attentive;
  Reason reason_used;
  std::int64_t snapshot_ts_ms;
};

struct StepBStateStore {
  DriverState current_state {DriverState::OK};
  double inattentive_elapsed_s {0.0};
  double recover_elapsed_s {0.0};
  bool absent_latched_run_cycle {false};
};

struct Thresholds {
  double t_warn_eff_s {0.0};
  double t_esc_eff_s {0.0};
  double t_absent_eff_s {0.0};
  double t_recover_hold_s {0.0};
};

struct StepBInput {
  PerceptionInput perception;
  double jetracer_input_0_4 {0.0};
  bool input_stale {false};
  std::optional<double> road_curvature;
  StepBStateStore store;
};

struct StepBOutput {
  DriverState next_state;
  Reason reason_used;
  bool absent_latched_run_cycle;
  std::int64_t input_snapshot_ts_ms;
  StepBStateStore next_store;
};

struct StepCLatchedState {
  bool driver_override_lock_latched {false};
  int mrm_activation_count_run_cycle {0};
  std::uint64_t run_cycle_id {0};
};

struct StepCInput {
  DriverState driver_state;
  Reason reason;
  float lkas_throttle;
  bool driver_override {false};
  bool notebook_input_alive {true};
  LkasSwitchEvent lkas_switch_event {LkasSwitchEvent::NONE};
  LkasMode previous_lkas_mode {LkasMode::OFF};
  ManoeuvreType manoeuvre_type {ManoeuvreType::NONE};
  ReasonContextSource reason_context_source {ReasonContextSource::STEP_B_BRIDGE};
  StepCLatchedState latched_state;
};

struct DashboardState {
  LkasMode lkas_mode;
  ManoeuvreType current_manoeuvre_type;
  HmiAction hmi_action;
  ReasonContextSource reason_context_source;
  Reason reason;
  bool driver_override_lock;
};

struct StepCOutput {
  float throttle_limit;
  HmiAction hmi_action;
  bool mrm_active;
  bool driver_override_lock;
  DashboardState dashboard_state;
  StepCLatchedState next_latched_state;
};

}  // namespace dcas
```

---

## 4) 모듈별 책임

### 4.1 `PerceptionAdapter`

책임:

- 동일 timestamp snapshot 결합
- `is_attentive` 우선 정규화
- `is_attentive=yes -> reason=NONE`
- timestamp mismatch 시 invalid snapshot 반환

```cpp
// include/dcas_policy/perception_adapter.hpp
#pragma once

#include "dcas_policy/types.hpp"

namespace dcas {

class PerceptionAdapter {
 public:
  NormalizedSnapshot Normalize(const PerceptionInput& input) const;
};

}  // namespace dcas
```

```cpp
// src/perception_adapter.cpp
#include "dcas_policy/perception_adapter.hpp"

namespace dcas {

NormalizedSnapshot PerceptionAdapter::Normalize(const PerceptionInput& input) const {
  if (input.is_attentive_ts_ms != input.reason_ts_ms) {
    return {
        .valid_snapshot = false,
        .is_attentive = input.is_attentive,
        .reason_used = Reason::UNKNOWN,
        .snapshot_ts_ms = input.is_attentive_ts_ms,
    };
  }

  if (input.is_attentive) {
    return {
        .valid_snapshot = true,
        .is_attentive = true,
        .reason_used = Reason::NONE,
        .snapshot_ts_ms = input.is_attentive_ts_ms,
    };
  }

  return {
      .valid_snapshot = true,
      .is_attentive = false,
      .reason_used = input.reason,
      .snapshot_ts_ms = input.is_attentive_ts_ms,
  };
}

}  // namespace dcas
```

### 4.2 `StateTimerStore`

책임:

- Step B 상태/타이머 저장
- run-cycle 간이 아니라 같은 run-cycle 동안 store 유지
- 호출자는 매 주기 store를 넣고, 결과 store를 다시 저장

```cpp
// include/dcas_policy/state_timer_store.hpp
#pragma once

#include "dcas_policy/types.hpp"

namespace dcas {

class StateTimerStore {
 public:
  StepBStateStore Load() const;
  void Save(const StepBStateStore& store);

 private:
  StepBStateStore store_;
};

}  // namespace dcas
```

### 4.3 `SpeedBandEstimator`

책임:

- `rho_v` 계산
- LPF, hysteresis, dwell 적용
- 최종 `SpeedBand` 산출

```cpp
// include/dcas_policy/speed_band_estimator.hpp
#pragma once

#include "dcas_policy/types.hpp"

namespace dcas {

class SpeedBandEstimator {
 public:
  SpeedBand Update(double jetracer_input_0_4, double dt_s);

 private:
  double rho_v_filtered_ {0.0};
  SpeedBand current_band_ {SpeedBand::LOW};
  double band_condition_hold_s_ {0.0};
};

}  // namespace dcas
```

### 4.4 `ThresholdScheduler`

책임:

- `speed_band -> T_warn/T_esc/T_absent/T_recover_hold`
- `T_warn_eff <= 5.0s` clamp 유지

```cpp
// include/dcas_policy/threshold_scheduler.hpp
#pragma once

#include "dcas_policy/types.hpp"

namespace dcas {

class ThresholdScheduler {
 public:
  Thresholds Compute(SpeedBand band) const;
};

}  // namespace dcas
```

### 4.5 `StepBTransitionEngine`

책임:

- snapshot/타이머/threshold 기반 상태 전이
- critical reason 즉시 ABSENT
- ABSENT latch 유지
- recover hold 처리
- stale(v0에서는 reserved)

```cpp
// include/dcas_policy/step_b_transition_engine.hpp
#pragma once

#include "dcas_policy/perception_adapter.hpp"
#include "dcas_policy/threshold_scheduler.hpp"
#include "dcas_policy/types.hpp"

namespace dcas {

class StepBTransitionEngine {
 public:
  explicit StepBTransitionEngine(
      PerceptionAdapter adapter,
      ThresholdScheduler scheduler);

  StepBOutput Evaluate(
      const StepBInput& input,
      SpeedBand speed_band,
      double dt_s) const;

 private:
  bool IsCriticalReason(Reason reason) const;

  PerceptionAdapter adapter_;
  ThresholdScheduler scheduler_;
};

}  // namespace dcas
```

핵심 전이 순서:

1. snapshot normalize
2. `current_state == ABSENT || absent_latched_run_cycle`면 ABSENT 유지
3. invalid snapshot이면 상태 유지
4. normalized reason critical이면 ABSENT + latch
5. inattentive elapsed 기반 상향 전이
6. recover hold 기반 복귀

### 4.6 `StepCPolicyEngine`

책임:

- `StepBOutput` 기반 throttle/HMI/MRM/lock 계산
- `lkas_mode` owner
- `lkas_switch_event` 처리
- `driver_override` 처리
- Step C run-cycle 래치 갱신

```cpp
// include/dcas_policy/step_c_policy_engine.hpp
#pragma once

#include "dcas_policy/types.hpp"

namespace dcas {

class StepCPolicyEngine {
 public:
  StepCOutput Evaluate(const StepCInput& input) const;

 private:
  float BaseGainFromState(DriverState state) const;
  float OverlayGainFromReason(Reason reason) const;
  HmiAction BaseHmiFromState(DriverState state) const;
};

}  // namespace dcas
```

Step C 평가 순서 권장:

1. `driver_state` 기반 기본 gain/HMI/MRM 결정
2. reason overlay 적용
3. ABSENT + critical reason이면 MRM/lock 보정
4. `lkas_switch_event` 반영
5. `notebook_input_alive`, `driver_state` 기반 `lkas_mode` fail-safe
6. `driver_override` 반영
7. `throttle_limit` 계산 후 `lkas_mode == OFF`면 `0.0`
8. `next_latched_state` 갱신

### 4.7 `PolicyRuntime`

책임:

- Step B / Step C orchestration
- state-store load/save
- run-cycle 경계 관리

```cpp
// include/dcas_policy/policy_runtime.hpp
#pragma once

#include "dcas_policy/speed_band_estimator.hpp"
#include "dcas_policy/state_timer_store.hpp"
#include "dcas_policy/step_b_transition_engine.hpp"
#include "dcas_policy/step_c_policy_engine.hpp"

namespace dcas {

class PolicyRuntime {
 public:
  struct Input {
    PerceptionInput perception;
    double jetracer_input_0_4 {0.0};
    double dt_s {0.05};
    bool input_stale {false};

    bool driver_override {false};
    bool notebook_input_alive {true};
    LkasSwitchEvent lkas_switch_event {LkasSwitchEvent::NONE};
    float lkas_throttle {0.0f};
    ManoeuvreType manoeuvre_type {ManoeuvreType::NONE};
  };

  struct Output {
    StepBOutput step_b;
    StepCOutput step_c;
  };

  Output Tick(const Input& input);

 private:
  StateTimerStore step_b_store_;
  SpeedBandEstimator speed_band_estimator_;
  StepCLatchedState step_c_latched_state_;

  PerceptionAdapter perception_adapter_;
  ThresholdScheduler threshold_scheduler_;
  StepBTransitionEngine step_b_engine_ {perception_adapter_, threshold_scheduler_};
  StepCPolicyEngine step_c_engine_;
  LkasMode lkas_mode_ {LkasMode::OFF};
};

}  // namespace dcas
```

---

## 5) 런타임 호출 흐름

```text
PerceptionInput
  -> PerceptionAdapter.Normalize()
  -> SpeedBandEstimator.Update()
  -> ThresholdScheduler.Compute()
  -> StepBTransitionEngine.Evaluate()
  -> StepCPolicyEngine.Evaluate()
  -> runtime/state-store save
```

권장 순서:

1. Perception normalize
2. Speed band update
3. Threshold compute
4. Step B evaluate + Step B state save
5. Step C evaluate + Step C latched state save
6. 최종 output publish

---

## 6) 최소 테스트 세트

### 6.1 `PerceptionAdapter`

- 동일 timestamp + `is_attentive=yes` -> `reason=NONE`
- 동일 timestamp + `is_attentive=no` + `reason=DROWSY` -> DROWSY 유지
- timestamp mismatch -> invalid snapshot

### 6.2 `StepBTransitionEngine`

- critical reason `UNRESPONSIVE` -> 즉시 ABSENT
- critical reason `INTOXICATED` -> 즉시 ABSENT
- `T_warn_eff` 초과 -> WARNING
- `T_esc_eff` 초과 -> ESCALATION
- `T_absent_eff` 초과 -> ABSENT
- `recover_elapsed >= 0.2 && < T_recover_hold` -> 상태 유지
- `recover_elapsed >= T_recover_hold` -> OK
- ABSENT latch on -> 복귀 차단

### 6.3 `StepCPolicyEngine`

- `OK/WARNING/ESCALATION/ABSENT`별 base gain 검증
- `DROWSY` overlay 0.9 검증
- `ABSENT + INTOXICATED` -> lock=true, MRM=true
- `ABSENT + UNRESPONSIVE` -> lock=false, MRM=true
- `driver_override=true && !lock` -> OFF, INFO, mrm=false
- `lkas_switch_event=ON/OFF` mode 전이 검증

### 6.4 `PolicyRuntime`

- 동일 snapshot 흐름 end-to-end
- timestamp mismatch 시 상태 유지
- critical reason 후 same run cycle 복귀 차단
- `lkas_switch_event` + `driver_override` 동시 주기 처리

---

## 7) 구현 우선순위

1. `types.hpp`
2. `PerceptionAdapter`
3. `ThresholdScheduler`
4. `StepBTransitionEngine`
5. `StepCPolicyEngine`
6. `PolicyRuntime`
7. 단위 테스트

---

## 8) 메모

- 본 문서는 구현 뼈대 초안이며, 실제 코드에서는 `namespace`, 파일명, enum naming convention은 팀 스타일에 맞게 조정 가능
- 프로토타입 v0에서는 stale fail-safe를 비활성으로 두고, 관련 입력/분기는 reserved로 유지
- `transition_reason`는 인터페이스에서 제외하고 내부 로그로만 남기는 방향을 권장
