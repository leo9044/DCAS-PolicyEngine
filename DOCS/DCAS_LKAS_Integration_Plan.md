# DCAS-LKAS Integration Plan

**Objective:** Connect DCAS (Drowsy Countermeasure Assist System) policy engine with LKAS to apply intelligent throttle limiting based on driver state.

---

## System Architecture

```
┌─────────────────┐
│  RealSense      │
│  Camera         │
└────────┬────────┘
         │
         ▼
┌─────────────────────────────────────────────────┐
│         Vehicle Main Loop                       │
│  (Vehicle-jetracer/src/vehicle.py)              │
├─────────────────────────────────────────────────┤
│                                                  │
│  1. Read frame from camera                      │
│  2. Send frame to LKAS (shared memory)          │
│  3. Get LKAS control (steering + throttle)      │
│                                                  │
│  ┌──────────────────────────────────────────┐  │
│  │ [NEW] DCAS Policy Check                  │  │
│  │ - Collect: attentive, reason, timestamp  │  │
│  │ - Call: dcas_policy_runner               │  │
│  │ - Get: throttle_limit                    │  │
│  │ - Apply: throttle = min(lkas_throttle,   │  │
│  │           throttle_limit)                │  │
│  └──────────────────────────────────────────┘  │
│                                                  │
│  4. Apply final throttle to NvidiaRacecar       │
│  5. Update vehicle state → Viewer               │
│                                                  │
└─────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────┐
│  NvidiaRacecar Actuation        │
│  (Motor + Steering)             │
└─────────────────────────────────┘

┌──────────────────────────────────┐
│  LKAS (Detection + Decision)     │
│  - Lane detection (CV/DL)        │
│  - PID steering control          │
│  - Shared memory sync            │
└──────────────────────────────────┘

┌──────────────────────────────────┐
│  Web Viewer                      │
│  - Dashboard (port 8080)         │
│  - Real-time monitoring          │
│  - Parameter tuning              │
└──────────────────────────────────┘
```

---

## Integration Steps

### Step 1: Data Collection at Vehicle Loop

**File:** `Vehicle-jetracer/src/vehicle.py`

**Location:** In the main loop, before applying control.

**Input sources for DCAS:**

```python
# From camera/frame metadata
frame_timestamp_ms = int(timestamp * 1000)
frame_id = frame_id_counter

# From user input (e.g., via viewer)
driver_attentive = True  # or from sensor
driver_reason = "NORMAL"  # or from eye tracking
reason_timestamp_ms = frame_timestamp_ms

# From LKAS control
lkas_throttle = control.throttle  # 0.0-1.0 from LKAS
lkas_mode = "ON_ACTIVE"  # From LKAS state
previous_lkas_mode = "ON_ACTIVE"  # Store last state

# From camera/vehicle state
jetracer_input_0_4 = estimate_speed_band()  # Normalize current speed
```

---

### Step 2: DCAS Policy Runner Invocation

**File:** `Vehicle-jetracer/src/dcas_client.py` (existing)

**Usage in vehicle.py:**

```python
from dcas_client import DCASPolicyClient, DCASMockInput

# Initialize once at startup
dcas_client = DCASPolicyClient()
dcas_client.preflight(auto_build=False)  # Ensure runner is ready

# In main loop, after LKAS control received
dcas_input = DCASMockInput(
    attentive=driver_attentive,
    reason=driver_reason,
    timestamp_ms=frame_timestamp_ms,
    reason_timestamp_ms=reason_timestamp_ms,
    delta_s=elapsed_time_since_last_call,  # seconds
    ticks=1,
    jetracer_input_0_4=jetracer_input_0_4,
    lkas_throttle=lkas_throttle,
    lkas_mode=lkas_mode,
    switch_event="NONE",
    driver_override=False,
    notebook_alive=True
)

# Call DCAS (returns list of policy outputs)
try:
    dcas_outputs = dcas_client.evaluate(dcas_input)
    if dcas_outputs:
        dcas_output = dcas_outputs[-1]  # Take last tick
        throttle_limit = dcas_output.get("throttle_limit", 1.0)
    else:
        throttle_limit = 1.0  # Fallback: no limit
except Exception as e:
    print(f"[DCAS] Error: {e}")
    throttle_limit = 1.0  # Fallback on error
```

---

### Step 3: Apply Throttle Clamping

**File:** `Vehicle-jetracer/src/vehicle.py`

**Location:** In `_apply_control_from_lkas()` or just before `_update_vehicle_state()`

```python
def _apply_control_from_lkas_with_dcas(self):
    """
    Apply control commands from LKAS with DCAS throttle limiting.
    """
    # Get steering from LKAS
    control = self.lkas.get_control(timeout=0.1)
    if control is not None:
        self._set_steering(control.steering)
        
        # Get LKAS throttle
        lkas_throttle = control.throttle
        
        # Apply DCAS throttle limit
        final_throttle = min(lkas_throttle, throttle_limit)
        
        # Set final throttle
        self._set_throttle(final_throttle)
```

---

### Step 4: Error Handling & Fallback

**Safety mechanisms:**

```python
class DCASSafetyManager:
    """Handles DCAS errors and fallbacks."""
    
    def __init__(self, dcas_client):
        self.dcas_client = dcas_client
        self.dcas_available = True
        self.fallback_throttle_limit = 1.0
        self.consecutive_errors = 0
        self.max_errors_before_disable = 5
    
    def get_throttle_limit(self, dcas_input):
        """Get throttle limit with error handling."""
        if not self.dcas_available:
            return self.fallback_throttle_limit
        
        try:
            outputs = self.dcas_client.evaluate(dcas_input)
            if outputs:
                self.consecutive_errors = 0  # Reset error counter
                return outputs[-1].get("throttle_limit", 1.0)
            else:
                raise RuntimeError("DCAS returned empty output")
        
        except Exception as e:
            self.consecutive_errors += 1
            print(f"[DCAS] Error (#{self.consecutive_errors}): {e}")
            
            if self.consecutive_errors >= self.max_errors_before_disable:
                print("[DCAS] Disabling due to repeated errors. Vehicle will use full LKAS throttle.")
                self.dcas_available = False
            
            return self.fallback_throttle_limit
    
    def reset(self):
        """Re-enable DCAS after manual intervention."""
        self.dcas_available = True
        self.consecutive_errors = 0
```

---

### Step 5: State Publishing to Viewer

**File:** `Vehicle-jetracer/src/vehicle.py`

**Enhancement:** Include DCAS state in vehicle status

```python
def _send_state(self, frame_id: int, dcas_throttle_limit: float = 1.0):
    """Send vehicle state including DCAS throttle limit."""
    state = {
        'steering': float(self.steering),
        'throttle': float(self.throttle),
        'brake': float(self.brake),
        'speed_kmh': 0.0,
        'position': None,
        'rotation': None,
        'paused': self.paused,
        
        # [NEW] DCAS state
        'dcas_throttle_limit': float(dcas_throttle_limit),
        'dcas_applied_throttle': float(self.throttle),  # Final throttle after DCAS
    }
    
    # Send via ZMQ to viewer
    self.state_pub.send_state(state)
```

---

## Implementation Checklist

- [ ] **Phase 1: Core Integration**
  - [ ] Add DCAS input collection to Vehicle main loop
  - [ ] Instantiate DCASPolicyClient in Vehicle.__init__()
  - [ ] Call dcas_client.evaluate() in main loop
  - [ ] Apply throttle clamping before _update_vehicle_state()

- [ ] **Phase 2: Error Handling**
  - [ ] Implement DCASSafetyManager
  - [ ] Add try-except around DCAS calls
  - [ ] Log DCAS errors to console
  - [ ] Test fallback when DCAS runner unavailable

- [ ] **Phase 3: Monitoring & Debugging**
  - [ ] Add DCAS state to vehicle status messages
  - [ ] Print DCAS throttle_limit to console (optional verbose mode)
  - [ ] Add DCAS metrics to viewer dashboard (optional)

- [ ] **Phase 4: Testing & Validation**
  - [ ] Unit test: DCAS integration with mock inputs
  - [ ] Integration test: Vehicle + LKAS + DCAS live run
  - [ ] Edge cases: DCAS timeout, corrupted input, invalid output
  - [ ] Performance: Measure DCAS evaluation latency (should be <100ms)

---

## Key Parameters to Track

| Parameter | Source | Range | Purpose |
|-----------|--------|-------|---------|
| `attentive` | Driver monitoring | bool | Is driver attentive? |
| `reason` | Eye tracking / sensor | string | Why is driver drowsy? |
| `jetracer_input_0_4` | Vehicle speed | 0.0-0.4 | Speed band (normalized) |
| `lkas_throttle` | LKAS decision | 0.0-1.0 | Throttle from lane keeping |
| `lkas_mode` | LKAS state | enum | LKAS operational state |
| `throttle_limit` | DCAS policy | 0.0-1.0 | Max allowed throttle |

---

## Expected Behavior

### Scenario 1: Driver Attentive
- `attentive = True`
- DCAS outputs: `throttle_limit = 1.0` (no restriction)
- Vehicle applies full LKAS throttle
- Result: Normal lane-keeping speed

### Scenario 2: Driver Drowsy (WARNING)
- `attentive = False`, `reason = "drowsy"`
- DCAS outputs: `throttle_limit = 0.7` (70% of LKAS)
- Vehicle applies clamped throttle
- Result: Reduced speed as warning to driver

### Scenario 3: Driver Drowsy (ESCALATION)
- `attentive = False`, `reason = "drowsy"` + time > threshold
- DCAS outputs: `throttle_limit = 0.3` (30% of LKAS)
- Vehicle applies heavily clamped throttle
- Result: Strong intervention to wake driver or pull over

---

## Files to Modify

1. **Vehicle-jetracer/src/vehicle.py**
   - Import DCASPolicyClient
   - Add DCAS input collection
   - Add throttle clamping logic
   - Add DCAS state to status messages

2. **Vehicle-jetracer/src/dcas_client.py** (existing - no changes needed)
   - Already has DCASPolicyClient and DCASMockInput

3. **(Optional) Vehicle-jetracer/src/constants.py**
   - Add DCAS timeout constants
   - Add speed band thresholds

---

## Commands to Test

```bash
# Terminal 1: LKAS
lkas --broadcast

# Terminal 2: Vehicle with DCAS
vehicle

# Terminal 3: Viewer
viewer --target vehicle --port 8080

# Browser
http://localhost:8080
```

Watch in browser:
- Lane detection (blue lines)
- Vehicle throttle (slider)
- DCAS throttle_limit (new metric)
- Real-time state changes as drowsiness is simulated

---

## Next Steps

1. Implement Phase 1 (core integration)
2. Run integration test with Vehicle + LKAS + DCAS
3. Validate throttle clamping on web viewer
4. Implement Phase 2 (error handling & safety)
5. Full end-to-end testing with Viewer monitoring

---

**Last Updated:** May 7, 2026  
**Status:** Ready for implementation
