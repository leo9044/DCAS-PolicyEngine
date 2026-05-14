# LKAS -> DCAS -> RT Actuator Code Walkthrough

작성일: 2026-05-14

## 1. 이 문서의 목적

이 문서는 LKAS에서 계산한 조향/스로틀 결과가 DCAS 정책 필터를 거쳐 RT Actuator에서 실제 JetRacer 하드웨어 명령으로 반영되는 흐름을 코드 기준으로 설명한다.

주요 실행 명령은 다음 두 노드다.

```bash
cd /home/leo/ads-skynet/DCAS-PolicyEngine/build
./dcas_rt_bridge --attentive true --reason none --dt 0.02 --period-ms 10
```

```bash
cd /home/leo/ads-skynet/rt-actuator
./build/rt_actuator --use-shm --serial /dev/ttyACM0 --serial-baud 115200
```

핵심 질문:

- LKAS가 만든 `throttle/steering`은 어디에 쓰이는가?
- DCAS는 어떤 값을 읽고 어떤 값을 새로 쓰는가?
- RT Actuator는 어떤 조건에서 하드웨어에 값을 인가하는가?
- 이 구조를 포트폴리오나 다음 프로젝트에서 어떤 설계 포인트로 설명할 수 있는가?

## 2. 전체 흐름 한 줄 요약

```text
LKAS/Vehicle
  -> rt_control_shm.lkas_to_dcas(raw throttle, raw steering)
  -> DCAS Policy Engine(throttle clamp, steering pass-through)
  -> rt_control_shm.dcas_to_actuator(final throttle, final steering)
  -> RT Actuator HAL(I2C/PWM hardware apply)
  -> rt_control_shm.actuator_to_dcas(speed, hardware_fault feedback)
```

DCAS는 차량을 직접 움직이지 않는다. DCAS는 LKAS의 원본 제어값을 읽고, 정책에 따라 throttle만 제한한 뒤, 최종 제어값을 RT Actuator에 전달하는 필터다.

## 3. 공유메모리 데이터 버스

관련 파일:

- `/home/leo/ads-skynet/rt-control-ipc/include/rt_control_shm.hpp`
- `/home/leo/ads-skynet/rt-control-ipc/src/rt_control_shm.cpp`

공유메모리 이름:

```cpp
constexpr const char* kRtControlShmName = "/rt_control_shm";
```

실제 파일 경로로는 보통 다음처럼 보인다.

```text
/dev/shm/rt_control_shm
```

### 3.1 메시지 구조체

LKAS가 DCAS에 보내는 원본 제어:

```cpp
struct LkasToDcasSample {
    std::uint64_t timestamp_us;
    float lkas_throttle;
    float lkas_steering;
    std::uint32_t reserved;
};
```

DCAS가 Actuator에 보내는 최종 제어:

```cpp
struct DcasToActuatorSample {
    std::uint64_t timestamp_us;
    float final_throttle;
    float final_steering;
    std::uint32_t is_valid;
};
```

Actuator가 DCAS에 보내는 하드웨어/속도 피드백:

```cpp
struct ActuatorToDcasSample {
    std::uint64_t timestamp_us;
    float current_speed_kmh;
    std::uint32_t hardware_fault;
    std::uint32_t reserved;
};
```

### 3.2 링버퍼 구조

공유메모리 전체 layout은 세 개의 링버퍼로 구성된다.

```cpp
struct RtControlShmLayout {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t ring_capacity;
    std::uint32_t reserved;
    RingBuffer<ActuatorToDcasSample> actuator_to_dcas;
    RingBuffer<LkasToDcasSample> lkas_to_dcas;
    RingBuffer<DcasToActuatorSample> dcas_to_actuator;
};
```

핵심 설계:

- `lkas_to_dcas`: LKAS 원본 제어값 업로드
- `dcas_to_actuator`: DCAS가 필터링한 최종 제어값 업로드
- `actuator_to_dcas`: actuator 속도/하드웨어 상태 업로드
- 각 링버퍼는 `head`, `tail`, `entries[64]`를 가진다.

### 3.3 최신 샘플 읽기 방식

DCAS와 Actuator는 모든 샘플을 순차 처리하지 않고 최신 샘플만 가져온다.

```cpp
template <typename T>
bool RtControlShm::pop_latest(RingBuffer<T>& ring, T& out_sample) {
    std::uint32_t head = ring.head.load(std::memory_order_acquire);
    std::uint32_t tail = ring.tail.load(std::memory_order_relaxed);
    if (tail == head) {
        return false;
    }
    std::uint32_t latest = head - 1;
    out_sample = ring.entries[latest % kRingCapacity];
    ring.tail.store(head, std::memory_order_release);
    return true;
}
```

의미:

- 오래된 제어 명령을 줄줄이 따라가지 않는다.
- 제어 시스템에서는 “가장 최신 입력”이 중요하므로 최신 샘플만 소비한다.
- `read_*` 계열은 샘플을 소비한다.
- 모니터링에는 소비하지 않는 `peek_*` 또는 `status`를 써야 한다.

포트폴리오 포인트:

- 저지연 제어값 전달을 위해 POSIX shared memory와 lock-free 스타일 ring buffer를 사용했다.
- 제어 루프는 backlog 처리보다 최신 샘플 추종을 우선하도록 설계했다.
- 대용량 카메라 데이터와 소형 제어 데이터를 분리해, 제어 경로의 지연과 복잡도를 줄였다.

## 4. LKAS가 원본 제어를 쓰는 위치

현재 LKAS raw control writer는 두 경로가 있다.

중요한 주의:

- `lkas_to_dcas` 링버퍼는 현재 단일 producer를 전제로 한 단순 `head` 증가 방식이다.
- LKAS decision server와 Vehicle Gateway가 동시에 같은 링버퍼에 쓰면 `head` 갱신 경쟁이 생길 수 있다.
- 실제 운용 구조에서는 raw control writer owner를 하나로 고정하는 것이 바람직하다.
- 이번 통합 테스트에서는 `vehicle --use-cpp-actuator` 경로에서 최신 `lkas_to_dcas` 샘플이 지속 갱신되는 것을 확인했다.
- 포트폴리오/최종 설계 설명에서는 “LKAS/Vehicle Gateway 중 한 곳이 LKAS raw control publisher 역할을 소유한다”고 표현하는 것이 정확하다.

### 4.1 LKAS decision server 경로

관련 파일:

- `/home/leo/Lkas/src/decision/server.py`
- `/home/leo/Lkas/src/integration/rt_control_shm.py`

LKAS decision server는 detection 결과를 읽고 controller로 제어값을 계산한다.

```python
control = self.controller.process_detection(detection)
```

그 다음 기존 control shared memory에도 쓰고, DCAS용 RT shared memory에도 raw control을 쓴다.

```python
if self._rt_writer:
    self._rt_writer.write_lkas_to_dcas(
        throttle=control.throttle,
        steering=control.steering,
    )
```

writer 내부에서는 `lkas_to_dcas` 링버퍼의 `head` 위치에 샘플을 쓴다.

```python
sample.timestamp_us = time.time_ns() // 1000
sample.lkas_throttle = float(throttle)
sample.lkas_steering = float(steering)

ring.entries[idx] = sample
ring.head = head + 1
```

### 4.2 Vehicle Gateway 경로

관련 파일:

- `/home/leo/Vehicle-jetracer/src/vehicle.py`
- `/home/leo/Vehicle-jetracer/src/rt_control_shm_writer.py`

`vehicle --use-cpp-actuator` 모드에서는 Python이 직접 하드웨어를 움직이지 않는다.

```python
def _update_vehicle_state(self):
    if self.use_cpp_actuator or self.car is None:
        return
    self.car.throttle = -self.throttle
    self.car.steering = -self.steering
```

대신 LKAS에서 받은 raw control을 공유메모리에 publish한다.

```python
def _publish_lkas_raw_control(self):
    if self.rt_control_writer is None:
        return
    self.rt_control_writer.write_lkas(self.throttle, self.steering)
```

그리고 LKAS control을 받을 때 다음처럼 동작한다.

```python
control = self.lkas.get_control(timeout=0.1)
if control is not None:
    self._set_steering(control.steering)
    if self.use_cpp_actuator:
        self._set_throttle(getattr(control, "throttle", self.throttle))
        self._publish_lkas_raw_control()
```

의미:

- Legacy 모드: Python Vehicle이 직접 `NvidiaRacecar`를 제어한다.
- C++ actuator 모드: Python Vehicle은 gateway 역할만 하고 raw control을 SHM에 올린다.
- 실제 하드웨어 인가는 RT Actuator만 수행한다.

포트폴리오 포인트:

- 기존 Python 직접 제어 경로를 깨지 않고 `--use-cpp-actuator` 플래그로 안전하게 새 RT 경로를 추가했다.
- Vehicle Gateway와 Actuator HAL의 책임을 분리해, Python은 카메라/통신, C++는 하드웨어 제어를 담당하도록 구조화했다.

## 5. DCAS bridge 코드 흐름

관련 파일:

- `/home/leo/ads-skynet/DCAS-PolicyEngine/src/dcas_rt_bridge.cpp`

실행 명령:

```bash
cd /home/leo/ads-skynet/DCAS-PolicyEngine/build
./dcas_rt_bridge --attentive true --reason none --dt 0.02 --period-ms 10
```

### 5.1 입력 파라미터

주요 CLI 입력:

```text
--attentive true|false
--reason none|phone|drowsy|unresponsive|intoxicated
--dt seconds
--period-ms n
--once
--verbose
```

현재 테스트에서는 인지팀 입력 대신 CLI mock으로 `is_attentive/reason`을 넣었다.

### 5.2 SHM 열기

DCAS bridge는 `/rt_control_shm`을 열고, 없으면 생성한다.

```cpp
rt_ipc::RtControlShm shm;
if (!shm.create_or_open(false) && !shm.create_or_open(true)) {
    std::cerr << "Failed to open rt_control_shm" << std::endl;
    return 1;
}
```

### 5.3 Actuator speed와 LKAS raw control 읽기

DCAS bridge는 loop마다 두 입력을 읽는다.

```cpp
if (shm.read_actuator_to_dcas(speed_sample)) {
    have_speed = true;
}

if (shm.read_lkas_to_dcas(lkas_sample)) {
    have_lkas = true;
}
```

두 입력이 모두 들어오기 전까지는 정책을 평가하지 않는다.

```cpp
if (!have_speed || !have_lkas) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    continue;
}
```

의미:

- DCAS는 차량 속도와 LKAS 제어값을 모두 알아야 정책을 계산한다.
- LKAS 값이 없으면 final control을 새로 쓰지 않는다.
- Actuator telemetry가 없으면 speed band 판단이 불완전하므로 대기한다.

### 5.4 speed를 정책 입력으로 변환

Actuator가 보낸 실제 속도는 Step B 정책의 `jetracer_input_0_4`로 변환된다.

```cpp
float speed_to_jetracer_input(float speed_kmh) {
    const float max_kmh = 40.0f;
    float scaled = speed_kmh * 0.4f / max_kmh;
    ...
    return scaled;
}
```

이 값은 Step B의 speed band와 시간 임계값 계산에 사용된다.

```cpp
tick.step_b.jetracer_input_0_4 = speed_to_jetracer_input(speed_sample.current_speed_kmh);
```

### 5.5 인지 입력과 LKAS 입력을 RuntimeTickInput으로 조립

인지 입력:

```cpp
tick.step_b.perception.is_attentive = is_attentive;
tick.step_b.perception.is_attentive_ts_ms = tick_ts;
tick.step_b.perception.reason = reason;
tick.step_b.perception.reason_ts_ms = tick_ts;
tick.step_b.delta_s = delta_s;
```

LKAS/Step C 입력:

```cpp
tick.step_c.previous_lkas_mode = lkas_mode;
tick.step_c.lkas_switch_event = dcas::LkasSwitchEvent::NONE;
tick.step_c.notebook_input_alive = notebook_input_alive;
tick.step_c.driver_override = driver_override;
tick.step_c.lkas_throttle = lkas_sample.lkas_throttle;
```

주의:

- 현재 bridge는 CLI mock이므로 `is_attentive_ts_ms`와 `reason_ts_ms`를 동일한 tick timestamp로 넣는다.
- 실제 인지팀 연동 시에는 별도 입력 adapter가 이 역할을 해야 한다.

### 5.6 정책 평가

```cpp
const dcas::RuntimeTickOutput output = runtime.Tick(tick);
lkas_mode = output.step_c.next_lkas_mode;
```

여기서 Step B/C가 수행된다.

- Step B: 운전자 상태 `OK/WARNING/ESCALATION/ABSENT` 계산
- Step C: 운전자 상태를 HMI/throttle limit/MRM/LKAS mode로 변환

### 5.7 throttle clamp와 steering pass-through

DCAS bridge의 핵심 필터 로직:

```cpp
const double raw_throttle = static_cast<double>(lkas_sample.lkas_throttle);
const double limit = output.step_c.throttle_limit;
const double final_throttle = speed_sample.hardware_fault
                                  ? 0.0
                                  : std::clamp(raw_throttle, 0.0, limit);
out.final_throttle = static_cast<float>(final_throttle);
out.final_steering = lkas_sample.lkas_steering;
out.is_valid = speed_sample.hardware_fault ? 0U : 1U;
shm.write_dcas_to_actuator(out);
```

의미:

- LKAS throttle은 그대로 actuator로 가지 않는다.
- DCAS가 계산한 `throttle_limit`을 상한으로 clamp된다.
- hardware fault가 있으면 throttle은 0이 되고 `is_valid=0`이 된다.
- steering은 DCAS에서 수정하지 않고 그대로 통과한다.

정책별 throttle gain:

```text
OK         -> 100%
WARNING    -> 70%
ESCALATION -> 30%
ABSENT     -> 0%
```

예시:

```text
LKAS throttle = 0.25
WARNING limit = 0.25 * 0.7 = 0.175
ESCALATION limit = 0.25 * 0.3 = 0.075
ABSENT limit = 0
```

포트폴리오 포인트:

- DCAS를 “controller”가 아니라 “safety filter”로 설계했다.
- LKAS의 판단을 완전히 대체하지 않고, 안전 정책에 따라 출력 범위를 제한했다.
- steering과 throttle을 분리해, 초기 버전에서는 longitudinal safety control에 집중하고 lateral command는 pass-through로 유지했다.

## 6. RT Actuator 코드 흐름

관련 파일:

- `/home/leo/ads-skynet/rt-actuator/src/main.cpp`
- `/home/leo/ads-skynet/rt-actuator/src/rt_actuator_hal.cpp`

실행 명령:

```bash
cd /home/leo/ads-skynet/rt-actuator
./build/rt_actuator --use-shm --serial /dev/ttyACM0 --serial-baud 115200
```

### 6.1 HAL 초기화

RT Actuator는 먼저 I2C 기반 HAL을 초기화한다.

```cpp
RtActuatorHal hal(hal_config);
if (!hal.is_ready()) {
    std::cerr << "Failed to initialize actuator HAL" << std::endl;
    return 1;
}
```

기본 설정:

```cpp
std::string i2c_path = "/dev/i2c-7";
int i2c1_addr = 0x40;
int i2c2_addr = 0x60;
float steering_gain = -0.65f;
float throttle_gain = 0.8f;
```

### 6.2 DCAS 최종 제어 읽기

`--use-shm` 모드에서는 `dcas_to_actuator`를 읽는다.

```cpp
if (use_shm) {
    if (shm.read_dcas_to_actuator(dcas_sample)) {
        last_dcas_ts = dcas_sample.timestamp_us;
    }
}
```

### 6.3 Watchdog

DCAS 샘플이 너무 오래되면 actuator는 throttle/steering을 0으로 만든다.

```cpp
constexpr std::uint64_t kDcasWatchdogTimeoutUs = 500000;  // 500ms
bool watchdog_ok = (last_dcas_ts > 0) && ((now - last_dcas_ts) <= kDcasWatchdogTimeoutUs);

float throttle = (watchdog_ok && dcas_sample.is_valid) ? dcas_sample.final_throttle : 0.0f;
float steering = (watchdog_ok && dcas_sample.is_valid) ? dcas_sample.final_steering : 0.0f;
```

의미:

- DCAS가 멈추면 actuator는 마지막 명령을 계속 유지하지 않는다.
- 500ms 이상 최신 DCAS 명령이 없으면 안전하게 0으로 간다.
- `is_valid=0`이면 즉시 0으로 간다.

포트폴리오 포인트:

- 상위 정책 노드 장애에 대비해 actuator 계층에 watchdog을 둬 fail-safe를 구현했다.
- 안전 정지는 DCAS가 아니라 가장 하위 actuator에서 보장하도록 설계했다.

### 6.4 하드웨어 인가

최종 throttle/steering은 HAL로 전달된다.

```cpp
hal.set_throttle(throttle);
hal.set_steering(steering);
```

`set_steering()`은 steering 값을 servo pulse width로 변환한다.

```cpp
float value = steering * config_.steering_gain + config_.steering_offset;
value = clamp(value, -1.0f, 1.0f);
float pulse_us = center_us + value * span_us;
std::uint16_t duty = pulse_us_to_counts(pulse_us, config_.steering_pwm_hz);
return set_pwm_duty(fd_steer_, config_.steering_channel, duty);
```

`set_throttle()`은 throttle 값을 motor PWM duty와 방향 제어 채널로 변환한다.

```cpp
float value = -clamp(throttle, -1.0f, 1.0f) * config_.throttle_gain;
std::uint16_t duty = scale_duty(std::abs(value));
```

그 뒤 PCA9685 채널 여러 개에 PWM duty를 쓴다.

```cpp
set_pwm_duty(fd_motor_, 0, duty);
set_pwm_duty(fd_motor_, 1, 4095);
...
```

의미:

- DCAS는 normalized control 값을 만든다.
- RT Actuator HAL은 normalized control 값을 실제 PWM/I2C 명령으로 변환한다.
- 하드웨어 상세는 HAL 안에 캡슐화된다.

### 6.5 속도 telemetry 업로드

RT Actuator는 serial에서 속도를 읽고 DCAS에 올린다.

```cpp
SerialSpeedReader serial_reader(opts.serial_path, opts.serial_baud);
if (!opts.serial_path.empty()) {
    serial_reader.start();
}
```

loop 안에서 최신 속도 값을 공유메모리에 쓴다.

```cpp
if (!opts.serial_path.empty() && shm.is_ready() && serial_reader.has_speed()) {
    rt_ipc::ActuatorToDcasSample telemetry{};
    telemetry.timestamp_us = now_us();
    telemetry.current_speed_kmh = serial_reader.latest_speed_kmh();
    telemetry.hardware_fault = 0;
    telemetry.reserved = 0;
    shm.write_actuator_to_dcas(telemetry);
}
```

의미:

- DCAS의 speed band 판단은 actuator가 올린 속도 값을 기반으로 한다.
- 속도는 카메라나 Python이 아니라 하위 하드웨어 계층에서 올라온다.

## 7. 실제 실행 시 데이터가 도는 순서

정상 모드 실행:

```bash
cd /home/leo/Lkas
lkas --broadcast
```

```bash
cd /home/leo/Vehicle-jetracer
vehicle --use-cpp-actuator
```

```bash
cd /home/leo/ads-skynet/DCAS-PolicyEngine/build
./dcas_rt_bridge --attentive true --reason none --dt 0.02 --period-ms 10
```

```bash
cd /home/leo/ads-skynet/rt-actuator
./build/rt_actuator --use-shm --serial /dev/ttyACM0 --serial-baud 115200
```

정상 상태 확인:

```bash
/home/leo/ads-skynet/rt-control-ipc/build/rt_control_shm_tool --mode status
```

예시 결과:

```text
latest actuator_to_dcas speed: 1.81 hardware_fault: 0
latest lkas_to_dcas throttle: 0.25 steering: -0.0775869
latest dcas_to_actuator final throttle: 0.25 steering: -0.0775869 valid: 1
```

해석:

- LKAS가 throttle `0.25`, steering `-0.0775869`를 냈다.
- DCAS 정상 집중 상태에서는 throttle을 제한하지 않았다.
- Actuator가 속도 `1.81km/h`를 DCAS에 피드백했다.
- `valid=1`, `hardware_fault=0`이므로 actuator는 이 값을 실제 하드웨어에 인가할 수 있다.

## 8. 정책 적용 예시

DCAS bridge를 단발 실행하면 정책별 필터 결과를 볼 수 있다.

OK:

```bash
./dcas_rt_bridge --once --attentive true --reason none --dt 0.02 --verbose
```

```text
lkas_throttle=0.25 throttle_limit=0.25 final_throttle=0.25 state=OK hmi=INFO
```

WARNING:

```bash
./dcas_rt_bridge --once --attentive false --reason phone --dt 4.0 --verbose
```

```text
lkas_throttle=0.25 throttle_limit=0.175 final_throttle=0.175 state=WARNING hmi=EOR
```

ESCALATION:

```bash
./dcas_rt_bridge --once --attentive false --reason phone --dt 7.0 --verbose
```

```text
lkas_throttle=0.25 throttle_limit=0.075 final_throttle=0.075 state=ESCALATION hmi=DCA
```

ABSENT:

```bash
./dcas_rt_bridge --once --attentive false --reason unresponsive --dt 0.1 --verbose
```

```text
lkas_throttle=0.25 throttle_limit=0 final_throttle=0 state=ABSENT hmi=MRM
```

이 결과는 DCAS가 LKAS를 대체하는 것이 아니라, LKAS 출력을 안전 정책에 맞게 제한하는 필터로 동작한다는 것을 보여준다.

## 9. 코드 읽을 때 기억할 핵심 포인트

핵심 1: 데이터 버스는 `rt_control_shm` 하나다.

```text
actuator_to_dcas
lkas_to_dcas
dcas_to_actuator
```

핵심 2: DCAS는 두 입력을 읽는다.

```text
LKAS raw control: throttle/steering
Actuator feedback: speed/hardware_fault
```

핵심 3: DCAS는 하나의 최종 출력을 쓴다.

```text
final_throttle = clamp(lkas_throttle, 0, throttle_limit)
final_steering = lkas_steering
```

핵심 4: Actuator는 DCAS 최종 출력만 믿는다.

```text
watchdog ok && is_valid -> final_throttle/final_steering apply
otherwise -> throttle=0, steering=0
```

핵심 5: 하드웨어 의존 코드는 HAL로 격리된다.

```text
normalized control -> PWM duty / servo pulse / I2C write
```

## 10. 포트폴리오에 쓸 수 있는 요약

프로젝트 설명 예시:

```text
LKAS가 계산한 원본 주행 제어값을 DCAS 정책 엔진이 실시간 shared memory 기반 필터로 감독하는 구조를 구현했다.
Python 기반 perception/LKAS 계층과 C++ 기반 actuator 계층 사이에 POSIX shared memory ring buffer를 두고,
DCAS는 운전자 부주의 상태에 따라 throttle을 100%/70%/30%/0%로 제한한다.
Actuator HAL은 DCAS 최종 명령만 하드웨어에 인가하며, 500ms watchdog과 is_valid 플래그로 상위 노드 장애 시 안전 정지하도록 설계했다.
```

기술 키워드:

- POSIX shared memory
- lock-free style ring buffer
- latest-sample control pipeline
- safety filter pattern
- Python-to-C++ control migration
- watchdog-based fail-safe actuator
- policy engine / actuator HAL separation
- steering pass-through, throttle clamping
- hardware feedback loop

설계 성과:

- Python LKAS와 C++ actuator 사이의 책임을 분리했다.
- 기존 LKAS 알고리즘을 유지하면서 DCAS 정책 필터를 중간에 삽입했다.
- 제어값 전달 경로를 `LKAS raw -> DCAS final -> Actuator apply`로 명확히 만들었다.
- 하위 actuator 계층에서 watchdog을 수행해 상위 정책 노드 장애에 대비했다.
- 실제 JetRacer에서 OK/WARNING/ESCALATION/ABSENT 정책별 throttle 제한이 공유메모리 파이프라인 위에서 정상 동작함을 확인했다.

## 11. 다음에 비슷한 구조를 만들 때 체크리스트

1. 제어 책임을 먼저 나눈다.
   - 판단 노드, 정책 필터 노드, 하드웨어 인가 노드를 분리한다.

2. 작은 제어값은 별도 저지연 IPC로 분리한다.
   - 카메라/이미지 같은 큰 데이터와 throttle/steering 같은 작은 제어 데이터는 같은 채널에 섞지 않는다.

3. 제어 루프는 최신 샘플 기준으로 설계한다.
   - 오래된 제어 입력을 순차 처리하면 지연이 누적될 수 있다.

4. actuator 계층에 watchdog을 둔다.
   - 상위 노드가 죽어도 마지막 명령이 계속 인가되지 않도록 한다.

5. 정책 필터는 원본 controller를 대체하지 않고 제한한다.
   - 기존 LKAS를 유지하면서 안전 정책을 추가하기 쉽다.

6. 모니터링 도구는 non-consuming read를 제공한다.
   - 실시간 시스템에서 debug read가 실제 소비자 동작을 방해하면 안 된다.

7. 테스트는 세 단계로 나눈다.
   - 정책 단위 테스트
   - SHM bridge 테스트
   - 실제 하드웨어 통합 테스트
