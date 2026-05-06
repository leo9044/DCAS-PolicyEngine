# DCAS PolicyEngine Test Scenarios

본 문서는 `state-machine-Baseline.md`(Step B)와 `control-policy-Baseline.md`(Step C)를 기준으로,
현재 C++ 구현이 정상 작동하는지 확인하기 위한 테스트 시나리오 목록이다.

**테스트 현황:**
- ✅ C++ 단위 테스트: 20개 시나리오 모두 통과 (모든 elapsed time 누적 추적)
- ✅ Python subprocess 테스트: 13개 주요 시나리오 통과 (상태머신 상태 검증)

대상 구현:

- C++ 단위 테스트: `tests/test_policy.cpp` (20 comprehensive scenarios)
- C++ mock runner: `build/dcas_policy_runner`
- Python subprocess mock: `/home/leo/Vehicle-jetracer/src/test_dcas_policy.py` (13 subprocess scenarios)

---

## 1) 공통 실행 절차

### 1.1 C++ 빌드

```bash
cd /home/leo/ads-skynet/DCAS-PolicyEngine
cmake -S . -B build
cmake --build build
```

기대 결과:

- `build/dcas_policy_runner` 생성
- `build/dcas_policy_tests` 생성
- 빌드 오류 없음

### 1.2 C++ 단위 테스트

```bash
ctest --test-dir build --output-on-failure
```

기대 결과:

- `dcas_policy_tests` 통과
- `100% tests passed`

### 1.3 C++ mock runner 직접 실행

```bash
./build/dcas_policy_runner --help
```

기대 결과:

- `--attentive`
- `--reason`
- `--ts`
- `--reason-ts`
- `--dt`
- `--ticks`
- `--jetracer`
- `--lkas-throttle`
- `--lkas-mode`
- `--switch`
- `--override`
- `--notebook-alive`

위 옵션 설명이 출력되어야 한다.

### 1.4 Python subprocess mock 실행

```bash
cd /home/leo/Vehicle-jetracer
python3 src/test_dcas_policy.py
```

기대 결과:

- 모든 mock case 출력
- 마지막 줄에 `[PASS] DCAS subprocess mock checks passed`

---

## 2) Step B 비동기 reason 입력 시나리오

### S-B-001: `reason_ts_ms`가 달라도 최신 VLM reason이면 유효

목적:

- VLM 호출 지연으로 `reason_ts_ms`가 `is_attentive_ts_ms`와 달라도, Step B가 지금까지 받은 최신 reason이면 채택하는지 확인한다.

입력:

| 필드 | 값 |
|---|---|
| `is_attentive` | `false` |
| `is_attentive_ts_ms` | `1000` |
| `reason` | `unresponsive` |
| `reason_ts_ms` | `1001` |
| `delta_s` | `1.0` |

실행 예:

```bash
./build/dcas_policy_runner --attentive false --reason unresponsive --ts 1000 --reason-ts 1001 --dt 1.0
```

기대 결과:

| 출력 | 기대값 |
|---|---|
| `step_b_next_state` | `ABSENT` |
| `reason` | `unresponsive` |
| `absent_latched` | `true` |
| `hmi_action` | `MRM` |
| `mrm_active` | `true` |

자동화 위치:

- 신규 필요: `TestAsyncReasonTimestampIsAccepted`
- 신규 필요: `async_reason_timestamp_accepted`

### S-B-002: `is_attentive=true`여도 critical reason은 즉시 최고 수준 대응

목적:

- 현재 `is_attentive=yes`라도 뒤늦게 도착한 critical VLM reason이면 `ABSENT/MRM`으로 즉시 상승하는지 확인한다.

입력:

| 필드 | 값 |
|---|---|
| `is_attentive` | `true` |
| `reason` | `intoxicated` |
| `is_attentive_ts_ms` | `1000` |
| `reason_ts_ms` | `1000` |
| `delta_s` | `1.0` |

실행 예:

```bash
./build/dcas_policy_runner --attentive true --reason intoxicated --dt 1.0
```

기대 결과:

| 출력 | 기대값 |
|---|---|
| `step_b_next_state` | `ABSENT` |
| `reason` | `intoxicated` |
| `absent_latched` | `true` |
| `reengagement_confirmed_200ms` | `false` |
| `hmi_action` | `MRM` |
| `mrm_active` | `true` |

자동화 위치:

- 신규 필요: `TestAttentiveCriticalReasonForcesAbsent`
- 신규 필요: `attentive_with_critical_reason_forces_absent`

### S-B-003: `is_attentive=no`, VLM reason 아직 없음

목적:

- VLM 결과가 아직 없더라도 `inattentive_elapsed` 기반 상태전이가 정상 진행되는지 확인한다.
- `ESCALATION` 이상 진입 시점까지 reason이 없으면 일반 `none` 대응이 적용되어야 한다.
- `unknown`은 "결과 없음"이 아니라 "VLM 결과는 왔지만 분류 불명확"인 non-critical reason으로 별도 취급한다.

입력:

| 필드 | 값 |
|---|---|
| `is_attentive` | `false` |
| `reason` | `none` |
| `jetracer_input_0_4` | `0.2` |
| `delta_s` | `1.0` |
| `ticks` | `4` |

기대 결과:

| 출력 | 기대값 |
|---|---|
| `step_b_next_state` | `ESCALATION` |
| `reason` | `none` |
| `hmi_action` | `DCA` |
| `mrm_active` | `false` |

자동화 위치:

- 신규 필요: `TestNoReasonStillEscalatesByTimer`
- 신규 필요: `no_reason_timer_escalation`

### S-B-004: `is_attentive=no`, 이전 VLM reason 도착

목적:

- 현재 no 상태에서 뒤늦게 도착한 non-critical reason이 최신 수신 reason이면, `ESCALATION` 이상에서 그 reason 기반 대응을 사용하는지 확인한다.

입력 흐름:

| 순서 | `is_attentive` | `reason` | `delta_s` | 기대 |
|---:|---|---|---:|---|
| 1 | `false` | `none` | `2.0` | `WARNING`, 일반 EOR |
| 2 | `false` | `phone` | `2.0` | `ESCALATION`, `reason=phone`, DCA |

기대 결과:

| 출력 | 기대값 |
|---|---|
| `step_b_next_state` | `ESCALATION` |
| `reason` | `phone` |
| `hmi_action` | `DCA` |

자동화 위치:

- 신규 필요: `TestLatestAsyncNonCriticalReasonUsedAtEscalation`

### S-B-005: `is_attentive=yes`, 이전 non-critical reason 도착 후 recover 구간 분기

목적:

- 현재는 yes지만 이전 no 상태에 대한 non-critical VLM reason이 늦게 들어온 경우, recover 시간에 따라 대응이 달라지는지 확인한다.

입력 흐름:

| 조건 | 기대 |
|---|---|
| `recover_elapsed < 0.2s` | 상태 유지, 최신 reason 기반 경고/대응 유지 |
| `0.2s <= recover_elapsed < T_recover_hold(3.0s)` | 상태 유지, 경고/대응 채널 완화 |
| `recover_elapsed >= T_recover_hold(3.0s)` | non-critical reason 무시, `OK + none` 복귀 |

자동화 위치:

- 신규 필요: `TestAsyncNonCriticalReasonDuringRecoverWindow`

---

## 3) Step B 상태 전이 시나리오

### S-B-101: MID band 비critical 부주의 연속 입력, `OK -> WARNING`

목적:

- MID band에서 `T_warn=2.0s` 기준으로 WARNING에 진입하는지 확인한다.

입력:

| 필드 | 값 |
|---|---|
| `is_attentive` | `false` |
| `reason` | `phone` 또는 `drowsy` |
| `jetracer_input_0_4` | `0.2` |
| `delta_s` | `2.0` 이상 |

실행 예:

```bash
./build/dcas_policy_runner --attentive false --reason drowsy --dt 2.5 --jetracer 0.2 --lkas-throttle 0.5 --lkas-mode ON_ACTIVE
```

기대 결과:

| 출력 | 기대값 |
|---|---|
| `step_b_next_state` | `WARNING` |
| `reason` | 입력 reason 유지 |
| `hmi_action` | `EOR` |
| `mrm_active` | `false` |

자동화 위치:

- `TestContinuousNonCriticalProgressionMidBand`
- `warning_drowsy`

### S-B-102: MID band 비critical 부주의 연속 입력, `WARNING -> ESCALATION`

목적:

- MID band에서 누적 inattentive 시간이 `T_esc=4.0s`에 도달하면 ESCALATION으로 상승하는지 확인한다.

입력:

| 필드 | 값 |
|---|---|
| `is_attentive` | `false` |
| `reason` | `phone` |
| `jetracer_input_0_4` | `0.2` |
| `delta_s` | `1.0` |
| `ticks` | `4` |

실행 예:

```bash
./build/dcas_policy_runner --attentive false --reason phone --dt 1.0 --ticks 4 --jetracer 0.2 --lkas-throttle 0.5 --lkas-mode ON_ACTIVE
```

기대 결과:

| 출력 | 기대값 |
|---|---|
| 마지막 `tick` | `3` |
| `step_b_next_state` | `ESCALATION` |
| `hmi_action` | `DCA` |
| `mrm_active` | `false` |

자동화 위치:

- `TestContinuousNonCriticalProgressionMidBand`
- `continuous_phone_to_escalation`

### S-B-103: MID band 비critical 부주의 연속 입력, `ESCALATION -> ABSENT`

목적:

- MID band에서 누적 inattentive 시간이 `T_absent=8.0s`에 도달하면 ABSENT로 상승하고 MRM이 활성화되는지 확인한다.

입력:

| 필드 | 값 |
|---|---|
| `is_attentive` | `false` |
| `reason` | `drowsy` |
| `jetracer_input_0_4` | `0.2` |
| `delta_s` | `1.0` |
| `ticks` | `8` |

실행 예:

```bash
./build/dcas_policy_runner --attentive false --reason drowsy --dt 1.0 --ticks 8 --jetracer 0.2 --lkas-throttle 0.5 --lkas-mode ON_ACTIVE
```

기대 결과:

| 출력 | 기대값 |
|---|---|
| 마지막 `tick` | `7` |
| `step_b_next_state` | `ABSENT` |
| `absent_latched` | `true` |
| `hmi_action` | `MRM` |
| `mrm_active` | `true` |
| `throttle_limit` | `0` |

자동화 위치:

- `TestContinuousInattentiveEventuallyAbsent`
- `continuous_drowsy_to_absent`

### S-B-104: critical reason 즉시 ABSENT

목적:

- `unresponsive`와 `intoxicated`가 타이머와 무관하게 즉시 ABSENT로 상승하는지 확인한다.

입력:

| 필드 | 값 |
|---|---|
| `is_attentive` | `false` |
| `reason` | `unresponsive` 또는 `intoxicated` |
| `delta_s` | `0.1` |
| timestamp | 동일 |

실행 예:

```bash
./build/dcas_policy_runner --attentive false --reason unresponsive --dt 0.1 --jetracer 0.1 --lkas-throttle 0.5 --lkas-mode ON_ACTIVE
```

기대 결과:

| 출력 | `unresponsive` 기대값 |
|---|---|
| `step_b_next_state` | `ABSENT` |
| `reason` | `unresponsive` |
| `absent_latched` | `true` |
| `hmi_action` | `MRM` |
| `mrm_active` | `true` |
| `driver_override_lock` | `false` |

추가 확인:

- `reason=intoxicated`인 경우 즉시 `ABSENT/MRM`
- `driver_override_lock`은 Step C lockout 정책에 따라 유지되어야 한다.

자동화 위치:

- `TestStepBCriticalReasonLatchesAbsent`
- `critical_unresponsive`

---

## 4) 복귀 및 경고/대응 해제 시나리오

### S-B-201: WARNING에서 200ms 재참여 확인 시 EOR만 해제

목적:

- `current_state=WARNING`이고 `recover_elapsed >= 200ms`이지만 `T_recover_hold` 미만이면 상태는 WARNING 유지, HMI는 INFO로 완화되는지 확인한다.

전제:

- 먼저 `WARNING` 상태에 진입한다.

입력 흐름:

| 순서 | `is_attentive` | `reason` | `delta_s` | 기대 |
|---:|---|---|---:|---|
| 1 | `false` | `phone` | `2.1` | `WARNING/EOR` |
| 2 | `true` | `phone` | `0.3` | `WARNING/INFO`, `reengagement_confirmed_200ms=true` |

기대 결과:

| 출력 | 기대값 |
|---|---|
| `step_b_next_state` | `WARNING` |
| `reengagement_confirmed_200ms` | `true` |
| `hmi_action` | `INFO` |
| `throttle_limit` | WARNING 기준 보수값 유지 |

자동화 위치:

- `TestRecover200msClearsEorButHoldsWarning`
- `TestStepCWarningCanClearEorAfter200msConfirmation`

### S-B-202: WARNING에서 recover hold 충족 시 OK 복귀

목적:

- `recover_elapsed >= T_recover_hold(3.0s)`이면 ABSENT latch가 없는 경우 `OK`로 복귀하는지 확인한다.

입력 흐름:

| 순서 | `is_attentive` | `reason` | `delta_s` | 기대 |
|---:|---|---|---:|---|
| 1 | `false` | `phone` | `2.1` | `WARNING` |
| 2 | `true` | `phone` | `0.3` | `WARNING`, EOR 해제 |
| 3 | `true` | `phone` | `2.7` | `OK` |

기대 결과:

| 출력 | 기대값 |
|---|---|
| `step_b_next_state` | `OK` |
| `reason` | `none` |
| `hmi_action` | `INFO` |
| `absent_latched` | `false` |

자동화 위치:

- `TestRecover200msClearsEorButHoldsWarning`
- `TestStepBRecoversToOkAfterHold`

### S-B-202A: ESCALATION에서 200ms 재참여 확인 시 DCA 대응 완화

목적:

- 현재 상태가 ESCALATION이라도 non-critical 경로에서 `recover_elapsed >= 200ms`이면 DCA 경고/대응 채널은 완화하되, `T_recover_hold` 전까지 상태는 유지되는지 확인한다.

입력 흐름:

| 순서 | `is_attentive` | `reason` | `delta_s` | 기대 |
|---:|---|---|---:|---|
| 1 | `false` | `phone` | `4.0` | `ESCALATION/DCA` |
| 2 | `true` | `phone` | `0.3` | `ESCALATION/INFO`, `reengagement_confirmed_200ms=true` |
| 3 | `true` | `phone` | `2.7` | `OK/INFO` |

자동화 위치:

- 신규 필요: `TestEscalationReengagementClearsDcaButHoldsState`

### S-B-203: ABSENT latch는 같은 run cycle에서 복귀 금지

목적:

- 한 번 ABSENT에 도달하면 이후 `is_attentive=true`가 들어와도 같은 run cycle에서는 ABSENT 유지되는지 확인한다.

입력 흐름:

| 순서 | `is_attentive` | `reason` | `delta_s` | 기대 |
|---:|---|---|---:|---|
| 1 | `false` | `intoxicated` | `0.1` | `ABSENT`, latch true |
| 2 | `true` | `none` | `2.0` | `ABSENT`, latch true |

기대 결과:

| 출력 | 기대값 |
|---|---|
| `step_b_next_state` | `ABSENT` |
| `absent_latched` | `true` |
| `hmi_action` | `MRM` |
| `mrm_active` | `true` |

자동화 위치:

- `TestAbsentLatchBlocksAttentiveRecovery`

### S-B-204: 새 run cycle에서 ABSENT latch 해제

목적:

- `ResetForNewRunCycle()` 호출 후에는 ABSENT latch가 초기화되는지 확인한다.

입력 흐름:

| 순서 | 동작 | 기대 |
|---:|---|---|
| 1 | `unresponsive`로 ABSENT 진입 | latch true |
| 2 | `ResetForNewRunCycle(2)` | state-store 초기화 |
| 3 | `is_attentive=true`, `reason=none` | `OK`, latch false |

자동화 위치:

- `TestResetForNewRunCycleClearsAbsentLatch`

---

## 5) Step C 정책 시나리오

### S-C-001: 상태별 HMI mapping

목적:

- Step B 상태가 Step C HMI로 일관되게 변환되는지 확인한다.

| `driver_state` | 기대 `hmi_action` | 기대 `mrm_active` |
|---|---|---|
| `OK` | `INFO` | `false` |
| `WARNING` | `EOR` | `false` |
| `ESCALATION` | `DCA` | `false` |
| `ABSENT` | `MRM` | `true` |

자동화 위치:

- `TestStepCWarningMapsToEor`
- `TestContinuousNonCriticalProgressionMidBand`
- `TestContinuousInattentiveEventuallyAbsent`

### S-C-002: 상태별 throttle limit

목적:

- 상태가 악화될수록 throttle 제한이 보수적으로 적용되는지 확인한다.

기준:

| 상태 | 기대 제한 |
|---|---|
| `OK` | `1.00 * lkas_throttle` |
| `WARNING` | 문서 기준 `<= 0.60 * lkas_throttle` |
| `ESCALATION` | 문서 기준 `<= 0.20 * lkas_throttle` |
| `ABSENT` | `0.0` |

현재 테스트 기대:

- `WARNING`에서 `lkas_throttle=0.8`이면 `throttle_limit=0.56`
- `ESCALATION`에서 `lkas_throttle=0.5`이면 `throttle_limit=0.15`
- `ABSENT`에서 `throttle_limit=0`

주의:

- 현재 구현은 `WARNING=0.7`, `ESCALATION=0.3` gain을 사용한다.
- 문서 기준의 `<=0.60`, `<=0.20`과 다르므로, 이 항목은 구현/문서 정합성 재확인이 필요하다.

자동화 위치:

- `TestStepCWarningMapsToEor`
- `continuous_phone_to_escalation`
- `continuous_drowsy_to_absent`

### S-C-003: notebook input alive는 LKAS 활성 조건으로만 사용

목적:

- `notebook_input_alive=false`일 때 별도 HMI 승격 없이 `ON_ACTIVE` 승격만 차단하는지 확인한다.

입력:

| 필드 | 값 |
|---|---|
| `driver_state` | `OK` |
| `previous_lkas_mode` | `ON_ACTIVE` |
| `notebook_input_alive` | `false` |

기대 결과:

| 출력 | 기대값 |
|---|---|
| `next_lkas_mode` | `ON_INACTIVE` |
| `hmi_action` | `INFO` |

자동화 위치:

- `TestStepCNotebookInputAliveOnlyBlocksActivation`

### S-C-004: 반복 MRM lockout

목적:

- 같은 run cycle에서 MRM이 반복되면 lockout이 걸리고 LKAS가 OFF로 내려가는지 확인한다.

입력:

| 필드 | 값 |
|---|---|
| `driver_state` | `ABSENT` |
| `reason` | `unresponsive` |
| `mrm_activation_count_run_cycle` | `1` |

기대 결과:

| 출력 | 기대값 |
|---|---|
| `mrm_active` | `true` |
| `driver_override_lock` | `true` |
| `next_lkas_mode` | `OFF` |

자동화 위치:

- `TestStepCAbsentActivatesMrmAndLockoutOnSecondActivation`

---

## 6) Reason별 최소 확인 세트

| Reason | `is_attentive` | 기대 Step B | 기대 Step C |
|---|---|---|---|
| `none` | `true` | `OK`, reason `none` | `INFO` |
| `unknown` | `false` | 타이머 기반 전이 | 상태 기반 HMI |
| `phone` | `false` | 타이머 기반 전이 | `WARNING=EOR`, `ESCALATION=DCA` |
| `drowsy` | `false` | 타이머 기반 전이 | `WARNING=EOR`, `ESCALATION=DCA`, 보수 throttle |
| `unresponsive` | `false` | 즉시 `ABSENT` | `MRM`, 오버라이드 허용 경로 |
| `intoxicated` | `false` | 즉시 `ABSENT` | `MRM`, 오버라이드 잠금 경로 |
| `phone/drowsy` | `true`, `recover_elapsed < 0.2s` | 현재 상태 유지, 최신 reason 유지 | 기존 non-critical 경고/대응 유지 |
| `phone/drowsy` | `true`, `0.2s <= recover_elapsed < 3.0s` | 현재 상태 유지 | 경고/대응 완화 |
| `phone/drowsy` | `true`, `recover_elapsed >= 3.0s` | `OK`, reason `none` | `INFO` |
| `unresponsive/intoxicated` | `true` | 즉시 `ABSENT` | `MRM` |

---

## 7) 수동 실행용 대표 명령 모음

### 정상 집중

```bash
./build/dcas_policy_runner --attentive true --reason none --dt 0.1 --jetracer 0.2 --lkas-throttle 0.5 --lkas-mode ON_ACTIVE
```

기대:

- `OK`
- `INFO`
- `mrm_active=false`

### 휴대폰 사용으로 WARNING

```bash
./build/dcas_policy_runner --attentive false --reason phone --dt 2.1 --jetracer 0.2 --lkas-throttle 0.5 --lkas-mode ON_ACTIVE
```

기대:

- `WARNING`
- `EOR`

### 휴대폰 사용 지속으로 ESCALATION

```bash
./build/dcas_policy_runner --attentive false --reason phone --dt 1.0 --ticks 4 --jetracer 0.2 --lkas-throttle 0.5 --lkas-mode ON_ACTIVE
```

기대:

- 마지막 tick에서 `ESCALATION`
- `DCA`

### 졸음 지속으로 ABSENT

```bash
./build/dcas_policy_runner --attentive false --reason drowsy --dt 1.0 --ticks 8 --jetracer 0.2 --lkas-throttle 0.5 --lkas-mode ON_ACTIVE
```

기대:

- 마지막 tick에서 `ABSENT`
- `MRM`
- `throttle_limit=0`

### 무반응 즉시 ABSENT

```bash
./build/dcas_policy_runner --attentive false --reason unresponsive --dt 0.1 --jetracer 0.1 --lkas-throttle 0.5 --lkas-mode ON_ACTIVE
```

기대:

- `ABSENT`
- `MRM`
- `mrm_active=true`

### 비동기 critical reason timestamp

```bash
./build/dcas_policy_runner --attentive false --reason unresponsive --ts 1000 --reason-ts 1001 --dt 1.0
```

기대:

- `ABSENT`
- `reason=unresponsive`
- `MRM`

---

## 8) 현재 미해결 또는 재확인 필요 항목

### 8.1 Step C throttle gain 문서/구현 차이

문서 기준:

- `WARNING <= 0.60 * lkas_throttle`
- `ESCALATION <= 0.20 * lkas_throttle`

현재 구현:

- `WARNING = 0.70 * lkas_throttle`
- `ESCALATION = 0.30 * lkas_throttle`

판정:

- 테스트는 현재 구현값을 기준으로 통과한다.
- 문서대로 엄격히 검증하려면 구현 gain을 `0.60`, `0.20`으로 바꾸거나, 문서 기준값을 업데이트해야 한다.

### 8.2 Speed band LPF/hysteresis/dwell

문서 기준:

- `rho_v_raw -> LPF -> hysteresis + dwell -> speed_band`

현재 구현:

- `jetracer_input_0_4`를 clamp한 뒤 즉시 `rho_v` band 판정
- LPF/hysteresis/dwell은 아직 단순화되어 있다.

판정:

- 현재 시나리오는 정적 band 기준 테스트다.
- band 경계 채터링 방지까지 검증하려면 별도 `SpeedBandEstimator` 테스트가 필요하다.

### 8.3 비동기 VLM reason 정책 미구현

문서 기준:

- `reason_ts_ms`가 `is_attentive_ts_ms`와 달라도 최신 VLM reason이면 채택
- `critical reason`은 현재 `is_attentive=yes/no`와 무관하게 즉시 `ABSENT/MRM`
- `is_attentive=yes` + non-critical latest reason은 recover 구간에 따라 경고/대응 유지, 완화, OK 복귀로 분기
- `T_recover_hold=3.0s`

현재 구현/테스트:

- 기존 구현은 동일 timestamp 기반 정규화와 `is_attentive=yes -> reason=none` 흐름 일부가 남아 있다.
- `T_recover_hold`도 코드에서는 아직 3.0s로 통일되지 않았을 수 있다.

판정:

- 이 문서의 신규 비동기 reason 시나리오를 통과하려면 Step B state-store에 `latest_reason/latest_reason_ts_ms`를 추가하고 테스트를 갱신해야 한다.

### 8.4 Vehicle-jetracer 실제 루프 연결

현재 검증:

- Python subprocess로 DCAS runner 호출 확인
- `vehicle.py` 문법 확인

미검증:

- 실제 카메라/LKAS/NvidiaRacecar 초기화 후 DCAS 결과를 throttle 적용 경로에 반영하는 통합 테스트

권장 다음 단계:

- `vehicle.py`에 `--dry-run` 또는 mock hardware 모드 추가
- DCAS 결과를 `_set_throttle()` 직전 제한값으로 적용
- dry-run에서 `is_attentive/reason` mock stream을 흘려 통합 확인
