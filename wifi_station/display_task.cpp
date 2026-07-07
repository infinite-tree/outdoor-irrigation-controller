#include "display_task.h"

#include "station_shared.h"

#include "Logo.h"
#include <Adafruit_GFX.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <GxEPD.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef DISPLAY_TASK_STACK
#define DISPLAY_TASK_STACK 8192
#endif
#ifndef STATION_WATCHDOG_TIMEOUT_MS
#define STATION_WATCHDOG_TIMEOUT_MS 120000
#endif

extern GxEPD_Class display;
extern bool remoteSignalOn;
extern bool vfdErrorActive;
extern byte currentZoneState;
extern int vfdMode;
extern bool timerRunning;
extern unsigned long timerStartTime;
extern unsigned long timerDuration;

void format_millis(unsigned long milliseconds, char *buffer);

static TaskHandle_t display_task_handle = nullptr;

static void render_display_status() {
  char remote_status[10] = "OFF";
  char zone1_status[10] = "OFF";
  char zone2_status[10] = "OFF";
  char manual_status[10] = "OFF";
  char pump_status[10] = "OFF";
  char timer_status[200] = "zone timer off";
  char remaining[100];
  char duration[100];

  if (remoteSignalOn) {
    strcpy(remote_status, "ON");
  }

  if (currentZoneState == ZONE1_ON) {
    strcpy(zone1_status, "ON");
  } else if (currentZoneState == ZONE2_ON) {
    strcpy(zone2_status, "ON");
  } else if (currentZoneState == GREENHOUSE_ON) {
    strcpy(manual_status, "GH");
  } else if (currentZoneState == All_ZONES_ON) {
    strcpy(zone1_status, "ON");
    strcpy(zone2_status, "ON");
  } else if (currentZoneState == CANON_ON) {
    strcpy(manual_status, "CANN");
  }

  if (vfdErrorActive) {
    strcpy(pump_status, "ERROR");
  } else if (vfdMode == 1) {
    strcpy(pump_status, "HALF");
  } else if (vfdMode == 2) {
    strcpy(pump_status, "FULL");
  }

  if (timerRunning) {
    format_millis(timerDuration - (millis() - timerStartTime), remaining);
    format_millis(timerDuration, duration);
    snprintf(timer_status, sizeof(timer_status), "%s of %s remaining", remaining,
             duration);
  }

  if (currentZoneState == ZONE_ERROR) {
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
}

static void display_task(void *param) {
  (void)param;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (ulTaskNotifyTake(pdTRUE, 0) > 0) {
    }
    render_display_status();
  }
}

void display_task_start() {
  if (display_task_handle != nullptr) {
    return;
  }

  xTaskCreatePinnedToCore(display_task, "display", DISPLAY_TASK_STACK, nullptr, 1,
                          &display_task_handle, 1);
}

void display_task_request_refresh() {
  if (display_task_handle == nullptr) {
    return;
  }
  xTaskNotifyGive(display_task_handle);
}
