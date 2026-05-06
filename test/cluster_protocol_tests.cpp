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
