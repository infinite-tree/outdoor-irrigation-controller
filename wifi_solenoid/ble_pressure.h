#pragma once

#include <BlePressureSensor.h>
#include <time.h>

bool ble_pressure_enabled();
void ble_pressure_init();
void ble_pressure_start_task();
bool ble_pressure_take_display_dirty();
BlePressureReading ble_pressure_get_cached();
bool ble_pressure_has_cache();
bool ble_pressure_is_fresh();
bool ble_pressure_is_stale();
const char *ble_pressure_last_error();
unsigned long ble_pressure_last_success_age_sec();
time_t ble_pressure_last_success_epoch();
