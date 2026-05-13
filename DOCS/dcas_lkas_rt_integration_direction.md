# 실시간성을 고려한 DCAS-LKAS 통합 방향

이 문서는 현재 시스템을 **필터 패턴(Filter Pattern)** 으로 재구성할 때의 통합 방향을 정리한다.

핵심 전제는 다음과 같다.

- 차량 제어 코드는 Python에서 C++로 분리한다.
- LKAS는 원본 주행 결정을 담당한다.
- DCAS는 LKAS 결과를 필터링하고 감독하는 정책 계층이다.
- Actuator HAL은 최종 하드웨어 인가만 담당한다.
- 실시간성은 하위 계층일수록 더 강하게 보장한다.

---

## 0. Git 브랜치 전략 및 레거시 호환성 (Safe Migration)

기존 팀원들의 Python 기반 제어 흐름을 깨지 않도록, 새 C++ Actuator 경로는 **스위치 플래그**로 안전하게 얹는다.

### 0.1 브랜치 생성

```bash
git checkout -b feat/rt-dcas-architecture
```

### 0.2 vehicle.py 레거시 보호 스위치

`vehicle.py`의 argparse에 `--use-cpp-actuator` 옵션을 추가한다.

- Legacy 모드: 기존 팀원용 Python 제어 유지
- New 모드: Python은 제어에서 손을 떼고, C++ Actuator가 하드웨어 제어

예시 흐름:

```python
if not args.use_cpp_actuator:
   # [Legacy 모드] 기존 팀원용: 파이썬이 control_shm을 읽고 직접 모터 제어
   self.car.steering = lkas_steering
   self.car.throttle = lkas_throttle
else:
   # [New 모드] Python은 제어에서 손을 뗌, C++ Actuator가 하드웨어 통제
   pass
```

---

## 1. 최종 아키텍처: The Filter Pattern

### Node 1. Vehicle Gateway [Python / Non-RT]

- 카메라 이미지 캡처
- LKAS로 프레임 전달
- Viewer 웹브라우저와 통신
- 차량 상위 상태 브로드캐스트

Vehicle Gateway는 **비실시간 입구**다. 여기서는 프레임 전송과 UI 연동이 중요하고, 모터를 직접 움직이지 않는다.

### Node 2. LKAS [Python]

- 비전 기반 차선 인식
- 원본 스로틀 / 조향각 계산
- MRM 상태 머신 내장

LKAS는 자율주행 두뇌다. 여기서는 “얼마나 가속할지”를 원본으로 계산하지만, 최종 제한값은 아직 확정하지 않는다.

### Node 3. DCAS Policy Engine [C++]

- 운전자 상태 평가
- LKAS 스로틀 값 제한(Clamping)
- 정책 기반 감독

DCAS는 **필터**다. 즉, LKAS가 낸 스로틀을 받아 정책에 따라 제한하고, 필요 시 MRM을 LKAS에 요청한다.

### Node 4. Actuator HAL [C++ / Highest Priority]

- 최종 제어값(PWM/I2C) 인가
- 하드웨어 센서 기반 속도 수집
- 상태 정보 상위 계층으로 송신

Actuator HAL은 **근육**이다. 판단하지 않고, 전달된 최종 제어값만 실행한다.

---

## 2. 통신 설계 및 데이터 흐름

이 아키텍처의 핵심은 **직렬 필터링**이다.

### 전체 데이터 순환

```text
[Data Pipeline Flow]

1. 차량 상태 수집 (속도)
   (하드웨어) -> [Node 4: Actuator HAL] -> (초고속 IPC) -> [Node 3: DCAS]
   ※ 속도는 파이썬이나 카메라가 아닌, C++ 액추에이터가 휠 엔코더나 모터 제어값을 기반으로 추정/계산하여 DCAS로 올려보냅니다.

2. 원본 스로틀 계산
   [Node 1: 카메라] -> [Node 2: LKAS] -> 원본 스로틀(lkas_throttle) 계산 -> (초고속 IPC) -> [Node 3: DCAS]

3. 스로틀 제한 (필터링)
   [Node 3: DCAS] 내부 연산:
   최종 스로틀 = clamp(lkas_throttle, 0.0, dcas_throttle_limit)

   조향(steering) 값도 최종 제어값 경로에 포함되며, DCAS는 조향을 수정하지 않고 그대로 전달한다.

4. 하드웨어 실행
   [Node 3: DCAS] -> 제한된 최종 스로틀(final_throttle) -> (초고속 IPC) -> [Node 4: Actuator HAL] -> 모터 구동

5. MRM 발동 시
   [Node 3: DCAS] -> MRM 트리거(mrm_request = true) -> [Node 2: LKAS]
   -> LKAS가 스스로 감속/조향 궤적 생성 -> (이후 2~4번 과정 동일하게 반복)

6. Viewer 시각화 경로
   Viewer는 shared memory를 직접 읽지 않는다.
   LKAS의 ZMQ 브로드캐스트를 구독하여 프레임/상태/탐지 결과를 수신한다.
```

### 의미 정리

- **속도는 Actuator HAL이 가장 정확하게 알고 있다.**
- **LKAS는 원본 throttle을 계산한다.**
- **DCAS는 LKAS throttle을 받아 정책으로 제한한다.**
- **Actuator HAL은 제한된 최종 throttle만 실행한다.**
- **MRM은 DCAS가 요청하고, LKAS 상태 머신이 수행한다.**

즉, 액추에이터가 LKAS와 DCAS를 별도로 판단하는 구조가 아니라, **DCAS가 중간 필터로서 LKAS 출력을 제한한 뒤 Actuator가 그대로 실행**하는 구조다.

---

## 3. 통신 스키마

아래 구조체들은 “어떤 데이터가 어디로 가는지”를 명확히 하기 위한 최소 메시지 정의다.

### 3.1 DCAS -> Actuator

전송 주기: 초당 50~100회

```cpp
struct DcasToActuator {
    uint64_t timestamp_us;     // 지연시간 측정용 타임스탬프
    float final_throttle;      // DCAS가 이미 제한을 끝낸 '최종' 스로틀 값
    bool is_valid;             // 시스템 정상 작동 여부 (Watchdog 용)
};
```

핵심:

- Actuator는 판단하지 않는다.
- `final_throttle`만 받아 모터에 쓴다.
- `is_valid`가 false이면 즉시 안전 정지로 간다.

### 3.2 Actuator -> DCAS

전송 주기: 초당 50~100회

```cpp
struct ActuatorToDcas {
    float current_speed_kmh;   // 모터/엔코더 기반 현재 속도
    bool hardware_fault;       // I2C 통신 에러 등 하드웨어 장애 여부
};
```

핵심:

- DCAS 정책 평가의 핵심 입력은 속도다.
- 속도는 가장 하단 계층에서 올라와야 한다.
- hardware fault는 정책보다 우선하는 안전 신호다.

### 3.3 DCAS -> LKAS

이벤트 발생 시 전송

```cpp
struct DcasToLkas {
    bool mrm_request;          // true가 되면 LKAS 내부의 MRM 상태 머신 강제 시작
    uint8_t target_mode;       // LKAS 모드 전환 요청 (예: Active -> Standby)
};
```

핵심:

- DCAS는 MRM을 직접 수행하지 않는다.
- LKAS 내부 MRM 로직을 시작시키는 트리거만 보낸다.
- 모드 전환은 LKAS가 책임진다.

### 3.4 현재 vehicle.py 기준 영향 범위

현재 [Vehicle-jetracer/src/vehicle.py](/home/leo/Vehicle-jetracer/src/vehicle.py) 는 다음을 한 프로세스 안에서 같이 하고 있다.

- 카메라 읽기
- LKAS에 프레임 전달
- LKAS 제어 수신
- `NvidiaRacecar` 직접 제어
- 상태 ZMQ 브로드캐스트

이 구조에서는 Python Vehicle가 사실상 "Gateway + Actuator"를 동시에 맡고 있으므로, 4번 노드(Actuator HAL)를 분리하려면 아래처럼 책임을 나눠야 한다.

- Vehicle Gateway: 카메라 캡처, viewer 통신, LKAS용 프레임 전달만 유지
- LKAS: 원본 throttle/steering 생성
- DCAS: LKAS throttle을 읽고 제한
- Actuator HAL: DCAS가 확정한 final_throttle만 하드웨어에 인가

즉, 현재 `vehicle.py` 안의 `NvidiaRacecar()` 생성과 `_update_vehicle_state()` 는 최종적으로 C++ Actuator HAL로 이동해야 한다.

---

## 4. 왜 이 구조가 실시간성에 유리한가

### 4.1 판단과 실행 분리

- DCAS는 정책 계산만 한다.
- Actuator는 제어 실행만 한다.
- 둘을 분리하면 실행 계층의 지터를 줄일 수 있다.

### 4.2 최하위 계층 우선순위 보장

- Actuator HAL은 최우선 순위로 실행된다.
- watchdog을 통해 상위 계층 장애 시 즉시 정지할 수 있다.
- 상위 계층이 멈춰도 하위 계층이 안전을 유지할 수 있다.

### 4.3 Python 제거 가능

현재 Vehicle 코드는 Python이다.
이 구조는 장기적으로 Vehicle 제어부를 C++로 이동시키는 전제를 만족한다.
즉, 지금은 브릿지 역할만 하고, 나중에는 RT 핵심을 C++로 옮길 수 있다.

---

## 5. 권장 구현 원칙

### 5.1 데이터 경계

- 큰 데이터: 카메라 프레임, segmentation 결과 -> shared memory
- 작은 제어 데이터: speed, throttle, mode, fault, MRM 요청 -> 저지연 IPC

### 5.2 DCAS의 책임

- LKAS throttle을 읽는다.
- 차량 속도를 읽는다.
- 정책으로 throttle을 제한한다.
- 필요 시 MRM을 LKAS에 요청한다.

### 5.3 Actuator의 책임

- 최종 throttle만 실행한다.
- 속도 센서와 hardware fault를 상위로 올린다.
- watchdog으로 안전 정지를 수행한다.

### 5.4 LKAS의 책임

- 차선 인식과 주행 원본 결정을 수행한다.
- MRM 상태 머신을 소유한다.
- DCAS의 요청을 받아 내부 상태를 전이한다.

---

## 6. IPC 재배치 및 데이터 파이프라인

### 6.1 기존 채널 (레거시 구조 유지)

- `image_shm`: Node 1(Python) -> Node 2(LKAS) (유지)
- `control_shm`: **사용 안함**

### 6.2 신규 C++ IPC

#### [SHM] rt_control_shm (신규 공유 메모리)

하나의 제어 SHM을 구역(Offset)으로 나눠 Lock-free 링버퍼로 통신한다.

- 구역 1: Actuator -> DCAS (current_speed_kmh)
- 구역 2: Node 2(LKAS) -> Node 3(DCAS) (원본 스로틀)
- 구역 3: DCAS -> Actuator (final_throttle)

#### [POSIX MQ] /mrm_event_queue

- DCAS -> LKAS
- MRM 발생 시에만 `mrm_request=true` 이벤트 송신

### 6.3 데이터 파이프라인 흐름도

```text
1. [Node 4: Actuator] -> (rt_control_shm) -> 현재 속도 전달 -> [Node 3: DCAS]
2. [Node 2: LKAS] -> (rt_control_shm) -> 원본 스로틀 전달 -> [Node 3: DCAS]
3. [Node 3: DCAS] 연산: 최종 스로틀 = clamp(원본 스로틀, 0.0, DCAS 제한치)
4. [Node 3: DCAS] -> (rt_control_shm) -> 최종 스로틀 전달 -> [Node 4: Actuator]
5. [Node 4: Actuator] -> I2C 하드웨어 모터 구동
```

### Phase 2. MRM 연동

- DCAS가 `mrm_request`를 만든다.
- LKAS가 이를 받아 MRM state machine을 시작한다.

### Phase 3. C++ Actuator HAL 분리

- Python Vehicle에서 모터 제어를 제거한다.
- C++ Actuator HAL이 최종 하드웨어 인가를 맡는다.

#### 3-1. Actuator HAL이 가져갈 현재 vehicle.py 기능

현재 [Vehicle-jetracer/src/vehicle.py](/home/leo/Vehicle-jetracer/src/vehicle.py) 에 있는 다음 코드는 Actuator HAL로 이동한다.

- `NvidiaRacecar` 초기화
- `_set_throttle()` / `_set_steering()` 이후 실제 모터 반영
- `_update_vehicle_state()` 의 하드웨어 write
- 차량 속도 추정/수집 로직

반대로 아래 코드는 Vehicle Gateway에 남겨도 된다.

- `Camera` 캡처
- LKAS frame push
- viewer 상태 publish

#### 3-2. 공유 메모리 수정 계획

기존 shared memory는 "큰 데이터"와 "작은 제어 데이터"로 다시 나눈다.

- 큰 데이터: `image_shm_name` 유지
- 중간 제어 데이터: `control_shm_name` 은 LKAS 원본 제어용으로 유지 또는 이름 변경
- 작은 RT 제어 데이터: `dcas_actuator_command_shm_name` 신규 추가
- 센서/상태 데이터: `actuator_telemetry_shm_name` 신규 추가
- 이벤트 데이터: `dcas_lkas_event_shm_name` 신규 추가

실행 관점에서는 아래와 같은 재배치가 된다.

- Vehicle Gateway -> LKAS: frame shared memory
- LKAS -> DCAS: raw control shared memory
- DCAS -> Actuator: filtered control shared memory
- Actuator -> DCAS: speed/fault telemetry shared memory
- DCAS -> LKAS: MRM event channel

### Phase 4. RT 강화

- PREEMPT_RT 적용
- `mlockall`
- CPU isolation
- SCHED_FIFO 우선순위 분리
- watchdog 강화

### Phase 5. 기존 Python 경로 정리

- `vehicle.py` 에 남아 있는 하드웨어 actuation 경로를 제거한다.
- Python Vehicle는 gateway 성격만 유지한다.
- 실제 모터 제어는 C++ Actuator HAL 한 곳에서만 일어나도록 고정한다.

---

## 7. 구현 순서 (Next Action Item)

가장 아래 단(근육)부터 위로 올라가며 조립한다.

1. C++ IPC 헤더 작성
   - `rt_control_shm`에 올라갈 바이너리 구조체와 Lock-free 링버퍼 읽기/쓰기 설계
2. Node 4 구축
   - C++에서 `/dev/i2c-1` 열고 PCA9685에 PWM 출력
   - 워치독 로직 포함
3. Node 3 연동
   - DCAS가 `rt_control_shm`을 읽고 제한값을 써넣는 로직 추가
4. Node 1 스위치 적용
   - `vehicle.py`에 `--use-cpp-actuator` 적용 및 통합 테스트

---

## 8. 현재 구현/검증 상태

MRM을 제외한 기본 제어 파이프라인은 문서 기준으로 연결 완료된 상태다.

완료된 노드 흐름:

```text
Vehicle Gateway(Python)
  -> 카메라 프레임을 LKAS에 전달

LKAS(Python)
  -> raw `lkas_throttle/lkas_steering` 계산
  -> `rt_control_shm.lkas_to_dcas`에 업로드

DCAS Policy Engine(C++)
  -> `rt_control_shm.lkas_to_dcas`에서 LKAS 원본 제어 읽기
  -> `rt_control_shm.actuator_to_dcas`에서 actuator 속도/하드웨어 상태 읽기
  -> 정책 기반 `final_throttle = clamp(lkas_throttle, 0.0, dcas_throttle_limit)` 계산
  -> steering은 수정하지 않고 그대로 전달
  -> `rt_control_shm.dcas_to_actuator`에 최종 제어 업로드

Actuator HAL(C++)
  -> `rt_control_shm.dcas_to_actuator`에서 최종 제어 읽기
  -> I2C/PWM 하드웨어에 최종 제어 인가
  -> serial 속도 값을 `rt_control_shm.actuator_to_dcas`로 업로드
```

실주행 smoke test에서 확인된 값 예:

```text
latest actuator_to_dcas speed: 1.45 hardware_fault: 0
latest lkas_to_dcas throttle: 0.20771 steering: 0.266297
latest dcas_to_actuator final throttle: 0.20771 steering: 0.266297 valid: 1
```

따라서 남은 핵심 검증은 **가짜/제어 가능한 `is_attentive`, `reason` 입력을 넣었을 때 전체 공유메모리 파이프라인에서 DCAS 제한 결과가 의도대로 바뀌는지** 확인하는 것이다.

주의:

- `rt_control_shm_tool --mode read_*`는 샘플을 소비(pop)하므로 실시간 모니터링에는 부적합하다.
- 모니터링은 non-consuming 모드인 `status`, `peek_lkas`, `peek_dcas`, `peek_act`를 사용한다.

```bash
cd /home/leo/ads-skynet/rt-control-ipc
./build/rt_control_shm_tool --mode status
./build/rt_control_shm_tool --mode peek_lkas
./build/rt_control_shm_tool --mode peek_dcas
./build/rt_control_shm_tool --mode peek_act
```

---

## 9. 전체 흐름 테스트 계획

이 테스트 계획은 `/home/leo/ads-skynet/DCAS-PolicyEngine/tests`의 C++ 정책 테스트를 기반으로 하고,
그 위에 shared memory bridge 및 실제 4프로세스 주행 검증을 단계적으로 얹는다.

### 9.1 테스트 레이어

| Layer | 목적 | 대상 |
|---|---|---|
| L0 정책 단위 테스트 | Step B/C 상태 전이와 throttle limit 계산 검증 | `tests/test_policy.cpp`, `ctest` |
| L1 SHM bridge smoke test | LKAS/Actuator mock 샘플을 SHM에 넣고 DCAS final 출력 확인 | `rt_control_shm_tool`, `dcas_rt_bridge --once` |
| L2 4프로세스 통합 테스트 | 실제 `lkas`, `vehicle`, `dcas_rt_bridge`, `rt_actuator` 동시 실행 확인 | 실제 주행 파이프라인 |
| L3 가짜 인지 입력 통합 테스트 | `is_attentive/reason` 조합별로 실제 final throttle 변화 확인 | `dcas_rt_bridge` CLI 인지 입력 |

### 9.2 L0: 정책 단위 테스트

목표:

- `is_attentive/reason/delta_s/speed band` 조합별 Step B 상태 전이 확인
- Step C가 `OK/WARNING/ESCALATION/ABSENT`를 throttle limit/HMI로 변환하는지 확인
- critical reason, recover, latch, LKAS mode gating 같은 정책 단위 불변식 확인

실행:

```bash
cd /home/leo/ads-skynet/DCAS-PolicyEngine
cmake --build build --target dcas_policy_tests
ctest --test-dir build --output-on-failure
```

필수 보강 테스트:

- `is_attentive=true, reason=none`이면 `OK`, `throttle_limit = 1.0 * lkas_throttle`
- `is_attentive=false, reason=phone/drowsy`, MID band 누적 2s 이상이면 `WARNING`
- `WARNING`에서 `throttle_limit < lkas_throttle`
- `is_attentive=false` 누적 4s 이상이면 `ESCALATION`
- `ESCALATION`에서 `throttle_limit < WARNING throttle_limit`
- `reason=unresponsive/intoxicated`이면 즉시 `ABSENT/MRM`
- `recover_elapsed >= T_recover_hold`이면 non-critical 경로에서 `OK` 복귀

주의:

- 현재 문서 기준과 `test_policy.cpp`의 일부 과거 expectation이 다를 수 있다.
- 특히 비동기 reason 정책, `T_recover_hold=3.0s`, Step C throttle gain 문서값과 구현값은 먼저 정합성을 맞춘 뒤 L0를 고정한다.

### 9.3 L1: SHM bridge smoke test

목표:

- LKAS/Actuator 실제 프로세스 없이도 `dcas_rt_bridge`가 SHM을 읽고 final 제어를 쓰는지 확인
- `final_throttle = clamp(lkas_throttle, 0.0, dcas_throttle_limit)` 불변식 확인
- steering이 DCAS에서 수정되지 않고 통과하는지 확인

기본 OK 테스트:

```bash
cd /home/leo/ads-skynet/rt-control-ipc
./build/rt_control_shm_tool --mode write_act --speed 1.0
./build/rt_control_shm_tool --mode write_lkas --throttle 0.25 --steering 0.20

cd /home/leo/ads-skynet/DCAS-PolicyEngine/build
./dcas_rt_bridge --once --attentive true --reason none --dt 0.02 --verbose

cd /home/leo/ads-skynet/rt-control-ipc
./build/rt_control_shm_tool --mode status
```

기대:

- `state=OK`
- `final_throttle`은 LKAS raw throttle과 같거나 매우 근접
- `final_steering`은 LKAS raw steering과 같거나 매우 근접
- `status`에서 `lkas_to_dcas`, `actuator_to_dcas`, `dcas_to_actuator` 최신 timestamp가 갱신됨

WARNING 테스트:

```bash
cd /home/leo/ads-skynet/rt-control-ipc
./build/rt_control_shm_tool --mode write_act --speed 12.0
./build/rt_control_shm_tool --mode write_lkas --throttle 0.80 --steering -0.25

cd /home/leo/ads-skynet/DCAS-PolicyEngine/build
./dcas_rt_bridge --once --attentive false --reason phone --dt 2.5 --verbose
```

기대:

- `state=WARNING`
- `final_throttle < lkas_throttle`
- `final_steering = lkas_steering`

ESCALATION 테스트:

```bash
cd /home/leo/ads-skynet/rt-control-ipc
./build/rt_control_shm_tool --mode write_act --speed 12.0
./build/rt_control_shm_tool --mode write_lkas --throttle 0.80 --steering -0.25

cd /home/leo/ads-skynet/DCAS-PolicyEngine/build
./dcas_rt_bridge --iterations 4 --attentive false --reason phone --dt 1.0 --period-ms 10 --verbose
```

기대:

- 최종 iteration에서 `state=ESCALATION`
- `final_throttle`은 WARNING보다 더 낮음
- `final_steering = lkas_steering`

critical reason 테스트:

```bash
cd /home/leo/ads-skynet/rt-control-ipc
./build/rt_control_shm_tool --mode write_act --speed 12.0
./build/rt_control_shm_tool --mode write_lkas --throttle 0.50 --steering 0.10

cd /home/leo/ads-skynet/DCAS-PolicyEngine/build
./dcas_rt_bridge --once --attentive false --reason unresponsive --dt 0.1 --verbose
```

기대:

- `state=ABSENT`
- `final_throttle=0`
- `valid=1`
- MRM 연동은 본 문서의 현재 범위에서는 별도 검증 대상으로 둔다.

### 9.4 L2: 4프로세스 통합 테스트

목표:

- 실제 노드가 독립 프로세스로 뜬 상태에서 `rt_control_shm` 값이 지속 갱신되는지 확인
- 차량이 라인을 따라 주행하면서 `LKAS raw -> DCAS final -> Actuator` 흐름이 유지되는지 확인

터미널 1:

```bash
cd /home/leo/Lkas
lkas --broadcast
```

터미널 2:

```bash
cd /home/leo/Vehicle-jetracer
vehicle --use-cpp-actuator
```

터미널 3:

```bash
cd /home/leo/ads-skynet/DCAS-PolicyEngine/build
./dcas_rt_bridge --attentive true --reason none --dt 0.02 --period-ms 10
```

터미널 4:

```bash
cd /home/leo/ads-skynet/rt-actuator
sudo ./build/rt_actuator --use-shm --serial /dev/ttyACM0 --serial-baud 115200
```

모니터링:

```bash
cd /home/leo/ads-skynet/rt-control-ipc
watch -n 0.2 './build/rt_control_shm_tool --mode status'
```

기대:

- `latest lkas_to_dcas throttle/steering`이 지속 갱신
- `latest dcas_to_actuator final throttle/steering`이 지속 갱신
- `latest actuator_to_dcas speed`가 serial 입력에 따라 갱신
- `age_ms`가 정상 주행 중 수십~수백 ms 이하로 유지
- 차량이 Python 직접 actuation 없이 라인을 따라 주행

### 9.5 L3: 가짜 인지 입력 기반 전체 흐름 테스트

목표:

- 실제 LKAS/Vehicle/Actuator가 떠 있는 상태에서 DCAS bridge의 `--attentive`, `--reason`, `--dt`만 바꿔 final throttle 변화가 정책대로 나타나는지 확인

테스트 방법:

1. L2의 4프로세스를 모두 실행한다.
2. `dcas_rt_bridge`만 중지 후 다른 인지 입력으로 재실행한다.
3. `rt_control_shm_tool --mode status`로 `latest dcas_to_actuator final throttle` 변화를 확인한다.

Case A: 정상 집중

```bash
./dcas_rt_bridge --attentive true --reason none --dt 0.02 --period-ms 10
```

기대:

- `final_throttle`이 `lkas_throttle`과 거의 동일
- `final_steering`은 `lkas_steering`과 동일

Case B: WARNING 유도

```bash
./dcas_rt_bridge --attentive false --reason phone --dt 2.5 --period-ms 10
```

기대:

- DCAS 상태가 `WARNING`으로 진입
- `final_throttle < lkas_throttle`
- steering은 그대로 유지

Case C: ESCALATION 유도

```bash
./dcas_rt_bridge --attentive false --reason phone --dt 1.0 --period-ms 10
```

실행 후 몇 초 이상 유지한다.

기대:

- DCAS 상태가 `ESCALATION`으로 상승
- `final_throttle`이 WARNING보다 더 낮아짐
- steering은 그대로 유지

Case D: 복귀 확인

```bash
./dcas_rt_bridge --attentive true --reason none --dt 0.02 --period-ms 10
```

기대:

- non-critical 경로에서는 recover hold 이후 `OK` 복귀
- `final_throttle`이 다시 LKAS raw throttle에 가까워짐

Case E: critical reason

```bash
./dcas_rt_bridge --attentive false --reason unresponsive --dt 0.1 --period-ms 10
```

기대:

- `final_throttle=0`
- MRM 메시지 연동은 추후 Phase 2 검증으로 분리

주의:

- 현재 `dcas_rt_bridge` CLI는 테스트용 mock 인지 입력을 고정값으로 넣는 구조다.
- 실제 인지팀 연동 전까지는 이 CLI 입력을 “가짜 인지 입력”으로 사용한다.
- 장기적으로는 `is_attentive/reason`도 별도 IPC 또는 perception adapter에서 받아 bridge에 주입해야 한다.

### 9.6 통과 기준

전체 흐름은 아래 조건을 만족하면 MRM 제외 기준 통과로 본다.

- `ctest` 정책 단위 테스트 통과
- L1 smoke test에서 `final_throttle <= lkas_throttle` 불변식 통과
- L1 smoke test에서 `final_steering == lkas_steering` 통과
- L2 4프로세스 실행 중 세 SHM 채널 timestamp가 지속 갱신
- L2 실제 주행에서 Python 직접 actuation 없이 차량이 라인 추종
- L3 가짜 인지 입력 변경에 따라 `OK -> WARNING -> ESCALATION -> recovery` throttle 제한 변화 확인
- critical reason 입력 시 `final_throttle=0` 확인

---

## 10. 결론

이 설계는 다음 원칙을 만족한다.

- LKAS는 원본 제어를 만든다.
- DCAS는 그 제어를 정책적으로 제한한다.
- Actuator HAL은 최종 실행만 한다.
- MRM은 LKAS 내부 상태 머신이 담당한다.
- 속도는 Actuator HAL에서 올라온다.

따라서 이 아키텍처는 **현재 Python 기반 Vehicle에서 시작하되, 최종적으로 실시간 C++ 모듈로 자연스럽게 전환 가능한 구조**다.
