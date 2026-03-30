#pragma once
// ============== MQTT Telemetry Client ==============

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

class MQTTClient {
public:
    void begin(const char* host, uint16_t port,
               const char* user, const char* pass,
               const char* topic, const char* clientId);

    // Call in loop
    void tick();

    // Publish a JSON telemetry payload
    bool publish(const char* json);

    // Templated: publish from JsonDocument
    bool publishJson(JsonDocument& doc);

    bool isConnected();
    void setInterval(uint16_t seconds) { _intervalMs = (uint32_t)seconds * 1000; }
    uint32_t interval() const { return _intervalMs; }

    // Returns true if it's time to send telemetry
    bool ready();

private:
    WiFiClient _wifiClient;
    PubSubClient _mqtt;
    const char* _host = "";
    uint16_t _port = 1883;
    const char* _user = "";
    const char* _pass = "";
    const char* _topic = "";
    const char* _clientId = "";
    uint32_t _intervalMs = 10000;
    uint32_t _lastPublish = 0;
    uint32_t _lastReconnect = 0;

    void reconnect();
};
