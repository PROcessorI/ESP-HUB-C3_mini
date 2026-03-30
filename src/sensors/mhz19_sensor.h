#pragma once
// ============== MH-Z19B CO2 Sensor (UART) ==============
// Commands sent every read() call; sensor takes ~30ms to respond.
// pin  = UART RX pin (ESP32 side)
// pin2 = UART TX pin (ESP32 side)
// uartNum = 1 (Serial1) or 2 (Serial2)

#include "sensor_base.h"
#include <HardwareSerial.h>

class MHZ19Sensor : public SensorBase {
public:
    MHZ19Sensor(uint8_t rxPin, uint8_t txPin, uint8_t uartNum = 1)
        : _rx(rxPin), _tx(txPin), _uartNum(uartNum) {}

    bool begin() override {
        _serial = new HardwareSerial(_uartNum);
        _serial->begin(9600, SERIAL_8N1, _rx, _tx);
        delay(100);
        _ready = true;
        Serial.printf("[MHZ19] UART%d RX=%d TX=%d\n", _uartNum, _rx, _tx);
        return true;
    }

    uint8_t read() override {
        clearValues();
        if (!_ready || !_serial) return 0;

        // Send read command
        static const uint8_t cmd[] = {0xFF,0x01,0x86,0,0,0,0,0,0x79};
        _serial->write(cmd, 9);

        // Wait for 9-byte response (max 100ms)
        uint32_t t = millis();
        while (_serial->available() < 9 && millis() - t < 100) delay(1);
        if (_serial->available() < 9) return 0;

        uint8_t buf[9];
        _serial->readBytes(buf, 9);

        // Validate
        if (buf[0] != 0xFF || buf[1] != 0x86) return 0;
        uint8_t chk = 0;
        for (int i = 1; i < 8; i++) chk += buf[i];
        chk = (~chk) + 1;
        if (buf[8] != chk) return 0;

        int co2 = buf[2] * 256 + buf[3];
        int temp = (int)buf[4] - 40;

        setVal(0, "co2",  "ppm", (float)co2);
        setVal(1, "temp", "\xC2\xB0""C", (float)temp);
        return 2;
    }

    ~MHZ19Sensor() {
        if (_serial) { _serial->end(); delete _serial; }
    }

    uint8_t valueCount() const override { return 2; }
    const SensorValue& getValue(uint8_t idx) const override { return _values[idx]; }
    const char* typeName() const override { return "MH-Z19"; }

private:
    uint8_t _rx, _tx, _uartNum;
    HardwareSerial* _serial = nullptr;
};
