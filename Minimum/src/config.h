#pragma once
// ============== ESP-HUB Minimal Configuration ==============
// Stores settings in JSON on LittleFS

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#define CONFIG_PATH "/config.json"
#define MAX_FIXTURE_TIMERS 4

struct FixtureTimer {
    bool     enabled          = false;
    uint8_t  hours            = 0;
    uint8_t  minutes          = 0;
    uint8_t  seconds          = 0;
    uint8_t  red_brightness   = 0;
    uint8_t  far_red_brightness = 0;
    uint8_t  blue_brightness  = 0;
    uint8_t  white_brightness = 0;
    char     label[32]        = "";
};

struct FixtureConfig {
    bool     enabled          = true;
    uint8_t  red_brightness   = 0;
    uint8_t  far_red_brightness = 0;
    uint8_t  blue_brightness  = 0;
    uint8_t  white_brightness = 0;
    uint8_t  uart_tx_pin      = 4;
    uint8_t  uart_rx_pin      = 3;
    uint32_t uart_baud        = 9600;
    
    FixtureTimer timers[MAX_FIXTURE_TIMERS];
};

struct HubConfig {
    char wifi_ssid[33]  = "";
    char wifi_pass[65]  = "";

    char ap_ssid[33]    = "ESP-HUB";
    char ap_pass[33]    = "12345678";
    bool ap_nat         = false;

    char device_name[33] = "ESP-HUB";

    uint16_t cpu_freq_mhz = 160;
    uint32_t serial_baud = 115200;

    bool mesh_enabled  = false;
    bool mesh_master_node = false;
    char mesh_ssid[33] = "ESP-HUB-MESH";
    char mesh_pass[65] = "1234567890";
    uint16_t mesh_port = 5555;
    uint8_t mesh_channel = 6;

    bool cron_enabled = true;
    char cron_tz[64]  = "UTC0";

    FixtureConfig fixture;
};

class ConfigManager {
public:
    HubConfig cfg;

    bool begin();
    bool load();
    bool save();
    void resetDefaults();
    void printConfig();
};