#ifndef CLUSTER_NODE_CAN_H
#define CLUSTER_NODE_CAN_H

#include "../CanInverterProtocol.h"

class ClusterNodeCanInverter : public CanInverterProtocol {
 public:
  static constexpr const char* Name = "BE Cluster Node (paralleled packs)";
  const char* name() override { return Name; }

  void update_values() override;
  void transmit_can(unsigned long currentMillis) override;
  void map_can_frame_to_variable(CAN_frame rx_frame) override;

  // v2: master coordinates contactor closing through inverter_allows_contactor_closing.
  bool controls_contactor() override { return true; }
  bool allows_contactor_closing() override;

 private:
  unsigned long previousMillis_frame0 = 0;
  unsigned long previousMillis_frame1 = 0;
  unsigned long previousMillis_frame2 = 0;
  unsigned long previousMillis_frame3 = 0;
  unsigned long previousMillis_frame4 = 0;
  uint8_t seq_counter = 0;

  // v2: master heartbeat / permissions state
  unsigned long last_master_frame_ms = 0;
  bool master_seen_ever = false;
  uint8_t last_permissions_bitmap = 0;
  uint8_t last_master_seq = 0;

  CAN_frame tx_frame0 = {.FD = false, .ext_ID = false, .DLC = 8, .ID = 0, .data = {0}};
  CAN_frame tx_frame1 = {.FD = false, .ext_ID = false, .DLC = 8, .ID = 0, .data = {0}};
  CAN_frame tx_frame2 = {.FD = false, .ext_ID = false, .DLC = 8, .ID = 0, .data = {0}};
  CAN_frame tx_frame3 = {.FD = false, .ext_ID = false, .DLC = 8, .ID = 0, .data = {0}};
  CAN_frame tx_frame4 = {.FD = false, .ext_ID = false, .DLC = 8, .ID = 0, .data = {0}};
};

extern uint8_t user_selected_cluster_node_pack_id;  // 1..8 (0 = unconfigured)

#endif
