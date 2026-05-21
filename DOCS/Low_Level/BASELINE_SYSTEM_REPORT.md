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
| Baseline ID | TBD |
| Baseline Type | Clean / Shared-load |
| 측정 일시 | TBD |
| 측정자 | leo |
| 차량 상태 | bench / wheels-up / low-speed / driving |
| Actuator mode | dry-run / serial / shm |
| LKAS 실행 여부 | yes / no |
| Vehicle Gateway 실행 여부 | yes / no |
| DCAS 실행 여부 | yes / no |
| rt-actuator 실행 여부 | yes / no |

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
TBD
```

JetPack/L4T:

```bash
cat /etc/nv_tegra_release
```

결과:

```text
TBD
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
| 로그인 사용자 수 | TBD |
| 활성 사용자 | TBD |
| 사용자별 프로세스 수 | TBD |
| 실험 외 주요 프로세스 | TBD |
| 비고 | TBD |

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
TBD
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
TBD
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
TBD
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
TBD
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
TBD
```

---

## 11. 다음 단계

Phase 0 완료 후 진행:

- `rt_periodic_probe` 구현
- stock kernel idle latency 측정
- stock kernel shared-load latency 측정
- CPU/GPU/memory 부하 조건 추가
