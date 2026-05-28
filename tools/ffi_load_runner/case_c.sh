#!/usr/bin/env bash
# Case C: GPU/YOLO-style load for tail-latency testing
set -euo pipefail

# Usage: case_c.sh [duration-sec] [batch-size] [image-size]
DURATION=${1:-60}
BATCH_SIZE=${2:-4}
IMAGE_SIZE=${3:-640}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Starting Case C load: duration=${DURATION}s batch=${BATCH_SIZE} image=${IMAGE_SIZE}"

if [[ -n "${YOLO_WEIGHTS:-}" ]]; then
  echo "YOLO_WEIGHTS=${YOLO_WEIGHTS}"
fi

exec python3 "${SCRIPT_DIR}/gpu_yolo_stress.py" \
  --duration "${DURATION}" \
  --batch-size "${BATCH_SIZE}" \
  --image-size "${IMAGE_SIZE}" \
  --device "cuda:0"
