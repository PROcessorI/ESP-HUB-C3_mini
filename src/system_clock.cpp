#include "system_clock.h"

SystemClock systemClock;

void SystemClock::begin() {
    _ntpSynced = false;
    _lastNTPTime = 0;
    _lastNTPMillis = millis();
    _backupStartMillis = millis();
    _rtcAvailable = false;
    _lastRTCSync = 0;
    
    // Попытка загрузить время из RTC, если доступна
    // (для усовершенствованной версии с поддержкой DS3231 или похожего)
    _loadFromRTC();
    
    Serial.println(F("[SYSCLOCK] System Clock initialized"));
    Serial.printf("[SYSCLOCK] Backup timer active: %s\n", 
                  _ntpSynced ? "NO (NTP synced)" : "YES (waiting for NTP)");
}

void SystemClock::syncFromNTP(time_t ntpTime) {
    if (ntpTime <= 100) {
        Serial.println(F("[SYSCLOCK] WARNING: Invalid NTP time received"));
        return;
    }
    
    _lastNTPTime = ntpTime;
    _lastNTPMillis = millis();
    _ntpSynced = true;
    
    Serial.printf("[SYSCLOCK] Synced with NTP: %u (seconds since epoch)\n", (unsigned)ntpTime);
    struct tm ti;
    localtime_r(&ntpTime, &ti);
    Serial.printf("[SYSCLOCK] Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                  ti.tm_hour, ti.tm_min, ti.tm_sec);
    
    // Сохранить в RTC, если доступна
    _saveToRTC(ntpTime);
}

time_t SystemClock::now() const {
    if (_ntpSynced && _lastNTPTime > 0) {
        // NTP синхронизирована — используем её с добавлением времени, прошедшего с синхронизации
        uint32_t elapsedMs = millis() - _lastNTPMillis;
        return _lastNTPTime + (elapsedMs / 1000);
    } else {
        // NTP недоступна — используем backup-таймер
        uint32_t elapsedMs = millis() - _backupStartMillis;
        uint64_t backupTimeSecs = elapsedMs / 1000;
        return _bootTime + backupTimeSecs;
    }
}

void SystemClock::getLocalTime(struct tm* timeinfo) const {
    if (!timeinfo) return;
    time_t t = now();
    localtime_r(&t, timeinfo);
}

uint32_t SystemClock::timeSinceSync() const {
    if (!_ntpSynced) return UINT32_MAX;
    return millis() - _lastNTPMillis;
}

void SystemClock::getBackupTime(uint8_t& hours, uint8_t& minutes, uint8_t& seconds) const {
    struct tm ti;
    getLocalTime(&ti);
    hours = ti.tm_hour;
    minutes = ti.tm_min;
    seconds = ti.tm_sec;
}

uint16_t SystemClock::getBackupDays() const {
    if (_ntpSynced) return 0;
    uint32_t elapsedMs = millis() - _backupStartMillis;
    uint64_t backupTimeSecs = elapsedMs / 1000;
    return (uint16_t)(backupTimeSecs / 86400UL);
}

void SystemClock::tick() {
    // Каждые 60 секунд проверять статус
    static uint32_t lastStatusPrint = 0;
    uint32_t now_ms = millis();
    
    if (now_ms - lastStatusPrint >= 60000) {
        lastStatusPrint = now_ms;
        
        if (_ntpSynced) {
            struct tm ti;
            getLocalTime(&ti);
            Serial.printf("[SYSCLOCK] NTP synced: %02d:%02d:%02d | elapsed: %ums\n",
                          ti.tm_hour, ti.tm_min, ti.tm_sec,
                          (unsigned)timeSinceSync());
        } else {
            uint8_t h, m, s;
            getBackupTime(h, m, s);
            uint16_t days = getBackupDays();
            Serial.printf("[SYSCLOCK] BACKUP TIMER: %ud %02d:%02d:%02d | waiting for NTP...\n",
                          days, h, m, s);
        }
    }
}

void SystemClock::_loadFromRTC() {
    // TODO: Если установлен RTC (DS3231 на I2C), читать из него
    // Сейчас это заглушка для совместимости
    _rtcAvailable = false;
}

void SystemClock::_saveToRTC(time_t t) {
    // TODO: Если установлен RTC, сохранять синхронизированное время
    // Сейчас это заглушка для совместимости
    (void)t;
}
