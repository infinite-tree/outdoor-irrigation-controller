#pragma once

#include <BlePressureSensor.h>

bool ble_pressure_enabled();
void ble_pressure_init();
BlePressureReading ble_pressure_read();
