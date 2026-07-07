#include "solenoid_shared.h"
#include "web_assets.h"
#include "ble_pressure.h"
#include "display_task.h"

#include <ArduinoJson.h>

#ifndef SOLENOID_WATCHDOG_TIMEOUT_MS
#define SOLENOID_WATCHDOG_TIMEOUT_MS 120000
#endif

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

static void append_pressure_json(JsonDocument &doc) {
  doc["pressure_enabled"] = ble_pressure_enabled();
  if (!ble_pressure_enabled()) {
    return;
  }

  if (!ble_pressure_has_cache()) {
    doc["pressure_valid"] = false;
    doc["pressure_stale"] = false;
    const char *error = ble_pressure_last_error();
    doc["pressure_error"] = error != nullptr ? error : "not read yet";
    return;
  }

  const BlePressureReading reading = ble_pressure_get_cached();
  const bool fresh = ble_pressure_is_fresh();
  const bool stale = ble_pressure_is_stale();

  doc["pressure_valid"] = fresh;
  doc["pressure_stale"] = stale;
  doc["pressure_psi"] = (int)reading.psi;
  doc["pressure_raw"] = reading.raw;
  if (ble_pressure_has_cache()) {
    const unsigned long age_sec = ble_pressure_last_success_age_sec();
    if (age_sec != ULONG_MAX) {
      doc["pressure_read_seconds_ago"] = age_sec;
    }
    const time_t read_epoch = ble_pressure_last_success_epoch();
    if (read_epoch > 100000) {
      doc["pressure_read_epoch"] = (long)read_epoch;
    }
  }
  if (reading.battery_valid) {
    doc["pressure_battery_pct"] = reading.battery_pct;
  }
  if (stale) {
    const char *error = ble_pressure_last_error();
    if (error != nullptr) {
      doc["pressure_error"] = error;
    }
  }
}

static void web_handle_get_status() {
  JsonDocument doc;
  doc["zone1_on"] = zone1On;
  doc["zone2_on"] = zone2On;

  JsonObject zones = doc["zones"].to<JsonObject>();
  zones["z1"] = zone1On ? "on" : "off";
  zones["z2"] = zone2On ? "on" : "off";

  append_pressure_json(doc);

  String jsonString;
  serializeJson(doc, jsonString);
  server.send(200, "application/json", jsonString);
}

static void web_handle_set_zone() {
  if (!server.hasArg("zone1") || !server.hasArg("zone2")) {
    server.send(400, "text/plain", "Missing parameters");
    return;
  }

  int zone1 = server.arg("zone1").toInt();
  int zone2 = server.arg("zone2").toInt();

  if (zone1 == 0) {
    close_solenoid(ZONE1);
  } else if (zone1 == 1) {
    open_solenoid(ZONE1);
  }

  if (zone2 == 0) {
    close_solenoid(ZONE2);
  } else if (zone2 == 1) {
    open_solenoid(ZONE2);
  }

  server.send(200, "text/plain", "Zones set");
  display_task_request_refresh();
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
  doc["zone1_on"] = zone1On;
  doc["zone2_on"] = zone2On;
  doc["pressure_enabled"] = ble_pressure_enabled();
  if (ble_pressure_enabled()) {
    doc["pressure_valid"] = ble_pressure_is_fresh();
    doc["pressure_stale"] = ble_pressure_is_stale();
    if (ble_pressure_has_cache()) {
      const BlePressureReading reading = ble_pressure_get_cached();
      doc["pressure_psi"] = (int)reading.psi;
      if (reading.battery_valid) {
        doc["pressure_battery_pct"] = reading.battery_pct;
      }
      const unsigned long age_sec = ble_pressure_last_success_age_sec();
      if (age_sec != ULONG_MAX) {
        doc["pressure_read_seconds_ago"] = age_sec;
      }
    }
  }
  doc["watchdog_timeout_sec"] = SOLENOID_WATCHDOG_TIMEOUT_MS / 1000;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void web_server_init() {
  Serial.print("Starting webserver ...");
  server.on("/", web_handle_index);
  server.on("/index.html", web_handle_index);
  server.on("/style.css", web_handle_style);
  server.on("/app.js", web_handle_app_js);
  server.on("/status", HTTP_GET, web_handle_get_status);
  server.on("/health", HTTP_GET, web_handle_health);
  server.on("/set_zone", HTTP_POST, web_handle_set_zone);
  server.begin();
  Serial.println("HTTP server started");
}

void web_server_poll() {
  server.handleClient();
}
