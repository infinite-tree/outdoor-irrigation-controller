#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <time.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <InfluxArduino.hpp>
#include <RootCert.h>

#include "lwip/inet.h"
#include "lwip/dns.h"

#include "Adafruit_GFX.h"
#include <Fonts/FreeMonoBold9pt7b.h>
#include <GxEPD.h>
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
// 2.13" b/w  form DKE GROUP
#include <GxDEPG0213BN/GxDEPG0213BN.h>

#include "Logo.h"
#include "Config.h"
#include "solenoid_shared.h"
#include "ble_pressure.h"
#include "display_task.h"

#ifndef BLE_PRESSURE_REFRESH_MS
#define BLE_PRESSURE_REFRESH_MS 600000
#endif
#ifndef DISPLAY_PRESSURE_UPDATE_MS
#define DISPLAY_PRESSURE_UPDATE_MS 600000
#endif
#ifndef INFLUX_DELAY
#define INFLUX_DELAY 300000
#endif
#ifndef SOLENOID_WATCHDOG_TIMEOUT_MS
#define SOLENOID_WATCHDOG_TIMEOUT_MS 120000
#endif
#ifndef SOLENOID_WIFI_RESTART_MS
#define SOLENOID_WIFI_RESTART_MS 3600000
#endif
#ifndef SOLENOID_DISPLAY_WEB_QUIET_MS
#define SOLENOID_DISPLAY_WEB_QUIET_MS 3000
#endif
#ifndef SOLENOID_INFLUX_WEB_QUIET_MS
#define SOLENOID_INFLUX_WEB_QUIET_MS 5000
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

#define ZONES_OFF           0x00
#define ZONE1_ON            0x01
#define ZONE2_ON            0x02
#define All_ZONES_ON        0x03

#define ZONE1_PIN_A     45
#define ZONE1_PIN_B     41
#define ZONE2_PIN_A     42
#define ZONE2_PIN_B     46

#ifndef SOLENOID_PULSE_LENGTH_MS
#define SOLENOID_PULSE_LENGTH_MS    150
#endif
#ifndef SOLENOID_PULSE_REPEATS
#define SOLENOID_PULSE_REPEATS      3
#endif
#ifndef SOLENOID_PULSE_GAP_MS
#define SOLENOID_PULSE_GAP_MS       75
#endif

#define DISPLAY_UPDATE_MILLIS 60000
#define WIFI_CONNECTION_MILLIS  3000

/* Configuration of NTP */
#define MY_NTP_SERVER "pool.ntp.org"
#define MY_TZ "PST8PDT,M3.2.0,M11.1.0"

SPIClass SDSPI(HSPI);
GxIO_Class io(SDSPI, EDP_CS_PIN, EDP_DC_PIN, EDP_RSET_PIN);
GxEPD_Class display(io, EDP_RSET_PIN, EDP_BUSY_PIN);

unsigned long lastDisplayUpdate = 0;
unsigned long lastInfluxSend = -3000000;
unsigned long wifiConnectionUpdate = 0;
unsigned long wifiDisconnectedSince = 0;
bool wifiReconnecting = false;
unsigned long wifiReconnectStarted = 0;
bool zone1On = false;
bool zone2On = false;
byte zoneStatus = 0;
bool webAction = false;
unsigned long lastWebRequestMs = 0;
bool pendingDisplayUpdate = false;

void solenoid_watchdog_feed();
void web_server_poll();

void solenoid_mark_web_activity() {
  lastWebRequestMs = millis();
}

static void solenoid_service_web_during_wait(unsigned long delay_ms) {
  const unsigned long end_ms = millis() + delay_ms;
  while ((long)(end_ms - millis()) > 0) {
    web_server_poll();
    solenoid_watchdog_feed();
    delay(10);
  }
}

static void solenoid_drive_pulse(uint8_t pin_a, uint8_t pin_b, uint8_t level_a, uint8_t level_b) {
  digitalWrite(pin_a, level_a);
  digitalWrite(pin_b, level_b);
  solenoid_service_web_during_wait(SOLENOID_PULSE_LENGTH_MS);
  digitalWrite(pin_a, LOW);
  digitalWrite(pin_b, LOW);
}

static void solenoid_pulse_actuator(uint8_t pin_a, uint8_t pin_b, uint8_t level_a, uint8_t level_b) {
  const uint8_t repeats = SOLENOID_PULSE_REPEATS < 1 ? 1 : SOLENOID_PULSE_REPEATS;
  for (uint8_t i = 0; i < repeats; i++) {
    solenoid_drive_pulse(pin_a, pin_b, level_a, level_b);
    if (i + 1 < repeats && SOLENOID_PULSE_GAP_MS > 0) {
      solenoid_service_web_during_wait(SOLENOID_PULSE_GAP_MS);
    }
  }
}

IPAddress local_IP(SOLENOID_STATIC_IP_0, SOLENOID_STATIC_IP_1, SOLENOID_STATIC_IP_2, SOLENOID_STATIC_IP_3);
IPAddress gateway(LAN_GATEWAY_0, LAN_GATEWAY_1, LAN_GATEWAY_2, LAN_GATEWAY_3);
IPAddress subnet(LAN_SUBNET_0, LAN_SUBNET_1, LAN_SUBNET_2, LAN_SUBNET_3);
IPAddress primaryDNS(LAN_DNS_PRIMARY_0, LAN_DNS_PRIMARY_1, LAN_DNS_PRIMARY_2, LAN_DNS_PRIMARY_3);

WebServer server(80);
InfluxArduino influx;

void setupInflux() {
  influx.configure(INFLUX_DATABASE, INFLUX_HOSTNAME);
  influx.authorize(INFLUX_USER, INFLUX_PASSWORD);
  influx.addCertificate(ROOT_CERT);
}

bool sendZoneStateToInflux() {
  // Actual commanded latch state on this board (compare with station hemp_zone*).
  const char *zone_tags = "location=main-pump,sensor=solenoid-controller";
  bool success = true;

  char zone1_value[16];
  snprintf(zone1_value, sizeof(zone1_value), "value=%d", zone1On ? 1 : 0);
  if (!influx.write("solenoid_zone1", zone_tags, zone1_value)) {
    success = false;
  }
  Serial.printf("Send solenoid_zone1 to influx. result: %d\n", success);

  char zone2_value[16];
  snprintf(zone2_value, sizeof(zone2_value), "value=%d", zone2On ? 1 : 0);
  if (!influx.write("solenoid_zone2", zone_tags, zone2_value)) {
    success = false;
  }
  Serial.printf("Send solenoid_zone2 to influx. result: %d\n", success);

  if (!success) {
    Serial.println(influx.getResponse());
  }
  return success;
}

bool sendDatapointsToInflux() {
  bool success = sendZoneStateToInflux();

  if (!ble_pressure_enabled() || !ble_pressure_is_fresh()) {
    return success;
  }

  BlePressureReading reading = ble_pressure_get_cached();
  const char *pressure_tags = "location=main-pump,sensor=solenoid-pressure";

  char pressure_value[16];
  snprintf(pressure_value, sizeof(pressure_value), "value=%d", (int)reading.psi);
  if (!influx.write("pressure_psi", pressure_tags, pressure_value)) {
    success = false;
  }
  Serial.printf("Send pressure_psi to influx. result: %d\n", success);

  if (reading.battery_valid) {
    char battery_value[16];
    snprintf(battery_value, sizeof(battery_value), "value=%d", reading.battery_pct);
    if (!influx.write("pressure_battery_pct", pressure_tags, battery_value)) {
      success = false;
    }
    Serial.printf("Send pressure_battery_pct to influx. result: %d\n", success);
  }

  return success;
}

void open_solenoid(uint8_t zone) {
  if (zone == ZONE1) {
    solenoid_pulse_actuator(ZONE1_PIN_A, ZONE1_PIN_B, LOW, HIGH);
    zone1On = true;
    if (zone2On) zoneStatus = All_ZONES_ON;
    else zoneStatus = ZONE1_ON;

  } else if (zone == ZONE2) {
    solenoid_pulse_actuator(ZONE2_PIN_A, ZONE2_PIN_B, LOW, HIGH);
    zone2On = true;
    if (zone1On) zoneStatus = All_ZONES_ON;
    else zoneStatus = ZONE2_ON;
  }
}

void close_solenoid(uint8_t zone) {
  if (zone == ZONE1) {
    solenoid_pulse_actuator(ZONE1_PIN_A, ZONE1_PIN_B, HIGH, LOW);
    zone1On = false;
    if (zone2On) zoneStatus = ZONE2_ON;
    else zoneStatus = ZONES_OFF;

  } else if (zone == ZONE2) {
    solenoid_pulse_actuator(ZONE2_PIN_A, ZONE2_PIN_B, HIGH, LOW);
    zone2On = false;
    if (zone1On) zoneStatus = ZONE1_ON;
    else zoneStatus = ZONES_OFF;
  }
}

void init_solenoids() {
  pinMode(ZONE1_PIN_A, OUTPUT);
  pinMode(ZONE1_PIN_B, OUTPUT);
  pinMode(ZONE2_PIN_A, OUTPUT);
  pinMode(ZONE2_PIN_B, OUTPUT);
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

void solenoid_watchdog_init() {
  esp_task_wdt_deinit();
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t wdt_config = {
      .timeout_ms = SOLENOID_WATCHDOG_TIMEOUT_MS,
      .idle_core_mask = 0,
      .trigger_panic = true,
  };
  esp_task_wdt_init(&wdt_config);
#else
  esp_task_wdt_init(SOLENOID_WATCHDOG_TIMEOUT_MS / 1000, true);
#endif
  esp_task_wdt_add(NULL);
  Serial.print("Watchdog enabled (");
  Serial.print(SOLENOID_WATCHDOG_TIMEOUT_MS / 1000);
  Serial.println(" s timeout)");
}

void solenoid_watchdog_feed() {
  esp_task_wdt_reset();
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

void setup() {
  delay(500);
  Serial.begin(115200);
  Serial.println("Solenoid Starting");

  init_display();
  init_solenoids();
  close_solenoid(ZONE1);
  close_solenoid(ZONE2);

  configTime(0, 0, MY_NTP_SERVER);
  setenv("TZ", MY_TZ, 1);
  tzset();

  wifi_connect(30000);
  setupInflux();
  ble_pressure_init();
  ble_pressure_start_task();
  display_task_start();
  web_server_init();
  solenoid_watchdog_init();

  solenoid_watchdog_feed();
  display_task_request_refresh();
  lastDisplayUpdate = millis();
  solenoid_watchdog_feed();
}

void loop() {
  solenoid_watchdog_feed();

  web_server_poll();
  web_server_poll();

  const bool pressure_refreshed = ble_pressure_take_display_dirty();
  const unsigned long display_interval = ble_pressure_enabled()
                                             ? DISPLAY_PRESSURE_UPDATE_MS
                                             : DISPLAY_UPDATE_MILLIS;

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
#if SOLENOID_WIFI_RESTART_MS > 0
      if (millis() - wifiDisconnectedSince > SOLENOID_WIFI_RESTART_MS) {
        Serial.println("WiFi down too long; restarting solenoid");
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

  if (webAction || pressure_refreshed) {
    webAction = false;
    pendingDisplayUpdate = true;
  }
  if (millis() - lastDisplayUpdate > display_interval) {
    pendingDisplayUpdate = true;
  }

  const unsigned long web_quiet_ms = millis() - lastWebRequestMs;
  if (pendingDisplayUpdate && web_quiet_ms >= SOLENOID_DISPLAY_WEB_QUIET_MS) {
    pendingDisplayUpdate = false;
    display_task_request_refresh();
    lastDisplayUpdate = millis();
  }

  if (millis() - lastInfluxSend > INFLUX_DELAY &&
      web_quiet_ms >= SOLENOID_INFLUX_WEB_QUIET_MS) {
    lastInfluxSend = millis();
    Serial.println("Sending solenoid data to influx...");
    if (!sendDatapointsToInflux()) {
      Serial.println("Failed to send solenoid data to InfluxDB");
      Serial.println(influx.getResponse());
    }
  }
}
