#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

// DNS doesn't seem to work without these
#include "lwip/inet.h"
#include "lwip/dns.h"


#include <time.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <InfluxArduino.hpp>
#include <RootCert.h>


SPIClass SDSPI(HSPI);
#include "Adafruit_GFX.h"
#include <Fonts/FreeMonoBold9pt7b.h>
#include <GxEPD.h>
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
// 2.13" b/w  form DKE GROUP
#include <GxDEPG0213BN/GxDEPG0213BN.h>

#include "Logo.h"
#include "Config.h"
#include "station_shared.h"
#include "schedule.h"
#include "display_task.h"

#ifndef VFD_ALERT_URL
#define VFD_ALERT_URL ""
#endif
#ifndef VFD_ERROR_SUMMARY
#define VFD_ERROR_SUMMARY "Frenic Mini VFD fault"
#endif

#ifndef STATION_PRESSURE_POLL_INTERVAL_MS
#define STATION_PRESSURE_POLL_INTERVAL_MS 300000
#endif
#ifndef STATION_PRESSURE_POLL_ACTIVE_MS
#define STATION_PRESSURE_POLL_ACTIVE_MS 60000
#endif
#ifndef STATION_PRESSURE_FETCH_RETRY_MS
#define STATION_PRESSURE_FETCH_RETRY_MS 30000
#endif
#ifndef STATION_PRESSURE_STATUS_REFRESH_MS
#define STATION_PRESSURE_STATUS_REFRESH_MS 15000
#endif
#ifndef STATION_WATCHDOG_TIMEOUT_MS
#define STATION_WATCHDOG_TIMEOUT_MS 120000
#endif
#ifndef STATION_WIFI_RESTART_MS
#define STATION_WIFI_RESTART_MS 3600000
#endif
#ifndef PRESSURE_LOW_PSI
#define PRESSURE_LOW_PSI 20.0f
#endif
#ifndef PRESSURE_HIGH_PSI
#define PRESSURE_HIGH_PSI 45.0f
#endif
#ifndef PRESSURE_LOW_ALARM_DURATION_MS
#define PRESSURE_LOW_ALARM_DURATION_MS 600000
#endif
#ifndef PRESSURE_HIGH_ALARM_DURATION_MS
#define PRESSURE_HIGH_ALARM_DURATION_MS 600000
#endif
#ifndef PRESSURE_BATTERY_LOW_PCT
#define PRESSURE_BATTERY_LOW_PCT 20
#endif
#ifndef PRESSURE_LOW_SUMMARY
#define PRESSURE_LOW_SUMMARY "Solenoid pressure low"
#endif
#ifndef PRESSURE_HIGH_SUMMARY
#define PRESSURE_HIGH_SUMMARY "Solenoid pressure high"
#endif
#ifndef PRESSURE_BATTERY_SUMMARY
#define PRESSURE_BATTERY_SUMMARY "Solenoid pressure sensor battery low"
#endif
#ifndef VFD_ERROR_DEBOUNCE_MS
#define VFD_ERROR_DEBOUNCE_MS 2000
#endif
#ifndef VFD_ERROR_CLEAR_DEBOUNCE_MS
#define VFD_ERROR_CLEAR_DEBOUNCE_MS 5000
#endif
#ifndef VFD_ALERT_COOLDOWN_MS
#define VFD_ALERT_COOLDOWN_MS 1800000
#endif


#define EDP_BUSY_PIN            48
#define EDP_RSET_PIN            47
#define EDP_DC_PIN              16
#define EDP_CS_PIN              15
#define EDP_CLK_PIN             14 // CLK
#define EDP_MOSI_PIN            11 // MOSI
#define EDP_MISO_PIN            -1


#define BOARD_LED               37
#define LED_ON                  HIGH

#define BAT_ADC_PIN             1
#define BUTTON_PIN              0


#define PUMP_INPUT_PIN        45
#define PUMP_GND_PIN          41
#define HALF_PWR_OUTPUT_PIN   42
#define FULL_PWR_OUTPUT_PIN   46
// PC817 output from Frenic Mini fault contact (collector to GND when active)
#define VFD_ERROR_INPUT_PIN   40

#define RELAY_ON              HIGH
#define RELAY_OFF             LOW

#define REMOTE_PUMP_ON               LOW
#define REMOTE_PUMP_OFF              HIGH
#define REMOTE_SAMPLE_DELAY_MS       8
#define REMOTE_SAMPLE_SIZE           20

#define VFD_ERROR_ACTIVE             LOW
#define VFD_ERROR_SAMPLE_DELAY_MS    8
#define VFD_ERROR_SAMPLE_SIZE        20

// Time in seconds
#define MINUTES               60
#define HOURS                 3600

#define DISPLAY_UPDATE_MILLIS 60000
#define WIFI_CONNECTION_MILLIS  3000

// Solenoid HTTP client: e-paper refresh can block the solenoid web server for several seconds.
#define SOLENOID_HTTP_TIMEOUT_MS      15000
#define SOLENOID_HTTP_CONNECT_MS      5000
#define SOLENOID_HTTP_RETRY_COUNT     3
#define SOLENOID_HTTP_RETRY_DELAY_MS  750

/* Configuration of NTP */
// choose the best fitting NTP server pool for your country
#define MY_NTP_SERVER "pool.ntp.org"

// choose your time zone from this list
// https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
#define MY_TZ "PST8PDT,M3.2.0,M11.1.0"


GxIO_Class io(SDSPI, EDP_CS_PIN, EDP_DC_PIN, EDP_RSET_PIN);
GxEPD_Class display(io, EDP_RSET_PIN, EDP_BUSY_PIN);


bool remoteSignalOn = false;
bool vfdErrorActive = false;
bool vfdLowPressureLockout = false;
bool pressureSensorEnabled = true;
byte currentZoneState = ZONES_OFF;

float solenoidPressurePsi = 0.0f;
int solenoidBatteryPct = -1;
bool solenoidPressureValid = false;
bool solenoidPressureStale = false;
bool solenoidBatteryValid = false;
bool solenoidPressureLowAlarm = false;
bool solenoidPressureHighAlarm = false;
bool solenoidBatteryLowAlarm = false;

IPAddress local_IP(STATION_STATIC_IP_0, STATION_STATIC_IP_1, STATION_STATIC_IP_2, STATION_STATIC_IP_3);
IPAddress gateway(LAN_GATEWAY_0, LAN_GATEWAY_1, LAN_GATEWAY_2, LAN_GATEWAY_3);
IPAddress subnet(LAN_SUBNET_0, LAN_SUBNET_1, LAN_SUBNET_2, LAN_SUBNET_3);
IPAddress primaryDNS(LAN_DNS_PRIMARY_0, LAN_DNS_PRIMARY_1, LAN_DNS_PRIMARY_2, LAN_DNS_PRIMARY_3);


WebServer server(80);

unsigned long timerStartTime = 0;
unsigned long timerDuration = 0;
bool timerRunning = false;
bool wateringRunActive = false;
bool webStartTimer = false;
bool webStopTimer = false;
time_t activeRunStartEpoch = 0;
byte timerMode = ZONES_OFF;

WateringRecord lastWateringZ1 = {0, 0};
WateringRecord lastWateringZ2 = {0, 0};
WateringRecord lastWateringGH = {0, 0};
WateringRecord lastWateringWC = {0, 0};
WateringRecord lastWateringRemote = {0, 0};

unsigned long remoteRunStartMs = 0;
time_t remoteRunStartEpoch = 0;

void record_watering_for_mode(byte mode, time_t startEpoch, unsigned long durationMs) {
  WateringRecord record = {startEpoch, durationMs};
  switch (mode) {
    case ZONE1_ON:
      lastWateringZ1 = record;
      break;
    case ZONE2_ON:
      lastWateringZ2 = record;
      break;
    case GREENHOUSE_ON:
      lastWateringGH = record;
      break;
    case CANON_ON:
      lastWateringWC = record;
      break;
    case All_ZONES_ON:
      lastWateringZ1 = record;
      lastWateringZ2 = record;
      break;
    default:
      break;
  }
}

void record_remote_watering(time_t startEpoch, unsigned long durationMs) {
  lastWateringRemote = {startEpoch, durationMs};
}

int vfdMode = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long wifiConnectionUpdate = 0;
bool wifiReconnecting = false;
unsigned long wifiReconnectStarted = 0;
unsigned long lastInfluxSend = -3000000; // Track last InfluxDB send time
unsigned long lastPressurePoll = 0;
unsigned long lastPressureFetchAttempt = 0;
unsigned long lastPressureStatusRefresh = 0;
unsigned long wifiDisconnectedSince = 0;
unsigned long pressureLowSince = 0;
unsigned long pressureHighSince = 0;
bool pressureLowAlertSent = false;
bool pressureHighAlertSent = false;
bool batteryLowAlertSent = false;
unsigned long vfdErrorPendingSince = 0;
unsigned long vfdErrorClearSince = 0;
unsigned long lastVfdAlertPostMs = 0;
InfluxArduino influx;


void setupInflux() {
  influx.configure(INFLUX_DATABASE, INFLUX_HOSTNAME);
  influx.authorize(INFLUX_USER, INFLUX_PASSWORD);
  influx.addCertificate(ROOT_CERT);
}

bool sendDatapointsToInflux() {
  bool success = true;
  
  // Create tags for the measurement
  const char* tags = "location=main-pump,sensor=pump-station";

  // Send vfd to InfluxDB
  char vfd_value[16];
  snprintf(vfd_value, sizeof(vfd_value), "value=%d", vfdMode);
  if (!influx.write("vfd_mode", tags, vfd_value)) success = false;
  Serial.printf("Send vfd_mode to influx. result:");
  Serial.println(success);

  // remote signal
  char remote_value[16];
  snprintf(remote_value, sizeof(remote_value), "value=%d", remoteSignalOn ? 1 : 0);
  if (!influx.write("remote_signal", tags, remote_value)) success = false;
  Serial.printf("Send remote_signal to influx. result:");
  Serial.println(success);

  // timer_running
  char timer_value[16];
  snprintf(timer_value, sizeof(timer_value), "value=%d", timerRunning ? 1 : 0);
  if (!influx.write("timer_running", tags, timer_value)) success = false;
  Serial.printf("Send timer_running to influx. result:");
  Serial.println(success);

  // zones
  uint8_t z1 = 0;
  uint8_t z2 = 0;
  uint8_t gh = 0;
  uint8_t wc = 0;
  if (currentZoneState == ZONE1_ON) z1 = 1;
  if (currentZoneState == ZONE2_ON) z2 = 1;
  if (currentZoneState == All_ZONES_ON) {
    z1 = 1;
    z2 = 1;
  }
  if (currentZoneState == GREENHOUSE_ON) gh = 1;
  if (currentZoneState == CANON_ON) wc = 1;

  char zone1_value[16];
  snprintf(zone1_value, sizeof(zone1_value), "value=%d", z1);
  if (!influx.write("hemp_zone1", tags, zone1_value)) success = false;
  Serial.printf("Send hemp_zone1 to influx. result:");
  Serial.println(success);

  char zone2_value[16];
  snprintf(zone2_value, sizeof(zone2_value), "value=%d", z2);
  if (!influx.write("hemp_zone2", tags, zone2_value)) success = false;
  Serial.printf("Send hemp_zone2 to influx. result:");
  Serial.println(success);

  char greenhouse_value[16];
  snprintf(greenhouse_value, sizeof(greenhouse_value), "value=%d", gh);
  if (!influx.write("hemp_greenhouse", tags, greenhouse_value)) success = false;
  Serial.printf("Send hemp_greenhouse to influx. result:");
  Serial.println(success);

  char water_canon_value[16];
  snprintf(water_canon_value, sizeof(water_canon_value), "value=%d", wc);
  if (!influx.write("hemp_water_canon", tags, water_canon_value)) success = false;
  Serial.printf("Send hemp_water_canon to influx. result:");
  Serial.println(success);

  return success;
}


bool send_zone_command(byte zone_state) {
  String url = "http://" + String(SOLENOID_HTTP_HOST) + "/set_zone";
  String postData = "";

  // Map zone states to solenoid parameters
  if (zone_state == ZONE1_ON) {
    postData = "zone1=1&zone2=0";
  } else if (zone_state == ZONE2_ON) {
    postData = "zone1=0&zone2=1";
  } else if (zone_state == All_ZONES_ON) {
    postData = "zone1=1&zone2=1";
  } else { // ZONES_OFF or any other state
    postData = "zone1=0&zone2=0";
  }

  for (uint8_t attempt = 1; attempt <= SOLENOID_HTTP_RETRY_COUNT; attempt++) {
    HTTPClient http;

    http.begin(url);
    http.setTimeout(SOLENOID_HTTP_TIMEOUT_MS);
    http.setConnectTimeout(SOLENOID_HTTP_CONNECT_MS);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    Serial.printf("Sending zone command (attempt %u/%u): %s\n",
                  attempt, SOLENOID_HTTP_RETRY_COUNT, postData.c_str());

    int httpResponseCode = http.POST(postData);

    if (httpResponseCode == 200) {
      String response = http.getString();
      Serial.print("Zone command successful: ");
      Serial.println(response);
      http.end();
      currentZoneState = zone_state;
      return true;
    }

    if (httpResponseCode < 0) {
      Serial.printf("Zone command failed: %s (%d)\n",
                    http.errorToString(httpResponseCode).c_str(), httpResponseCode);
    } else {
      Serial.printf("Zone command failed with HTTP code: %d\n", httpResponseCode);
    }
    http.end();

    if (attempt < SOLENOID_HTTP_RETRY_COUNT) {
      Serial.printf("Retrying solenoid command in %lu ms\n", SOLENOID_HTTP_RETRY_DELAY_MS);
      station_service_web_during_wait(SOLENOID_HTTP_RETRY_DELAY_MS);
    }
  }

  currentZoneState = ZONE_ERROR;
  return false;
}

bool read_remote_pump_input() {
  int on_count = 0;
  for (int i = 0; i < REMOTE_SAMPLE_SIZE; i++) {
    if (digitalRead(PUMP_INPUT_PIN) == REMOTE_PUMP_ON) {
      on_count++;
    }
    delay(REMOTE_SAMPLE_DELAY_MS);
    if ((i % 5) == 4) {
      station_watchdog_feed();
    }
  }
  // Majority over REMOTE_SAMPLE_DELAY_MS * REMOTE_SAMPLE_SIZE (~160 ms) spans
  // ~8-10 AC cycles at 50/60 Hz when the opto is energized.
  return on_count > (REMOTE_SAMPLE_SIZE / 2);
}

bool read_vfd_error_input() {
  int active_count = 0;
  for (int i = 0; i < VFD_ERROR_SAMPLE_SIZE; i++) {
    if (digitalRead(VFD_ERROR_INPUT_PIN) == VFD_ERROR_ACTIVE) {
      active_count++;
    }
    delay(VFD_ERROR_SAMPLE_DELAY_MS);
  }
  // Majority over VFD_ERROR_SAMPLE_DELAY_MS * VFD_ERROR_SAMPLE_SIZE (~160 ms)
  // spans several AC cycles when the opto is energized.
  return active_count > (VFD_ERROR_SAMPLE_SIZE / 2);
}

void format_event_datetime(char *buffer, size_t buflen) {
  time_t now;
  time(&now);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  strftime(buffer, buflen, "%Y-%m-%dT%H:%M:%S%z", &timeinfo);
}

bool send_vfd_error_alert(const char *summary, const char *datetime) {
  if (VFD_ALERT_URL[0] == '\0') {
    Serial.println("VFD_ALERT_URL not configured; skipping alert POST");
    return false;
  }

  const unsigned long now = millis();
  if (lastVfdAlertPostMs != 0 &&
      now - lastVfdAlertPostMs < VFD_ALERT_COOLDOWN_MS) {
    Serial.print("Alert rate-limited (");
    Serial.print(summary);
    Serial.println("); skipping POST");
    return false;
  }

  HTTPClient http;
  http.begin(VFD_ALERT_URL);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["error_summary"] = summary;
  doc["datetime"] = datetime;

  String body;
  serializeJson(doc, body);

  Serial.print("Alert POST ");
  Serial.println(body);

  int httpResponseCode = http.POST(body);
  bool success = (httpResponseCode >= 200 && httpResponseCode < 300);

  if (success) {
    lastVfdAlertPostMs = now;
    Serial.print("Alert accepted: ");
    Serial.println(http.getString());
  } else {
    Serial.print("Alert failed with code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
  return success;
}

void station_watchdog_init() {
  esp_task_wdt_deinit();
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t wdt_config = {
      .timeout_ms = STATION_WATCHDOG_TIMEOUT_MS,
      .idle_core_mask = 0,
      .trigger_panic = true,
  };
  esp_task_wdt_init(&wdt_config);
#else
  esp_task_wdt_init(STATION_WATCHDOG_TIMEOUT_MS / 1000, true);
#endif
  esp_task_wdt_add(NULL);
  Serial.print("Watchdog enabled (");
  Serial.print(STATION_WATCHDOG_TIMEOUT_MS / 1000);
  Serial.println(" s timeout)");
}

void station_watchdog_feed() {
  esp_task_wdt_reset();
}

static void station_service_web_during_wait(unsigned long delay_ms) {
  const unsigned long end_ms = millis() + delay_ms;
  while ((long)(end_ms - millis()) > 0) {
    server.handleClient();
    station_watchdog_feed();
    delay(10);
  }
}

bool pump_is_active() {
  return vfdMode > 0 || remoteSignalOn;
}

unsigned long pressure_poll_interval_ms() {
  return pump_is_active() ? STATION_PRESSURE_POLL_ACTIVE_MS
                          : STATION_PRESSURE_POLL_INTERVAL_MS;
}

void request_immediate_pressure_poll() {
  if (!pressureSensorEnabled) {
    return;
  }
  lastPressurePoll = 0;
}

bool parse_solenoid_pressure_json(JsonDocument &doc, SolenoidPressureSample *sample) {
  if (!(doc["pressure_enabled"] | false)) {
    sample->ok = false;
    sample->stale = false;
    return true;
  }

  sample->ok = doc["pressure_valid"] | false;
  sample->stale = doc["pressure_stale"] | false;

  if (!sample->ok && !sample->stale) {
    sample->stale = false;
    const char *message = doc["pressure_error"] | "unknown error";
    Serial.print("Solenoid pressure error: ");
    Serial.println(message);
    return false;
  }

  if (doc["pressure_psi"].isNull()) {
    Serial.println("Solenoid status missing pressure_psi");
    sample->ok = false;
    sample->stale = false;
    return false;
  }

  sample->psi = doc["pressure_psi"].as<float>();
  sample->battery_valid = !doc["pressure_battery_pct"].isNull();
  sample->battery_pct =
      sample->battery_valid ? doc["pressure_battery_pct"].as<int>() : -1;
  if (sample->stale) {
    const char *message = doc["pressure_error"] | "stale reading";
    Serial.print("Solenoid pressure stale: ");
    Serial.println(message);
    sample->ok = false;
  }
  return true;
}

bool fetch_solenoid_status_once(SolenoidPressureSample *sample) {
  String url = "http://" + String(SOLENOID_HTTP_HOST) + "/status";
  WiFiClient client;
  HTTPClient http;

  http.setTimeout(SOLENOID_HTTP_TIMEOUT_MS);
  http.setConnectTimeout(SOLENOID_HTTP_CONNECT_MS);
  http.setReuse(false);
  http.begin(client, url);
  http.addHeader("Connection", "close");

  int httpResponseCode = http.GET();
  if (httpResponseCode != 200) {
    Serial.print("Solenoid status poll failed with code: ");
    Serial.println(httpResponseCode);
    http.end();
    sample->ok = false;
    sample->stale = false;
    return false;
  }

  JsonDocument doc;
  WiFiClient *stream = http.getStreamPtr();
  DeserializationError error =
      stream != nullptr ? deserializeJson(doc, *stream) : DeserializationError::EmptyInput;
  http.end();

  if (error) {
    Serial.print("Solenoid status JSON parse failed: ");
    Serial.println(error.c_str());
    sample->ok = false;
    sample->stale = false;
    return false;
  }

  return parse_solenoid_pressure_json(doc, sample);
}

bool fetch_solenoid_status(SolenoidPressureSample *sample) {
  for (uint8_t attempt = 1; attempt <= SOLENOID_HTTP_RETRY_COUNT; attempt++) {
    if (fetch_solenoid_status_once(sample)) {
      return true;
    }
    if (attempt < SOLENOID_HTTP_RETRY_COUNT) {
      station_service_web_during_wait(SOLENOID_HTTP_RETRY_DELAY_MS);
    }
  }
  return false;
}

void reset_pressure_alarm_state() {
  pressureLowSince = 0;
  pressureHighSince = 0;
  pressureLowAlertSent = false;
  pressureHighAlertSent = false;
}

static bool pressure_cache_is_empty() {
  return !solenoidPressureValid && !solenoidPressureStale;
}

static bool pressure_poll_due(unsigned long now) {
  const unsigned long poll_interval = pressure_poll_interval_ms();
  if (lastPressurePoll == 0) {
    return true;
  }
  if (now - lastPressurePoll >= poll_interval) {
    return true;
  }
  if (pressure_cache_is_empty() && lastPressureFetchAttempt != 0 &&
      now - lastPressureFetchAttempt >= STATION_PRESSURE_FETCH_RETRY_MS) {
    return true;
  }
  return false;
}

static bool apply_solenoid_pressure_sample(const SolenoidPressureSample &sample) {
  if (!sample.ok && !sample.stale) {
    solenoidPressureValid = false;
    solenoidPressureStale = false;
    return false;
  }

  solenoidPressurePsi = sample.psi;
  solenoidBatteryValid = sample.battery_valid;
  if (sample.battery_valid) {
    solenoidBatteryPct = sample.battery_pct;
  }

  if (sample.stale) {
    solenoidPressureValid = false;
    solenoidPressureStale = true;
    return true;
  }

  solenoidPressureValid = true;
  solenoidPressureStale = false;
  return true;
}

bool refresh_pressure_from_solenoid() {
  if (!pressureSensorEnabled) {
    return false;
  }

  const unsigned long now = millis();
  lastPressureFetchAttempt = now;

  SolenoidPressureSample sample = {};
  if (!fetch_solenoid_status(&sample)) {
    if (!pressure_cache_is_empty()) {
      solenoidPressureValid = false;
      solenoidPressureStale = true;
      Serial.println("Solenoid status poll failed; keeping last pressure as stale");
    }
    return false;
  }

  if (!sample.ok && !sample.stale) {
    solenoidPressureValid = false;
    solenoidPressureStale = false;
    return false;
  }

  apply_solenoid_pressure_sample(sample);
  lastPressurePoll = now;

  Serial.print("Solenoid pressure ");
  Serial.print((int)sample.psi);
  Serial.println(sample.stale ? " psi (stale)" : " psi");
  if (sample.battery_valid) {
    Serial.print("Solenoid sensor battery ");
    Serial.print(sample.battery_pct);
    Serial.println("%");
  }
  return true;
}

static volatile bool pressure_status_refresh_pending = false;
static bool pressure_status_refresh_in_progress = false;

void request_pressure_for_status_refresh() {
  if (!pressureSensorEnabled) {
    return;
  }

  const unsigned long now = millis();
  const bool cache_empty = pressure_cache_is_empty();
  const bool cache_zero =
      (solenoidPressureValid || solenoidPressureStale) && solenoidPressurePsi == 0.0f;
  const bool refresh_due = lastPressureStatusRefresh == 0 ||
                           now - lastPressureStatusRefresh >=
                               STATION_PRESSURE_STATUS_REFRESH_MS;

  if (!cache_empty && !cache_zero && !refresh_due) {
    return;
  }

  pressure_status_refresh_pending = true;
}

void process_pending_pressure_status_refresh() {
  if (!pressureSensorEnabled || !pressure_status_refresh_pending ||
      pressure_status_refresh_in_progress) {
    return;
  }

  pressure_status_refresh_pending = false;
  pressure_status_refresh_in_progress = true;
  lastPressureStatusRefresh = millis();
  refresh_pressure_from_solenoid();
  pressure_status_refresh_in_progress = false;
}

static void trigger_low_pressure_vfd_lockout();
static void clear_low_pressure_vfd_lockout();

static void clear_station_pressure_state() {
  pressure_status_refresh_pending = false;
  lastPressurePoll = 0;
  lastPressureFetchAttempt = 0;
  solenoidPressureValid = false;
  solenoidPressureStale = false;
  solenoidBatteryValid = false;
  solenoidPressureLowAlarm = false;
  solenoidPressureHighAlarm = false;
  solenoidBatteryLowAlarm = false;
  batteryLowAlertSent = false;
  reset_pressure_alarm_state();
  clear_low_pressure_vfd_lockout();
}

void pressure_sensor_init() {
  Preferences prefs;
  if (!prefs.begin("presscfg", true)) {
    pressureSensorEnabled = true;
    return;
  }
  pressureSensorEnabled = prefs.getBool("sensor", prefs.getBool("safety", true));
  prefs.end();
  Serial.print("Remote pressure sensor ");
  Serial.println(pressureSensorEnabled ? "enabled" : "disabled");
  if (!pressureSensorEnabled) {
    clear_station_pressure_state();
  }
}

bool set_pressure_sensor_enabled(bool enabled) {
  pressureSensorEnabled = enabled;
  Preferences prefs;
  if (prefs.begin("presscfg", false)) {
    prefs.putBool("sensor", enabled);
    prefs.end();
  }
  Serial.print("Remote pressure sensor ");
  Serial.println(enabled ? "enabled" : "disabled");
  if (!enabled) {
    clear_station_pressure_state();
  } else {
    request_immediate_pressure_poll();
  }
  display_task_request_refresh();
  return true;
}

static void evaluate_pressure_alarms(const SolenoidPressureSample &sample,
                                     unsigned long now) {
  const bool pump_on = pump_is_active();
  if (!pump_on) {
    reset_pressure_alarm_state();
    batteryLowAlertSent = false;
    return;
  }

  if (!sample.ok || sample.stale) {
    return;
  }

  char datetime[40];
  format_event_datetime(datetime, sizeof(datetime));

  if (sample.psi < PRESSURE_LOW_PSI) {
    if (pressureLowSince == 0) {
      pressureLowSince = now;
    }
    if (!pressureLowAlertSent &&
        now - pressureLowSince >= PRESSURE_LOW_ALARM_DURATION_MS) {
      pressureLowAlertSent = true;
      send_vfd_error_alert(PRESSURE_LOW_SUMMARY, datetime);
      Serial.println("Solenoid pressure low alarm");
      trigger_low_pressure_vfd_lockout();
    }
  } else {
    pressureLowSince = 0;
    if (!vfdLowPressureLockout) {
      pressureLowAlertSent = false;
      solenoidPressureLowAlarm = false;
    }
  }

  if (sample.psi > PRESSURE_HIGH_PSI) {
    if (pressureHighSince == 0) {
      pressureHighSince = now;
    }
    if (!pressureHighAlertSent &&
        now - pressureHighSince >= PRESSURE_HIGH_ALARM_DURATION_MS) {
      solenoidPressureHighAlarm = true;
      pressureHighAlertSent = true;
      send_vfd_error_alert(PRESSURE_HIGH_SUMMARY, datetime);
      Serial.println("Solenoid pressure high alarm");
    }
  } else {
    pressureHighSince = 0;
    pressureHighAlertSent = false;
    solenoidPressureHighAlarm = false;
  }

  if (sample.battery_valid && sample.battery_pct < PRESSURE_BATTERY_LOW_PCT) {
    if (!batteryLowAlertSent) {
      solenoidBatteryLowAlarm = true;
      batteryLowAlertSent = true;
      send_vfd_error_alert(PRESSURE_BATTERY_SUMMARY, datetime);
      Serial.println("Solenoid sensor battery low alarm");
    }
  } else if (sample.battery_valid) {
    batteryLowAlertSent = false;
    solenoidBatteryLowAlarm = false;
  }
}

void update_pressure_monitoring() {
  if (!pressureSensorEnabled) {
    return;
  }

  const unsigned long now = millis();
  if (!pressure_poll_due(now)) {
    return;
  }

  SolenoidPressureSample sample = {};
  lastPressureFetchAttempt = now;
  if (!fetch_solenoid_status(&sample)) {
    if (!pressure_cache_is_empty()) {
      solenoidPressureValid = false;
      solenoidPressureStale = true;
      Serial.println("Solenoid status poll failed; keeping last pressure as stale");
    }
    return;
  }

  if (!sample.ok && !sample.stale) {
    solenoidPressureValid = false;
    solenoidPressureStale = false;
    return;
  }

  apply_solenoid_pressure_sample(sample);
  lastPressurePoll = now;

  Serial.print("Solenoid pressure ");
  Serial.print((int)sample.psi);
  Serial.println(sample.stale ? " psi (stale)" : " psi");
  if (sample.battery_valid) {
    Serial.print("Solenoid sensor battery ");
    Serial.print(sample.battery_pct);
    Serial.println("%");
  }

  evaluate_pressure_alarms(sample, now);
}

void update_vfd() {
  const uint8_t prev_vfd_mode = vfdMode;
  int  vfd_power = 0;

  if (vfdLowPressureLockout) {
    vfdMode = 0;
    digitalWrite(HALF_PWR_OUTPUT_PIN, RELAY_OFF);
    digitalWrite(FULL_PWR_OUTPUT_PIN, RELAY_OFF);
    return;
  }

  // if the system is trying to stop, then don't pay attention to the current state becuase the VFD needs to stop first
  if (timerRunning) {
    if (currentZoneState == ZONE1_ON) vfd_power++;
    if (currentZoneState == ZONE2_ON) vfd_power++;
    if (currentZoneState == GREENHOUSE_ON) vfd_power++;
    if (currentZoneState == All_ZONES_ON) vfd_power = 2;
    if (currentZoneState == CANON_ON) vfd_power = 2;
  }

  if (remoteSignalOn) {
    vfd_power++;
  } 

  
  vfdMode = min(vfd_power, 2);
  if (vfdMode == 1) {
    // 1 activity, half power
    digitalWrite(HALF_PWR_OUTPUT_PIN, RELAY_ON);
    digitalWrite(FULL_PWR_OUTPUT_PIN, RELAY_OFF);
  } else if (vfdMode == 2) {
    // 2 activities full power
    digitalWrite(HALF_PWR_OUTPUT_PIN, RELAY_OFF);
    digitalWrite(FULL_PWR_OUTPUT_PIN, RELAY_ON);
  } else {
    // any other scenario, off
    digitalWrite(HALF_PWR_OUTPUT_PIN, RELAY_OFF);
    digitalWrite(FULL_PWR_OUTPUT_PIN, RELAY_OFF);
  }

  if (vfdMode > 0 && prev_vfd_mode == 0) {
    request_immediate_pressure_poll();
  }
}


void start_timer() {
  if (vfdLowPressureLockout) {
    Serial.println("Start blocked: low pressure VFD lockout");
    return;
  }

  wateringRunActive = false;

  if (timerMode != GREENHOUSE_ON && timerMode != CANON_ON) {
    if (!send_zone_command(timerMode)) {
      timerRunning = false;
      activeRunStartEpoch = 0;
      update_vfd();
      display_task_request_refresh();
      return;
    }
  } else {
    currentZoneState = timerMode;
  }

  timerStartTime = millis();
  time(&activeRunStartEpoch);
  timerRunning = true;
  wateringRunActive = true;
  update_vfd();
  display_task_request_refresh();
}

void stop_timer() {
  byte completedMode = timerMode;
  time_t startedAt = activeRunStartEpoch;
  unsigned long durationMs = millis() - timerStartTime;
  bool recordRun = wateringRunActive;

  byte ending_state = currentZoneState;
  timerRunning = false;
  wateringRunActive = false;
  activeRunStartEpoch = 0;
  timerMode = ZONES_OFF;

  update_vfd();

  if (ending_state != GREENHOUSE_ON && ending_state != CANON_ON) {
    if (!send_zone_command(ZONES_OFF)) {
      Serial.println("Failed to close solenoid zones on stop");
    }
  } else {
    currentZoneState = ZONES_OFF;
  }

  if (recordRun) {
    record_watering_for_mode(completedMode, startedAt, durationMs);
  }

  display_task_request_refresh();
}

static void clear_low_pressure_vfd_lockout() {
  if (!vfdLowPressureLockout) {
    reset_pressure_alarm_state();
    return;
  }
  vfdLowPressureLockout = false;
  solenoidPressureLowAlarm = false;
  reset_pressure_alarm_state();
  Serial.println("Low pressure VFD lockout cleared");
  update_vfd();
  display_task_request_refresh();
}

static void trigger_low_pressure_vfd_lockout() {
  if (vfdLowPressureLockout) {
    return;
  }
  if (!pressureSensorEnabled) {
    Serial.println("Low pressure alarm ignored; remote pressure sensor disabled");
    return;
  }
  vfdLowPressureLockout = true;
  solenoidPressureLowAlarm = true;
  Serial.println("Low pressure VFD lockout — disable the pressure sensor to run the pump");
  if (timerRunning) {
    stop_timer();
  } else {
    update_vfd();
    display_task_request_refresh();
  }
}

bool wifi_connect(unsigned long timeout_ms) {
    Serial.print("Connecting to wifi ");

    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, primaryDNS)) {
        Serial.println("STA Failed to configure");
    }

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long started = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (timeout_ms > 0 && (millis() - started) >= timeout_ms) {
            Serial.println("\nWiFi connect timeout");
            return false;
        }
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    return true;
}

void init_wifi() {
    wifi_connect(0);
}

void init_display() {
    Serial.print("Initializing display ... ");
    pinMode(EDP_MISO_PIN, INPUT_PULLUP);
    SDSPI.begin(EDP_CLK_PIN, EDP_MISO_PIN, EDP_MOSI_PIN, EDP_CS_PIN);

    display.init();
    display.setTextColor(GxEPD_BLACK);
    delay(10);
    display.setRotation(3);
    delay(10);
    display.fillScreen(GxEPD_WHITE);
    delay(10);
    display.drawExampleBitmap(logo_200_blk, 0, 0, 72, 128, GxEPD_BLACK);
    display.update();
    Serial.println("ready!");
}

void format_millis(unsigned long milliseconds, char *buffer) {
    unsigned long time_in_seconds = milliseconds/MILLISECONDS;
    unsigned long hrs = 0;
    unsigned long minutes = 0;
    unsigned long secs = 0;

    if (time_in_seconds > HOURS) {
      hrs = time_in_seconds/HOURS;
      minutes = (time_in_seconds - (hrs*HOURS))/MINUTES;
      secs = time_in_seconds % MINUTES;
      if (secs > 0) {
        sprintf(buffer, "%luh %02lum %02lus", hrs, minutes, secs);
      } else {
        sprintf(buffer, "%luh %02lum", hrs, minutes);
      }
    } else if (time_in_seconds > MINUTES) {
      minutes = time_in_seconds/MINUTES;
      secs = time_in_seconds % MINUTES;
      if (secs > 0) {
        sprintf(buffer, "%lum %02lus", minutes, secs);
      } else {
        sprintf(buffer, "%lum", minutes);
      }
    } else {
      sprintf(buffer, "%lus", time_in_seconds);
    }
}

String format_time_ago(unsigned long secondsAgo) {
  if (secondsAgo < 60) {
    return String(secondsAgo) + " seconds ago";
  } else if (secondsAgo < 3600) {
    unsigned long minutes = secondsAgo / 60;
    unsigned long remainingSeconds = secondsAgo % 60;
    if (remainingSeconds > 0) {
      return String(minutes) + "m " + String(remainingSeconds) + "s ago";
    } else {
      return String(minutes) + " min ago";
    }
  } else if (secondsAgo < 86400) {
    unsigned long hours = secondsAgo / 3600;
    unsigned long remainingMinutes = (secondsAgo % 3600) / 60;
    if (remainingMinutes > 0) {
      return String(hours) + "h " + String(remainingMinutes) + "m ago";
    } else {
      return String(hours) + " hours ago";
    }
  } else {
    unsigned long days = secondsAgo / 86400;
    unsigned long remainingHours = (secondsAgo % 86400) / 3600;
    if (remainingHours > 0) {
      return String(days) + "d " + String(remainingHours) + "h ago";
    } else {
      return String(days) + " days ago";
    }
  }
}

String format_duration(unsigned long milliseconds) {
  unsigned long seconds = milliseconds / MILLISECONDS;
  unsigned long hours = seconds / 3600;
  unsigned long minutes = (seconds % 3600) / 60;
  unsigned long remainingSeconds = seconds % 60;
  
  if (hours > 0) {
    return String(hours) + "h " + String(minutes) + "m " + String(remainingSeconds) + "s";
  } else if (minutes > 0) {
    return String(minutes) + "m " + String(remainingSeconds) + "s";
  } else {
    return String(remainingSeconds) + "s";
  }
}

void setup() {
  delay(500);
  Serial.begin(115200);
  Serial.println("Station Starting");

  // setup display
  init_display();

  // setup IO
  pinMode(PUMP_INPUT_PIN, INPUT_PULLUP);
  pinMode(PUMP_GND_PIN, OUTPUT);
  digitalWrite(PUMP_GND_PIN, LOW);
  
  pinMode(HALF_PWR_OUTPUT_PIN, OUTPUT);
  digitalWrite(HALF_PWR_OUTPUT_PIN, RELAY_OFF);
  
  pinMode(FULL_PWR_OUTPUT_PIN, OUTPUT);
  digitalWrite(FULL_PWR_OUTPUT_PIN, RELAY_OFF);

  pinMode(VFD_ERROR_INPUT_PIN, INPUT_PULLUP);

  // setup wifi and time code
  configTime(0, 0, MY_NTP_SERVER);
  setenv("TZ", MY_TZ, 1);
  tzset();

  wifi_connect(30000);
  display_task_start();
  web_server_init();
  schedule_init();
  pressure_sensor_init();
  setupInflux();
  station_watchdog_init();
  display_task_request_refresh();
  lastDisplayUpdate = millis();
}

void loop() {
  station_watchdog_feed();

  // Service web requests first; never block inside HTTP handlers.
  server.handleClient();
  process_pending_pressure_status_refresh();
  server.handleClient();

  // Handle timer
  if (timerRunning && (millis() - timerStartTime >= timerDuration)) {
    stop_timer();
  }

  schedule_tick(timerRunning);
  schedule_process_pending();

  // Remote pump: majority vote across several AC cycles (see REMOTE_SAMPLE_*)
  bool remote_on = read_remote_pump_input();
  if (!remoteSignalOn && remote_on) {
    remoteSignalOn = true;
    remoteRunStartMs = millis();
    time(&remoteRunStartEpoch);
    Serial.println("REMOTE OFF -> ON");
    update_vfd();
    display_task_request_refresh();
  } else if (remoteSignalOn && !remote_on) {
    remoteSignalOn = false;
    if (remoteRunStartEpoch != 0) {
      record_remote_watering(remoteRunStartEpoch, millis() - remoteRunStartMs);
    }
    remoteRunStartMs = 0;
    remoteRunStartEpoch = 0;
    Serial.println("REMOTE ON -> OFF");
    update_vfd();
    display_task_request_refresh();
  }

  bool vfd_error_now = read_vfd_error_input();
  const unsigned long vfd_now = millis();
  if (vfd_error_now) {
    vfdErrorClearSince = 0;
    if (!vfdErrorActive) {
      if (vfdErrorPendingSince == 0) {
        vfdErrorPendingSince = vfd_now;
      } else if (vfd_now - vfdErrorPendingSince >= VFD_ERROR_DEBOUNCE_MS) {
        vfdErrorActive = true;
        display_task_request_refresh();
        char datetime[40];
        format_event_datetime(datetime, sizeof(datetime));
        send_vfd_error_alert(VFD_ERROR_SUMMARY, datetime);
        Serial.println("VFD error detected");
        vfdErrorPendingSince = 0;
      }
    }
  } else {
    vfdErrorPendingSince = 0;
    if (vfdErrorActive) {
      if (vfdErrorClearSince == 0) {
        vfdErrorClearSince = vfd_now;
      } else if (vfd_now - vfdErrorClearSince >= VFD_ERROR_CLEAR_DEBOUNCE_MS) {
        vfdErrorActive = false;
        display_task_request_refresh();
        Serial.println("VFD error cleared");
        vfdErrorClearSince = 0;
      }
    } else {
      vfdErrorClearSince = 0;
    }
  }

  update_pressure_monitoring();

  // Handle web events outside of the webserver response path.
  // Always clear deferred flags so a stop request while idle (or a start while
  // already running) cannot stick around and cancel the next start.
  if (webStartTimer) {
    webStartTimer = false;
    if (!timerRunning) {
      start_timer();
    }
  }
  if (webStopTimer) {
    webStopTimer = false;
    if (timerRunning) {
      stop_timer();
    }
  }

  if (millis() - wifiConnectionUpdate > WIFI_CONNECTION_MILLIS) {
    wifiConnectionUpdate = millis();
    if (WiFi.status() != WL_CONNECTED) {
      if (wifiDisconnectedSince == 0) {
        wifiDisconnectedSince = millis();
      }
      if (!wifiReconnecting) {
        Serial.println("ERROR: Wifi disconnected, retrying...");
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        wifiReconnecting = true;
        wifiReconnectStarted = millis();
      }
#if STATION_WIFI_RESTART_MS > 0
      if (millis() - wifiDisconnectedSince > STATION_WIFI_RESTART_MS) {
        Serial.println("WiFi down too long; restarting station");
        delay(100);
        ESP.restart();
      }
#endif
    } else {
      wifiDisconnectedSince = 0;
      if (wifiReconnecting) {
        wifiReconnecting = false;
        Serial.println("WiFi reconnected");
        Serial.println(WiFi.localIP());
      }
    }
  }

  if (wifiReconnecting && millis() - wifiReconnectStarted > 30000) {
    Serial.println("WiFi retry timeout, will try again");
    wifiReconnecting = false;
  }

  // Update the display
  if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_MILLIS) {
    display_task_request_refresh();
    lastDisplayUpdate = millis();
  }


  // Send data to InfluxDB every 
  if (millis() - lastInfluxSend > INFLUX_DELAY) {
    lastInfluxSend = millis();
    Serial.println("Sending data to influx...");
    bool success = sendDatapointsToInflux();
    if (success) {
        Serial.println("Data sent to InfluxDB successfully");
    } else {
        Serial.println("Failed to send data to InfluxDB");
        Serial.println(influx.getResponse());
        Serial.println();
      }
  }
}