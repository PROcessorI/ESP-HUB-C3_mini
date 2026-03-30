#pragma once
// ============== Sensor Manager ==============
// Creates, initializes and polls sensors based on config.
// Routes output per-sensor: MQTT, HTTP POST, CAN TX, Serial.

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "sensors/sensor_base.h"
#include "can_manager.h"
#include "http_reporter.h"
#include "mqtt_client.h"
#include "wifi_manager.h"

class SensorManager {
public:
    ~SensorManager() { destroyAll(); }

    // Set external managers before begin()
    void setCANManager(CANManager* can)   { _can = can; }
    void setHTTPReporter(HTTPReporter* h) { _http = h; }
    void setMQTTClient(MQTTClient* m)     { _mqtt = m; }
    void setWiFiManager(WiFiManager* w)   { _wifi = w; }

    // Create sensor instances from config
    void begin(HubConfig& cfg);

    // Poll all active sensors
    void readAll();

    // Build MQTT JSON payload (all MQTT-protocol sensors)
    void buildJson(JsonDocument& doc, const char* deviceName);

    // Route output for all sensors according to outProto
    void publishAll(const char* deviceName);

    // Access sensor instances
    SensorBase* getSensor(uint8_t slot) { return _sensors[slot]; }
    uint8_t slotCount() const { return MAX_SENSORS; }
    uint8_t totalValues() const;

private:
    SensorBase*   _sensors[MAX_SENSORS] = {};
    HubConfig*    _cfg  = nullptr;
    CANManager*   _can  = nullptr;
    HTTPReporter* _http = nullptr;
    MQTTClient*   _mqtt = nullptr;
    WiFiManager*  _wifi = nullptr;

    void destroyAll();
    SensorBase* createSensor(const SensorConfig& sc);
    BusType     effectiveBus(const SensorConfig& sc);
};
