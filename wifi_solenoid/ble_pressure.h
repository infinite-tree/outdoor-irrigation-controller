#pragma once

#include <BlePressureSensor.h>

bool ble_pressure_enabled();
void ble_pressure_init();
void ble_pressure_refresh_if_stale(unsigned long max_age_ms, bool force = false);
BlePressureReading ble_pressure_get_cached();
bool ble_pressure_has_cache();
