#pragma once
// ============== Generic Analog Sensor ==============
// Reads raw ADC value and converts to voltage
//
// ВАЖНО: ESP32 имеет два АЦП контроллера:
//   ADC1 (GPIO32-39)  — работает ВСЕГДА, в том числе при активном WiFi ✓
//   ADC2 (GPIO0,2,4,12-15,25-27) — НЕ работает когда WiFi включён ✗
//
// GPIO 4 = ADC2_CH0 — НЕ будет читать данные при активном WiFi!
// Для датчиков света (LDR/фоторезистор) при работающем WiFi используйте:
//   GPIO32, GPIO33, GPIO34, GPIO35, GPIO36, GPIO39

#include "sensor_base.h"

// Таблица ADC2 пинов ESP32 (не работают с WiFi)
static const uint8_t kAdc2Pins[] = {0, 2, 4, 12, 13, 14, 15, 25, 26, 27};

static bool isAdc2Pin(uint8_t pin) {
    for (uint8_t i = 0; i < sizeof(kAdc2Pins); i++) {
        if (kAdc2Pins[i] == pin) return true;
    }
    return false;
}

class AnalogSensor : public SensorBase {
public:
    AnalogSensor(uint8_t pin) : _pin(pin) {}

    bool begin() override {
        pinMode(_pin, INPUT);
        analogReadResolution(12);
        _ready = true;

        if (isAdc2Pin(_pin)) {
            Serial.printf("[ANALOG] GPIO%d = ADC2. "
                          "\u0418\u0441\u043f\u043e\u043b\u044c\u0437\u0443\u0435\u0442\u0441\u044f "
                          "\u043c\u0435\u0434\u0438\u0430\u043d\u043d\u0430\u044f "
                          "\u0432\u044b\u0431\u043e\u0440\u043a\u0430 (32 \u0441\u0430\u043c\u043f\u043b\u0430) "
                          "— WiFi \u043d\u0435 \u043e\u0441\u0442\u0430\u043d\u0430\u0432\u043b\u0438\u0432\u0430\u0435\u0442\u0441\u044f.\n", _pin);
        } else {
            Serial.printf("[ANALOG] OK on GPIO%d (ADC1)\n", _pin);
        }
        return true;
    }

    uint8_t read() override {
        clearValues();
        if (!_ready) return 0;

        int raw = adcMedian(_pin);  // median of 32 samples — filters WiFi RF bursts
        float voltage = raw * 3.3f / 4095.0f;

        setVal(0, "raw",     "",  (float)raw);
        setVal(1, "voltage", "V", voltage);
        return 2;
    }

    uint8_t valueCount() const override { return 2; }
    const SensorValue& getValue(uint8_t idx) const override { return _values[idx]; }
    const char* typeName() const override { return "Analog"; }
    bool isADC2()              const { return isAdc2Pin(_pin); }
    bool needsWifiPause() const override { return false; }  // oversampling handles ADC2

private:
    uint8_t _pin;
};
