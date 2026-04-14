#include "wifi_manager.h"
#include "system_clock.h"
#include <esp_wifi.h>
#include <esp_bt.h>
#include <esp_netif.h>

extern "C" {
#include <lwip/lwip_napt.h>
}

static bool s_staEventHooked = false;
static uint8_t s_authFailStreak = 0;
static bool s_wpa2FallbackApplied = false;

void WiFiManager::begin(const char* staSSID, const char* staPass,
                        const char* apSSID, const char* apPass,
                        bool apNat) {
    _apSSID  = apSSID;
    _apPass  = apPass;
    _staSSID = staSSID;    // Store for periodic retry logic
    _staPass = staPass;
    _apNatConfig = apNat;
    _lastApIp = IPAddress(0,0,0,0);
    _noApFoundStreak = 0;
    _retryBlockedUntil = 0;

    // Reset WiFi state (clear any leftover connections from flash)
    WiFi.persistent(false);
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    delay(200);

    // Always start AP so the hotspot is reachable regardless of STA state
    if (strlen(_staSSID) > 0) {
        // AP+STA simultaneously — hotspot always visible
        WiFi.mode(WIFI_AP_STA);
        delay(200);
        // Keep modem sleep disabled during STA auth/association for more
        // reliable WPA2/WPA3 transition handshakes on ESP32-C3.
        WiFi.setSleep(false);  // WIFI_PS_NONE
        // Explicit AP subnet
        WiFi.softAPConfig(IPAddress(192,168,4,1),
                          IPAddress(192,168,4,1),
                          IPAddress(255,255,255,0));
        // Do not force channel, let it follow STA channel to avoid connection drops
        WiFi.softAP(_apSSID, _apPass);
        delay(500);

        // Verify AP actually started
        if (WiFi.softAPIP() == IPAddress(0,0,0,0)) {
            Serial.println(F("[WIFI] AP start failed, retrying..."));
            delay(500);
            WiFi.softAP(_apSSID, _apPass);
            delay(500);
        }

        _apRunning = true;
        _lastApIp = WiFi.softAPIP();
        Serial.println(F("\n╔════════════════════════════════════════╗"));
        Serial.printf("║  Access Point Initialized:           ║\n");
        Serial.printf("║   SSID: %-33s ║\n", _apSSID);
        Serial.printf("║   IP: %-35s ║\n", WiFi.softAPIP().toString().c_str());
        Serial.println(F("╚════════════════════════════════════════╝\n"));

        // Start DNS proxy (wildcard mode: all -> 192.168.4.1)
        _dns.begin(WiFi.softAPIP());
        _dnsStarted = true;

        Serial.printf("[WIFI] Connecting to '%s'...\n", _staSSID);
        // Hex-dump password bytes to help debug AUTH_FAIL
        {
            int plen = _staPass ? strlen(_staPass) : 0;
            Serial.printf("[WIFI] Pass len=%d bytes: ", plen);
            for (int i = 0; i < plen; i++) Serial.printf("%02X ", (uint8_t)_staPass[i]);
            Serial.println();
        }
        // Keep driver-level reconnect enabled for faster/stabler recovery.
        WiFi.setAutoReconnect(true);

        if (!s_staEventHooked) {
            WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
                if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
                    s_authFailStreak = 0;
                    return;
                }
                if (event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) return;

                uint8_t reason = info.wifi_sta_disconnected.reason;
                if (reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_AUTH_EXPIRE) {
                    if (s_authFailStreak < 0xFF) s_authFailStreak++;
                    if (s_authFailStreak >= 3 && !s_wpa2FallbackApplied) {
#ifdef WIFI_AUTH_WPA2_PSK
                        WiFi.setMinSecurity(WIFI_AUTH_WPA2_PSK);
                        s_wpa2FallbackApplied = true;
                        Serial.println(F("[WIFI] Auth fallback enabled: WPA2 threshold"));
                        WiFi.reconnect();
#endif
                    }
                } else {
                    s_authFailStreak = 0;
                }
            });
            s_staEventHooked = true;
        }

        bool sameSsid = (_apSSID && _staSSID && strcmp(_apSSID, _staSSID) == 0);
        if (sameSsid) {
            Serial.println(F("[WIFI] STA SSID equals AP SSID — skipping STA connect to avoid self-loop"));
            _state = WIFI_ST_AP_MODE;
            _lastRetry = millis();
        } else {
            WiFi.begin(_staSSID, _staPass);
            _state = WIFI_ST_CONNECTING;
            _connectStart = millis();
            _lastRetry = millis();
        }
    } else {
        // No STA credentials — pure AP mode
        Serial.println(F("[WIFI] No SSID configured, starting AP"));
        startAP();
    }
}

void WiFiManager::pauseRadio() {
    // Oversampling in sensors eliminates the need for WiFi stop/start.
    // esp_wifi_stop() was destructive — broke AP and STA state machines.
    // This method is kept for API compatibility but is intentionally a no-op.
    _radioPaused = true;
}

void WiFiManager::resumeRadio() {
    _radioPaused = false;
}

// ── Runtime radio kill/restore (serial console commands) ──────────────────────
void WiFiManager::wifiOff() {
    if (_wifiOff) {
        Serial.println(F("[WIFI] Radio already OFF"));
        return;
    }
    _wifiOff = true;

    // Stop captive DNS before killing the radio
    if (_dnsStarted) { _dns.stop(); _dnsStarted = false; }

    // Gracefully disconnect before changing mode avoids STA state-machine junk
    WiFi.disconnect(false);
    WiFi.softAPdisconnect(false);
    delay(100);
    WiFi.mode(WIFI_OFF);
    _apRunning = false;
    _state     = WIFI_ST_IDLE;
    Serial.println(F("[WIFI] Radio OFF — ADC2 pins now available"));
}

void WiFiManager::wifiOn() {
    if (!_wifiOff) {
        Serial.println(F("[WIFI] Radio already ON"));
        return;
    }
    _wifiOff = false;

    // Reuse stored credentials — same path as initial begin()
    if (_staSSID && strlen(_staSSID)) {
        begin(_staSSID, _staPass, _apSSID, _apPass, _apNatConfig);
    } else {
        // AP-only mode
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
        WiFi.softAP(_apSSID, _apPass);
        delay(500);
        _apRunning = true;
        _dns.begin(WiFi.softAPIP());
        _dnsStarted = true;
        Serial.println(F("\n╔════════════════════════════════════════╗"));
        Serial.printf("║ 🌐 Access Point Restored:              ║\n");
        Serial.printf("║   IP: %-35s ║\n", WiFi.softAPIP().toString().c_str());
        Serial.println(F("╚════════════════════════════════════════╝\n"));
    }
}

WiFiState WiFiManager::tick() {
    reconcileApInterface();

    // Feed DNS if AP is running
    if (_dnsStarted) _dns.tick();

    switch (_state) {
        case WIFI_ST_CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                _state = WIFI_ST_CONNECTED;
                _noApFoundStreak = 0;
                _retryBlockedUntil = 0;
                
                // Prominent WiFi connection announcement
                Serial.println(F("\n╔════════════════════════════════════════╗"));
                Serial.printf("║ ✓ WiFi Connected to: %-22s ║\n", WiFi.SSID().c_str());
                Serial.printf("║   IP Address: %-29s ║\n", WiFi.localIP().toString().c_str());
                Serial.printf("║   RSSI: %-34d dBm ║\n", WiFi.RSSI());
                Serial.println(F("╚════════════════════════════════════════╝\n"));
                
                enableNAT(); // Enable IP forwarding
                
                // Initialize Time (NTP) — MSK+3 using POSIX tz string
                // configTzTime sets timezone AND starts SNTP in one call
                configTzTime("MSK-3", "pool.ntp.org", "time.nist.gov", "time.google.com");
                Serial.println(F("[NTP] SNTP started (MSK+3), waiting for sync..."));
                // Brief wait — confirm sync within 5 seconds (non-blocking check)
                {
                    unsigned long t0 = millis();
                    struct tm ti;
                    while (millis() - t0 < 5000) {
                        if (getLocalTime(&ti, 100) && ti.tm_year > 100) {
                            Serial.printf("[NTP] Time synced: %02d:%02d:%02d %02d.%02d.%04d\n",
                                ti.tm_hour, ti.tm_min, ti.tm_sec,
                                ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
                            // Синхронизировать системные часы
                            time_t ntpTime;
                            time(&ntpTime);
                            systemClock.syncFromNTP(ntpTime);
                            break;
                        }
                    }
                    if (!(ti.tm_year > 100)) {
                        Serial.println(F("[NTP] Sync pending — will retry in background"));
                    }
                }
                
            } else if (WiFi.status() == WL_NO_SSID_AVAIL && millis() - _connectStart > 10000) {
                if (_noApFoundStreak < 10) _noApFoundStreak++;
                uint32_t pauseMs = (uint32_t)_noApFoundStreak * 30000UL;
                if (pauseMs > 300000UL) pauseMs = 300000UL;
                _retryBlockedUntil = millis() + pauseMs;
                Serial.printf("[WIFI] STA AP not found — AP-only mode for %lus (streak=%u)\n",
                              (unsigned long)(pauseMs / 1000UL),
                              (unsigned)_noApFoundStreak);
                _state = WIFI_ST_AP_MODE;
                _lastRetry = millis();
            } else if (millis() - _connectStart > 45000) { // Timeout 45s (increased for cold boot)
                Serial.println(F("[WIFI] STA connect timeout, falling back to AP-only logically"));
                if (_retryBlockedUntil < millis() + 30000UL) {
                    _retryBlockedUntil = millis() + 30000UL;
                }
                _state = WIFI_ST_AP_MODE;
                _lastRetry = millis();
            }
            break;

        case WIFI_ST_CONNECTED:
            // Periodically confirm NTP sync (every 10 sec until synced)
            if (!_ntpSynced && millis() - _ntpCheckMs >= 60000) {
                _ntpCheckMs = millis();
                struct tm ti;
                if (getLocalTime(&ti, 0) && ti.tm_year > 100) {
                    _ntpSynced = true;
                    Serial.printf("[NTP] Time synced: %02d:%02d:%02d %02d.%02d.%04d\n",
                        ti.tm_hour, ti.tm_min, ti.tm_sec,
                        ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
                    time_t ntpTime;
                    time(&ntpTime);
                    systemClock.syncFromNTP(ntpTime);
                } else if (millis() > 30000) {
                    Serial.println(F("[NTP] Still waiting for sync..."));
                }
            }
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println(F("[WIFI] Lost connection, waiting for auto-reconnect..."));
                _natEnabled = false;
                restoreCaptivePortal();
                _state = WIFI_ST_CONNECTING;
                _connectStart = millis();
                WiFi.reconnect();
            }
            break;

        case WIFI_ST_AP_MODE:
            // Check if WiFi auto-reconnect happened in the background
            if (WiFi.status() == WL_CONNECTED) {
                _state = WIFI_ST_CONNECTED;
                _noApFoundStreak = 0;
                _retryBlockedUntil = 0;
                Serial.println(F("\n╔════════════════════════════════════════╗"));
                Serial.println(F("║ ✓ STA Reconnected Automatically:       ║"));
                Serial.printf("║   IP: %-35s ║\n", WiFi.localIP().toString().c_str());
                Serial.println(F("╚════════════════════════════════════════╝\n"));
                enableNAT();
                break;
            }
            if (_retryBlockedUntil && millis() < _retryBlockedUntil) {
                break;
            }
            // Periodically retry manually if auto-reconnect hasn't succeeded
            // Retry every 30 seconds to be more aggressive about reconnecting
            if (_staSSID && strlen(_staSSID) > 0 && (millis() - _lastRetry > 30000)) {
                _lastRetry = millis();
                Serial.printf("[WIFI] Periodically retrying STA: '%s'...\n", _staSSID);
                WiFi.reconnect();
                _state = WIFI_ST_CONNECTING;
                _connectStart = millis();
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
    WiFi.softAP(_apSSID, _apPass); // Default channel
    delay(500);
    _apRunning = true;
    _lastApIp = WiFi.softAPIP();
    _state = WIFI_ST_AP_MODE;
    
    Serial.println(F("\n╔════════════════════════════════════════╗"));
    Serial.printf("║ ⚠ Access Point (AP) Started:           ║\n");
    Serial.printf("║   SSID: %-33s ║\n", _apSSID);
    Serial.printf("║   IP Address: %-29s ║\n", WiFi.softAPIP().toString().c_str());
    Serial.println(F("║   Password: Check configuration        ║"));
    Serial.println(F("╚════════════════════════════════════════╝\n"));

    if (!_dnsStarted) {
        _dns.begin(WiFi.softAPIP());
        _dnsStarted = true;
        Serial.println(F("[WIFI] DNS captive portal started"));
    }
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

int WiFiManager::apClientsCount() const {
    return WiFi.softAPgetStationNum();
}

String WiFiManager::apClientsMACs() const {
    // In Arduino ESP32 2.x, there isn't a direct list of stations in the WiFi class
    // but we can use the ESP-IDF primitives.
    String result = "";
    wifi_sta_list_t stationList;
    esp_wifi_ap_get_sta_list(&stationList);
    
    for (int i = 0; i < stationList.num; i++) {
        char macStr[18];
        const uint8_t* mac = stationList.sta[i].mac;
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        if (result.length() > 0) result += ", ";
        result += macStr;
    }
    return result;
}

// ---------------------------------------------------------------------------
// NAT: forward AP-client traffic through the STA interface to the internet.
// Also fixes DNS: stops captive portal DNS and configures AP DHCP to hand out
// the upstream DNS (from STA) so AP clients can resolve hostnames normally.
// Requires espressif32@5.3.0 (Arduino 2.x) where lwIP has forwarding compiled in.
// ---------------------------------------------------------------------------
void WiFiManager::ensureNaptInitialized() {
    if (_naptInitialized) return;

    // On the current Arduino-ESP32 lwIP API, ip_napt_init() is not exported.
    // NAPT tables are prepared by the stack and ip_napt_enable() is the public toggle.
    _naptInitialized = true;
    Serial.println(F("[WIFI] NAPT ready (using ip_napt_enable API)"));
}

void WiFiManager::enableNAT() {
    if (_natEnabled) return;

    if (!_apNatConfig) {
        _natEnabled = false;
        Serial.println(F("[WIFI] STA connected: keeping captive-portal DNS for auto-open"));
        return;
    }

    _natEnabled = true;

    ensureNaptInitialized();

    IPAddress apIp = WiFi.softAPIP();
    if (apIp == IPAddress(0,0,0,0)) {
        Serial.println(F("[WIFI] NAT skipped: AP IP is not ready yet"));
        _natEnabled = false;
        return;
    }

    IPAddress upstreamDns = WiFi.dnsIP(0);
    if (upstreamDns == IPAddress(0,0,0,0)) {
        upstreamDns = IPAddress(1, 1, 1, 1);
    }

    Serial.printf("[WIFI] ===================== NAPT DIAGNOSTICS =====================\n");
    Serial.printf("[WIFI] SoftAP IP (WiFi API): %s  (uint32: 0x%08X)\n", apIp.toString().c_str(), (uint32_t)apIp);
    Serial.printf("[WIFI] Upstream DNS: %s\n", upstreamDns.toString().c_str());

#if defined(ESP_IDF_VERSION_MAJOR)
    for (esp_netif_t* netif = esp_netif_next(NULL); netif != NULL; netif = esp_netif_next(netif)) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            uint32_t netif_ip = ip_info.ip.addr;
            const char* desc = esp_netif_get_desc(netif);
            const char* key = esp_netif_get_ifkey(netif);
            bool match = (netif_ip == (uint32_t)apIp);
            Serial.printf("[WIFI] -> lwIP netif [%s|%s]: IP=%s (0x%08X) Match=%s\n", 
                key ? key : "unk", desc ? desc : "unk", 
                IPAddress(netif_ip).toString().c_str(), 
                netif_ip, 
                match ? "YES" : "NO");
            if (match) {
                Serial.printf("[WIFI] -> EXACT MATCH FOUND on netif %s\n", desc ? desc : "unk");
            }
        }
    }
#endif

    // Switch DNS proxy to forward mode:
    //   captive probe hosts -> 192.168.4.1  (portal auto-opens)
    //   all other hosts     -> forwarded to 1.1.1.1 (internet DNS works)
    if (_dnsStarted) {
        _dns.setUpstream(upstreamDns);
    }

    ip_napt_enable((uint32_t)apIp, 1);
    Serial.printf("[WIFI] ==========================================================\n");
    Serial.println(F("[WIFI] NAT (IP Forwarding) Enable function was called!"));
}

// Restore captive portal DNS when STA disconnects.
void WiFiManager::restoreCaptivePortal() {
    if (!_apRunning) return;
    _natEnabled = false;

    // Do not deinit NAPT on transient STA loss.
    // On ESP-IDF 4.4 (C3), frequent enable/disable cycles can trigger
    // lwIP mem_free asserts in ip_napt_deinit().
    // Switch DNS proxy back to wildcard mode (all -> 192.168.4.1)
    if (_dnsStarted) {
        _dns.setUpstream(IPAddress(0,0,0,0));
    }
    Serial.println(F("[WIFI] Captive portal restored (DNS wildcard mode)"));
}

void WiFiManager::reconcileApInterface() {
    if (!_apRunning) return;

    IPAddress currentApIp = WiFi.softAPIP();
    if (currentApIp == IPAddress(0,0,0,0)) return;
    if (currentApIp == _lastApIp) return;

    if (_lastApIp != IPAddress(0,0,0,0)) {
        Serial.printf("[WIFI] AP subnet changed: %s -> %s\n",
                      _lastApIp.toString().c_str(),
                      currentApIp.toString().c_str());
    } else {
        Serial.printf("[WIFI] AP IP detected: %s\n", currentApIp.toString().c_str());
    }

    // Rebind captive DNS to the current AP interface address.
    if (_dnsStarted) {
        _dns.stop();
        _dns.begin(currentApIp);
        if (_natEnabled && _apNatConfig) {
            _dns.setUpstream(IPAddress(1, 1, 1, 1));
        }
    }

    // If NAT is active, move NAPT from old AP address to new one.
    if (_natEnabled) {
        ip_napt_enable((uint32_t)currentApIp, 1);
    }

    _lastApIp = currentApIp;
    Serial.printf("[WEB] AP portal URL: http://%s/\n", currentApIp.toString().c_str());
}

