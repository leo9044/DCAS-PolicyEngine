# Phase 0 Baseline System Report

작성일: 2026-05-21  
대상: Jetson Orin Nano shared development host  
목적: DCAS 하위 레벨 FFI/실시간성 실험의 기준선 고정

---

## 1. Baseline 원칙

이 리포트는 성능 개선을 증명하기 전에 현재 시스템 상태를 고정하기 위한 문서다.

이 Jetson은 여러 사용자가 함께 쓰는 공유 장비이므로, baseline은 반드시 두 종류로 분리한다.

- `Clean baseline`: 다른 사용자 프로세스를 최소화한 통제 상태
- `Shared-load baseline`: 실제 팀 개발 환경처럼 다른 사용자/백그라운드 프로세스가 존재하는 상태

이후 모든 결과는 같은 조건끼리만 비교한다.

---

## 2. 실험 조건

| 항목 | 값 |
| --- | --- |
| Baseline ID | P0-SHARED-20260522-1344 |
| Baseline Type | Shared-load |
| 측정 일시 | 2026-05-22T13:44:43+02:00 |
| 측정자 | leo |
| 차량 상태 | not running |
| Actuator mode | not active |
| LKAS 실행 여부 | no |
| Vehicle Gateway 실행 여부 | no |
| DCAS 실행 여부 | no |
| rt-actuator 실행 여부 | no |

---

## 3. 시스템 정보

기록 명령:

```bash
date --iso-8601=seconds
hostname
uname -a
nproc
cat /etc/os-release
```

결과:

```text
date: 2026-05-22T13:44:43+02:00
hostname: jetracer
kernel: Linux jetracer 5.15.148-tegra #1 SMP PREEMPT Thu Sep 18 15:08:33 PDT 2025 aarch64 aarch64 aarch64 GNU/Linux
CPU count: 6
OS: Ubuntu 22.04.5 LTS (Jammy Jellyfish)
```

JetPack/L4T:

```bash
cat /etc/nv_tegra_release
```

결과:

```text
# R36 (release), REVISION: 4.7, GCID: 42132812, BOARD: generic, EABI: aarch64, DATE: Thu Sep 18 22:54:44 UTC 2025
# KERNEL_VARIANT: oot
TARGET_USERSPACE_LIB_DIR=nvidia
TARGET_USERSPACE_LIB_DIR_PATH=usr/lib/aarch64-linux-gnu/nvidia
```

---

## 4. Multi-User Contamination Check

목적:

- 다른 사용자의 프로세스가 CPU/GPU/메모리/I/O/카메라/시리얼 자원을 점유하고 있는지 확인한다.
- latency tail 또는 FTTI 결과를 오염시킬 수 있는 외부 변수를 기록한다.

기록 명령:

```bash
who
w
ps -eo user,pid,ppid,psr,pri,ni,policy,stat,comm,args --sort=user,pid
ps -eo user= | sort | uniq -c
```

결과 요약:

| 항목 | 값 |
| --- | --- |
| 로그인 사용자 수 | 1 |
| 활성 사용자 | siwoo (`:0`, GNOME session) |
| 사용자별 프로세스 수 | root 248, siwoo 88, leo 24, avahi 2, kernoops 2, 기타 system user 소수 |
| 실험 외 주요 프로세스 | GNOME/GDM desktop session, system services, kernel IRQ threads |
| 비고 | Codex 기본 샌드박스에서는 PID namespace가 분리되어 실제 host process가 보이지 않으므로, host 관측은 escalation된 read-only command로 수행함 |

---

## 5. CPU / Scheduler / Affinity 상태

기록 명령:

```bash
lscpu
ps -eLo user,pid,tid,psr,pri,rtprio,policy,stat,comm --sort=psr,pid
chrt -p $$
taskset -pc $$
```

결과:

```text
lscpu summary:
- Architecture: aarch64
- CPU model: ARM Cortex-A78AE
- CPU(s): 6
- Online CPU list: 0-5
- Thread(s) per core: 1
- Core(s) per cluster: 3
- Cluster(s): 2
- CPU max MHz: 1728.0000
- CPU min MHz: 115.2000
- L1d cache: 384 KiB (6 instances)
- L1i cache: 384 KiB (6 instances)
- L2 cache: 1.5 MiB (6 instances)
- L3 cache: 4 MiB (2 instances)
- NUMA node(s): 1
- NUMA node0 CPU(s): 0-5

CPU governor:
- cpu0-cpu5: schedutil

Current shell scheduling:
- policy: SCHED_OTHER
- priority: 0
- affinity: 0-5

Observed host scheduler notes:
- Many kernel IRQ threads are SCHED_FIFO priority 50.
- migration threads are SCHED_FIFO priority 99.
- No DCAS/LKAS/rt-actuator RT thread was active during this baseline.
```

---

## 6. GPU / 메모리 / Thermal 상태

기록 명령:

```bash
free -h
tegrastats --interval 1000
```

권장:

- `tegrastats`는 10초 이상 관찰하고 대표 구간을 붙인다.
- 실험 중에는 별도 로그 파일로 저장한다.

결과:

```text
free -h:
- Mem total: 7.4 GiB
- Mem used: 2.6 GiB
- Mem free: 2.9 GiB
- Mem buff/cache: 2.0 GiB
- Mem available: 4.6 GiB
- Swap total: 3.7 GiB
- Swap used: 0 B

tegrastats 5-second sample:
- RAM: about 2845-2850 / 7620 MB
- Swap: 0 / 3810 MB
- CPU load: roughly 1-40% per core during sample
- CPU frequency: 729-1267 MHz during sample
- GR3D_FREQ: 0%
- Temperature: CPU about 44.7-45.0 C, GPU about 45.8-46.1 C
```

---

## 7. IRQ / Kernel Noise 후보

기록 명령:

```bash
cat /proc/interrupts
for f in /proc/irq/*/smp_affinity_list; do echo "$f: $(cat "$f")"; done
```

결과:

```text
IRQ summary:
- arch_timer interrupt is distributed across CPU0-CPU5.
- Many device IRQs are currently concentrated on CPU0.
- Examples observed on CPU0: mmc0, hsp, xhci, i2c, GPU-related gk20a IRQ threads.
- This is acceptable for Phase 0 observation, but later RT-core isolation experiments should explicitly revisit IRQ affinity.

Representative /proc/interrupts observations:
- arch_timer: CPU0 40714, CPU1 50846, CPU2 36931, CPU3 36239, CPU4 37276, CPU5 38624
- mmc0: CPU0 87305, CPU1-CPU5 0
- xhci-hcd:usb1: CPU0 1040, CPU1-CPU5 0
- hsp: CPU0 30187, CPU1-CPU5 0
```

---

## 8. DCAS-LKAS-Actuator Pipeline 상태

실행 프로세스:

```bash
pgrep -af 'lkas|vehicle|dcas_rt_bridge|rt_actuator|rt_control_shm_tool'
```

SHM 상태:

```bash
/home/leo/ads-skynet/rt-control-ipc/build/rt_control_shm_tool --mode status
/home/leo/ads-skynet/rt-control-ipc/build/rt_control_shm_tool --mode read_lkas
/home/leo/ads-skynet/rt-control-ipc/build/rt_control_shm_tool --mode read_dcas
/home/leo/ads-skynet/rt-control-ipc/build/rt_control_shm_tool --mode read_act
```

결과:

```text
Process check:
- No active `lkas --broadcast`
- No active `vehicle --use-cpp-actuator`
- No active `dcas_rt_bridge`
- No active `rt_actuator`

SHM status:
- `/home/leo/ads-skynet/rt-control-ipc/build/rt_control_shm_tool --mode status`
- Result: `Failed to open rt_control_shm`

Interpretation:
- DCAS-LKAS-Actuator pipeline was not running during this Phase 0 snapshot.
- This baseline records the shared host idle/desktop state, not the full driving pipeline state.
- A second Phase 0 snapshot should be captured with LKAS, Vehicle Gateway, DCAS bridge, and rt-actuator running.
```

---

## 9. Baseline Pass/Fail 기준

Phase 0은 성능 목표를 달성하는 단계가 아니라, 측정 조건을 재현 가능하게 고정하는 단계다.

Pass 조건:

- 시스템/커널/JetPack/L4T 정보가 기록됨
- multi-user 상태가 기록됨
- 주요 프로세스와 CPU affinity가 기록됨
- GPU/메모리/thermal 상태가 기록됨
- IRQ 분포가 기록됨
- SHM pipeline 상태가 기록됨
- Clean baseline 또는 Shared-load baseline 여부가 명확함

Fail 조건:

- 다른 사용자 부하가 있었는지 알 수 없음
- 어떤 프로세스가 실행 중이었는지 재현할 수 없음
- SHM pipeline 상태가 빠짐
- 측정 조건이 Clean인지 Shared-load인지 불명확함

---

## 10. 관찰 및 메모

```text
This is a Shared-load baseline, not a Clean baseline.

Reasons:
- Host GUI session is active under user `siwoo`.
- GNOME/GDM and desktop service processes are running.
- CPU frequency governor is `schedutil`, not fixed performance mode.
- DCAS/LKAS/Actuator pipeline is not running.
- SHM segment is absent.

This snapshot is still useful as P0-SHARED because it documents the real shared Jetson development environment immediately after boot.
It should not be compared directly against a future Clean isolated PREEMPT_RT measurement.
```

---

## 11. 다음 단계

Phase 0에서 아직 남은 것:

- `P0-SHARED-PIPELINE-*`: LKAS, Vehicle Gateway, DCAS bridge, rt-actuator를 실행한 상태의 shared-load pipeline baseline 캡처
- 가능하면 `P0-CLEAN-*`: 다른 사용자/GUI/불필요 프로세스를 최소화한 clean baseline 캡처

그 다음 진행:

- `rt_periodic_probe` 구현
- stock kernel idle latency 측정
- stock kernel shared-load latency 측정
- CPU/GPU/memory 부하 조건 추가
