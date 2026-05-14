# DCAS-LKAS-RT Actuator Integration Test Report

작성일: 2026-05-14

## 1. 목적

본 문서는 DCAS 정책이 실제 LKAS -> DCAS -> RT Actuator -> JetRacer 제어 경로에 정상 반영되는지 확인하기 위해 수행한 테스트 방법, 명령어, 관측 결과, 평가를 기록한다.

핵심 검증 대상은 다음이다.

- LKAS가 생성한 원본 `lkas_throttle/lkas_steering`이 `rt_control_shm`을 통해 DCAS로 전달되는지 확인
- DCAS가 운전자 상태 정책에 따라 `final_throttle`을 제한하는지 확인
- DCAS가 steering은 수정하지 않고 그대로 전달하는지 확인
- RT Actuator가 DCAS 최종 제어값을 읽고 하드웨어 경로를 유지하는지 확인
- DCAS 정책 테스트와 실제 공유메모리 통합 테스트가 모두 통과하는지 확인

MRM의 LKAS 상태 머신 실제 연동은 본 테스트의 범위에서 제외한다. 단, DCAS가 `ABSENT/MRM` 상태에서 `final_throttle=0`으로 만드는지는 확인한다.

## 2. 테스트 전제

테스트 환경:

- LKAS 실행 경로: `/home/leo/Lkas`
- Vehicle Gateway 실행 경로: `/home/leo/Vehicle-jetracer`
- DCAS 실행 경로: `/home/leo/ads-skynet/DCAS-PolicyEngine/build`
- RT Actuator 실행 경로: `/home/leo/ads-skynet/rt-actuator`
- SHM 확인 도구: `/home/leo/ads-skynet/rt-control-ipc/build/rt_control_shm_tool`

실행 순서상 주의:

- `vehicle --use-cpp-actuator`는 LKAS의 `detection`/`control` shared memory가 생성된 뒤 실행해야 한다.
- Vehicle을 너무 빨리 실행하면 `Detection shared memory 'detection' not found`로 실패할 수 있다.
- `rt_control_shm_tool --mode read_*`는 샘플을 소비하므로 모니터링에는 `status`를 사용한다.
- `rt_actuator`는 현재 사용자 `leo`가 필요한 그룹에 속해 있어 `sudo` 없이 실행 가능했다.

## 3. 기본 4프로세스 실행

### 3.1 LKAS 실행

```bash
cd /home/leo/Lkas
lkas --broadcast
```

확인 로그:

```text
Detection Server Started
Decision Server Started
rt_control_shm writer ready (LKAS -> DCAS)
```

평가:

- Detection server와 Decision server가 정상 시작됐다.
- Decision server가 `rt_control_shm` writer를 준비했다.
- 이후 Vehicle Gateway가 프레임을 넣으면 LKAS 원본 제어가 DCAS로 올라갈 준비가 됐다.

### 3.2 Vehicle Gateway 실행

```bash
cd /home/leo/Vehicle-jetracer
vehicle --use-cpp-actuator
```

확인 로그:

```text
Actuator mode: C++ rt-actuator
Camera ready!
C++ actuator mode enabled (Python actuation disabled)
rt_control_shm LKAS writer initialized
LKAS initialized
Starting vehicle loop
Sending frames to LKAS via shared memory
```

평가:

- Python Vehicle은 직접 모터를 제어하지 않는 C++ actuator mode로 실행됐다.
- 카메라 프레임이 LKAS로 전달됐다.
- LKAS 원본 제어를 `rt_control_shm`에 쓰는 writer가 초기화됐다.

### 3.3 DCAS bridge 실행

```bash
cd /home/leo/ads-skynet/DCAS-PolicyEngine/build
./dcas_rt_bridge --attentive true --reason none --dt 0.02 --period-ms 10
```

평가:

- 정상 집중 상태를 가정한 DCAS bridge가 실행됐다.
- DCAS는 LKAS 원본 제어와 actuator telemetry를 읽고 `dcas_to_actuator`에 최종 제어를 쓴다.

### 3.4 RT Actuator 실행

```bash
cd /home/leo/ads-skynet/rt-actuator
./build/rt_actuator --use-shm --serial /dev/ttyACM0 --serial-baud 115200
```

평가:

- `dcas_to_actuator`의 최종 throttle/steering을 읽는 actuator 경로가 실행됐다.
- serial 기반 속도 telemetry가 `actuator_to_dcas`로 업로드됐다.

## 4. 기본 파이프라인 상태 확인

명령어:

```bash
/home/leo/ads-skynet/rt-control-ipc/build/rt_control_shm_tool --mode status
```

관측 결과:

```text
latest actuator_to_dcas speed: 2.17 hardware_fault: 0
latest lkas_to_dcas throttle: 0.25 steering: -0.0822016
latest dcas_to_actuator final throttle: 0.25 steering: -0.0821577 valid: 1
```

평가:

- LKAS 원본 throttle `0.25`가 DCAS로 전달됐다.
- 정상 집중 상태에서 DCAS 최종 throttle도 `0.25`로 유지됐다.
- steering은 DCAS에서 수정되지 않고 거의 동일하게 통과했다.
- actuator speed telemetry가 DCAS로 올라왔다.
- `hardware_fault=0`, `valid=1`이므로 기본 경로는 정상이다.

## 5. DCAS 정책 반영 테스트

테스트 방식:

1. 정상 실행 중인 `dcas_rt_bridge`만 잠시 중지한다.
2. LKAS, Vehicle, RT Actuator는 그대로 유지한다.
3. `dcas_rt_bridge --once`로 정책 케이스를 단발 실행한다.
4. verbose 출력으로 `state`, `hmi`, `throttle_limit`, `final_throttle`, `final_steering`을 확인한다.
5. 테스트 후 정상 bridge를 다시 실행한다.

기존 DCAS bridge PID 확인:

```bash
pgrep -af dcas_rt_bridge
```

기존 DCAS bridge 중지:

```bash
kill <dcas_rt_bridge_pid>
```

### 5.1 OK 케이스

명령어:

```bash
cd /home/leo/ads-skynet/DCAS-PolicyEngine/build
./dcas_rt_bridge --once --attentive true --reason none --dt 0.02 --verbose
```

관측 결과:

```text
speed_kmh=0
lkas_throttle=0.25
throttle_limit=0.25
final_throttle=0.25
final_steering=-0.0732834
state=OK
hmi=INFO
valid=1
```

평가:

- 정상 집중 상태에서 DCAS는 LKAS throttle을 제한하지 않았다.
- `final_throttle == lkas_throttle`이므로 OK 정책은 정상이다.
- HMI는 `INFO`로 유지됐다.

### 5.2 WARNING 케이스

명령어:

```bash
cd /home/leo/ads-skynet/DCAS-PolicyEngine/build
./dcas_rt_bridge --once --attentive false --reason phone --dt 4.0 --verbose
```

관측 결과:

```text
speed_kmh=0
lkas_throttle=0.25
throttle_limit=0.175
final_throttle=0.175
final_steering=-0.0757638
state=WARNING
hmi=EOR
valid=1
```

평가:

- LOW speed band에서 `dt=4.0`은 WARNING 진입 조건을 만족한다.
- WARNING 정책에 따라 throttle이 `0.25 * 0.7 = 0.175`로 제한됐다.
- HMI는 `EOR`로 출력됐다.
- steering은 그대로 전달됐다.

### 5.3 ESCALATION 케이스

명령어:

```bash
cd /home/leo/ads-skynet/DCAS-PolicyEngine/build
./dcas_rt_bridge --once --attentive false --reason phone --dt 7.0 --verbose
```

관측 결과:

```text
speed_kmh=0
lkas_throttle=0.25
throttle_limit=0.075
final_throttle=0.075
final_steering=-0.0788651
state=ESCALATION
hmi=DCA
valid=1
```

평가:

- LOW speed band에서 `dt=7.0`은 ESCALATION 진입 조건을 만족한다.
- ESCALATION 정책에 따라 throttle이 `0.25 * 0.3 = 0.075`로 제한됐다.
- HMI는 `DCA`로 출력됐다.
- WARNING보다 더 강한 throttle 제한이 적용됐다.

### 5.4 Timer 기반 ABSENT 케이스

명령어:

```bash
cd /home/leo/ads-skynet/DCAS-PolicyEngine/build
./dcas_rt_bridge --once --attentive false --reason phone --dt 11.0 --verbose
```

관측 결과:

```text
speed_kmh=0
lkas_throttle=0.25
throttle_limit=0
final_throttle=0
final_steering=-0.0856615
state=ABSENT
hmi=MRM
valid=1
```

평가:

- LOW speed band에서 `dt=11.0`은 ABSENT 진입 조건을 만족한다.
- ABSENT 정책에 따라 throttle이 0으로 제한됐다.
- HMI는 `MRM`으로 출력됐다.
- MRM 실제 LKAS 연동은 별도 범위지만, DCAS 출력 정책은 정상이다.

### 5.5 Critical reason 기반 ABSENT 케이스

명령어:

```bash
cd /home/leo/ads-skynet/DCAS-PolicyEngine/build
./dcas_rt_bridge --once --attentive false --reason unresponsive --dt 0.1 --verbose
```

관측 결과:

```text
speed_kmh=0
lkas_throttle=0.25
throttle_limit=0
final_throttle=0
final_steering=-0.0727737
state=ABSENT
hmi=MRM
valid=1
```

평가:

- `reason=unresponsive`는 critical reason으로 즉시 ABSENT를 유도했다.
- 매우 짧은 `dt=0.1`에서도 timer와 무관하게 throttle이 0으로 제한됐다.
- critical reason 경로는 정상이다.

## 6. 정상 모드 복구 확인

정책 단발 테스트 후 정상 DCAS bridge를 재실행했다.

```bash
cd /home/leo/ads-skynet/DCAS-PolicyEngine/build
./dcas_rt_bridge --attentive true --reason none --dt 0.02 --period-ms 10
```

복구 후 SHM 확인:

```bash
/home/leo/ads-skynet/rt-control-ipc/build/rt_control_shm_tool --mode status
```

관측 결과:

```text
latest actuator_to_dcas speed: 1.81 hardware_fault: 0
latest lkas_to_dcas throttle: 0.25 steering: -0.0775869
latest dcas_to_actuator final throttle: 0.25 steering: -0.0775869 valid: 1
```

평가:

- 정상 집중 모드로 복구 후 `final_throttle`이 다시 LKAS 원본 throttle과 같아졌다.
- steering도 그대로 통과했다.
- actuator telemetry도 계속 갱신됐다.

## 7. C++ 정책 단위 테스트

명령어:

```bash
cd /home/leo/ads-skynet/DCAS-PolicyEngine
ctest --test-dir build --output-on-failure
```

관측 결과:

```text
Test project /home/leo/ads-skynet/DCAS-PolicyEngine/build
    Start 1: dcas_policy_tests
1/1 Test #1: dcas_policy_tests ................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 1
```

평가:

- C++ 정책 단위 테스트가 통과했다.
- 실제 SHM 통합 테스트 결과와 정책 단위 테스트가 서로 모순되지 않는다.

## 8. 최종 프로세스 상태

명령어:

```bash
pgrep -af 'lkas|vehicle|dcas_rt_bridge|rt_actuator'
```

관측 결과:

```text
lkas --broadcast
python3.10 -m lkas.decision.run ...
python3.10 -m lkas.detection.run ...
vehicle --use-cpp-actuator
./build/rt_actuator --use-shm --serial /dev/ttyACM0 --serial-baud 115200
/home/leo/ads-skynet/DCAS-PolicyEngine/build/dcas_rt_bridge --attentive true --reason none --dt 0.02 --period-ms 10
```

평가:

- 테스트 종료 후 4개 핵심 프로세스가 정상 모드로 살아 있다.
- DCAS bridge는 정상 집중 입력으로 복구되어 있다.

## 9. 최종 평가

테스트 결과, MRM 실제 연동을 제외한 현재 범위에서 다음이 확인됐다.

- LKAS -> DCAS raw control 업로드 정상
- Actuator -> DCAS speed/hardware 상태 업로드 정상
- DCAS -> Actuator final control 업로드 정상
- OK 상태에서 throttle/steering 통과 정상
- WARNING 상태에서 throttle 70% 제한 정상
- ESCALATION 상태에서 throttle 30% 제한 정상
- ABSENT 상태에서 throttle 0 제한 정상
- Critical reason에서 즉시 ABSENT 진입 및 throttle 0 제한 정상
- steering은 DCAS 정책에서 수정하지 않고 통과 정상
- 테스트 후 정상 주행 모드 복구 정상
- `dcas_policy_tests` 통과

따라서 현재 구현은 문서 기준의 핵심 DCAS 정책 필터 동작을 실제 공유메모리 파이프라인 위에서 정상 수행한다고 평가한다.

남은 별도 검증 항목:

- MRM event queue를 통한 DCAS -> LKAS 실제 MRM 요청 연동
- `is_attentive/reason`을 CLI mock이 아니라 실제 인지팀 입력으로 연결했을 때의 end-to-end 검증
- 장시간 주행 중 timestamp age, watchdog, serial speed 안정성 확인
