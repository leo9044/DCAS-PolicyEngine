"""
perception_feeder.py — Jetson B

Bridges Jetson A driver perception state into rt_control_shm so that
dcas_rt_bridge reads live PerceptionInput on every policy tick.

Flow:
  Jetson A ZMQ PUB :5563
    → PerceptionInputSubscriber.poll()
    → mmap write → /dev/shm/rt_control_shm
        perception_to_dcas @ 4648  (24 bytes)
        perception_seq      @ 4672  (4 bytes, monotonically incremented)
    → dcas_rt_bridge reads on each 10 ms tick
"""

import mmap
import os
import struct
import sys
import time

from common.communication.zmq_broadcast import PerceptionInputSubscriber

# ── SHM layout (rt_control_shm.hpp version 2) ────────────────────────────────
_SHM_PATH              = '/dev/shm/rt_control_shm'
_SHM_SIZE              = 4680
_PERCEPTION_SAMPLE_OFF = 4648   # offsetof(RtControlShmLayout, perception_to_dcas)
_PERCEPTION_SEQ_OFF    = 4672   # offsetof(RtControlShmLayout, perception_seq)

# PerceptionToDcasSample: u64 timestamp_us, u32 is_attentive,
#                         u32 camera_blocked, u32 reason, u32 reserved
_SAMPLE = struct.Struct('<QIIII')   # 24 bytes, little-endian

_REASON_MAP = {
    'none':           0,
    'phone':          1,
    'drowsy':         2,
    'unresponsive':   3,
    'intoxicated':    4,
    'unknown':        5,
    'blocked_camera': 6,
}

# ── SHM helpers ───────────────────────────────────────────────────────────────

def _open_shm(retry_s: float = 1.0) -> mmap.mmap:
    """Block until /dev/shm/rt_control_shm exists, then mmap it."""
    while True:
        if os.path.exists(_SHM_PATH):
            try:
                fd = os.open(_SHM_PATH, os.O_RDWR)
                buf = mmap.mmap(fd, _SHM_SIZE)
                os.close(fd)
                print(f'[perception_feeder] Mapped {_SHM_PATH} ({_SHM_SIZE} B)')
                return buf
            except OSError as e:
                print(f'[perception_feeder] mmap failed: {e}, retrying in {retry_s} s')
        else:
            print(f'[perception_feeder] Waiting for {_SHM_PATH} ...')
        time.sleep(retry_s)


def _read_seq(buf: mmap.mmap) -> int:
    buf.seek(_PERCEPTION_SEQ_OFF)
    return struct.unpack('<I', buf.read(4))[0]


def _write(buf: mmap.mmap, is_attentive: bool, camera_blocked: bool,
           reason: str, timestamp_us: int, seq: int) -> None:
    # Write struct first, then seq — mirrors the acquire/release pair in the
    # C++ read_perception_to_dcas (seq load with acquire, struct read after).
    buf.seek(_PERCEPTION_SAMPLE_OFF)
    buf.write(_SAMPLE.pack(
        timestamp_us,
        int(is_attentive),
        int(camera_blocked),
        _REASON_MAP.get(reason, 5),   # fall back to UNKNOWN for unrecognised strings
        0,                             # reserved
    ))
    buf.seek(_PERCEPTION_SEQ_OFF)
    buf.write(struct.pack('<I', seq))


# ── Main ─────────────────────────────────────────────────────────────────────

def main() -> None:
    buf = _open_shm()

    # On restart, resume from the current SHM seq so the C++ side sees a
    # monotonically increasing counter and does not treat the first write as
    # a no-op.  Start at 1 if the slot was never written (seq == 0).
    seq = _read_seq(buf)
    seq = max(seq + 1, 1)

    sub = PerceptionInputSubscriber()
    print('[perception_feeder] Running. Ctrl+C to stop.')

    try:
        while True:
            msg = sub.poll()
            if msg is not None:
                _write(
                    buf,
                    is_attentive=msg.is_attentive,
                    camera_blocked=msg.camera_blocked,
                    reason=msg.reason,
                    timestamp_us=int(time.time() * 1_000_000),
                    seq=seq,
                )
                seq += 1
                if seq > 0xFFFFFFFF:
                    seq = 1      # wrap; never return to 0 (0 = never-written sentinel)
            else:
                time.sleep(0.005)   # 5 ms idle sleep — perception arrives at ~30 Hz
    except KeyboardInterrupt:
        print('\n[perception_feeder] Stopped.')
    finally:
        sub.close()
        buf.close()


if __name__ == '__main__':
    main()
