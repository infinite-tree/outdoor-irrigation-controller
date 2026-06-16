#include "solenoid_shared.h"
#include "web_assets.h"
#include "ble_pressure.h"

#include <ArduinoJson.h>

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
    doc["pressure_error"] = "not read yet";
    return;
  }

  BlePressureReading reading = ble_pressure_get_cached();
  doc["pressure_valid"] = reading.ok;
  if (reading.ok) {
    doc["pressure_psi"] = (int)reading.psi;
    if (reading.battery_valid) {
      doc["pressure_battery_pct"] = reading.battery_pct;
    }
  } else if (reading.error != nullptr) {
    doc["pressure_error"] = reading.error;
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
  webAction = true;
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
}

void web_server_init() {
  Serial.print("Starting webserver ...");
  server.on("/", web_handle_index);
  server.on("/index.html", web_handle_index);
  server.on("/style.css", web_handle_style);
  server.on("/app.js", web_handle_app_js);
  server.on("/status", HTTP_GET, web_handle_get_status);
  server.on("/set_zone", HTTP_POST, web_handle_set_zone);
  server.begin();
  Serial.println("HTTP server started");
}
