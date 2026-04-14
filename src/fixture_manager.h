#pragma once
// ============== Fixture Manager (Управление светильниками) ==============
// ESP32-C3 UART управление Inventronics NSM-1k2q200mg (4 канала)
// Протокол: Digital Dimming Vm1.0
// UART1: 9600 baud, 8N1, TX=GPIO4, RX=GPIO3
// Каналы: CH1=Red, CH2=FarRed, CH3=Blue, CH4=White

#include <Arduino.h>
#include "config.h"

#define FIXTURE_TX_PIN 4     // ESP32-C3: GPIO4
#define FIXTURE_RX_PIN 3     // ESP32-C3: GPIO3
#define FIXTURE_BAUD 9600

// --- Константы протокола ---
#define FIXTURE_FRAME_HEADER 0x3A     // ':'
#define FIXTURE_CMD_SET 0x3C          // Команда записи (SET)
#define FIXTURE_CMD_SET_RESP 0x3D     // Ответ на SET
#define FIXTURE_OFFSET_MULTI 0xEE     // Мультиканальный режим
#define FIXTURE_DATA_LENGTH 0x05      // 1 маска + 4 канала
#define FIXTURE_CHANNEL_MASK 0x0F     // CH1–CH4 активны
#define FIXTURE_FRAME_END_CR 0x0D     // \r
#define FIXTURE_FRAME_END_LF 0x0A     // \n
#define FIXTURE_ACK_OK 0x55           // Успешный ответ

// --- Яркость (0–200, шаг 0.5%) ---
#define FIXTURE_BRIGHTNESS_OFF 0      // 0%
#define FIXTURE_BRIGHTNESS_30 60      // 30%  (0x3C)
#define FIXTURE_BRIGHTNESS_50 100     // 50%  (0x64)
#define FIXTURE_BRIGHTNESS_70 140     // 70%  (0x8C)
#define FIXTURE_BRIGHTNESS_100 200    // 100% (0xC8)

// --- Тайминги ---
#define FIXTURE_CMD_INTERVAL_MS 150
#define FIXTURE_RESPONSE_TIMEOUT_MS 500

class FixtureManager {
public:
    void begin(FixtureConfig* cfg = nullptr);
    void tick();
    
    // Установка яркости каналов (0-200)
    bool setChannels(uint8_t red, uint8_t farRed, uint8_t blue, uint8_t white);
    
    // Пресеты
    bool setPreset(uint8_t presetId);
    
    // Получение текущих значений
    uint8_t getRed() const { return _currentRed; }
    uint8_t getFarRed() const { return _currentFarRed; }
    uint8_t getBlue() const { return _currentBlue; }
    uint8_t getWhite() const { return _currentWhite; }
    
    // Статус
    bool isEnabled() const { return _enabled; }
    bool isLastAckOk() const { return _lastAckOk; }
    
    // Включение/выключение менеджера
    void enable(bool en) { _enabled = en; }

    // Timers state & demo
    uint32_t _timerLastTick[MAX_FIXTURE_TIMERS] = {0};
    uint32_t _pulseEnd[MAX_FIXTURE_TIMERS] = {0};
    bool     _pulseActive[MAX_FIXTURE_TIMERS] = {false};
    uint32_t _timerRunEnd[MAX_FIXTURE_TIMERS] = {0};
    void runDemo();

    void reloadTimers() {
        for (int i = 0; i < MAX_FIXTURE_TIMERS; i++) {
            _timerLastTick[i] = 0;
            _pulseActive[i] = false;
            _pulseEnd[i] = 0;
            _timerRunEnd[i] = 0;
        }
    }



private:
    bool _enabled = false;
    bool _lastAckOk = false;
    uint32_t _lastCmdTime = 0;
    
    FixtureConfig* _cfg = nullptr;
    int _lastCheckedSecond = -1;
    unsigned long _lastDebugPrint = 0;

    uint8_t _currentRed = 0;
    uint8_t _currentFarRed = 0;
    uint8_t _currentBlue = 0;
    uint8_t _currentWhite = 0;
    
    // Внутренние методы
    uint8_t _calculateChecksum(uint8_t cmd, uint8_t offset, uint8_t length,
                               uint8_t mask, uint8_t ch1, uint8_t ch2, 
                               uint8_t ch3, uint8_t ch4);
    bool _sendDimmingCommand(uint8_t red, uint8_t farRed, uint8_t blue, uint8_t white);
    bool _readResponse();

};
