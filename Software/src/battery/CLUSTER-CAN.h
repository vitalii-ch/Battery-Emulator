#ifndef CLUSTER_CAN_H
#define CLUSTER_CAN_H

#include "CanBattery.h"
#include "CLUSTER-PROTOCOL.h"
#include "CLUSTER-HTML.h"

class ClusterCanBattery : public CanBattery {
 public:
  static constexpr const char* Name = "BE Cluster (paralleled packs)";

  ClusterCanBattery();

  void setup() override;
  void update_values() override;
  void handle_incoming_can_frame(CAN_frame rx_frame) override;
  void transmit_can(unsigned long currentMillis) override;

  BatteryHtmlRenderer& get_status_renderer() override { return html_renderer; }

  // Public for HTML renderer access
  const cluster_protocol::PackSnapshot& get_pack(uint8_t pack_id) const {
    return packs[pack_id_to_index(pack_id)];
  }

  // Visible for tests: most recently computed contactor permission bitmap
  // (one bit per pack, bit i = pack (i+1) may close contactor).
  uint8_t current_permission_bitmap() const { return latest_permission_bitmap; }

 private:
  cluster_protocol::PackSnapshot packs[cluster_protocol::MAX_PACKS] = {};
  bool insufficient_packs_event_active = false;
  ClusterHtmlRenderer html_renderer{*this};

  // v2 contactor permission state
  uint8_t latest_permission_bitmap = 0;
  unsigned long previousMillis_permissions = 0;
  uint8_t permissions_seq = 0;
  CAN_frame tx_permissions_frame = {.FD = false, .ext_ID = false, .DLC = 8,
                                    .ID = cluster_protocol::MASTER_PERMISSIONS_FRAME_ID,
                                    .data = {0}};

  static uint8_t pack_id_to_index(uint8_t pack_id) {
    // pack_id 1..8 -> index 0..7
    return (pack_id >= 1 && pack_id <= cluster_protocol::MAX_PACKS) ? (pack_id - 1) : 0;
  }

  void update_alive_flags(uint32_t now_ms);
  void check_voltage_divergence(const cluster_protocol::AggregateResult& r);
  void check_topology_consistency();
  void apply_to_datalayer(const cluster_protocol::AggregateResult& r);
  uint8_t compute_permission_bitmap(const cluster_protocol::AggregateResult& r) const;
};

extern uint8_t user_selected_cluster_expected_pack_count;  // 1..8

#endif
