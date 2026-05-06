#ifndef TESLA_SX_MOD_BATTERY_H
#define TESLA_SX_MOD_BATTERY_H

#include "TESLA-BATTERY.h"

// Replays the original 2024-10 Tesla S/X TX behavior:
// only 0x1CF (10ms), 0x118 (10ms), 0x221 static pair (30ms),
// gated solely by inverter_allows_contactor_closing.
class TeslaModelSXModBattery : public TeslaBattery {
 public:
  TeslaModelSXModBattery() : TeslaBattery() {}
  TeslaModelSXModBattery(DATALAYER_BATTERY_TYPE* dl, DATALAYER_INFO_TESLA* extended, CAN_Interface targetCan)
      : TeslaBattery(dl, extended, targetCan) {}

  static constexpr const char* Name = "Tesla Model S/X Mod";
  virtual void setup(void);
  virtual void transmit_can(unsigned long currentMillis);

  // UDS would inject 0x602/0x610 sequences that did not exist in the
  // referenced 2024-10 behavior; disable to keep the experiment clean.
  bool supports_clear_isolation() { return false; }
  bool supports_reset_BMS() { return false; }
  bool supports_reset_SOC() { return false; }

 private:
  CAN_frame TESLA_221_STATIC_1 = {.FD = false,
                                  .ext_ID = false,
                                  .DLC = 8,
                                  .ID = 0x221,
                                  .data = {0x41, 0x11, 0x01, 0x00, 0x00, 0x00, 0x20, 0x96}};

  CAN_frame TESLA_221_STATIC_2 = {.FD = false,
                                  .ext_ID = false,
                                  .DLC = 8,
                                  .ID = 0x221,
                                  .data = {0x61, 0x15, 0x01, 0x00, 0x00, 0x00, 0x20, 0xBA}};

  unsigned long previousMillis30 = 0;
};

#endif
