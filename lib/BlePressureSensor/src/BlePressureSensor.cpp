#include "BlePressureSensor.h"

#include <NimBLEDevice.h>
#include <math.h>
#include <string>

#if defined(ESP_PLATFORM)
#include "esp_coexist.h"
#endif

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
  if (config.scale_tenths) {
    const int16_t raw_signed = (int16_t)raw;
    float psi = (float)raw_signed / config.raw_tenths_divisor;
    if (config.psi_max > 0.0f && psi > config.psi_max) {
      psi = config.psi_max;
    }
    if (psi < config.psi_min) {
      psi = config.psi_min;
    }
    return (float)lroundf(psi);
  }

  if (config.raw_zero_sentinel != 0 && raw == config.raw_zero_sentinel) {
    return 0.0f;
  }

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
  if (psi < 0.0f) {
    return 0.0f;
  }
  return (float)lroundf(psi);
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

void BlePressureSensor::resetStack() {
  Serial.println("BLE pressure stack reset");

  if (client_ != nullptr) {
    NimBLEClient *client = static_cast<NimBLEClient *>(client_);
    if (client->isConnected()) {
      client->disconnect();
      delay(150);
    }
    NimBLEDevice::deleteClient(client);
    client_ = nullptr;
  }

  if (initialized_) {
    NimBLEDevice::deinit(true);
    initialized_ = false;
  }

  delay(500);
  begin(config_);
}

void BlePressureSensor::ensureDisconnected() {
  if (client_ == nullptr) {
    return;
  }

  NimBLEClient *client = static_cast<NimBLEClient *>(client_);
  if (client->isConnected()) {
    client->disconnect();
    delay(100);
  }
}

static void preferBleRadio() {
#if defined(ESP_PLATFORM)
  esp_coex_preference_set(ESP_COEX_PREFER_BT);
#endif
}

bool BlePressureSensor::connectByAddress(uint8_t address_type) {
  NimBLEClient *client = static_cast<NimBLEClient *>(client_);
  NimBLEAddress address(std::string(config_.device_id), address_type);
  Serial.print("BLE connecting to ");
  Serial.print(config_.device_id);
  Serial.print(" (type ");
  Serial.print(address_type);
  Serial.println(")...");

  preferBleRadio();
  const bool connected = client->connect(address);
  if (!connected) {
    Serial.println("BLE address connect failed");
    ensureDisconnected();
  }
  return connected;
}

bool BlePressureSensor::connect() {
  if (!initialized_ || client_ == nullptr) {
    return false;
  }

  ensureDisconnected();

  if (connectByAddress(config_.address_type)) {
    return true;
  }
  if (config_.address_type != BLE_ADDR_RANDOM &&
      connectByAddress(BLE_ADDR_RANDOM)) {
    return true;
  }
  return false;
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

BlePressureReading BlePressureSensor::readOnce() {
  if (!connect()) {
    return makeError("BLE connect failed");
  }

  NimBLEClient *client = static_cast<NimBLEClient *>(client_);
  NimBLERemoteService *service =
      client->getService(NimBLEUUID(config_.service_uuid));
  if (service == nullptr) {
    ensureDisconnected();
    return makeError("BLE service not found");
  }

  NimBLERemoteCharacteristic *pressure_char =
      service->getCharacteristic(NimBLEUUID(config_.characteristic_uuid));
  if (pressure_char == nullptr || !pressure_char->canRead()) {
    ensureDisconnected();
    return makeError("BLE pressure characteristic not found");
  }

  std::string value = pressure_char->readValue();
  if (value.size() < 2) {
    ensureDisconnected();
    return makeError("BLE pressure value too short");
  }

  const uint16_t raw =
      config_.big_endian
          ? (uint16_t)(((uint16_t)(uint8_t)value[0] << 8) | (uint8_t)value[1])
          : (uint16_t)((uint8_t)value[0] | ((uint16_t)(uint8_t)value[1] << 8));
  const int16_t raw_signed = (int16_t)raw;

  BlePressureReading reading;
  reading.ok = true;
  reading.raw = raw;
  reading.psi = rawToPsi(raw, config_);
  reading.battery_pct = -1;
  reading.battery_valid = readBatteryPercent(&reading.battery_pct);
  reading.error = nullptr;

  Serial.print("BLE pressure bytes");
  for (size_t i = 0; i < value.size() && i < 8; i++) {
    Serial.printf(" %02X", (uint8_t)value[i]);
  }
  Serial.printf(" -> raw=0x%04X (%d) psi=%d\n", raw, (int)raw_signed,
                (int)reading.psi);

  ensureDisconnected();
  return reading;
}

BlePressureReading BlePressureSensor::read() {
  if (!enabled()) {
    return makeError("BLE pressure sensor not configured");
  }

  if (!initialized_) {
    begin(config_);
  }

  const uint8_t max_retries = config_.max_retries < 1 ? 1 : config_.max_retries;
  BlePressureReading last_error = makeError("BLE read failed");

  for (uint8_t attempt = 0; attempt < max_retries; attempt++) {
    if (attempt > 0) {
      Serial.print("BLE pressure attempt ");
      Serial.print(attempt + 1);
      Serial.print("/");
      Serial.println(max_retries);
      ensureDisconnected();
      delay(config_.retry_delay_ms);
    }

    BlePressureReading result = readOnce();
    if (result.ok) {
      if (attempt > 0) {
        Serial.println("BLE pressure read recovered");
      }
      return result;
    }

    last_error = result;
    if (result.error != nullptr) {
      Serial.print("BLE pressure read failed: ");
      Serial.println(result.error);
    }
    ensureDisconnected();
  }

  return last_error;
}
