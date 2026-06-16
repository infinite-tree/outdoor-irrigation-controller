#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <WebServer.h>
#include <ArduinoJson.h>

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

#ifndef BLE_PRESSURE_REFRESH_MS
#define BLE_PRESSURE_REFRESH_MS 600000
#endif
#ifndef DISPLAY_PRESSURE_UPDATE_MS
#define DISPLAY_PRESSURE_UPDATE_MS 600000
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

#define SOLENOID_PULSE_LENGTH   100

#define DISPLAY_UPDATE_MILLIS 60000
#define WIFI_CONNECTION_MILLIS  3000

/* Configuration of NTP */
#define MY_NTP_SERVER "pool.ntp.org"
#define MY_TZ "PST8PDT,M3.2.0,M11.1.0"

SPIClass SDSPI(HSPI);
GxIO_Class io(SDSPI, EDP_CS_PIN, EDP_DC_PIN, EDP_RSET_PIN);
GxEPD_Class display(io, EDP_RSET_PIN, EDP_BUSY_PIN);

unsigned long lastDisplayUpdate = 0;
unsigned long wifiConnectionUpdate = 0;
bool zone1On = false;
bool zone2On = false;
byte zoneStatus = 0;
bool webAction = false;

IPAddress local_IP(SOLENOID_STATIC_IP_0, SOLENOID_STATIC_IP_1, SOLENOID_STATIC_IP_2, SOLENOID_STATIC_IP_3);
IPAddress gateway(LAN_GATEWAY_0, LAN_GATEWAY_1, LAN_GATEWAY_2, LAN_GATEWAY_3);
IPAddress subnet(LAN_SUBNET_0, LAN_SUBNET_1, LAN_SUBNET_2, LAN_SUBNET_3);

WebServer server(80);

void open_solenoid(uint8_t zone) {
  if (zone == ZONE1) {
    digitalWrite(ZONE1_PIN_A, LOW);
    digitalWrite(ZONE1_PIN_B, HIGH);
    delay(SOLENOID_PULSE_LENGTH);
    digitalWrite(ZONE1_PIN_A, LOW);
    digitalWrite(ZONE1_PIN_B, LOW);
    zone1On = true;
    if (zone2On) zoneStatus = All_ZONES_ON;
    else zoneStatus = ZONE1_ON;

  } else if (zone == ZONE2) {
    digitalWrite(ZONE2_PIN_A, LOW);
    digitalWrite(ZONE2_PIN_B, HIGH);
    delay(SOLENOID_PULSE_LENGTH);
    digitalWrite(ZONE2_PIN_A, LOW);
    digitalWrite(ZONE2_PIN_B, LOW);
    zone2On = true;
    if (zone1On) zoneStatus = All_ZONES_ON;
    else zoneStatus = ZONE2_ON;
  }
}

void close_solenoid(uint8_t zone) {
  if (zone == ZONE1) {
    digitalWrite(ZONE1_PIN_A, HIGH);
    digitalWrite(ZONE1_PIN_B, LOW);
    delay(SOLENOID_PULSE_LENGTH);
    digitalWrite(ZONE1_PIN_A, LOW);
    digitalWrite(ZONE1_PIN_B, LOW);
    zone1On = false;
    if (zone2On) zoneStatus = ZONE2_ON;
    else zoneStatus = ZONES_OFF;

  } else if (zone == ZONE2) {
    digitalWrite(ZONE2_PIN_A, HIGH);
    digitalWrite(ZONE2_PIN_B, LOW);
    delay(SOLENOID_PULSE_LENGTH);
    digitalWrite(ZONE2_PIN_A, LOW);
    digitalWrite(ZONE2_PIN_B, LOW);
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

void update_display_status() {
  char zone1_status[10] = "OFF";
  char zone2_status[10] = "OFF";
  if (zoneStatus == ZONE1_ON) {
    strcpy(zone1_status, "ON");
  } else if (zoneStatus == ZONE2_ON) {
    strcpy(zone2_status, "ON");
  } else if (zoneStatus == All_ZONES_ON) {
    strcpy(zone1_status, "ON");
    strcpy(zone2_status, "ON");
  }

  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.fillScreen(GxEPD_WHITE);
  delay(10);
  display.drawExampleBitmap(logo_200_blk, 0, 0, 72, 128, GxEPD_BLACK);

  display.setCursor(90, 35);
  display.println("Zone 1:");
  display.setCursor(170, 35);
  display.println(zone1_status);

  display.setCursor(90, 55);
  display.println("Zone 2:");
  display.setCursor(170, 55);
  display.println(zone2_status);

  char pressure_text[16] = "N/A";
  char battery_text[8] = "N/A";
  if (ble_pressure_enabled() && ble_pressure_has_cache()) {
    BlePressureReading reading = ble_pressure_get_cached();
    if (reading.ok) {
      snprintf(pressure_text, sizeof(pressure_text), "%.1f psi", reading.psi);
      if (reading.battery_valid) {
        snprintf(battery_text, sizeof(battery_text), "%d%%", reading.battery_pct);
      } else {
        strcpy(battery_text, "--");
      }
    } else {
      strcpy(pressure_text, "--");
      strcpy(battery_text, "--");
    }
  }

  display.setCursor(90, 75);
  display.println("Pressure:");
  display.setCursor(170, 75);
  display.println(pressure_text);

  display.setCursor(90, 95);
  display.println("Battery:");
  display.setCursor(170, 95);
  display.println(battery_text);

  display.update();
  lastDisplayUpdate = millis();
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

void init_wifi() {
  Serial.print("Connecting to wifi ");

  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("STA Failed to configure");
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
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

  init_wifi();
  ble_pressure_init();
  web_server_init();

  update_display_status();
}

void loop() {
  server.handleClient();

  const bool pressure_refreshed =
      ble_pressure_refresh_if_stale(BLE_PRESSURE_REFRESH_MS);
  const unsigned long display_interval = ble_pressure_enabled()
                                             ? DISPLAY_PRESSURE_UPDATE_MS
                                             : DISPLAY_UPDATE_MILLIS;

  if (millis() - wifiConnectionUpdate > WIFI_CONNECTION_MILLIS) {
    wifiConnectionUpdate = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("ERROR: Wifi disconnected!");
      server.close();
      WiFi.disconnect();
      init_wifi();
      web_server_init();
    }
  }

  if (webAction || pressure_refreshed ||
      millis() - lastDisplayUpdate > display_interval) {
    webAction = false;
    update_display_status();
  }
}
