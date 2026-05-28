# Phase2 RT Results

This file will summarize Phase 2 measurements: load-induced interference experiments.

## Metadata

- tester: 
- date: 
- kernel: 
- boot params: 
- notes: 

## Cases

### Case B (stock, CPU+MEM load) — smoke (60s)

- Load script: `tools/ffi_load_runner/case_b.sh`
- Logs: `/tmp/rt_tests/phase2/smoke1/`
- Notes: `stress-ng` run completed successfully (60s); no OOM or process kills observed in `dmesg` capture.

### Case C / D / E

- (fill similarly)

### Case C (GPU/YOLO-style load, privileged rerun, 60s)

- Load script: `tools/ffi_load_runner/case_c.sh`
- Load profile: CUDA-based YOLO-style inference stress, batch 4, image 640, 60s
- Probe command:

```bash
sudo /home/leo/ads-skynet/DCAS-PolicyEngine/build/rt_periodic_probe \
	--period-us 10000 \
	--duration 60 \
	--cpu 4 \
	--priority 80 \
	--deadline-us 1000 \
	--output /tmp/rt_tests/phase2/case_c3/rt_periodic_probe.csv
```

- CSV: `/tmp/rt_tests/phase2/case_c3/rt_periodic_probe.csv`
- line count: 6001
- sample count: 6000
- CPU pinning: CPU 4 for all samples
- deadline misses: 0
- latency p50: 24 us
- latency p99: 294 us
- latency max: 488 us
- actual period max: 10348 us

Interpretation:

- In the clean privileged rerun, the probe stayed within the 1 ms deadline threshold for all samples while GPU load was active.
- This suggests the previous tail spike was largely attributable to the earlier non-privileged / interrupted execution path and not to a persistent GPU-induced overload in this specific rerun.
- I could not re-read `dmesg` noninteractively because `sudo -n dmesg` still requires a password in this environment, so kernel-log OOM confirmation remains unavailable.
- The next tuning step is to repeat Case C under explicit CPU/IRQ isolation and compare the tail against this privileged baseline.

### Case C (GPU/YOLO-style load) — unprivileged (60s)

- Load script: `tools/ffi_load_runner/case_c.sh`
- Probe CSV: `/tmp/rt_tests/phase2/case_c1/rt_periodic_probe.csv`
- sample count: 6000
- deadline misses: 161
- latency p50: 72 us
- latency p99: 2118 us
- latency max: 5362 us
- notes: non-privileged run showed significant tail latency and deadline misses; SCHED_FIFO could not be applied without sudo.

### Case D (CPU/IRQ/cgroup isolation) — privileged (60s)

- Load script: `tools/ffi_load_runner/case_d.sh`
- Probe CSV: `/tmp/rt_tests/phase3/case_d_1779979020/rt_periodic_probe.csv`
- logs: `/tmp/rt_tests/phase3/case_d_1779979020/`
- sample count: 6000
- deadline misses: 0
- latency p50: 22 us
- latency p99: 259 us
- latency max: 409 us
- notes: cpuset-based isolation (rt.slice -> CPU4, qm.slice -> CPU0-3) with GPU stress produced lower tail latency vs. unisolated runs; no OOM/killer observed in dmesg.

## Plots

- histogram: 
- cdf: 

## Interpretation

- short conclusions and next tuning step
