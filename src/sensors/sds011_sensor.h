#pragma once
// ============== SDS011 Dust Sensor (UART) ==============
// Passive mode: sensor sends data every 1 second automatically.
// We just read the latest frame available in the buffer.
// pin  = UART RX pin
// pin2 = UART TX pin
// Frame: AA C0 PM25_L PM25_H PM10_L PM10_H 0 0 CHECKSUM AB

#include "sensor_base.h"
#include <HardwareSerial.h>

class SDS011Sensor : public SensorBase {
public:
    SDS011Sensor(uint8_t rxPin, uint8_t txPin, uint8_t uartNum = 1)
        : _rx(rxPin), _tx(txPin), _uartNum(uartNum) {}

    bool begin() override {
        _serial = new HardwareSerial(_uartNum);
        _serial->begin(9600, SERIAL_8N1, _rx, _tx);
        delay(100);
        _ready = true;
        Serial.printf("[SDS011] UART%d RX=%d TX=%d\n", _uartNum, _rx, _tx);
        return true;
    }

    uint8_t read() override {
        clearValues();
        if (!_ready || !_serial) return 0;

        // Find a complete 10-byte frame in buffer
        uint32_t deadline = millis() + 1500; // up to 1.5s for a frame
        uint8_t buf[10];
        while (millis() < deadline) {
            // Wait for start byte
            while (_serial->available() && (uint8_t)_serial->peek() != 0xAA)
                _serial->read();
            if (_serial->available() < 10) { delay(5); continue; }

            _serial->readBytes(buf, 10);
            if (buf[0] != 0xAA || buf[1] != 0xC0 || buf[9] != 0xAB) continue;

            uint8_t chk = 0;
            for (int i = 2; i < 8; i++) chk += buf[i];
            if (buf[8] != chk) continue;

            float pm25 = (buf[3]*256 + buf[2]) / 10.0f;
            float pm10 = (buf[5]*256 + buf[4]) / 10.0f;
            setVal(0, "pm2.5", "\xCE\xBCg/m3", pm25);
            setVal(1, "pm10",  "\xCE\xBCg/m3", pm10);
            return 2;
        }
        return 0;
    }

    ~SDS011Sensor() {
        if (_serial) { _serial->end(); delete _serial; }
    }

    uint8_t valueCount() const override { return 2; }
    const SensorValue& getValue(uint8_t idx) const override { return _values[idx]; }
    const char* typeName() const override { return "SDS011"; }

private:
    uint8_t _rx, _tx, _uartNum;
    HardwareSerial* _serial = nullptr;
};
