#!/usr/bin/env bash
# Case B: stock kernel, no isolation, CPU+memory load (safer for Jetson)
set -euo pipefail

# Usage: case_b.sh [duration-sec] [cpu-workers] [vm-workers]
DURATION=${1:-60}
CPU_WORKERS=${2:-4}
VM_WORKERS=${3:-1}

echo "Starting Case B load: duration=${DURATION}s cpu=${CPU_WORKERS} vm=${VM_WORKERS}"

# Start combined stress-ng with bounded VM allocation to avoid OOM on Jetson
# Use a conservative fixed allocation (1G) rather than a percent of total RAM.
stress-ng \
	--cpu ${CPU_WORKERS} --cpu-method fft \
	--vm ${VM_WORKERS} --vm-bytes 1G \
	-t ${DURATION}s --metrics-brief

echo "Case B load finished"
