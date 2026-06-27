#include "station_shared.h"
#include "web_assets.h"
#include "schedule.h"

#include <ArduinoJson.h>

#ifndef STATION_WATCHDOG_TIMEOUT_MS
#define STATION_WATCHDOG_TIMEOUT_MS 120000
#endif

#define UI_STATUS_OFF   0
#define UI_STATUS_ON    1
#define UI_STATUS_ERROR 2

static bool mode_includes_zone1(byte mode) {
  return mode == ZONE1_ON || mode == All_ZONES_ON;
}

static bool mode_includes_zone2(byte mode) {
  return mode == ZONE2_ON || mode == All_ZONES_ON;
}

static String mode_label(byte mode) {
  switch (mode) {
    case ZONE1_ON: return "Zone 1";
    case ZONE2_ON: return "Zone 2";
    case GREENHOUSE_ON: return "Greenhouse";
    case All_ZONES_ON: return "Zones 1 & 2";
    case CANON_ON: return "Water cannon";
    case ZONE_ERROR: return "Solenoid error";
    default: return "Idle";
  }
}

static uint8_t ui_solenoid_zone1_status() {
  if (currentZoneState == ZONE_ERROR && mode_includes_zone1(timerMode)) {
    return UI_STATUS_ERROR;
  }
  if (currentZoneState == ZONE1_ON || currentZoneState == All_ZONES_ON) {
    return UI_STATUS_ON;
  }
  return UI_STATUS_OFF;
}

static uint8_t ui_solenoid_zone2_status() {
  if (currentZoneState == ZONE_ERROR && mode_includes_zone2(timerMode)) {
    return UI_STATUS_ERROR;
  }
  if (currentZoneState == ZONE2_ON || currentZoneState == All_ZONES_ON) {
    return UI_STATUS_ON;
  }
  return UI_STATUS_OFF;
}

static uint8_t ui_local_mode_status(byte mode) {
  if (currentZoneState == ZONE_ERROR && timerMode == mode) {
    return UI_STATUS_ERROR;
  }
  if (timerRunning && timerMode == mode) {
    return UI_STATUS_ON;
  }
  return UI_STATUS_OFF;
}

static const char *ui_status_key(uint8_t status) {
  if (status == UI_STATUS_ON) return "on";
  if (status == UI_STATUS_ERROR) return "error";
  return "off";
}

static String vfd_label(int mode) {
  if (vfdErrorActive) return "Error";
  if (mode == 2) return "Full";
  if (mode == 1) return "Half";
  return "Off";
}

static void json_append_watering_row(JsonObject parent, const char *key, const WateringRecord &record) {
  JsonObject row = parent[key].to<JsonObject>();
  if (record.startEpoch == 0) {
    row["when"] = "Never";
    row["duration"] = "—";
    return;
  }

  time_t now;
  time(&now);
  unsigned long ago = (now >= record.startEpoch) ? (unsigned long)(now - record.startEpoch) : 0;
  row["when"] = format_time_ago(ago);
  row["duration"] = (record.durationMs > 0) ? format_duration(record.durationMs) : "0s";
}

static void web_serve_progmem(const char *content_type, const char *body) {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send_P(200, content_type, body);
}

static void web_handle_index() {
  web_serve_progmem("text/html", WEB_INDEX_HTML);
}

static void web_handle_style() {
  web_serve_progmem("text/css", WEB_STYLE_CSS);
}

static void web_handle_app_js() {
  web_serve_progmem("application/javascript", WEB_APP_JS);
}

static void web_handle_message() {
  web_serve_progmem("text/html", WEB_MESSAGE_HTML);
}

static void format_clock_label(char *buffer, size_t buflen) {
  time_t now;
  time(&now);
  struct tm tm;
  localtime_r(&now, &tm);

  if (tm.tm_year < 120) {
    snprintf(buffer, buflen, "—");
    return;
  }

  static const char *DAYS[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
  };
  static const char *MONTHS[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };

  const char *suffix = (tm.tm_hour >= 12) ? "pm" : "am";
  int hour12 = tm.tm_hour % 12;
  if (hour12 == 0) {
    hour12 = 12;
  }

  snprintf(
    buffer,
    buflen,
    "%s, %s %d · %d:%02d %s",
    DAYS[tm.tm_wday],
    MONTHS[tm.tm_mon],
    tm.tm_mday,
    hour12,
    tm.tm_min,
    suffix
  );
}

static void web_handle_get_status() {
  request_pressure_for_status_refresh();

  char remaining[100];
  char duration[100];
  char nowMain[160];
  char nowSub[160];
  char clockLabel[64];

  static JsonDocument doc;
  doc.clear();

  format_clock_label(clockLabel, sizeof(clockLabel));
  doc["clock"] = clockLabel;

  doc["timer_running"] = timerRunning;
  doc["solenoid_error"] = (currentZoneState == ZONE_ERROR);
  doc["vfd_error"] = vfdErrorActive;

  JsonObject zones = doc["zones"].to<JsonObject>();
  zones["z1"] = ui_status_key(ui_solenoid_zone1_status());
  zones["z2"] = ui_status_key(ui_solenoid_zone2_status());
  zones["gh"] = ui_status_key(ui_local_mode_status(GREENHOUSE_ON));
  zones["wc"] = ui_status_key(ui_local_mode_status(CANON_ON));

  if (timerRunning) {
    format_millis(timerDuration - (millis() - timerStartTime), remaining);
    format_millis(timerDuration, duration);
    if (vfdErrorActive) {
      snprintf(nowMain, sizeof(nowMain), "VFD fault · %s · %s left", mode_label(timerMode).c_str(), remaining);
    } else {
      snprintf(nowMain, sizeof(nowMain), "%s · %s left", mode_label(timerMode).c_str(), remaining);
    }
    if (vfdErrorActive) {
      snprintf(nowSub, sizeof(nowSub), "VFD drive error · %s total · Pump %s", duration, vfd_label(vfdMode).c_str());
    } else {
      snprintf(nowSub, sizeof(nowSub), "%s total · Pump %s", duration, vfd_label(vfdMode).c_str());
    }
    if (remoteSignalOn) {
      strlcat(nowSub, " · Remote on", sizeof(nowSub));
    }
    doc["now_main"] = nowMain;
    doc["now_sub"] = nowSub;
    doc["remaining_seconds"] = (timerDuration - (millis() - timerStartTime)) / MILLISECONDS;
    doc["duration_seconds"] = timerDuration / MILLISECONDS;
    doc["watering_started_at"] = activeRunStartEpoch;
  } else {
    if (vfdErrorActive) {
      snprintf(nowMain, sizeof(nowMain), "VFD fault");
    } else {
      snprintf(nowMain, sizeof(nowMain), "Not watering");
    }
    if (vfdErrorActive) {
      snprintf(nowSub, sizeof(nowSub), "VFD drive error · Pump %s", vfd_label(vfdMode).c_str());
    } else {
      snprintf(nowSub, sizeof(nowSub), "Pump %s", vfd_label(vfdMode).c_str());
    }
    if (remoteSignalOn) {
      strlcat(nowSub, " · Remote on", sizeof(nowSub));
    }
    doc["now_main"] = nowMain;
    doc["now_sub"] = nowSub;
  }

  doc["timer_mode"] = timerMode;
  doc["solenoid_state"] = currentZoneState;
  doc["remote_signal_on"] = remoteSignalOn;
  doc["vfd"] = vfdMode;
  doc["pressure_valid"] = solenoidPressureValid;
  doc["pressure_stale"] = solenoidPressureStale;
  if (solenoidPressureValid || solenoidPressureStale) {
    doc["pressure_psi"] = (int)solenoidPressurePsi;
  }
  if (solenoidBatteryValid) {
    doc["pressure_battery_pct"] = solenoidBatteryPct;
  }
  doc["pressure_low_alarm"] = solenoidPressureLowAlarm;
  doc["pressure_high_alarm"] = solenoidPressureHighAlarm;
  doc["pressure_battery_low_alarm"] = solenoidBatteryLowAlarm;
  if (lastPressurePoll != 0) {
    doc["pressure_poll_seconds_ago"] = (millis() - lastPressurePoll) / 1000;
  }

  JsonObject history = doc["last_watering"].to<JsonObject>();
  json_append_watering_row(history, "z1", lastWateringZ1);
  json_append_watering_row(history, "z2", lastWateringZ2);
  json_append_watering_row(history, "gh", lastWateringGH);
  json_append_watering_row(history, "wc", lastWateringWC);
  json_append_watering_row(history, "remote", lastWateringRemote);

  schedule_append_status_json(doc);

  static char jsonBuffer[3072];
  size_t jsonLen = serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));
  if (jsonLen == 0 || jsonLen >= sizeof(jsonBuffer)) {
    server.send(500, "text/plain", "Status JSON too large");
    return;
  }
  server.send(200, "application/json", jsonBuffer);
}

static void web_handle_start_timer() {
  if (server.hasArg("duration") && server.hasArg("zone")) {
    int duration = server.arg("duration").toInt();
    int zone = server.arg("zone").toInt();

    if (duration > 0 && (zone == ZONE1_ON || zone == ZONE2_ON || zone == GREENHOUSE_ON || zone == All_ZONES_ON || zone == CANON_ON)) {
      if (timerRunning) {
        server.send(409, "text/plain", "Already watering");
        return;
      }
      webStopTimer = false;
      timerDuration = duration * 60 * MILLISECONDS;
      timerMode = zone;
      webStartTimer = true;
      server.sendHeader("Location", "/message.html?m=Watering%20started", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(400, "text/plain", "Invalid duration or zone");
    }
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

static void web_handle_stop_timer() {
  webStartTimer = false;
  webStopTimer = true;
  server.sendHeader("Location", "/message.html?m=Watering%20stopped", true);
  server.send(302, "text/plain", "");
}

static void web_handle_get_schedules() {
  static JsonDocument doc;
  doc.clear();
  JsonArray arr = doc["schedules"].to<JsonArray>();
  schedule_append_list_json(arr);

  static char jsonBuffer[2048];
  size_t jsonLen = serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));
  if (jsonLen == 0 || jsonLen >= sizeof(jsonBuffer)) {
    server.send(500, "text/plain", "Failed to encode schedules");
    return;
  }
  server.send(200, "application/json", jsonBuffer);
}

static void web_handle_put_schedules() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing body");
    return;
  }

  String body = server.arg("plain");
  if (body.isEmpty()) {
    server.send(400, "text/plain", "Missing body");
    return;
  }

  String errorOut;
  if (!schedule_replace_all(body.c_str(), errorOut)) {
    server.send(400, "text/plain", errorOut);
    return;
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

static void web_handle_health() {
  static JsonDocument doc;
  doc.clear();

  doc["uptime_sec"] = millis() / 1000;
  doc["heap_free"] = ESP.getFreeHeap();
  doc["wifi_connected"] = WiFi.status() == WL_CONNECTED;
  if (WiFi.status() == WL_CONNECTED) {
    doc["wifi_rssi"] = WiFi.RSSI();
  }
  doc["pump_active"] = pump_is_active();
  doc["vfd"] = vfdMode;
  doc["remote_signal_on"] = remoteSignalOn;
  doc["timer_running"] = timerRunning;
  doc["pressure_valid"] = solenoidPressureValid;
  doc["pressure_stale"] = solenoidPressureStale;
  if (solenoidPressureValid || solenoidPressureStale) {
    doc["pressure_psi"] = (int)solenoidPressurePsi;
  }
  if (lastPressurePoll != 0) {
    doc["pressure_poll_seconds_ago"] = (millis() - lastPressurePoll) / 1000;
  }
  doc["watchdog_timeout_sec"] = STATION_WATCHDOG_TIMEOUT_MS / 1000;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

static bool webRoutesRegistered = false;
static bool webServerStarted = false;

void web_server_init() {
  if (!webRoutesRegistered) {
    server.on("/", web_handle_index);
    server.on("/index.html", web_handle_index);
    server.on("/style.css", web_handle_style);
    server.on("/app.js", web_handle_app_js);
    server.on("/message.html", web_handle_message);
    server.on("/status", HTTP_GET, web_handle_get_status);
    server.on("/health", HTTP_GET, web_handle_health);
    server.on("/start", HTTP_POST, web_handle_start_timer);
    server.on("/stop", HTTP_POST, web_handle_stop_timer);
    server.on("/schedules", HTTP_GET, web_handle_get_schedules);
    server.on("/schedules", HTTP_PUT, web_handle_put_schedules);
    webRoutesRegistered = true;
  }

  if (webServerStarted) {
    return;
  }

  Serial.print("Starting webserver ...");
  server.begin();
  webServerStarted = true;
  Serial.println("HTTP server started");
}
