#!/usr/bin/env bash
# Case D: CPU + IRQ isolation orchestration
set -euo pipefail

# Usage: case_d.sh [duration-sec] [rt-cpus] [qm-cpus] [irq-ids] [irq-cpus]
# Examples:
#  case_d.sh 60 4 0-3 "16,17" 4

DURATION=${1:-60}
RT_CPUS=${2:-4}
QM_CPUS=${3:-0-3}
IRQ_IDS=${4:-}
IRQ_CPUS=${5:-}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTDIR="/tmp/rt_tests/phase3/case_d_$(date +%s)"
mkdir -p "${OUTDIR}"

echo "Case D: duration=${DURATION}s rt_cpus=${RT_CPUS} qm_cpus=${QM_CPUS} irq_ids=${IRQ_IDS} irq_cpus=${IRQ_CPUS}"
echo "Logs -> ${OUTDIR}"

# Start lightweight monitors
vmstat 5 $(( (DURATION + 4) / 5 )) > "${OUTDIR}/vmstat.log" & VMSTAT_PID=$!
top -b -d 5 -n $(( (DURATION + 4) / 5 )) > "${OUTDIR}/top.log" & TOP_PID=$!

# Apply cpuset (cgroup v2) -- requires root
echo "Applying cpuset settings (requires sudo)"
sudo bash -c "mkdir -p /sys/fs/cgroup/rt.slice /sys/fs/cgroup/qm.slice || true"
sudo bash -c "echo 0 > /sys/fs/cgroup/rt.slice/cpuset.mems || true"
sudo bash -c "echo 0 > /sys/fs/cgroup/qm.slice/cpuset.mems || true"
sudo bash -c "echo '${RT_CPUS}' > /sys/fs/cgroup/rt.slice/cpuset.cpus"
sudo bash -c "echo '${QM_CPUS}' > /sys/fs/cgroup/qm.slice/cpuset.cpus"

if [[ -n "${IRQ_IDS}" && -n "${IRQ_CPUS}" ]]; then
  echo "Setting IRQ affinity for IRQs: ${IRQ_IDS} -> cpus: ${IRQ_CPUS} (requires sudo)"
  IFS=',' read -ra IIDS <<< "${IRQ_IDS}"
  for irq in "${IIDS[@]}"; do
    sudo bash -c "if [[ -f /proc/irq/${irq}/smp_affinity_list ]]; then echo '${IRQ_CPUS}' > /proc/irq/${irq}/smp_affinity_list || true; fi"
  done
fi

# Start GPU stress (Case C) in background
echo "Starting GPU stress (Case C)"
"${SCRIPT_DIR}/case_c.sh" "${DURATION}" 4 640 > "${OUTDIR}/case_c.out" 2>&1 & GPU_PID=$!

# Give stressor a moment to initialize
sleep 2

# Start rt_periodic_probe (privileged run recommended)
PROBE_BIN="/home/leo/ads-skynet/DCAS-PolicyEngine/build/rt_periodic_probe"
PROBE_OUT="${OUTDIR}/rt_periodic_probe.csv"
echo "Starting rt_periodic_probe -> ${PROBE_OUT} (requires sudo for SCHED_FIFO)"
sudo "${PROBE_BIN}" --period-us 10000 --duration "${DURATION}" --cpu "${RT_CPUS}" --priority 80 --deadline-us 1000 --output "${PROBE_OUT}" > "${OUTDIR}/probe.out" 2>&1 & PROBE_PID=$!

# Move processes into cgroups for isolation (best-effort)
sleep 1
echo "Moving PIDs into cgroups"
sudo bash -c "echo ${PROBE_PID} > /sys/fs/cgroup/rt.slice/cgroup.procs || true"
sudo bash -c "echo ${GPU_PID} > /sys/fs/cgroup/qm.slice/cgroup.procs || true"

# Wait for workload to finish
wait ${PROBE_PID} || true
wait ${GPU_PID} || true

# Stop monitors
kill ${VMSTAT_PID} ${TOP_PID} >/dev/null 2>&1 || true

echo "Case D finished. Logs: ${OUTDIR}"
echo "Check dmesg for OOM/kill: sudo dmesg --ctime | tail -n 200 > ${OUTDIR}/dmesg_after.log"

exit 0
