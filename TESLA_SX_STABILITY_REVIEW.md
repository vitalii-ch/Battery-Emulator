# Tesla S/X Stability Review: `tesla_sx_support` vs `main`

**Date:** 2026-03-31
**Author:** vitalii-ch
**Branch compared:** `origin/improvement/tesla_sx_support` (frozen Oct 2024) vs `main` (current)

---

## Context

The old `tesla_sx_support` branch provided stable operation with Tesla Model S/X batteries (1+ year uptime confirmed). After the codebase evolved on `main` (OOP refactoring, new features, new CAN frames), S/X batteries became unstable: they fail to start reliably, require "Reset BMS" commands, and report `VEHICLE_NODE_FAULT` in HVIL status.

---

## Critical Differences Found

### 1. Initial vehicleState changed: DRIVE -> OFF (commit `f2482e1f`)

**Severity: HIGH**

Old behavior:
```cpp
uint8_t vehicleState = 1;      // DRIVE from the start
uint8_t powerDownSeconds = 9;   // Full countdown available
```

New behavior:
```cpp
uint8_t vehicleState = 0;      // OFF at startup
uint8_t powerDownSeconds = 0;   // Countdown exhausted
```

**Impact:** At startup, the emulator now sends OFF-state 0x221 frames instead of DRIVE frames. The BMS receives "car is OFF" commands and will not close contactors. The transition to DRIVE only happens when `update_values()` runs (every 1s) AND `inverter_allows_contactor_closing` is already true. This creates a startup delay and may prevent BMS initialization entirely if the BMS expects DRIVE commands early.

**Fix candidate:** Revert to `vehicleState = CAR_DRIVE` and `powerDownSeconds = 9` as defaults, or make this configurable per battery type.

---

### 2. Digital HVIL (0x1CF/0x118) gated by FAULT status -- chicken-and-egg

**Severity: HIGH**

Old gating:
```cpp
if (datalayer.system.status.inverter_allows_contactor_closing) {
    // Send 0x1CF and 0x118
}
```

New gating:
```cpp
if ((datalayer.system.status.inverter_allows_contactor_closing) &&
    (datalayer.battery.status.bms_status != FAULT)) {
    // Send 0x1CF and 0x118
}
```

**Impact:** This creates a deadlock:
1. BMS starts without digital HVIL signals -> reports `VEHICLE_NODE_FAULT`
2. BMS enters FAULT state
3. Emulator sees FAULT -> stops sending 0x1CF/0x118
4. BMS never receives HVIL signals -> stays in FAULT
5. Loop never breaks without manual "Reset BMS"

**Fix candidate:** Remove `bms_status != FAULT` check from 0x1CF/0x118 gating. Digital HVIL must be sent unconditionally (only gated by `inverter_allows_contactor_closing`), because the BMS NEEDS these signals to clear the HVIL fault.

---

### 3. 0x221 interval changed: 30ms -> 50ms

**Severity: MEDIUM-HIGH**

| Aspect | Old | New |
|--------|-----|-----|
| Interval | 30ms | 50ms |
| Frames per cycle | 2 (TESLA_221_1 + TESLA_221_2) | 1 (Mux0 or Mux1, alternating) |
| Data format | Static bytes | Dynamic with counter + checksum |
| Content | `41 11 01 00 00 00 20 96` then `61 15 01 00 00 00 20 BA` | Muxed DRIVE/ACCESSORY/GOING_DOWN/OFF frames |

**Impact:** The S/X BMS may have strict timing expectations for contactor control messages. The old code sent TWO frames every 30ms cycle. The new code sends ONE frame every 50ms. This is:
- 67% longer interval (30ms -> 50ms)
- Half the message density (2 per cycle -> 1 per cycle)
- Different data content entirely

The BMS may interpret this as a communication timeout or reject the new frame format.

**Fix candidate:** Test with 30ms interval and sending both mux frames per cycle for S/X batteries.

---

### 4. Massive increase in CAN bus traffic

**Severity: MEDIUM**

Old version sent **3 CAN IDs**: 0x221, 0x1CF, 0x118

New version sends **30+ CAN IDs** at various intervals:

| Interval | New frames |
|----------|-----------|
| 10ms | 0x118 (non-HVIL version), 0x2E1 (6 muxes) |
| 50ms | 0x221, 0x3C2, 0x39D, 0x3A1 |
| 100ms | 0x102, 0x103, 0x229, 0x241, 0x2D1, 0x2A8, 0x2E8, 0x7FF (5 muxes), 0x602 |
| 500ms | 0x213, 0x284, 0x293, 0x313, 0x333, 0x334, 0x3B3, 0x55A |
| 1000ms | 0x082, 0x321 |

**Impact:**
- CAN bus load significantly increased, potentially delaying critical messages
- S/X BMS may react differently to frames designed for 3/Y (e.g., 0x7FF GTW_carConfig with chassisType)
- Some frames may confuse the S/X BMS or trigger unexpected behavior
- The checksum/counter generation for all these frames adds CPU overhead, potentially causing timing jitter on critical messages

**Fix candidate:** Consider sending only the essential frames for S/X: 0x221, 0x1CF, 0x118 (like the old version), and add others incrementally to identify which cause issues.

---

### 5. 0x3A1 only sent when contactors are CLOSED

**Severity: MEDIUM**

```cpp
if (battery_contactor == 4) {  // Contactors closed
    transmit_can_frame(&TESLA_3A1);  // VCFRONT_vehicleStatus
}
```

**Impact:** Frame 0x3A1 prevents `VCFRONT_MIA_InDrive` fault. But if contactors never close (due to issues #1-#3 above), this frame is never sent, potentially compounding the problem. The BMS may need this frame during the closing sequence, not just after.

---

### 6. update_values interval: 5000ms -> 1000ms

**Severity: LOW-MEDIUM**

The vehicleState state machine (DRIVE -> ACCESSORY -> GOING_DOWN -> OFF) ticks 5x faster:
- Old: each state lasted ~15s (3 ticks * 5s), total shutdown = 45s
- New: each state lasts ~3s (3 ticks * 1s), total shutdown = 9s

**Impact:** The faster shutdown may not give the S/X BMS enough time to properly open contactors. S/X packs are physically larger and may need more time for the contactor control sequence.

---

### 7. 0x118 frame handling diverged

**Severity: MEDIUM**

Old: 0x118 is a 16-frame rotating array sent at 10ms (digital HVIL mode only)
New: Two different 0x118 paths:
- Digital HVIL mode: same rotating array at 10ms
- Non-digital HVIL mode: single static `TESLA_118` (DI_systemStatus) with counter+checksum at 10ms

**Impact:** If an S/X battery is used WITHOUT digital HVIL enabled, it now receives a completely different 0x118 frame that wasn't sent in the old version at all. This frame (DI_systemStatus) may confuse the BMS.

---

## Summary Table

| # | Issue | Severity | Old Behavior | New Behavior | Likely Impact |
|---|-------|----------|-------------|-------------|---------------|
| 1 | vehicleState init | HIGH | DRIVE | OFF | BMS won't close contactors at startup |
| 2 | HVIL FAULT gating | HIGH | No FAULT check | FAULT blocks HVIL | Deadlock: FAULT prevents fix |
| 3 | 0x221 timing | MED-HIGH | 30ms, 2 frames | 50ms, 1 frame | BMS timeout on contactor cmd |
| 4 | CAN bus load | MEDIUM | 3 frame types | 30+ frame types | Timing jitter, BMS confusion |
| 5 | 0x3A1 gating | MEDIUM | N/A (not sent) | Only when closed | Missing during close sequence |
| 6 | State machine speed | LOW-MED | 45s shutdown | 9s shutdown | Too fast for S/X |
| 7 | 0x118 dual mode | MEDIUM | HVIL only | Always (different data) | Wrong frame for S/X |

---

## Recommended Fix Priority

1. **Remove FAULT check from digital HVIL gating** (issue #2) -- this is the most likely cause of `VEHICLE_NODE_FAULT` and the need for "Reset BMS"
2. **Revert initial vehicleState to DRIVE** (issue #1) -- or add S/X-specific startup logic
3. **Test 30ms interval for 0x221 with dual frames** (issue #3) -- for S/X specifically
4. **Reduce CAN frame set for S/X** (issue #4) -- send only frames the S/X BMS expects
5. **Send 0x3A1 during contactor closing** (issue #5) -- not just when already closed
6. **Adjust shutdown timing for S/X** (issue #6) -- longer transition periods

---

## Files Referenced

- `Software/src/battery/TESLA-BATTERY.cpp` -- main battery logic (current)
- `Software/src/battery/TESLA-BATTERY.h` -- constants, class definition (current)
- `Software/src/battery/TESLA-LEGACY-BATTERY.cpp` -- legacy S/X 2012-2020 (new, separate)
- `Software/Software.cpp` -- main loop, task scheduling
- `Software/src/devboard/utils/types.h` -- timing constants
- `Software/src/devboard/safety/safety.cpp` -- safety checks, CAN alive counter
- `Software/src/communication/contactorcontrol/comm_contactorcontrol.cpp` -- GPIO contactor control

## Key Commits

- `f2482e1f` (2026-03-22) -- "set default state to OFF instead of DRIVE for tesla" -- **SUSPECT**
- `02444686` (2025-09-17) -- "Make contactor opening take 9s instead of 60s"
- `f48b4235` (2025-09-25) -- "Simplify DigitalHVIL sending for performance"
- `ad316bbf` (2025-10-03) -- "Add filtering of opening of 3A1 message"
- `8a1e34c4` (~2025) -- "Tesla battery class" (OOP refactoring)
- `e299b867` (2025-09) -- "Remove all ifdefs from Tesla code"
