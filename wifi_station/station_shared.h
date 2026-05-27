#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <time.h>

#define ZONES_OFF           0x00
#define ZONE1_ON            0x01
#define ZONE2_ON            0x02
#define GREENHOUSE_ON       0x03
#define All_ZONES_ON        0x04
#define CANON_ON            0x05
#define ZONE_ERROR          0x99

#define MILLISECONDS          1000

extern WebServer server;

extern bool timerRunning;
extern bool wateringRunActive;
extern unsigned long timerStartTime;
extern unsigned long timerDuration;
extern bool webStartTimer;
extern bool webStopTimer;
extern time_t activeRunStartEpoch;
extern byte timerMode;

struct WateringRecord {
  time_t startEpoch;
  unsigned long durationMs;
};

extern WateringRecord lastWateringZ1;
extern WateringRecord lastWateringZ2;
extern WateringRecord lastWateringGH;
extern WateringRecord lastWateringWC;

void record_watering_for_mode(byte mode, time_t startEpoch, unsigned long durationMs);
extern byte currentZoneState;
extern int vfdMode;
extern bool remoteSignalOn;
extern bool vfdErrorActive;

void format_millis(unsigned long milliseconds, char *buffer);
String format_time_ago(unsigned long secondsAgo);
String format_duration(unsigned long milliseconds);

void web_server_init();
