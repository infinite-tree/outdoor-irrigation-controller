#include "station_shared.h"
#include "web_assets.h"

#include <ArduinoJson.h>

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

static void web_handle_get_status() {
  char remaining[100];
  char duration[100];

  JsonDocument doc;
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
    String nowMain = mode_label(timerMode) + " · " + remaining + " left";
    if (vfdErrorActive) {
      nowMain = "VFD fault · " + nowMain;
    }
    doc["now_main"] = nowMain;
    String nowSub = String(duration) + " total · Pump " + vfd_label(vfdMode);
    if (vfdErrorActive) {
      nowSub = "VFD drive error · " + nowSub;
    }
    if (remoteSignalOn) {
      nowSub += " · Remote on";
    }
    doc["now_sub"] = nowSub;
    doc["remaining_seconds"] = (timerDuration - (millis() - timerStartTime)) / MILLISECONDS;
    doc["duration_seconds"] = timerDuration / MILLISECONDS;
    doc["watering_started_at"] = activeRunStartEpoch;
  } else {
    doc["now_main"] = vfdErrorActive ? "VFD fault" : "Not watering";
    String nowSub = "Pump " + vfd_label(vfdMode);
    if (vfdErrorActive) {
      nowSub = "VFD drive error · " + nowSub;
    }
    if (remoteSignalOn) {
      nowSub += " · Remote on";
    }
    doc["now_sub"] = nowSub;
  }

  doc["timer_mode"] = timerMode;
  doc["solenoid_state"] = currentZoneState;
  doc["remote_signal_on"] = remoteSignalOn;
  doc["vfd"] = vfdMode;

  JsonObject history = doc["last_watering"].to<JsonObject>();
  json_append_watering_row(history, "z1", lastWateringZ1);
  json_append_watering_row(history, "z2", lastWateringZ2);
  json_append_watering_row(history, "gh", lastWateringGH);
  json_append_watering_row(history, "wc", lastWateringWC);

  String jsonString;
  serializeJson(doc, jsonString);
  server.send(200, "application/json", jsonString);
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

void web_server_init() {
  Serial.print("Starting webserver ...");
  server.on("/", web_handle_index);
  server.on("/index.html", web_handle_index);
  server.on("/style.css", web_handle_style);
  server.on("/app.js", web_handle_app_js);
  server.on("/message.html", web_handle_message);
  server.on("/status", HTTP_GET, web_handle_get_status);
  server.on("/start", HTTP_POST, web_handle_start_timer);
  server.on("/stop", HTTP_POST, web_handle_stop_timer);
  server.begin();
  Serial.println("HTTP server started");
}
