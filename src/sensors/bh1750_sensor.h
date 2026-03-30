#pragma once
// ============== BH1750 Light Sensor ==============

#include "sensor_base.h"
#include <Wire.h>
#include <BH1750.h>

class BH1750Sensor : public SensorBase {
public:
    BH1750Sensor(uint8_t addr = 0x23) : _addr(addr), _bh(addr) {}

    bool begin() override {
        _ready = _bh.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
        if (_ready) Serial.printf("[BH1750] OK at 0x%02X\n", _addr);
        else        Serial.printf("[BH1750] FAIL at 0x%02X\n", _addr);
        return _ready;
    }

    uint8_t read() override {
        clearValues();
        if (!_ready) return 0;

        if (_bh.measurementReady()) {
            float lux = _bh.readLightLevel();
            if (lux >= 0) {
                setVal(0, "light", "lx", lux);
                return 1;
            }
        }
        return 0;
    }

    uint8_t valueCount() const override { return 1; }
    const SensorValue& getValue(uint8_t idx) const override { return _values[idx]; }
    const char* typeName() const override { return "BH1750"; }

private:
    uint8_t _addr;
    BH1750 _bh;
};
