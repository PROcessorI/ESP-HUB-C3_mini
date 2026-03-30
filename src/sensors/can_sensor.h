#pragma once
// ============== CAN Raw Frame Sensor ==============
// Listens for a specific CAN ID and exposes frame bytes as values.
// Uses CANManager (shared singleton).
// canId  = 11-bit or 29-bit CAN ID to filter
// canDlc = how many bytes of the frame to expose (1..8)
// Bytes are exposed in pairs as signed int16 (val0=bytes[0-1], val1=bytes[2-3], ...)
// Single-byte option: odd DLC → trailing byte exposed as uint8

#include "sensor_base.h"
#include "../can_manager.h"

class CANRawSensor : public SensorBase {
public:
    CANRawSensor(CANManager* can, uint32_t canId, uint8_t dlc = 8)
        : _can(can), _id(canId), _dlc(dlc > 8 ? 8 : dlc) {}

    bool begin() override {
        if (!_can || !_can->isRunning()) {
            Serial.printf("[CAN_RX] CANManager not running (ID=0x%03lX)\n",
                          (unsigned long)_id);
            return false;
        }
        _ready = true;
        Serial.printf("[CAN_RX] listening for ID=0x%03lX dlc=%d\n",
                      (unsigned long)_id, _dlc);
        return true;
    }

    uint8_t read() override {
        clearValues();
        if (!_ready) return 0;

        CanFrame fr;
        if (!_can->getFrame(_id, fr)) return 0;
        if (millis() - fr.ts > 5000) return 0; // stale > 5s → invalid

        uint8_t count = 0;
        // Expose as int16 pairs
        for (uint8_t i = 0; i + 1 < _dlc && count < MAX_VALUES_PER_SENSOR; i += 2) {
            int16_t v = (int16_t)(fr.data[i] | (fr.data[i+1] << 8));
            char name[8]; snprintf(name, sizeof(name), "w%d", i/2);
            setVal(count++, name, "", (float)v);
        }
        // Trailing single byte
        if (_dlc % 2 != 0 && count < MAX_VALUES_PER_SENSOR) {
            uint8_t byteIdx = _dlc - 1;
            char name[8]; snprintf(name, sizeof(name), "b%d", byteIdx);
            setVal(count++, name, "", (float)fr.data[byteIdx]);
        }
        return count;
    }

    uint8_t valueCount() const override { return (_dlc + 1) / 2; }
    const SensorValue& getValue(uint8_t idx) const override { return _values[idx]; }
    const char* typeName() const override { return "CAN Raw"; }

private:
    CANManager* _can;
    uint32_t    _id;
    uint8_t     _dlc;
};
