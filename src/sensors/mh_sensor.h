#pragma once
// ============== MH-Sensor Series ==============
// Generic analog+digital dual-output sensor module
//
// Popular variants:
//   MH-LDR  — photoresistor / light intensity
//   MH-FL   — flame / fire detector
//   MH-WS   — rain / water surface
//   MH-MS   — soil moisture
//   MH-HALL — hall effect / magnetic field
//   MH-IR   — IR obstacle / proximity
//   MH-MA   — sound / microphone (peak)
//   MH-GAS  — generic gas (paired with MQ module)
//
// Wiring:
//   AO  → GPIO pin  (analog output, 0–3.3V proportional to measured value)
//   DO  → GPIO pin2 (digital threshold, HIGH/LOW; 0 = not connected)
//   VCC → 3.3V (some modules tolerant of 5V, check datasheet)
//   GND → GND
//
// Outputs:
//   raw      — raw 12-bit ADC value (0–4095)
//   voltage  — AO voltage (V)
//   percent  — relative intensity (0–100 %), inverted:
//              100% = maximum measuring (most light/moisture/etc.)
//              0%   = minimum (nothing measured)
//   digital  — DO pin state (1 = threshold triggered), only if pin2 > 0
//
// ADC constraint (ESP32): same as SENSOR_ANALOG
//   ADC1 (GPIO32-39) — always works, including when WiFi is ON  ✓
//   ADC2 (GPIO0,2,4,12-15,25-27) — blocked by WiFi radio        ✗
//
// Threshold (potentiometer on DO):
//   Turn clockwise  → raise threshold (DO goes HIGH earlier)
//   Turn counter-CW → lower threshold

#include "sensor_base.h"

// ADC2 pin table for ESP32 (separate from analog_sensor.h to avoid include conflicts)
static const uint8_t kMhAdc2Pins[] = { 0, 2, 4, 12, 13, 14, 15, 25, 26, 27 };

static bool mhIsAdc2Pin(uint8_t pin) {
    for (uint8_t i = 0; i < sizeof(kMhAdc2Pins); i++) {
        if (kMhAdc2Pins[i] == pin) return true;
    }
    return false;
}

class MHSensor : public SensorBase {
public:
    // pin  = AO analog output pin (required)
    // pin2 = DO digital output pin (optional, 0 = not connected)
    MHSensor(uint8_t pin, uint8_t pin2 = 0) : _aoPin(pin), _doPin(pin2) {}

    bool begin() override {
        pinMode(_aoPin, INPUT);
        analogReadResolution(12);

        if (_doPin > 0) {
            pinMode(_doPin, INPUT);
            Serial.printf("[MH] DO digital pin: GPIO%d\n", _doPin);
        }

        _ready = true;

        if (mhIsAdc2Pin(_aoPin)) {
            Serial.printf("[MH] WARNING: GPIO%d = ADC2 — не работает при активном WiFi!\n", _aoPin);
            Serial.printf("[MH] Используйте GPIO32-39 (ADC1) для надёжной работы с WiFi.\n");
        } else {
            Serial.printf("[MH] AO: GPIO%d (ADC1 — совместим с WiFi)%s\n",
                          _aoPin, _doPin > 0 ? "" : "  DO: не подключён");
        }
        return true;
    }

    uint8_t read() override {
        clearValues();
        if (!_ready) return 0;

        int raw = adcMedian(_aoPin);  // median of 32 samples — filters WiFi RF bursts on ADC2
        float voltage = raw * 3.3f / 4095.0f;
        float percent = 100.0f - (raw * 100.0f / 4095.0f);

        setVal(0, "raw",     "",  (float)raw);
        setVal(1, "voltage", "V", voltage);
        setVal(2, "percent", "%", percent);

        if (_doPin > 0) {
            setVal(3, "digital", "", (float)digitalRead(_doPin));
            return 4;
        }
        return 3;
    }

    uint8_t valueCount()                  const override { return (_doPin > 0) ? 4 : 3; }
    const SensorValue& getValue(uint8_t idx) const override { return _values[idx]; }
    const char* typeName()                const override { return "MH-Sensor"; }
    bool isADC2()                         const          { return mhIsAdc2Pin(_aoPin); }
    bool needsWifiPause()                 const override { return false; }  // oversampling handles ADC2

private:
    uint8_t _aoPin;
    uint8_t _doPin;
};
