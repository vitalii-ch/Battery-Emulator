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
