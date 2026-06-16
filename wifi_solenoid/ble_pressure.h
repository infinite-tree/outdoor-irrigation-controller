#pragma once

#include <Arduino.h>

struct BlePressureReading {
  bool ok;
  uint16_t raw;
  float psi;
  int battery_pct;
  bool battery_valid;
  const char *error;
};

bool ble_pressure_enabled();
void ble_pressure_init();
BlePressureReading ble_pressure_read();
