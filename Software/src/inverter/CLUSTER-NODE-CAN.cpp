#include "CLUSTER-NODE-CAN.h"
#include "../battery/CLUSTER-PROTOCOL.h"
#include "../datalayer/datalayer.h"

uint8_t user_selected_cluster_node_pack_id = 0;

using namespace cluster_protocol;

void ClusterNodeCanInverter::update_values() {
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
