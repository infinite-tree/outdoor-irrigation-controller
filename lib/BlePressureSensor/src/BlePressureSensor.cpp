#include "BlePressureSensor.h"

#include <NimBLEDevice.h>
#include <string>

#define BLE_BATTERY_SERVICE_UUID ((uint16_t)0x180F)
#define BLE_BATTERY_LEVEL_CHAR_UUID ((uint16_t)0x2A19)

BlePressureSensor::BlePressureSensor() : initialized_(false), client_(nullptr) {}

bool BlePressureSensor::enabled() const {
  return config_.device_id != nullptr && config_.device_id[0] != '\0' &&
         config_.service_uuid != nullptr && config_.service_uuid[0] != '\0' &&
         config_.characteristic_uuid != nullptr &&
         config_.characteristic_uuid[0] != '\0';
}

bool BlePressureSensor::isInitialized() const { return initialized_; }

float BlePressureSensor::rawToPsi(uint16_t raw, const BlePressureConfig &config) {
  if (raw < config.raw_floor) {
    return 0.0f;
  }

  const int32_t raw_span = config.raw_ref_high - config.raw_ref_low;
  if (raw_span <= 0) {
    return 0.0f;
  }

  const float psi_span = config.psi_ref_high - config.psi_ref_low;
  const float psi = config.psi_ref_low +
                    ((float)((int32_t)raw - config.raw_ref_low) * psi_span) /
                        (float)raw_span;
  return psi < 0.0f ? 0.0f : psi;
}

BlePressureReading BlePressureSensor::makeError(const char *message) {
  return {false, 0, 0.0f, -1, false, message};
}

void BlePressureSensor::begin(const BlePressureConfig &config) {
  config_ = config;
  if (!enabled() || initialized_) {
    return;
  }

  NimBLEDevice::init("");
  client_ = NimBLEDevice::createClient();
  static_cast<NimBLEClient *>(client_)->setConnectTimeout(config_.connect_timeout_ms);
  initialized_ = true;
}

bool BlePressureSensor::connect() {
  if (!initialized_ || client_ == nullptr) {
    return false;
  }

  NimBLEClient *client = static_cast<NimBLEClient *>(client_);
  if (client->isConnected()) {
    return true;
  }

  NimBLEAddress address(std::string(config_.device_id), config_.address_type);
  return client->connect(address);
}

bool BlePressureSensor::readBatteryPercent(int *battery_pct) {
  NimBLEClient *client = static_cast<NimBLEClient *>(client_);
  NimBLERemoteService *service =
      client->getService(NimBLEUUID(BLE_BATTERY_SERVICE_UUID));
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

BlePressureReading BlePressureSensor::read() {
  if (!enabled()) {
    return makeError("BLE pressure sensor not configured");
  }

  if (!initialized_) {
    begin(config_);
  }

  if (!connect()) {
    return makeError("BLE connect failed");
  }

  NimBLEClient *client = static_cast<NimBLEClient *>(client_);
  NimBLERemoteService *service =
      client->getService(NimBLEUUID(config_.service_uuid));
  if (service == nullptr) {
    client->disconnect();
    return makeError("BLE service not found");
  }

  NimBLERemoteCharacteristic *pressure_char =
      service->getCharacteristic(NimBLEUUID(config_.characteristic_uuid));
  if (pressure_char == nullptr || !pressure_char->canRead()) {
    client->disconnect();
    return makeError("BLE pressure characteristic not found");
  }

  std::string value = pressure_char->readValue();
  if (value.size() < 2) {
    client->disconnect();
    return makeError("BLE pressure value too short");
  }

  const uint16_t raw = (uint8_t)value[0] | ((uint8_t)value[1] << 8);

  BlePressureReading reading;
  reading.ok = true;
  reading.raw = raw;
  reading.psi = rawToPsi(raw, config_);
  reading.battery_pct = -1;
  reading.battery_valid = readBatteryPercent(&reading.battery_pct);
  reading.error = nullptr;

  client->disconnect();
  return reading;
}
