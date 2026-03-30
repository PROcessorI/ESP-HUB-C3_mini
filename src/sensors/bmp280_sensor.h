#pragma once
// ============== BMP280 Pressure/Temperature Sensor ==============

#include "sensor_base.h"
#include <Wire.h>
#include <Adafruit_BMP280.h>

class BMP280Sensor : public SensorBase {
public:
    // addr: I2C address (0x76 or 0x77), default 0x76
    BMP280Sensor(uint8_t addr = 0x76) : _addr(addr) {}

    bool begin() override {
        _ready = _bmp.begin(_addr);
        if (_ready) {
            _bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                             Adafruit_BMP280::SAMPLING_X2,   // temp
                             Adafruit_BMP280::SAMPLING_X16,  // pressure
                             Adafruit_BMP280::FILTER_X16,
                             Adafruit_BMP280::STANDBY_MS_500);
            Serial.printf("[BMP280] OK at 0x%02X\n", _addr);
        } else {
            Serial.printf("[BMP280] FAIL at 0x%02X\n", _addr);
        }
        return _ready;
    }

    uint8_t read() override {
        clearValues();
        if (!_ready) return 0;

        float t = _bmp.readTemperature();
        float p = _bmp.readPressure() / 100.0f;  // hPa
        float a = _bmp.readAltitude(1013.25f);

        uint8_t cnt = 0;
        if (!isnan(t)) { setVal(0, "temperature", "\xC2\xB0""C", t); cnt++; }
        if (!isnan(p)) { setVal(1, "pressure", "hPa", p); cnt++; }
        if (!isnan(a)) { setVal(2, "altitude", "m", a); cnt++; }
        return cnt;
    }

    uint8_t valueCount() const override { return 3; }
    const SensorValue& getValue(uint8_t idx) const override { return _values[idx]; }
    const char* typeName() const override { return "BMP280"; }

private:
    uint8_t _addr;
    Adafruit_BMP280 _bmp;
};
