#include "http_reporter.h"
#include "sensors/sensor_base.h"

int HTTPReporter::postSensor(const char* url, const char* device,
                              uint8_t slot, const SensorBase* sensor,
                              const char* label) {
    if (!sensor || !sensor->isReady()) return -3;

    String json = "{\"device\":\"";
    json += device;
    json += "\",\"slot\":";
    json += slot;
    json += ",\"label\":\"";
    json += label;
    json += "\",\"data\":{";

    bool first = true;
    for (uint8_t v = 0; v < sensor->valueCount(); v++) {
        const SensorValue& sv = sensor->getValue(v);
        if (!sv.valid) continue;
        if (!first) json += ',';
        first = false;
        json += '"'; json += sv.name; json += "\":";
        json += String(sv.value, 3);
    }
    json += "}}";
    return post(url, json);
}
