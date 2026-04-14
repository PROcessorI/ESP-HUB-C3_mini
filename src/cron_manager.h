#pragma once
// ============== CRON Manager ==============
// Periodic/daily/once task scheduler for ESP-HUB.
// Exposes fired actions via pollFired() for main-loop dispatch.
// Entries are persisted to /cron.json on LittleFS.

#include <Arduino.h>
#include <LittleFS.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define CRON_MAX_ENTRIES   16
#define CRON_ACTION_LEN    128
#define CRON_TZ_LEN        64
#define CRON_JSON_PATH     "/cron.json"
#define CRON_TASK_STACK    6144
#define CRON_TASK_CORE     0
#define CRON_TASK_PRIO     3

enum CronType : uint8_t {
    CRON_PERIODIC = 0,  // every N seconds
    CRON_DAILY    = 1,  // at HH:MM every day (requires NTP sync)
    CRON_ONCE     = 2,  // once after N seconds from add time
};

struct CronEntry {
    uint8_t  id           = 0;
    CronType type         = CRON_PERIODIC;
    uint32_t interval_sec = 60;     // PERIODIC: period; ONCE: delay from creation
    uint8_t  hour         = 0;      // DAILY: trigger hour  (0-23)
    uint8_t  minute       = 0;      // DAILY: trigger minute (0-59)
    char     action[CRON_ACTION_LEN] = "";
    uint32_t last_run     = 0;      // PERIODIC/ONCE: unix ts; DAILY: YYYYMMDD
    bool     enabled      = true;
    bool     fired        = false;  // ONCE: already fired?
};

class CronManager {
public:
    // Load entries from LittleFS, init mutex & timezone
    void begin();

    // Start background FreeRTOS task (call after WiFi/NTP ready)
    void startTask();

    // Add a new cron entry.  Returns allocated id (1-255) or 0 on error.
    // interval_or_hour: PERIODIC=interval_sec, DAILY=hour (0-23), ONCE=delay_sec
    uint8_t add(CronType type, uint32_t interval_or_hour,
                uint8_t minute, const char* action, bool enabled = true);

    // Convenience: periodic with seconds granularity
    uint8_t addSeconds(uint32_t interval_sec, const char* action);

    // Remove entry by id
    bool remove(uint8_t id);

    // Enable/disable entry by id
    bool setEnabled(uint8_t id, bool en);

    // Number of non-empty entries
    int count() const;

    // Fill buf with a compact JSON array (returns bytes written, ≤ len)
    int listJson(char* buf, size_t len) const;

    // Timezone: POSIX TZ string, e.g. "UTC0" or "MSK-3"
    void setTimezone(const char* tz);
    void getTimezone(char* buf, size_t len) const;

    // Returns true if NTP has set a valid year (≥ 2024)
    bool isTimeSynced() const;

    // Poll for a fired action — returns true + copies action to buf.
    bool pollFired(char* buf, size_t len);

    // Called every second from background task
    void checkFires();

private:
    CronEntry         _entries[CRON_MAX_ENTRIES];
    char              _tz[CRON_TZ_LEN] = "UTC0";
    volatile bool     _pendingFire      = false;
    char              _pendingAction[CRON_ACTION_LEN];
    SemaphoreHandle_t _mutex = nullptr;
    TaskHandle_t      _task  = nullptr;

    void    load();
    void    save();
    uint8_t allocId();

    static void s_taskFn(void* pv);
};

extern CronManager cronMgr;
