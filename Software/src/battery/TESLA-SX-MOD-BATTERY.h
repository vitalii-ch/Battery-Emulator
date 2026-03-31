#ifndef TESLA_SX_MOD_BATTERY_H
#define TESLA_SX_MOD_BATTERY_H

#include "TESLA-BATTERY.h"

class TeslaModelSXModBattery : public TeslaBattery {
 public:
  static constexpr const char* Name = "Tesla Model S/X Mod";
  virtual void setup(void);
  virtual void transmit_can(unsigned long currentMillis);

 private:
  bool uds_active();
  // When true, suppress all CAN TX while vehicleState is still OFF after power-on.
  // Once vehicleState leaves OFF, the block is lifted permanently.
  bool suppress_can_until_drive = true;
  bool startup_can_blocked = true;

  // When true, only send the 3 original S/X frames (0x221, 0x1CF, 0x118)
  // instead of the full 25+ frame set from the base class.
  bool minimal_can_frames = true;

  // When true, use old static 0x221 format (two fixed frames at 30ms)
  // instead of dynamic muxed frames with counter+checksum at 50ms.
  bool use_static_221 = true;

  // Old-style static contactor frames from tesla_sx_support branch
  CAN_frame TESLA_221_STATIC_1 = {.FD = false,
                                  .ext_ID = false,
                                  .DLC = 8,
                                  .ID = 0x221,
                                  .data = {0x41, 0x11, 0x01, 0x00, 0x00, 0x00, 0x20, 0x96}};  // Close contactors

  CAN_frame TESLA_221_STATIC_2 = {.FD = false,
                                  .ext_ID = false,
                                  .DLC = 8,
                                  .ID = 0x221,
                                  .data = {0x61, 0x15, 0x01, 0x00, 0x00, 0x00, 0x20, 0xBA}};  // HV up for drive

  unsigned long previousMillis30 = 0;
};

#endif