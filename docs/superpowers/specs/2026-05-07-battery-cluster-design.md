# Battery Cluster — масштабування паралельних пакетів через master/satellite

**Status:** Design draft (v1 + v2 + v3 implemented; v3 = protocol versioning, see end)
**Date:** 2026-05-07
**Branch:** feature/master-slave-pair

## Мета

Дати змогу підключати **до 8 батарейних пакетів паралельно** до одного інвертора, зберігаючи коректну агрегацію телеметрії та limits, і **повторно використовуючи** існуючі battery- та inverter-драйвери Battery-Emulator.

Поточна архітектура BE підтримує до 3 пакетів (`battery`, `battery2`, `battery3`) обмежено — drivers per battery type явно перелічені в `BATTERIES.cpp::setup_battery()`, parallel safety обмежена voltage matching, а агрегація limits фактично відсутня. Масштабувати це далі шляхом додавання `battery4..8` неекономно.

## Підхід: distributed cluster (master + N satellites)

Кожен пакет має власний ESP32 з Battery-Emulator (satellite), який читає свій BMS існуючим драйвером і **публікує телеметрію на cluster CAN-шину** через новий "інвертор-протокол" `BE Cluster Node`. Окремий ESP32 (master) підключається до cluster-шини як **"батарея"** (новий battery-driver `BE Cluster`), агрегує дані всіх пакетів у `datalayer.battery` і використовує **будь-який існуючий inverter-driver** для розмови з реальним інвертором.

```
                  ┌─────────────────────┐
                  │   Real inverter     │
                  │ (BYD/SOLAX/Pylon/…) │
                  └──────────▲──────────┘
                             │ existing inverter CAN/RS485
                  ┌──────────┴──────────┐
                  │      MASTER         │
                  │  ESP32 (3LB-class)  │
                  │  CAN.inverter ──────┘
                  │  CAN.battery ──┐
                  └────────────────│────┘
                                   │ cluster CAN bus
            ┌────────┬─────────────┴───┬────────┐
        ┌───┴──┐ ┌───┴──┐         ┌────┴─┐ ┌────┴─┐
        │ Sat1 │ │ Sat2 │   ...   │ Sat7 │ │ Sat8 │
        │ ESP32│ │ ESP32│         │ ESP32│ │ ESP32│
        └───┬──┘ └───┬──┘         └──┬───┘ └──┬───┘
            │ BMS    │ BMS           │ BMS    │ BMS
         pack 1   pack 2          pack 7   pack 8
            ║        ║               ║        ║
            ╚════════╩═══════════════╩════════╝
                    Паралельне DC-зєднання
                  (контактори керуються зовні)
```

### Ролі

| Компонент | Що робить | Хто реалізує |
|-----------|-----------|--------------|
| **Satellite** | Звичайний BE: читає локальний BMS, transmit'ить телеметрію на cluster-шину | існуючий battery-driver + новий `ClusterNodeCanInverter` |
| **Master** | Слухає cluster-шину, агрегує до 8 пакетів, заповнює `datalayer.battery`, шле на інвертор | новий `ClusterCanBattery` + існуючий inverter-driver |

### Що поза скоупом

- **Контактори і pre-charge** — керуються **зовнішньою апаратурою** (механічні реле з voltage-matching, або зовнішня BMS-плата на рівні пакета). Battery-Emulator на satellite не керує контакторами; master не керує також. Cluster-протокол **однонапрямний** (satellite → master), control-back-channel не потрібен.
- **Heterogeneous chemistry** — всі пакети мають бути однієї хімії та близького voltage range. Це user responsibility; BE raise'ить voltage-divergence alarm, але не enforce'ить.
- **Per-cell voltages** через cluster — не передаються (вибух bandwidth, не потрібно інвертору). Лишаються видимими на webserver кожного satellite окремо.

## Cluster CAN протокол

**Параметри:** 500 kbps, 11-bit standard ID, однонапрямний (satellite → master).

**Адресна схема:** `CAN ID = base + pack_id`, `pack_id ∈ 1..8`. Чотири фрейми на пакет з різною частотою:

| Frame | Базовий ID | Період   | Призначення                       |
|-------|------------|----------|-----------------------------------|
| 0     | `0x500`    | 100 ms   | Live status (V/I/SOC/BMS)        |
| 1     | `0x510`    | 200 ms   | Limits + cell V min/max          |
| 2     | `0x520`    | 500 ms   | Temperature, SOH, balancing      |
| 3     | `0x530`    | 1000 ms  | Capacity (total/remaining Wh)    |
| 4     | `0x540`    | 5000 ms  | Static info (design V, chem, N cells) |

Тобто пакет №3 шле по: `0x503`, `0x513`, `0x523`, `0x533`, `0x543`.

**Pack ID = 0** зарезервовано як "unconfigured" sentinel — якщо master побачить `0x500` → raise `EVENT_CLUSTER_UNCONFIGURED_PACK`.

### Layout фреймів (всі поля Little-Endian)

```
Frame 0 (0x50X) — Live status, 100 ms
[0-1] voltage_dV         (uint16)   — pack voltage
[2-3] current_dA         (int16)    — pack current (+ = charging)
[4-5] reported_soc       (uint16)   — 0..10000 (00.00–100.00%)
[6]   bms_status         (uint8)    — bms_status_enum
[7]   seq_counter        (uint8)    — інкремент per frame, для детекції втрат

Frame 1 (0x51X) — Limits + cells, 200 ms
[0-1] max_charge_current_dA    (uint16)
[2-3] max_discharge_current_dA (uint16)
[4-5] cell_max_voltage_mV      (uint16)
[6-7] cell_min_voltage_mV      (uint16)

Frame 2 (0x52X) — Temp + state, 500 ms
[0-1] temperature_max_dC  (int16)
[2-3] temperature_min_dC  (int16)
[4-5] soh_pptt            (uint16)   — 0..10000
[6]   balancing_status    (uint8)
[7]   real_bms_status     (uint8)

Frame 3 (0x53X) — Capacity, 1000 ms
[0-3] total_capacity_Wh      (uint32)
[4-7] remaining_capacity_Wh  (uint32)

Frame 4 (0x54X) — Static info, 5000 ms
[0-1] max_design_voltage_dV       (uint16)   — pack design upper voltage
[2-3] min_design_voltage_dV       (uint16)   — pack design lower voltage
[4-5] max_cell_voltage_limit_mV   (uint16)   — cell upper limit
[6]   chemistry                    (uint8)    — battery_chemistry_enum
[7]   number_of_cells              (uint8)    — 1..255
```

**Frame 4 (static info)** транслює constant-під-час-роботи параметри пакета. На master:
- При `seen_ever == false`: master чекає frame 4 від пакета щоб дізнатись topology
- Frame 4 перевіряється на consistency між пакетами (chemistry і number_of_cells мають збігатися); divergence → `EVENT_CLUSTER_TOPOLOGY_MISMATCH`
- `min_cell_voltage_limit_mV` не передається; виводиться з chemistry (LFP=2500, NCA=2700, ..., як в існуючих battery-драйверах). Якщо колись треба буде явно — додамо frame 5.

**Bandwidth:** 8 пакетів × ~17 frames/sec середнє ≈ 18 kbps з 500 kbps. Колосальний запас.

**Прикладна перевірка/CRC:** не потрібно — CAN має CRC на рівні протоколу. Sequence-counter використовується тільки для детекції втрат і duplicate-ID.

## Правила агрегації на майстері

Master тримає масив `PackSnapshot[8]`:

```cpp
struct PackSnapshot {
  uint32_t last_seen_ms;      // millis() останнього frame 0
  bool alive;                 // (now - last_seen_ms) < timeout_ms
  bool seen_ever;
  uint8_t last_seq;
  // всі поля з фреймів 0..3
  uint16_t voltage_dV;
  int16_t  current_dA;
  uint16_t reported_soc;
  uint8_t  bms_status;
  uint16_t max_charge_current_dA;
  uint16_t max_discharge_current_dA;
  uint16_t cell_max_voltage_mV;
  uint16_t cell_min_voltage_mV;
  int16_t  temperature_max_dC;
  int16_t  temperature_min_dC;
  uint16_t soh_pptt;
  uint8_t  balancing_status;
  uint8_t  real_bms_status;
  uint32_t total_capacity_Wh;
  uint32_t remaining_capacity_Wh;
  // Frame 4 (static info)
  uint16_t max_design_voltage_dV;
  uint16_t min_design_voltage_dV;
  uint16_t max_cell_voltage_limit_mV;
  uint8_t  chemistry;
  uint8_t  number_of_cells;
};
```

На кожен виклик `update_values()`:

| Поле в `datalayer.battery` | Формула |
|----------------------------|---------|
| `voltage_dV` | mean(alive packs) |
| `current_dA`, `reported_current_dA` | **SUM**(alive packs) — паралельні струми додаються |
| `reported_soc`, `real_soc` | capacity-weighted mean: `Σ(soc_i × cap_i) / Σ(cap_i)` |
| `total_capacity_Wh`, `reported_total_capacity_Wh` | SUM |
| `remaining_capacity_Wh`, `reported_remaining_capacity_Wh` | SUM |
| `max_charge_current_dA` | **`min(pack[i]) × N_alive`**; якщо `min == 0` → 0 |
| `max_discharge_current_dA` | **`min(pack[i]) × N_alive`**; якщо `min == 0` → 0 |
| `max_charge_power_W`, `max_discharge_power_W` | derived: `current × voltage / 100` |
| `cell_max_voltage_mV` | MAX across alive packs |
| `cell_min_voltage_mV` | MIN across alive packs |
| `temperature_max_dC` | MAX |
| `temperature_min_dC` | MIN |
| `soh_pptt` | MIN (здоров'я системи = здоров'я найслабшого) |
| `bms_status` | worst-of: `FAULT > UPDATING > STANDBY > IDLE > ACTIVE` |
| `balancing_status` | OR-агрегація |
| `chemistry`, `max_design_voltage_dV`, `min_design_voltage_dV`, `number_of_cells`, `max_cell_voltage_mV` (limit) | з Frame 4 (static info), консолідовано з усіх пакетів; consistency перевіряється |
| `min_cell_voltage_mV` (limit) | derived з `chemistry` (default per chemistry) |

### Чому `min × N` для лімітів, а не SUM

Розглянемо 5 пакетів: 4 "100A OK", 1 з-за температури "20A OK".

- **SUM** → 420A на cluster. Інвертор подає 420A → ділиться приблизно рівно (~84A на пакет). Слабкий пакет отримує 84A замість 20A → **перевантаження**.
- **min × N** → 100A на cluster. Ділиться по ~20A на пакет. Безпечно, втрачаємо ~76% піку, але всі пакети в межах.

В DIY-ESS пакети можуть мати різний стан і вік, тому SUM небезпечний без активного балансування струмів. `min × N` — стандартний conservative підхід комерційних паралельних BMS (Pylontech, BYD).

В майбутньому можна додати user-setting `cluster_trust_mode` (`safe` / `aggressive`) для переключення на SUM, але v1 — `min × N`.

### Pack health & timeouts

- Pack `alive` якщо `now - last_seen_ms < pack_timeout_ms` (default 1000 ms)
- Pack timeout (був alive і пропав) → `EVENT_CLUSTER_PACK_LOST(pack_id)` + виключаємо з агрегації
- `N_alive < expected_pack_count` → `bms_status = FAULT`, ліміти = 0, `EVENT_CLUSTER_INSUFFICIENT_PACKS`
- Sequence-counter стрибає назад або duplicate → `EVENT_CLUSTER_DUPLICATE_PACK_ID(pack_id)`
- `(max - min) voltage_dV > 50dV` (5V) серед alive packs → `EVENT_CLUSTER_VOLTAGE_DIVERGENCE` (warning, не блокуючий — контактори зовні)
- На startup: до отримання `expected_pack_count` пакетів — `bms_status = UPDATING`, ліміти = 0
- `pack_id = 0` на шині → `EVENT_CLUSTER_UNCONFIGURED_PACK`
- `N_alive = 0` → cluster у FAULT

## Конфігурація користувача

### Satellite-вузол

| Параметр | Тип | Default | Видимий |
|----------|-----|---------|---------|
| Battery type | enum (existing) | None | завжди |
| Inverter protocol | `InverterProtocolType::ClusterNodeCan` | — | завжди |
| `cluster_node_pack_id` | `uint8_t` (1..8) | 0 (= unconfigured, alarm) | коли inverter = `ClusterNodeCan` |

### Master-вузол

| Параметр | Тип | Default | Видимий |
|----------|-----|---------|---------|
| Battery type | `BatteryType::ClusterCan` | — | завжди |
| `cluster_expected_pack_count` | `uint8_t` (1..8) | 1 | коли battery = `ClusterCan` |
| Inverter protocol | будь-який існуючий | — | завжди |

### NVM (`comm_nvm.cpp`)

Дві нові preference-keys в існуючу `Preferences` namespace:

```
"clst_pack_id"     // uint8 — на satellite (cluster_node_pack_id)
"clst_pack_cnt"    // uint8 — на master (cluster_expected_pack_count)
```

## Web UI

**Satellite settings page:**
- Існуюча сторінка налаштувань — додати "BE Cluster Node" в Inverter dropdown
- Коли вибрано — input "Pack ID" (1..8)

**Master settings page:**
- Додати "BE Cluster (paralleled packs)" в Battery dropdown
- Коли вибрано — input "Expected pack count" (1..8)

**Master advanced view (`CLUSTER-HTML.h/.cpp`)** — новий `BatteryHtmlRenderer`:

```
┌────────┬─────────┬─────────┬─────────┬──────┬──────┬──────────┬─────────┐
│ Pack # │ Status  │ V (V)   │ I (A)   │ SOC  │ Temp │ Last seen│ Faults  │
├────────┼─────────┼─────────┼─────────┼──────┼──────┼──────────┼─────────┤
│   1    │  ALIVE  │  402.3  │  +18.5  │  73% │  24°C│   45 ms  │   —     │
│   2    │  ALIVE  │  401.9  │  +17.8  │  72% │  26°C│   52 ms  │   —     │
│   3    │  LOST   │  ---    │  ---    │ ---  │ ---  │ 4520 ms  │ TIMEOUT │
│  ...                                                                   │
└────────┴─────────┴─────────┴─────────┴──────┴──────┴──────────┴─────────┘
```

## Hardware

### Master
- ≥2 CAN-шини
- Підходить: `3LB`, `devkit`, `stark`. Не підходить: `LilyGo single-CAN`
- `can_config.battery` → cluster bus
- `can_config.inverter` → реальний інвертор

### Satellite
- ≥2 CAN-шини (стандартно для BE single-pack уже сьогодні)
- `can_config.battery` → локальний BMS
- `can_config.inverter` → cluster bus (виходить на master)

**Висновок:** жодних нових апаратних вимог. Будь-яка multi-CAN BE-плата підходить.

## Структура файлів (нові)

```
Software/src/inverter/
  CLUSTER-NODE-CAN.cpp     // ClusterNodeCanInverter : InverterProtocol
  CLUSTER-NODE-CAN.h
Software/src/battery/
  CLUSTER-CAN.cpp          // ClusterCanBattery : Battery
  CLUSTER-CAN.h
  CLUSTER-HTML.h           // CLUSTER-HTML.cpp + BatteryHtmlRenderer для master view
  CLUSTER-HTML.cpp
```

### Зміни існуючих файлів

- `Software/src/inverter/InverterProtocol.h` — додати `ClusterNodeCan = 25` в `InverterProtocolType` (перед `Highest`)
- `Software/src/inverter/INVERTERS.h` — `#include "CLUSTER-NODE-CAN.h"`
- `Software/src/inverter/INVERTERS.cpp` — додати case в `name_for_inverter_type()` та в `setup_inverter()` switch
- `Software/src/battery/Battery.h` — додати `ClusterCan = 53` в `BatteryType` (перед `Highest`)
- `Software/src/battery/BATTERIES.cpp` — додати case в `name_for_battery_type()` та в `create_battery()` switch
- `Software/src/battery/BATTERIES.h` — `#include "CLUSTER-CAN.h"`
- `Software/src/communication/nvm/comm_nvm.cpp` — load/save `clst_pack_id`, `clst_pack_cnt`
- `Software/src/devboard/webserver/settings_html.cpp` — UI для нових параметрів (conditional rendering)
- `Software/src/devboard/utils/events.h` (та .cpp) — нові event-codes:
  ```
  EVENT_CLUSTER_PACK_LOST
  EVENT_CLUSTER_DUPLICATE_PACK_ID
  EVENT_CLUSTER_UNCONFIGURED_PACK
  EVENT_CLUSTER_VOLTAGE_DIVERGENCE
  EVENT_CLUSTER_INSUFFICIENT_PACKS
  EVENT_CLUSTER_TOPOLOGY_MISMATCH
  ```

## Backwards compatibility

Cluster — **чисто адитивна** фіча.
- Single-pack BE — без змін
- Double-pack (`battery2`) — без змін
- Triple-pack (`battery3`) — без змін
- Жодного існуючого battery- або inverter-драйвера не модифіковано

Користувачі, які не вмикають Cluster Mode, не побачать різниці.

## Безпекові інваріанти

1. До отримання `expected_pack_count` живих пакетів master шле інвертору `bms_status = UPDATING`, `max_charge/discharge = 0`
2. Якщо хоч один пакет має `max_charge = 0` → cluster шле `max_charge = 0`
3. Те саме для discharge
4. `bms_status` cluster'а ≥ worst pack `bms_status` (worst-of-N)
5. Pack timeout > 1000 ms → виключається з агрегації; `< expected_count` живих → FAULT
6. Voltage divergence > 5V серед alive packs → warning event (м'який сигнал)
7. (v2) Voltage spread > 1.5V → master НЕ дає permission на закриття контакторів; satellite залишає контактори відкритими
8. (v2) Master heartbeat timeout > 1500 ms → satellite вважає permission = false → BE відкриває контактор (fail-safe)

## v2: Master-coordinated contactor permission

**Заміна** концепції "external hardware" з v1. Master використовує існуючий BE-механізм `inverter_allows_contactor_closing` для централізованого voltage-matching та координації закриття контакторів усіх satellite. Pre-charge виконує кожен satellite свій локальний (через його battery-driver, без змін). Master лише гейтить **момент закриття main contactor** на основі агрегованої телеметрії.

### Як це лягає на існуючу BE-архітектуру

`InverterProtocol::controls_contactor()` і `allows_contactor_closing()` — стандартні віртуальні методи (`InverterProtocol.h:53-55`). У `comm_contactorcontrol.cpp:143-144`:
```cpp
if (inverter && inverter->controls_contactor()) {
  datalayer.system.status.inverter_allows_contactor_closing = inverter->allows_contactor_closing();
}
```
Цей флаг далі гейтить контактор у `comm_contactorcontrol.cpp:193, 201, 265` — повністю штатна логіка для будь-якого сьогоднішнього інвертора. Ми використовуємо її як є.

### Новий frame: master → satellites

Single broadcast frame, періодично:

```
Frame 0x5F0 — Cluster permissions, master broadcast every 100 ms:
  [0]   permission_bitmap   (bit i = pack (i+1) may close main contactor)
  [1]   sequence            (uint8 — інкремент per frame)
  [2-7] reserved
```

CAN ID `0x5F0` стоїть окремо від satellite frame-base (0x500..0x540), не перетинається.

### Master permission rule

Для кожного `pack_id ∈ 1..MAX_PACKS` встановлюємо bit `(pack_id - 1)` тільки якщо ВСІ умови:

1. `packs[pack_id - 1].alive` — pack живий
2. `r.n_alive >= expected_pack_count` — достатньо пакетів у кластері
3. `r.voltage_max_dV - r.voltage_min_dV <= CONTACTOR_CLOSE_VOLTAGE_THRESHOLD_DV` (15dV = 1.5V)
4. `r.bms_status != FAULT` — кластер не у фолті

Інакше bit залишається `0`. У результаті:
- Усі пакети закриваються одночасно якщо все добре
- Жоден не закривається якщо щось не так
- Можливо у майбутньому — селективний deny конкретного пакета (сьогодні немає причини; всі або ніхто)

### Satellite — `ClusterNodeCanInverter`

```cpp
bool controls_contactor() override { return true; }
bool allows_contactor_closing() override {
  uint8_t pid = user_selected_cluster_node_pack_id;
  if (pid < 1 || pid > MAX_VALID_PACK_ID) return false;  // unconfigured
  if (millis() - last_master_frame_ms > MASTER_HEARTBEAT_TIMEOUT_MS) return false;  // master gone
  return (last_permissions_bitmap >> (pid - 1)) & 0x01;
}
```

Receive handler оновлює `last_permissions_bitmap` і `last_master_frame_ms` коли приходить frame `0x5F0`.

### Voltage threshold rationale

Тримаємо два пороги на різних рівнях суворості:
- `VOLTAGE_DIVERGENCE_THRESHOLD_DV = 50` (5V) — м'який, raise warning event, не блокує
- `CONTACTOR_CLOSE_VOLTAGE_THRESHOLD_DV = 15` (1.5V) — суворий, блокує закриття контактора

1.5V узгоджено з існуючим `parallel_safety.cpp` для double/triple-battery — однакова поведінка з double-battery режимом.

### Fail-safe сценарії

| Сценарій | Поведінка |
|----------|-----------|
| Master power loss / reboot | Frame не приходить → satellite через 1500ms → `false` → BE відкриває контактор |
| Cluster CAN bus down | Frame не доходить → той самий timeout → відкриваються |
| Один satellite тимчасово відстав | Master бачить voltage spread → drops permission для всіх → відкриваються до повернення |
| Satellite з `pack_id = 0` | `allows_contactor_closing()` завжди false → не закривається. Master не присилатиме permission бо unconfigured pack alarm |
| Master не визнає сатеміта | Satellite не отримує bit для свого pack_id → false |

### Hardware implications

Скасовується вимога з v1: "external hardware керує контакторами з voltage matching". Достатньо стандартних BE-керованих контакторів на satellite (як у single-pack BE сьогодні): pre-charge resistor + main contactor pin, керовані `comm_contactorcontrol.cpp`. Кожен satellite використовує свою існуючу схему.

## v3: Protocol versioning

Додано explicit version negotiation, щоб у майбутньому при breaking-change у wire-format обидві сторони відмовлялись працювати замість тихого misinterpretation'у байтів.

### Constant

```cpp
constexpr uint8_t CLUSTER_PROTOCOL_VERSION = 1;
```

Bumping rules:
- **Bump** — будь-яка зміна layout існуючого фрейму, або зміна семантики поля (одиниці виміру, інтерпретація бітів)
- **НЕ bump** — додавання нових фреймів, заповнення reserved байтів новим змістом (старші версії ігнорують)
- **НЕ bump** — зміни логіки на одній стороні (агрегаційні правила, voltage thresholds)

### Satellite → Master: Frame 5

Новий низькочастотний фрейм:

```
Frame 5 (0x55X) — Protocol info, every 5000 ms:
  [0]   protocol_version  (uint8) — CLUSTER_PROTOCOL_VERSION at build time
  [1-7] reserved (zero) — for future capability/feature flags
```

Base ID `0x550`, з offset'ом `+pack_id` (0x551..0x558). 7 reserved байтів — простір для майбутніх opt-in полів без bumping'а версії.

### Master → Satellites: оновлений 0x5F0

Використовуємо раніше зарезервований byte 2:

```
Frame 0x5F0 (updated):
  [0]   permission_bitmap
  [1]   sequence
  [2]   master_protocol_version    ← v3
  [3-7] reserved
```

Жодного нового frame ID на master-side — лише розширення існуючого.

### Логіка перевірки

**Master:**
- При прийомі Frame 5 від pack — зберігає `protocol_version` та `protocol_version_seen=true` у `PackSnapshot`
- `compute_permission_bitmap()` має додаткову (strict) перевірку: якщо хоч один alive pack має `protocol_version_seen=true && protocol_version != CLUSTER_PROTOCOL_VERSION` → bitmap = 0 (вся cluster без permission)
- Pack що ще не надіслав Frame 5 — `protocol_version_seen=false` → не отримує permission, але не блокує інших (короткочасно при startup, до ~5s)
- Якщо є mismatch → set_event(EVENT_CLUSTER_PROTOCOL_VERSION_MISMATCH)

**Satellite:**
- При прийомі 0x5F0 — читає byte 2 → `last_master_protocol_version`
- `allows_contactor_closing()` повертає `false` якщо `last_master_protocol_version != CLUSTER_PROTOCOL_VERSION` (additional gate after pack_id check, master_seen, heartbeat timeout)
- Якщо є mismatch → set_event(EVENT_CLUSTER_PROTOCOL_VERSION_MISMATCH)

### Чому "strict" mode

Будь-який version mismatch у кластері означає одне з:
- Хтось деплойнув не той firmware на одну з нод
- Wire fault corrupting frames
- Software bug

Безпечна реакція — **зупинити все**, бо ми не довіряємо стану. Кращий помилково-консервативний день, ніж runaway charge/discharge через misinterpreted поле.

### Backwards compatibility

Жодної. v3 — перший release з версіонуванням; нічого ще немає в полі. У майбутньому якщо буде v2: master і satellite з різними версіями просто не будуть працювати разом (deliberate). Користувач має оновити обидві сторони синхронно.

## Відкриті питання / майбутні розширення

- **Trust mode** (`safe` / `aggressive`) — переключення між `min × N` і SUM для лімітів. Поки не реалізуємо, але архітектура дозволяє додати без breaking changes.
- **Per-cell voltages aggregation** — якщо колись знадобиться (наприклад для cellmonitor сторінки на master) — додати opt-in 4-бітний фрейм-набір з більшою періодичністю (1 фрейм на пакет на 200ms × 8 байт = 16 cells). Потрібно лише при N_cells_per_pack > ~200.
- **CAN-FD** — якщо в майбутньому понадобиться передавати більше даних (наприклад per-cell), CAN-FD дає 64 байти/фрейм. Архітектурно — апгрейд на рівні фрейм-формату; адресна схема не зміниться.
- **Master fallback на single satellite** — якщо master впав, можна було б автоматично переключити satellite на pack-mode напряму до інвертора. Поза скоупом v1.
- **>8 пакетів** — потребуватиме extended 29-bit ID або іншого base-offset. Архітектурно — заміна 4-бітного ID на 5-бітний; інші компоненти не змінюються.