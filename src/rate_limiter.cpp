#include "rate_limiter.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <algorithm>

RateLimiter rateLimiter;

#define RL_JSON_PATH "/ratelimit.json"

void RateLimiter::begin(bool enabled, int maxPerHour, int maxPerDay) {
    _enabled    = enabled;
    _maxPerHour = std::max(1, maxPerHour);
    _maxPerDay  = std::max(1, maxPerDay);
    load();
    Serial.printf("[RL] enabled=%d %d/hr %d/day; today=%d\n",
                  _enabled, _maxPerHour, _maxPerDay, _reqDay);
}

void RateLimiter::updateWindow() {
    struct tm ti;
    if (!getLocalTime(&ti, 10)) return;

    if (ti.tm_hour != _lastHour) {
        _reqHour  = 0;
        _lastHour = ti.tm_hour;
    }
    if (ti.tm_yday != _lastYday) {
        _reqDay   = 0;
        _lastYday = ti.tm_yday;
        persist();
    }
}

bool RateLimiter::check(char* reason, size_t reasonLen) {
    if (!_enabled) return true;
    updateWindow();
    if (_reqHour >= _maxPerHour) {
        snprintf(reason, reasonLen,
                 "Rate limit: %d/%d requests this hour. Try again later.",
                 _reqHour, _maxPerHour);
        return false;
    }
    if (_reqDay >= _maxPerDay) {
        snprintf(reason, reasonLen,
                 "Daily limit: %d/%d requests today. Resets at midnight.",
                 _reqDay, _maxPerDay);
        return false;
    }
    return true;
}

void RateLimiter::recordRequest() {
    updateWindow();
    _reqHour++;
    _reqDay++;
    if (_reqDay % 5 == 0) persist();
}

void RateLimiter::reset() {
    _reqHour = 0;
    _reqDay  = 0;
    persist();
}

void RateLimiter::persist() {
    JsonDocument doc;
    doc["day"]  = _reqDay;
    doc["yday"] = _lastYday;
    File f = LittleFS.open(RL_JSON_PATH, "w");
    if (!f) return;
    serializeJson(doc, f);
    f.close();
}

void RateLimiter::load() {
    File f = LittleFS.open(RL_JSON_PATH, "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        _reqDay   = doc["day"]  | 0;
        _lastYday = doc["yday"] | -1;
    }
    f.close();
    // If the stored yday differs from today, reset
    struct tm ti;
    if (getLocalTime(&ti, 10) && ti.tm_yday != _lastYday) {
        _reqDay   = 0;
        _lastYday = ti.tm_yday;
    }
}
