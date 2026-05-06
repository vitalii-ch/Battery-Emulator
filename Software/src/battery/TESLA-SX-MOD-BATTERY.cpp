#include "TESLA-SX-MOD-BATTERY.h"

// Defined in TESLA-BATTERY.cpp -- the only frame arrays we reuse.
extern const CAN_frame can_msg_1CF[];
extern const CAN_frame can_msg_118[];

void TeslaModelSXModBattery::setup(void) {
  if (allows_contactor_closing) {
    *allows_contactor_closing = true;
  }

  strncpy(datalayer.system.info.battery_protocol, Name, 63);
  datalayer.system.info.battery_protocol[63] = '\0';
  // Suppress parent's auto-triggered UDS query for the BMS part number:
  // SX-Mod overrides transmit_can() and never advances UDS state machines,
  // so leaving this false would park stateMachineBMSQuery at 0 forever.
  parsed_battery_partNumber = true;

  // Defense-in-depth: clear any user-trigger UDS request flags up-front so the
  // inherited update_values() cannot arm stateMachine{ClearIsolationFault,BMSReset,SOCReset}
  // — Mod's transmit_can() never services them and they would silently hang at 0.
  datalayer_battery->settings.user_requests_tesla_isolation_clear = false;
  datalayer_battery->settings.user_requests_tesla_bms_reset = false;
  datalayer_battery->settings.user_requests_tesla_soc_reset = false;

  datalayer_battery->info.max_design_voltage_dV = MAX_PACK_VOLTAGE_SX_NCMA;
  datalayer_battery->info.min_design_voltage_dV = MIN_PACK_VOLTAGE_SX_NCMA;
  datalayer_battery->info.max_cell_voltage_mV = MAX_CELL_VOLTAGE_NCA_NCM;
  datalayer_battery->info.min_cell_voltage_mV = MIN_CELL_VOLTAGE_NCA_NCM;
  datalayer_battery->info.max_cell_voltage_deviation_mV = MAX_CELL_DEVIATION_NCA_NCM;
}

void TeslaModelSXModBattery::transmit_can(unsigned long currentMillis) {
  // 10ms: digital HVIL (0x1CF rotates 0..7, 0x118 rotates 0..15)
  if (currentMillis - previousMillis10 >= INTERVAL_10_MS) {
    previousMillis10 = currentMillis;

    if (datalayer.system.status.inverter_allows_contactor_closing) {
      transmit_can_frame(&can_msg_1CF[index_1CF]);
      index_1CF = (index_1CF + 1) % 8;

      transmit_can_frame(&can_msg_118[index_118]);
      index_118 = (index_118 + 1) % 16;
    } else {
      index_1CF = 0;
      index_118 = 0;
    }
  }

  // 30ms: static 0x221 pair (close contactors + hv_up_for_drive)
  if (currentMillis - previousMillis30 >= INTERVAL_30_MS) {
    previousMillis30 = currentMillis;

    if (datalayer.system.status.inverter_allows_contactor_closing) {
      transmit_can_frame(&TESLA_221_STATIC_1);
      transmit_can_frame(&TESLA_221_STATIC_2);
    }
  }
}
