#pragma once
// ============== DS18B20 Temperature Sensor ==============

#include "sensor_base.h"
#include <OneWire.h>
#include <DallasTemperature.h>

class DS18B20Sensor : public SensorBase {
public:
    DS18B20Sensor(uint8_t pin) : _pin(pin), _wire(pin), _dallas(&_wire) {}

    bool begin() override {
        _dallas.begin();
        _ready = (_dallas.getDeviceCount() > 0);
        if (_ready) {
            _dallas.setResolution(12);
            Serial.printf("[DS18B20] OK on pin %d (%d devices)\n", _pin, _dallas.getDeviceCount());
        } else {
            Serial.printf("[DS18B20] FAIL on pin %d\n", _pin);
        }
        return _ready;
    }

    uint8_t read() override {
        clearValues();
        if (!_ready) return 0;

        _dallas.requestTemperatures();
        float t = _dallas.getTempCByIndex(0);

        if (t != DEVICE_DISCONNECTED_C) {
            setVal(0, "temperature", "\xC2\xB0""C", t);
            return 1;
        }
        return 0;
    }

    uint8_t valueCount() const override { return 1; }
    const SensorValue& getValue(uint8_t idx) const override { return _values[idx]; }
    const char* typeName() const override { return "DS18B20"; }

private:
    uint8_t _pin;
    OneWire _wire;
    DallasTemperature _dallas;
};
