#include "wifi_manager.h"
#include <esp_wifi.h>

void WiFiManager::begin(const char* staSSID, const char* staPass,
                        const char* apSSID, const char* apPass,
                        bool apNat) {
    _apSSID  = apSSID;
    _apPass  = apPass;
    _staSSID = staSSID;
    _staPass = staPass;
    _lastApIp = IPAddress(0,0,0,0);

    WiFi.persistent(false);
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    delay(200);

    if (strlen(_staSSID) > 0) {
        WiFi.mode(WIFI_AP_STA);
        delay(200);
        WiFi.setSleep(false);
        WiFi.softAPConfig(IPAddress(192,168,4,1),
                          IPAddress(192,168,4,1),
                          IPAddress(255,255,255,0));
        WiFi.softAP(_apSSID, _apPass);
        delay(500);

        _apRunning = true;
        _lastApIp = WiFi.softAPIP();

        Serial.println(F("\n╔════════════════════════════════════════╗"));
        Serial.printf("║  Access Point Initialized:           ║\n");
        Serial.printf("║   SSID: %-33s ║\n", _apSSID);
        Serial.printf("║   IP: %-35s ║\n", WiFi.softAPIP().toString().c_str());
        Serial.println(F("╚════════════════════════════════════════╝\n"));

    } else {
        startAP();
    }
}

WiFiState WiFiManager::tick() {
    switch (_state) {
        case WIFI_ST_CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                _state = WIFI_ST_CONNECTED;
                Serial.println(F("\n╔════════════════════════════════════════╗"));
                Serial.printf("║ ✓ WiFi Connected to: %-22s ║\n", WiFi.SSID().c_str());
                Serial.printf("║   IP Address: %-29s ║\n", WiFi.localIP().toString().c_str());
                Serial.printf("║   RSSI: %-34d dBm ║\n", WiFi.RSSI());
                Serial.println(F("╚════════════════════════════════════════╝\n"));
            } else if (millis() - _connectStart > 45000) {
                _state = WIFI_ST_AP_MODE;
            }
            break;

        case WIFI_ST_CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                _state = WIFI_ST_CONNECTING;
                _connectStart = millis();
                WiFi.reconnect();
            }
            break;

        case WIFI_ST_AP_MODE:
            if (WiFi.status() == WL_CONNECTED) {
                _state = WIFI_ST_CONNECTED;
            }
            break;

        default:
            break;
    }
    return _state;
}

void WiFiManager::startAP() {
    WiFi.mode(WIFI_AP);
    delay(200);
    WiFi.setSleep(false);
    WiFi.softAPConfig(IPAddress(192,168,4,1),
                      IPAddress(192,168,4,1),
                      IPAddress(255,255,255,0));
    WiFi.softAP(_apSSID, _apPass);
    delay(500);
    _apRunning = true;
    _lastApIp = WiFi.softAPIP();
    _state = WIFI_ST_AP_MODE;
    
    Serial.println(F("\n╔════════════════════════════════════════╗"));
    Serial.printf("║ ⚠ Access Point (AP) Started:           ║\n");
    Serial.printf("║   SSID: %-33s ║\n", _apSSID);
    Serial.printf("║   IP Address: %-29s ║\n", WiFi.softAPIP().toString().c_str());
    Serial.println(F("╚═══════════��════════════════════════════╝\n"));
}

String WiFiManager::localIP() const {
    return WiFi.localIP().toString();
}

String WiFiManager::apIP() const {
    return WiFi.softAPIP().toString();
}

int WiFiManager::rssi() const {
    if (WiFi.status() == WL_CONNECTED) return WiFi.RSSI();
    return 0;
}

String WiFiManager::macAddress() const {
    return WiFi.macAddress();
}