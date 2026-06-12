#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>

#define MAX_SCHEDULES       12
#define SCHED_FREQ_INTERVAL 0
#define SCHED_FREQ_WEEKLY   1

struct WaterSchedule {
  uint16_t id;
  uint8_t zone;
  uint16_t durationMin;
  uint8_t hour;
  uint8_t minute;
  uint8_t freqType;
  uint8_t intervalDays;
  uint8_t weekdays;
  time_t lastFiredSlot;
};

struct NextScheduledInfo {
  bool valid;
  time_t epoch;
  char label[64];
  char when[48];
};

bool schedule_zone_valid(uint8_t zone);
String schedule_zone_label(uint8_t zone);

void schedule_init();
size_t schedule_count();
const WaterSchedule *schedule_get(size_t index);
bool schedule_replace_all(const char *jsonBody, String &errorOut);
void schedule_tick(bool timerRunning);
void schedule_process_pending();
void schedule_get_next(NextScheduledInfo &info);
void schedule_append_status_json(JsonDocument &doc);
void schedule_append_list_json(JsonArray &arr);
