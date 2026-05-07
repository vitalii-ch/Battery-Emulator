#include "CLUSTER-CAN.h"
#include <Arduino.h>
#include "../../datalayer/datalayer.h"
#include "../../devboard/utils/events.h"

using namespace cluster_protocol;

uint8_t user_selected_cluster_expected_pack_count = 1;

ClusterCanBattery::ClusterCanBattery() : CanBattery() {}

void ClusterCanBattery::setup() {
  // datalayer fields will be filled from frame4 (static info) once first pack reports
  // Until then, keep defaults.
  datalayer.battery.status.bms_status = UPDATING;
  datalayer.battery.status.max_charge_current_dA = 0;
  datalayer.battery.status.max_discharge_current_dA = 0;
}

void ClusterCanBattery::handle_incoming_can_frame(CAN_frame rx_frame) {
  uint16_t id = rx_frame.ID;
  uint8_t pack_id = 0;
  void (*decoder)(const uint8_t[8], PackSnapshot&) = nullptr;

  if (is_frame_for_base(id, FRAME0_BASE)) {
    pack_id = pack_id_from_frame(id, FRAME0_BASE);
    decoder = decode_frame0;
  } else if (is_frame_for_base(id, FRAME1_BASE)) {
    pack_id = pack_id_from_frame(id, FRAME1_BASE);
    decoder = decode_frame1;
  } else if (is_frame_for_base(id, FRAME2_BASE)) {
    pack_id = pack_id_from_frame(id, FRAME2_BASE);
    decoder = decode_frame2;
  } else if (is_frame_for_base(id, FRAME3_BASE)) {
    pack_id = pack_id_from_frame(id, FRAME3_BASE);
    decoder = decode_frame3;
  } else if (is_frame_for_base(id, FRAME4_BASE)) {
    pack_id = pack_id_from_frame(id, FRAME4_BASE);
    decoder = decode_frame4;
  } else if (is_frame_for_base(id, FRAME5_BASE)) {
    pack_id = pack_id_from_frame(id, FRAME5_BASE);
    decoder = decode_frame5;
  } else if (id == FRAME0_BASE || id == FRAME1_BASE || id == FRAME2_BASE
             || id == FRAME3_BASE || id == FRAME4_BASE || id == FRAME5_BASE) {
    // pack_id = 0 sentinel — unconfigured satellite
    set_event(EVENT_CLUSTER_UNCONFIGURED_PACK, 0);
    return;
  } else {
    return;  // Not our frame
  }

  if (pack_id < MIN_VALID_PACK_ID || pack_id > MAX_VALID_PACK_ID) return;

  uint8_t idx = pack_id - 1;
  PackSnapshot& s = packs[idx];

  // Frame 0 carries seq counter — detect duplicate ID via sequence anomaly.
  // Two satellites with the same pack_id will interleave their counters,
  // so we expect to see the seq either matching expected_next or close to
  // it (allowing a few lost frames). A large negative jump in (buf_seq -
  // expected_next), interpreted as a signed int8, indicates the seq
  // moved backwards by more than DUPLICATE_ID_BACKWARD_JUMP — a strong
  // signal of two transmitters fighting for the same ID. Cast to int8_t
  // means counter wrap (255 → 0) appears as delta = -1, not flagged.
  constexpr int8_t DUPLICATE_ID_BACKWARD_JUMP = -8;
  if (decoder == decode_frame0 && s.seen_ever) {
    uint8_t prev_seq = s.last_seq;
    uint8_t expected_next = (uint8_t)(prev_seq + 1);
    uint8_t buf_seq = rx_frame.data.u8[7];
    int8_t delta = (int8_t)(buf_seq - expected_next);
    if (delta < DUPLICATE_ID_BACKWARD_JUMP) {
      set_event(EVENT_CLUSTER_DUPLICATE_PACK_ID, pack_id);
    }
  }

  decoder(rx_frame.data.u8, s);
  s.last_seen_ms = millis();
  s.seen_ever = true;
}

void ClusterCanBattery::update_alive_flags(uint32_t now_ms) {
  for (uint8_t i = 0; i < MAX_PACKS; ++i) {
    bool was_alive = packs[i].alive;
    bool now_alive = packs[i].seen_ever &&
                     (now_ms - packs[i].last_seen_ms < PACK_TIMEOUT_MS);
    packs[i].alive = now_alive;
    if (was_alive && !now_alive) {
      set_event(EVENT_CLUSTER_PACK_LOST, (uint8_t)(i + 1));
    }
  }
}

void ClusterCanBattery::check_voltage_divergence(const AggregateResult& r) {
  if (r.n_alive < 2) return;
  uint16_t diff = r.voltage_max_dV - r.voltage_min_dV;
  if (diff > VOLTAGE_DIVERGENCE_THRESHOLD_DV) {
    set_event(EVENT_CLUSTER_VOLTAGE_DIVERGENCE, (uint8_t)(diff / 10));
  }
}

void ClusterCanBattery::check_topology_consistency() {
  uint8_t ref_chem = 0xFF;
  uint8_t ref_cells = 0;
  for (uint8_t i = 0; i < MAX_PACKS; ++i) {
    if (!packs[i].alive) continue;
    if (packs[i].number_of_cells == 0) continue;  // frame4 not yet received
    if (ref_chem == 0xFF) {
      ref_chem = packs[i].chemistry;
      ref_cells = packs[i].number_of_cells;
    } else if (packs[i].chemistry != ref_chem || packs[i].number_of_cells != ref_cells) {
      set_event(EVENT_CLUSTER_TOPOLOGY_MISMATCH, (uint8_t)(i + 1));
      return;
    }
  }
}

void ClusterCanBattery::apply_to_datalayer(const AggregateResult& r) {
  datalayer.battery.status.voltage_dV = r.voltage_dV;
  datalayer.battery.status.current_dA = r.current_dA;
  datalayer.battery.status.reported_current_dA = r.current_dA;
  datalayer.battery.status.reported_soc = (uint16_t)r.reported_soc;
  datalayer.battery.status.real_soc = (uint16_t)r.reported_soc;
  datalayer.battery.status.max_charge_current_dA = r.max_charge_current_dA;
  datalayer.battery.status.max_discharge_current_dA = r.max_discharge_current_dA;
  datalayer.battery.status.max_charge_power_W =
      (uint32_t)r.max_charge_current_dA * (uint32_t)r.voltage_dV / 100u;
  datalayer.battery.status.max_discharge_power_W =
      (uint32_t)r.max_discharge_current_dA * (uint32_t)r.voltage_dV / 100u;
  datalayer.battery.status.cell_max_voltage_mV = r.cell_max_voltage_mV;
  datalayer.battery.status.cell_min_voltage_mV = r.cell_min_voltage_mV;
  datalayer.battery.status.temperature_max_dC = r.temperature_max_dC;
  datalayer.battery.status.temperature_min_dC = r.temperature_min_dC;
  datalayer.battery.status.soh_pptt = r.soh_pptt;
  datalayer.battery.status.bms_status = (bms_status_enum)r.bms_status;
  datalayer.battery.status.balancing_status =
      r.balancing_status ? BALANCING_STATUS_ACTIVE : BALANCING_STATUS_READY;
  datalayer.battery.info.total_capacity_Wh = r.total_capacity_Wh;
  datalayer.battery.info.reported_total_capacity_Wh = r.total_capacity_Wh;
  datalayer.battery.status.remaining_capacity_Wh = r.remaining_capacity_Wh;
  datalayer.battery.status.reported_remaining_capacity_Wh = r.remaining_capacity_Wh;

  // Static info (from any alive pack with frame4 data)
  for (uint8_t i = 0; i < MAX_PACKS; ++i) {
    if (packs[i].alive && packs[i].number_of_cells > 0) {
      datalayer.battery.info.max_design_voltage_dV = packs[i].max_design_voltage_dV;
      datalayer.battery.info.min_design_voltage_dV = packs[i].min_design_voltage_dV;
      datalayer.battery.info.max_cell_voltage_mV = packs[i].max_cell_voltage_limit_mV;
      datalayer.battery.info.chemistry = (battery_chemistry_enum)packs[i].chemistry;
      datalayer.battery.info.number_of_cells = packs[i].number_of_cells;
      // Frame 4 doesn't carry min_cell_voltage_mV; derive a conservative default
      // per chemistry so safety.cpp under-voltage checks use the right floor.
      switch ((battery_chemistry_enum)packs[i].chemistry) {
        case battery_chemistry_enum::LFP:
          datalayer.battery.info.min_cell_voltage_mV = 2500;
          break;
        case battery_chemistry_enum::NCA:
        case battery_chemistry_enum::NMC:
          datalayer.battery.info.min_cell_voltage_mV = 2700;
          break;
        case battery_chemistry_enum::ZEBRA:
          datalayer.battery.info.min_cell_voltage_mV = 1800;
          break;
        default:
          datalayer.battery.info.min_cell_voltage_mV = 2700;
          break;
      }
      break;
    }
  }
}

void ClusterCanBattery::update_values() {
  uint32_t now = millis();
  update_alive_flags(now);

  AggregateResult r = aggregate(packs);

  bool insufficient = (r.n_alive < user_selected_cluster_expected_pack_count);
  if (insufficient && !insufficient_packs_event_active) {
    set_event(EVENT_CLUSTER_INSUFFICIENT_PACKS, r.n_alive);
    insufficient_packs_event_active = true;
  } else if (!insufficient && insufficient_packs_event_active) {
    clear_event(EVENT_CLUSTER_INSUFFICIENT_PACKS);
    insufficient_packs_event_active = false;
  }

  if (insufficient) {
    r.bms_status = (r.n_alive == 0) ? UPDATING : FAULT;
    r.max_charge_current_dA = 0;
    r.max_discharge_current_dA = 0;
  }

  check_voltage_divergence(r);
  check_topology_consistency();
  check_protocol_versions();
  apply_to_datalayer(r);

  // v2: compute per-pack contactor permission bitmap.
  // transmit_can() picks this up and broadcasts every MASTER_PERMISSIONS_PERIOD_MS.
  latest_permission_bitmap = compute_permission_bitmap(r);

  datalayer.battery.status.CAN_battery_still_alive =
      (r.n_alive > 0) ? CAN_STILL_ALIVE : 0;
}

uint8_t ClusterCanBattery::compute_permission_bitmap(const AggregateResult& r) const {
  // Cluster-wide preconditions: all packs share fate. If any of these fail,
  // no satellite gets permission to close.
  if (r.n_alive < user_selected_cluster_expected_pack_count) return 0;
  if (r.bms_status == FAULT) return 0;
  if (r.n_alive >= 2) {
    uint16_t spread = r.voltage_max_dV - r.voltage_min_dV;
    if (spread > CONTACTOR_CLOSE_VOLTAGE_THRESHOLD_DV) return 0;
  }
  // Strict version-mismatch check: a single pack reporting an incompatible
  // CLUSTER_PROTOCOL_VERSION makes the whole cluster untrusted (someone
  // deployed wrong firmware, or wire faults are corrupting frames). Refuse
  // permission to ALL packs until the situation is resolved by a human.
  for (uint8_t i = 0; i < MAX_PACKS; ++i) {
    if (!packs[i].alive) continue;
    if (!packs[i].protocol_version_seen) continue;  // not yet known — wait
    if (packs[i].protocol_version != CLUSTER_PROTOCOL_VERSION) return 0;
  }
  // Permit each alive pack that has fully introduced itself (received Frame 5
  // with matching version). A pack that hasn't yet emitted Frame 5 is held
  // back from permission until its first Frame 5 arrives (≤ 5s after boot).
  uint8_t bitmap = 0;
  for (uint8_t i = 0; i < MAX_PACKS; ++i) {
    if (!packs[i].alive) continue;
    if (!packs[i].protocol_version_seen) continue;
    bitmap |= (uint8_t)(1u << i);
  }
  return bitmap;
}

void ClusterCanBattery::check_protocol_versions() {
  // Raise (or clear) EVENT_CLUSTER_PROTOCOL_VERSION_MISMATCH based on whether
  // any alive pack has reported an incompatible version.
  bool any_mismatch = false;
  uint8_t worst_pack_id = 0;
  for (uint8_t i = 0; i < MAX_PACKS; ++i) {
    if (!packs[i].alive) continue;
    if (!packs[i].protocol_version_seen) continue;  // not yet known
    if (packs[i].protocol_version != CLUSTER_PROTOCOL_VERSION) {
      any_mismatch = true;
      worst_pack_id = (uint8_t)(i + 1);
      break;
    }
  }
  if (any_mismatch && !protocol_version_mismatch_event_active) {
    set_event(EVENT_CLUSTER_PROTOCOL_VERSION_MISMATCH, worst_pack_id);
    protocol_version_mismatch_event_active = true;
  } else if (!any_mismatch && protocol_version_mismatch_event_active) {
    clear_event(EVENT_CLUSTER_PROTOCOL_VERSION_MISMATCH);
    protocol_version_mismatch_event_active = false;
  }
}

void ClusterCanBattery::transmit_can(unsigned long currentMillis) {
  if (currentMillis - previousMillis_permissions >= MASTER_PERMISSIONS_PERIOD_MS) {
    previousMillis_permissions = currentMillis;
    permissions_seq++;
    encode_permissions(tx_permissions_frame.data.u8, latest_permission_bitmap,
                       permissions_seq, CLUSTER_PROTOCOL_VERSION);
    transmit_can_frame(&tx_permissions_frame);
  }
}
