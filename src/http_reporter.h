#pragma once
// ============== HTTP Reporter ==============
// HTTP POST JSON telemetry to a per-sensor webhook URL.
// Usage: reporter.post(url, json_string);

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

class HTTPReporter {
public:
    // POST a JSON string to url. Returns HTTP response code (200=OK, <0=error).
    int post(const char* url, const String& json) {
        if (!url || strlen(url) == 0) return -1;
        if (WiFi.status() != WL_CONNECTED) return -2;

        HTTPClient http;
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(5000);

        int code = http.POST((uint8_t*)json.c_str(), json.length());
        if (code < 0) {
            Serial.printf("[HTTP] POST to %s failed: %d\n", url, code);
        } else {
            Serial.printf("[HTTP] POST to %s -> %d\n", url, code);
        }
        http.end();
        return code;
    }

    // Build a simple JSON from a sensor slot and POST it
    int postSensor(const char* url, const char* device,
                   uint8_t slot, const class SensorBase* sensor,
                   const char* label);
};
