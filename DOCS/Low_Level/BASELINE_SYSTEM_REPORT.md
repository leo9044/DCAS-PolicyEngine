# Phase 0 Baseline System Report

작성일: 2026-05-27  
대상: Jetson Orin Nano bench camera pipeline  
목적: DCAS 하위 레벨 FFI/실시간성 실험의 기준선 고정

---

## 1. Baseline ID

| 항목 | 값 |
| --- | --- |
| Baseline ID | P0-BENCH-CAMERA-PIPELINE-20260527-1748 |
| Baseline Type | Bench camera pipeline |
| 측정 일시 | 2026-05-27T17:48:22+02:00 |
| Hostname | leo-rt |
| 차량 상태 | not connected / not driving |
| Camera | Intel RealSense D455f |
| Actuator mode | Jetson `rt_actuator --use-shm` process active, speed feedback mocked once via SHM |
| Physical actuator claim | none |
| LKAS 실행 여부 | yes, `lkas --broadcast` |
| Vehicle Gateway 실행 여부 | yes, `vehicle --use-cpp-actuator` |
| DCAS 실행 여부 | yes, `dcas_rt_bridge` |
| Viewer 실행 여부 | yes, `viewer`, HTTP `8080`, WebSocket `8081` |

이 snapshot은 실제 차량/MCU/physical actuator 검증이 아니라 `BENCH-CAMERA` Software-in-the-loop 기준선이다.

---

## 2. Scope

이 baseline의 목적:

- Camera/LKAS/Vehicle/Viewer가 만드는 실제 Jetson 부하를 기준선으로 고정한다.
- `LKAS -> rt_control_shm -> DCAS -> rt_control_shm -> actuator command` 경로가 살아 있는지 확인한다.
- actuator speed feedback은 실제 serial/I2C가 아니라 `rt_control_shm_tool --mode write_act --speed 0.0`으로 mock 입력을 한 번 넣어 DCAS bridge의 final command write를 활성화했다.

이 baseline에서 주장하지 않는 것:

- 실제 actuator I2C/PWM timing
- serial speed feedback timing
- physical motor response
- MCU heartbeat / relay cut / independent safety monitor
- vehicle safe-state behavior

---

## 3. System Info

기록 명령:

```bash
date --iso-8601=seconds
hostname
uname -a
nproc
cat /etc/os-release
cat /etc/nv_tegra_release
```

결과:

```text
date: 2026-05-27T17:48:22+02:00
hostname: leo-rt
kernel: Linux leo-rt 5.15.148-tegra #1 SMP PREEMPT Thu Sep 18 15:08:33 PDT 2025 aarch64 aarch64 aarch64 GNU/Linux
CPU count: 6
OS: Ubuntu 22.04.5 LTS (Jammy Jellyfish)
JetPack/L4T: R36.4.7, GCID 42132812, KERNEL_VARIANT oot
```

---

## 4. CPU / Memory / Governor

```text
CPU model: ARM Cortex-A78AE
CPU(s): 6, online 0-5
CPU max/min MHz: 1728.0000 / 115.2000
L3 cache: 4 MiB (2 instances)
CPU governor: cpu0-cpu5 schedutil

free -h:
- Mem total: 7.4 GiB
- Mem used: 4.2 GiB
- Mem free: 80 MiB
- Mem buff/cache: 3.2 GiB
- Mem available: 3.0 GiB
- Swap total: 3.7 GiB
- Swap used: 1.0 MiB
```

---

## 5. Camera / Video Devices

```text
lsusb:
- Bus 002 Device 003: ID 8086:0b5c Intel Corp. Intel(R) RealSense(TM) Depth Camera 455f

v4l2-ctl --list-devices:
- Intel(R) RealSense(TM) Depth Camera: /dev/video0 through /dev/video5, /dev/media1, /dev/media2
- NVIDIA Tegra Video Input Device: /dev/media0
```

---

## 6. Process Topology

```text
PID 57988  lkas            lkas --broadcast
PID 58052  python3.10      lkas.decision.run --detection-shm-name detection --control-shm-name control
PID 58087  python3.10      lkas.detection.run --method dl --image-shm-name image --detection-shm-name detection
PID 60106  vehicle         vehicle --use-cpp-actuator
PID 62144  dcas_rt_bridge  ./dcas_rt_bridge
PID 63179  rt_actuator     ./build/rt_actuator --use-shm
PID 132015 viewer          viewer
```

Observed scheduler notes:

```text
- Pipeline processes are currently SCHED_OTHER.
- No CPU affinity isolation is applied yet.
- Kernel migration threads are SCHED_FIFO priority 99.
- Many IRQ threads are SCHED_FIFO priority 50.
```

---

## 7. Viewer / ZMQ State

```text
LISTEN 0.0.0.0:8080 viewer HTTP
LISTEN 0.0.0.0:8081 viewer WebSocket
LISTEN 0.0.0.0:5557 LKAS broadcast
LISTEN 0.0.0.0:5558 action
LISTEN 0.0.0.0:5559 parameter
LISTEN 0.0.0.0:5560 parameter forward
LISTEN 0.0.0.0:5561 action forward
LISTEN 0.0.0.0:5562 vehicle status

Established:
- viewer -> LKAS broadcast 5557
- viewer -> LKAS action 5558
- viewer -> LKAS parameter 5559
- browser/node -> viewer WebSocket 8081
```

---

## 8. SHM Pipeline State

기록 명령:

```bash
od -An -td8 -N8 /dev/shm/image
od -An -td4 -j16 -N16 /dev/shm/image
od -An -td8 -N8 /dev/shm/detection
od -An -td8 -N8 /dev/shm/control
/home/leo/ads-skynet/rt-control-ipc/build/rt_control_shm_tool --mode status
```

결과:

```text
/dev/shm/image:
- frame_id: 113167
- width: 640
- height: 360
- channels: 3
- ready: 1

/dev/shm/detection:
- frame_id: 113162

/dev/shm/control:
- frame_id: 113162

rt_control_shm:
- actuator_to_dcas: head=1 tail=1 latest speed=0, age_ms about 18695 at capture
- lkas_to_dcas: head=122996 tail=122996 latest throttle=0.25 steering=-0.0526859 age_ms=13
- dcas_to_actuator: head=1836 tail=1835 latest final_throttle=0.25 final_steering=-0.0526859 valid=1 age_ms=4
```

Interpretation:

```text
Camera frame SHM is updating.
LKAS detection and decision SHM are updating.
LKAS raw control reaches rt_control_shm.
After one mock actuator speed sample, dcas_rt_bridge writes fresh final command samples.
```

---

## 9. Thermal / Load

기록 명령:

```bash
timeout 6s tegrastats --interval 1000
```

결과 요약:

```text
RAM: about 4646-4652 / 7620 MB
Swap: 1 / 3810 MB
CPU load: frequently high during DL camera pipeline, e.g. CPU1 at 92-100%
CPU frequency: up to 1728 MHz on several cores
GR3D_FREQ: 28-97%
CPU temperature: about 64.8-65.1 C
GPU temperature: about 66.1-66.5 C
```

---

## 10. IRQ / Kernel Noise Candidates

기록 명령:

```bash
cat /proc/interrupts
```

요약:

```text
- arch_timer distributed across CPU0-CPU5.
- xhci-hcd:usb1 is concentrated on CPU0 and has a high count. This is relevant for the RealSense USB camera.
- mmc0 is concentrated on CPU0.
- hsp and several Jetson device IRQs are concentrated on CPU0.
- uvcvideo kernel workers are active and observed on multiple CPUs.
```

This is acceptable for P0 observation. CPU/IRQ isolation phases should revisit USB/camera IRQ affinity explicitly.

---

## 11. Baseline Interpretation

```text
This is a Bench Camera Pipeline baseline.

The camera, LKAS detection, LKAS decision, Vehicle Gateway, Viewer, DCAS bridge, and rt_control_shm path are active.
The final DCAS command path is active after mock actuator speed input.
This baseline does not claim physical actuator timing, I2C/PWM timing, serial speed feedback, MCU heartbeat, or vehicle safe-state behavior.
It is valid evidence for Jetson Linux OS-level timing/memory interference under camera + DL workload.
```

Pass conditions for this Phase 0 baseline:

- System/kernel/L4T information recorded.
- Camera device and V4L2 nodes recorded.
- Pipeline process topology recorded.
- Viewer/ZMQ connectivity recorded.
- SHM frame/control freshness recorded.
- Thermal/GPU/CPU load recorded.
- IRQ candidates recorded.
- Bench-camera scope and non-claims are explicit.

