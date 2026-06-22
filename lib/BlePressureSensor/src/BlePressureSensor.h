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
  uint8_t max_retries;
  uint32_t retry_delay_ms;
  uint16_t raw_zero_sentinel;
  bool scale_tenths;
  bool big_endian;
  float raw_tenths_divisor;
  float psi_max;
  float psi_min;

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
        address_type(0),
        max_retries(3),
        retry_delay_ms(2000),
        raw_zero_sentinel(0xFFFF),
        scale_tenths(true),
        big_endian(true),
        raw_tenths_divisor(10.0f),
        psi_max(250.0f),
        psi_min(-20.0f) {}
};

class BlePressureSensor {
 public:
  BlePressureSensor();
  void begin(const BlePressureConfig &config);
  bool enabled() const;
  bool isInitialized() const;
  BlePressureReading read();
  void resetStack();

  static float rawToPsi(uint16_t raw, const BlePressureConfig &config);

 private:
  BlePressureConfig config_;
  bool initialized_;
  void *client_;

  void ensureDisconnected();
  bool connect();
  bool connectByAddress(uint8_t address_type);
  BlePressureReading readOnce();
  bool readBatteryPercent(int *battery_pct);
  static BlePressureReading makeError(const char *message);
};
