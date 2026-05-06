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
