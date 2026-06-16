#include "ble_pressure.h"
#include "Config.h"

#include <NimBLEDevice.h>
#include <string>

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

#define BLE_CONNECT_TIMEOUT_MS 8000
#define BLE_READ_TIMEOUT_MS    5000

// Bluetooth SIG Battery Service (0x180F) and Battery Level characteristic (0x2A19).
#define BLE_BATTERY_SERVICE_UUID      ((uint16_t)0x180F)
#define BLE_BATTERY_LEVEL_CHAR_UUID   ((uint16_t)0x2A19)

static bool ble_initialized = false;
static NimBLEClient *ble_client = nullptr;

static bool ble_configured() {
  return BLE_PRESSURE_DEVICE_ID[0] != '\0' &&
         BLE_PRESSURE_SERVICE_UUID[0] != '\0' &&
         BLE_PRESSURE_CHAR_UUID[0] != '\0';
}

bool ble_pressure_enabled() {
  return ble_configured();
}

static float raw_to_psi(uint16_t raw) {
  if (raw < BLE_PRESSURE_RAW_FLOOR) {
    return 0.0f;
  }

  const int32_t raw_span = BLE_PRESSURE_RAW_REF_HIGH - BLE_PRESSURE_RAW_REF_LOW;
  if (raw_span <= 0) {
    return 0.0f;
  }

  const float psi_span = BLE_PRESSURE_PSI_REF_HIGH - BLE_PRESSURE_PSI_REF_LOW;
  const float psi = BLE_PRESSURE_PSI_REF_LOW +
                    ((float)((int32_t)raw - BLE_PRESSURE_RAW_REF_LOW) * psi_span) /
                        (float)raw_span;
  return psi < 0.0f ? 0.0f : psi;
}

static BlePressureReading make_error(const char *message) {
  return {false, 0, 0.0f, -1, false, message};
}

void ble_pressure_init() {
  if (!ble_configured() || ble_initialized) {
    return;
  }

  NimBLEDevice::init("");
  ble_client = NimBLEDevice::createClient();
  ble_client->setConnectTimeout(BLE_CONNECT_TIMEOUT_MS);
  ble_initialized = true;
}

static bool connect_to_sensor() {
  if (!ble_initialized || ble_client == nullptr) {
    return false;
  }

  if (ble_client->isConnected()) {
    return true;
  }

  NimBLEAddress address(std::string(BLE_PRESSURE_DEVICE_ID), BLE_ADDR_PUBLIC);
  return ble_client->connect(address);
}

static bool read_battery_percent(int *battery_pct) {
  NimBLERemoteService *service =
      ble_client->getService(NimBLEUUID(BLE_BATTERY_SERVICE_UUID));
  if (service == nullptr) {
    return false;
  }

  NimBLERemoteCharacteristic *battery_char =
      service->getCharacteristic(NimBLEUUID(BLE_BATTERY_LEVEL_CHAR_UUID));
  if (battery_char == nullptr || !battery_char->canRead()) {
    return false;
  }

  std::string value = battery_char->readValue();
  if (value.empty()) {
    return false;
  }

  *battery_pct = (uint8_t)value[0];
  if (*battery_pct > 100) {
    *battery_pct = 100;
  }
  return true;
}

BlePressureReading ble_pressure_read() {
  if (!ble_configured()) {
    return make_error("BLE pressure sensor not configured");
  }

  if (!ble_initialized) {
    ble_pressure_init();
  }

  if (!connect_to_sensor()) {
    return make_error("BLE connect failed");
  }

  NimBLERemoteService *service =
      ble_client->getService(NimBLEUUID(BLE_PRESSURE_SERVICE_UUID));
  if (service == nullptr) {
    ble_client->disconnect();
    return make_error("BLE service not found");
  }

  NimBLERemoteCharacteristic *pressure_char =
      service->getCharacteristic(NimBLEUUID(BLE_PRESSURE_CHAR_UUID));
  if (pressure_char == nullptr || !pressure_char->canRead()) {
    ble_client->disconnect();
    return make_error("BLE pressure characteristic not found");
  }

  std::string value = pressure_char->readValue();
  if (value.size() < 2) {
    ble_client->disconnect();
    return make_error("BLE pressure value too short");
  }

  const uint16_t raw = (uint8_t)value[0] | ((uint8_t)value[1] << 8);

  BlePressureReading reading;
  reading.ok = true;
  reading.raw = raw;
  reading.psi = raw_to_psi(raw);
  reading.battery_pct = -1;
  reading.battery_valid = read_battery_percent(&reading.battery_pct);
  reading.error = nullptr;

  ble_client->disconnect();
  return reading;
}
