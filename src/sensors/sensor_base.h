#pragma once
// ============== Abstract Sensor Interface ==============
// All sensor drivers inherit from this base

#include <Arduino.h>
#include "driver/adc.h"

// Single reading value with name and unit
struct SensorValue {
    char name[16];   // e.g. "temperature"
    char unit[8];    // e.g. "°C"
    float value;
    bool valid;
};

#define MAX_VALUES_PER_SENSOR 4

class SensorBase {
public:
    virtual ~SensorBase() {}

    // Initialize hardware. Returns true on success.
    virtual bool begin() = 0;

    // Read sensor data. Returns number of valid readings (0 = error).
    virtual uint8_t read() = 0;

    // Get number of values this sensor provides
    virtual uint8_t valueCount() const = 0;

    // Get a specific reading
    virtual const SensorValue& getValue(uint8_t idx) const = 0;

    // Human-readable sensor type name
    virtual const char* typeName() const = 0;

    // Is sensor initialized and working?
    bool isReady() const { return _ready; }

    // True if reading this sensor requires WiFi to be suspended first.
    virtual bool needsWifiPause() const { return false; }

protected:
    bool _ready = false;
    SensorValue _values[MAX_VALUES_PER_SENSOR];

    // Медианный ADC-фильтр.
    // Для ADC2 пинов: используется adc2_get_raw() с повторами (WiFi может блокировать ADC2).
    // Для ADC1 пинов: простой analogRead(), без конфликтов.
    // ESP32-C3: ADC1 = GPIO0-4, ADC2 = только GPIO5 (ADC2_CHANNEL_0)
    // ESP32:    ADC1 = GPIO32-39, ADC2 = GPIO 0,2,4,12-15,25-27
    static int adcMedian(uint8_t pin, uint8_t n = 32) {
        if (n > 32) n = 32;

        // Маппинг GPIO → ADC2 канал (ADC2_CHANNEL_MAX = не ADC2 пин)
        adc2_channel_t ch = ADC2_CHANNEL_MAX;
#if defined(CONFIG_IDF_TARGET_ESP32C3)
        // ESP32-C3: ADC2 имеет только 1 канал — GPIO5
        if (pin == 5) ch = ADC2_CHANNEL_0;
#else
        // ESP32: стандартный маппинг ADC2 (10 каналов)
        switch (pin) {
            case  0: ch = ADC2_CHANNEL_1; break;
            case  2: ch = ADC2_CHANNEL_2; break;
            case  4: ch = ADC2_CHANNEL_0; break;
            case 12: ch = ADC2_CHANNEL_5; break;
            case 13: ch = ADC2_CHANNEL_4; break;
            case 14: ch = ADC2_CHANNEL_6; break;
            case 15: ch = ADC2_CHANNEL_3; break;
            case 25: ch = ADC2_CHANNEL_8; break;
            case 26: ch = ADC2_CHANNEL_9; break;
            case 27: ch = ADC2_CHANNEL_7; break;
            default: break;
        }
#endif
        bool isAdc2 = (ch != ADC2_CHANNEL_MAX);

        int buf[32];
        uint8_t count = 0;
        uint32_t deadline = millis() + 500;  // give up after 500 ms

        while (count < n && (millis() < deadline)) {
            int raw = -1;
            if (isAdc2) {
                // adc2_get_raw() returns ESP_ERR_TIMEOUT when WiFi holds the lock.
                // Retry with a short delay — WiFi releases ADC2 during modem-sleep gaps.
                esp_err_t err = adc2_get_raw(ch, ADC_WIDTH_BIT_12, &raw);
                if (err != ESP_OK) {
                    delayMicroseconds(500);  // wait for WiFi to release
                    continue;
                }
            } else {
                raw = analogRead(pin);  // ADC1: no WiFi contention
            }
            buf[count++] = raw;
        }

        if (count == 0) return -1;  // complete failure (WiFi never released)

        // Insertion sort (fast for small N)
        for (uint8_t i = 1; i < count; i++) {
            int key = buf[i];
            int8_t j = (int8_t)(i - 1);
            while (j >= 0 && buf[j] > key) { buf[j + 1] = buf[j]; j--; }
            buf[j + 1] = key;
        }
        return buf[count / 2];  // median
    }

    void clearValues() {
        for (uint8_t i = 0; i < MAX_VALUES_PER_SENSOR; i++) {
            _values[i].valid = false;
            _values[i].value = 0;
        }
    }

    void setVal(uint8_t idx, const char* name, const char* unit, float val) {
        if (idx >= MAX_VALUES_PER_SENSOR) return;
        strlcpy(_values[idx].name, name, sizeof(_values[idx].name));
        strlcpy(_values[idx].unit, unit, sizeof(_values[idx].unit));
        _values[idx].value = val;
        _values[idx].valid = true;
    }
};
