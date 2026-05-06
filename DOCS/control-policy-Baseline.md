# Control Policy Baseline (Step C)

본 문서는 `DCAS-PolicyEngine`의 Step C(정책 결정/제어 제한) **기준선(Baseline)** 명세다.
Step B 상태(`OK -> WARNING -> ESCALATION -> ABSENT`)를 입력받아,
제어 제한·HMI·MRM 활성 여부를 일관되게 산출한다.

핵심 목표:

1. 규정 앵커(UNECE/Euro NCAP)를 참고하되, 프로젝트 기준선 정책을 일관되게 고정
2. 규정 고정값(A)과 캘리브레이션 항목(B/C) 분리

---

## 1) Step C 목적과 범위

- 목적:
  - 운전자 상태 악화 시 위험 완화(감속/경고 강화)
  - 재참여 유도(EOR/DCA 메시지 일관화)
  - Driver Unavailability 단계에서 최소위험 정지 방향으로 전환
- 범위:
  - `throttle_limit`: 상태별 제어값
  - `hmi_action`: 경고/안내 메시지 (INFO/EOR/DCA/MRM)
  - `mrm_active`: ABSENT 진입 시 MRM 감속 모드 활성 여부
  - `driver_override_lock`: MRM 상태에서 운전자 오버라이드 잠금 여부(`intoxicated`만 `true`)
- 비범위:
  - 상태 전이 타이밍 계산(= Step B)
  - 저수준 제어기(PID/MPC) 내부 파라미터 튜닝

---

## 2) 입력/출력 인터페이스

### 2.1 입력

- `driver_state`: `OK | WARNING | ESCALATION | ABSENT`
- `reason`: `phone | drowsy | unresponsive | intoxicated | none | unknown`
- `lkas_throttle`: LKAS가 요청한 원시 종방향 제어값
- `input_stale`: 센서/파싱 stale (**프로토타입 v0에서는 미사용, 예약 필드**)
- `driver_override` (optional): 운전자가 비지원 제어(조향/제동/가속)로 직접 인수했는지 여부
- `lkas_switch_event` (optional): `NONE | ON | OFF`
- `previous_lkas_mode`: `OFF | ON_INACTIVE | ON_ACTIVE` (필수, runtime/state-store 복원값)
- `notebook_input_alive` (optional): 노트북(인지 입력) 수신 유효 여부
- `reengagement_confirmed_200ms` (optional): Step B가 계산한 200ms 재참여 확인 신호(non-critical EOR/DCA 경고·대응 완화용)
- `manoeuvre_type` (optional): `NONE | CURVE_FOLLOW | LANE_CHANGE | TURN | MRM`
- `reason_context_source` (optional): `perception_notebook | step_b_bridge` (맥락 입력 출처 추적용)

### 2.1.1 맥락 입력 소비 규칙 (용어 통일)

- 인지팀은 inattentive 판단 시 노트북에서 VLM을 직접 호출하고 맥락(`reason`)을 전달한다.
- 인지 흐름상 `is_attentive` 판단이 항상 기준 신호이며, Step B는 이를 authoritative input으로 사용한다.
- `reason`은 VLM 호출 지연 때문에 현재 `is_attentive`보다 이전 상황을 설명할 수 있다.
- Step B는 최신 VLM 결과를 `latest_reason`으로 보존하고, 이번 계산에 실제 사용할 `effective_reason`을 Step C에 전달한다.
- `critical reason`(`unresponsive/intoxicated`)은 현재 `is_attentive`와 무관하게 항상 `ABSENT/MRM`으로 해석한다.
- `is_attentive_ts_ms`, `reason_ts_ms`는 Step C 판단용 입력이 아니며, Step B의 최신성 판정/로그/진단에서만 사용된다.
- Step C는 Step B가 산출한 최종 `driver_state`와 `effective_reason`만 소비한다.
- Step C는 매 주기 `effective_reason` 1개만 소비한다.
- 이전 VLM reason이 뒤늦게 들어오는 경우에도 Step B가 최신성 판정 후 채택한 값만 유효하다.
- 정책 결정 우선순위는 항상 `driver_state`가 최상위이고, 맥락은 overlay/HMI 보강 신호다.

### 2.1.2 단일 맥락 입력 규칙

- 인지팀은 한 번에 `reason` 1개만 전달한다.
- Step C는 Step B가 선택한 `effective_reason` 1개를 그대로 overlay/HMI 판단에 사용한다.
- `is_attentive=yes`라도 recover 상태가 200ms 미만이면 Step B는 non-critical `latest_reason`을 유지해 기존 경고/대응을 계속할 수 있다.
- `0.2s <= recover_elapsed < T_recover_hold`이면 Step B는 `reengagement_confirmed_200ms=true`를 전달할 수 있고, Step C는 non-critical 경고/대응 채널을 완화한다.
- `recover_elapsed >= T_recover_hold`이면 Step B는 non-critical reason을 무시하고 `OK + none`을 전달한다.
- 단, `critical reason`은 어떤 recover 구간에서도 무시하지 않는다.
- `recover_elapsed` 자체는 Step C 외부 입력 필드가 아니다.
- 대신 필요한 경우 Step B가 `reengagement_confirmed_200ms` bridge 신호를 출력하고, Step C는 그 신호만 소비한다.

설계 원칙:

- 속도/곡률 맥락은 Step B 전이 임계값에서 이미 반영되므로,
  Step C는 **상태 기반 강도 정책**으로 단순화한다(이중 반영 방지).
- 인터페이스 호환을 위해 입력 `manoeuvre_type`은 출력 `dashboard_state.current_manoeuvre_type`으로 매핑한다.

### 2.2 출력

- `throttle_limit`: 상태별 제어값 제한
- `hmi_action`: 경고/지시 메시지 (상태에 따라 결정)
- `mrm_active`: `true | false` (ABSENT 진입 시 `true`)
- `driver_override_lock`: `true | false` (`driver_state=ABSENT and reason=intoxicated`일 때 `true`)
  - `driver_override_lock=true`가 되면 프로그램(run cycle) 종료 전까지 해제하지 않는다.
- `policy_latched_state` (internal/state-store 권장):
  - `driver_override_lock_latched`
  - `mrm_activation_count_run_cycle`
  - `run_cycle_id`
- `dashboard_state` (권장):
  - `lkas_mode`: `OFF | ON_INACTIVE | ON_ACTIVE`
  - `current_manoeuvre_type`: `NONE | CURVE_FOLLOW | LANE_CHANGE | TURN | MRM`
  - `hmi_action`: INFO/EOR/DCA/MRM 경고/지시 메시지
  - `reason_context_source`: 맥락 입력 출처 추적 정보(옵션)

래치 상태 소유 원칙:

- run cycle 지속 상태(`driver_override_lock_latched`, `mrm_activation_count_run_cycle`, lockout)는 **Step C 내부 계산값**이지만,
  저장 주체는 상위 `DCAS-PolicyEngine runtime/state-store`로 둔다.
- 이유:
  - Step B와 Step C가 동일 run cycle 경계를 공유해야 함
  - 프로세스/함수 재호출 간에도 래치를 안정적으로 유지해야 함
  - 순수 함수형 정책 평가와 persistent state 관리를 분리할 수 있음
- 따라서 Step C는 래치 갱신 규칙의 owner이고, runtime/state-store는 그 값을 저장/복원하는 owner다.

추가 해석(R171규정 정합):

- `lkas_mode`는 Step C가 authoritative owner로 관리한다.
- `lkas_mode`는 아래 순서로 진행한다.
  - `OFF`: 사용자가 ON 요구를 하지 않은 상태
  - `ON_INACTIVE`: 사용자가 ON을 눌렀지만 LKAS 시작 조건이 아직 충족되지 않은 상태
  - `ON_ACTIVE`: LKAS가 실제로 제어를 수행하는 상태

LKAS 시작 조건(`OFF/ON_INACTIVE -> ON_ACTIVE` 진입 조건):

- `driver_state == OK`
- `notebook_input_alive == true` (노트북 입력 수신 가능)

사용자 조작 규칙:

- 사용자 조작은 `ON`, `OFF` 두 가지만 사용한다.
- Step C는 매 주기 `lkas_switch_event`를 소비해 `lkas_mode`를 갱신한다.
- `lkas_switch_event=ON`이고 시작 조건이 충족되면 `ON_ACTIVE`로 **즉시 전이**한다.
- `lkas_switch_event=ON`이고 시작 조건이 미충족이면 `ON_INACTIVE`에 머문다.
- `lkas_switch_event=OFF`이면 현재 상태와 무관하게 `OFF`로 전이한다.
- `lkas_switch_event=NONE`이면 기존 `lkas_mode`를 유지한다.
- 이 활성화 전이는 3초 대기 없이 즉시 제어 개입을 시작한다.
- 같은 주기에 `lkas_switch_event`와 `driver_override`가 동시에 들어오면, **`lkas_switch_event`를 우선 적용**한다.

전이 규칙(모드 계층):

- `OFF -> ON_INACTIVE`: `lkas_switch_event=ON`이고 시작 조건이 아직 미충족
- `OFF -> ON_ACTIVE`: `lkas_switch_event=ON`이고 시작 조건도 충족된 경우
- `ON_INACTIVE -> ON_ACTIVE`: ON 유지 중 시작 조건 충족 시 즉시 제어 개입 시작
- `ON_ACTIVE -> ON_INACTIVE`: ON은 유지되나 시작 조건을 다시 잃은 경우
- `ON_INACTIVE/ON_ACTIVE -> OFF`: `lkas_switch_event=OFF` 또는 시스템 종료 시
- `ON_ACTIVE -> OFF`: `driver_override=true`가 유효하고 `driver_override_lock=false`이면 즉시 제어 권한 종료

대시보드 의미 규칙:

- 현재 수행 중인 기동의 종류(커브 추종/MRM 등)는 `current_manoeuvre_type`으로 표시한다.
- 다음 예정 기동 정보는 Step C 입력 범위에 포함하지 않는다.
- 운전자 개입 요구(EOR/DCA/Unavailability)는 별도 enum이 아니라 `hmi_action`으로 전달한다.
- `lkas_mode`는 “전원 상태”가 아니라 “사용자 요청/시작 조건/제어 개입 수준”을 함께 표현하는 상태다.

---

## 3) 규정/평가 앵커 (Step C 고정 제약)

| 앵커 | Step C에 필요한 해석 | 출처 |
|---|---|---|
| 시스템 제어는 충돌 위험을 줄이되 운전자 개입 가능해야 함 | 급격/과도한 제어를 피하고, 항상 운전자 개입 가능 상태 유지 | UNECE R171 5.3.4, 5.3.6.1[^c1] |
| 경계/종료 시 보조 기능은 controllable way로 종료 | `ESCALATION/ABSENT`에서도 종료·감속 전략은 조향 안정성 유지 | UNECE R171 5.3.5.2.1[^c1] |
| EOR는 시각 + 타 모달리티, DCA는 즉시 수동 인수 명령 | `WARNING`은 EOR만 수행하고, `ESCALATION`은 DCA + 강도 증가를 수행한다(단, Step B가 `unresponsive/intoxicated`를 ABSENT로 전달하면 Step C는 즉시 MRM 수행) | UNECE R171 5.5.4.2.3.2~3[^c1] |
| 경고/상태 변화 시 운전자가 혼동하지 않도록 일관된 표시 필요 | Step C는 단일 HMI 채널에서 상태 우선순위에 따라 일관된 표시를 유지 | UNECE R171 5.5.4 계열 해석[^c1] |
| 운전자가 모드/기동 상황을 오인하지 않도록 명확한 HMI 필요 | 현재 수행 중인 기동과 시스템 상태를 명확히 표기하여 모드 혼동 최소화 | UNECE R171 5.5.1, 5.5.2, 5.5.4.1[^c1] |

해석 원칙:

- 규정은 "정확한 throttle/steer 비율"을 직접 강제하지 않는다.
- 본 문서의 규정 앵커는 설계 참고용이며, 기준선 시간/비율값은 프로젝트 정책 테이블을 우선한다.
- 단, `T_warn_eff`의 최대 허용값 `5.0s`는 규정 상한으로 유지한다.
- 따라서 비율값은 캘리브레이션 대상(B/C)이지만,
  - 에스컬레이션 구조,
  - 다중모달 경고,
  - controllability,
  - emergency 우선순위
  는 고정(A)으로 취급한다.

---

## 4) Core Action Mapping (기준선 1차값)

> 아래 수치는 JetRacer급 플랫폼의 보수 기준선이다. 차량 플랫폼별 재튜닝 전제로 사용한다.

| DriverState | throttle_limit | HMI | mrm_active |
|---|---:|---|---|
| OK | `1.00 * lkas_throttle` | 정보성 표시만 | false |
| WARNING | `<= 0.60 * lkas_throttle` | EOR (반복 음향/햅틱) | false |
| ESCALATION | `<= 0.20 * lkas_throttle` | DCA (즉시 수동 인수, 기본) | false |
| ABSENT | `0.0` | MRM + 최대 경고 | **true** |

실행 규칙:

- 상위 상태 완화 금지: `ABSENT > ESCALATION > WARNING > OK`
- ABSENT 상태에서 `throttle_limit = 0.0` 및 `mrm_active = true`
  - `throttle_limit = 0.0`은 **속도 0**이 아니라 **추가 종방향 출력 금지**를 뜻한다
  - `mrm_active = true`일 때 MRM 감속 모드(능동 제동) 즉시 활성화
  - 권장 감속도: `a_mrm_cmd <= -2.0 m/s^2` (플랫폼/브레이크 HW capability에 맞춰 캘리브레이션)
- 상태 전이 시 `throttle_limit` 목표가 급변하면, 최종 인가값은 반드시 Rate Limiter 또는 LPF를 통과
  - 권장 예: `|d(throttle_limit_cmd)/dt| <= 0.1 /s`
- Step B가 `reason in {unresponsive, intoxicated}`를 감지하면 현재 레벨과 무관하게 `driver_state=ABSENT`로 Step C에 전달한다.
- Step C는 위 입력을 받으면 즉시 `MRM`을 수행한다.
  - `reason=intoxicated`: `driver_override_lock=true` (오버라이드 잠금)
  - `reason=unresponsive`: `driver_override_lock=false` (오버라이드 허용)

규정 해석(critical 예외 경로):

- 본 정책은 R171 5.5.4.2.6의 "경고 시퀀스는 일부 단계를 건너뛰거나 동시/억제/지연 제공 가능" 조항에 따라,
  `unresponsive/intoxicated`를 safety-concept critical 예외로 분류해 단계 건너뛰기를 허용한다.

---

## 5) Reason Overlay (상태 기반 정책 위에 덧씌움)

| reason | 적용 상태 | 제어값 영향 | HMI 문구/동작 |
|---|---|---|---|
| `phone` | WARNING+ | 제어값 추가 보수화 없음(Core 유지) | "전방주시 필요(휴대폰 감지)" + 비프 주기 단축 |
| `drowsy` | WARNING+ | `throttle_limit` 추가 10% 보수화 | "졸음 경고, 즉시 주시" + 휴식 유도 문구 |
| `unresponsive` | 모든 상태(입력 시 Step B가 ABSENT로 전달) | 즉시 MRM, 오버라이드 허용 | "반응 없음, 안전 정지 절차 진행" + 수동 인수 가능 고지 |
| `intoxicated` | 모든 상태(입력 시 Step B가 ABSENT로 전달) | 즉시 MRM, 오버라이드 잠금 | "비정상 주행 감지, 시스템이 차량을 안전 정지" + 오버라이드 잠금 고지 |
| `none/unknown` | 모든 상태 | Core만 적용 | 일반 eyes-on 경고 |

Overlay 불변식:

- Overlay는 상향 보수화만 허용(완화 금지)
- `input_stale` 기반 우회 로직은 프로토타입 v0에서 적용하지 않는다(추후 fail-safe 단계에서 활성화)

---

## 6) HMI 에스컬레이션 시퀀스 (Step B 타이머와 결합)

이 섹션은 **상태 전이 사실을 운전자에게 알리고**, 단계별로 필요한 행동(주시 복귀/즉시 인수)을
명령형으로 전달하는 HMI 규칙이다.

| 단계 | Step B 상태 기준 | Step C HMI 요구 |
|---|---|---|
| Stage 0 | `OK` | 정보성 표시만 |
| Stage 1 | `WARNING` | EOR: 연속 시각 + 음향/햅틱 최소 1개 |
| Stage 2 | `ESCALATION` | **DCA + 강도 증가(반복/증폭, 맥락 맞춤 대응)** |
| Stage 3 | `ABSENT` | MRM (Minimum Risk Maneuver) + 최대 경고 |

### 6.1 `hmi_action` 표시 내용 표준 (필수)

`hmi_action`은 아래 4개 레벨로 고정한다.

- `INFO`
- `EOR`
- `DCA`
- `MRM`

| `hmi_action` | 트리거 조건 | 표시 텍스트(기본) | 표시/알림 강도 | 해제 조건 |
|---|---|---|---|---|
| `INFO` | `driver_state=OK` 일반 주행 | "시스템 정상 대기/주행" | 정보성 시각 표시만 | 상태 악화 시 상위 레벨로 즉시 승격 |
| `INFO` | `driver_override=true` 이후 시스템 OFF 상태 | **"수동 인수 확인 - 시스템 OFF, 자동 재개 없음"** | 정보성 시각 표시 + 고정 배너 권장 | 운전자 명시 `ON` 조작으로 재활성화 완료 시 |
| `EOR` | `driver_state=WARNING` | "전방 주시 필요" / "핸들을 잡아주세요" | 연속 시각 + 음향/햅틱(최소 1개) | `reengagement_confirmed_200ms=true` 시 경고 채널 완화 가능(상태 유지), Step B 복귀(`OK`) 시 완전 해제 |
| `DCA` | `driver_state=ESCALATION` 및 `reason ∉ {unresponsive, intoxicated}` | **"즉시 수동 인수"** | 명령형 최대 가시성 + 강한 반복 음향/햅틱 + 맥락 맞춤 강도 증가 | `reengagement_confirmed_200ms=true` 또는 운전자 인수 확인(`driver_override=true`) 시 non-critical 대응 완화 가능, `MRM` 승격 시 대체 |
| `MRM` | `driver_state=ABSENT` 또는 `mrm_active=true` | **"운전자 부재 - 안전 정지 중"** | 최대 경고(시각/음향/햅틱) + 진행 상태 고정 표시 | run cycle 정책상 자동 해제 금지(수동/재시동 정책 따름) |

문구 구성 규칙:

- `reason`이 있으면 부가 문구를 suffix로 추가한다. 예: `EOR + drowsy -> "전방 주시 필요 (졸음 경고)"`
- 동일 시점 다중 요청 충돌 시 우선순위는 `MRM > DCA > EOR > INFO`를 적용한다.

### 6.1.2 Context-Specific HMI Strategies (4대 핵심 상황)

| Context | 상태 진단 | 최적 전략 | HMI 구현 지침 | HMI 예시 |
|---|---|---|---|---|
| `Phone` | 정신은 멀쩡하나 시선과 인지가 다른 곳에 쏠린 시각/수동적 주의태만 | 짧은 직접 명령 + 즉시 반복 + HUD 활용 | 일반적인 경고 대신 행동을 명시하는 명령형 문구를 사용하고, 첫 경고를 놓치지 않도록 짧은 주기로 반복한다. 시각 경고는 계기판보다 전방 시야(HUD)에 띄운다. | `"휴대폰 사용 중단. 전방 주시."` (HUD 시각 경고 + 음성 동시 출력) |
| `Drowsy` | 각성 수준 저하, 판단력/반응 속도 저하, 눈을 감고 있을 확률이 높은 생리적 졸음운전 | 다중 감각 각성 유도(참신성) + 행동 전환(휴식) 유도 | 시각보다 청각과 햅틱(진동)을 우선 사용한다. 동일 패턴 반복으로 인한 습관화를 피하기 위해 무작위 경고음 또는 패턴 변화를 적용하고, 반드시 휴식 행동으로 이어지게 2단계로 안내한다. | `(무작위 패턴의 경고음 + 시트 진동) "졸음 감지. 즉시 전방 주시 후 안전한 위치에서 휴식하십시오."` |
| `Intoxicated` | 위험 인지 저하/비정상 주행으로 수동 인수 신뢰가 낮은 상태 | 즉시 MRM + 운전자 오버라이드 잠금 | Step B가 `ABSENT + intoxicated`를 전달하면 Step C는 지체 없이 MRM을 실행하고, `driver_override_lock=true`를 적용한다. | `"비정상 주행 감지. 시스템이 차량을 안전 정지합니다."` + `"수동 인수 잠금"` |
| `Unresponsive` | 무반응/의식 없음으로 즉시 최소위험정지가 필요한 상태 | 즉시 MRM + 운전자 오버라이드 허용 | Step B가 `ABSENT + unresponsive`를 전달하면 Step C는 즉시 MRM을 실행하되, 운전자 의식 회복 가능성을 고려해 `driver_override_lock=false`를 유지한다. | `"반응 없음. 안전 정지 절차를 진행합니다."` + `"수동 인수 가능"` |

정합 조건:

- 동일 시점 다중 HMI 요청 충돌은 `MRM > DCA > EOR > INFO` 우선순위로 해소한다.

---

## 7) Fail-safe 및 운영 규칙

### 7.1 stale/fault 처리 (프로토타입 v0)

- 프로토타입 v0에서는 `input_stale`를 정책 분기 조건으로 사용하지 않는다.
- stale/fault fail-safe는 v1 안전강화 단계에서 활성화한다.

### 7.2 경계/종료 controllability

- 시스템 종료/경계 초과/오류 상황에서도 급격한 종방향 변화 금지
- 조향은 LKAS 원출력을 유지하고, DCAS는 종방향 제한 및 HMI/긴급 상태만 중재

### 7.3 LKAS 시작 전제조건 fail-safe

- `driver_state != OK` 또는 `notebook_input_alive=false`면:
  - `lkas_mode`를 `ON_ACTIVE`로 승격하지 않는다(`ON_INACTIVE` 또는 `OFF` 유지).
  - 별도 fail-safe HMI 승격은 요구하지 않는다.
  - 즉, `hmi_action`은 오직 현재 `driver_state` 기준으로만 결정한다.

### 7.3.1 수동 인수(`driver_override`) 시 비활성화 범위

- `driver_override=true`가 유효하게 들어오고 `driver_override_lock=false`이면, DCAS는 **제어 권한 경로(control-authority path)** 를 즉시 비활성화한다.
- 단, 같은 주기에 `lkas_switch_event`가 들어오면 `lkas_switch_event` 처리 결과를 우선 반영한 뒤 `driver_override`를 평가한다.
- 비활성화 범위는 아래 3가지를 포함한다.
  - `lkas_mode=OFF`로 강등하여 LKAS/DCAS 자동 제어 재개를 금지
  - `mrm_active=false`로 내려 자동 감속/기동 명령을 중단
  - `hmi_action=INFO`로 전환하고 "수동 인수 확인 - 시스템 OFF"를 고정 표시
- 단, 아래 **감시/기록 경로(observer path)** 는 계속 유지한다.
  - `driver_override` 입력 수신
  - `driver_override_lock` / `mrm_activation_count_run_cycle` / lockout 래치 유지
  - reason/state 로그 기록
- 따라서 "DCAS 시스템 자체 비활성화"는 **자동 제어 출력 경로만 OFF** 하는 의미이며, 오버라이드 입력을 포함한 안전 감시 경로는 run cycle 종료 전까지 살아 있어야 한다.
- `driver_override_lock=true`인 경우(`ABSENT + intoxicated`)에는 위 비활성화 규칙을 적용하지 않고, MRM을 유지한다.

### 7.4 DCA 인수/운전자 부재 응답 이후 재활성화 규칙 (비-critical 경로 전용)

- 사전 규칙:
  - Step B가 `reason in {unresponsive, intoxicated}`를 감지하면 `driver_state=ABSENT`로 Step C에 전달한다.
  - Step C는 즉시 MRM을 수행한다.
    - `reason=intoxicated` -> `driver_override_lock=true`
    - `reason=unresponsive` -> `driver_override_lock=false`
  - 따라서 아래 DCA 시나리오 A/B는 `reason ∉ {unresponsive, intoxicated}`인 비-critical ESCALATION 경로에만 적용한다.

- 시나리오 A: DCA 직후 운전자가 직접 인수(`driver_override=true`)
  - DCA는 즉시 해제한다(요청 확인 완료).
  - 시스템은 즉시 `OFF`로 전환하고, 자동 제어 출력 경로를 중단한다.
  - 감시/로그/lockout 경로는 유지한다.
  - 주행 보조 재개는 반드시 운전자 명시 `ON` 조작으로만 허용한다(자동 재개 금지).

- 시나리오 B: DCA 미응답으로 ABSENT 진입 (MRM 활성화)
  - 해당 이벤트를 `mrm_activation_count_run_cycle`로 누적 관리한다.
  - 같은 run cycle에서 반복 MRM이 누적되면 활성화 잠금(lockout)을 적용한다.
  - 기본 권고: `mrm_activation_count_run_cycle > 1`이면 run cycle 종료 전까지 DCAS 재활성화 금지.
  - lockout 해제는 run cycle 재시작(차량 재시동) 이후에만 허용한다.

주의(규정 해석):

- R171 5.5.4.2.8.1은 반복/장기 이탈에 대한 활성화 차단 전략을 요구하며,
  최소 기준은 "같은 run cycle에서 MRM이 1회를 초과"하는 경우다.
- 즉, 단 1회 MRM만으로 무조건 lockout은 규정 고정값이라기보다 제조사 보수 정책 영역이다.

대시보드 송신 필드:

- `lkas_mode` (`OFF/ON_INACTIVE/ON_ACTIVE`)
- `current_manoeuvre_type` (`NONE/CURVE_FOLLOW/LANE_CHANGE/TURN/MRM`)
- `hmi_action` (상태별 경고/안내: INFO/EOR/DCA/MRM)
- `mrm_active` (`true/false`): ABSENT 상태일 때 `true`
- `driver_override_lock` (`true/false`): `driver_state=ABSENT and reason=intoxicated`일 때 `true`
  - `true`로 설정되면 프로그램(run cycle) 종료 전까지 유지한다.

---

## 8) 캘리브레이션 항목 (B/C) vs 고정 앵커 (A)

### 8.1 근거 강도 등급

- `A`: 규정 본문 또는 공식 시험 요건에 직접 명시
- `B`: 공식 프로토콜 + 연구 경향으로 방향성이 강함
- `C`: 플랫폼 제약 기반 운영 가정

### 8.2 분류표

| 항목 | 분류 | 근거 강도 | 비고 |
|---|---|---|---|
| 다중모달 EOR/DCA 요구(`unresponsive/intoxicated`의 Step B 즉시 ABSENT 전달 후 MRM 수행 포함) | 확정값 | A | UNECE R171 5.5.4.2.3.2~3[^c1] |
| controllable termination 보장 | 확정값 | A | UNECE R171 5.3.5.2.1[^c1] |
| 상태별 throttle 비율값 | 가정값 | C | JetRacer baseline, 플랫폼별 재튜닝 |
| reason overlay 보수화 | 가정값 | C | 운영 안정성 가정 |
| 상태 전이 시 Rate Limiter/LPF | 가정값(권장) | B | UNECE controllability 취지 정합[^c1] |
| ABSENT → MRM 즉시 전환 | 확정값 | A | ABSENT 상태 진입 시 `mrm_active=true` (R171 5.3.7.3) |

---

## 9) 구현 인터페이스

> 아래 코드는 참고용 C++ 스타일 예시이며, 구현 계약의 유일한 기준은 아니다.

```cpp
PolicyOutput EvaluatePolicy(
    DriverState driver_state,
    Reason effective_reason,
    float lkas_throttle,
    bool reengagement_confirmed_200ms,
    LkasMode previous_lkas_mode,
    bool notebook_input_alive = true,
    bool driver_override = false,
    bool driver_override_lock_latched = false,
    int mrm_activation_count_run_cycle = 0,
    LkasSwitchEvent lkas_switch_event = LkasSwitchEvent::NONE,
    ManoeuvreType current_manoeuvre_type = ManoeuvreType::NONE) {
  const auto [ratio, base_hmi, base_mrm_active] = GetPolicyBase(driver_state);
  auto hmi = base_hmi;
  auto mrm_active = base_mrm_active;
  auto driver_override_lock = driver_override_lock_latched;
  auto lkas_mode = previous_lkas_mode;

  const Reason resolved_reason = NormalizeReason(effective_reason);
  const float overlay_gain = GetReasonOverlayGain(resolved_reason);

  if (driver_state == DriverState::ABSENT &&
      (resolved_reason == Reason::INTOXICATED || resolved_reason == Reason::UNRESPONSIVE)) {
    hmi = HmiAction::MRM;
    mrm_active = true;
    driver_override_lock = driver_override_lock || (resolved_reason == Reason::INTOXICATED);
  }

  if ((driver_state == DriverState::WARNING || driver_state == DriverState::ESCALATION) &&
      reengagement_confirmed_200ms &&
      resolved_reason != Reason::INTOXICATED &&
      resolved_reason != Reason::UNRESPONSIVE) {
    hmi = HmiAction::INFO;  // non-critical 경고/대응 채널만 완화, 상태는 Step B가 유지
  }

  if (lkas_switch_event == LkasSwitchEvent::OFF) {
    lkas_mode = LkasMode::OFF;
  } else if (lkas_switch_event == LkasSwitchEvent::ON &&
             driver_state == DriverState::OK && notebook_input_alive) {
    lkas_mode = LkasMode::ON_ACTIVE;
  } else if (lkas_switch_event == LkasSwitchEvent::ON) {
    lkas_mode = LkasMode::ON_INACTIVE;
  }

  if (lkas_mode == LkasMode::ON_ACTIVE &&
      (driver_state != DriverState::OK || !notebook_input_alive)) {
    lkas_mode = LkasMode::ON_INACTIVE;
  }

  if (driver_override && !driver_override_lock) {
    lkas_mode = LkasMode::OFF;
    hmi = HmiAction::INFO;
    mrm_active = false;
  }

  float throttle_limit = ratio * overlay_gain * lkas_throttle;
  if (lkas_mode == LkasMode::OFF) {
    throttle_limit = 0.0f;
  }

  return BuildPolicyOutput(
      throttle_limit,
      hmi,
      mrm_active,
      driver_override_lock,
      lkas_mode,
      current_manoeuvre_type,
      resolved_reason,
      mrm_activation_count_run_cycle);
}
```

권장 단위 테스트:

- **상태 단조성**: 상위 상태(`ABSENT`)에서 `throttle_limit`이 하위 상태보다 작거나 같음
- **MRM 활성**: `driver_state=ABSENT`일 때 `mrm_active=true`
- **오버라이드 잠금 분기**: `driver_state=ABSENT`에서 `reason=intoxicated`면 `driver_override_lock=true`, `reason=unresponsive`면 `false`
- **오버라이드 잠금 유지**: `driver_override_lock=true`가 되면 프로그램(run cycle) 종료 전까지 유지되는지 확인
- **Step B->Step C 계약(critical-1)**: `ABSENT + intoxicated` 입력 시 즉시 `hmi_action=MRM`, `mrm_active=true`, `driver_override_lock=true`
- **Step B->Step C 계약(critical-2)**: `ABSENT + unresponsive` 입력 시 즉시 `hmi_action=MRM`, `mrm_active=true`, `driver_override_lock=false`
- **수동 인수 OFF 전환**: `driver_override=true and driver_override_lock=false`면 `lkas_mode=OFF`, `hmi_action=INFO`, `mrm_active=false`로 전환되는지 확인
- **잠금 상태 오버라이드 무시**: `driver_override=true and driver_override_lock=true`면 `MRM`이 유지되고 `OFF`로 전환되지 않는지 확인
- **입력 검증/폴백**: `reason`이 부재/비정상이면 `unknown`으로 정규화되는지 확인
- **reason 처리**: `reason=intoxicated`에서 Overlay 보수화 규칙이 적용되는지 확인
- **effective reason 반영**: Step B가 비동기 VLM 입력 중 선택한 `effective_reason` 1개만 정책 계산에 사용되는지 확인
- **비율 적용**: 각 상태별 비율값이 정확히 적용되는지 확인

---

## 10) 참고문헌

[^c1]: UNECE, *UN Regulation No. 171 (E/ECE/TRANS/505/Rev.3/Add.170)*, `DOCS/R171e.pdf` (5.3.4, 5.3.5.2.1, 5.3.6.1, 5.5.4.2.2.2, 5.5.4.2.3.2~3).
[^c2]: Euro NCAP, *Safe Driving Driver Engagement Protocol v1.1* (2025), section 1.4.1~1.4.3.3. https://cdn.euroncap.com/cars/assets/euro_ncap_protocol_safe_driving_driver_engagement_v11_a30e874152.pdf
[^c3]: Peng et al., *Driver’s lane keeping ability with eyes off road: Insights from a naturalistic study*, Accid Anal Prev, PMID: 22836114. https://pubmed.ncbi.nlm.nih.gov/22836114/
[^c4]: Liang et al., *How dangerous is looking away from the road?*, Human Factors, PMID: 23397818. https://pubmed.ncbi.nlm.nih.gov/23397818/
