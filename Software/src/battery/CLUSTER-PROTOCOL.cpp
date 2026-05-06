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

AggregateResult aggregate(const PackSnapshot[MAX_PACKS]) {
  AggregateResult r = {};
  return r;
}

}  // namespace cluster_protocol
