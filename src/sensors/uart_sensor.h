#pragma once
// ============== Generic UART Line Sensor ==============
// Reads one text line per second. Parses:
//   JSON: {"temperature":23.5,"humidity":60.2}  (up to 4 numeric keys)
//   CSV:  23.5,60.2,1013.0                       (up to 4 comma-separated floats)
// Values are exposed as val0..valN or by JSON key name.

#include "sensor_base.h"
#include <HardwareSerial.h>
#include <ArduinoJson.h>

class UartGenericSensor : public SensorBase {
public:
    UartGenericSensor(uint8_t rxPin, uint8_t txPin,
                      uint32_t baud = 9600, uint8_t uartNum = 1)
        : _rx(rxPin), _tx(txPin), _baud(baud), _uartNum(uartNum) {}

    bool begin() override {
        _serial = new HardwareSerial(_uartNum);
        _serial->begin(_baud, SERIAL_8N1, _rx, _tx);
        _serial->setTimeout(1100);
        delay(100);
        _ready = true;
        Serial.printf("[UART] Generic UART%d RX=%d TX=%d @%lu\n",
                      _uartNum, _rx, _tx, (unsigned long)_baud);
        return true;
    }

    uint8_t read() override {
        clearValues();
        if (!_ready || !_serial) return 0;
        if (!_serial->available()) return 0;

        String line = _serial->readStringUntil('\n');
        line.trim();
        if (!line.length()) return 0;

        // Try JSON parse first
        if (line.startsWith("{")) {
            JsonDocument doc;
            if (deserializeJson(doc, line) == DeserializationError::Ok) {
                uint8_t idx = 0;
                for (JsonPair kv : doc.as<JsonObject>()) {
                    if (idx >= MAX_VALUES_PER_SENSOR) break;
                    if (kv.value().is<float>() || kv.value().is<int>()) {
                        setVal(idx, kv.key().c_str(), "", kv.value().as<float>());
                        idx++;
                    }
                }
                return idx;
            }
        }

        // CSV fallback
        uint8_t idx = 0;
        int start = 0;
        while (idx < MAX_VALUES_PER_SENSOR) {
            int comma = line.indexOf(',', start);
            String tok = (comma < 0) ? line.substring(start)
                                     : line.substring(start, comma);
            tok.trim();
            if (tok.length()) {
                char name[8]; snprintf(name, sizeof(name), "val%d", idx);
                setVal(idx, name, "", tok.toFloat());
                idx++;
            }
            if (comma < 0) break;
            start = comma + 1;
        }
        return idx;
    }

    ~UartGenericSensor() {
        if (_serial) { _serial->end(); delete _serial; }
    }

    uint8_t valueCount() const override { return MAX_VALUES_PER_SENSOR; }
    const SensorValue& getValue(uint8_t idx) const override { return _values[idx]; }
    const char* typeName() const override { return "UART Generic"; }

private:
    uint8_t  _rx, _tx;
    uint32_t _baud;
    uint8_t  _uartNum;
    HardwareSerial* _serial = nullptr;
};
