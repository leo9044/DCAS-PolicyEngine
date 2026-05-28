#!/usr/bin/env python3
"""YOLO-style CUDA inference stressor for Phase 2 latency testing.

If a real YOLO weights file is provided via YOLO_WEIGHTS and the needed
runtime is installed, this script can be extended later. For the current
workspace, it runs a deterministic CUDA inference loop that mimics the
shape of a vision model load without risking missing-model failures.
"""

from __future__ import annotations

import argparse
import os
import time

import torch
import torch.nn as nn


class YoloLikeNet(nn.Module):
    def __init__(self) -> None:
        super().__init__()
        self.net = nn.Sequential(
            nn.Conv2d(3, 32, kernel_size=3, stride=2, padding=1),
            nn.SiLU(),
            nn.Conv2d(32, 64, kernel_size=3, stride=2, padding=1),
            nn.SiLU(),
            nn.Conv2d(64, 128, kernel_size=3, stride=2, padding=1),
            nn.SiLU(),
            nn.Conv2d(128, 128, kernel_size=3, stride=1, padding=1),
            nn.SiLU(),
            nn.Conv2d(128, 64, kernel_size=1),
            nn.SiLU(),
            nn.AdaptiveAvgPool2d((1, 1)),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.net(x)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="YOLO-style GPU stress runner")
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--batch-size", type=int, default=4)
    parser.add_argument("--image-size", type=int, default=640)
    parser.add_argument("--device", type=str, default="cuda:0")
    parser.add_argument("--report-every", type=int, default=10)
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if not torch.cuda.is_available():
        raise SystemExit("CUDA is not available; Case C requires a CUDA-capable Jetson.")

    device = torch.device(args.device)
    torch.backends.cudnn.benchmark = True
    torch.set_float32_matmul_precision("high")

    if os.environ.get("YOLO_WEIGHTS"):
        print(f"YOLO_WEIGHTS set to {os.environ['YOLO_WEIGHTS']}, but no YOLO runtime is installed; using YOLO-style CUDA load instead.")

    print(f"GPU device: {torch.cuda.get_device_name(device)}")
    print(f"batch_size={args.batch_size} image_size={args.image_size} duration={args.duration}s")

    model = YoloLikeNet().to(device).eval()
    image = torch.rand((args.batch_size, 3, args.image_size, args.image_size), device=device, dtype=torch.float32)
    mat_a = torch.rand((1024, 1024), device=device, dtype=torch.float32)
    mat_b = torch.rand((1024, 1024), device=device, dtype=torch.float32)

    start = time.monotonic()
    iterations = 0
    last_report = start

    with torch.inference_mode():
        while time.monotonic() - start < args.duration:
            # Vision-like preprocessing + forward pass + dense math to keep CUDA busy.
            x = image
            x = torch.nn.functional.interpolate(x, size=(args.image_size, args.image_size), mode="bilinear", align_corners=False)
            x = model(x)
            y = torch.matmul(mat_a, mat_b)
            z = torch.relu(y)
            torch.cuda.synchronize()
            _ = (x.mean() + z.mean()).item()
            iterations += 1

            now = time.monotonic()
            if now - last_report >= args.report_every:
                mem_alloc = torch.cuda.memory_allocated(device) / (1024 * 1024)
                mem_reserved = torch.cuda.memory_reserved(device) / (1024 * 1024)
                print(
                    f"iterations={iterations} elapsed={now - start:.1f}s "
                    f"mem_alloc_mb={mem_alloc:.1f} mem_reserved_mb={mem_reserved:.1f}"
                )
                last_report = now

    print(f"Case C GPU stress finished: iterations={iterations} duration={time.monotonic() - start:.2f}s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
