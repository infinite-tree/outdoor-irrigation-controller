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

static BlePressureSensor ble_pressure_sensor;

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

BlePressureReading ble_pressure_read() {
  return ble_pressure_sensor.read();
}
