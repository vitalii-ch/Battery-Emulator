# Battery Cluster Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Дати змогу підключати до 8 батарейних пакетів паралельно через distributed master/satellite архітектуру з власним cluster CAN-протоколом, повторно використовуючи всі існуючі battery- та inverter-драйвери.

**Architecture:** Кожен пакет — окремий ESP32 з BE (satellite), який публікує телеметрію новим "інвертор-протоколом" `BE Cluster Node` на cluster CAN-шину. Окремий ESP32 (master) приймає до 8 satellite через нову "батарею" `BE Cluster`, агрегує дані в `datalayer.battery` і використовує будь-який існуючий inverter-driver. Однонапрямний протокол, контактори керуються зовнішньою апаратурою.

**Tech Stack:** C++ (Arduino framework, ESP32), GoogleTest для unit-тестів на host (CMake), CAN @ 500 kbps standard 11-bit ID.

**Spec:** [`docs/superpowers/specs/2026-05-07-battery-cluster-design.md`](../specs/2026-05-07-battery-cluster-design.md)

---

## File Structure

**Create:**
- `Software/src/inverter/CLUSTER-NODE-CAN.h` — `ClusterNodeCanInverter` class
- `Software/src/inverter/CLUSTER-NODE-CAN.cpp` — implementation
- `Software/src/battery/CLUSTER-CAN.h` — `ClusterCanBattery` class + `PackSnapshot`
- `Software/src/battery/CLUSTER-CAN.cpp` — implementation
- `Software/src/battery/CLUSTER-PROTOCOL.h` — shared protocol constants (frame IDs, periods, helpers)
- `Software/src/battery/CLUSTER-PROTOCOL.cpp` — pure aggregation/encode/decode helpers (testable)
- `Software/src/battery/CLUSTER-HTML.h` — `ClusterHtmlRenderer` declaration
- `Software/src/battery/CLUSTER-HTML.cpp` — per-pack health table rendering
- `test/cluster_protocol_tests.cpp` — gtest cases for encoding/decoding
- `test/cluster_aggregation_tests.cpp` — gtest cases for aggregation rules

**Modify:**
- `Software/src/inverter/InverterProtocol.h:6-32` — add `ClusterNodeCan = 25` enum value
- `Software/src/inverter/INVERTERS.h:7-29` — add `#include "CLUSTER-NODE-CAN.h"`
- `Software/src/inverter/INVERTERS.cpp:38-116, 118-…` — add cases to `name_for_inverter_type()` and `setup_inverter()`
- `Software/src/battery/Battery.h:8-61` — add `ClusterCan = 53`
- `Software/src/battery/BATTERIES.h` — add `#include "CLUSTER-CAN.h"` (look at existing includes)
- `Software/src/battery/BATTERIES.cpp:43-152, 162-271` — add cases to `name_for_battery_type()` and `create_battery()`
- `Software/src/devboard/utils/events.h:12-124` — add 6 new event names
- `Software/src/devboard/utils/events.cpp` — set severity for new events; add user-facing strings
- `Software/src/communication/nvm/comm_nvm.cpp` — load/save `CLSTPACKID` and `CLSTPACKCNT`
- `Software/src/devboard/webserver/settings_html.cpp` — UI inputs for new settings
- `test/CMakeLists.txt:69-187` — add new sources

---

## Task 1: Add new event codes

**Why first:** інші компоненти будуть set_event'ити ці коди. Краще щоб enum значення вже існувало.

**Files:**
- Modify: `Software/src/devboard/utils/events.h:122-124`
- Modify: `Software/src/devboard/utils/events.cpp` (init_events block ~line 75; switch-case ~line 267)

- [ ] **Step 1: Додати event-коди в enum**

Edit `Software/src/devboard/utils/events.h`. Знайти рядок `XX(EVENT_GPIO_CONFLICT)` (~line 123) і вставити перед `XX(EVENT_NOF_EVENTS)` шість нових:

```cpp
  XX(EVENT_GPIO_CONFLICT)               \
  XX(EVENT_CLUSTER_PACK_LOST)           \
  XX(EVENT_CLUSTER_DUPLICATE_PACK_ID)   \
  XX(EVENT_CLUSTER_UNCONFIGURED_PACK)   \
  XX(EVENT_CLUSTER_VOLTAGE_DIVERGENCE)  \
  XX(EVENT_CLUSTER_INSUFFICIENT_PACKS)  \
  XX(EVENT_CLUSTER_TOPOLOGY_MISMATCH)   \
  XX(EVENT_NOF_EVENTS)
```

- [ ] **Step 2: Призначити severity-рівні в `init_events()`**

Edit `Software/src/devboard/utils/events.cpp`. Знайти блок де призначаються `events.entries[EVENT_VOLTAGE_DIFFERENCE].level` (~line 75) і поряд додати:

```cpp
  events.entries[EVENT_CLUSTER_PACK_LOST].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_CLUSTER_DUPLICATE_PACK_ID].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_CLUSTER_UNCONFIGURED_PACK].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_CLUSTER_VOLTAGE_DIVERGENCE].level = EVENT_LEVEL_WARNING;
  events.entries[EVENT_CLUSTER_INSUFFICIENT_PACKS].level = EVENT_LEVEL_ERROR;
  events.entries[EVENT_CLUSTER_TOPOLOGY_MISMATCH].level = EVENT_LEVEL_ERROR;
```

- [ ] **Step 3: Додати user-facing описи**

В тому ж файлі `events.cpp` знайти `case EVENT_VOLTAGE_DIFFERENCE:` (~line 267) і поряд (в тому ж switch) додати:

```cpp
    case EVENT_CLUSTER_PACK_LOST:
      return "Cluster pack timeout: a satellite pack stopped responding. Check pack power and CAN wiring.";
    case EVENT_CLUSTER_DUPLICATE_PACK_ID:
      return "Cluster duplicate pack ID detected. Two satellites are using the same pack_id setting.";
    case EVENT_CLUSTER_UNCONFIGURED_PACK:
      return "Cluster unconfigured pack: a satellite is transmitting with pack_id=0. Set Pack ID on each satellite.";
    case EVENT_CLUSTER_VOLTAGE_DIVERGENCE:
      return "Cluster voltage divergence: pack voltages differ by more than 5V. Check pack health and contactor states.";
    case EVENT_CLUSTER_INSUFFICIENT_PACKS:
      return "Cluster insufficient packs: fewer alive packs than expected. Cluster set to FAULT, charge/discharge disabled.";
    case EVENT_CLUSTER_TOPOLOGY_MISMATCH:
      return "Cluster topology mismatch: satellite packs report different chemistry or cell count.";
```

- [ ] **Step 4: Build перевірка**

Run: `cmake -S test -B test/build && cmake --build test/build -j 2>&1 | tail -20`
Expected: жодних помилок компіляції в events.cpp; решта файлів ще можуть бути не зачеплені.

- [ ] **Step 5: Commit**

```bash
git add Software/src/devboard/utils/events.h Software/src/devboard/utils/events.cpp
git commit -m "$(cat <<'EOF'
Add cluster event codes for battery cluster master/satellite

EOF
)"
```

---

## Task 2: Add new enum values for ClusterCan battery and ClusterNodeCan inverter

**Files:**
- Modify: `Software/src/inverter/InverterProtocol.h:6-32`
- Modify: `Software/src/battery/Battery.h:8-61`

- [ ] **Step 1: Додати `ClusterNodeCan` в InverterProtocolType**

Edit `Software/src/inverter/InverterProtocol.h:30`. Знайти `SmaSBSByd = 24,` і вставити після:

```cpp
  PylonLV485 = 23,
  SmaSBSByd = 24,
  ClusterNodeCan = 25,
  Highest
};
```

- [ ] **Step 2: Додати `ClusterCan` в BatteryType**

Edit `Software/src/battery/Battery.h:60`. Знайти `EnnoidBMS = 52,` і вставити після:

```cpp
  ThunderstruckBMS = 51,
  EnnoidBMS = 52,
  ClusterCan = 53,
  Highest
};
```

- [ ] **Step 3: Build перевірка**

Run: `cmake --build test/build -j 2>&1 | tail -20`
Expected: enum значення додано, але `name_for_*` switch'і ще не покривають їх — це ok, поки ми не використовуємо ці значення. Можуть бути unused-warning'и які ми виправимо в Task 5.

- [ ] **Step 4: Commit**

```bash
git add Software/src/inverter/InverterProtocol.h Software/src/battery/Battery.h
git commit -m "$(cat <<'EOF'
Add ClusterNodeCan and ClusterCan enum values

EOF
)"
```

---

## Task 3: Cluster protocol header — frame IDs, periods, layouts

**Files:**
- Create: `Software/src/battery/CLUSTER-PROTOCOL.h`

- [ ] **Step 1: Створити файл**

Create `Software/src/battery/CLUSTER-PROTOCOL.h`:

```cpp
#ifndef CLUSTER_PROTOCOL_H
#define CLUSTER_PROTOCOL_H

#include <stdint.h>

// Cluster CAN protocol — однонапрямний (satellite → master).
// Address scheme: CAN ID = base + pack_id, pack_id ∈ 1..8.
// Pack_id = 0 reserved as "unconfigured" sentinel.

namespace cluster_protocol {

constexpr uint8_t MAX_PACKS = 8;
constexpr uint8_t MIN_VALID_PACK_ID = 1;
constexpr uint8_t MAX_VALID_PACK_ID = 8;

// Frame base IDs (lower nibble = pack_id)
constexpr uint16_t FRAME0_BASE = 0x500;  // Live status
constexpr uint16_t FRAME1_BASE = 0x510;  // Limits + cells
constexpr uint16_t FRAME2_BASE = 0x520;  // Temp + state
constexpr uint16_t FRAME3_BASE = 0x530;  // Capacity
constexpr uint16_t FRAME4_BASE = 0x540;  // Static info

// Transmit periods (ms)
constexpr uint32_t FRAME0_PERIOD_MS = 100;
constexpr uint32_t FRAME1_PERIOD_MS = 200;
constexpr uint32_t FRAME2_PERIOD_MS = 500;
constexpr uint32_t FRAME3_PERIOD_MS = 1000;
constexpr uint32_t FRAME4_PERIOD_MS = 5000;

// Master-side timeouts/thresholds
constexpr uint32_t PACK_TIMEOUT_MS = 1000;
constexpr uint16_t VOLTAGE_DIVERGENCE_THRESHOLD_DV = 50;  // 5.0V

// Helpers
constexpr uint16_t frame_id(uint16_t base, uint8_t pack_id) { return base + pack_id; }
constexpr uint8_t pack_id_from_frame(uint16_t frame_id_value, uint16_t base) {
  return static_cast<uint8_t>(frame_id_value - base);
}
constexpr bool is_frame_for_base(uint16_t frame_id_value, uint16_t base) {
  return frame_id_value >= base && frame_id_value <= (base + MAX_VALID_PACK_ID);
}

// Per-pack snapshot of all fields received from frames 0..4
struct PackSnapshot {
  uint32_t last_seen_ms;
  bool alive;
  bool seen_ever;
  uint8_t last_seq;

  // Frame 0
  uint16_t voltage_dV;
  int16_t  current_dA;
  uint16_t reported_soc;       // 0..10000
  uint8_t  bms_status;         // bms_status_enum

  // Frame 1
  uint16_t max_charge_current_dA;
  uint16_t max_discharge_current_dA;
  uint16_t cell_max_voltage_mV;
  uint16_t cell_min_voltage_mV;

  // Frame 2
  int16_t  temperature_max_dC;
  int16_t  temperature_min_dC;
  uint16_t soh_pptt;
  uint8_t  balancing_status;
  uint8_t  real_bms_status;

  // Frame 3
  uint32_t total_capacity_Wh;
  uint32_t remaining_capacity_Wh;

  // Frame 4 (static info)
  uint16_t max_design_voltage_dV;
  uint16_t min_design_voltage_dV;
  uint16_t max_cell_voltage_limit_mV;
  uint8_t  chemistry;
  uint8_t  number_of_cells;
};

// Encode helpers (satellite-side) — write 8 bytes into provided buffer
void encode_frame0(uint8_t buf[8], uint16_t voltage_dV, int16_t current_dA,
                   uint16_t reported_soc, uint8_t bms_status, uint8_t seq);
void encode_frame1(uint8_t buf[8], uint16_t max_charge_current_dA, uint16_t max_discharge_current_dA,
                   uint16_t cell_max_mV, uint16_t cell_min_mV);
void encode_frame2(uint8_t buf[8], int16_t temp_max_dC, int16_t temp_min_dC,
                   uint16_t soh_pptt, uint8_t balancing_status, uint8_t real_bms_status);
void encode_frame3(uint8_t buf[8], uint32_t total_Wh, uint32_t remaining_Wh);
void encode_frame4(uint8_t buf[8], uint16_t max_design_voltage_dV, uint16_t min_design_voltage_dV,
                   uint16_t max_cell_voltage_limit_mV, uint8_t chemistry, uint8_t number_of_cells);

// Decode helpers (master-side) — read 8 bytes into snapshot fields
void decode_frame0(const uint8_t buf[8], PackSnapshot& s);
void decode_frame1(const uint8_t buf[8], PackSnapshot& s);
void decode_frame2(const uint8_t buf[8], PackSnapshot& s);
void decode_frame3(const uint8_t buf[8], PackSnapshot& s);
void decode_frame4(const uint8_t buf[8], PackSnapshot& s);

}  // namespace cluster_protocol

#endif
```

- [ ] **Step 2: Commit**

```bash
git add Software/src/battery/CLUSTER-PROTOCOL.h
git commit -m "$(cat <<'EOF'
Add cluster protocol header with frame IDs, periods, and helpers

EOF
)"
```

---

## Task 4: TDD encode/decode — RED

**Files:**
- Create: `test/cluster_protocol_tests.cpp`

- [ ] **Step 1: Створити failing test**

Create `test/cluster_protocol_tests.cpp`:

```cpp
#include <gtest/gtest.h>
#include "../Software/src/battery/CLUSTER-PROTOCOL.h"

using namespace cluster_protocol;

TEST(ClusterProtocol, FrameIdHelper) {
  EXPECT_EQ(frame_id(FRAME0_BASE, 1), 0x501);
  EXPECT_EQ(frame_id(FRAME0_BASE, 8), 0x508);
  EXPECT_EQ(frame_id(FRAME3_BASE, 5), 0x535);
  EXPECT_EQ(frame_id(FRAME4_BASE, 8), 0x548);
}

TEST(ClusterProtocol, PackIdFromFrame) {
  EXPECT_EQ(pack_id_from_frame(0x503, FRAME0_BASE), 3);
  EXPECT_EQ(pack_id_from_frame(0x547, FRAME4_BASE), 7);
}

TEST(ClusterProtocol, IsFrameForBase) {
  EXPECT_TRUE(is_frame_for_base(0x501, FRAME0_BASE));
  EXPECT_TRUE(is_frame_for_base(0x508, FRAME0_BASE));
  EXPECT_FALSE(is_frame_for_base(0x509, FRAME0_BASE));  // out of pack_id range
  EXPECT_FALSE(is_frame_for_base(0x500, FRAME0_BASE));  // pack_id=0 reserved (NOT a valid satellite frame)
}

TEST(ClusterProtocol, EncodeDecodeFrame0Roundtrip) {
  uint8_t buf[8] = {0};
  encode_frame0(buf, /*voltage_dV*/ 4023, /*current_dA*/ -185,
                /*reported_soc*/ 7350, /*bms_status*/ 4, /*seq*/ 42);
  PackSnapshot s = {};
  decode_frame0(buf, s);
  EXPECT_EQ(s.voltage_dV, 4023);
  EXPECT_EQ(s.current_dA, -185);
  EXPECT_EQ(s.reported_soc, 7350);
  EXPECT_EQ(s.bms_status, 4);
  EXPECT_EQ(s.last_seq, 42);
}

TEST(ClusterProtocol, EncodeDecodeFrame1Roundtrip) {
  uint8_t buf[8] = {0};
  encode_frame1(buf, /*max_charge*/ 1500, /*max_discharge*/ 2000,
                /*cell_max*/ 4150, /*cell_min*/ 3920);
  PackSnapshot s = {};
  decode_frame1(buf, s);
  EXPECT_EQ(s.max_charge_current_dA, 1500);
  EXPECT_EQ(s.max_discharge_current_dA, 2000);
  EXPECT_EQ(s.cell_max_voltage_mV, 4150);
  EXPECT_EQ(s.cell_min_voltage_mV, 3920);
}

TEST(ClusterProtocol, EncodeDecodeFrame2Roundtrip) {
  uint8_t buf[8] = {0};
  encode_frame2(buf, /*tmax*/ 285, /*tmin*/ -52, /*soh*/ 9650,
                /*balancing_status*/ 1, /*real_bms_status*/ 2);
  PackSnapshot s = {};
  decode_frame2(buf, s);
  EXPECT_EQ(s.temperature_max_dC, 285);
  EXPECT_EQ(s.temperature_min_dC, -52);
  EXPECT_EQ(s.soh_pptt, 9650);
  EXPECT_EQ(s.balancing_status, 1);
  EXPECT_EQ(s.real_bms_status, 2);
}

TEST(ClusterProtocol, EncodeDecodeFrame3Roundtrip) {
  uint8_t buf[8] = {0};
  encode_frame3(buf, /*total*/ 30000, /*remaining*/ 21500);
  PackSnapshot s = {};
  decode_frame3(buf, s);
  EXPECT_EQ(s.total_capacity_Wh, 30000u);
  EXPECT_EQ(s.remaining_capacity_Wh, 21500u);
}

TEST(ClusterProtocol, EncodeDecodeFrame4Roundtrip) {
  uint8_t buf[8] = {0};
  encode_frame4(buf, /*vmax*/ 4500, /*vmin*/ 3000, /*cell_limit*/ 4250,
                /*chemistry*/ 2, /*ncells*/ 96);
  PackSnapshot s = {};
  decode_frame4(buf, s);
  EXPECT_EQ(s.max_design_voltage_dV, 4500);
  EXPECT_EQ(s.min_design_voltage_dV, 3000);
  EXPECT_EQ(s.max_cell_voltage_limit_mV, 4250);
  EXPECT_EQ(s.chemistry, 2);
  EXPECT_EQ(s.number_of_cells, 96);
}

TEST(ClusterProtocol, Frame0LittleEndianLayout) {
  uint8_t buf[8] = {0};
  encode_frame0(buf, /*voltage_dV*/ 0x1234, /*current_dA*/ 0x0102,
                /*reported_soc*/ 0xABCD, /*bms_status*/ 0x05, /*seq*/ 0x42);
  EXPECT_EQ(buf[0], 0x34);  // voltage low
  EXPECT_EQ(buf[1], 0x12);  // voltage high
  EXPECT_EQ(buf[2], 0x02);  // current low
  EXPECT_EQ(buf[3], 0x01);  // current high
  EXPECT_EQ(buf[4], 0xCD);  // soc low
  EXPECT_EQ(buf[5], 0xAB);  // soc high
  EXPECT_EQ(buf[6], 0x05);
  EXPECT_EQ(buf[7], 0x42);
}
```

- [ ] **Step 2: Зареєструвати в CMakeLists.txt**

Edit `test/CMakeLists.txt:69-187`. Знайти список `add_executable(tests` та додати дві нові лінії:

```cmake
add_executable(tests 
    tests.cpp
    safety_tests.cpp
    voltage_sync_tests.cpp
    bms_reset_tests.cpp
    cluster_protocol_tests.cpp
    ...
```

Також додай `../Software/src/battery/CLUSTER-PROTOCOL.cpp` в список source files (буде створено в Task 5).

- [ ] **Step 3: Створити stub `.cpp` щоб build'ився (буде перевизначено в Task 5)**

Create empty `Software/src/battery/CLUSTER-PROTOCOL.cpp`:

```cpp
#include "CLUSTER-PROTOCOL.h"

namespace cluster_protocol {
void encode_frame0(uint8_t[8], uint16_t, int16_t, uint16_t, uint8_t, uint8_t) {}
void encode_frame1(uint8_t[8], uint16_t, uint16_t, uint16_t, uint16_t) {}
void encode_frame2(uint8_t[8], int16_t, int16_t, uint16_t, uint8_t, uint8_t) {}
void encode_frame3(uint8_t[8], uint32_t, uint32_t) {}
void encode_frame4(uint8_t[8], uint16_t, uint16_t, uint16_t, uint8_t, uint8_t) {}
void decode_frame0(const uint8_t[8], PackSnapshot&) {}
void decode_frame1(const uint8_t[8], PackSnapshot&) {}
void decode_frame2(const uint8_t[8], PackSnapshot&) {}
void decode_frame3(const uint8_t[8], PackSnapshot&) {}
void decode_frame4(const uint8_t[8], PackSnapshot&) {}
}
```

- [ ] **Step 4: Run tests — verify they FAIL**

Run: `cmake -S test -B test/build && cmake --build test/build -j && cd test/build && ctest --output-on-failure -R ClusterProtocol`
Expected: всі ClusterProtocol тести FAIL (через stub'и).

- [ ] **Step 5: Commit (failing tests)**

```bash
git add test/cluster_protocol_tests.cpp test/CMakeLists.txt Software/src/battery/CLUSTER-PROTOCOL.cpp
git commit -m "$(cat <<'EOF'
Add failing tests for cluster protocol encode/decode

EOF
)"
```

---

## Task 5: TDD encode/decode — GREEN

**Files:**
- Modify: `Software/src/battery/CLUSTER-PROTOCOL.cpp`

- [ ] **Step 1: Реалізувати encode/decode**

Replace contents of `Software/src/battery/CLUSTER-PROTOCOL.cpp`:

```cpp
#include "CLUSTER-PROTOCOL.h"

namespace cluster_protocol {

static inline void put_u16_le(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}
static inline void put_i16_le(uint8_t* p, int16_t v) { put_u16_le(p, (uint16_t)v); }
static inline void put_u32_le(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static inline uint16_t get_u16_le(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline int16_t get_i16_le(const uint8_t* p) { return (int16_t)get_u16_le(p); }
static inline uint32_t get_u32_le(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void encode_frame0(uint8_t buf[8], uint16_t voltage_dV, int16_t current_dA,
                   uint16_t reported_soc, uint8_t bms_status, uint8_t seq) {
  put_u16_le(buf + 0, voltage_dV);
  put_i16_le(buf + 2, current_dA);
  put_u16_le(buf + 4, reported_soc);
  buf[6] = bms_status;
  buf[7] = seq;
}

void encode_frame1(uint8_t buf[8], uint16_t max_charge_current_dA, uint16_t max_discharge_current_dA,
                   uint16_t cell_max_mV, uint16_t cell_min_mV) {
  put_u16_le(buf + 0, max_charge_current_dA);
  put_u16_le(buf + 2, max_discharge_current_dA);
  put_u16_le(buf + 4, cell_max_mV);
  put_u16_le(buf + 6, cell_min_mV);
}

void encode_frame2(uint8_t buf[8], int16_t temp_max_dC, int16_t temp_min_dC,
                   uint16_t soh_pptt, uint8_t balancing_status, uint8_t real_bms_status) {
  put_i16_le(buf + 0, temp_max_dC);
  put_i16_le(buf + 2, temp_min_dC);
  put_u16_le(buf + 4, soh_pptt);
  buf[6] = balancing_status;
  buf[7] = real_bms_status;
}

void encode_frame3(uint8_t buf[8], uint32_t total_Wh, uint32_t remaining_Wh) {
  put_u32_le(buf + 0, total_Wh);
  put_u32_le(buf + 4, remaining_Wh);
}

void encode_frame4(uint8_t buf[8], uint16_t max_design_voltage_dV, uint16_t min_design_voltage_dV,
                   uint16_t max_cell_voltage_limit_mV, uint8_t chemistry, uint8_t number_of_cells) {
  put_u16_le(buf + 0, max_design_voltage_dV);
  put_u16_le(buf + 2, min_design_voltage_dV);
  put_u16_le(buf + 4, max_cell_voltage_limit_mV);
  buf[6] = chemistry;
  buf[7] = number_of_cells;
}

void decode_frame0(const uint8_t buf[8], PackSnapshot& s) {
  s.voltage_dV = get_u16_le(buf + 0);
  s.current_dA = get_i16_le(buf + 2);
  s.reported_soc = get_u16_le(buf + 4);
  s.bms_status = buf[6];
  s.last_seq = buf[7];
}

void decode_frame1(const uint8_t buf[8], PackSnapshot& s) {
  s.max_charge_current_dA = get_u16_le(buf + 0);
  s.max_discharge_current_dA = get_u16_le(buf + 2);
  s.cell_max_voltage_mV = get_u16_le(buf + 4);
  s.cell_min_voltage_mV = get_u16_le(buf + 6);
}

void decode_frame2(const uint8_t buf[8], PackSnapshot& s) {
  s.temperature_max_dC = get_i16_le(buf + 0);
  s.temperature_min_dC = get_i16_le(buf + 2);
  s.soh_pptt = get_u16_le(buf + 4);
  s.balancing_status = buf[6];
  s.real_bms_status = buf[7];
}

void decode_frame3(const uint8_t buf[8], PackSnapshot& s) {
  s.total_capacity_Wh = get_u32_le(buf + 0);
  s.remaining_capacity_Wh = get_u32_le(buf + 4);
}

void decode_frame4(const uint8_t buf[8], PackSnapshot& s) {
  s.max_design_voltage_dV = get_u16_le(buf + 0);
  s.min_design_voltage_dV = get_u16_le(buf + 2);
  s.max_cell_voltage_limit_mV = get_u16_le(buf + 4);
  s.chemistry = buf[6];
  s.number_of_cells = buf[7];
}

}  // namespace cluster_protocol
```

- [ ] **Step 2: Run tests — verify all PASS**

Run: `cmake --build test/build -j && cd test/build && ctest --output-on-failure -R ClusterProtocol`
Expected: всі тести PASS.

- [ ] **Step 3: Commit**

```bash
git add Software/src/battery/CLUSTER-PROTOCOL.cpp
git commit -m "$(cat <<'EOF'
Implement cluster protocol encode/decode helpers

EOF
)"
```

---

## Task 6: TDD aggregation rules — RED

**Files:**
- Create: `test/cluster_aggregation_tests.cpp`
- Modify: `Software/src/battery/CLUSTER-PROTOCOL.h` — додати aggregation API
- Modify: `Software/src/battery/CLUSTER-PROTOCOL.cpp` — додати stubs

- [ ] **Step 1: Розширити header з aggregation API**

Edit `Software/src/battery/CLUSTER-PROTOCOL.h`. Перед `}  // namespace cluster_protocol` додати:

```cpp
// Aggregation result — fields populated by aggregate() from alive packs
struct AggregateResult {
  uint8_t  n_alive;
  uint16_t voltage_dV;          // mean
  int16_t  current_dA;          // sum
  uint32_t reported_soc;        // capacity-weighted mean (0..10000)
  uint16_t max_charge_current_dA;     // min × N_alive (0 if any pack=0)
  uint16_t max_discharge_current_dA;  // min × N_alive (0 if any pack=0)
  uint16_t cell_max_voltage_mV;       // MAX
  uint16_t cell_min_voltage_mV;       // MIN
  int16_t  temperature_max_dC;        // MAX
  int16_t  temperature_min_dC;        // MIN
  uint16_t soh_pptt;                  // MIN
  uint8_t  bms_status;                // worst-of (FAULT > UPDATING > STANDBY > IDLE > ACTIVE)
  uint8_t  balancing_status;          // OR (1 if any active)
  uint32_t total_capacity_Wh;         // SUM
  uint32_t remaining_capacity_Wh;     // SUM
  uint16_t voltage_max_dV;            // for divergence check
  uint16_t voltage_min_dV;            // for divergence check
};

// Compute aggregate over packs[0..MAX_PACKS-1]; only entries with alive=true considered.
// Pure function — no side effects, no datalayer access. Easy to test.
AggregateResult aggregate(const PackSnapshot packs[MAX_PACKS]);
```

- [ ] **Step 2: Stub implementation in `.cpp`**

Edit `Software/src/battery/CLUSTER-PROTOCOL.cpp`. Перед `}  // namespace cluster_protocol` додати:

```cpp
AggregateResult aggregate(const PackSnapshot[MAX_PACKS]) {
  AggregateResult r = {};
  return r;
}
```

- [ ] **Step 3: Створити failing tests**

Create `test/cluster_aggregation_tests.cpp`:

```cpp
#include <gtest/gtest.h>
#include "../Software/src/battery/CLUSTER-PROTOCOL.h"
#include "../Software/src/devboard/utils/types.h"

using namespace cluster_protocol;

// Helper: build an alive PackSnapshot
static PackSnapshot make_pack(uint16_t v_dV, int16_t i_dA, uint16_t soc_pptt,
                              uint16_t max_chg_dA, uint16_t max_dis_dA,
                              uint32_t total_Wh, uint32_t rem_Wh,
                              uint8_t bms_status = ACTIVE) {
  PackSnapshot p = {};
  p.alive = true;
  p.seen_ever = true;
  p.voltage_dV = v_dV;
  p.current_dA = i_dA;
  p.reported_soc = soc_pptt;
  p.max_charge_current_dA = max_chg_dA;
  p.max_discharge_current_dA = max_dis_dA;
  p.total_capacity_Wh = total_Wh;
  p.remaining_capacity_Wh = rem_Wh;
  p.bms_status = bms_status;
  p.cell_max_voltage_mV = 4100;
  p.cell_min_voltage_mV = 3900;
  p.temperature_max_dC = 250;
  p.temperature_min_dC = 200;
  p.soh_pptt = 9800;
  return p;
}

TEST(ClusterAggregation, NoAlivePacksReturnsZeros) {
  PackSnapshot packs[MAX_PACKS] = {};
  AggregateResult r = aggregate(packs);
  EXPECT_EQ(r.n_alive, 0);
  EXPECT_EQ(r.max_charge_current_dA, 0);
  EXPECT_EQ(r.max_discharge_current_dA, 0);
}

TEST(ClusterAggregation, VoltageIsMean) {
  PackSnapshot packs[MAX_PACKS] = {};
  packs[0] = make_pack(4000, 0, 5000, 100, 100, 30000, 15000);
  packs[1] = make_pack(4020, 0, 5000, 100, 100, 30000, 15000);
  packs[2] = make_pack(4040, 0, 5000, 100, 100, 30000, 15000);
  AggregateResult r = aggregate(packs);
  EXPECT_EQ(r.voltage_dV, 4020);  // (4000+4020+4040)/3
}

TEST(ClusterAggregation, CurrentIsSum) {
  PackSnapshot packs[MAX_PACKS] = {};
  packs[0] = make_pack(4000, 100, 5000, 100, 100, 30000, 15000);
  packs[1] = make_pack(4000, 150, 5000, 100, 100, 30000, 15000);
  packs[2] = make_pack(4000, -50, 5000, 100, 100, 30000, 15000);
  AggregateResult r = aggregate(packs);
  EXPECT_EQ(r.current_dA, 200);  // 100 + 150 - 50
}

TEST(ClusterAggregation, CapacityIsSum) {
  PackSnapshot packs[MAX_PACKS] = {};
  packs[0] = make_pack(4000, 0, 5000, 100, 100, 30000, 15000);
  packs[1] = make_pack(4000, 0, 5000, 100, 100, 25000, 10000);
  AggregateResult r = aggregate(packs);
  EXPECT_EQ(r.total_capacity_Wh, 55000u);
  EXPECT_EQ(r.remaining_capacity_Wh, 25000u);
}

TEST(ClusterAggregation, SocIsCapacityWeightedMean) {
  PackSnapshot packs[MAX_PACKS] = {};
  // Pack 1: 30kWh cap, 50% SOC
  packs[0] = make_pack(4000, 0, 5000, 100, 100, 30000, 15000);
  // Pack 2: 10kWh cap, 90% SOC
  packs[1] = make_pack(4000, 0, 9000, 100, 100, 10000, 9000);
  AggregateResult r = aggregate(packs);
  // Weighted: (5000*30000 + 9000*10000) / 40000 = (150000000 + 90000000) / 40000 = 6000
  EXPECT_EQ(r.reported_soc, 6000u);
}

TEST(ClusterAggregation, MaxChargeIsMinTimesNAlive) {
  PackSnapshot packs[MAX_PACKS] = {};
  packs[0] = make_pack(4000, 0, 5000, /*max_chg*/ 100, 100, 30000, 15000);
  packs[1] = make_pack(4000, 0, 5000, /*max_chg*/ 200, 100, 30000, 15000);
  packs[2] = make_pack(4000, 0, 5000, /*max_chg*/  20, 100, 30000, 15000);
  AggregateResult r = aggregate(packs);
  EXPECT_EQ(r.max_charge_current_dA, 60);  // min(20) × 3
}

TEST(ClusterAggregation, MaxChargeZeroIfAnyPackZero) {
  PackSnapshot packs[MAX_PACKS] = {};
  packs[0] = make_pack(4000, 0, 5000, 100, 100, 30000, 15000);
  packs[1] = make_pack(4000, 0, 5000,   0, 100, 30000, 15000);  // pack 2 says NO charge
  AggregateResult r = aggregate(packs);
  EXPECT_EQ(r.max_charge_current_dA, 0);
}

TEST(ClusterAggregation, MaxDischargeIsMinTimesNAlive) {
  PackSnapshot packs[MAX_PACKS] = {};
  packs[0] = make_pack(4000, 0, 5000, 100, /*max_dis*/ 50, 30000, 15000);
  packs[1] = make_pack(4000, 0, 5000, 100, /*max_dis*/ 70, 30000, 15000);
  AggregateResult r = aggregate(packs);
  EXPECT_EQ(r.max_discharge_current_dA, 100);  // min(50) × 2
}

TEST(ClusterAggregation, CellVoltageMaxAndMin) {
  PackSnapshot packs[MAX_PACKS] = {};
  packs[0] = make_pack(4000, 0, 5000, 100, 100, 30000, 15000);
  packs[0].cell_max_voltage_mV = 4150;
  packs[0].cell_min_voltage_mV = 3950;
  packs[1] = make_pack(4000, 0, 5000, 100, 100, 30000, 15000);
  packs[1].cell_max_voltage_mV = 4090;
  packs[1].cell_min_voltage_mV = 3870;
  AggregateResult r = aggregate(packs);
  EXPECT_EQ(r.cell_max_voltage_mV, 4150);
  EXPECT_EQ(r.cell_min_voltage_mV, 3870);
}

TEST(ClusterAggregation, TemperatureMaxAndMin) {
  PackSnapshot packs[MAX_PACKS] = {};
  packs[0] = make_pack(4000, 0, 5000, 100, 100, 30000, 15000);
  packs[0].temperature_max_dC = 350;
  packs[0].temperature_min_dC = 100;
  packs[1] = make_pack(4000, 0, 5000, 100, 100, 30000, 15000);
  packs[1].temperature_max_dC = 280;
  packs[1].temperature_min_dC = -50;
  AggregateResult r = aggregate(packs);
  EXPECT_EQ(r.temperature_max_dC, 350);
  EXPECT_EQ(r.temperature_min_dC, -50);
}

TEST(ClusterAggregation, SohIsMin) {
  PackSnapshot packs[MAX_PACKS] = {};
  packs[0] = make_pack(4000, 0, 5000, 100, 100, 30000, 15000);
  packs[0].soh_pptt = 9700;
  packs[1] = make_pack(4000, 0, 5000, 100, 100, 30000, 15000);
  packs[1].soh_pptt = 8500;
  AggregateResult r = aggregate(packs);
  EXPECT_EQ(r.soh_pptt, 8500);
}

TEST(ClusterAggregation, BmsStatusWorstOf) {
  // FAULT > UPDATING > STANDBY > IDLE > ACTIVE
  PackSnapshot packs[MAX_PACKS] = {};
  packs[0] = make_pack(4000, 0, 5000, 100, 100, 30000, 15000, ACTIVE);
  packs[1] = make_pack(4000, 0, 5000, 100, 100, 30000, 15000, FAULT);
  AggregateResult r = aggregate(packs);
  EXPECT_EQ(r.bms_status, FAULT);
}

TEST(ClusterAggregation, BmsStatusUpdatingBeatsActive) {
  PackSnapshot packs[MAX_PACKS] = {};
  packs[0] = make_pack(4000, 0, 5000, 100, 100, 30000, 15000, ACTIVE);
  packs[1] = make_pack(4000, 0, 5000, 100, 100, 30000, 15000, UPDATING);
  AggregateResult r = aggregate(packs);
  EXPECT_EQ(r.bms_status, UPDATING);
}

TEST(ClusterAggregation, OnlyAlivePacksCounted) {
  PackSnapshot packs[MAX_PACKS] = {};
  packs[0] = make_pack(4000, 100, 5000, 100, 100, 30000, 15000);
  packs[1] = make_pack(4000, 100, 5000, 100, 100, 30000, 15000);
  packs[1].alive = false;  // dead pack — has data but should not be counted
  AggregateResult r = aggregate(packs);
  EXPECT_EQ(r.n_alive, 1);
  EXPECT_EQ(r.current_dA, 100);  // only pack 0
  EXPECT_EQ(r.total_capacity_Wh, 30000u);
}

TEST(ClusterAggregation, VoltageMaxMinTrackedSeparately) {
  PackSnapshot packs[MAX_PACKS] = {};
  packs[0] = make_pack(4000, 0, 5000, 100, 100, 30000, 15000);
  packs[1] = make_pack(4050, 0, 5000, 100, 100, 30000, 15000);
  packs[2] = make_pack(3970, 0, 5000, 100, 100, 30000, 15000);
  AggregateResult r = aggregate(packs);
  EXPECT_EQ(r.voltage_max_dV, 4050);
  EXPECT_EQ(r.voltage_min_dV, 3970);
}
```

- [ ] **Step 4: Зареєструвати в CMakeLists.txt**

Edit `test/CMakeLists.txt`. Додати `cluster_aggregation_tests.cpp` в add_executable.

- [ ] **Step 5: Run tests — verify FAIL**

Run: `cmake --build test/build -j && cd test/build && ctest --output-on-failure -R ClusterAggregation`
Expected: всі ClusterAggregation тести FAIL.

- [ ] **Step 6: Commit**

```bash
git add test/cluster_aggregation_tests.cpp test/CMakeLists.txt Software/src/battery/CLUSTER-PROTOCOL.h Software/src/battery/CLUSTER-PROTOCOL.cpp
git commit -m "$(cat <<'EOF'
Add failing tests for cluster aggregation rules

EOF
)"
```

---

## Task 7: TDD aggregation rules — GREEN

**Files:**
- Modify: `Software/src/battery/CLUSTER-PROTOCOL.cpp` — replace stub `aggregate()` with real impl

- [ ] **Step 1: Реалізувати aggregate()**

Edit `Software/src/battery/CLUSTER-PROTOCOL.cpp`. Замінити stub `aggregate()` на:

```cpp
// Worst-of priority for bms_status_enum:
// FAULT > UPDATING > STANDBY > IDLE > ACTIVE > others
static int bms_status_priority(uint8_t s) {
  switch (s) {
    case FAULT:    return 5;
    case UPDATING: return 4;
    case STANDBY:  return 3;
    case IDLE:     return 2;
    case ACTIVE:   return 1;
    default:       return 0;
  }
}

AggregateResult aggregate(const PackSnapshot packs[MAX_PACKS]) {
  AggregateResult r = {};

  uint32_t voltage_sum = 0;
  uint16_t v_max = 0;
  uint16_t v_min = 0xFFFF;
  uint64_t soc_weighted = 0;     // sum(soc_i × cap_i)
  uint64_t cap_total = 0;
  uint16_t min_charge = 0xFFFF;
  uint16_t min_discharge = 0xFFFF;
  bool any_charge_zero = false;
  bool any_discharge_zero = false;
  uint16_t cell_max = 0;
  uint16_t cell_min = 0xFFFF;
  int16_t temp_max = INT16_MIN;
  int16_t temp_min = INT16_MAX;
  uint16_t soh_min = 0xFFFF;
  uint8_t worst_bms = ACTIVE;
  uint8_t balancing_or = 0;

  for (uint8_t i = 0; i < MAX_PACKS; ++i) {
    if (!packs[i].alive) continue;
    r.n_alive++;
    voltage_sum += packs[i].voltage_dV;
    if (packs[i].voltage_dV > v_max) v_max = packs[i].voltage_dV;
    if (packs[i].voltage_dV < v_min) v_min = packs[i].voltage_dV;
    r.current_dA += packs[i].current_dA;
    soc_weighted += (uint64_t)packs[i].reported_soc * (uint64_t)packs[i].total_capacity_Wh;
    cap_total += packs[i].total_capacity_Wh;
    r.total_capacity_Wh += packs[i].total_capacity_Wh;
    r.remaining_capacity_Wh += packs[i].remaining_capacity_Wh;
    if (packs[i].max_charge_current_dA == 0) any_charge_zero = true;
    if (packs[i].max_charge_current_dA < min_charge) min_charge = packs[i].max_charge_current_dA;
    if (packs[i].max_discharge_current_dA == 0) any_discharge_zero = true;
    if (packs[i].max_discharge_current_dA < min_discharge) min_discharge = packs[i].max_discharge_current_dA;
    if (packs[i].cell_max_voltage_mV > cell_max) cell_max = packs[i].cell_max_voltage_mV;
    if (packs[i].cell_min_voltage_mV < cell_min) cell_min = packs[i].cell_min_voltage_mV;
    if (packs[i].temperature_max_dC > temp_max) temp_max = packs[i].temperature_max_dC;
    if (packs[i].temperature_min_dC < temp_min) temp_min = packs[i].temperature_min_dC;
    if (packs[i].soh_pptt < soh_min) soh_min = packs[i].soh_pptt;
    if (bms_status_priority(packs[i].bms_status) > bms_status_priority(worst_bms))
      worst_bms = packs[i].bms_status;
    balancing_or |= packs[i].balancing_status;
  }

  if (r.n_alive == 0) return r;  // already-zeroed result

  r.voltage_dV = (uint16_t)(voltage_sum / r.n_alive);
  r.voltage_max_dV = v_max;
  r.voltage_min_dV = v_min;
  r.reported_soc = (uint32_t)(cap_total > 0 ? (soc_weighted / cap_total) : 0);
  r.max_charge_current_dA = any_charge_zero ? 0 : (uint16_t)(min_charge * r.n_alive);
  r.max_discharge_current_dA = any_discharge_zero ? 0 : (uint16_t)(min_discharge * r.n_alive);
  r.cell_max_voltage_mV = cell_max;
  r.cell_min_voltage_mV = cell_min;
  r.temperature_max_dC = temp_max;
  r.temperature_min_dC = temp_min;
  r.soh_pptt = soh_min;
  r.bms_status = worst_bms;
  r.balancing_status = balancing_or;
  return r;
}
```

- [ ] **Step 2: Run tests — verify all PASS**

Run: `cmake --build test/build -j && cd test/build && ctest --output-on-failure -R ClusterAggregation`
Expected: всі тести PASS.

- [ ] **Step 3: Commit**

```bash
git add Software/src/battery/CLUSTER-PROTOCOL.cpp
git commit -m "$(cat <<'EOF'
Implement cluster aggregation rules (min×N for limits, sum for current/capacity)

EOF
)"
```

---

## Task 8: ClusterNodeCanInverter (satellite) — class skeleton

**Files:**
- Create: `Software/src/inverter/CLUSTER-NODE-CAN.h`
- Create: `Software/src/inverter/CLUSTER-NODE-CAN.cpp`

- [ ] **Step 1: Створити header**

Create `Software/src/inverter/CLUSTER-NODE-CAN.h`:

```cpp
#ifndef CLUSTER_NODE_CAN_H
#define CLUSTER_NODE_CAN_H

#include "CanInverterProtocol.h"

class ClusterNodeCanInverter : public CanInverterProtocol {
 public:
  static constexpr const char* Name = "BE Cluster Node (paralleled packs)";
  const char* name() override { return Name; }

  void update_values() override;
  void transmit_can(unsigned long currentMillis) override;
  void map_can_frame_to_variable(CAN_frame rx_frame) override;

 private:
  unsigned long previousMillis_frame0 = 0;
  unsigned long previousMillis_frame1 = 0;
  unsigned long previousMillis_frame2 = 0;
  unsigned long previousMillis_frame3 = 0;
  unsigned long previousMillis_frame4 = 0;
  uint8_t seq_counter = 0;

  CAN_frame tx_frame0 = {.FD = false, .ext_ID = false, .DLC = 8, .ID = 0, .data = {0}};
  CAN_frame tx_frame1 = {.FD = false, .ext_ID = false, .DLC = 8, .ID = 0, .data = {0}};
  CAN_frame tx_frame2 = {.FD = false, .ext_ID = false, .DLC = 8, .ID = 0, .data = {0}};
  CAN_frame tx_frame3 = {.FD = false, .ext_ID = false, .DLC = 8, .ID = 0, .data = {0}};
  CAN_frame tx_frame4 = {.FD = false, .ext_ID = false, .DLC = 8, .ID = 0, .data = {0}};
};

extern uint8_t user_selected_cluster_node_pack_id;  // 1..8 (0 = unconfigured)

#endif
```

- [ ] **Step 2: Створити implementation**

Create `Software/src/inverter/CLUSTER-NODE-CAN.cpp`:

```cpp
#include "CLUSTER-NODE-CAN.h"
#include "../battery/CLUSTER-PROTOCOL.h"
#include "../datalayer/datalayer.h"

uint8_t user_selected_cluster_node_pack_id = 0;

using namespace cluster_protocol;

void ClusterNodeCanInverter::update_values() {
  // Encode current datalayer.battery state into pre-allocated CAN frames.
  // Actual transmission happens in transmit_can() at appropriate intervals.

  uint8_t pack_id = user_selected_cluster_node_pack_id;
  // CAN IDs are set every cycle in case pack_id changes via settings page.
  tx_frame0.ID = frame_id(FRAME0_BASE, pack_id);
  tx_frame1.ID = frame_id(FRAME1_BASE, pack_id);
  tx_frame2.ID = frame_id(FRAME2_BASE, pack_id);
  tx_frame3.ID = frame_id(FRAME3_BASE, pack_id);
  tx_frame4.ID = frame_id(FRAME4_BASE, pack_id);

  encode_frame0(tx_frame0.data.u8,
                datalayer.battery.status.voltage_dV,
                datalayer.battery.status.current_dA,
                datalayer.battery.status.reported_soc,
                (uint8_t)datalayer.battery.status.bms_status,
                seq_counter);
  encode_frame1(tx_frame1.data.u8,
                datalayer.battery.status.max_charge_current_dA,
                datalayer.battery.status.max_discharge_current_dA,
                datalayer.battery.status.cell_max_voltage_mV,
                datalayer.battery.status.cell_min_voltage_mV);
  encode_frame2(tx_frame2.data.u8,
                datalayer.battery.status.temperature_max_dC,
                datalayer.battery.status.temperature_min_dC,
                datalayer.battery.status.soh_pptt,
                (uint8_t)datalayer.battery.status.balancing_status,
                (uint8_t)datalayer.battery.status.real_bms_status);
  encode_frame3(tx_frame3.data.u8,
                datalayer.battery.info.total_capacity_Wh,
                datalayer.battery.status.remaining_capacity_Wh);
  encode_frame4(tx_frame4.data.u8,
                datalayer.battery.info.max_design_voltage_dV,
                datalayer.battery.info.min_design_voltage_dV,
                datalayer.battery.info.max_cell_voltage_mV,
                (uint8_t)datalayer.battery.info.chemistry,
                datalayer.battery.info.number_of_cells);
}

void ClusterNodeCanInverter::transmit_can(unsigned long currentMillis) {
  if (currentMillis - previousMillis_frame0 >= FRAME0_PERIOD_MS) {
    previousMillis_frame0 = currentMillis;
    seq_counter++;
    // Re-encode seq_counter byte before tx (cheaper than re-encoding all of frame 0)
    tx_frame0.data.u8[7] = seq_counter;
    transmit_can_frame(&tx_frame0);
  }
  if (currentMillis - previousMillis_frame1 >= FRAME1_PERIOD_MS) {
    previousMillis_frame1 = currentMillis;
    transmit_can_frame(&tx_frame1);
  }
  if (currentMillis - previousMillis_frame2 >= FRAME2_PERIOD_MS) {
    previousMillis_frame2 = currentMillis;
    transmit_can_frame(&tx_frame2);
  }
  if (currentMillis - previousMillis_frame3 >= FRAME3_PERIOD_MS) {
    previousMillis_frame3 = currentMillis;
    transmit_can_frame(&tx_frame3);
  }
  if (currentMillis - previousMillis_frame4 >= FRAME4_PERIOD_MS) {
    previousMillis_frame4 = currentMillis;
    transmit_can_frame(&tx_frame4);
  }
}

void ClusterNodeCanInverter::map_can_frame_to_variable(CAN_frame rx_frame) {
  (void)rx_frame;  // satellite ignores everything on cluster bus (master is read-only)
}
```

- [ ] **Step 3: Commit**

```bash
git add Software/src/inverter/CLUSTER-NODE-CAN.h Software/src/inverter/CLUSTER-NODE-CAN.cpp
git commit -m "$(cat <<'EOF'
Add ClusterNodeCanInverter satellite-side protocol class

EOF
)"
```

---

## Task 9: Wire ClusterNodeCanInverter into INVERTERS.{h,cpp}

**Files:**
- Modify: `Software/src/inverter/INVERTERS.h`
- Modify: `Software/src/inverter/INVERTERS.cpp`

- [ ] **Step 1: Додати include в INVERTERS.h**

Edit `Software/src/inverter/INVERTERS.h`. Знайти список `#include` (~lines 7-29) і додати:

```cpp
#include "CLUSTER-NODE-CAN.h"
```

(зберегти алфавітний порядок не обов'язково — додати в кінці перед extern declarations)

- [ ] **Step 2: Додати case в `name_for_inverter_type()`**

Edit `Software/src/inverter/INVERTERS.cpp`. Знайти `case InverterProtocolType::VCU:` (~line 109-110) і додати після нього (перед `case InverterProtocolType::Highest`):

```cpp
    case InverterProtocolType::VCU:
      return VCUInverter::Name;

    case InverterProtocolType::ClusterNodeCan:
      return ClusterNodeCanInverter::Name;

    case InverterProtocolType::Highest:
      return "None";
```

- [ ] **Step 3: Додати case в `setup_inverter()`**

Знайти аналогічну секцію в `setup_inverter()` switch (~line 118+). Додати case:

```cpp
    case InverterProtocolType::VCU:
      inverter = new VCUInverter();
      break;

    case InverterProtocolType::ClusterNodeCan:
      inverter = new ClusterNodeCanInverter();
      break;
```

- [ ] **Step 4: Build перевірка**

Run: `cmake --build test/build -j 2>&1 | tail -20`
Expected: компілюється без помилок.

- [ ] **Step 5: Commit**

```bash
git add Software/src/inverter/INVERTERS.h Software/src/inverter/INVERTERS.cpp
git commit -m "$(cat <<'EOF'
Register ClusterNodeCanInverter in inverter factory

EOF
)"
```

---

## Task 10: ClusterCanBattery (master) — class skeleton

**Files:**
- Create: `Software/src/battery/CLUSTER-CAN.h`
- Create: `Software/src/battery/CLUSTER-CAN.cpp`

- [ ] **Step 1: Створити header**

Create `Software/src/battery/CLUSTER-CAN.h`:

```cpp
#ifndef CLUSTER_CAN_H
#define CLUSTER_CAN_H

#include "CanBattery.h"
#include "CLUSTER-PROTOCOL.h"
#include "CLUSTER-HTML.h"

class ClusterCanBattery : public CanBattery {
 public:
  static constexpr const char* Name = "BE Cluster (paralleled packs)";

  ClusterCanBattery();

  void setup() override;
  void update_values() override;
  void handle_incoming_can_frame(CAN_frame rx_frame) override;
  void transmit_can(unsigned long currentMillis) override { (void)currentMillis; }

  BatteryHtmlRenderer& get_status_renderer() override { return html_renderer; }

  // Public for HTML renderer access
  const cluster_protocol::PackSnapshot& get_pack(uint8_t pack_id) const {
    return packs[pack_id_to_index(pack_id)];
  }

 private:
  cluster_protocol::PackSnapshot packs[cluster_protocol::MAX_PACKS] = {};
  bool insufficient_packs_event_active = false;
  ClusterHtmlRenderer html_renderer{*this};

  static uint8_t pack_id_to_index(uint8_t pack_id) {
    // pack_id 1..8 -> index 0..7
    return (pack_id >= 1 && pack_id <= cluster_protocol::MAX_PACKS) ? (pack_id - 1) : 0;
  }

  void update_alive_flags(uint32_t now_ms);
  void check_voltage_divergence(const cluster_protocol::AggregateResult& r);
  void check_topology_consistency();
  void apply_to_datalayer(const cluster_protocol::AggregateResult& r);
};

extern uint8_t user_selected_cluster_expected_pack_count;  // 1..8

#endif
```

- [ ] **Step 2: Створити implementation**

Create `Software/src/battery/CLUSTER-CAN.cpp`:

```cpp
#include "CLUSTER-CAN.h"
#include <Arduino.h>
#include "../datalayer/datalayer.h"
#include "../devboard/utils/events.h"

using namespace cluster_protocol;

uint8_t user_selected_cluster_expected_pack_count = 1;

ClusterCanBattery::ClusterCanBattery() : CanBattery() {}

void ClusterCanBattery::setup() {
  // datalayer fields will be filled from frame4 (static info) once first pack reports
  // Until then, keep defaults.
  datalayer.battery.status.bms_status = UPDATING;
  datalayer.battery.status.max_charge_current_dA = 0;
  datalayer.battery.status.max_discharge_current_dA = 0;
}

void ClusterCanBattery::handle_incoming_can_frame(CAN_frame rx_frame) {
  // Determine which frame and pack_id
  uint16_t id = rx_frame.ID;
  uint8_t pack_id = 0;
  void (*decoder)(const uint8_t[8], PackSnapshot&) = nullptr;

  if (is_frame_for_base(id, FRAME0_BASE)) {
    pack_id = pack_id_from_frame(id, FRAME0_BASE);
    decoder = decode_frame0;
  } else if (is_frame_for_base(id, FRAME1_BASE)) {
    pack_id = pack_id_from_frame(id, FRAME1_BASE);
    decoder = decode_frame1;
  } else if (is_frame_for_base(id, FRAME2_BASE)) {
    pack_id = pack_id_from_frame(id, FRAME2_BASE);
    decoder = decode_frame2;
  } else if (is_frame_for_base(id, FRAME3_BASE)) {
    pack_id = pack_id_from_frame(id, FRAME3_BASE);
    decoder = decode_frame3;
  } else if (is_frame_for_base(id, FRAME4_BASE)) {
    pack_id = pack_id_from_frame(id, FRAME4_BASE);
    decoder = decode_frame4;
  } else if (id == FRAME0_BASE || id == FRAME1_BASE || id == FRAME2_BASE
             || id == FRAME3_BASE || id == FRAME4_BASE) {
    // pack_id = 0 sentinel — unconfigured satellite
    set_event(EVENT_CLUSTER_UNCONFIGURED_PACK, 0);
    return;
  } else {
    return;  // Not our frame
  }

  if (pack_id < MIN_VALID_PACK_ID || pack_id > MAX_VALID_PACK_ID) return;

  uint8_t idx = pack_id - 1;
  PackSnapshot& s = packs[idx];

  // Frame 0 carries seq counter — detect duplicate ID via sequence anomaly
  if (decoder == decode_frame0 && s.seen_ever) {
    uint8_t prev_seq = s.last_seq;
    uint8_t expected_next = (uint8_t)(prev_seq + 1);
    // Allow up to 4 lost frames before flagging duplicate-ID
    uint8_t buf_seq = rx_frame.data.u8[7];
    int8_t delta = (int8_t)(buf_seq - expected_next);
    if (delta < -8) {
      set_event(EVENT_CLUSTER_DUPLICATE_PACK_ID, pack_id);
    }
  }

  decoder(rx_frame.data.u8, s);
  s.last_seen_ms = millis();
  s.seen_ever = true;
}

void ClusterCanBattery::update_alive_flags(uint32_t now_ms) {
  for (uint8_t i = 0; i < MAX_PACKS; ++i) {
    bool was_alive = packs[i].alive;
    bool now_alive = packs[i].seen_ever &&
                     (now_ms - packs[i].last_seen_ms < PACK_TIMEOUT_MS);
    packs[i].alive = now_alive;
    if (was_alive && !now_alive) {
      set_event(EVENT_CLUSTER_PACK_LOST, (uint8_t)(i + 1));
    }
  }
}

void ClusterCanBattery::check_voltage_divergence(const AggregateResult& r) {
  if (r.n_alive < 2) return;
  uint16_t diff = r.voltage_max_dV - r.voltage_min_dV;
  if (diff > VOLTAGE_DIVERGENCE_THRESHOLD_DV) {
    set_event(EVENT_CLUSTER_VOLTAGE_DIVERGENCE, (uint8_t)(diff / 10));
  }
}

void ClusterCanBattery::check_topology_consistency() {
  // chemistry and number_of_cells must match across all alive packs that have static info
  uint8_t ref_chem = 0xFF;
  uint8_t ref_cells = 0;
  for (uint8_t i = 0; i < MAX_PACKS; ++i) {
    if (!packs[i].alive) continue;
    if (packs[i].number_of_cells == 0) continue;  // frame4 not yet received
    if (ref_chem == 0xFF) {
      ref_chem = packs[i].chemistry;
      ref_cells = packs[i].number_of_cells;
    } else if (packs[i].chemistry != ref_chem || packs[i].number_of_cells != ref_cells) {
      set_event(EVENT_CLUSTER_TOPOLOGY_MISMATCH, (uint8_t)(i + 1));
      return;
    }
  }
}

void ClusterCanBattery::apply_to_datalayer(const AggregateResult& r) {
  datalayer.battery.status.voltage_dV = r.voltage_dV;
  datalayer.battery.status.current_dA = r.current_dA;
  datalayer.battery.status.reported_current_dA = r.current_dA;
  datalayer.battery.status.reported_soc = (uint16_t)r.reported_soc;
  datalayer.battery.status.real_soc = (uint16_t)r.reported_soc;
  datalayer.battery.status.max_charge_current_dA = r.max_charge_current_dA;
  datalayer.battery.status.max_discharge_current_dA = r.max_discharge_current_dA;
  // Power = current_dA × voltage_dV / 100  (dA × dV / 100 = W)
  datalayer.battery.status.max_charge_power_W =
      (uint32_t)r.max_charge_current_dA * (uint32_t)r.voltage_dV / 100u;
  datalayer.battery.status.max_discharge_power_W =
      (uint32_t)r.max_discharge_current_dA * (uint32_t)r.voltage_dV / 100u;
  datalayer.battery.status.cell_max_voltage_mV = r.cell_max_voltage_mV;
  datalayer.battery.status.cell_min_voltage_mV = r.cell_min_voltage_mV;
  datalayer.battery.status.temperature_max_dC = r.temperature_max_dC;
  datalayer.battery.status.temperature_min_dC = r.temperature_min_dC;
  datalayer.battery.status.soh_pptt = r.soh_pptt;
  datalayer.battery.status.bms_status = (bms_status_enum)r.bms_status;
  datalayer.battery.status.balancing_status =
      (balancing_status_enum)(r.balancing_status ? BALANCING_STATUS_ACTIVE : BALANCING_STATUS_INACTIVE);
  datalayer.battery.info.total_capacity_Wh = r.total_capacity_Wh;
  datalayer.battery.info.reported_total_capacity_Wh = r.total_capacity_Wh;
  datalayer.battery.status.remaining_capacity_Wh = r.remaining_capacity_Wh;
  datalayer.battery.status.reported_remaining_capacity_Wh = r.remaining_capacity_Wh;

  // Static info (from any alive pack with frame4 data)
  for (uint8_t i = 0; i < MAX_PACKS; ++i) {
    if (packs[i].alive && packs[i].number_of_cells > 0) {
      datalayer.battery.info.max_design_voltage_dV = packs[i].max_design_voltage_dV;
      datalayer.battery.info.min_design_voltage_dV = packs[i].min_design_voltage_dV;
      datalayer.battery.info.max_cell_voltage_mV = packs[i].max_cell_voltage_limit_mV;
      datalayer.battery.info.chemistry = (battery_chemistry_enum)packs[i].chemistry;
      datalayer.battery.info.number_of_cells = packs[i].number_of_cells;
      break;
    }
  }
}

void ClusterCanBattery::update_values() {
  uint32_t now = millis();
  update_alive_flags(now);

  AggregateResult r = aggregate(packs);

  // Insufficient packs check
  bool insufficient = (r.n_alive < user_selected_cluster_expected_pack_count);
  if (insufficient && !insufficient_packs_event_active) {
    set_event(EVENT_CLUSTER_INSUFFICIENT_PACKS, r.n_alive);
    insufficient_packs_event_active = true;
  } else if (!insufficient && insufficient_packs_event_active) {
    clear_event(EVENT_CLUSTER_INSUFFICIENT_PACKS);
    insufficient_packs_event_active = false;
  }

  if (insufficient) {
    // Override aggregation with safe defaults
    r.bms_status = (r.n_alive == 0) ? UPDATING : FAULT;
    r.max_charge_current_dA = 0;
    r.max_discharge_current_dA = 0;
  }

  check_voltage_divergence(r);
  check_topology_consistency();
  apply_to_datalayer(r);

  datalayer.battery.status.CAN_battery_still_alive =
      (r.n_alive > 0) ? CAN_STILL_ALIVE : 0;
}
```

- [ ] **Step 3: Build перевірка (буде fail на CLUSTER-HTML.h)**

Не комітимо ще — наступний task створить HTML renderer.

---

## Task 11: ClusterHtmlRenderer

**Files:**
- Create: `Software/src/battery/CLUSTER-HTML.h`
- Create: `Software/src/battery/CLUSTER-HTML.cpp`

- [ ] **Step 1: Створити header**

Create `Software/src/battery/CLUSTER-HTML.h`:

```cpp
#ifndef CLUSTER_HTML_H
#define CLUSTER_HTML_H

#include "../devboard/webserver/BatteryHtmlRenderer.h"

class ClusterCanBattery;

class ClusterHtmlRenderer : public BatteryHtmlRenderer {
 public:
  ClusterHtmlRenderer(const ClusterCanBattery& battery) : battery(battery) {}
  String get_status_html() override;

 private:
  const ClusterCanBattery& battery;
};

#endif
```

- [ ] **Step 2: Створити implementation**

Create `Software/src/battery/CLUSTER-HTML.cpp`:

```cpp
#include "CLUSTER-HTML.h"
#include <Arduino.h>
#include "CLUSTER-CAN.h"
#include "CLUSTER-PROTOCOL.h"

using namespace cluster_protocol;

String ClusterHtmlRenderer::get_status_html() {
  String html;
  html.reserve(2048);
  html += "<h4>Cluster pack status</h4>";
  html += "<table style='border-collapse:collapse'>";
  html += "<tr><th>Pack #</th><th>Status</th><th>V (V)</th><th>I (A)</th>"
          "<th>SOC %</th><th>T max (°C)</th><th>Last seen (ms ago)</th></tr>";

  uint32_t now = millis();
  for (uint8_t pack_id = MIN_VALID_PACK_ID; pack_id <= MAX_VALID_PACK_ID; ++pack_id) {
    const PackSnapshot& s = battery.get_pack(pack_id);
    if (!s.seen_ever) continue;
    uint32_t age = now - s.last_seen_ms;
    html += "<tr><td>";
    html += pack_id;
    html += "</td><td>";
    html += s.alive ? "ALIVE" : "LOST";
    html += "</td><td>";
    html += s.voltage_dV / 10.0f;
    html += "</td><td>";
    html += s.current_dA / 10.0f;
    html += "</td><td>";
    html += s.reported_soc / 100.0f;
    html += "</td><td>";
    html += s.temperature_max_dC / 10.0f;
    html += "</td><td>";
    html += age;
    html += "</td></tr>";
  }
  html += "</table>";
  return html;
}
```

- [ ] **Step 3: Build перевірка**

Run: `cmake --build test/build -j 2>&1 | tail -20`
Expected: компілюється; запам'ятай що CLUSTER-CAN.cpp та CLUSTER-HTML.cpp ще не в CMakeLists.txt — додамо в Task 14.

- [ ] **Step 4: Commit (Task 10 + 11 разом)**

```bash
git add Software/src/battery/CLUSTER-CAN.h Software/src/battery/CLUSTER-CAN.cpp \
        Software/src/battery/CLUSTER-HTML.h Software/src/battery/CLUSTER-HTML.cpp
git commit -m "$(cat <<'EOF'
Add ClusterCanBattery master-side aggregator and HTML renderer

EOF
)"
```

---

## Task 12: Wire ClusterCanBattery into BATTERIES.{h,cpp}

**Files:**
- Modify: `Software/src/battery/BATTERIES.h`
- Modify: `Software/src/battery/BATTERIES.cpp`

- [ ] **Step 1: Додати include в BATTERIES.h**

Edit `Software/src/battery/BATTERIES.h`. Знайти список includes (для всіх battery .h файлів) і додати:

```cpp
#include "CLUSTER-CAN.h"
```

- [ ] **Step 2: Додати case в `name_for_battery_type()`**

Edit `Software/src/battery/BATTERIES.cpp:43-152`. Перед `default:` додати:

```cpp
    case BatteryType::ClusterCan:
      return ClusterCanBattery::Name;
    default:
      return nullptr;
```

- [ ] **Step 3: Додати case в `create_battery()`**

Edit `Software/src/battery/BATTERIES.cpp:162-271`. Перед `default:` додати:

```cpp
    case BatteryType::ClusterCan:
      return new ClusterCanBattery();
    default:
      return nullptr;
```

- [ ] **Step 4: Build**

Run: `cmake --build test/build -j 2>&1 | tail -20`
Expected: ok.

- [ ] **Step 5: Commit**

```bash
git add Software/src/battery/BATTERIES.h Software/src/battery/BATTERIES.cpp
git commit -m "$(cat <<'EOF'
Register ClusterCanBattery in battery factory

EOF
)"
```

---

## Task 13: NVM persistence for cluster settings

**Files:**
- Modify: `Software/src/communication/nvm/comm_nvm.cpp`

- [ ] **Step 1: Додати load/save для `CLSTPACKID` і `CLSTPACKCNT`**

Edit `Software/src/communication/nvm/comm_nvm.cpp`. Знайти секцію де load'ять інші user-settings (~lines 79-90):

```cpp
  user_selected_battery_type = (BatteryType)settings.getUInt("BATTTYPE", (int)BatteryType::None);
  ...
  user_selected_inverter_protocol = (InverterProtocolType)settings.getUInt("INV", ...);
```

Поряд додати:

```cpp
  user_selected_cluster_node_pack_id = (uint8_t)settings.getUInt("CLSTPACKID", 0);
  user_selected_cluster_expected_pack_count = (uint8_t)settings.getUInt("CLSTPACKCNT", 1);
```

- [ ] **Step 2: Додати extern declarations у відповідних файлах**

Edit `Software/src/communication/nvm/comm_nvm.cpp` додай includes:

```cpp
#include "../../battery/CLUSTER-CAN.h"
#include "../../inverter/CLUSTER-NODE-CAN.h"
```

- [ ] **Step 3: Додати save-side**

В тому ж файлі знайти save-функцію (де `settings.putUInt(...)`) і додати:

```cpp
  settings.putUInt("CLSTPACKID", user_selected_cluster_node_pack_id);
  settings.putUInt("CLSTPACKCNT", user_selected_cluster_expected_pack_count);
```

- [ ] **Step 4: Build**

Run: `cmake --build test/build -j 2>&1 | tail -20`
Expected: ok.

- [ ] **Step 5: Commit**

```bash
git add Software/src/communication/nvm/comm_nvm.cpp
git commit -m "$(cat <<'EOF'
Persist cluster pack_id and expected pack count to NVM

EOF
)"
```

---

## Task 14: Update CMakeLists.txt for new sources

**Files:**
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Додати нові battery/inverter sources**

Edit `test/CMakeLists.txt:69-187`. В `add_executable(tests ...)` додати:

```cmake
    ../Software/src/battery/CLUSTER-CAN.cpp
    ../Software/src/battery/CLUSTER-HTML.cpp
    ../Software/src/battery/CLUSTER-PROTOCOL.cpp
    ../Software/src/inverter/CLUSTER-NODE-CAN.cpp
```

(вони мають бути серед інших battery/inverter source files)

- [ ] **Step 2: Build full test suite**

Run: `cmake --build test/build -j 2>&1 | tail -20`
Expected: ok.

- [ ] **Step 3: Run all tests**

Run: `cd test/build && ctest --output-on-failure`
Expected: всі тести (старі + нові ClusterProtocol + ClusterAggregation) PASS.

- [ ] **Step 4: Commit**

```bash
git add test/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Wire cluster sources into test build

EOF
)"
```

---

## Task 15: TDD master-side integration — pack lifecycle

**Files:**
- Create: `test/cluster_master_tests.cpp`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Створити integration tests**

Create `test/cluster_master_tests.cpp`:

```cpp
#include <gtest/gtest.h>
#include "../Software/src/battery/CLUSTER-CAN.h"
#include "../Software/src/battery/CLUSTER-PROTOCOL.h"
#include "../Software/src/datalayer/datalayer.h"
#include "../Software/src/devboard/utils/events.h"

extern uint64_t current_time;  // from test/emul/time.cpp; controls millis()

using namespace cluster_protocol;

class ClusterMasterTest : public ::testing::Test {
 protected:
  ClusterCanBattery* master;

  void SetUp() override {
    init_events();
    master = new ClusterCanBattery();
    master->setup();
    user_selected_cluster_expected_pack_count = 2;
    current_time = 1000;
  }
  void TearDown() override {
    delete master;
  }

  // Helper: feed a frame 0 from given pack
  void send_frame0(uint8_t pack_id, uint16_t v_dV, int16_t i_dA,
                   uint16_t soc_pptt, uint8_t bms_status, uint8_t seq) {
    CAN_frame f = {.FD = false, .ext_ID = false, .DLC = 8,
                   .ID = frame_id(FRAME0_BASE, pack_id), .data = {0}};
    encode_frame0(f.data.u8, v_dV, i_dA, soc_pptt, bms_status, seq);
    master->handle_incoming_can_frame(f);
  }
  void send_frame1(uint8_t pack_id, uint16_t maxc, uint16_t maxd, uint16_t cmax, uint16_t cmin) {
    CAN_frame f = {.FD = false, .ext_ID = false, .DLC = 8,
                   .ID = frame_id(FRAME1_BASE, pack_id), .data = {0}};
    encode_frame1(f.data.u8, maxc, maxd, cmax, cmin);
    master->handle_incoming_can_frame(f);
  }
  void send_frame3(uint8_t pack_id, uint32_t total_Wh, uint32_t rem_Wh) {
    CAN_frame f = {.FD = false, .ext_ID = false, .DLC = 8,
                   .ID = frame_id(FRAME3_BASE, pack_id), .data = {0}};
    encode_frame3(f.data.u8, total_Wh, rem_Wh);
    master->handle_incoming_can_frame(f);
  }
};

TEST_F(ClusterMasterTest, NoPacksMeansUpdatingStatus) {
  master->update_values();
  EXPECT_EQ(datalayer.battery.status.bms_status, UPDATING);
  EXPECT_EQ(datalayer.battery.status.max_charge_current_dA, 0);
  EXPECT_EQ(datalayer.battery.status.max_discharge_current_dA, 0);
}

TEST_F(ClusterMasterTest, TwoPacksAggregateCurrent) {
  send_frame0(/*pack*/ 1, 4000, 50, 5000, ACTIVE, 1);
  send_frame3(1, 30000, 15000);
  send_frame0(/*pack*/ 2, 4010, 70, 5500, ACTIVE, 1);
  send_frame3(2, 30000, 18000);
  master->update_values();
  EXPECT_EQ(datalayer.battery.status.current_dA, 120);  // 50+70
  EXPECT_EQ(datalayer.battery.info.total_capacity_Wh, 60000u);
}

TEST_F(ClusterMasterTest, PackTimeoutMarksLost) {
  send_frame0(1, 4000, 50, 5000, ACTIVE, 1);
  send_frame0(2, 4010, 70, 5500, ACTIVE, 1);
  master->update_values();
  EXPECT_EQ(datalayer.battery.status.current_dA, 120);

  // Advance time past PACK_TIMEOUT_MS (1000) for pack 2
  send_frame0(1, 4000, 50, 5000, ACTIVE, 2);  // pack 1 sends again
  current_time += 2000;  // 2 seconds pass
  master->update_values();

  // Pack 2 should be lost; pack 1 also lost since we didn't refresh after time advance
  // Actually pack 1 was last seen at t=1000 (first send); after +2000=3000, > 1000 timeout. Both lost.
  EXPECT_EQ(datalayer.battery.status.bms_status, UPDATING);  // n_alive=0 → UPDATING
}

TEST_F(ClusterMasterTest, InsufficientPacksTriggersFault) {
  user_selected_cluster_expected_pack_count = 3;
  send_frame0(1, 4000, 50, 5000, ACTIVE, 1);
  send_frame0(2, 4010, 70, 5500, ACTIVE, 1);
  master->update_values();
  EXPECT_EQ(datalayer.battery.status.bms_status, FAULT);
  EXPECT_EQ(datalayer.battery.status.max_charge_current_dA, 0);
  EXPECT_EQ(datalayer.battery.status.max_discharge_current_dA, 0);
}

TEST_F(ClusterMasterTest, VoltageDivergenceSetsEvent) {
  // expected = 2 packs
  send_frame0(1, 4000, 0, 5000, ACTIVE, 1);
  send_frame3(1, 30000, 15000);
  send_frame0(2, 4080, 0, 5000, ACTIVE, 1);  // 8V higher → divergence > 5V
  send_frame3(2, 30000, 15000);
  master->update_values();
  // Event should be active (we don't check level here, just that it's set)
  EXPECT_EQ(events.entries[EVENT_CLUSTER_VOLTAGE_DIVERGENCE].state, EVENT_STATE_ACTIVE);
}

TEST_F(ClusterMasterTest, UnconfiguredPackTriggersEvent) {
  // pack_id = 0 sentinel
  CAN_frame f = {.FD = false, .ext_ID = false, .DLC = 8, .ID = FRAME0_BASE, .data = {0}};
  master->handle_incoming_can_frame(f);
  EXPECT_EQ(events.entries[EVENT_CLUSTER_UNCONFIGURED_PACK].state, EVENT_STATE_ACTIVE);
}

TEST_F(ClusterMasterTest, ChargeLimitMinTimesNAlive) {
  user_selected_cluster_expected_pack_count = 2;
  send_frame0(1, 4000, 0, 5000, ACTIVE, 1);
  send_frame1(1, /*max_chg*/ 100, /*max_dis*/ 200, 4100, 3900);
  send_frame3(1, 30000, 15000);
  send_frame0(2, 4000, 0, 5000, ACTIVE, 1);
  send_frame1(2, /*max_chg*/ 50, /*max_dis*/ 200, 4100, 3900);  // pack 2 derated
  send_frame3(2, 30000, 15000);
  master->update_values();
  EXPECT_EQ(datalayer.battery.status.max_charge_current_dA, 100);  // min(50)*2
  EXPECT_EQ(datalayer.battery.status.max_discharge_current_dA, 400);  // min(200)*2
}
```

- [ ] **Step 2: Зареєструвати в CMakeLists.txt**

Edit `test/CMakeLists.txt`. Додати `cluster_master_tests.cpp` в `add_executable(tests ...)`.

- [ ] **Step 3: Перевірити що `current_time` доступний**

Глобальний `uint64_t current_time` існує в `test/emul/time.cpp` і `millis()` повертає його. Тести керують часом через присвоєння (наприклад `current_time = 1000` або `current_time += 2000`). Нічого додавати не треба.

- [ ] **Step 4: Run tests**

Run: `cmake --build test/build -j && cd test/build && ctest --output-on-failure -R ClusterMaster`
Expected: всі ClusterMaster тести PASS.

- [ ] **Step 5: Commit**

```bash
git add test/cluster_master_tests.cpp test/CMakeLists.txt test/emul/time.cpp
git commit -m "$(cat <<'EOF'
Add cluster master integration tests for pack lifecycle and aggregation

EOF
)"
```

---

## Task 16: Web UI — settings page

**Files:**
- Modify: `Software/src/devboard/webserver/settings_html.cpp`

- [ ] **Step 1: Знайти структуру UI rendering**

Run: `grep -n "InverterProtocolType\|user_selected_inverter_protocol\|BATTTYPE\|user_selected_battery_type" /Users/vitalijcasovskij/IdeaProjects/Battery-Emulator-origin/Software/src/devboard/webserver/settings_html.cpp`

Знайти секцію де dropdown для inverter та battery. Ймовірно патерн повторювані `<option value=...>` блоки.

- [ ] **Step 2: Додати pack_id input для satellite (видимий коли inverter = ClusterNodeCan)**

Edit `settings_html.cpp`. В формі settings, після dropdown для інвертора, додати JavaScript-керований input:

```html
<div id="cluster_node_pack_id_div" style="display:none">
  <label>Cluster Pack ID (1-8):
    <input type="number" name="CLSTPACKID" min="1" max="8"
           value="<%PACK_ID%>" />
  </label>
</div>
```

(Замінити `<%PACK_ID%>` на runtime-substitution як це робиться для інших полів — слідуй patterns у settings_html.cpp.)

JS to show/hide based on inverter dropdown:

```javascript
document.querySelector('select[name="INV"]').addEventListener('change', function() {
  document.getElementById('cluster_node_pack_id_div').style.display =
    (this.value == '25') ? 'block' : 'none';  // 25 = ClusterNodeCan
});
```

- [ ] **Step 3: Додати expected_pack_count input для master**

Аналогічно для battery dropdown:

```html
<div id="cluster_expected_count_div" style="display:none">
  <label>Cluster expected pack count (1-8):
    <input type="number" name="CLSTPACKCNT" min="1" max="8"
           value="<%PACK_COUNT%>" />
  </label>
</div>
```

JS:
```javascript
document.querySelector('select[name="BATTTYPE"]').addEventListener('change', function() {
  document.getElementById('cluster_expected_count_div').style.display =
    (this.value == '53') ? 'block' : 'none';  // 53 = ClusterCan
});
```

- [ ] **Step 4: Додати handler у POST endpoint**

Знайти де обробляються POST settings (можливо `webserver.cpp` чи `comm_nvm.cpp`). Додати:

```cpp
if (server->hasArg("CLSTPACKID")) {
  user_selected_cluster_node_pack_id = (uint8_t)server->arg("CLSTPACKID").toInt();
}
if (server->hasArg("CLSTPACKCNT")) {
  user_selected_cluster_expected_pack_count = (uint8_t)server->arg("CLSTPACKCNT").toInt();
}
```

- [ ] **Step 5: Build перевірка (на хост-тестах)**

Note: webserver code зазвичай НЕ збирається у тест-CMake (потребує WiFiServer). Перевірка тут — тільки PlatformIO build. Запустити PIO build для DEV-плати:

```bash
cd /Users/vitalijcasovskij/IdeaProjects/Battery-Emulator-origin
pio run -e esp32devkit_330 -j 2>&1 | tail -30
```

Expected: build succeeds.

- [ ] **Step 6: Commit**

```bash
git add Software/src/devboard/webserver/settings_html.cpp \
        Software/src/devboard/webserver/webserver.cpp
git commit -m "$(cat <<'EOF'
Add web UI inputs for cluster pack ID and expected pack count

EOF
)"
```

---

## Task 17: Manual smoke test — satellite

**Files:** N/A (manual procedure)

**Setup:**
- 1× ESP32 dev kit з 2 CAN портами (наприклад devkit board)
- Існуюча batt-конфігурація (наприклад TestFakeBattery або реальний BMW-I3)
- CAN-сніффер (Innomaker / Korlan / SavvyCAN logger) на cluster bus

- [ ] **Step 1: Прошити satellite з налаштуваннями**

Відкрити web UI → settings:
- Battery type: TestFakeBattery (для початку без реального BMS)
- Inverter: BE Cluster Node
- Pack ID: 3
- Зберегти.

- [ ] **Step 2: Перевірити CAN-фрейми на cluster bus**

CAN-сніффер на 500 kbps має побачити:
- `0x503` кожні 100 ms (frame 0)
- `0x513` кожні 200 ms (frame 1)
- `0x523` кожні 500 ms (frame 2)
- `0x533` кожні 1000 ms (frame 3)
- `0x543` кожні 5000 ms (frame 4)

- [ ] **Step 3: Декодувати один frame 0 вручну**

Очікувано: 8 байт. Перші 2 байти LE = voltage_dV TestFake. Наприклад якщо тест-batt каже 370V → voltage_dV=3700=0x0E74 → байти `74 0E`.

- [ ] **Step 4: Перевірити що pack_id=0 викликає alarm**

Тимчасово встановити Pack ID = 0 в UI. Очікується:
- Frame'и шлються по `0x500/0x510/...`
- Якщо master є на шині — він setEvent EVENT_CLUSTER_UNCONFIGURED_PACK

- [ ] **Step 5: Звіт**

Записати в issues / журнал розробки: фрейми спостерігаються? CAN ID збігаються? Є аномалії timing?

---

## Task 18: Manual smoke test — master + 2 satellites

**Setup:**
- 1× master ESP32 з 2 CAN
- 2× satellite ESP32 з 2 CAN кожен
- Cluster bus: 500 kbps, всі на одній шині
- Інверторний bus: підключений до тестового інвертора (наприклад Pylon-сумісний симулятор)

- [ ] **Step 1: Налаштувати master**

Web UI:
- Battery: BE Cluster
- Expected pack count: 2
- Inverter: PylonCan (або BYD-CAN)

- [ ] **Step 2: Налаштувати satellite #1**

- Battery: TestFakeBattery
- Inverter: BE Cluster Node
- Pack ID: 1

- [ ] **Step 3: Налаштувати satellite #2**

- Battery: TestFakeBattery
- Inverter: BE Cluster Node
- Pack ID: 2

- [ ] **Step 4: Запустити всі три, перевірити master web UI**

Master advanced battery view має показати таблицю з 2 alive packs. SOC, V, I — sensible values.

- [ ] **Step 5: Симулювати втрату satellite**

Вимкнути satellite #2. Через ~1.5s master має:
- В таблиці: pack 2 = LOST
- bms_status = FAULT (бо n_alive < expected)
- max_charge/discharge = 0
- Active event: EVENT_CLUSTER_PACK_LOST

- [ ] **Step 6: Відновити satellite #2 і перевірити recovery**

Увімкнути satellite #2 знов. Через ~1s master має:
- pack 2 = ALIVE
- bms_status повертається до ACTIVE
- Event EVENT_CLUSTER_INSUFFICIENT_PACKS clear'ється

- [ ] **Step 7: Звіт**

Зафіксувати: чи відразу повертається? Чи є flapping? Час до recovery?

---

## Self-review checklist (run after writing all tasks)

- [ ] Кожна секція spec'а покрита щонайменше одним task'ом:
  - Cluster CAN protocol → Tasks 3, 4, 5
  - Aggregation rules → Tasks 6, 7, 15
  - Pack health & timeouts → Task 15 (pack lifecycle, insufficient packs, voltage divergence, unconfigured pack)
  - Topology consistency → Task 10 (`check_topology_consistency`)
  - Configuration / NVM → Task 13
  - Web UI → Task 16
  - Hardware → не код-task; покрито в Task 17/18 manual smoke
  - Backwards compatibility → автоматично (нові enum значення, нічого існуючого не змінено)
  - Events → Task 1
- [ ] Жодних "TODO" / "TBD" / "implement later" в плані
- [ ] Типи й сигнатури консистентні: `PackSnapshot`, `AggregateResult`, `aggregate()` — однакові всюди
- [ ] Кожен step має точний код або точну команду
- [ ] Frequent commits: кожен task закінчується commit'ом
- [ ] TDD: aggregation і protocol — RED→GREEN cycle (Tasks 4-7); master-side integration (Task 15)

---

## Out of scope (не імплементуємо в цьому плані)

- Trust-mode setting (SUM vs min×N) — додамо коли буде попит
- Per-cell voltage aggregation — поки не потрібно інвертору
- CAN-FD варіант — після появи потреби
- Master fallback на satellite-direct — окремий план
- >8 пакетів (5-bit pack_id) — окремий план

Усе це задокументовано в spec'у в секції "Відкриті питання / майбутні розширення".
