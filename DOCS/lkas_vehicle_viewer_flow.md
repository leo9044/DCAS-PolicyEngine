# LKAS -> Vehicle -> Viewer Execution Flow

This document explains the runtime flow of the current system in the order it is usually started:

1. `lkas --broadcast`
2. `vehicle`
3. `viewer`

It also explains which source file runs first, what that file does, and how control/data move to the next stage.

---

## High-Level Overview

The system is split into three separate processes:

- **LKAS**: lane detection + lane-keeping decision logic
- **Vehicle**: camera input + actuation on the JetRacer
- **Viewer**: browser dashboard for monitoring and remote control

The processes are independent, but they communicate through shared memory and ZMQ.

### Data Flow

- Vehicle reads images from the camera and feeds them into LKAS shared memory.
- LKAS reads the frame, performs lane detection and steering control, and optionally broadcasts state.
- Viewer subscribes to the broadcast stream and displays the live image, overlays, and state.

---

## 1) First Executed Process: LKAS

### Command

```bash
lkas --broadcast
```

### First file executed

The `lkas` command is the console entry point defined in `Lkas/pyproject.toml`.
It resolves to:

- `Lkas/src/run.py`
- `main()`
- `LKASServer`

### Code flow

1. The shell runs the `lkas` executable.
2. Python enters `Lkas/src/run.py`.
3. `main()` parses command-line arguments such as:
   - `--method cv` or `--method dl`
   - `--config`
   - `--gpu`
   - `--broadcast`
4. `main()` creates `LKASServer(...)`.
5. `LKASServer.run()` starts the LKAS sub-processes.

### What `LKASServer` does

Inside `Lkas/src/server.py`:

- it loads configuration from `common.config.ConfigManager`
- it resolves shared memory names
- it creates process managers for detection and decision
- if `--broadcast` is set, it creates the broadcast manager

### Sub-process start order

`LKASServer._launch_processes()` starts:

1. **Decision server first**
2. **Detection server second**

This order matters because the decision side expects the detection/control pipeline to be ready around the same startup window.

### What LKAS owns

LKAS is responsible for:

- reading frames from shared memory
- detecting lanes
- computing steering/throttle decisions
- broadcasting frame/detection/state data when enabled

---

## 2) Second Executed Process: Vehicle

### Command

```bash
vehicle
```

### First file executed

The `vehicle` command is also a console entry point.
It resolves to:

- `Vehicle-jetracer/src/main.py`
- `main()`
- `Vehicle`

### Code flow

1. The shell runs the `vehicle` executable.
2. Python enters `Vehicle-jetracer/src/main.py`.
3. `main()` parses arguments such as:
   - `--device`
   - `--publish-state-hz`
   - `--keepalive`
4. `main()` loads shared configuration via `ConfigManager.load()`.
5. `main()` prints the selected camera, ports, image size, and throttle base.
6. `main()` constructs `Vehicle(...)`.
7. `main()` calls `v.run()`.

### What `Vehicle` does

Inside `Vehicle-jetracer/src/vehicle.py`:

- it opens the camera with `Camera(device_path=...)`
- it initializes `NvidiaRacecar`
- it creates an `LKAS` client with shared memory names
- it sets up ZMQ publishers/subscribers for state and control
- it enters the main control loop in `run()`

### Main loop flow in `Vehicle.run()`

For each iteration:

1. poll action/parameter messages
2. send vehicle state periodically
3. read a frame from the camera
4. send the frame to LKAS shared memory
5. receive LKAS control output
6. apply steering and throttle to the hardware
7. repeat

### What Vehicle owns

Vehicle is responsible for:

- camera capture
- shared-memory frame delivery to LKAS
- actuation through `NvidiaRacecar`
- publishing vehicle state for viewer/monitoring

---

## 3) Third Executed Process: Viewer

### Command

```bash
viewer
```

### First file executed

The `viewer` command is the console entry point defined in `Web-viewer/pyproject.toml`.
It resolves to:

- `Web-viewer/src/run.py`
- `main()`
- `ZMQWebViewer`

### Code flow

1. The shell runs the `viewer` executable.
2. Python enters `Web-viewer/src/run.py`.
3. `main()` loads the shared configuration from `ConfigManager.load()`.
4. `main()` builds default ZMQ URLs.
5. `main()` parses viewer-specific options such as:
   - `--target`
   - `--vehicle`
   - `--actions`
   - `--parameters`
   - `--port`
   - `--simulation-mode`
6. `main()` creates `ZMQWebViewer(...)`.
7. `viewer.start()` starts:
   - the HTTP server
   - the WebSocket server
   - the ZMQ polling loop
   - the render loop
8. `viewer.run()` blocks until Ctrl+C.

### What the Viewer does

Inside `Web-viewer/src/run.py` and its helper modules:

- it subscribes to vehicle/LKAS data via `ViewerSubscriber`
- it receives frame/detection/state messages
- it renders overlays using the visualizer/renderer
- it serves the browser UI on port 8080
- it sends actions/parameter updates back to the vehicle/LKAS side

### What Viewer owns

Viewer is responsible for:

- displaying live frames
- drawing lanes and HUD overlays
- forwarding user actions and parameter updates
- providing the browser dashboard

---

## Recommended Startup Order

The safe startup order is:

1. **LKAS**
2. **Vehicle**
3. **Viewer**

### Why this order

- LKAS should be ready first so shared memory and control loops exist.
- Vehicle should then connect to LKAS and start sending frames.
- Viewer can start last because it is only a subscriber/dashboard.

---

## File-by-File Summary

### LKAS

- Entry point: `Lkas/src/run.py`
- Orchestrator: `Lkas/src/server.py`
- Role: start detection and decision servers, optionally broadcast

### Vehicle

- Entry point: `Vehicle-jetracer/src/main.py`
- Main loop: `Vehicle-jetracer/src/vehicle.py`
- Role: read camera frames, feed LKAS, apply actuation, publish state

### Viewer

- Entry point: `Web-viewer/src/run.py`
- Shared state and handlers: viewer helper modules under `Web-viewer/src/`
- Role: subscribe to data, render the dashboard, send remote commands

---

## Practical Run Commands

```bash
# 1. Start LKAS
lkas --broadcast

# 2. Start vehicle
vehicle

# 3. Start viewer
viewer --target vehicle --port 8080
```

Open the browser at:

```text
http://localhost:8080
```

---

## Notes

- The viewer does not create the vehicle stream by itself.
- The vehicle must publish the data path that LKAS/viewer expect.
- If the viewer shows `Connecting...`, check that LKAS and vehicle are both running and that the expected ZMQ/shared-memory channels match the current config.
- If the vehicle cannot see the camera, the viewer will also stay empty because the frame source is missing.

---

## DCAS Relevance

This current flow is the base flow before DCAS is attached.
When DCAS is integrated, the likely insertion point is inside the vehicle loop, after LKAS control is read and before throttle is applied.

That means the future flow becomes:

- LKAS produces control
- Vehicle receives control
- DCAS may clamp throttle
- Vehicle applies final actuation
- Viewer shows the resulting state
