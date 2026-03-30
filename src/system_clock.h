#pragma once
// ============== System Clock Manager (Внутренний регулятор времени) ==============
// Обеспечивает резервное время на основе millis() при отсутствии интернета
// Синхронизируется с NTP, когда подключение восстановлено
// Использует RTC (при наличии) для сохранения времени при перезагрузке

#include <Arduino.h>
#include <time.h>

class SystemClock {
public:
    // Инициализация
    void begin();
    
    // Синхронизация с NTP (вызывается из wifi_manager после успешного подключения)
    void syncFromNTP(time_t ntpTime);
    
    // Получить текущее время (работает всегда: NTP или backup)
    time_t now() const;
    
    // Получить структуру tm (для localtime_r замещения)
    void getLocalTime(struct tm* timeinfo) const;
    
    // Проверить, синхронизировано ли время с NTP
    bool isSyncedWithNTP() const { return _ntpSynced; }
    
    // Получить разницу времени от последней синхронизации (мс)
    uint32_t timeSinceSync() const;
    
    // Проверить, работает ли запасной таймер
    bool isBackupTimerActive() const { return !_ntpSynced; }
    
    // Получить текущий часh/минуты/секунды из backup-таймера
    void getBackupTime(uint8_t& hours, uint8_t& minutes, uint8_t& seconds) const;
    
    // Получить количество дней (от включения системы в backup-режиме)
    uint16_t getBackupDays() const;
    
    // Вызывать регулярно (из Serial.tick или main loop)
    // Использует RTC если доступна, иначе только millis()
    void tick();
    
private:
    // Состояние синхронизации
    bool _ntpSynced = false;
    time_t _lastNTPTime = 0;
    uint32_t _lastNTPMillis = 0;
    
    // Резервное время (когда NTP недоступна)
    uint32_t _backupStartMillis = 0;
    time_t _bootTime = 1000000;       // условный boot time, если никогда не было NTP
    
    // Состояние RTC (если имеется)
    bool _rtcAvailable = false;
    uint32_t _lastRTCSync = 0;
    
    // Вспомогательные методы
    void _loadFromRTC();
    void _saveToRTC(time_t t);
};

extern SystemClock systemClock;
