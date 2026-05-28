# DCAS 하위 레벨 FFI/실시간성 검증 프로젝트 계획

작성일: 2026-05-18  
대상 시스템: Jetson Orin Nano 계열 Linux/Ubuntu + Camera/LKAS + DCAS Policy Engine + rt-control-ipc + rt-actuator dry-run/mock 경로

---

## 1. 결론 먼저

이 프로젝트는 "DCAS를 ASIL-B로 인증했다" 또는 "Linux에서 hard real-time을 보장했다"라고 주장하는 프로젝트가 아니다.

가장 바람직한 목표는 다음이다.

> 고부하 Camera/LKAS/AI/QM 워크로드가 같은 Jetson Linux 시스템에서 동작하더라도, DCAS 제어 브릿지와 Actuator dry-run/mock 경로가 시간 간섭과 메모리 간섭으로부터 얼마나 보호되는지 실험적으로 증명하는 FFI evidence package를 만든다.

즉, 포트폴리오 문장으로는 아래가 안전하고 강하다.

> POSIX 실시간 API, Linux PREEMPT_RT, CPU/IRQ 격리, shared memory IPC, camera/LKAS 부하, fault injection을 결합하여 mixed-criticality SDV 제어 경로의 시간/메모리 Freedom From Interference와 Linux 제어 경로 fault reaction behavior를 계측 기반으로 검증했다.

현재 1차 범위에서는 MCU heartbeat, 실제 actuator 출력 차단, 독립 안전 모니터 기반 실행 환경 격리는 제외한다. 이 항목들은 추후 차량/MCU 환경에서 확장 검증한다.

---

## 2. 비판적 검토

### 2.1 유지해야 할 핵심 아이디어

- DCAS/LKAS/Actuator를 필터 패턴으로 분리한 현재 구조는 좋다.
- `LKAS -> DCAS -> Actuator` 직렬 경로는 안전 감독 계층을 설명하기 쉽다.
- POSIX `clock_nanosleep(TIMER_ABSTIME)`, `mlockall`, `mmap`, `pthread`/`sched` 기반 구현은 Linux, QNX, RTOS 계열로 설명을 확장하기 좋다.
- `cyclictest`, `rtla timerlat/osnoise`, `ftrace`, `perf`를 사용한 계측 계획은 실시간성 주장을 데이터로 바꾸는 데 적합하다.
- 하위 MCU heartbeat는 Linux가 멈췄을 때의 실행 간섭 방어 논리로 매우 설득력 있으므로, 추후 확장 범위로 남겨둔다.
- fault injection과 stale-command/fallback 측정을 추가하면 "단순 튜닝 프로젝트"가 아니라 "Linux 제어 경로 결함 반응을 검증한 safety evidence project"로 격상된다.

### 2.2 수정해야 할 위험한 표현

- "ASIL-B 파티션", "ASIL-D 파티션"은 포트폴리오에서는 조심해야 한다.
- ISO 26262 ASIL은 구현 스타일만으로 달성되는 것이 아니라 item definition, HARA, safety goal, technical safety concept, verification, tool qualification, safety case가 필요하다.
- Jetson Linux + PREEMPT_RT + Ubuntu 조합은 그 자체로 안전 인증 플랫폼이 아니다.
- 따라서 문서에서는 `ASIL-B-like timing critical control path`, `ASIL-D-like independent safety monitor prototype`, `ISO 26262-inspired FFI concept`처럼 표현한다.

### 2.3 가장 큰 기술적 한계

- `cpuset`은 CPU 스케줄링 격리에는 효과적이지만 GPU, DRAM bandwidth, memory controller, shared LLC, DMA 간섭을 완전히 막지는 못한다.
- `mlockall`은 page fault와 swap 위험을 줄이지만 cache eviction, DRAM contention, kernel internal latency를 제거하지 않는다.
- PREEMPT_RT는 worst-case latency를 낮추는 도구이지, 모든 상황에서 hard real-time deadline을 수학적으로 보장하는 인증 근거는 아니다.
- 현재 Jetson + camera bench 환경에서는 실제 actuator I2C/PWM 출력, 차량 동역학, MCU 독립 fail-safe를 검증하지 않는다.
- Zephyr MCU heartbeat는 fail-safe 검출 구조를 만들 수 있지만, 추후 구현하더라도 그 MCU 펌웨어를 별도 safety process 없이 ASIL-D라고 주장하면 안 된다.

### 2.4 현재 1차 범위와 제외 범위

1차 범위는 Jetson Orin Nano 단독 장비에 camera를 연결한 bench 환경이다.

포함:

- Camera capture와 LKAS vision pipeline이 만드는 실제 CPU/GPU/memory 부하
- `Vehicle/LKAS -> rt_control_shm -> dcas_rt_bridge -> rt_control_shm -> rt_actuator dry-run/mock` 제어 데이터 경로
- POSIX periodic probe 기반 10 ms scheduling latency 측정
- CPU affinity, cgroup/cpuset, IRQ 관찰, PREEMPT_RT 비교
- page fault, CPU migration, deadline miss, SHM freshness/sequence 오류 계측
- `dcas_rt_bridge` kill/hang, SHM stale/corrupt, CPU/GPU/memory overload 같은 Linux 내부 fault injection

제외:

- 실제 차량 주행 검증
- 실제 actuator I2C/PWM 출력 인가
- serial speed feedback 실측
- MCU heartbeat, relay cut, actuator node 물리 격리
- Linux kernel panic 이후 독립 MCU safe-state 유지 검증

따라서 Jetson + camera 환경은 현재 1차 목표를 충족하기에 충분하다. 단, 이 결과는 `BENCH-CAMERA` evidence로 표기하고, 차량 탑재 Jetson에서 얻는 `VEHICLE-FULL` evidence와 구분한다.

### 2.5 Actuator 제외 bench 검증의 의미

현재 1차 범위는 Software-in-the-loop 성격의 bench 검증이다. 실제 actuator I2C/PWM 출력과 모터 구동을 제외하더라도, 이 단계는 독립적인 의미를 가진다.

차량 제어 경로를 나누면 다음과 같다.

```text
1. 센서/카메라 입력
2. 인지 및 판단 연산 (AI / 비전 / LKAS)
3. 제어 로직 연산 (DCAS) 및 OS scheduling 대기
4. I/O 통신 (I2C / Serial / CAN 등)
5. 모터 구동 (Actuator)
```

PREEMPT_RT, CPU affinity/cpuset, IRQ 관찰, `mlockall`, stack pre-fault, absolute periodic loop로 방어하려는 핵심 간섭은 3번 단계에서 발생한다.

- AI/LKAS가 같은 Jetson에서 CPU/GPU/memory bandwidth를 많이 사용한다.
- 그 부하가 DCAS/control thread의 wake-up을 늦춘다.
- 10 ms 제어 주기에서 control thread가 20 ms 뒤에 깨어나면, actuator가 아무리 빠르게 반응해도 이미 deadline을 놓친 것이다.

따라서 현재 단계의 질문은 "실제 모터가 잘 움직이는가?"가 아니다.

> AI와 제어 스레드가 같은 Jetson Linux 안에서 공존할 때, DCAS/control thread가 정해진 10 ms 주기 안에 I/O 통신 직전 단계까지 도달하도록 OS 레벨에서 시간/메모리 간섭을 충분히 억제했는가?

이 질문은 실제 actuator를 제외해도 검증 가능하다. 오히려 actuator 물리 반응, 차량 동역학, 외부 전원/배선 변수를 제외하므로, Jetson Linux 아키텍처의 FFI 특성을 더 선명하게 측정할 수 있다.

추후 차량/MCU 단계에서는 이 결과 위에 다음 항목을 추가한다.

- 실제 I/O 통신 지연
- actuator hardware reaction
- independent MCU heartbeat / relay cut
- physical safe-state output

즉, `BENCH-CAMERA` 단계는 전체 차량 safety case의 전부는 아니지만, SDV Linux compute node 내부 FFI argument의 핵심 증거다.

---

## 3. 프로젝트 목표

### 3.1 기술 목표

Jetson Linux에서 아래 조건을 만족하는지 단계별로 측정한다.

- DCAS/Actuator 제어 주기: 10 ms
- 목표 max scheduling latency: 1 ms 미만
- deadline miss: 정의한 시험 시간 동안 0회
- warm-up 이후 major/minor page fault: 0회
- RT thread CPU migration: 0회
- LKAS/DCAS/Actuator shared memory pipeline 정상 동작
- Linux 제어 노드 정지, SHM stale/corrupt, overload fault 발생 시 software fallback 또는 timeout-safe 반응 확인
- fault injection campaign에서 `T_inject`, `T_detect`, `T_react`, `T_safe_software`를 분리 측정

### 3.2 포트폴리오 목표

최종 산출물은 단순한 데모 영상이 아니라 다음 증거 묶음이어야 한다.

- before/after latency histogram
- load condition별 CSV raw data
- `rtla timerlat/osnoise` 로그
- `ftrace`/`trace-cmd` 스케줄링 trace
- POSIX periodic loop 코드
- shared memory IPC 코드 설명
- camera/LKAS 부하 조건과 mock/dry-run actuator 코드 설명
- fault injection harness와 software FTTI-style 측정 리포트
- FFI 관점의 실험 리포트

추후 확장 산출물:

- Zephyr heartbeat safety monitor 코드
- MCU fail-safe test report
- 실제 actuator output cut / relay cut 측정 리포트

---

## 4. 현재 시스템 기준 아키텍처

현재 구현된 실차 경로는 다음과 같다.

```text
Vehicle Gateway / Camera
        |
        v
LKAS Python
  - lane perception
  - raw throttle / steering
        |
        v
rt-control-ipc shared memory
        |
        v
DCAS Policy Engine / dcas_rt_bridge
  - driver state policy
  - throttle clamp
  - final throttle / steering write
        |
        v
rt-control-ipc shared memory
        |
        v
rt-actuator C++
  - final command read
  - serial / hardware output
  - actuator state upload
```

이 프로젝트는 위 경로를 "기능 구현"이 아니라 "mixed-criticality 실험 플랫폼"으로 확장한다.

추가할 계층은 다음이다.

```text
QM Load Partition
  - YOLO / VLM / stress-ng / GPU load / memory load

RT Control Partition
  - dcas_rt_bridge
  - rt-actuator
  - periodic latency probe

Dry-run / Mock Actuator Safety Fallback
  - command freshness timeout
  - throttle zero / neutral command fallback
  - no physical I2C/PWM output in bench scope
```

---

## 5. FFI 요구사항 매핑

### 5.1 메모리 간섭 방어

방어 대상:

- AI 프로세스가 제어 프로세스의 주소 공간을 침범
- 제어 루프 실행 중 page fault 발생
- shared memory 구조체 오염
- memory pressure로 인한 swap/reclaim 지연

적용 기술:

- 프로세스 주소 공간 분리
- POSIX shared memory 권한 최소화
- shared memory ABI 고정 및 version/magic/sequence 필드 검증
- `mlockall(MCL_CURRENT | MCL_FUTURE)`
- stack pre-fault
- dynamic allocation 금지 또는 초기화 단계로 제한
- `perf stat`으로 page fault 계측
- cgroup memory limit로 QM 부하 제한

증거:

- warm-up 이후 `major-faults = 0`
- warm-up 이후 RT loop 구간 `minor-faults = 0`
- shared memory sequence 오류 0회
- ASAN/UBSAN 또는 fuzz-style IPC parser test 통과

주의:

- CPU core 분리만으로 DRAM bandwidth, memory controller, GPU DMA 간섭이 사라지지는 않는다.
- 따라서 "메모리 간섭 완전 제거"가 아니라 "프로세스/IPC/page-fault 계층의 메모리 간섭을 완화하고 계측했다"라고 표현한다.

### 5.2 시간 간섭 방어

방어 대상:

- YOLO/VLM/GPU 부하가 DCAS 10 ms 주기를 밀어냄
- Python/LKAS/Viewer가 RT control path CPU 시간을 빼앗음
- IRQ, RCU callback, kernel thread가 RT core에 들어옴
- kernel non-preemptible section 때문에 scheduling latency 증가

적용 기술:

- `SCHED_FIFO` 또는 `SCHED_RR`
- `pthread_setschedparam` / `sched_setscheduler`
- `sched_setaffinity` 또는 `taskset`
- `cpuset`/cgroup 기반 QM/RT CPU 분리
- IRQ affinity 조정
- `isolcpus`, `nohz_full`, `rcu_nocbs` 부트 파라미터 검토
- PREEMPT_RT kernel
- `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` absolute periodic loop

증거:

- max latency < 1 ms
- 10 ms deadline miss 0회
- CPU migration 0회
- `rtla timerlat`에서 RT core worst-case latency 감소
- `rtla osnoise`에서 RT core OS noise 감소
- `ftrace`에서 긴 IRQ/softirq/scheduler blocking 구간 식별 및 개선

주의:

- PREEMPT_RT를 적용해도 GPU driver, firmware, DMA, thermal throttling, power management로 지연이 발생할 수 있다.
- 따라서 시험 조건을 명확히 고정해야 한다.

### 5.3 실행 간섭 방어

현재 1차 범위에서는 MCU에 actuator node를 물리적으로 격리하는 실행 환경 간섭 방어는 구현하지 않는다. 대신 Jetson 내부 Linux process fault에 대해 software watchdog, freshness timeout, dry-run/mock actuator fallback이 동작하는지 측정한다.

방어 대상:

- DCAS process hang
- rt-actuator hang
- shared memory writer 중단
- scheduler starvation
- 제어 loop가 살아 있지만 command freshness가 만료됨

적용 기술:

- DCAS -> Actuator freshness timestamp
- Actuator watchdog timeout
- actuator command timeout 시 throttle zero
- dry-run/mock actuator에서 neutral command fallback 기록
- future option: Jetson -> MCU GPIO heartbeat, CAN heartbeat, redundant serial heartbeat

증거:

- DCAS process kill 시 dry-run/mock actuator fallback 전이 시간
- `dcas_rt_bridge` hang 시 final command timeout 감지
- SHM writer 중단 시 stale sample timeout 감지
- fault injection 시점부터 software safe command가 선택되는 시점까지의 FTTI-style budget 측정

주의:

- Linux 내부 오류를 Linux가 스스로 완전히 방어한다고 주장하지 않는다.
- 실행 환경 간섭의 최종 방어는 추후 독립 MCU가 맡는 구조로 확장한다.
- 현재 단계에서는 "physical safe-state output applied"가 아니라 "software fallback command selected"까지만 검증한다.

### 5.4 Fault Injection + FTTI 측정

이 프로젝트의 신기성 보강 축은 fault injection campaign이다.

기본 개념:

```text
fault injection time
        |
        v
fault detection time
        |
        v
fault reaction start
        |
        v
software fallback command selected
```

측정할 시간:

- `T_inject`: 결함을 주입한 시각
- `T_detect`: DCAS, dry-run/mock Actuator, 또는 probe가 결함을 감지한 시각
- `T_react`: software fallback 전이를 시작한 시각
- `T_safe_software`: throttle zero 또는 neutral command가 software output으로 선택된 시각
- `Detection Latency = T_detect - T_inject`
- `Reaction Latency = T_safe_software - T_detect`
- `FTTI-style Consumption = T_safe_software - T_inject`

초기 목표:

- shared memory stale fault는 actuator watchdog timeout 이내 safe-state 진입
- process kill fault는 다음 control cycle 또는 watchdog timeout 이내 safe-state 진입
- CPU/GPU overload fault는 deadline miss count와 safe-state fallback 여부를 함께 기록

핵심 포인트:

- 현재 bench 범위의 FTTI-style 값은 물리 출력 차단 시간이 아니라 software fallback command 선택 시간이다.
- FTTI는 임의로 "달성했다"고 말하는 값이 아니라, fault별로 budget을 정의하고 그 budget을 얼마나 소비했는지 측정해야 한다.
- 단순 평균이 아니라 max, p99, p99.9, miss count를 함께 제시해야 한다.
- fault injection은 Jetson + camera bench에서는 dry-run/mock actuator 조건에서만 수행한다.

---

## 6. 단계별 실행 로드맵

### Phase 0. 기준선 고정

목표:

- 현재 DCAS/LKAS/Actuator shared memory pipeline을 실험 기준선으로 고정한다.
- 어떤 프로세스가 어떤 CPU에서 어떤 주기로 도는지 기록한다.

작업:

- Jetson CPU 개수 확인: 현재 `nproc = 6`
- bench 단독/공유 여부 확인 및 multi-user contamination check 수행
- 현재 로그인 사용자와 사용자별 프로세스 수 기록
- 다른 사용자 프로세스가 CPU/GPU/메모리/카메라/시리얼을 점유 중인지 확인
- 프로세스 목록 및 CPU affinity 기록
- `rt_control_shm_tool --mode status` 상태 기록
- `dcas_rt_bridge` 정상/경고/escalation/mock reason test 재실행
- 현재 커널 버전, JetPack/L4T 버전, governor, thermal 상태 기록

측정 조건 분리:

- `P0-BENCH-IDLE`: Jetson 단독/bench, camera/LKAS/DCAS/actuator 비활성
- `P0-BENCH-MOCK-PIPELINE`: camera 없이 SHM mock writer와 dry-run/mock actuator로 pipeline 활성
- `P0-BENCH-CAMERA-PIPELINE`: camera + LKAS + DCAS + dry-run/mock actuator 활성
- `P0-VEHICLE-FULL-PIPELINE`: 추후 차량 탑재 Jetson에서 camera/serial/I2C/actuator 포함

주의:

- 계정이 4개인 것 자체는 문제가 아니다.
- 문제는 다른 사용자의 프로세스가 CPU/GPU/메모리/IRQ/I/O/카메라/시리얼 자원을 점유해 latency tail을 오염시키는 것이다.
- 따라서 모든 latency/FTTI 결과에는 측정 당시 로그인 사용자, 주요 백그라운드 프로세스, GPU 사용 상태를 함께 기록한다.
- Phase 1 이후 성능 비교는 같은 조건끼리만 비교한다. 예를 들어 `BENCH-CAMERA stock`은 `BENCH-CAMERA isolated`, `BENCH-CAMERA PREEMPT_RT`와 비교하고, `VEHICLE-FULL stock`은 `VEHICLE-FULL isolated`, `VEHICLE-FULL PREEMPT_RT`와 비교한다.

산출물:

- `BASELINE_SYSTEM_REPORT.md`
- 프로세스 topology diagram
- 현재 latency/throughput baseline

### Phase 1. POSIX 10 ms periodic probe 작성

목표:

- DCAS와 별개로 OS scheduling latency만 측정할 수 있는 최소 C++ probe를 만든다.

필수 구현:

```cpp
mlockall(MCL_CURRENT | MCL_FUTURE);
prefault_stack();
sched_setscheduler(0, SCHED_FIFO, &param);
sched_setaffinity(0, ...);

next = now();
while (running) {
    next += 10ms;
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
    actual = now();
    latency = actual - next;
    log_csv(timestamp, latency, cpu, page_faults, seq);
}
```

측정:

```bash
sudo cyclictest -m -Sp95 -i10000 -h400 -D10m
sudo chrt -f 98 ./rt_periodic_probe --period-us 10000 --cpu 4 --duration 600
perf stat -e page-faults,minor-faults,major-faults,context-switches,cpu-migrations ./rt_periodic_probe
```

Phase 1 측정은 두 단계로 나누어 남긴다.

1. Non-root smoke run
   - 목적: probe 자체의 CSV 형식, deadline miss 계산, CPU affinity 옵션이 정상 동작하는지 확인한다.
   - 기대: `SCHED_FIFO` 설정은 실패할 수 있으며, 이 실패는 권한 확인 결과로 기록한다.

2. Root/RT-authorized run
   - 목적: 실제 `SCHED_FIFO` priority와 CPU pinning을 적용한 상태에서 같은 10 ms loop latency를 재측정한다.
   - 위치: non-root smoke run 다음에 수행하여 `SCHED_OTHER`와 `SCHED_FIFO`의 차이를 같은 probe/같은 deadline 정의로 비교한다.
   - 해석: 이 단계는 아직 cpuset/IRQ isolation 전이므로, tail latency가 남아도 실패가 아니라 Phase 3/4에서 줄여야 할 기준선으로 기록한다.
   - 권장 명령:

```bash
sudo /home/leo/ads-skynet/DCAS-PolicyEngine/build/rt_periodic_probe \
  --period-us 10000 \
  --duration 60 \
  --cpu 4 \
  --priority 80 \
  --deadline-us 1000 \
  --output /tmp/rt_periodic_probe_phase1_fifo80_cpu4_60s.csv
```

이 결과는 `DOCS/Low_Level/RT_LATENCY_RESULTS.md`에 아래 항목으로 남긴다.

- command line
- sample count와 CSV line count
- CPU pinning 유지 여부
- deadline miss row
- latency avg/p50/p99/max
- actual period max
- non-root smoke run 대비 변화

현재 Phase 1의 의미는 "RT 권한을 주면 tail latency가 완전히 사라진다"가 아니라, "같은 unisolated bench 환경에서 `SCHED_FIFO`가 p50/p99/max/miss count를 어떻게 바꾸는지 정량화한다"이다.

성공 기준:

- idle 상태에서 max latency 기록
- CPU/memory/GPU load 상태에서 max latency 기록
- deadline miss 조건 정의: `latency > 1000 us` 또는 `actual_period > 10000 us`

### Phase 2. 간섭 부하 재현

목표:

- QM workload가 RT control path에 주는 간섭을 의도적으로 재현한다.

부하 종류:

- CPU load: `stress-ng --cpu`
- memory load: `stress-ng --vm`
- IO load: log write / file copy
- GPU load: YOLO/VLM inference loop 또는 CUDA sample
- bench camera stack load: camera capture, `lkas --broadcast`, `vehicle --use-cpp-actuator`, `dcas_rt_bridge`, `rt_actuator --dry-run` 또는 mock actuator
- vehicle stack load: 추후 차량에서 `rt_actuator` 실제 I2C/PWM/serial 포함

실험 매트릭스:

| Case | Kernel | Isolation | Load | 측정 대상 |
| --- | --- | --- | --- | --- |
| A | stock | none | idle | 기준선 |
| B | stock | none | CPU/GPU/mem | 취약성 |
| C | stock | cpuset/affinity | CPU/GPU/mem | 격리 효과 |
| D | PREEMPT_RT | cpuset/affinity | CPU/GPU/mem | RT 효과 |
| E | PREEMPT_RT | cpuset + IRQ tuning + nohz/rcu | bench camera stack | 1차 최종 |
| F | PREEMPT_RT | cpuset + IRQ tuning + nohz/rcu | vehicle full stack | 추후 확장 |

성공 기준:

- 격리 전후 histogram 차이가 명확해야 한다.
- 단순 평균이 아니라 max, p99.9, p99.99, deadline miss count를 제시한다.

### Phase 3. CPU/IRQ/cgroup 격리

목표:

- QM 부하가 RT core에 scheduling pressure를 주지 못하도록 격리한다.

권장 CPU 배치:

| CPU | 용도 |
| --- | --- |
| CPU0 | housekeeping, kernel threads, IRQ 기본 |
| CPU1-3 | QM: Camera, LKAS, Vehicle, YOLO, VLM, viewer |
| CPU4 | DCAS periodic bridge |
| CPU5 | dry-run/mock rt-actuator 또는 latency probe |

적용 후보:

```bash
taskset -c 4 sudo chrt -f 98 ./dcas_rt_bridge ...
taskset -c 5 sudo chrt -f 99 ./rt_actuator --use-shm --dry-run ...
```

cgroup/cpuset 예시:

```bash
sudo mkdir -p /sys/fs/cgroup/qm.slice
sudo mkdir -p /sys/fs/cgroup/rt.slice
echo 1-3 | sudo tee /sys/fs/cgroup/qm.slice/cpuset.cpus
echo 4-5 | sudo tee /sys/fs/cgroup/rt.slice/cpuset.cpus
```

IRQ 확인:

```bash
cat /proc/interrupts
cat /proc/irq/*/smp_affinity_list
```

고급 부트 파라미터 후보:

```text
isolcpus=4,5 nohz_full=4,5 rcu_nocbs=4,5 irqaffinity=0-3
```

주의:

- `isolcpus`, `nohz_full`, `rcu_nocbs`는 잘못 적용하면 오히려 latency가 나빠질 수 있다.
- Jetson의 특정 IRQ/GPU driver thread가 특정 CPU를 요구할 수 있으므로 적용 전후를 반드시 계측한다.

### Phase 3 실행 요약 (Case D)

- **무엇을 수행했나:** `cpuset` 기반 CPU 격리(리얼타임 슬라이스 `rt.slice` -> CPU4, QM 슬라이스 `qm.slice` -> CPU0-3) 적용 후, GPU 부하(Case C 스트레스)와 함께 `rt_periodic_probe`를 권한 있는 상태로 실행하여 tail-latency를 측정함.
- **관련 스크립트:** [tools/ffi_load_runner/case_d.sh](tools/ffi_load_runner/case_d.sh), 적용 스크립트: [tools/ffi_load_runner/apply_isolation.sh](tools/ffi_load_runner/apply_isolation.sh)
- **로그 위치:** `/tmp/rt_tests/phase3/case_d_*/` (rt CSV 및 모니터 로그 포함)
- **측정 요약 (rt_periodic_probe, 60s):** samples=6000, deadline_misses=0, p50=22 µs, p99=259 µs, max=409 µs
- **비교(이전 Case C, privileged):** Case C 최종값 p50=24 µs, p99=294 µs, max=488 µs — Phase 3 격리 후 p99 및 max가 개선되어 tail이 줄어듦(≈16% max 개선).
- **시스템 상태:** dmesg에서 OOM/kill 징후 없음; cgroup 이동 및 cpuset 설정은 스크립트 로그에 기록됨.
- **결론 및 권장:** CPU/cgroup 수준의 격리는 GPU/DRAM 관련 간섭 전체를 제거하지는 못하지만 control-path tail latency를 유의미하게 낮춤. 다음 단계로 PREEMPT_RT 커널 적용(Phase 4 / Case E)을 통해 더 낮은 worst-case를 목표로 비교 실험을 진행할 것을 권장함.


### Phase 4. PREEMPT_RT 커널 및 kernel tracing

목표:

- stock kernel과 PREEMPT_RT kernel의 worst-case latency를 비교한다.

작업:

- NVIDIA Jetson Linux RT kernel 문서 기준으로 RT kernel 설치 또는 빌드
- `uname -a`, `/sys/kernel/realtime`, kernel config 기록
- `cyclictest` 장시간 측정
- `rtla timerlat top/hist` 측정
- `rtla osnoise top/hist` 측정
- `ftrace` 또는 `trace-cmd`로 긴 latency 원인 추적

명령 예시:

```bash
sudo rtla timerlat top -c 4-5 -d 10m
sudo rtla osnoise top -c 4-5 -d 10m
sudo trace-cmd record -e sched_switch -e irq_handler_entry -e irq_handler_exit -e timerlat:* sleep 60
sudo trace-cmd report
```

해석 포인트:

- PREEMPT_RT는 spinlock 계층을 rt_mutex/PI 기반으로 바꿔 preemption 가능 구간을 늘린다.
- threaded IRQ를 통해 interrupt context가 scheduler priority 체계에 더 잘 들어오게 된다.
- 하지만 모든 latency source가 사라지는 것은 아니므로 trace로 남는 tail latency를 설명해야 한다.

### Phase 5. Fault Injection + FTTI budget verification

목표:

- Linux 제어 경로가 멈추거나 stale/corrupt sample을 만들 때 dry-run/mock actuator가 timeout-safe fallback을 선택하는지 검증한다.
- 결함 주입 시점부터 software safe command 선택까지의 시간을 fault별로 측정한다.
- "정상 동작한다"가 아니라 "정의한 software FTTI-style budget 안에서 fallback command를 선택한다"를 데이터로 입증한다.

1차 bench 권장 구조:

```text
Jetson RT control processes
  - writes final command to shm

Dry-run / mock actuator
  - checks command freshness
  - if command is stale, selects throttle zero / neutral command
  - logs detection/reaction/safe-software timestamps
```

상태 머신:

```text
INIT -> ARMED -> RUNNING -> STALE_COMMAND -> SAFE_SOFTWARE
                    |              ^
                    v              |
                FAULT_DETECTED ----+
```

시험:

| Fault ID | 주입 결함 | 주입 방법 | 기대 감지 주체 | 기대 안전 반응 |
| --- | --- | --- | --- | --- |
| FI-01 | DCAS process crash | `SIGKILL dcas_rt_bridge` | dry-run/mock actuator watchdog | final command stale -> throttle zero |
| FI-02 | DCAS process hang | `SIGSTOP dcas_rt_bridge` 또는 debug flag | dry-run/mock actuator watchdog | final command stale -> throttle zero |
| FI-03 | Actuator process crash | `SIGKILL rt_actuator --dry-run` | fault injector / external monitor | actuator process loss 기록 |
| FI-04 | SHM stale sample | DCAS write 중단 | actuator freshness checker | throttle zero |
| FI-05 | SHM corrupted sample | invalid magic/version/sequence injection | DCAS/Actuator IPC validator | sample reject + safe fallback |
| FI-06 | LKAS raw command stuck high | fixed high throttle write | DCAS clamp | policy limit 이하로 제한 |
| FI-07 | camera/LKAS overload | camera + LKAS vision load | RT probe / watchdog | latency tail, stale 여부 기록 |
| FI-08 | CPU overload | stress-ng / fork bomb 제한 환경 | RT probe / watchdog | deadline miss 기록, 필요 시 safe fallback |
| FI-09 | GPU/memory pressure | YOLO/VLM/stress memory | RT probe / watchdog | latency tail 기록 |
| FI-10 | controlled kernel panic | 추후 vehicle/MCU bench only | MCU heartbeat monitor | independent safe-state 유지 |

성공 기준:

- stale command watchdog timeout 이내 software safe command 선택
- process kill/hang fault는 다음 control cycle 또는 watchdog timeout 이내 감지
- safe software state 이후 throttle zero 또는 neutral command 유지
- fault별 `T_inject`, `T_detect`, `T_react`, `T_safe_software` CSV 기록
- fault별 max, p99, p99.9 FTTI-style consumption 계산
- fault별 pass/fail 기준을 `software_FTTI_budget_ms`로 명시

주의:

- 현재 Jetson + camera 범위에서는 kernel panic, GPIO heartbeat loss, relay cut은 수행하지 않는다.
- 실제 주행 중 kernel panic 유도는 위험하므로 추후에도 바퀴를 띄운 bench에서만 수행한다.
- MCU takeover는 추후 확장 시에도 "ASIL-D 구현"이 아니라 "independent safety monitor prototype"이라고 표현한다.
- `SIGKILL`, `SIGSTOP`, SHM corrupt 등은 dry-run/mock actuator 조건을 기본으로 한다.

## 7. 구현 산출물 제안

### 7.1 새 코드

권장 디렉터리:

```text
ads-skynet/
  rt-control-ipc/
  rt-actuator/
  DCAS-PolicyEngine/
    tools/
      rt_periodic_probe/
      ffi_load_runner/
      fault_injector/
      ftti_analyzer/
      trace_collect/
    scripts/
      plot_latency.py
      summarize_rt_results.py
  safety-mcu/              # future extension
    zephyr_heartbeat_monitor/
```

구현 항목:

- `rt_periodic_probe`: 10 ms POSIX loop latency logger
- `dcas_timing_logger`: 실제 `dcas_rt_bridge`에 timing log 옵션 추가
- `ffi_load_runner`: CPU/memory/GPU/LKAS 부하 조합 실행 스크립트
- `fault_injector`: process kill/hang, SHM stale/corrupt, camera/LKAS overload, load spike를 재현하는 fault injection runner
- `ftti_analyzer`: `T_inject`, `T_detect`, `T_react`, `T_safe_software` 로그를 모아 FTTI-style consumption을 계산하는 분석 도구
- `plot_latency.py`: histogram, CDF, tail latency plot
- `zephyr_heartbeat_monitor`: GPIO heartbeat monitor, 추후 확장

### 7.2 문서

- `BASELINE_SYSTEM_REPORT.md`
- `FFI_TEST_MATRIX.md`
- `RT_LATENCY_RESULTS.md`
- `FAULT_INJECTION_MATRIX.md`
- `SOFTWARE_FTTI_BUDGET_REPORT.md`
- `KERNEL_TRACE_ANALYSIS.md`
- `MCU_FAILSAFE_TEST_REPORT.md` (future extension)
- `PORTFOLIO_SUMMARY.md`

---

## 8. 평가 지표

| 항목 | 지표 | 목표 |
| --- | --- | --- |
| 주기 | nominal period | 10 ms |
| latency | max scheduling latency | < 1 ms |
| tail | p99.99 latency | 실험별 비교 |
| deadline | miss count | 0회 |
| memory | major fault | 0회 |
| memory | minor fault after warm-up | 0회 |
| scheduling | CPU migration | 0회 |
| IPC | stale sample | 0회 또는 timeout-safe |
| safety | software fallback reaction | watchdog budget 이내 |
| fault injection | FTTI-style consumption | fault별 software budget 이내 |
| fault injection | detection latency | fault별 p99/p99.9/max 제시 |
| fault injection | safe-state correctness | software throttle zero 또는 neutral command 100% |
| evidence | reproducibility | command + raw CSV + plot 포함 |

장시간 검증 단계:

- smoke: 5분
- baseline: 30분
- stress: 1시간
- portfolio-grade: 4-8시간

---

## 9. 추천 실험 순서

1. 현재 stock kernel에서 `rt_periodic_probe` idle 측정
2. stock kernel에서 `BENCH-MOCK-PIPELINE` 또는 `BENCH-CAMERA-PIPELINE` 측정
3. stock kernel에서 CPU/GPU/memory stress 추가
4. `mlockall`, stack pre-fault, `TIMER_ABSTIME`, `SCHED_FIFO`, affinity 적용
5. cpuset/cgroup으로 QM/RT 분리
6. IRQ affinity 조정
7. PREEMPT_RT 적용 후 동일 실험 반복
8. `rtla`/`ftrace`로 tail latency 원인 분석
9. fault injection harness 구현
10. fault별 software FTTI-style budget verification
11. 결과를 FFI 관점으로 재정리
12. 추후 차량/MCU 환경에서 heartbeat fail-safe bench test 확장

---

## 10. 포트폴리오 서사

나쁜 서사:

> Linux에 PREEMPT_RT를 깔아서 ASIL-B 제어기를 만들었다.

좋은 서사:

> 비전 AI와 차량 제어가 같은 Jetson HPC에서 공존하는 mixed-criticality 상황을 만들고, QM workload가 DCAS/Actuator 제어 경로에 주는 간섭을 시간/메모리 관점으로 분해했다. POSIX periodic loop, shared memory IPC, CPU/IRQ 격리, PREEMPT_RT, kernel tracing을 단계적으로 적용하여 Freedom From Interference를 계측 기반 evidence package로 구성했다.

더 좋은 서사:

> 여기에 fault injection campaign을 추가하여 DCAS crash, actuator hang, shared memory stale/corruption, camera/LKAS overload, CPU/GPU overload 상황에서 fault detection부터 software fallback command 선택까지의 FTTI-style consumption을 측정했다. 즉, 단순히 latency를 낮춘 것이 아니라 Linux 제어 경로 결함 발생 후 fallback 반응 시간까지 검증했다.

더 강한 한 줄:

> I did not just reduce jitter; I built an interference argument for an SDV control path and backed it with kernel-level measurements.

---

## 11. 핵심 리스크와 대응

| 리스크 | 영향 | 대응 |
| --- | --- | --- |
| Jetson RT kernel 설치 실패 | Phase 4 지연 | Phase 1-3만으로도 stock vs isolation evidence 확보 |
| GPU 부하가 CPU 격리와 무관하게 latency 유발 | tail latency 증가 | limitation으로 명시하고 GPU/memory interference를 별도 분석 |
| root 권한/부트 파라미터 실수 | 시스템 부팅 문제 | 변경 전 현재 boot entry 백업, 단계별 적용 |
| `SCHED_FIFO` runaway | 시스템 lock-up | watchdog shell, priority 제한, CPU 하나는 housekeeping으로 남김 |
| 측정 시간이 짧음 | evidence 약함 | smoke/30min/1h/8h 단계 구분 |
| ASIL 표현 과장 | 전문성 저하 | "ISO 26262-inspired", "ASIL-like", "evidence package"로 표현 |
| bench 결과를 차량 결과로 과장 | evidence 신뢰도 저하 | `BENCH-CAMERA`와 `VEHICLE-FULL` 결과를 명확히 분리 |
| fault injection 중 실제 차량 움직임 | 물리적 위험 | 현재 범위에서는 dry-run/mock actuator만 허용 |
| fault 주입 시각 불명확 | FTTI 데이터 신뢰도 저하 | monotonic timestamp, GPIO marker, CSV event log 동시 기록 |

---

## 12. 참고 레퍼런스

### 공식 문서

- Linux kernel PREEMPT_RT theory of operation: https://docs.kernel.org/core-api/real-time/theory.html
- Linux kernel real-time documentation index: https://docs.kernel.org/core-api/real-time/index.html
- Linux ftrace documentation: https://docs.kernel.org/trace/ftrace.html
- Linux rtla timerlat documentation: https://docs.kernel.org/tools/rtla/rtla-timerlat.html
- Linux osnoise tracer documentation: https://docs.kernel.org/trace/osnoise-tracer.html
- Linux cpuset documentation: https://docs.kernel.org/admin-guide/cgroup-v1/cpusets.html
- Linux nohz documentation: https://docs.kernel.org/timers/no_hz.html
- Linux real-time group scheduling: https://docs.kernel.org/scheduler/sched-rt-group.html
- The Open Group POSIX `clock_nanosleep`: https://pubs.opengroup.org/onlinepubs/009696899/functions/clock_nanosleep.html
- The Open Group POSIX memory locking definitions in `<sys/mman.h>`: https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_mman.h.html
- The Open Group POSIX `mmap`: https://pubs.opengroup.org/onlinepubs/9799919799/functions/mmap.html
- The Open Group POSIX scheduling model and `pthread_setschedparam` semantics: https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html
- The Open Group POSIX `pthread_setschedparam`: https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_setschedparam.html
- NVIDIA Jetson Linux Real-Time Kernel: https://docs.nvidia.com/jetson/archives/r36.5/DeveloperGuide/SD/Kernel/RealTimeKernel.html
- Zephyr safety overview: https://docs.zephyrproject.org/latest/safety/safety_overview.html
- Zephyr safety documentation index: https://docs.zephyrproject.org/latest/safety/index.html
- Zephyr peripherals documentation: https://docs.zephyrproject.org/latest/hardware/peripherals/index.html
- AUTOSAR Functional Safety Measures overview: https://www.autosar.org/fileadmin/standards/R19-11/CP/AUTOSAR_EXP_FunctionalSafetyMeasures.pdf
- AUTOSAR Main Requirements: https://autosar.org/fileadmin/standards/R22-11/FO/AUTOSAR_RS_Main.pdf

### 대표 논문과 연구 문헌

- Steve Vestal, "Preemptive Scheduling of Multi-criticality Systems with Varying Degrees of Execution Time Assurance", RTSS 2007, DOI: https://doi.org/10.1109/RTSS.2007.47
- Alan Burns and Robert I. Davis, "A Survey of Research into Mixed Criticality Systems", ACM Computing Surveys, DOI: https://doi.org/10.1145/3131347
- Burns and Davis author version: https://www-users.york.ac.uk/~rd17/papers/CSURreviewMCS.pdf
- Linux Foundation Real-Time Linux documentation: https://wiki.linuxfoundation.org/realtime/documentation/start
- Linux Foundation PREEMPT_RT technical details: https://wiki.linuxfoundation.org/realtime/documentation/technical_details/start

---

## 13. 다음 액션

가장 먼저 할 일은 문서 작업이 아니라 Phase 1 probe 구현이다.

추천 첫 번째 커밋:

```text
feat(rt): add POSIX 10ms periodic latency probe
```

최소 기능:

- `clock_nanosleep(TIMER_ABSTIME)` 기반 10 ms loop
- `mlockall`

---

### Phase 2 실행 체크리스트 (Actionable)

목표: Phase 1에서 수집된 `RT_LATENCY_RESULTS.md`를 바탕으로, 의도적 QM 부하를 가해 latency tail과 deadline miss를 재현하고, 격리/튜닝 조치의 효과를 계량적으로 측정한다.

핵심 산출물:
- `DOCS/Low_Level/PHASE2_RT_RESULTS.md` (CSV 링크, 명령행, histogram, p50/p99/p99.9/p99.99, deadline miss)
- raw CSV files in `/tmp/rt_tests/` with descriptive filenames

단계별 체크리스트:
1. 준비
  - Phase 1 결과 (`DOCS/Low_Level/RT_LATENCY_RESULTS.md`) 확인
  - 측정용 디렉터리 생성: `mkdir -p /tmp/rt_tests/phase2`
2. 부하 시나리오 스크립트 작성
  - `tools/ffi_load_runner`에 `case_b.sh`, `case_c.sh`, `case_d.sh` 스크립트 추가
  - 각 스크립트는 load 시작 → 안정화 30s → `rt_periodic_probe` 실행 60s → load stop 순서로 동작
3. 측정 실행 예시 (stock kernel)

```bash
mkdir -p /tmp/rt_tests/phase2
sudo /home/leo/ads-skynet/DCAS-PolicyEngine/build/rt_periodic_probe \
  --period-us 10000 --duration 60 --cpu 4 --priority 80 \
  --output /tmp/rt_tests/phase2/phase2_stock_caseB_cpu4.csv
# in parallel, start load script
sudo ./tools/ffi_load_runner/case_b.sh &
```

4. 측정 실행 예시 (isolation + PREEMPT_RT)

```bash
# apply cpuset/cgroup and IRQ affinity as defined in Phase 3
sudo ./tools/ffi_load_runner/apply_isolation.sh
sudo /home/leo/ads-skynet/DCAS-PolicyEngine/build/rt_periodic_probe \
  --period-us 10000 --duration 60 --cpu 4 --priority 80 \
  --output /tmp/rt_tests/phase2/phase2_preemptrt_caseD_cpu4.csv
sudo ./tools/ffi_load_runner/case_d.sh &
```

5. 수집 및 분석
  - 모든 CSV를 `tools/plot_latency.py`로 가공하여 histogram과 CDF 생성
  - `tools/summarize_rt_results.py`로 p50/p99/p99.9/p99.99/max/deadline-miss 계산
  - 결과 요약을 `DOCS/Low_Level/PHASE2_RT_RESULTS.md`로 작성

6. 검토 항목
  - 각 케이스별 CPU pinning이 실제 유지되었는지 확인 (`taskset -pc <pid>`)
  - perf stat으로 page-faults/major-faults 확인
  - CPU migration, IRQ/softirq 스파이크는 `trace-cmd`로 추적

권장 파일 템플릿: `DOCS/Low_Level/PHASE2_RT_RESULTS.md`

```
# Phase2 RT Results

## Test metadata
- command: ...
- kernel: ...
- boot params: ...
- note: ...

## Case B (stock, load)
- CSV: /tmp/rt_tests/phase2/...
- p50: ... p99: ... p99.9: ... p99.99: ... max: ...
- deadline misses: N

## Plots
- histogram: path
- cdf: path

## Interpretation
- short conclusions and next tuning step
```

---

다음 단계로는 `tools/rt_periodic_probe` 빌드 확인 및 `tools/ffi_load_runner`의 간단한 `case_b.sh` 스크립트를 추가해 드릴까요?
- stack pre-fault
- `SCHED_FIFO` priority option
- CPU affinity option
- CSV output
- deadline miss count

이것이 완성되면 그 다음에는 fault injection harness를 붙인다.

두 번째 커밋:

```text
feat(safety): add fault injection and FTTI event logger
```

최소 기능:

- `T_inject`, `T_detect`, `T_react`, `T_safe_software` monotonic timestamp 기록
- `dcas_rt_bridge` kill/hang 주입
- SHM stale sample 주입
- camera/LKAS overload 주입
- fault별 software FTTI-style budget pass/fail 리포트

이 두 축이 완성되면 프로젝트는 단순 jitter 튜닝이 아니라 "Jetson + camera bench 환경에서 interference와 Linux 제어 경로 fault reaction을 모두 측정한 safety evidence project"가 된다.
