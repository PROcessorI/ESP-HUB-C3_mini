#include "sensor_manager.h"
// GPIO / 1-Wire
#include "sensors/dht_sensor.h"
#include "sensors/ds18b20_sensor.h"
#include "sensors/analog_sensor.h"
#include "sensors/mh_sensor.h"
// I2C
#include "sensors/bmp280_sensor.h"
#include "sensors/bh1750_sensor.h"
// UART
#include "sensors/mhz19_sensor.h"
#include "sensors/sds011_sensor.h"
#include "sensors/uart_sensor.h"
// CAN
#include "sensors/can_sensor.h"
#include <DHT.h>

// ----------------------------------------------------------------
// Determine effective bus (resolve BUS_AUTO)
// ----------------------------------------------------------------
BusType SensorManager::effectiveBus(const SensorConfig& sc) {
    if (sc.bus != BUS_AUTO) return sc.bus;
    return defaultBusForType(sc.type);
}

// ----------------------------------------------------------------
// Sensor factory
// ----------------------------------------------------------------
SensorBase* SensorManager::createSensor(const SensorConfig& sc) {
    switch (sc.type) {
        // GPIO
        case SENSOR_DHT11:        return new DHTSensor(sc.pin, DHT11);
        case SENSOR_DHT22:        return new DHTSensor(sc.pin, DHT22);
        case SENSOR_DS18B20:      return new DS18B20Sensor(sc.pin);
        case SENSOR_ANALOG:       return new AnalogSensor(sc.pin);
        case SENSOR_MH_SERIES:    return new MHSensor(sc.pin, sc.pin2);  // pin=AO, pin2=DO
        // I2C
        case SENSOR_BMP280:       return new BMP280Sensor(sc.pin);    // pin = I2C addr
        case SENSOR_BH1750:       return new BH1750Sensor(sc.pin);    // pin = I2C addr
        // UART
        case SENSOR_MHZ19:
            return new MHZ19Sensor(sc.pin, sc.pin2, sc.uartNum);
        case SENSOR_SDS011:
            return new SDS011Sensor(sc.pin, sc.pin2, sc.uartNum);
        case SENSOR_UART_GENERIC:
            return new UartGenericSensor(sc.pin, sc.pin2, sc.baud, sc.uartNum);
        // CAN
        case SENSOR_CAN_RAW:
            if (_can) return new CANRawSensor(_can, sc.canId, sc.canDlc);
            Serial.println(F("[SENS] CAN sensor: CANManager not set"));
            return nullptr;
        default: return nullptr;
    }
}

// ----------------------------------------------------------------
// begin()
// ----------------------------------------------------------------
void SensorManager::begin(HubConfig& cfg) {
    _cfg = &cfg;
    destroyAll();
    Serial.println(F("[SENS] Initializing sensors..."));

    for (int i = 0; i < MAX_SENSORS; i++) {
        const SensorConfig& sc = cfg.sensors[i];
        if (!sc.enabled || sc.type == SENSOR_NONE) continue;

        BusType bus = effectiveBus(sc);
        Serial.printf("[SENS] Slot %d: %s bus=%s out=%s\n", i,
            sensorTypeName(sc.type), busTypeName(bus), outProtoName(sc.outProto));

        _sensors[i] = createSensor(sc);
        if (_sensors[i] && !_sensors[i]->begin()) {
            Serial.printf("[SENS] Slot %d init failed\n", i);
        }
    }
}

// ----------------------------------------------------------------
// readAll()
// ----------------------------------------------------------------
void SensorManager::readAll() {
    // Check if any active sensor needs WiFi paused (ADC2 pins)
    bool needsPause = false;
    if (_wifi) {
        for (int i = 0; i < MAX_SENSORS; i++) {
            if (_sensors[i] && _sensors[i]->isReady() && _sensors[i]->needsWifiPause()) {
                needsPause = true;
                break;
            }
        }
    }

    // Pause WiFi once for all ADC2 sensors — minimises radio-off time
    if (needsPause) {
        _wifi->pauseRadio();
    }

    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!_sensors[i] || !_sensors[i]->isReady()) continue;

        // If WiFi is NOT paused but this sensor needs a pause — shouldn't happen;
        // but handle gracefully (no WiFiManager configured)
        _sensors[i]->read();
    }

    if (needsPause) {
        _wifi->resumeRadio();
    }
}

// ----------------------------------------------------------------
// buildJson() — MQTT payload (all sensors with OUT_MQTT)
// ----------------------------------------------------------------
void SensorManager::buildJson(JsonDocument& doc, const char* deviceName) {
    doc["device"] = deviceName;
    doc["uptime"] = millis() / 1000;
#include <time.h>
    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);
    if (ti.tm_year > 100) {
        char tb[16];
        snprintf(tb, sizeof(tb), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
        doc["sys_time"] = tb;
    }
    doc["heap"]   = ESP.getFreeHeap();

    JsonObject data = doc["sensors"].to<JsonObject>();

    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!_sensors[i] || !_sensors[i]->isReady()) continue;
        if (_cfg && _cfg->sensors[i].outProto != OUT_MQTT) continue;

        for (uint8_t v = 0; v < _sensors[i]->valueCount(); v++) {
            const SensorValue& sv = _sensors[i]->getValue(v);
            if (sv.valid) {
                char key[32];
                snprintf(key, sizeof(key), "%d_%s", i, sv.name);
                data[key] = serialized(String(sv.value, 2));
            }
        }
    }
}

// ----------------------------------------------------------------
// publishAll() — route each sensor to its configured outProto
// ----------------------------------------------------------------
void SensorManager::publishAll(const char* deviceName) {
    if (!_cfg) return;

    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!_sensors[i] || !_sensors[i]->isReady()) continue;
        const SensorConfig& sc = _cfg->sensors[i];

        switch (sc.outProto) {
            case OUT_MQTT:
                // handled by main buildJson() + mqttClient
                break;

            case OUT_HTTP:
                if (_http && strlen(sc.httpUrl) > 0) {
                    _http->postSensor(sc.httpUrl, deviceName, i, _sensors[i], sc.label);
                }
                break;

            case OUT_CAN:
                if (_can && _can->isRunning()) {
                    // Pack up to 4 int16 values into CAN frame (max 8 bytes)
                    uint8_t buf[8] = {};
                    uint8_t dlc = 0;
                    for (uint8_t v = 0; v < _sensors[i]->valueCount() && dlc + 2 <= 8; v++) {
                        const SensorValue& sv = _sensors[i]->getValue(v);
                        if (!sv.valid) continue;
                        int16_t ival = (int16_t)sv.value;
                        buf[dlc++] = ival & 0xFF;
                        buf[dlc++] = (ival >> 8) & 0xFF;
                    }
                    _can->sendFrame(sc.canId, buf, dlc);
                }
                break;

            case OUT_SERIAL:
                // Disabled automatic output to keep serial console clean.
                // Use 'read' or 'sensors' commands in serial console instead.
                break;

            default:
                break;
        }
    }
}

// ----------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------
uint8_t SensorManager::totalValues() const {
    uint8_t total = 0;
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (_sensors[i] && _sensors[i]->isReady()) {
            for (uint8_t v = 0; v < _sensors[i]->valueCount(); v++)
                if (_sensors[i]->getValue(v).valid) total++;
        }
    }
    return total;
}

void SensorManager::destroyAll() {
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (_sensors[i]) { delete _sensors[i]; _sensors[i] = nullptr; }
    }
}
