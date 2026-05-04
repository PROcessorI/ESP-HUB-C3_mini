#include "config.h"

bool ConfigManager::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println(F("[CFG] LittleFS mount failed!"));
        return false;
    }
    Serial.println(F("[CFG] LittleFS mounted"));
    if (!load()) {
        Serial.println(F("[CFG] No config found, using defaults"));
        resetDefaults();
        save();
    }
    return true;
}

bool ConfigManager::load() {
    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[CFG] JSON parse error: %s\n", err.c_str());
        return false;
    }

    strlcpy(cfg.wifi_ssid, doc["wifi_ssid"] | "", sizeof(cfg.wifi_ssid));
    strlcpy(cfg.wifi_pass, doc["wifi_pass"] | "", sizeof(cfg.wifi_pass));

    strlcpy(cfg.ap_ssid, doc["ap_ssid"] | "ESP-HUB", sizeof(cfg.ap_ssid));
    strlcpy(cfg.ap_pass, doc["ap_pass"] | "12345678", sizeof(cfg.ap_pass));
    cfg.ap_nat = doc["ap_nat"] | false;

    strlcpy(cfg.device_name, doc["device_name"] | "ESP-HUB", sizeof(cfg.device_name));

    cfg.mesh_enabled = doc["mesh_en"] | false;
    cfg.mesh_master_node = false;
    strlcpy(cfg.mesh_ssid, doc["mesh_ssid"] | "ESP-HUB-MESH", sizeof(cfg.mesh_ssid));
    strlcpy(cfg.mesh_pass, doc["mesh_pass"] | "1234567890", sizeof(cfg.mesh_pass));
    cfg.mesh_port = doc["mesh_port"] | 5555;
    cfg.mesh_channel = doc["mesh_ch"] | 6;
    if (strlen(cfg.mesh_ssid) == 0) {
        strlcpy(cfg.mesh_ssid, "ESP-HUB-MESH", sizeof(cfg.mesh_ssid));
    }
    {
        size_t meshPassLen = strlen(cfg.mesh_pass);
        if (meshPassLen < 8 || meshPassLen > 63) {
            strlcpy(cfg.mesh_pass, "1234567890", sizeof(cfg.mesh_pass));
        }
    }
    if (cfg.mesh_port == 0) cfg.mesh_port = 5555;
    if (cfg.mesh_channel < 1 || cfg.mesh_channel > 13) cfg.mesh_channel = 6;

    cfg.cpu_freq_mhz = doc["cpu_freq"] | 160;
    cfg.serial_baud  = doc["serial_baud"] | 115200;

    JsonObject fix = doc["fixture"];
    cfg.fixture.enabled            = fix["en"] | true;
    cfg.fixture.red_brightness     = fix["red"] | 0;
    cfg.fixture.far_red_brightness = fix["fr"] | 0;
    cfg.fixture.blue_brightness    = fix["blue"] | 0;
    cfg.fixture.white_brightness   = fix["white"] | 0;
    cfg.fixture.uart_tx_pin        = fix["tx"] | 4;
    cfg.fixture.uart_rx_pin        = fix["rx"] | 3;
    cfg.fixture.uart_baud          = fix["baud"] | 9600UL;

    if (cfg.fixture.uart_tx_pin == 4 && cfg.fixture.uart_rx_pin == 5) {
        cfg.fixture.uart_rx_pin = 3;
    }

    Serial.println(F("[CFG] Config loaded"));
    return true;
}

bool ConfigManager::save() {
    JsonDocument doc;

    doc["wifi_ssid"]    = cfg.wifi_ssid;
    doc["wifi_pass"]    = cfg.wifi_pass;

    doc["ap_ssid"]      = cfg.ap_ssid;
    doc["ap_pass"]      = cfg.ap_pass;
    doc["ap_nat"]       = cfg.ap_nat;
    doc["device_name"]  = cfg.device_name;
    doc["mesh_en"]      = cfg.mesh_enabled;
    doc["mesh_master"]  = false;
    doc["mesh_ssid"]    = cfg.mesh_ssid;
    doc["mesh_pass"]    = cfg.mesh_pass;
    doc["mesh_port"]    = cfg.mesh_port;
    doc["mesh_ch"]      = cfg.mesh_channel;
    doc["cpu_freq"]     = cfg.cpu_freq_mhz;
    doc["serial_baud"]  = cfg.serial_baud;

    JsonObject fix = doc["fixture"].to<JsonObject>();
    fix["en"]   = cfg.fixture.enabled;
    fix["red"]  = cfg.fixture.red_brightness;
    fix["fr"]   = cfg.fixture.far_red_brightness;
    fix["blue"] = cfg.fixture.blue_brightness;
    fix["white"]= cfg.fixture.white_brightness;
    fix["tx"]   = cfg.fixture.uart_tx_pin;
    fix["rx"]   = cfg.fixture.uart_rx_pin;
    fix["baud"] = cfg.fixture.uart_baud;

    File f = LittleFS.open(CONFIG_PATH, "w");
    if (!f) {
        Serial.println(F("[CFG] Failed to open config for writing"));
        return false;
    }
    serializeJson(doc, f);
    f.close();
    Serial.println(F("[CFG] Config saved"));
    return true;
}

void ConfigManager::resetDefaults() {
    cfg = HubConfig();
}

void ConfigManager::printConfig() {
    Serial.println(F("===== Hub Config ====="));
    Serial.printf("  Device: %s\n", cfg.device_name);
    Serial.printf("  WiFi:   ssid='%s' pass_len=%d\n",
                  cfg.wifi_ssid, (int)strlen(cfg.wifi_pass));
    Serial.printf("  Mesh:   %s  mode=%s ssid='%s' pass_len=%d port=%u ch=%u\n",
                  cfg.mesh_enabled ? "ENABLED" : "DISABLED",
                  "PEER",
                  cfg.mesh_ssid,
                  (int)strlen(cfg.mesh_pass),
                  (unsigned)cfg.mesh_port,
                  (unsigned)cfg.mesh_channel);
    Serial.printf("  Fixture: %s R=%d FR=%d B=%d W=%d\n",
        cfg.fixture.enabled ? "ON" : "OFF",
        cfg.fixture.red_brightness,
        cfg.fixture.far_red_brightness,
        cfg.fixture.blue_brightness,
        cfg.fixture.white_brightness);
    Serial.println(F("======================"));
}