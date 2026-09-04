#include "ble_pressure.h"

#include "Config.h"

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <math.h>
#include <time.h>

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
#ifndef BLE_PRESSURE_CONNECT_TIMEOUT_MS
#define BLE_PRESSURE_CONNECT_TIMEOUT_MS 8000
#endif
#ifndef BLE_PRESSURE_MAX_RETRIES
#define BLE_PRESSURE_MAX_RETRIES 3
#endif
#ifndef BLE_PRESSURE_RETRY_DELAY_MS
#define BLE_PRESSURE_RETRY_DELAY_MS 2000
#endif
#ifndef BLE_PRESSURE_STACK_RESET_FAILURES
#define BLE_PRESSURE_STACK_RESET_FAILURES 3
#endif
#ifndef BLE_PRESSURE_FAIL_RETRY_MS
#define BLE_PRESSURE_FAIL_RETRY_MS 60000
#endif
#ifndef BLE_PRESSURE_RAW_ZERO_SENTINEL
#define BLE_PRESSURE_RAW_ZERO_SENTINEL 0xFFFF
#endif
#ifndef BLE_PRESSURE_SCALE_TENTHS
#define BLE_PRESSURE_SCALE_TENTHS 1
#endif
#ifndef BLE_PRESSURE_BIG_ENDIAN
#define BLE_PRESSURE_BIG_ENDIAN 1
#endif
#ifndef BLE_PRESSURE_TENTHS_DIVISOR
#define BLE_PRESSURE_TENTHS_DIVISOR 10.0f
#endif
#ifndef BLE_PRESSURE_MAX_PSI
#define BLE_PRESSURE_MAX_PSI 250.0f
#endif
#ifndef BLE_PRESSURE_MIN_PSI
#define BLE_PRESSURE_MIN_PSI -250.0f
#endif
#ifndef BLE_PRESSURE_OFFSET_PSI
#define BLE_PRESSURE_OFFSET_PSI 0.0f
#endif
#ifndef BLE_PRESSURE_OFFSET_MAX_PSI
#define BLE_PRESSURE_OFFSET_MAX_PSI 250.0f
#endif
#ifndef BLE_PRESSURE_TASK_STACK
#define BLE_PRESSURE_TASK_STACK 12288
#endif
#ifndef BLE_PRESSURE_TASK_POLL_MS
#define BLE_PRESSURE_TASK_POLL_MS 1000
#endif

static BlePressureSensor ble_pressure_sensor;
static SemaphoreHandle_t cache_mutex = nullptr;
static volatile bool display_dirty = false;

static BlePressureReading published_reading = {
    false, 0, 0.0f, 0.0f, -1, false, nullptr};
static float pressure_offset_psi = BLE_PRESSURE_OFFSET_PSI;
static bool published_has_cache = false;
static bool published_read_fresh = false;
static char published_error_msg[64] = "not read yet";
static unsigned long published_success_millis = 0;
static time_t published_success_epoch = 0;

static unsigned long last_ble_read = 0;
static uint8_t consecutive_failures = 0;

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
  config.connect_timeout_ms = BLE_PRESSURE_CONNECT_TIMEOUT_MS;
  config.max_retries = BLE_PRESSURE_MAX_RETRIES;
  config.retry_delay_ms = BLE_PRESSURE_RETRY_DELAY_MS;
  config.raw_zero_sentinel = BLE_PRESSURE_RAW_ZERO_SENTINEL;
  config.scale_tenths = BLE_PRESSURE_SCALE_TENTHS != 0;
  config.big_endian = BLE_PRESSURE_BIG_ENDIAN != 0;
  config.raw_tenths_divisor = BLE_PRESSURE_TENTHS_DIVISOR;
  config.psi_max = BLE_PRESSURE_MAX_PSI;
  config.psi_min = BLE_PRESSURE_MIN_PSI;
  return config;
}

static void publish_cache(const BlePressureReading &reading, bool read_fresh,
                          bool has_cache, unsigned long success_millis,
                          time_t success_epoch, const char *error_msg) {
  if (cache_mutex == nullptr) {
    return;
  }
  xSemaphoreTake(cache_mutex, portMAX_DELAY);
  published_reading = reading;
  published_reading.error = nullptr;
  published_has_cache = has_cache;
  published_read_fresh = read_fresh;
  published_success_millis = success_millis;
  published_success_epoch = success_epoch;
  if (error_msg == nullptr || error_msg[0] == '\0') {
    published_error_msg[0] = '\0';
  } else {
    strncpy(published_error_msg, error_msg, sizeof(published_error_msg) - 1);
    published_error_msg[sizeof(published_error_msg) - 1] = '\0';
  }
  xSemaphoreGive(cache_mutex);
}

static bool refresh_if_stale(unsigned long max_age_ms) {
  if (!ble_pressure_enabled()) {
    return false;
  }

  const unsigned long now = millis();
  if (last_ble_read != 0) {
    const unsigned long age = now - last_ble_read;
    bool read_fresh;
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    read_fresh = published_read_fresh;
    xSemaphoreGive(cache_mutex);

    if (read_fresh && age < max_age_ms) {
      return false;
    }
    if (!read_fresh && age < BLE_PRESSURE_FAIL_RETRY_MS) {
      return false;
    }
  }

  BlePressureReading reading = ble_pressure_sensor.read();
  last_ble_read = now;

  bool has_cache = false;
  unsigned long success_millis = 0;
  time_t success_epoch = 0;
  if (cache_mutex != nullptr) {
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    has_cache = published_has_cache;
    success_millis = published_success_millis;
    success_epoch = published_success_epoch;
    xSemaphoreGive(cache_mutex);
  }

  if (reading.ok) {
    reading.psi = (float)lroundf(reading.sensor_psi + pressure_offset_psi);
    success_millis = now;
    const time_t now_epoch = time(nullptr);
    if (now_epoch > 100000) {
      success_epoch = now_epoch;
    }
    consecutive_failures = 0;
    publish_cache(reading, true, true, success_millis, success_epoch, nullptr);
    if (pressure_offset_psi != 0.0f) {
      Serial.printf("BLE pressure applied offset=%.1f -> psi=%d\n",
                    pressure_offset_psi, (int)reading.psi);
    }
    return true;
  }

  const char *error_msg = reading.error;
  if (error_msg != nullptr) {
    Serial.print("BLE pressure refresh failed: ");
    Serial.println(error_msg);
  }

  BlePressureReading cached = reading;
  if (has_cache && cache_mutex != nullptr) {
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    cached = published_reading;
    xSemaphoreGive(cache_mutex);
  }
  publish_cache(cached, false, has_cache, success_millis, success_epoch,
                error_msg);

  consecutive_failures++;
  if (consecutive_failures >= BLE_PRESSURE_STACK_RESET_FAILURES) {
    Serial.print("BLE pressure ");
    Serial.print(consecutive_failures);
    Serial.println(" consecutive failures; resetting BLE stack");
    ble_pressure_sensor.resetStack();
    consecutive_failures = 0;

    BlePressureReading cached_after_reset = reading;
    reading = ble_pressure_sensor.read();
    if (reading.ok) {
      success_millis = now;
      const time_t now_epoch = time(nullptr);
      if (now_epoch > 100000) {
        success_epoch = now_epoch;
      }
      reading.psi = (float)lroundf(reading.sensor_psi + pressure_offset_psi);
      publish_cache(reading, true, true, success_millis, success_epoch, nullptr);
      Serial.println("BLE pressure read recovered after stack reset");
      return true;
    }
    error_msg = reading.error;
    if (has_cache && cache_mutex != nullptr) {
      xSemaphoreTake(cache_mutex, portMAX_DELAY);
      cached_after_reset = published_reading;
      xSemaphoreGive(cache_mutex);
    } else {
      cached_after_reset = reading;
    }
    publish_cache(cached_after_reset, false, has_cache, success_millis,
                  success_epoch, error_msg);
  }

  return true;
}

static void ble_pressure_task(void *param) {
  (void)param;
  for (;;) {
    if (refresh_if_stale(BLE_PRESSURE_REFRESH_MS)) {
      display_dirty = true;
    }
    vTaskDelay(pdMS_TO_TICKS(BLE_PRESSURE_TASK_POLL_MS));
  }
}

bool ble_pressure_enabled() {
  return BLE_PRESSURE_DEVICE_ID[0] != '\0' &&
         BLE_PRESSURE_SERVICE_UUID[0] != '\0' &&
         BLE_PRESSURE_CHAR_UUID[0] != '\0';
}

static void persist_pressure_offset(float offset) {
  Preferences prefs;
  if (!prefs.begin("presscfg", false)) {
    Serial.println("BLE pressure offset persist failed");
    return;
  }
  prefs.putFloat("offset", offset);
  prefs.end();
}

static void load_pressure_offset() {
  Preferences prefs;
  if (!prefs.begin("presscfg", true)) {
    pressure_offset_psi = BLE_PRESSURE_OFFSET_PSI;
    return;
  }
  pressure_offset_psi = prefs.getFloat("offset", BLE_PRESSURE_OFFSET_PSI);
  prefs.end();
}

static void apply_offset_to_published_cache() {
  if (cache_mutex == nullptr) {
    return;
  }
  xSemaphoreTake(cache_mutex, portMAX_DELAY);
  if (published_has_cache) {
    published_reading.psi =
        (float)lroundf(published_reading.sensor_psi + pressure_offset_psi);
  }
  xSemaphoreGive(cache_mutex);
  display_dirty = true;
}

void ble_pressure_init() {
  load_pressure_offset();
  ble_pressure_sensor.begin(ble_pressure_config_from_build());
  if (!ble_pressure_enabled()) {
    return;
  }
  Serial.print("BLE pressure config: tenths=");
  Serial.print(BLE_PRESSURE_SCALE_TENTHS != 0 ? "yes" : "no");
  Serial.print(" byte_order=");
  Serial.print(BLE_PRESSURE_BIG_ENDIAN != 0 ? "big-endian" : "little-endian");
  Serial.print(" max=");
  Serial.print((int)BLE_PRESSURE_MAX_PSI);
  Serial.print(" offset=");
  Serial.println(pressure_offset_psi, 1);
}

void ble_pressure_start_task() {
  if (!ble_pressure_enabled() || cache_mutex != nullptr) {
    return;
  }

  cache_mutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(ble_pressure_task, "ble_pressure",
                          BLE_PRESSURE_TASK_STACK, nullptr, 1, nullptr, 0);
}

bool ble_pressure_take_display_dirty() {
  if (!display_dirty) {
    return false;
  }
  display_dirty = false;
  return true;
}

BlePressureReading ble_pressure_get_cached() {
  BlePressureReading reading = published_reading;
  if (cache_mutex != nullptr) {
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    reading = published_reading;
    xSemaphoreGive(cache_mutex);
  }
  return reading;
}

bool ble_pressure_has_cache() {
  bool has_cache = published_has_cache;
  if (cache_mutex != nullptr) {
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    has_cache = published_has_cache;
    xSemaphoreGive(cache_mutex);
  }
  return has_cache;
}

bool ble_pressure_is_fresh() {
  bool fresh = published_has_cache && published_read_fresh;
  if (cache_mutex != nullptr) {
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    fresh = published_has_cache && published_read_fresh;
    xSemaphoreGive(cache_mutex);
  }
  return fresh;
}

bool ble_pressure_is_stale() {
  bool stale = published_has_cache && !published_read_fresh;
  if (cache_mutex != nullptr) {
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    stale = published_has_cache && !published_read_fresh;
    xSemaphoreGive(cache_mutex);
  }
  return stale;
}

const char *ble_pressure_last_error() {
  if (cache_mutex != nullptr) {
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    const char *error =
        published_error_msg[0] != '\0' ? published_error_msg : nullptr;
    xSemaphoreGive(cache_mutex);
    return error;
  }
  if (published_error_msg[0] == '\0') {
    return nullptr;
  }
  return published_error_msg;
}

unsigned long ble_pressure_last_success_age_sec() {
  unsigned long success_millis = published_success_millis;
  bool has_cache = published_has_cache;
  if (cache_mutex != nullptr) {
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    success_millis = published_success_millis;
    has_cache = published_has_cache;
    xSemaphoreGive(cache_mutex);
  }
  if (!has_cache || success_millis == 0) {
    return ULONG_MAX;
  }
  return (millis() - success_millis) / 1000UL;
}

time_t ble_pressure_last_success_epoch() {
  time_t epoch = published_success_epoch;
  if (cache_mutex != nullptr) {
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    epoch = published_success_epoch;
    xSemaphoreGive(cache_mutex);
  }
  return epoch;
}

float ble_pressure_offset_psi() { return pressure_offset_psi; }

bool ble_pressure_set_offset(float offset_psi, const char **error_out) {
  if (fabsf(offset_psi) > BLE_PRESSURE_OFFSET_MAX_PSI) {
    if (error_out != nullptr) {
      *error_out = "offset out of range";
    }
    return false;
  }

  pressure_offset_psi = offset_psi;
  persist_pressure_offset(pressure_offset_psi);
  apply_offset_to_published_cache();
  Serial.printf("BLE pressure offset set to %.1f psi\n", pressure_offset_psi);
  if (error_out != nullptr) {
    *error_out = nullptr;
  }
  return true;
}

bool ble_pressure_zero_to_current(const char **error_out) {
  if (!ble_pressure_enabled()) {
    if (error_out != nullptr) {
      *error_out = "BLE pressure sensor not configured";
    }
    return false;
  }
  if (!ble_pressure_has_cache()) {
    if (error_out != nullptr) {
      *error_out = "no pressure reading yet";
    }
    return false;
  }

  BlePressureReading reading = ble_pressure_get_cached();
  return ble_pressure_set_offset(-reading.sensor_psi, error_out);
}
