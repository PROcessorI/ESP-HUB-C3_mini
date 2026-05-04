#pragma once
// ============== WiFi Manager (Minimal) ==============
// Handles AP mode for mesh network

#include <Arduino.h>
#include <WiFi.h>

enum WiFiState : uint8_t {
    WIFI_ST_IDLE,
    WIFI_ST_CONNECTING,
    WIFI_ST_CONNECTED,
    WIFI_ST_AP_MODE
};

class WiFiManager {
public:
    void begin(const char* staSSID, const char* staPass,
               const char* apSSID, const char* apPass,
               bool apNat = false);

    WiFiState tick();

    WiFiState state() const { return _state; }
    bool isConnected() const { return _state == WIFI_ST_CONNECTED; }
    bool isAP() const { return _state == WIFI_ST_AP_MODE || _apRunning; }

    String localIP() const;
    String apIP() const;
    int rssi() const;
    String macAddress() const;

private:
    WiFiState _state = WIFI_ST_IDLE;
    uint32_t _connectStart = 0;
    uint32_t _connectTimeout = 45000;
    const char* _apSSID  = nullptr;
    const char* _apPass  = nullptr;
    const char* _staSSID = nullptr;
    const char* _staPass = nullptr;
    uint32_t    _lastRetry = 0;
    uint8_t     _noApFoundStreak = 0;
    uint32_t    _retryBlockedUntil = 0;

    void startAP();
    bool _apRunning   = false;
    IPAddress _lastApIp = IPAddress(0,0,0,0);
};
