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
