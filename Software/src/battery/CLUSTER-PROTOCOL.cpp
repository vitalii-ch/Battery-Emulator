#include "CLUSTER-PROTOCOL.h"
#include "../devboard/utils/types.h"
#include <climits>

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

void encode_permissions(uint8_t buf[8], uint8_t permission_bitmap, uint8_t seq) {
  buf[0] = permission_bitmap;
  buf[1] = seq;
  buf[2] = 0;
  buf[3] = 0;
  buf[4] = 0;
  buf[5] = 0;
  buf[6] = 0;
  buf[7] = 0;
}

void decode_permissions(const uint8_t buf[8], uint8_t& permission_bitmap, uint8_t& seq) {
  permission_bitmap = buf[0];
  seq = buf[1];
}

// Worst-of priority for bms_status_enum:
// FAULT > UPDATING > STANDBY > INACTIVE > DARKSTART > ACTIVE > others
static int bms_status_priority(uint8_t s) {
  switch (s) {
    case FAULT:     return 5;
    case UPDATING:  return 4;
    case STANDBY:   return 3;
    case INACTIVE:  return 2;
    case DARKSTART: return 1;
    case ACTIVE:    return 0;
    default:        return 0;
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

}  // namespace cluster_protocol
