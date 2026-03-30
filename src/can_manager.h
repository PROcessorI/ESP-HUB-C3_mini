#pragma once
// ============== CAN Bus Manager (ESP32 TWAI) ==============
// Wraps the ESP32 built-in TWAI/CAN controller.
// NOT available on ESP32-C3 (no TWAI peripheral).

#include <Arduino.h>
#include <soc/soc_caps.h>

#if SOC_TWAI_SUPPORTED
#include "driver/twai.h"
#endif

#define CAN_RX_CACHE_SIZE 32   // max distinct IDs to cache

struct CanFrame {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
    uint32_t ts;   // millis() timestamp
    bool     valid = false;
};

class CANManager {
public:
    bool begin(uint8_t txPin = 5, uint8_t rxPin = 4, uint32_t bitrate = 500000) {
#if SOC_TWAI_SUPPORTED
        return _begin(txPin, rxPin, bitrate);
#else
        (void)txPin; (void)rxPin; (void)bitrate;
        Serial.println(F("[CAN] Not supported on this chip"));
        return false;
#endif
    }

    void tick() {
#if SOC_TWAI_SUPPORTED
        _tick();
#endif
    }

    void end() {
#if SOC_TWAI_SUPPORTED
        _end();
#endif
    }

    bool getFrame(uint32_t id, CanFrame& out) const {
#if SOC_TWAI_SUPPORTED
        return _getFrame(id, out);
#else
        (void)id; (void)out;
        return false;
#endif
    }

    bool sendFrame(uint32_t id, const uint8_t* data, uint8_t dlc) {
#if SOC_TWAI_SUPPORTED
        return _sendFrame(id, data, dlc);
#else
        (void)id; (void)data; (void)dlc;
        return false;
#endif
    }

    bool isRunning() const { return _running; }

    uint8_t txPin = 5;
    uint8_t rxPin = 4;

private:
    bool _running = false;
    CanFrame _cache[CAN_RX_CACHE_SIZE];

#if SOC_TWAI_SUPPORTED
    bool _begin(uint8_t txPin, uint8_t rxPin, uint32_t bitrate);
    void _tick();
    void _end();
    bool _getFrame(uint32_t id, CanFrame& out) const;
    bool _sendFrame(uint32_t id, const uint8_t* data, uint8_t dlc);
    int findSlot(uint32_t id) const;
    int freeSlot() const;
#endif
};
