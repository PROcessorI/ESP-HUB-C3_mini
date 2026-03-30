#include "fixture_manager.h"
#include "system_clock.h"

// =============================================================================
// Инициализация
// =============================================================================
void FixtureManager::begin(FixtureConfig* cfg) {
    _cfg = cfg;
    // UART1 для связи с драйвером (ESP32-C3: TX=GPIO4, RX=GPIO5)
    Serial1.begin(FIXTURE_BAUD, SERIAL_8N1, FIXTURE_RX_PIN, FIXTURE_TX_PIN);
    _enabled = true;
    _currentRed = 0;
    _currentFarRed = 0;
    _currentBlue = 0;
    _currentWhite = 0;

    Serial.printf("[FIXTURE] Initialized on UART1 (TX=%d, RX=%d)\n", FIXTURE_TX_PIN, FIXTURE_RX_PIN);

    // Начальное состояние — всё выключено
    setChannels(0, 0, 0, 0);
}

void FixtureManager::tick() {
    if (!_enabled || !_cfg) return;

    // =========================================================
    // ИНТЕРВАЛЬНЫЕ ТАЙМЕРЫ — работают без NTP, только millis()
    // =========================================================
    unsigned long now_ms = millis();
    for (int i = 0; i < MAX_FIXTURE_TIMERS; i++) {
        if (!_cfg->timers[i].enabled) continue;
        if (_cfg->timers[i].action == FIX_TIMER_OFF) continue;

        uint32_t intervalMs = (_cfg->timers[i].hours   * 3600UL +
                               _cfg->timers[i].minutes * 60UL   +
                               _cfg->timers[i].seconds) * 1000UL;
        if (intervalMs == 0) continue;

        // Завершить PULSE если время вышло
        if (_pulseActive[i] && now_ms >= _pulseEnd[i]) {
            _pulseActive[i] = false;
            setChannels(0, 0, 0, 0);
            Serial.printf("[FIXTURE] Timer %d: pulse ended -> OFF\n", i);
        }

        // Выключить таймер по времени работы, если установлено
        if (_timerRunEnd[i] > 0 && now_ms >= _timerRunEnd[i]) {
            _timerRunEnd[i] = 0;
            _pulseActive[i] = false;
            setChannels(0, 0, 0, 0);
            Serial.printf("[FIXTURE] Timer %d: runtime ended -> OFF\n", i);
        }

        // Запустить следующий цикл
        if (now_ms - _timerLastTick[i] >= intervalMs) {
            _timerLastTick[i] = now_ms;
            uint8_t r=0, fr=0, b=0, w=0;
            bool isPulse = false;
            switch (_cfg->timers[i].action) {
                case FIX_TIMER_GROW:
                    r=140; fr=100; b=100; w=60;  break;
                case FIX_TIMER_FULL:
                    r=200; fr=200; b=200; w=200; break;
                case FIX_TIMER_RED:
                    r=200; break;
                case FIX_TIMER_BLUE:
                    b=200; break;
                case FIX_TIMER_PULSE_GROW:
                    r=140; fr=100; b=100; w=60; isPulse=true; break;
                case FIX_TIMER_PULSE_FULL:
                    r=200; fr=200; b=200; w=200; isPulse=true; break;
                case FIX_TIMER_CUSTOM:
                    r=_cfg->timers[i].red; fr=_cfg->timers[i].far_red;
                    b=_cfg->timers[i].blue; w=_cfg->timers[i].white; break;
                case FIX_TIMER_PULSE_CUSTOM:
                    r=_cfg->timers[i].red; fr=_cfg->timers[i].far_red;
                    b=_cfg->timers[i].blue; w=_cfg->timers[i].white;
                    isPulse=true; break;
                default: continue;
            }
            Serial.printf("[FIXTURE] >>> TIMER %d FIRED (every %us) '%s' -> R=%d FR=%d B=%d W=%d%s\n",
                i, (unsigned)(intervalMs/1000), _cfg->timers[i].label, r, fr, b, w,
                isPulse ? " [PULSE]" : "");
            setChannels(r, fr, b, w);
            
            // Set runtime end time if specified
            uint32_t runTimeMs = (_cfg->timers[i].run_hours * 3600UL +
                                   _cfg->timers[i].run_minutes * 60UL +
                                   _cfg->timers[i].run_seconds) * 1000UL;
            if (runTimeMs > 0) {
                _timerRunEnd[i] = now_ms + runTimeMs;
                Serial.printf("[FIXTURE] Timer %d: will auto-shutoff after %us\n", i, (unsigned)(runTimeMs/1000));
            } else {
                _timerRunEnd[i] = 0;
            }
            
            if (isPulse && _cfg->timers[i].duration_ms > 0) {
                _pulseActive[i] = true;
                _pulseEnd[i] = now_ms + _cfg->timers[i].duration_ms;
            }
        }
    }

    // =========================================================
    // СЦЕНАРИИ по времени — работают с системным часом
    // Если интернет есть, используется NTP; иначе — резервный таймер
    // =========================================================
    struct tm timeinfo;
    systemClock.getLocalTime(&timeinfo);

    // timeinfo.tm_year > 100 means time is synced (or at least we have a valid year)
    // Сценарии работают в обоих режимах: NTP и резервный
    bool usingBackup = systemClock.isBackupTimerActive();
    
    // Предупреждение раз в 60 секунд, если используется резервный таймер
    if (usingBackup && millis() - _lastDebugPrint >= 60000) {
        _lastDebugPrint = millis();
        Serial.printf("[FIXTURE] Using BACKUP timer for scenarios (no NTP). Time source: internal clock.\n");
    }

    // Периодический статус каждые 60 секунд
    if (!usingBackup && millis() - _lastDebugPrint >= 60000) {
        _lastDebugPrint = millis();
        Serial.printf("[FIXTURE] Time: %02d:%02d:%02d (NTP synced) | Enabled scenarios: ",
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        int cnt = 0;
        for (int i = 0; i < MAX_FIXTURE_SCENARIOS; i++) {
            if (_cfg->scenarios[i].enabled) {
                Serial.printf("#%d->%02d:%02d:%02d(R=%d FR=%d B=%d W=%d) ",
                              i,
                              _cfg->scenarios[i].start_hour,
                              _cfg->scenarios[i].start_minute,
                              _cfg->scenarios[i].start_second,
                              _cfg->scenarios[i].red,
                              _cfg->scenarios[i].far_red,
                              _cfg->scenarios[i].blue,
                              _cfg->scenarios[i].white);
                cnt++;
            }
        }
        if (cnt == 0) Serial.print(F("none"));
        Serial.println();
    }

    if (_lastCheckedSecond != timeinfo.tm_sec) {
        _lastCheckedSecond = timeinfo.tm_sec;

        // Check if any scenario matches the current time
        for (int i = 0; i < MAX_FIXTURE_SCENARIOS; i++) {
            if (_cfg->scenarios[i].enabled &&
                _cfg->scenarios[i].start_hour == timeinfo.tm_hour &&
                _cfg->scenarios[i].start_minute == timeinfo.tm_min &&
                _cfg->scenarios[i].start_second == timeinfo.tm_sec) {
                
                Serial.printf("[FIXTURE] >>> SCENARIO %d FIRED at %02d:%02d:%02d -> R=%d FR=%d B=%d W=%d\n", 
                              i, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                              _cfg->scenarios[i].red, _cfg->scenarios[i].far_red,
                              _cfg->scenarios[i].blue, _cfg->scenarios[i].white);
                
                setChannels(_cfg->scenarios[i].red, 
                            _cfg->scenarios[i].far_red,
                            _cfg->scenarios[i].blue, 
                            _cfg->scenarios[i].white);
                            
                // Update main config state so UI reflects it
                _cfg->red_brightness   = _cfg->scenarios[i].red;
                _cfg->far_red_brightness = _cfg->scenarios[i].far_red;
                _cfg->blue_brightness  = _cfg->scenarios[i].blue;
                _cfg->white_brightness = _cfg->scenarios[i].white;
            }
        }
    }
}

// =============================================================================
// Расчёт контрольной суммы
// =============================================================================
uint8_t FixtureManager::_calculateChecksum(uint8_t cmd, uint8_t offset, uint8_t length,
                                           uint8_t mask, uint8_t ch1, uint8_t ch2,
                                           uint8_t ch3, uint8_t ch4) {
    uint16_t sum = cmd + offset + length + mask + ch1 + ch2 + ch3 + ch4;
    return (uint8_t)(sum & 0xFF);
}

// =============================================================================
// Отправка команды SET для 4 каналов
// Параметры: яркость 0–200 для каждого канала
// =============================================================================
bool FixtureManager::_sendDimmingCommand(uint8_t red, uint8_t farRed, uint8_t blue,
                                          uint8_t white) {
    // Ограничение значений
    if (red > 200) red = 200;
    if (farRed > 200) farRed = 200;
    if (blue > 200) blue = 200;
    if (white > 200) white = 200;

    uint8_t checksum = _calculateChecksum(FIXTURE_CMD_SET, FIXTURE_OFFSET_MULTI, 
                                           FIXTURE_DATA_LENGTH, FIXTURE_CHANNEL_MASK,
                                           red, farRed, blue, white);

    // Формирование кадра (12 байт)
    uint8_t frame[12] = {
        FIXTURE_FRAME_HEADER,   // 0x3A
        FIXTURE_CMD_SET,        // 0x3C
        FIXTURE_OFFSET_MULTI,   // 0xEE
        FIXTURE_DATA_LENGTH,    // 0x05
        FIXTURE_CHANNEL_MASK,   // 0x0F
        red,                    // CH1: красный
        blue,                   // CH2: синий
        farRed,                 // CH3: дальний красный
        white,                  // CH4: белый дневной
        checksum,               // контрольная сумма
        FIXTURE_FRAME_END_CR,   // 0x0D
        FIXTURE_FRAME_END_LF    // 0x0A
    };

    // Отправка
    Serial1.write(frame, 12);
    Serial1.flush();

    // Отладочный вывод
    Serial.print(F("[FIX TX] "));
    for (int i = 0; i < 12; i++) {
        if (frame[i] < 0x10) Serial.print(F("0"));
        Serial.print(frame[i], HEX);
        Serial.print(F(" "));
    }
    Serial.print(F(" | R="));
    Serial.print(red * 0.5, 1);
    Serial.print(F("% FR="));
    Serial.print(farRed * 0.5, 1);
    Serial.print(F("% B="));
    Serial.print(blue * 0.5, 1);
    Serial.print(F("% W="));
    Serial.print(white * 0.5, 1);
    Serial.println(F("%"));

    return true;
}

// =============================================================================
// Чтение ответа (ACK) от драйвера
// =============================================================================
bool FixtureManager::_readResponse() {
    unsigned long startTime = millis();
    uint8_t response[8];
    int idx = 0;

    while (millis() - startTime < FIXTURE_RESPONSE_TIMEOUT_MS) {
        if (Serial1.available()) {
            uint8_t b = Serial1.read();
            if (idx == 0 && b != FIXTURE_FRAME_HEADER) continue; // ждём заголовок
            response[idx++] = b;
            if (idx >= 8) break;
        }
    }

    if (idx >= 8) {
        // Отладочный вывод ответа
        Serial.print(F("[FIX RX] "));
        for (int i = 0; i < 8; i++) {
            if (response[i] < 0x10) Serial.print(F("0"));
            Serial.print(response[i], HEX);
            Serial.print(F(" "));
        }

        // Проверка ACK
        if (response[1] == FIXTURE_CMD_SET_RESP && response[4] == FIXTURE_ACK_OK) {
            Serial.println(F(" -> ACK OK"));
            return true;
        } else {
            Serial.println(F(" -> ACK FAIL"));
            return false;
        }
    }

    Serial.println(F("[FIX RX] Timeout - no response"));
    return false;
}

// =============================================================================
// Установка яркости каналов с проверкой ответа
// =============================================================================
bool FixtureManager::setChannels(uint8_t red, uint8_t farRed, uint8_t blue, uint8_t white) {
    // Проверка интервала между командами
    if (millis() - _lastCmdTime < FIXTURE_CMD_INTERVAL_MS) {
        delay(FIXTURE_CMD_INTERVAL_MS - (millis() - _lastCmdTime));
    }
    
    _lastAckOk = _sendDimmingCommand(red, farRed, blue, white);
    // Так как у нас нет линии RX (или обратной связи) от светильника - 
    // отключаем ожидание ACK, чтобы не вешать контроллер на 500мс
    if (_lastAckOk) {
        _lastAckOk = true; 
    }
    
    _lastCmdTime = millis();
    
    if (_lastAckOk) {
        _currentRed = red;
        _currentFarRed = farRed;
        _currentBlue = blue;
        _currentWhite = white;
    }
    
    return _lastAckOk;
}

// =============================================================================
// Пресеты
// =============================================================================
bool FixtureManager::setPreset(uint8_t presetId) {
    switch (presetId) {
        case 0: // OFF
            Serial.println(F("[FIXTURE] Preset: OFF"));
            return setChannels(0, 0, 0, 0);
        
        case 1: // Red 100%
            Serial.println(F("[FIXTURE] Preset: Red 100%"));
            return setChannels(FIXTURE_BRIGHTNESS_100, 0, 0, 0);
        
        case 2: // Far Red 100%
            Serial.println(F("[FIXTURE] Preset: Far Red 100%"));
            return setChannels(0, FIXTURE_BRIGHTNESS_100, 0, 0);
        
        case 3: // Blue 100%
            Serial.println(F("[FIXTURE] Preset: Blue 100%"));
            return setChannels(0, 0, FIXTURE_BRIGHTNESS_100, 0);
        
        case 4: // White 100%
            Serial.println(F("[FIXTURE] Preset: White 100%"));
            return setChannels(0, 0, 0, FIXTURE_BRIGHTNESS_100);
        
        case 5: // Full 100%
            Serial.println(F("[FIXTURE] Preset: Full 100%"));
            return setChannels(FIXTURE_BRIGHTNESS_100, FIXTURE_BRIGHTNESS_100, 
                               FIXTURE_BRIGHTNESS_100, FIXTURE_BRIGHTNESS_100);
        
        case 6: // Grow preset (R70 FR50 B50 W30)
            Serial.println(F("[FIXTURE] Preset: Grow (R70 FR50 B50 W30)"));
            return setChannels(FIXTURE_BRIGHTNESS_70, FIXTURE_BRIGHTNESS_50, 
                               FIXTURE_BRIGHTNESS_50, FIXTURE_BRIGHTNESS_30);
        
        case 7: // Red + Far Red 100%
            Serial.println(F("[FIXTURE] Preset: Red + Far Red 100%"));
            return setChannels(FIXTURE_BRIGHTNESS_100, FIXTURE_BRIGHTNESS_100, 0, 0);
        
        default:
            Serial.println(F("[FIXTURE] Unknown preset"));
            return false;
    }
}


void FixtureManager::runDemo() {
    Serial.println("--- Demo: Red 100% ---");
    setChannels(FIXTURE_BRIGHTNESS_100, 0, 0, 0);
    delay(2000);

    Serial.println("--- Demo: Far Red 100% ---");
    setChannels(0, FIXTURE_BRIGHTNESS_100, 0, 0);
    delay(2000);

    Serial.println("--- Demo: Blue 100% ---");
    setChannels(0, 0, FIXTURE_BRIGHTNESS_100, 0);
    delay(2000);

    Serial.println("--- Demo: White 100% ---");
    setChannels(0, 0, 0, FIXTURE_BRIGHTNESS_100);
    delay(2000);

    Serial.println("--- Demo: Red + Far Red 100% ---");
    setChannels(FIXTURE_BRIGHTNESS_100, FIXTURE_BRIGHTNESS_100, 0, 0);
    delay(2000);

    Serial.println("--- Demo: Grow (R70 FR50 B50 W30) ---");
    setChannels(FIXTURE_BRIGHTNESS_70, FIXTURE_BRIGHTNESS_50, FIXTURE_BRIGHTNESS_50, FIXTURE_BRIGHTNESS_30);
    delay(2000);

    Serial.println("--- Demo: Full 100% ---");
    setChannels(FIXTURE_BRIGHTNESS_100, FIXTURE_BRIGHTNESS_100, FIXTURE_BRIGHTNESS_100, FIXTURE_BRIGHTNESS_100);
    delay(2000);

    Serial.println("--- Demo: All OFF ---");
    setChannels(0, 0, 0, 0);
}
