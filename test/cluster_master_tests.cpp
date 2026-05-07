#include <gtest/gtest.h>
#include "../Software/src/battery/cluster/CLUSTER-CAN.h"
#include "../Software/src/battery/cluster/CLUSTER-PROTOCOL.h"
#include "../Software/src/datalayer/datalayer.h"
#include "../Software/src/devboard/utils/events.h"

extern uint64_t current_time;  // from test/emul/time.cpp; controls millis()
void init_events(void);

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
  void TearDown() override { delete master; }

  void send_frame0(uint8_t pack_id, uint16_t v_dV, int16_t i_dA, uint16_t soc_pptt, uint8_t bms_status,
                   uint8_t seq) {
    CAN_frame f = {.FD = false, .ext_ID = false, .DLC = 8, .ID = frame_id(FRAME0_BASE, pack_id), .data = {0}};
    encode_frame0(f.data.u8, v_dV, i_dA, soc_pptt, bms_status, seq);
    master->handle_incoming_can_frame(f);
  }
  void send_frame1(uint8_t pack_id, uint16_t maxc, uint16_t maxd, uint16_t cmax, uint16_t cmin) {
    CAN_frame f = {.FD = false, .ext_ID = false, .DLC = 8, .ID = frame_id(FRAME1_BASE, pack_id), .data = {0}};
    encode_frame1(f.data.u8, maxc, maxd, cmax, cmin);
    master->handle_incoming_can_frame(f);
  }
  void send_frame3(uint8_t pack_id, uint32_t total_Wh, uint32_t rem_Wh) {
    CAN_frame f = {.FD = false, .ext_ID = false, .DLC = 8, .ID = frame_id(FRAME3_BASE, pack_id), .data = {0}};
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

  // Both packs alive at t=1000.
  // Advance time to 3000 — both packs > PACK_TIMEOUT_MS (1000ms) old → timeout.
  current_time += 2000;
  master->update_values();

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
  send_frame0(1, 4000, 0, 5000, ACTIVE, 1);
  send_frame3(1, 30000, 15000);
  send_frame0(2, 4080, 0, 5000, ACTIVE, 1);  // 8V higher → divergence > 5V threshold
  send_frame3(2, 30000, 15000);
  master->update_values();
  EXPECT_EQ(get_event_pointer(EVENT_CLUSTER_VOLTAGE_DIVERGENCE)->state, EVENT_STATE_ACTIVE);
}

TEST_F(ClusterMasterTest, UnconfiguredPackTriggersEvent) {
  // ID == FRAME0_BASE (pack_id=0) is the unconfigured sentinel
  CAN_frame f = {.FD = false, .ext_ID = false, .DLC = 8, .ID = FRAME0_BASE, .data = {0}};
  master->handle_incoming_can_frame(f);
  EXPECT_EQ(get_event_pointer(EVENT_CLUSTER_UNCONFIGURED_PACK)->state, EVENT_STATE_ACTIVE);
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
  EXPECT_EQ(datalayer.battery.status.max_charge_current_dA, 100);   // min(50)*2
  EXPECT_EQ(datalayer.battery.status.max_discharge_current_dA, 400);  // min(200)*2
}

// v2 contactor permission tests

TEST_F(ClusterMasterTest, PermissionBitmapZeroWhenNoPacks) {
  master->update_values();
  EXPECT_EQ(master->current_permission_bitmap(), 0);
}

TEST_F(ClusterMasterTest, PermissionBitmapZeroWhenInsufficientPacks) {
  user_selected_cluster_expected_pack_count = 3;
  send_frame0(1, 4000, 0, 5000, ACTIVE, 1);
  send_frame0(2, 4000, 0, 5000, ACTIVE, 1);
  master->update_values();
  EXPECT_EQ(master->current_permission_bitmap(), 0);
}

TEST_F(ClusterMasterTest, PermissionBitmapZeroOnVoltageSpread) {
  user_selected_cluster_expected_pack_count = 2;
  // Spread = 16 dV (1.6V) > CONTACTOR_CLOSE_VOLTAGE_THRESHOLD_DV (15 dV / 1.5V)
  send_frame0(1, 4000, 0, 5000, ACTIVE, 1);
  send_frame3(1, 30000, 15000);
  send_frame0(2, 4016, 0, 5000, ACTIVE, 1);
  send_frame3(2, 30000, 15000);
  master->update_values();
  EXPECT_EQ(master->current_permission_bitmap(), 0);
}

TEST_F(ClusterMasterTest, PermissionBitmapSetWhenAllOk) {
  user_selected_cluster_expected_pack_count = 2;
  send_frame0(1, 4000, 0, 5000, ACTIVE, 1);
  send_frame3(1, 30000, 15000);
  send_frame0(2, 4010, 0, 5000, ACTIVE, 1);
  send_frame3(2, 30000, 15000);
  master->update_values();
  // Bits 0 and 1 set = packs 1 and 2 alive and permitted
  EXPECT_EQ(master->current_permission_bitmap(), 0x03);
}

TEST_F(ClusterMasterTest, PermissionBitmapZeroOnFault) {
  user_selected_cluster_expected_pack_count = 2;
  send_frame0(1, 4000, 0, 5000, ACTIVE, 1);
  send_frame3(1, 30000, 15000);
  send_frame0(2, 4000, 0, 5000, FAULT, 1);  // pack 2 in FAULT
  send_frame3(2, 30000, 15000);
  master->update_values();
  EXPECT_EQ(master->current_permission_bitmap(), 0);
}

TEST_F(ClusterMasterTest, PermissionBitmapClearsAfterPackTimeout) {
  user_selected_cluster_expected_pack_count = 2;
  send_frame0(1, 4000, 0, 5000, ACTIVE, 1);
  send_frame3(1, 30000, 15000);
  send_frame0(2, 4000, 0, 5000, ACTIVE, 1);
  send_frame3(2, 30000, 15000);
  master->update_values();
  EXPECT_EQ(master->current_permission_bitmap(), 0x03);

  // Advance time past pack timeout — both packs become not-alive
  current_time += 2000;
  master->update_values();
  EXPECT_EQ(master->current_permission_bitmap(), 0);
}

TEST_F(ClusterMasterTest, PermissionAtExactThresholdAllowed) {
  user_selected_cluster_expected_pack_count = 2;
  // Spread = exactly 15 dV (1.5V) — at threshold, should still be permitted
  send_frame0(1, 4000, 0, 5000, ACTIVE, 1);
  send_frame3(1, 30000, 15000);
  send_frame0(2, 4015, 0, 5000, ACTIVE, 1);
  send_frame3(2, 30000, 15000);
  master->update_values();
  EXPECT_EQ(master->current_permission_bitmap(), 0x03);
}
