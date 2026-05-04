#include "fixture_manager.h"

// =============================================================================
// Инициализация
// =============================================================================
void FixtureManager::begin(FixtureConfig* cfg) {
    _cfg = cfg;
    uint8_t txPin = FIXTURE_TX_PIN;
    uint8_t rxPin = FIXTURE_RX_PIN;
    uint32_t baud = FIXTURE_BAUD;

    if (_cfg) {
        txPin = _cfg->uart_tx_pin;
        rxPin = _cfg->uart_rx_pin;
        if (_cfg->uart_baud >= 1200) {
            baud = _cfg->uart_baud;
        }
    }

    // UART1 для связи с драйвером (ESP32-C3: TX=GPIO4, RX=GPIO3 по умолчанию)
    Serial1.begin(baud, SERIAL_8N1, rxPin, txPin);
    _enabled = true;
    _currentRed = 0;
    _currentFarRed = 0;
    _currentBlue = 0;
    _currentWhite = 0;

    Serial.printf("[FIXTURE] Initialized on UART1 (TX=%u, RX=%u, %lu baud)\n",
                  (unsigned)txPin, (unsigned)rxPin, (unsigned long)baud);

    // Начальное состояние — всё выключено
    setChannels(0, 0, 0, 0);
}

void FixtureManager::tick() {
    if (!_enabled || !_cfg) return;
    
    // Check fixture timers
    checkTimers();
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
        yield();  // Feed watchdog during timeout wait
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
    yield();  // Feed watchdog

    Serial.println("--- Demo: Far Red 100% ---");
    setChannels(0, FIXTURE_BRIGHTNESS_100, 0, 0);
    delay(2000);
    yield();

    Serial.println("--- Demo: Blue 100% ---");
    setChannels(0, 0, FIXTURE_BRIGHTNESS_100, 0);
    delay(2000);
    yield();

    Serial.println("--- Demo: White 100% ---");
    setChannels(0, 0, 0, FIXTURE_BRIGHTNESS_100);
    delay(2000);
    yield();

    Serial.println("--- Demo: Red + Far Red 100% ---");
    setChannels(FIXTURE_BRIGHTNESS_100, FIXTURE_BRIGHTNESS_100, 0, 0);
    delay(2000);
    yield();

    Serial.println("--- Demo: Grow (R70 FR50 B50 W30) ---");
    setChannels(FIXTURE_BRIGHTNESS_70, FIXTURE_BRIGHTNESS_50, FIXTURE_BRIGHTNESS_50, FIXTURE_BRIGHTNESS_30);
    delay(2000);
    yield();

    Serial.println("--- Demo: Full 100% ---");
    setChannels(FIXTURE_BRIGHTNESS_100, FIXTURE_BRIGHTNESS_100, FIXTURE_BRIGHTNESS_100, FIXTURE_BRIGHTNESS_100);
    delay(2000);
    yield();

    Serial.println("--- Demo: All OFF ---");
    setChannels(0, 0, 0, 0);
}

// =============================================================================
// Timer Management
// =============================================================================

uint32_t FixtureManager::timeToNextTimer(int i) const {
    if (!_cfg || i < 0 || i >= MAX_FIXTURE_TIMERS) return 0;
    const FixtureTimer& t = _cfg->timers[i];
    if (!t.enabled) return 0;
    
    uint32_t interval = ((uint32_t)t.hours * 3600 +
                         (uint32_t)t.minutes * 60 +
                         (uint32_t)t.seconds) * 1000UL;
    if (interval == 0) return 0;
    
    uint32_t elapsed = millis() - _lastTimerTick[i];
    return (elapsed >= interval) ? 0 : (interval - elapsed);
}

void FixtureManager::checkTimers() {
    if (!_cfg) return;
    uint32_t now = millis();
    
    for (int i = 0; i < MAX_FIXTURE_TIMERS; i++) {
        FixtureTimer& t = _cfg->timers[i];
        if (!t.enabled) continue;
        
        uint32_t interval = ((uint32_t)t.hours * 3600 +
                             (uint32_t)t.minutes * 60 +
                             (uint32_t)t.seconds) * 1000UL;
        if (interval == 0) continue;
        
        // Check if interval has elapsed
        if (now - _lastTimerTick[i] < interval) continue;
        
        // Timer triggered
        _lastTimerTick[i] = now;
        
        // Apply timer brightness settings
        setChannels(t.red_brightness, t.far_red_brightness, 
                    t.blue_brightness, t.white_brightness);
        
        Serial.printf("[FIXTURE] Timer %d (%s) triggered -> R=%d FR=%d B=%d W=%d\n",
                      i, t.label, t.red_brightness, t.far_red_brightness, 
                      t.blue_brightness, t.white_brightness);
    }
}