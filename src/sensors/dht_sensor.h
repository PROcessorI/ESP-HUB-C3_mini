#pragma once
// ============== DHT11 / DHT22 Sensor Driver ==============

#include "sensor_base.h"
#include <DHT.h>

class DHTSensor : public SensorBase {
public:
    DHTSensor(uint8_t pin, uint8_t dhtType)
        : _pin(pin), _dhtType(dhtType), _dht(pin, dhtType) {}

    bool begin() override {
        _dht.begin();
        delay(100);
        float t = _dht.readTemperature();
        _ready = !isnan(t);
        if (_ready) Serial.printf("[DHT] OK on pin %d\n", _pin);
        else        Serial.printf("[DHT] FAIL on pin %d\n", _pin);
        return _ready;
    }

    uint8_t read() override {
        clearValues();
        if (!_ready) return 0;

        float t = _dht.readTemperature();
        float h = _dht.readHumidity();

        uint8_t cnt = 0;
        if (!isnan(t)) { setVal(0, "temperature", "\xC2\xB0""C", t); cnt++; }
        if (!isnan(h)) { setVal(1, "humidity", "%", h); cnt++; }
        return cnt;
    }

    uint8_t valueCount() const override { return 2; }
    const SensorValue& getValue(uint8_t idx) const override { return _values[idx]; }
    const char* typeName() const override {
        return _dhtType == DHT22 ? "DHT22" : "DHT11";
    }

private:
    uint8_t _pin;
    uint8_t _dhtType;
    DHT _dht;
};
