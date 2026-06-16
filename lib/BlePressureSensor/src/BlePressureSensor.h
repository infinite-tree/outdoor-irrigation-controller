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

struct BlePressureConfig {
  const char *device_id;
  const char *service_uuid;
  const char *characteristic_uuid;
  uint16_t raw_floor;
  int32_t raw_ref_low;
  float psi_ref_low;
  int32_t raw_ref_high;
  float psi_ref_high;
  uint32_t connect_timeout_ms;
  uint8_t address_type;

  BlePressureConfig()
      : device_id(nullptr),
        service_uuid(nullptr),
        characteristic_uuid(nullptr),
        raw_floor(2000),
        raw_ref_low(19000),
        psi_ref_low(34.0f),
        raw_ref_high(26000),
        psi_ref_high(37.0f),
        connect_timeout_ms(8000),
        address_type(0) {}
};

class BlePressureSensor {
 public:
  BlePressureSensor();
  void begin(const BlePressureConfig &config);
  bool enabled() const;
  bool isInitialized() const;
  BlePressureReading read();

  static float rawToPsi(uint16_t raw, const BlePressureConfig &config);

 private:
  BlePressureConfig config_;
  bool initialized_;
  void *client_;

  bool connect();
  bool readBatteryPercent(int *battery_pct);
  static BlePressureReading makeError(const char *message);
};
