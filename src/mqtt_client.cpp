#include "mqtt_client.h"

void MQTTClient::begin(const char* host, uint16_t port,
                       const char* user, const char* pass,
                       const char* topic, const char* clientId) {
    _host = host;
    _port = port;
    _user = user;
    _pass = pass;
    _topic = topic;
    _clientId = clientId;

    _mqtt.setClient(_wifiClient);
    _mqtt.setServer(_host, _port);
    _mqtt.setBufferSize(512);

    Serial.printf("[MQTT] Configured: %s:%d topic=%s\n", _host, _port, _topic);
}

void MQTTClient::tick() {
    if (!strlen(_host)) return;
    if (!WiFi.isConnected()) return;

    if (!_mqtt.connected()) {
        if (millis() - _lastReconnect > 5000) {
            reconnect();
            _lastReconnect = millis();
        }
    }
    _mqtt.loop();
}

void MQTTClient::reconnect() {
    Serial.printf("[MQTT] Connecting to %s:%d...\n", _host, _port);
    bool ok;
    if (strlen(_user) > 0) {
        ok = _mqtt.connect(_clientId, _user, _pass);
    } else {
        ok = _mqtt.connect(_clientId);
    }
    if (ok) {
        Serial.println(F("[MQTT] Connected!"));
    } else {
        Serial.printf("[MQTT] Failed, rc=%d\n", _mqtt.state());
    }
}

bool MQTTClient::publish(const char* json) {
    if (!_mqtt.connected()) return false;
    bool ok = _mqtt.publish(_topic, json);
    if (ok) {
        _lastPublish = millis();
        Serial.printf("[MQTT] Published to %s (%d bytes)\n", _topic, strlen(json));
    }
    return ok;
}

bool MQTTClient::publishJson(JsonDocument& doc) {
    char buf[512];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0) return false;
    return publish(buf);
}

bool MQTTClient::isConnected() {
    return _mqtt.connected();
}

bool MQTTClient::ready() {
    return (millis() - _lastPublish >= _intervalMs);
}
