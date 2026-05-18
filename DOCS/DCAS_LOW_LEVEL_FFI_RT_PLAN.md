# DCAS 하위 레벨 FFI/실시간성 검증 프로젝트 계획

작성일: 2026-05-18  
대상 시스템: Jetson Orin Nano 계열 Linux/Ubuntu + DCAS Policy Engine + rt-control-ipc + rt-actuator + 향후 MCU 안전 모니터

---

## 1. 결론 먼저

이 프로젝트는 "DCAS를 ASIL-B로 인증했다" 또는 "Linux에서 hard real-time을 보장했다"라고 주장하는 프로젝트가 아니다.

가장 바람직한 목표는 다음이다.

> 고부하 AI/QM 워크로드가 같은 Jetson Linux 시스템에서 동작하더라도, DCAS 제어 브릿지와 Actuator 경로가 시간 간섭, 메모리 간섭, 실행 간섭으로부터 얼마나 보호되는지 실험적으로 증명하는 FFI evidence package를 만든다.

즉, 포트폴리오 문장으로는 아래가 안전하고 강하다.

> POSIX 실시간 API, Linux PREEMPT_RT, CPU/IRQ 격리, shared memory IPC, MCU heartbeat fail-safe를 결합하여 mixed-criticality SDV 제어 경로의 Freedom From Interference를 계측 기반으로 검증했다.

---

## 2. 비판적 검토

### 2.1 유지해야 할 핵심 아이디어

- DCAS/LKAS/Actuator를 필터 패턴으로 분리한 현재 구조는 좋다.
- `LKAS -> DCAS -> Actuator` 직렬 경로는 안전 감독 계층을 설명하기 쉽다.
- POSIX `clock_nanosleep(TIMER_ABSTIME)`, `mlockall`, `mmap`, `pthread`/`sched` 기반 구현은 Linux, QNX, RTOS 계열로 설명을 확장하기 좋다.
- `cyclictest`, `rtla timerlat/osnoise`, `ftrace`, `perf`를 사용한 계측 계획은 실시간성 주장을 데이터로 바꾸는 데 적합하다.
- 하위 MCU heartbeat는 Linux가 멈췄을 때의 실행 간섭 방어 논리로 매우 설득력 있다.

### 2.2 수정해야 할 위험한 표현

- "ASIL-B 파티션", "ASIL-D 파티션"은 포트폴리오에서는 조심해야 한다.
- ISO 26262 ASIL은 구현 스타일만으로 달성되는 것이 아니라 item definition, HARA, safety goal, technical safety concept, verification, tool qualification, safety case가 필요하다.
- Jetson Linux + PREEMPT_RT + Ubuntu 조합은 그 자체로 안전 인증 플랫폼이 아니다.
- 따라서 문서에서는 `ASIL-B-like timing critical control path`, `ASIL-D-like independent safety monitor prototype`, `ISO 26262-inspired FFI concept`처럼 표현한다.

### 2.3 가장 큰 기술적 한계

- `cpuset`은 CPU 스케줄링 격리에는 효과적이지만 GPU, DRAM bandwidth, memory controller, shared LLC, DMA 간섭을 완전히 막지는 못한다.
- `mlockall`은 page fault와 swap 위험을 줄이지만 cache eviction, DRAM contention, kernel internal latency를 제거하지 않는다.
- PREEMPT_RT는 worst-case latency를 낮추는 도구이지, 모든 상황에서 hard real-time deadline을 수학적으로 보장하는 인증 근거는 아니다.
- Zephyr MCU heartbeat는 fail-safe 검출 구조를 만들 수 있지만, 그 MCU 펌웨어도 별도 safety process 없이 ASIL-D라고 주장하면 안 된다.

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
- Linux 제어 노드 정지 또는 heartbeat 손실 시 MCU 안전 모니터가 FTTI 예산 내 안전 상태로 전이

### 3.2 포트폴리오 목표

최종 산출물은 단순한 데모 영상이 아니라 다음 증거 묶음이어야 한다.

- before/after latency histogram
- load condition별 CSV raw data
- `rtla timerlat/osnoise` 로그
- `ftrace`/`trace-cmd` 스케줄링 trace
- POSIX periodic loop 코드
- shared memory IPC 코드 설명
- Zephyr heartbeat safety monitor 코드
- FFI 관점의 실험 리포트

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

Independent Safety Monitor
  - Zephyr MCU heartbeat monitor
  - fail-safe output cut or safe command fallback
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

방어 대상:

- DCAS process hang
- rt-actuator hang
- Linux kernel panic
- shared memory writer 중단
- scheduler starvation
- 제어 loop가 살아 있지만 command freshness가 만료됨

적용 기술:

- DCAS -> Actuator freshness timestamp
- Actuator watchdog timeout
- Jetson -> MCU GPIO heartbeat
- MCU-side watchdog
- heartbeat 3회 또는 5회 누락 시 safe state
- actuator command timeout 시 throttle zero
- future option: CAN heartbeat, redundant serial heartbeat

증거:

- DCAS process kill 시 actuator safe state 전이 시간
- `dcas_rt_bridge` hang 시 final command timeout 감지
- Jetson heartbeat 중단 시 MCU takeover 시간
- forced kernel panic 또는 GPIO heartbeat 중단 시 MCU safe output 유지

주의:

- Linux 내부 오류를 Linux가 스스로 완전히 방어한다고 주장하지 않는다.
- 실행 간섭의 최종 방어는 독립 MCU가 맡는 구조로 설명한다.

---

## 6. 단계별 실행 로드맵

### Phase 0. 기준선 고정

목표:

- 현재 DCAS/LKAS/Actuator shared memory pipeline을 실험 기준선으로 고정한다.
- 어떤 프로세스가 어떤 CPU에서 어떤 주기로 도는지 기록한다.

작업:

- Jetson CPU 개수 확인: 현재 `nproc = 6`
- 프로세스 목록 및 CPU affinity 기록
- `rt_control_shm_tool --mode status` 상태 기록
- `dcas_rt_bridge` 정상/경고/escalation/mock reason test 재실행
- 현재 커널 버전, JetPack/L4T 버전, governor, thermal 상태 기록

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
- 실제 stack load: `lkas --broadcast`, `vehicle --use-cpp-actuator`, `dcas_rt_bridge`, `rt_actuator`

실험 매트릭스:

| Case | Kernel | Isolation | Load | 측정 대상 |
| --- | --- | --- | --- | --- |
| A | stock | none | idle | 기준선 |
| B | stock | none | CPU/GPU/mem | 취약성 |
| C | stock | cpuset/affinity | CPU/GPU/mem | 격리 효과 |
| D | PREEMPT_RT | cpuset/affinity | CPU/GPU/mem | RT 효과 |
| E | PREEMPT_RT | cpuset + IRQ tuning + nohz/rcu | full stack | 최종 |

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
| CPU1-3 | QM: LKAS, Vehicle, YOLO, VLM, viewer |
| CPU4 | DCAS periodic bridge |
| CPU5 | rt-actuator 또는 latency probe |

적용 후보:

```bash
taskset -c 4 sudo chrt -f 98 ./dcas_rt_bridge ...
taskset -c 5 sudo chrt -f 99 ./rt_actuator --use-shm ...
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

### Phase 5. MCU heartbeat fail-safe

목표:

- Linux 제어 경로가 멈춰도 하위 MCU가 독립적으로 안전 상태를 유지하는 구조를 만든다.

권장 구조:

```text
Jetson RT control process
  - toggles GPIO heartbeat every 10 ms
  - writes final command to shm
  - optionally sends serial/CAN heartbeat

Zephyr MCU
  - samples heartbeat
  - if heartbeat missing for 30-50 ms, enters SAFE_STATE
  - disables throttle output or commands neutral
  - logs takeover reason
```

상태 머신:

```text
INIT -> ARMED -> RUNNING -> HEARTBEAT_LOST -> SAFE_STATE
                    |              ^
                    v              |
                FAULT_DETECTED ----+
```

시험:

- `dcas_rt_bridge` kill
- `rt_actuator` kill
- shared memory writer stop
- GPIO heartbeat stop
- Jetson process CPU starvation
- optional: controlled kernel panic only on safe bench setup

성공 기준:

- heartbeat 3주기 누락 시 safe state 전이
- 총 fault reaction time 50 ms 이내 목표
- safe state 이후 throttle output zero 또는 relay cut 유지

주의:

- 실제 주행 중 kernel panic 유도는 위험하므로 바퀴를 띄운 bench에서만 수행한다.
- MCU takeover는 "ASIL-D 구현"이 아니라 "independent safety monitor prototype"이라고 표현한다.

---

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
      trace_collect/
    scripts/
      plot_latency.py
      summarize_rt_results.py
  safety-mcu/
    zephyr_heartbeat_monitor/
```

구현 항목:

- `rt_periodic_probe`: 10 ms POSIX loop latency logger
- `dcas_timing_logger`: 실제 `dcas_rt_bridge`에 timing log 옵션 추가
- `ffi_load_runner`: CPU/memory/GPU/LKAS 부하 조합 실행 스크립트
- `plot_latency.py`: histogram, CDF, tail latency plot
- `zephyr_heartbeat_monitor`: GPIO heartbeat monitor

### 7.2 문서

- `BASELINE_SYSTEM_REPORT.md`
- `FFI_TEST_MATRIX.md`
- `RT_LATENCY_RESULTS.md`
- `KERNEL_TRACE_ANALYSIS.md`
- `MCU_FAILSAFE_TEST_REPORT.md`
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
| safety | heartbeat timeout reaction | 30-50 ms 내 |
| evidence | reproducibility | command + raw CSV + plot 포함 |

장시간 검증 단계:

- smoke: 5분
- baseline: 30분
- stress: 1시간
- portfolio-grade: 4-8시간

---

## 9. 추천 실험 순서

1. 현재 stock kernel에서 `rt_periodic_probe` idle 측정
2. stock kernel에서 LKAS/Vehicle/DCAS/Actuator full stack 측정
3. stock kernel에서 CPU/GPU/memory stress 추가
4. `mlockall`, stack pre-fault, `TIMER_ABSTIME`, `SCHED_FIFO`, affinity 적용
5. cpuset/cgroup으로 QM/RT 분리
6. IRQ affinity 조정
7. PREEMPT_RT 적용 후 동일 실험 반복
8. `rtla`/`ftrace`로 tail latency 원인 분석
9. MCU heartbeat fail-safe bench test
10. 결과를 FFI 관점으로 재정리

---

## 10. 포트폴리오 서사

나쁜 서사:

> Linux에 PREEMPT_RT를 깔아서 ASIL-B 제어기를 만들었다.

좋은 서사:

> 비전 AI와 차량 제어가 같은 Jetson HPC에서 공존하는 mixed-criticality 상황을 만들고, QM workload가 DCAS/Actuator 제어 경로에 주는 간섭을 시간/메모리/실행 관점으로 분해했다. POSIX periodic loop, shared memory IPC, CPU/IRQ 격리, PREEMPT_RT, kernel tracing, MCU heartbeat fail-safe를 단계적으로 적용하여 Freedom From Interference를 계측 기반 evidence package로 구성했다.

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
- stack pre-fault
- `SCHED_FIFO` priority option
- CPU affinity option
- CSV output
- deadline miss count

이것이 완성되면 그 다음부터는 모든 주장이 숫자로 바뀐다.
