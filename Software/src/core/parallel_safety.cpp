#include "parallel_safety.h"
#include "../datalayer/datalayer.h"
#include "../devboard/utils/events.h"

void check_parallel_battery_safety(uint8_t batteryNumber) {

  if (batteryNumber == 2) {
    // Fail-closed prerequisites:
    //  - both packs must have a fresh CAN frame counter (datalayer.h default = 0,
    //    parser sets to CAN_STILL_ALIVE on each frame)
    //  - both voltages must be non-zero (update_values() runs before this tick;
    //    voltage_dV is 0 until the first 0x132 frame arrives)
    if (!datalayer.battery.status.CAN_battery_still_alive ||
        !datalayer.battery2.status.CAN_battery_still_alive ||
        datalayer.battery.status.voltage_dV == 0 || datalayer.battery2.status.voltage_dV == 0) {
      datalayer.system.status.battery2_allowed_contactor_closing = false;
      return;
    }

    // EITHER pack reporting FAULT must immediately disengage pack #2 — do not wait
    // through the 10-second voltage drift window. FAULT often comes with sudden
    // voltage jumps; any delay means cross-pack current into the faulted side.
    if (datalayer.battery.status.bms_status == FAULT ||
        datalayer.battery2.status.bms_status == FAULT) {
      datalayer.system.status.battery2_allowed_contactor_closing = false;
      return;
    }
    uint16_t voltage_diff_battery2_towards_main =
        abs(datalayer.battery.status.voltage_dV - datalayer.battery2.status.voltage_dV);
    static uint8_t secondsOutOfVoltageSyncBattery2 = 0;

    if (voltage_diff_battery2_towards_main <= 15) {  // If we are within 1.5V between the batteries
      clear_event(EVENT_VOLTAGE_DIFFERENCE);
      secondsOutOfVoltageSyncBattery2 = 0;
      if (datalayer.battery.status.bms_status == FAULT) {
        // If main battery is in fault state, disengage the second battery
        datalayer.system.status.battery2_allowed_contactor_closing = false;
      } else {  // If main battery is OK, allow second battery to join
        datalayer.system.status.battery2_allowed_contactor_closing = true;
      }
    } else {  //Voltage between the two packs is too large
      set_event(EVENT_VOLTAGE_DIFFERENCE, (uint8_t)(voltage_diff_battery2_towards_main / 10));

      //If we start to drift out of sync between the two packs for more than 10 seconds, open contactors
      if (secondsOutOfVoltageSyncBattery2 < 10) {
        secondsOutOfVoltageSyncBattery2++;
      } else {
        datalayer.system.status.battery2_allowed_contactor_closing = false;
      }
    }
  }

  if (batteryNumber == 3) {
    // Mirror the battery2 guards.
    if (!datalayer.battery.status.CAN_battery_still_alive ||
        !datalayer.battery3.status.CAN_battery_still_alive ||
        datalayer.battery.status.voltage_dV == 0 || datalayer.battery3.status.voltage_dV == 0) {
      datalayer.system.status.battery3_allowed_contactor_closing = false;
      return;
    }

    if (datalayer.battery.status.bms_status == FAULT ||
        datalayer.battery3.status.bms_status == FAULT) {
      datalayer.system.status.battery3_allowed_contactor_closing = false;
      return;
    }
    uint16_t voltage_diff_battery3_towards_main =
        abs(datalayer.battery.status.voltage_dV - datalayer.battery3.status.voltage_dV);
    static uint8_t secondsOutOfVoltageSyncBattery3 = 0;

    if (voltage_diff_battery3_towards_main <= 15) {  // If we are within 1.5V between the batteries
      clear_event(EVENT_VOLTAGE_DIFFERENCE);
      secondsOutOfVoltageSyncBattery3 = 0;
      if (datalayer.battery.status.bms_status == FAULT) {
        // If main battery is in fault state, disengage the second battery
        datalayer.system.status.battery3_allowed_contactor_closing = false;
      } else {  // If main battery is OK, allow second battery to join
        datalayer.system.status.battery3_allowed_contactor_closing = true;
      }
    } else {  //Voltage between the two packs is too large
      set_event(EVENT_VOLTAGE_DIFFERENCE, (uint8_t)(voltage_diff_battery3_towards_main / 10));

      //If we start to drift out of sync between the two packs for more than 10 seconds, open contactors
      if (secondsOutOfVoltageSyncBattery3 < 10) {
        secondsOutOfVoltageSyncBattery3++;
      } else {
        datalayer.system.status.battery3_allowed_contactor_closing = false;
      }
    }
  }
}
