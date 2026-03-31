#include "TESLA-SX-MOD-BATTERY.h"
#include "../devboard/utils/logging.h"

// Defined in TESLA-BATTERY.cpp -- digital HVIL frame arrays
extern CAN_frame can_msg_1CF[];
extern CAN_frame can_msg_118[];

// Defined in TESLA-BATTERY.cpp -- checksum generators
extern void generateMuxFrameCounterChecksum(CAN_frame& f, uint8_t frameCounter, int ctrStartBit, int ctrBitLength,
                                            int csumStartBit, int csumBitLength);

void TeslaModelSXModBattery::setup(void) {
  if (allows_contactor_closing) {
    *allows_contactor_closing = true;
  }

  strncpy(datalayer.system.info.battery_protocol, Name, 63);
  datalayer.system.info.battery_protocol[63] = '\0';
  datalayer.battery.info.max_design_voltage_dV = MAX_PACK_VOLTAGE_SX_NCMA;
  datalayer.battery.info.min_design_voltage_dV = MIN_PACK_VOLTAGE_SX_NCMA;
  datalayer.battery.info.max_cell_voltage_mV = MAX_CELL_VOLTAGE_NCA_NCM;
  datalayer.battery.info.min_cell_voltage_mV = MIN_CELL_VOLTAGE_NCA_NCM;
  datalayer.battery.info.max_cell_voltage_deviation_mV = MAX_CELL_DEVIATION_NCA_NCM;

  // Initialize startup CAN block based on flag
  startup_can_blocked = suppress_can_until_drive;
}

bool TeslaModelSXModBattery::uds_active() {
  return (stateMachineClearIsolationFault != 0xFF) || (stateMachineBMSReset != 0xFF) ||
         (stateMachineSOCReset != 0xFF) || (stateMachineBMSQuery != 0xFF);
}

void TeslaModelSXModBattery::transmit_can(unsigned long currentMillis) {

  // When suppress_can_until_drive is enabled:
  // Block all CAN TX while vehicleState is still OFF after power-on.
  // Once vehicleState transitions away from OFF, lift the block permanently.
  if (startup_can_blocked) {
    if (vehicleState != CAR_OFF) {
      startup_can_blocked = false;
      logging.println("SX-Mod: Startup CAN block lifted, vehicleState left OFF");
    } else {
      return;  // Suppress all CAN transmission during startup OFF state
    }
  }

  // If minimal mode is OFF, or we're NOT in normal DRIVE, or UDS is active:
  // use the full parent implementation (all 25+ frames, including UDS state machines)
  if (!minimal_can_frames || vehicleState != CAR_DRIVE || uds_active()) {
    TeslaBattery::transmit_can(currentMillis);
    return;
  }

  // --- Normal DRIVE in minimal mode: only 0x1CF, 0x118, and 0x221 ---

  // Send 10ms messages: Digital HVIL (0x1CF + 0x118)
  if (currentMillis - previousMillis10 >= INTERVAL_10_MS) {
    previousMillis10 = currentMillis;

    // Gate only by inverter_allows (NO FAULT check -- fixes chicken-and-egg deadlock)
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

  // Send 0x221 contactor command (we're guaranteed vehicleState == CAR_DRIVE here)
  if (use_static_221) {
    // Old format: two static frames every 30ms, no counter/checksum
    if (currentMillis - previousMillis30 >= INTERVAL_30_MS) {
      previousMillis30 = currentMillis;

      if (datalayer.system.status.inverter_allows_contactor_closing) {
        transmit_can_frame(&TESLA_221_STATIC_1);
        transmit_can_frame(&TESLA_221_STATIC_2);
      }
    }
  } else {
    // New format: muxed frames with counter+checksum every 50ms
    if (currentMillis - previousMillis50 >= INTERVAL_50_MS) {
      previousMillis50 = currentMillis;

      if (alternateMux) {
        generateMuxFrameCounterChecksum(TESLA_221_DRIVE_Mux0, frameCounter_TESLA_221, 52, 4, 56, 8);
        transmit_can_frame(&TESLA_221_DRIVE_Mux0);
      } else {
        generateMuxFrameCounterChecksum(TESLA_221_DRIVE_Mux1, frameCounter_TESLA_221, 52, 4, 56, 8);
        transmit_can_frame(&TESLA_221_DRIVE_Mux1);
      }

      alternateMux ^= 1;
      frameCounter_TESLA_221 = (frameCounter_TESLA_221 + 1) % 16;
    }
  }
}
