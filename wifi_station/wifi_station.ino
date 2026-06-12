#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>

// DNS doesn't seem to work without these
#include "lwip/inet.h"
#include "lwip/dns.h"


#include <time.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
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

#ifndef VFD_ALERT_URL
#define VFD_ALERT_URL ""
#endif
#ifndef VFD_ERROR_SUMMARY
#define VFD_ERROR_SUMMARY "Frenic Mini VFD fault"
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
#define VFD_ERROR_SAMPLE_SIZE        5

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
byte currentZoneState = ZONES_OFF;

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
int vfdMode = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long wifiConnectionUpdate = 0;
bool pendingDisplayUpdate = false;
unsigned long lastInfluxSend = -3000000; // Track last InfluxDB send time
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
      delay(SOLENOID_HTTP_RETRY_DELAY_MS);
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
    delay(2);
  }
  return active_count >= 3;
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

  HTTPClient http;
  http.begin(VFD_ALERT_URL);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["error_summary"] = summary;
  doc["datetime"] = datetime;

  String body;
  serializeJson(doc, body);

  Serial.print("VFD alert POST ");
  Serial.println(body);

  int httpResponseCode = http.POST(body);
  bool success = (httpResponseCode >= 200 && httpResponseCode < 300);

  if (success) {
    Serial.print("VFD alert accepted: ");
    Serial.println(http.getString());
  } else {
    Serial.print("VFD alert failed with code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
  return success;
}

void update_vfd() {
  int  vfd_power = 0;

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
}


void start_timer() {
  wateringRunActive = false;

  if (timerMode != GREENHOUSE_ON && timerMode != CANON_ON) {
    if (!send_zone_command(timerMode)) {
      timerRunning = false;
      activeRunStartEpoch = 0;
      update_vfd();
      pendingDisplayUpdate = true;
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
  pendingDisplayUpdate = true;
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

  pendingDisplayUpdate = true;
}

void init_wifi() {
    Serial.print("Connecting to wifi ");

    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, primaryDNS)) {
        Serial.println("STA Failed to configure");
    }

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print (".");
    }
    Serial.println("\nWiFi connected");

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
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

void update_display_status(bool remote_signal, int zone_status, uint8_t vfd_mode) {
    char remote_status[10] = "OFF";
    char zone1_status[10] = "OFF";
    char zone2_status[10] = "OFF";
    char manual_status[10] = "OFF";
    char pump_status[10] = "OFF";
    char timer_status[200] = "zone timer off";

    char remaining[100];
    char duration[100];

    if (remote_signal) strcpy(remote_status, "ON");
    
    if (zone_status == ZONE1_ON) {
       strcpy(zone1_status, "ON");
    } else if (zone_status == ZONE2_ON) {
      strcpy(zone2_status, "ON");
    } else if (zone_status == GREENHOUSE_ON) {
      strcpy(manual_status, "GH");
    } else if (zone_status == All_ZONES_ON) {
      strcpy(zone1_status, "ON");
      strcpy(zone2_status, "ON");
    } else if (zone_status == CANON_ON) {
      strcpy(manual_status, "CANN");
    }
    if (vfdErrorActive) {
      strcpy(pump_status, "ERROR");
    } else if (vfd_mode == 1) {
      strcpy(pump_status, "HALF");
    } else if (vfd_mode == 2) {
      strcpy(pump_status, "FULL");
    }
    
    if (timerRunning) {
      format_millis(timerDuration - (millis() - timerStartTime), remaining);
      format_millis(timerDuration, duration);
      sprintf(timer_status, "%s of %s remaining", remaining, duration);
    }

    if (zone_status == ZONE_ERROR) {
      strcpy(timer_status, "Zone command ERROR");
    }

    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.fillScreen(GxEPD_WHITE);
    delay(10);
    display.drawExampleBitmap(logo_200_blk, 0, 0, 72, 128, GxEPD_BLACK);

    display.setCursor(90, 15);
    display.println("Remote:");
    display.setCursor(170, 15);
    display.println(remote_status);

    display.setCursor(90, 35);
    display.println("Zone 1:");
    display.setCursor(170, 35);
    display.println(zone1_status);

    display.setCursor(90, 55);
    display.println("Zone 2:"); 
    display.setCursor(170, 55);
    display.println(zone2_status);

    display.setCursor(90, 75);
    display.println("Manual:"); 
    display.setCursor(170, 75);
    display.println(manual_status);

    display.setCursor(90, 95);
    display.println(" VFD %:"); 
    display.setCursor(170, 95);
    display.println(pump_status);

    display.setCursor(20, 115);
    display.println(timer_status);

    display.update();
    lastDisplayUpdate = millis();
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

  init_wifi();
  web_server_init();
  setupInflux();
  update_display_status(false, false, ZONES_OFF);
}

void loop() {
  //  service web requests
  server.handleClient();

  // Handle timer
  if (timerRunning && (millis() - timerStartTime >= timerDuration)) {
    stop_timer();
  }

  // Remote pump: majority vote across several AC cycles (see REMOTE_SAMPLE_*)
  bool remote_on = read_remote_pump_input();
  if (!remoteSignalOn && remote_on) {
    remoteSignalOn = true;
    Serial.println("REMOTE OFF -> ON");
    update_vfd();
    pendingDisplayUpdate = true;
  } else if (remoteSignalOn && !remote_on) {
    remoteSignalOn = false;
    Serial.println("REMOTE ON -> OFF");
    update_vfd();
    pendingDisplayUpdate = true;
  }

  bool vfd_error_now = read_vfd_error_input();
  if (!vfdErrorActive && vfd_error_now) {
    vfdErrorActive = true;
    pendingDisplayUpdate = true;
    char datetime[40];
    format_event_datetime(datetime, sizeof(datetime));
    send_vfd_error_alert(VFD_ERROR_SUMMARY, datetime);
    Serial.println("VFD error detected");
  } else if (vfdErrorActive && !vfd_error_now) {
    vfdErrorActive = false;
    pendingDisplayUpdate = true;
    Serial.println("VFD error cleared");
  }

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

  if (millis() - wifiConnectionUpdate >  WIFI_CONNECTION_MILLIS) {
    wifiConnectionUpdate = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("ERROR: Wifi disconnected!");
      server.close();
      WiFi.disconnect();
      init_wifi();
      web_server_init();
    }
  }

  // Update the display
  if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_MILLIS) {
    pendingDisplayUpdate = true;
  }
  if (pendingDisplayUpdate) {
    pendingDisplayUpdate = false;
    update_display_status(remoteSignalOn, currentZoneState, vfdMode);
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