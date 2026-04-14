#pragma once
// ============== WiFi Manager ==============
// Handles STA connection with AP fallback
// NAT (IP forwarding) allows AP clients to access internet via STA.
// Requires CONFIG_LWIP_IP_FORWARD=y + CONFIG_LWIP_IPV4_NAPT=y in sdkconfig.defaults.

#include <Arduino.h>
#include <WiFi.h>
#include "dns_proxy.h"

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

    // Call in loop. Returns current state.
    WiFiState tick();

    WiFiState state() const { return _state; }
    bool isConnected() const { return _state == WIFI_ST_CONNECTED; }
    bool isAP() const { return _state == WIFI_ST_AP_MODE || _apRunning; }

    String localIP() const;
    String apIP() const;
    int rssi() const;
    String macAddress() const;

    // BLE coexistence: must be called BEFORE begin().
    // When true, WiFi uses WIFI_PS_MIN_MODEM instead of WIFI_PS_NONE.
    // WIFI_PS_NONE aborts() if BLE is also running (ESP32 hardware requirement).
    void setBluetoothCoex(bool ble) { _bleCoex = ble; }

    // Temporarily stop the WiFi radio so ADC2 pins can be read.
    // Call resumeRadio() immediately after the ADC read.
    void pauseRadio();
    void resumeRadio();
    bool isRadioPaused() const { return _radioPaused; }

    // Runtime radio on/off (serial console).
    // wifiOff() fully kills AP+STA so ADC2 pins become available.
    // wifiOn() restores the radio using the credentials supplied to begin().
    void wifiOff();
    void wifiOn();
    bool isWifiEnabled() const { return !_wifiOff; }

    // Connected clients info
    int apClientsCount() const;
    String apClientsMACs() const; // Returns comma-separated list of MACs

private:
    bool _bleCoex     = false; // true = use modem sleep for BLE coexistence
    bool _radioPaused = false; // true while WiFi stopped for ADC2 reads
    bool _wifiOff     = false; // true when radio killed by wifiOff()
    WiFiState _state = WIFI_ST_IDLE;
    uint32_t _connectStart = 0;
    uint32_t _connectTimeout = 45000; // 45 sec — increased for cold boot WiFi init
    const char* _apSSID  = nullptr;
    const char* _apPass  = nullptr;
    const char* _staSSID = nullptr;
    const char* _staPass = nullptr;
    uint32_t    _lastRetry = 0;
    uint8_t     _noApFoundStreak = 0;
    uint32_t    _retryBlockedUntil = 0;

    void startAP();
    void enableNAT();           // Enable IP forwarding + fix DNS for AP clients
    void ensureNaptInitialized();
    void restoreCaptivePortal(); // Restore captive DNS after STA disconnect
    void reconcileApInterface(); // Handle AP IP changes (e.g. when mesh reconfigures SoftAP)
    bool _natEnabled = false;
    bool _apNatConfig = false;
    bool _naptInitialized = false;
public:
    bool isNatEnabled() const { return _natEnabled; }
private:

    CaptiveDnsProxy _dns;
    bool _dnsStarted  = false;
    bool _apRunning   = false;
    IPAddress _lastApIp = IPAddress(0,0,0,0);
    bool _ntpSynced   = false;  // true once SNTP confirmed time is valid
    uint32_t _ntpCheckMs = 0;   // last time we checked NTP status
};
