#!/usr/bin/env bash
# apply_isolation.sh: cgroup v2-compatible cpuset + IRQ affinity adjustments
set -euo pipefail

echo "Applying cpuset/cgroup v2 isolation (requires root)"
if [[ $(id -u) -ne 0 ]]; then
  echo "Run as root to apply isolation settings"
  exit 1
fi

RT_CPUS="4-5"
QM_CPUS="1-3"

# Ensure cgroup v2 cpuset controller is enabled
if [[ -f /sys/fs/cgroup/cgroup.controllers ]]; then
  grep -q cpuset /sys/fs/cgroup/cgroup.controllers || {
    echo "cpuset controller not enabled in cgroup v2; try mounting with controllers enabled"
  }
fi

mkdir -p /sys/fs/cgroup/rt.slice || true
mkdir -p /sys/fs/cgroup/qm.slice || true

# IMPORTANT: For cpuset in cgroup v2, set cpuset.mems before cpuset.cpus
echo "0" > /sys/fs/cgroup/rt.slice/cpuset.mems || true
echo "0" > /sys/fs/cgroup/qm.slice/cpuset.mems || true

echo ${RT_CPUS} > /sys/fs/cgroup/rt.slice/cpuset.cpus || true
echo ${QM_CPUS} > /sys/fs/cgroup/qm.slice/cpuset.cpus || true

echo "Suggested IRQ affinity check:"
cat /proc/interrupts | head -n 10
echo "To tune IRQ affinity, write smp_affinity_list into /proc/irq/<N>/smp_affinity_list"

echo "Isolation applied (best-effort). Verify with: cat /sys/fs/cgroup/rt.slice/cpuset.cpus"
echo "To move a process into the RT slice: echo <PID> > /sys/fs/cgroup/rt.slice/cgroup.procs"
