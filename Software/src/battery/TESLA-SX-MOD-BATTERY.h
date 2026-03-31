#ifndef TESLA_SX_MOD_BATTERY_H
#define TESLA_SX_MOD_BATTERY_H

#include "TESLA-BATTERY.h"

class TeslaModelSXModBattery : public TeslaBattery {
 public:
  static constexpr const char* Name = "Tesla Model S/X Mod";
  virtual void setup(void);
};

#endif