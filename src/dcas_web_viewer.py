"""
DCAS Web Viewer

HTTP: 8088  WebSocket: 8089

브라우저로 접속: http://localhost:8088

카메라 피드 전송:
    viewer.broadcast_lkas_frame(frame)    # numpy BGR frame
    viewer.broadcast_driver_frame(frame)  # numpy BGR frame

상태 전송:
    viewer.broadcast_status({
        "driver_state": "OK",         # OK / WARNING / ESCALATION / ABSENT
        "hmi_action": "INFO",         # INFO / EOR / DCA / MRM
        "reason": "none",             # none / phone / drowsy / unresponsive / intoxicated / blocked_camera
        "lkas_mode": "ON_ACTIVE",
        "mrm_active": False,
        "is_attentive": True,
    })
"""

import asyncio
import json
import time
import cv2
import numpy as np
import zmq
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread, Lock, Event
from typing import Optional, Set, Dict
import websockets

try:
    from common.src.config.config import ConfigManager
except ImportError:
    ConfigManager = None  # type: ignore

_DEFAULT_ZMQ_URL        = "tcp://localhost:5557"
_DEFAULT_DRIVER_FEED_URL = "tcp://192.168.86.32:5564"


def _load_zmq_url() -> str:
    if ConfigManager is not None:
        try:
            cfg = ConfigManager.load()
            comm = cfg.communication
            host = comm.zmq_broadcast_host
            if host == "*":
                host = "localhost"
            return f"tcp://{host}:{comm.zmq_broadcast_port}"
        except Exception:
            pass
    return _DEFAULT_ZMQ_URL


def _load_driver_feed_url() -> str:
    if ConfigManager is not None:
        try:
            cfg = ConfigManager.load()
            comm = cfg.communication
            return f"tcp://{comm.jetson_a_ip}:{comm.driver_feed_port}"
        except Exception:
            pass
    return _DEFAULT_DRIVER_FEED_URL


_DEFAULT_DCAS_STATUS_URL = "tcp://localhost:5565"


def _load_dcas_status_url() -> str:
    if ConfigManager is not None:
        try:
            cfg = ConfigManager.load()
            return f"tcp://localhost:{cfg.communication.dcas_status_port}"
        except Exception:
            pass
    return _DEFAULT_DCAS_STATUS_URL


HTTP_PORT = 8088
WS_PORT   = 8089

LKAS_CHANNEL   = 1
DRIVER_CHANNEL = 2


# ── WebSocket Server ────────────────────────────────────────────

class DCASWebSocketServer:
    def __init__(self, port: int):
        self.port = port
        self.clients: Set = set()
        self._lock = Lock()
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._thread: Optional[Thread] = None
        self._server = None
        self._ready = False
        self._send_futures: Dict[int, asyncio.Future] = {}

    def start(self):
        self._thread = Thread(target=self._run, daemon=True)
        self._thread.start()
        waited = 0.0
        while not self._ready and waited < 5:
            time.sleep(0.1)
            waited += 0.1

    def stop(self):
        if not self._loop or not self._loop.is_running():
            return

        async def _shutdown():
            with self._lock:
                clients = list(self.clients)
                self.clients.clear()
            tasks = [asyncio.ensure_future(c.close()) for c in clients]
            if tasks:
                await asyncio.wait(tasks, timeout=2)
            if self._server:
                self._server.close()
                await self._server.wait_closed()
            self._loop.stop()

        future = asyncio.run_coroutine_threadsafe(_shutdown(), self._loop)
        try:
            future.result(timeout=5)
        except Exception:
            if self._loop.is_running():
                self._loop.call_soon_threadsafe(self._loop.stop)

    @property
    def client_count(self) -> int:
        return len(self.clients)

    def broadcast_binary(self, data: bytes):
        if not self._loop:
            return
        with self._lock:
            dead = set()
            for client in self.clients:
                try:
                    cid = id(client)
                    prev = self._send_futures.get(cid)
                    if prev is not None and not prev.done():
                        continue
                    future = asyncio.run_coroutine_threadsafe(
                        client.send(data), self._loop)
                    self._send_futures[cid] = future
                except Exception:
                    dead.add(client)
            for c in dead:
                self._send_futures.pop(id(c), None)
            self.clients -= dead

    def broadcast_text(self, message: str):
        if not self._loop:
            return
        with self._lock:
            dead = set()
            for client in self.clients:
                try:
                    asyncio.run_coroutine_threadsafe(client.send(message), self._loop)
                except Exception:
                    dead.add(client)
            self.clients -= dead

    def _run(self):
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)

        async def _start():
            try:
                server = await websockets.serve(
                    self._ws_handler, '0.0.0.0', self.port,
                    ping_interval=20, ping_timeout=20)
                print(f"[DCAS-WS] Started on port {self.port}")
                self._ready = True
                return server
            except Exception as e:
                print(f"[DCAS-WS] Startup error: {e}")
                self._ready = False
                return None

        try:
            self._server = self._loop.run_until_complete(_start())
            if self._server:
                self._loop.run_forever()
        finally:
            pending = asyncio.all_tasks(self._loop)
            for task in pending:
                task.cancel()
            if pending:
                self._loop.run_until_complete(
                    asyncio.gather(*pending, return_exceptions=True))
            self._loop.close()

    async def _ws_handler(self, websocket):
        with self._lock:
            self.clients.add(websocket)
        addr = f"{websocket.remote_address[0]}:{websocket.remote_address[1]}"
        print(f"[DCAS-WS] Client connected: {addr}")
        try:
            async for _ in websocket:
                pass
        except websockets.exceptions.ConnectionClosed:
            pass
        finally:
            with self._lock:
                self.clients.discard(websocket)
            self._send_futures.pop(id(websocket), None)
            print(f"[DCAS-WS] Client disconnected: {addr}")


# ── LKAS Frame Subscriber ───────────────────────────────────────

class LKASFrameSubscriber:
    """Daemon thread that subscribes to the LKAS ZMQ broadcast and feeds frames."""

    def __init__(self, zmq_url: str, broadcast_fn):
        self._url = zmq_url
        self._broadcast = broadcast_fn
        self._stop_event = Event()
        self._thread: Optional[Thread] = None

    def start(self):
        self._stop_event.clear()
        self._thread = Thread(target=self._run, daemon=True, name="lkas-frame-sub")
        self._thread.start()

    def stop(self):
        self._stop_event.set()
        if self._thread:
            self._thread.join(timeout=2.0)
            self._thread = None

    def _run(self):
        ctx = zmq.Context()
        sock = ctx.socket(zmq.SUB)
        sock.setsockopt(zmq.SUBSCRIBE, b'frame')
        sock.setsockopt(zmq.RCVTIMEO, 100)  # ms — lets us check stop_event
        sock.setsockopt(zmq.RCVHWM, 5)      # drop stale frames if we're slow
        sock.connect(self._url)
        print(f"[DCAS-LKAS] Subscribed to {self._url}")
        try:
            while not self._stop_event.is_set():
                try:
                    parts = sock.recv_multipart()
                    if len(parts) < 3:
                        continue
                    buf = np.frombuffer(parts[2], dtype=np.uint8)
                    frame = cv2.imdecode(buf, cv2.IMREAD_COLOR)
                    if frame is not None:
                        self._broadcast(frame)
                except zmq.Again:
                    pass  # 100 ms timeout — loop and recheck stop_event
        finally:
            sock.close()
            ctx.term()
            print("[DCAS-LKAS] Subscriber stopped")


# ── Driver Frame Subscriber ─────────────────────────────────────

class DriverFrameSubscriber:
    """Daemon thread that subscribes to dms.py on Jetson A and feeds driver frames."""

    def __init__(self, zmq_url: str, broadcast_fn):
        self._url = zmq_url
        self._broadcast = broadcast_fn
        self._stop_event = Event()
        self._thread: Optional[Thread] = None

    def start(self):
        self._stop_event.clear()
        self._thread = Thread(target=self._run, daemon=True, name="driver-frame-sub")
        self._thread.start()

    def stop(self):
        self._stop_event.set()
        if self._thread:
            self._thread.join(timeout=2.0)
            self._thread = None

    def _run(self):
        ctx = zmq.Context()
        sock = ctx.socket(zmq.SUB)
        sock.setsockopt(zmq.SUBSCRIBE, b'driver_frame')
        sock.setsockopt(zmq.RCVTIMEO, 100)
        sock.setsockopt(zmq.RCVHWM, 5)
        sock.connect(self._url)
        print(f"[DCAS-DRV] Subscribed to {self._url}")
        try:
            while not self._stop_event.is_set():
                try:
                    parts = sock.recv_multipart()
                    if len(parts) < 2:
                        continue
                    buf = np.frombuffer(parts[1], dtype=np.uint8)
                    frame = cv2.imdecode(buf, cv2.IMREAD_COLOR)
                    if frame is not None:
                        self._broadcast(frame)
                except zmq.Again:
                    pass
        finally:
            sock.close()
            ctx.term()
            print("[DCAS-DRV] Subscriber stopped")


# ── DCAS Status Subscriber ──────────────────────────────────────

class DCASStatusSubscriber:
    """Daemon thread that subscribes to dcas_rt_bridge JSON status and feeds broadcast_status."""

    def __init__(self, zmq_url: str, broadcast_fn):
        self._url = zmq_url
        self._broadcast = broadcast_fn
        self._stop_event = Event()
        self._thread: Optional[Thread] = None

    def start(self):
        self._stop_event.clear()
        self._thread = Thread(target=self._run, daemon=True, name="dcas-status-sub")
        self._thread.start()

    def stop(self):
        self._stop_event.set()
        if self._thread:
            self._thread.join(timeout=2.0)
            self._thread = None

    def _run(self):
        ctx = zmq.Context()
        sock = ctx.socket(zmq.SUB)
        sock.setsockopt(zmq.SUBSCRIBE, b'')
        sock.setsockopt(zmq.RCVTIMEO, 100)
        sock.setsockopt(zmq.RCVHWM, 5)
        sock.connect(self._url)
        print(f"[DCAS-STATUS] Subscribed to {self._url}")
        try:
            while not self._stop_event.is_set():
                try:
                    raw = sock.recv()
                    data = json.loads(raw)
                    self._broadcast(data)
                except zmq.Again:
                    pass
                except (json.JSONDecodeError, Exception):
                    pass
        finally:
            sock.close()
            ctx.term()
            print("[DCAS-STATUS] Subscriber stopped")


# ── HTTP Server ─────────────────────────────────────────────────

class DCASHTTPServer:
    def __init__(self, port: int, ws_port: int):
        self.port = port
        self.ws_port = ws_port
        self._server: Optional[ThreadingHTTPServer] = None
        self._thread: Optional[Thread] = None

    def start(self):
        ws_port = self.ws_port

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, fmt, *args):
                msg = fmt % args
                if "code 4" in msg or "code 5" in msg:
                    print(f"[DCAS-HTTP] {msg}")

            def do_GET(self):
                if self.path == '/':
                    self._serve_html()
                elif self.path == '/health':
                    self.send_response(200)
                    self.send_header('Content-Type', 'text/plain')
                    self.end_headers()
                    self.wfile.write(b'OK\n')
                elif self.path == '/favicon.ico':
                    self.send_response(204)
                    self.end_headers()
                else:
                    self.send_error(404)

            def _serve_html(self):
                html_path = Path(__file__).parent / 'dcas_viewer.html'
                try:
                    with open(html_path, 'r', encoding='utf-8') as f:
                        html = f.read().replace('{{WS_PORT}}', str(ws_port))
                    payload = html.encode('utf-8')
                    self.send_response(200)
                    self.send_header('Content-Type', 'text/html; charset=utf-8')
                    self.send_header('Content-Length', str(len(payload)))
                    self.end_headers()
                    self.wfile.write(payload)
                except Exception as e:
                    self.send_response(500)
                    self.send_header('Content-Type', 'text/plain')
                    self.end_headers()
                    self.wfile.write(f"Error: {e}".encode())

        try:
            self._server = ThreadingHTTPServer(('0.0.0.0', self.port), Handler)
            self._thread = Thread(target=self._server.serve_forever, daemon=True)
            self._thread.start()
            time.sleep(0.2)
            print(f"[DCAS-HTTP] Started on port {self.port}")
        except Exception as e:
            print(f"[DCAS-HTTP] Failed to start: {e}")

    def stop(self):
        if self._server:
            self._server.shutdown()


# ── Orchestrator ────────────────────────────────────────────────

class DCASWebViewer:
    """
    사용법:
        viewer = DCASWebViewer()
        viewer.start()

        # 카메라 피드 (numpy BGR)
        viewer.broadcast_lkas_frame(frame)
        viewer.broadcast_driver_frame(frame)

        # DCAS 상태
        viewer.broadcast_status({
            "driver_state": "OK",
            "hmi_action": "INFO",
            "reason": "none",
            "lkas_mode": "ON_ACTIVE",
            "mrm_active": False,
            "is_attentive": True,
        })

        viewer.stop()
    """

    def __init__(self, http_port: int = HTTP_PORT, ws_port: int = WS_PORT,
                 zmq_url: Optional[str] = None,
                 driver_feed_url: Optional[str] = None,
                 dcas_status_url: Optional[str] = None):
        self.http_port = http_port
        self.ws_port   = ws_port

        self._frame_interval  = 1.0 / 30
        self._status_interval = 0.2   # 5 Hz
        self._last_lkas_time   = 0.0
        self._last_driver_time = 0.0
        self._last_status_time = 0.0

        self.ws_server     = DCASWebSocketServer(port=ws_port)
        self.http_server   = DCASHTTPServer(port=http_port, ws_port=ws_port)
        self._lkas_sub     = LKASFrameSubscriber(
            zmq_url or _load_zmq_url(), self.broadcast_lkas_frame)
        self._driver_sub   = DriverFrameSubscriber(
            driver_feed_url or _load_driver_feed_url(), self.broadcast_driver_frame)
        self._status_sub   = DCASStatusSubscriber(
            dcas_status_url or _load_dcas_status_url(), self.broadcast_status)

    def start(self):
        self.http_server.start()
        self.ws_server.start()
        self._lkas_sub.start()
        self._driver_sub.start()
        self._status_sub.start()
        print(f"\n[DCAS Viewer] http://localhost:{self.http_port}\n")

    def stop(self):
        self._lkas_sub.stop()
        self._driver_sub.stop()
        self._status_sub.stop()
        self.ws_server.stop()
        self.http_server.stop()

    def _encode_frame(self, frame, channel: int, quality: int = 80) -> Optional[bytes]:
        success, buf = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, quality])
        if not success:
            return None
        prefix = bytes([channel])
        return prefix + buf.tobytes()

    def broadcast_lkas_frame(self, frame):
        now = time.time()
        if now - self._last_lkas_time < self._frame_interval:
            return
        if self.ws_server.client_count == 0:
            return
        data = self._encode_frame(frame, LKAS_CHANNEL)
        if data:
            self._last_lkas_time = now
            self.ws_server.broadcast_binary(data)

    def broadcast_driver_frame(self, frame):
        now = time.time()
        if now - self._last_driver_time < self._frame_interval:
            return
        if self.ws_server.client_count == 0:
            return
        data = self._encode_frame(frame, DRIVER_CHANNEL)
        if data:
            self._last_driver_time = now
            self.ws_server.broadcast_binary(data)

    def broadcast_status(self, status: dict):
        now = time.time()
        if now - self._last_status_time < self._status_interval:
            return
        if self.ws_server.client_count == 0:
            return
        self._last_status_time = now
        status['type'] = 'status'
        if 'timestamp' not in status:
            status['timestamp'] = time.strftime('%H:%M:%S')
        self.ws_server.broadcast_text(json.dumps(status))


# ── Mock Demo ───────────────────────────────────────────────────

if __name__ == '__main__':
    import math

    viewer = DCASWebViewer()
    viewer.start()

    # Mock 시나리오: OK → WARNING → ESCALATION → ABSENT(MRM) → 반복
    SCENARIOS = [
        {"driver_state": "OK",         "hmi_action": "INFO", "reason": "none",           "lkas_mode": "ON_ACTIVE",   "mrm_active": False, "is_attentive": True,  "duration": 4},
        {"driver_state": "WARNING",    "hmi_action": "EOR",  "reason": "drowsy",         "lkas_mode": "ON_ACTIVE",   "mrm_active": False, "is_attentive": False, "duration": 3},
        {"driver_state": "ESCALATION", "hmi_action": "DCA",  "reason": "phone",          "lkas_mode": "ON_INACTIVE", "mrm_active": False, "is_attentive": False, "duration": 3},
        {"driver_state": "ABSENT",     "hmi_action": "MRM",  "reason": "unresponsive",   "lkas_mode": "OFF",         "mrm_active": True,  "is_attentive": False, "duration": 3},
        {"driver_state": "WARNING",    "hmi_action": "EOR",  "reason": "blocked_camera", "lkas_mode": "ON_INACTIVE", "mrm_active": False, "is_attentive": False, "duration": 3},
        {"driver_state": "ABSENT",     "hmi_action": "MRM",  "reason": "blocked_camera", "lkas_mode": "OFF",         "mrm_active": True,  "is_attentive": False, "duration": 3},
    ]

    scenario_idx = 0
    scenario_start = time.time()

    frame_w, frame_h = 640, 360
    t = 0.0

    print("Press Ctrl+C to stop")

    try:
        while True:
            # 시나리오 전환
            s = SCENARIOS[scenario_idx]
            if time.time() - scenario_start > s["duration"]:
                scenario_idx = (scenario_idx + 1) % len(SCENARIOS)
                scenario_start = time.time()
                s = SCENARIOS[scenario_idx]
                print(f"[Mock] → {s['driver_state']} / {s['hmi_action']} / {s['reason']}")

            # Mock 도로 카메라 (차선 그리기)
            road = np.zeros((frame_h, frame_w, 3), dtype=np.uint8)
            road[:] = (20, 20, 20)
            offset = int(math.sin(t * 0.5) * 30)
            cv2.line(road, (frame_w//2 - 80 + offset, frame_h), (frame_w//2 - 40 + offset, frame_h//2), (255, 255, 255), 2)
            cv2.line(road, (frame_w//2 + 80 + offset, frame_h), (frame_w//2 + 40 + offset, frame_h//2), (255, 255, 255), 2)
            # viewer.broadcast_lkas_frame(road)

            # Mock 운전자 카메라
            driver = np.zeros((frame_h, frame_w, 3), dtype=np.uint8)
            driver[:] = (15, 15, 25)
            face_color = (100, 200, 100) if s["is_attentive"] else (60, 60, 180)
            cx, cy = frame_w // 2, frame_h // 2
            cv2.circle(driver, (cx, cy - 20), 60, face_color, -1)
            cv2.circle(driver, (cx - 20, cy - 30), 8, (255, 255, 255), -1)
            cv2.circle(driver, (cx + 20, cy - 30), 8, (255, 255, 255), -1)
            if s["is_attentive"]:
                cv2.ellipse(driver, (cx, cy), (25, 15), 0, 0, 180, (255, 255, 255), 2)
            else:
                cv2.line(driver, (cx - 25, cy), (cx + 25, cy), (100, 100, 100), 2)
            # viewer.broadcast_driver_frame(driver)

            # 상태 전송 — real data comes from DCASStatusSubscriber (dcas_rt_bridge:5565)
            # viewer.broadcast_status(dict(s))

            t += 0.05
            time.sleep(0.033)  # ~30 FPS

    except KeyboardInterrupt:
        print("\n[DCAS Viewer] Stopping...")
        viewer.stop()
