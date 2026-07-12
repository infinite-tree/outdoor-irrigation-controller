#include "schedule.h"

#include "station_shared.h"

#include <ArduinoJson.h>
#include <Preferences.h>

static Preferences schedulePrefs;
static WaterSchedule schedules[MAX_SCHEDULES];
static size_t scheduleCount = 0;
static int lastCheckedMinuteKey = -1;
static bool schedulePersistPending = false;
static NextScheduledInfo cachedNext;
static time_t cachedNextComputedAt = 0;

static void schedule_invalidate_cache() {
  cachedNextComputedAt = 0;
}

static void schedule_zone_label_buf(uint8_t zone, char *buffer, size_t buflen) {
  const char *label = "Unknown";
  switch (zone) {
    case ZONE1_ON: label = "Zone 1"; break;
    case ZONE2_ON: label = "Zone 2"; break;
    case GREENHOUSE_ON: label = "Greenhouse"; break;
    case All_ZONES_ON: label = "Zones 1 & 2"; break;
    case CANON_ON: label = "Water cannon"; break;
  }
  snprintf(buffer, buflen, "%s", label);
}

static bool time_is_valid() {
  time_t now;
  time(&now);
  return now > 1704067200;
}

static time_t local_slot_time(int year, int month, int mday, int hour, int minute) {
  struct tm tm = {};
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = mday;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = 0;
  tm.tm_isdst = -1;
  return mktime(&tm);
}

static time_t start_of_local_day(time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  tm.tm_hour = 0;
  tm.tm_min = 0;
  tm.tm_sec = 0;
  tm.tm_isdst = -1;
  return mktime(&tm);
}

static int calendar_days_between(time_t fromSlot, time_t toDayStart) {
  time_t fromDay = start_of_local_day(fromSlot);
  return (int)((toDayStart - fromDay) / 86400);
}

static bool weekday_matches(uint8_t weekdays, int wday) {
  if (weekdays == 0) {
    return false;
  }
  return (weekdays >> wday) & 1;
}

static void format_hour_minute(int hour, int minute, char *buffer, size_t buflen) {
  const char *suffix = (hour >= 12) ? "PM" : "AM";
  int hour12 = hour % 12;
  if (hour12 == 0) {
    hour12 = 12;
  }
  snprintf(buffer, buflen, "%d:%02d %s", hour12, minute, suffix);
}

static void format_local_when(time_t epoch, char *buffer, size_t buflen) {
  time_t now;
  time(&now);
  time_t todayStart = start_of_local_day(now);
  time_t targetDay = start_of_local_day(epoch);

  struct tm tm;
  localtime_r(&epoch, &tm);
  char timePart[16];
  format_hour_minute(tm.tm_hour, tm.tm_min, timePart, sizeof(timePart));

  if (targetDay == todayStart) {
    snprintf(buffer, buflen, "Today %s", timePart);
  } else if (targetDay == todayStart + 86400) {
    snprintf(buffer, buflen, "Tomorrow %s", timePart);
  } else {
    static const char *DAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    snprintf(
      buffer,
      buflen,
      "%s %d/%d %s",
      DAY_NAMES[tm.tm_wday],
      tm.tm_mon + 1,
      tm.tm_mday,
      timePart
    );
  }
}

bool schedule_zone_valid(uint8_t zone) {
  return zone == ZONE1_ON || zone == ZONE2_ON || zone == GREENHOUSE_ON ||
         zone == All_ZONES_ON || zone == CANON_ON;
}

String schedule_zone_label(uint8_t zone) {
  switch (zone) {
    case ZONE1_ON: return "Zone 1";
    case ZONE2_ON: return "Zone 2";
    case GREENHOUSE_ON: return "Greenhouse";
    case All_ZONES_ON: return "Zones 1 & 2";
    case CANON_ON: return "Water cannon";
    default: return "Unknown";
  }
}

static bool schedule_slot_due(const WaterSchedule &sched, const struct tm &nowTm, time_t slotStart) {
  if (sched.lastFiredSlot >= slotStart) {
    return false;
  }

  if (sched.freqType == SCHED_FREQ_WEEKLY) {
    return weekday_matches(sched.weekdays, nowTm.tm_wday);
  }

  if (sched.freqType == SCHED_FREQ_INTERVAL) {
    if (sched.intervalDays < 1) {
      return false;
    }
    if (sched.lastFiredSlot == 0) {
      return true;
    }
    time_t todayStart = start_of_local_day(slotStart);
    return calendar_days_between(sched.lastFiredSlot, todayStart) >= sched.intervalDays;
  }

  return false;
}

static bool schedule_due_now(const WaterSchedule &sched, time_t now, const struct tm &nowTm) {
  if (!schedule_zone_valid(sched.zone) || sched.durationMin == 0) {
    return false;
  }
  if (nowTm.tm_hour != sched.hour || nowTm.tm_min != sched.minute) {
    return false;
  }

  time_t slotStart = local_slot_time(
    nowTm.tm_year + 1900,
    nowTm.tm_mon + 1,
    nowTm.tm_mday,
    sched.hour,
    sched.minute
  );
  return schedule_slot_due(sched, nowTm, slotStart);
}

static time_t slot_on_day(const struct tm &dayBase, int hour, int minute) {
  struct tm tm = dayBase;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = 0;
  tm.tm_isdst = -1;
  return mktime(&tm);
}

static time_t schedule_next_weekly(const WaterSchedule &sched, time_t afterEpoch) {
  struct tm base;
  localtime_r(&afterEpoch, &base);

  for (int dayOffset = 0; dayOffset < 8; dayOffset++) {
    struct tm dayTm = base;
    dayTm.tm_mday += dayOffset;
    time_t slotStart = slot_on_day(dayTm, sched.hour, sched.minute);
    if (slotStart <= afterEpoch) {
      continue;
    }

    struct tm slotTm;
    localtime_r(&slotStart, &slotTm);
    if (weekday_matches(sched.weekdays, slotTm.tm_wday)) {
      return slotStart;
    }
  }

  return 0;
}

static time_t schedule_next_interval(const WaterSchedule &sched, time_t afterEpoch) {
  struct tm base;
  localtime_r(&afterEpoch, &base);

  if (sched.lastFiredSlot == 0) {
    time_t todaySlot = slot_on_day(base, sched.hour, sched.minute);
    if (todaySlot > afterEpoch) {
      return todaySlot;
    }
    struct tm tomorrow = base;
    tomorrow.tm_mday += 1;
    return slot_on_day(tomorrow, sched.hour, sched.minute);
  }

  time_t nextDay = start_of_local_day(sched.lastFiredSlot);
  for (int attempt = 0; attempt < 400; attempt++) {
    nextDay += (time_t)sched.intervalDays * 86400;
    struct tm dayTm;
    localtime_r(&nextDay, &dayTm);
    time_t slotStart = slot_on_day(dayTm, sched.hour, sched.minute);
    if (slotStart > afterEpoch) {
      return slotStart;
    }
  }

  return 0;
}

static time_t schedule_next_after(const WaterSchedule &sched, time_t afterEpoch) {
  if (!schedule_zone_valid(sched.zone) || sched.durationMin == 0) {
    return 0;
  }
  if (sched.freqType == SCHED_FREQ_WEEKLY) {
    if (sched.weekdays == 0) {
      return 0;
    }
    return schedule_next_weekly(sched, afterEpoch);
  }
  if (sched.freqType == SCHED_FREQ_INTERVAL) {
    if (sched.intervalDays < 1) {
      return 0;
    }
    return schedule_next_interval(sched, afterEpoch);
  }
  return 0;
}

static uint16_t schedule_next_id() {
  uint16_t maxId = 0;
  for (size_t i = 0; i < scheduleCount; i++) {
    if (schedules[i].id > maxId) {
      maxId = schedules[i].id;
    }
  }
  return maxId + 1;
}

static void schedule_persist() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (size_t i = 0; i < scheduleCount; i++) {
    const WaterSchedule &s = schedules[i];
    JsonObject row = arr.add<JsonObject>();
    row["id"] = s.id;
    row["zone"] = s.zone;
    row["duration"] = s.durationMin;
    row["hour"] = s.hour;
    row["minute"] = s.minute;
    row["freq"] = (s.freqType == SCHED_FREQ_WEEKLY) ? "weekly" : "interval";
    row["interval_days"] = s.intervalDays;
    row["weekdays"] = s.weekdays;
    row["last_fired_slot"] = (long)s.lastFiredSlot;
  }

  String json;
  serializeJson(doc, json);
  if (json.isEmpty() && scheduleCount > 0) {
    Serial.println("schedule_persist: serialize failed");
    return;
  }
  schedulePrefs.putString("data", json);
  schedule_invalidate_cache();
}

void schedule_process_pending() {
  if (!schedulePersistPending) {
    return;
  }
  schedulePersistPending = false;
  schedule_persist();
}

static void schedule_load() {
  scheduleCount = 0;
  String json = schedulePrefs.getString("data", "");
  if (json.isEmpty()) {
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.println("schedule_load: invalid JSON, clearing saved schedules");
    schedulePrefs.remove("data");
    return;
  }

  if (!doc.is<JsonArray>()) {
    Serial.println("schedule_load: expected array, clearing saved schedules");
    schedulePrefs.remove("data");
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject row : arr) {
    if (scheduleCount >= MAX_SCHEDULES) {
      break;
    }

    WaterSchedule s = {};
    s.id = row["id"] | schedule_next_id();
    s.zone = row["zone"] | 0;
    s.durationMin = row["duration"] | 0;
    s.hour = row["hour"] | 0;
    s.minute = row["minute"] | 0;
    const char *freq = row["freq"] | "weekly";
    s.freqType = (strcmp(freq, "interval") == 0) ? SCHED_FREQ_INTERVAL : SCHED_FREQ_WEEKLY;
    s.intervalDays = row["interval_days"] | 1;
    if (row["weekdays"].is<JsonArray>()) {
      uint8_t mask = 0;
      int idx = 0;
      for (JsonVariant v : row["weekdays"].as<JsonArray>()) {
        if (idx < 7 && v.as<int>() != 0) {
          mask |= (1 << idx);
        }
        idx++;
      }
      s.weekdays = mask;
    } else {
      s.weekdays = row["weekdays"] | 0;
    }
    s.lastFiredSlot = row["last_fired_slot"] | 0;

    if (!schedule_zone_valid(s.zone) || s.durationMin == 0 || s.hour > 23 || s.minute > 59) {
      continue;
    }
    schedules[scheduleCount++] = s;
  }
}

static void schedule_json_row(const WaterSchedule &s, JsonObject row) {
  row["id"] = s.id;
  row["zone"] = s.zone;
  row["duration"] = s.durationMin;
  row["hour"] = s.hour;
  row["minute"] = s.minute;
  row["freq"] = (s.freqType == SCHED_FREQ_WEEKLY) ? "weekly" : "interval";
  row["interval_days"] = s.intervalDays;

  JsonArray days = row["weekdays"].to<JsonArray>();
  for (int d = 0; d < 7; d++) {
    days.add(((s.weekdays >> d) & 1) ? 1 : 0);
  }
}

void schedule_init() {
  schedulePrefs.begin("irr_sched", false);
  schedule_load();
}

size_t schedule_count() {
  return scheduleCount;
}

const WaterSchedule *schedule_get(size_t index) {
  if (index >= scheduleCount) {
    return nullptr;
  }
  return &schedules[index];
}

bool schedule_replace_all(const char *jsonBody, String &errorOut) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, jsonBody);
  if (err) {
    errorOut = "Invalid JSON";
    return false;
  }

  if (!doc.is<JsonArray>()) {
    errorOut = "Expected JSON array";
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() > MAX_SCHEDULES) {
    errorOut = "Too many schedules";
    return false;
  }

  WaterSchedule next[MAX_SCHEDULES];
  size_t nextCount = 0;
  uint16_t nextId = schedule_next_id();

  for (JsonObject row : arr) {
    WaterSchedule s = {};
    s.id = row["id"] | 0;
    s.zone = row["zone"] | 0;
    s.durationMin = row["duration"] | 0;
    s.hour = row["hour"] | 0;
    s.minute = row["minute"] | 0;
    const char *freq = row["freq"] | "";
    if (strcmp(freq, "interval") == 0) {
      s.freqType = SCHED_FREQ_INTERVAL;
      s.intervalDays = row["interval_days"] | 0;
      s.weekdays = 0;
      if (s.intervalDays < 1 || s.intervalDays > 365) {
        errorOut = "interval_days must be 1-365";
        return false;
      }
    } else if (strcmp(freq, "weekly") == 0) {
      s.freqType = SCHED_FREQ_WEEKLY;
      s.intervalDays = 0;
      if (row["weekdays"].is<JsonArray>()) {
        uint8_t mask = 0;
        int idx = 0;
        for (JsonVariant v : row["weekdays"].as<JsonArray>()) {
          if (idx < 7 && v.as<int>() != 0) {
            mask |= (1 << idx);
          }
          idx++;
        }
        s.weekdays = mask;
      } else {
        s.weekdays = row["weekdays"] | 0;
      }
      if (s.weekdays == 0) {
        errorOut = "Select at least one weekday";
        return false;
      }
    } else {
      errorOut = "freq must be weekly or interval";
      return false;
    }

    if (!schedule_zone_valid(s.zone)) {
      errorOut = "Invalid zone";
      return false;
    }
    if (s.durationMin < 1 || s.durationMin > 24 * 60) {
      errorOut = "Invalid duration";
      return false;
    }
    if (s.hour > 23 || s.minute > 59) {
      errorOut = "Invalid time";
      return false;
    }

    for (size_t i = 0; i < scheduleCount; i++) {
      if (schedules[i].id == s.id) {
        s.lastFiredSlot = schedules[i].lastFiredSlot;
        break;
      }
    }

    if (s.id == 0) {
      s.id = nextId++;
    }

    next[nextCount++] = s;
  }

  scheduleCount = nextCount;
  for (size_t i = 0; i < scheduleCount; i++) {
    schedules[i] = next[i];
  }
  schedule_invalidate_cache();
  schedulePersistPending = true;
  return true;
}

void schedule_get_next(NextScheduledInfo &info) {
  info.valid = false;
  info.epoch = 0;
  info.label[0] = '\0';
  info.when[0] = '\0';

  if (!time_is_valid() || scheduleCount == 0) {
    return;
  }

  time_t now;
  time(&now);
  if (cachedNextComputedAt > 0 && (now - cachedNextComputedAt) < 300) {
    info = cachedNext;
    return;
  }

  time_t best = 0;
  size_t bestIndex = 0;
  for (size_t i = 0; i < scheduleCount; i++) {
    time_t candidate = schedule_next_after(schedules[i], now);
    if (candidate == 0) {
      continue;
    }
    if (best == 0 || candidate < best) {
      best = candidate;
      bestIndex = i;
    }
  }

  if (best == 0) {
    cachedNext = info;
    cachedNextComputedAt = now;
    return;
  }

  const WaterSchedule &s = schedules[bestIndex];
  info.valid = true;
  info.epoch = best;
  char zoneLabel[24];
  schedule_zone_label_buf(s.zone, zoneLabel, sizeof(zoneLabel));
  snprintf(info.label, sizeof(info.label), "%s · %u min", zoneLabel, s.durationMin);
  format_local_when(best, info.when, sizeof(info.when));
  cachedNext = info;
  cachedNextComputedAt = now;
}

void schedule_tick(bool timerRunning) {
  if (!time_is_valid() || timerRunning) {
    return;
  }

  time_t now;
  time(&now);
  struct tm nowTm;
  localtime_r(&now, &nowTm);
  int minuteKey = (nowTm.tm_yday * 24 * 60) + (nowTm.tm_hour * 60) + nowTm.tm_min;
  if (minuteKey == lastCheckedMinuteKey) {
    return;
  }
  lastCheckedMinuteKey = minuteKey;

  for (size_t i = 0; i < scheduleCount; i++) {
    WaterSchedule &sched = schedules[i];
    if (!schedule_due_now(sched, now, nowTm)) {
      continue;
    }

    time_t slotStart = local_slot_time(
      nowTm.tm_year + 1900,
      nowTm.tm_mon + 1,
      nowTm.tm_mday,
      sched.hour,
      sched.minute
    );

    Serial.printf(
      "Schedule %u fired: zone=%u duration=%u\n",
      sched.id,
      sched.zone,
      sched.durationMin
    );

    webStopTimer = false;
    timerDuration = (unsigned long)sched.durationMin * 60UL * MILLISECONDS;
    timerMode = sched.zone;
    if (!start_timer()) {
      Serial.printf("Schedule %u start failed; slot not marked fired\n", sched.id);
      return;
    }

    sched.lastFiredSlot = slotStart;
    schedule_persist();
    return;
  }
}

void schedule_append_status_json(JsonDocument &doc) {
  NextScheduledInfo next;
  schedule_get_next(next);
  JsonObject nextObj = doc["next_scheduled"].to<JsonObject>();
  if (!next.valid) {
    nextObj["active"] = false;
    return;
  }
  nextObj["active"] = true;
  nextObj["epoch"] = (long)next.epoch;
  nextObj["label"] = next.label;
  nextObj["when"] = next.when;
}

void schedule_append_list_json(JsonArray &arr) {
  for (size_t i = 0; i < scheduleCount; i++) {
    JsonObject row = arr.add<JsonObject>();
    if (row.isNull()) {
      Serial.println("schedule_append_list_json: out of memory");
      return;
    }
    schedule_json_row(schedules[i], row);
  }
}
