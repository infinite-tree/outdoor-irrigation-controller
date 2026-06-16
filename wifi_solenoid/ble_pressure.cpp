#include "ble_pressure.h"

#include "Config.h"

#ifndef BLE_PRESSURE_DEVICE_ID
#define BLE_PRESSURE_DEVICE_ID ""
#endif
#ifndef BLE_PRESSURE_SERVICE_UUID
#define BLE_PRESSURE_SERVICE_UUID ""
#endif
#ifndef BLE_PRESSURE_CHAR_UUID
#define BLE_PRESSURE_CHAR_UUID ""
#endif
#ifndef BLE_PRESSURE_RAW_FLOOR
#define BLE_PRESSURE_RAW_FLOOR 2000
#endif
#ifndef BLE_PRESSURE_RAW_REF_LOW
#define BLE_PRESSURE_RAW_REF_LOW 19000
#endif
#ifndef BLE_PRESSURE_PSI_REF_LOW
#define BLE_PRESSURE_PSI_REF_LOW 34.0f
#endif
#ifndef BLE_PRESSURE_RAW_REF_HIGH
#define BLE_PRESSURE_RAW_REF_HIGH 26000
#endif
#ifndef BLE_PRESSURE_PSI_REF_HIGH
#define BLE_PRESSURE_PSI_REF_HIGH 37.0f
#endif
#ifndef BLE_PRESSURE_REFRESH_MS
#define BLE_PRESSURE_REFRESH_MS 600000
#endif

static BlePressureSensor ble_pressure_sensor;
static BlePressureReading cached_reading = {false, 0, 0.0f, -1, false, "not read yet"};
static unsigned long last_ble_read = 0;
static bool cache_valid = false;

static BlePressureConfig ble_pressure_config_from_build() {
  BlePressureConfig config;
  config.device_id = BLE_PRESSURE_DEVICE_ID;
  config.service_uuid = BLE_PRESSURE_SERVICE_UUID;
  config.characteristic_uuid = BLE_PRESSURE_CHAR_UUID;
  config.raw_floor = BLE_PRESSURE_RAW_FLOOR;
  config.raw_ref_low = BLE_PRESSURE_RAW_REF_LOW;
  config.psi_ref_low = BLE_PRESSURE_PSI_REF_LOW;
  config.raw_ref_high = BLE_PRESSURE_RAW_REF_HIGH;
  config.psi_ref_high = BLE_PRESSURE_PSI_REF_HIGH;
  return config;
}

bool ble_pressure_enabled() {
  return BLE_PRESSURE_DEVICE_ID[0] != '\0' &&
         BLE_PRESSURE_SERVICE_UUID[0] != '\0' &&
         BLE_PRESSURE_CHAR_UUID[0] != '\0';
}

void ble_pressure_init() {
  ble_pressure_sensor.begin(ble_pressure_config_from_build());
}

bool ble_pressure_refresh_if_stale(unsigned long max_age_ms, bool force) {
  if (!ble_pressure_enabled()) {
    return false;
  }

  const unsigned long now = millis();
  if (!force && cache_valid && (now - last_ble_read) < max_age_ms) {
    return false;
  }

  cached_reading = ble_pressure_sensor.read();
  last_ble_read = now;
  cache_valid = true;
  if (!cached_reading.ok && cached_reading.error != nullptr) {
    Serial.print("BLE pressure read failed: ");
    Serial.println(cached_reading.error);
  }
  return true;
}

BlePressureReading ble_pressure_get_cached() {
  return cached_reading;
}

bool ble_pressure_has_cache() {
  return cache_valid;
}
