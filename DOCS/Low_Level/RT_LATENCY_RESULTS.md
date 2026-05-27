# RT Latency Results

작성일: 2026-05-27  
대상: Jetson Orin Nano bench camera environment  
Phase: 1 - POSIX 10 ms periodic probe smoke test

---

## 1. Probe

Binary:

```bash
/home/leo/ads-skynet/DCAS-PolicyEngine/build/rt_periodic_probe
```

Build:

```bash
cmake -S /home/leo/ads-skynet/DCAS-PolicyEngine -B /home/leo/ads-skynet/DCAS-PolicyEngine/build
cmake --build /home/leo/ads-skynet/DCAS-PolicyEngine/build --target rt_periodic_probe -j2
```

Implemented behavior:

- `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` absolute periodic loop
- `mlockall(MCL_CURRENT | MCL_FUTURE)` attempt
- stack pre-fault
- optional CPU affinity via `--cpu`
- optional `SCHED_FIFO` via `--priority`
- CSV output with sequence, expected timestamp, actual timestamp, latency, actual period, CPU, page faults, deadline miss
- summary statistics: sample count, miss count, average, p50, p99, max latency, max actual period

CSV columns:

```text
seq,expected_us,actual_us,latency_us,actual_period_us,cpu,minor_faults,major_faults,deadline_miss
```

Deadline miss definition in this smoke test:

```text
latency_us > deadline_us OR actual_period_us > period_us + deadline_us
```

Current default threshold:

```text
period_us = 10000
deadline_us = 1000
```

---

## 2. Smoke Test Results

### P1-SMOKE-IDLE-DEFAULT-20260527

Command:

```bash
/home/leo/ads-skynet/DCAS-PolicyEngine/build/rt_periodic_probe \
  --period-us 10000 \
  --duration 2 \
  --deadline-us 1000 \
  --output /tmp/rt_periodic_probe_phase1_idle.csv
```

Summary:

```text
samples=200
deadline_misses=10
latency_avg_us=220.43
latency_p50_us=74
latency_p99_us=2325.32
latency_max_us=2771
actual_period_max_us=12692
```

CSV sanity check:

```text
header + 200 samples = 201 lines
```

### P1-SMOKE-CPU4-20260527

Command:

```bash
/home/leo/ads-skynet/DCAS-PolicyEngine/build/rt_periodic_probe \
  --period-us 10000 \
  --duration 2 \
  --cpu 4 \
  --deadline-us 1000 \
  --output /tmp/rt_periodic_probe_phase1_cpu4.csv
```

Summary:

```text
samples=200
deadline_misses=12
latency_avg_us=294.595
latency_p50_us=72
latency_p99_us=3148.07
latency_max_us=11128
actual_period_max_us=21128
```

### P1-SMOKE-FIFO-PERMISSION-CHECK-20260527

Command:

```bash
/home/leo/ads-skynet/DCAS-PolicyEngine/build/rt_periodic_probe \
  --period-us 10000 \
  --duration 1 \
  --cpu 4 \
  --priority 80 \
  --deadline-us 1000 \
  --output /tmp/rt_periodic_probe_phase1_fifo_attempt.csv
```

Result:

```text
warning: sched_setscheduler(SCHED_FIFO,80) failed: Operation not permitted
samples=100
deadline_misses=6
latency_avg_us=232.24
latency_p50_us=71.5
latency_p99_us=2932.96
latency_max_us=3325
actual_period_max_us=13255
```

Interpretation:

- Non-root execution can collect latency CSV.
- RT scheduling requires privilege, `CAP_SYS_NICE`, or `sudo chrt`/equivalent setup.
- The current unisolated bench camera environment already shows millisecond-scale latency tail events.

### P1-RT-AUTHORIZED-FIFO80-CPU4-60S-20260527

Goal:

```text
Run the same 10 ms probe with CPU4 pinning and SCHED_FIFO priority 80.
```

Command:

```bash
sudo /home/leo/ads-skynet/DCAS-PolicyEngine/build/rt_periodic_probe \
  --period-us 10000 \
  --duration 60 \
  --cpu 4 \
  --priority 80 \
  --deadline-us 1000 \
  --output /tmp/rt_periodic_probe_phase1_fifo80_cpu4_60s.csv
```

Summary:

```text
samples=6000
deadline_misses=4
latency_avg_us=47.4545
latency_p50_us=23
latency_p99_us=275
latency_max_us=2721
actual_period_max_us=12697
```

CSV sanity check:

```text
header + 6000 samples = 6001 lines
```

CPU pinning check:

```text
cpu=4 for all 6000 samples
```

Deadline miss rows:

```text
3525,9642332343,9642333620,1277,11259,4,438,0,1
4475,9651832343,9651833467,1124,11101,4,438,0,1
5289,9659972343,9659973974,1631,11414,4,438,0,1
5803,9665112343,9665115064,2721,12697,4,438,0,1
```

Worst sample:

```text
seq=5803
latency_us=2721
actual_period_us=12697
cpu=4
deadline_miss=1
```

Comparison against non-root smoke runs:

| Run | Duration | CPU | Policy | Samples | Misses | p50 us | p99 us | max us |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| P1-SMOKE-IDLE-DEFAULT | 2 s | scheduler-chosen | SCHED_OTHER | 200 | 10 | 74 | 2325.32 | 2771 |
| P1-SMOKE-CPU4 | 2 s | 4 | SCHED_OTHER | 200 | 12 | 72 | 3148.07 | 11128 |
| P1-RT-AUTHORIZED-FIFO80-CPU4-60S | 60 s | 4 | SCHED_FIFO priority 80 | 6000 | 4 | 23 | 275 | 2721 |

Interpretation:

- `SCHED_FIFO` priority 80 with CPU4 pinning substantially improves the common-case and p99 latency in this unisolated bench environment.
- CPU migration is eliminated for the measured process: all samples report `cpu=4`.
- Deadline misses are reduced but not eliminated. Four samples exceed the 1 ms deadline threshold, with the worst observed latency at 2721 us.
- This is expected before cpuset, IRQ affinity, camera IRQ placement, and broader kernel-noise tuning.
- Phase 3/4 should treat these four miss rows as the baseline tail events to reduce.

---

## 3. Verification

```bash
ctest --test-dir /home/leo/ads-skynet/DCAS-PolicyEngine/build --output-on-failure
```

Result:

```text
1/1 Test #1: dcas_policy_tests ................ Passed
100% tests passed
```

---

## 4. Next Measurements

- Repeat `SCHED_FIFO` CPU4 measurement with camera/LKAS pipeline explicitly marked as `BENCH-CAMERA`.
- Repeat under CPU/GPU/memory load.
- Compare CPU affinity choices before cpuset/IRQ isolation.
