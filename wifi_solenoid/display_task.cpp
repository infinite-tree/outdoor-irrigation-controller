#include "display_task.h"

#include "ble_pressure.h"
#include "solenoid_shared.h"

#include "Logo.h"
#include <Adafruit_GFX.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <GxEPD.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef DISPLAY_TASK_STACK
#define DISPLAY_TASK_STACK 8192
#endif
#ifndef SOLENOID_WATCHDOG_TIMEOUT_MS
#define SOLENOID_WATCHDOG_TIMEOUT_MS 120000
#endif

extern GxEPD_Class display;
extern byte zoneStatus;

#define ZONES_OFF 0x00
#define ZONE1_ON 0x01
#define ZONE2_ON 0x02
#define All_ZONES_ON 0x03

static TaskHandle_t display_task_handle = nullptr;

static void render_display_status() {
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
    if (ble_pressure_is_stale()) {
      snprintf(pressure_text, sizeof(pressure_text), "~%d psi", (int)reading.psi);
    } else {
      snprintf(pressure_text, sizeof(pressure_text), "%d psi", (int)reading.psi);
    }
    if (reading.battery_valid) {
      snprintf(battery_text, sizeof(battery_text), "%d%%", reading.battery_pct);
    } else {
      strcpy(battery_text, "--");
    }
  }

  display.setCursor(90, 75);
  display.println("Press:");
  display.setCursor(182, 75);
  display.println(pressure_text);

  display.setCursor(90, 95);
  display.println("Battery:");
  display.setCursor(182, 95);
  display.println(battery_text);

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
