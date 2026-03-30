#include "cron_manager.h"
#include <ArduinoJson.h>
#include <time.h>

CronManager cronMgr;

// ─────────────────────────────────────────────────────────────────────────────
void CronManager::begin() {
    memset(_entries, 0, sizeof(_entries));
    if (!_mutex) _mutex = xSemaphoreCreateMutex();
    load();
    if (_tz[0]) { setenv("TZ", _tz, 1); tzset(); }
    Serial.printf("[CRON] Loaded %d entries, TZ=%s\n", count(), _tz);
}

void CronManager::startTask() {
    if (_task) return;
    xTaskCreatePinnedToCore(s_taskFn, "cron_mgr", CRON_TASK_STACK,
                            this, CRON_TASK_PRIO, &_task, CRON_TASK_CORE);
}

void CronManager::s_taskFn(void* pv) {
    CronManager* self = static_cast<CronManager*>(pv);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        self->checkFires();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void CronManager::checkFires() {
    if (_pendingFire) return;  // previous fire not yet consumed by main loop

    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);
    uint32_t now_u = (uint32_t)now;

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

    for (int i = 0; i < CRON_MAX_ENTRIES; i++) {
        CronEntry& e = _entries[i];
        if (!e.id || !e.enabled || e.action[0] == '\0') continue;

        bool fire = false;

        switch (e.type) {
        case CRON_PERIODIC:
            if (e.interval_sec > 0 && now_u - e.last_run >= e.interval_sec) {
                fire = true;
                e.last_run = now_u;
            }
            break;

        case CRON_DAILY:
            // Only when NTP year is plausible
            if (ti.tm_year + 1900 >= 2024) {
                uint32_t today = (uint32_t)((ti.tm_year + 1900) * 10000UL
                                            + (ti.tm_mon + 1) * 100UL
                                            + ti.tm_mday);
                if ((int)ti.tm_hour == e.hour &&
                    (int)ti.tm_min  == e.minute &&
                    e.last_run != today) {
                    fire = true;
                    e.last_run = today;
                }
            }
            break;

        case CRON_ONCE:
            if (!e.fired && e.interval_sec > 0 &&
                now_u >= e.last_run + e.interval_sec) {
                fire = true;
                e.fired   = true;
                e.enabled = false;
            }
            break;
        }

        if (fire) {
            strlcpy(_pendingAction, e.action, sizeof(_pendingAction));
            _pendingFire = true;
            save();
            break;  // at most one action per tick
        }
    }

    xSemaphoreGive(_mutex);
}

bool CronManager::pollFired(char* buf, size_t len) {
    if (!_pendingFire) return false;
    strlcpy(buf, _pendingAction, len);
    _pendingFire = false;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
uint8_t CronManager::allocId() {
    for (uint8_t id = 1; id != 0; id++) {
        bool used = false;
        for (int i = 0; i < CRON_MAX_ENTRIES; i++)
            if (_entries[i].id == id) { used = true; break; }
        if (!used) return id;
    }
    return 0;
}

uint8_t CronManager::add(CronType type, uint32_t interval_or_hour,
                         uint8_t minute, const char* action, bool enabled) {
    if (!action || !action[0]) return 0;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return 0;

    int slot = -1;
    for (int i = 0; i < CRON_MAX_ENTRIES; i++)
        if (!_entries[i].id) { slot = i; break; }

    if (slot < 0) { xSemaphoreGive(_mutex); return 0; }

    uint8_t id = allocId();
    if (!id)    { xSemaphoreGive(_mutex); return 0; }

    CronEntry& e = _entries[slot];
    e = CronEntry{};
    e.id      = id;
    e.type    = type;
    e.enabled = enabled;

    time_t now; time(&now);

    if (type == CRON_PERIODIC) {
        e.interval_sec = interval_or_hour;
        e.last_run     = (uint32_t)now;  // start counting from now
    } else if (type == CRON_DAILY) {
        e.hour   = (uint8_t)interval_or_hour;
        e.minute = minute;
        e.last_run = 0;  // haven't fired yet
    } else { // CRON_ONCE
        e.interval_sec = interval_or_hour;
        e.last_run     = (uint32_t)now;  // fire after interval_sec seconds
    }
    strlcpy(e.action, action, CRON_ACTION_LEN);

    save();
    xSemaphoreGive(_mutex);
    return id;
}

uint8_t CronManager::addSeconds(uint32_t interval_sec, const char* action) {
    return add(CRON_PERIODIC, interval_sec, 0, action);
}

bool CronManager::remove(uint8_t id) {
    if (!id) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
    for (int i = 0; i < CRON_MAX_ENTRIES; i++) {
        if (_entries[i].id == id) {
            _entries[i] = CronEntry{};
            save();
            xSemaphoreGive(_mutex);
            return true;
        }
    }
    xSemaphoreGive(_mutex);
    return false;
}

bool CronManager::setEnabled(uint8_t id, bool en) {
    if (!id) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
    for (int i = 0; i < CRON_MAX_ENTRIES; i++) {
        if (_entries[i].id == id) {
            _entries[i].enabled = en;
            save();
            xSemaphoreGive(_mutex);
            return true;
        }
    }
    xSemaphoreGive(_mutex);
    return false;
}

int CronManager::count() const {
    int n = 0;
    for (int i = 0; i < CRON_MAX_ENTRIES; i++)
        if (_entries[i].id) n++;
    return n;
}

int CronManager::listJson(char* buf, size_t len) const {
    // Produce {"entries":[...]} JSON
    size_t pos = 0;
    // open wrapper
    const char* hdr = "{\"entries\":[";
    size_t hlen = strlen(hdr);
    if (hlen >= len) { buf[0]='\0'; return 0; }
    memcpy(buf, hdr, hlen); pos = hlen;

    bool first = true;
    for (int i = 0; i < CRON_MAX_ENTRIES; i++) {
        const CronEntry& e = _entries[i];
        if (!e.id) continue;

        // Write comma separator
        if (!first) {
            if (pos + 1 >= len) break;
            buf[pos++] = ',';
        }

        // Write entry object
        char tmp[290];
        int n = snprintf(tmp, sizeof(tmp),
            "{\"id\":%u,\"type\":%d,\"hour\":%u,\"minute\":%u,"
            "\"interval_sec\":%lu,\"enabled\":%s,\"action\":",
            e.id, (int)e.type, e.hour, e.minute,
            (unsigned long)e.interval_sec,
            e.enabled ? "true" : "false");

        if (n < 0 || pos + (size_t)n + 4 >= len) break;
        memcpy(buf + pos, tmp, n); pos += n;

        // Append escaped action as JSON string
        buf[pos++] = '"';
        for (const char* p = e.action; *p && pos + 3 < len; p++) {
            if (*p == '"' || *p == '\\') buf[pos++] = '\\';
            buf[pos++] = *p;
        }
        if (pos + 3 < len) { buf[pos++] = '"'; buf[pos++] = '}'; }
        first = false;
    }

    if (pos + 3 < len) {
        buf[pos++] = ']';
        buf[pos++] = '}';
    }
    buf[pos] = '\0';
    return (int)pos;
}

// ─────────────────────────────────────────────────────────────────────────────
void CronManager::setTimezone(const char* tz) {
    if (!tz || !tz[0]) return;
    strlcpy(_tz, tz, sizeof(_tz));
    setenv("TZ", _tz, 1);
    tzset();
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        save();
        xSemaphoreGive(_mutex);
    }
}

void CronManager::getTimezone(char* buf, size_t len) const {
    strlcpy(buf, _tz, len);
}

bool CronManager::isTimeSynced() const {
    struct tm ti;
    if (!getLocalTime(&ti, 10)) return false;
    return (ti.tm_year + 1900) >= 2024;
}

// ─────────────────────────────────────────────────────────────────────────────
void CronManager::load() {
    File f = LittleFS.open(CRON_JSON_PATH, "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
    f.close();

    const char* tz = doc["tz"] | "UTC0";
    strlcpy(_tz, tz, sizeof(_tz));

    JsonArray arr = doc["entries"].as<JsonArray>();
    int slot = 0;
    for (JsonObject o : arr) {
        if (slot >= CRON_MAX_ENTRIES) break;
        uint8_t id = o["id"] | 0;
        if (!id) continue;
        CronEntry& e = _entries[slot];
        e = CronEntry{};
        e.id           = id;
        e.type         = (CronType)(o["type"].as<uint8_t>());
        e.enabled      = o["enabled"] | true;
        e.fired        = o["fired"]   | false;
        e.interval_sec = o["isec"]    | 60UL;
        e.hour         = o["hour"]    | 0;
        e.minute       = o["min"]     | 0;
        e.last_run     = o["lr"]      | 0UL;
        strlcpy(e.action, o["action"] | "", CRON_ACTION_LEN);
        slot++;
    }
}

void CronManager::save() {
    JsonDocument doc;
    doc["tz"] = _tz;
    JsonArray arr = doc["entries"].to<JsonArray>();
    for (int i = 0; i < CRON_MAX_ENTRIES; i++) {
        const CronEntry& e = _entries[i];
        if (!e.id) continue;
        JsonObject o = arr.add<JsonObject>();
        o["id"]      = e.id;
        o["type"]    = (int)e.type;
        o["enabled"] = e.enabled;
        o["fired"]   = e.fired;
        o["isec"]    = e.interval_sec;
        o["hour"]    = e.hour;
        o["min"]     = e.minute;
        o["lr"]      = e.last_run;
        o["action"]  = e.action;
    }
    File f = LittleFS.open(CRON_JSON_PATH, "w");
    if (!f) return;
    serializeJson(doc, f);
    f.close();
}
