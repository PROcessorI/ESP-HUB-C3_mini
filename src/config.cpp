#include "config.h"

// ---- Human-readable sensor names ----
static const char* _sensorNames[] = {
    "None",
    // GPIO
    "DHT11", "DHT22", "DS18B20", "Analog",
    // I2C
    "BMP280", "BH1750",
    // UART
    "MH-Z19 (CO2)", "SDS011 (Dust)", "UART Generic",
#if SOC_TWAI_SUPPORTED
    // CAN
    "CAN Raw",
#endif
    // Generic analog modules
    "MH-Sensor"
};

const char* sensorTypeName(SensorType t) {
    if (t < SENSOR_TYPE_COUNT) return _sensorNames[t];
    return "Unknown";
}

SensorType sensorTypeFromIndex(uint8_t idx) {
    if (idx < SENSOR_TYPE_COUNT) return (SensorType)idx;
    return SENSOR_NONE;
}

static const char* _busNames[] = {
    "Auto", "GPIO", "I2C", "1-Wire", "UART",
#if SOC_TWAI_SUPPORTED
    "CAN",
#endif
};
const char* busTypeName(BusType b) {
    if (b < BUS_TYPE_COUNT) return _busNames[b];
    return "?";
}

static const char* _outNames[] = {
    "MQTT", "HTTP POST",
#if SOC_TWAI_SUPPORTED
    "CAN TX",
#endif
    "Serial"
};
const char* outProtoName(OutProtocol p) {
    if (p < OUT_PROTOCOL_COUNT) return _outNames[p];
    return "?";
}

BusType defaultBusForType(SensorType t) {
    switch (t) {
        case SENSOR_DHT11: case SENSOR_DHT22: case SENSOR_ANALOG: return BUS_GPIO;
        case SENSOR_DS18B20:                                       return BUS_ONEWIRE;
        case SENSOR_BMP280: case SENSOR_BH1750:                    return BUS_I2C;
        case SENSOR_MHZ19: case SENSOR_SDS011:
        case SENSOR_UART_GENERIC:                                  return BUS_UART;
#if SOC_TWAI_SUPPORTED
        case SENSOR_CAN_RAW:                                        return BUS_CAN;
#endif
        case SENSOR_MH_SERIES:                                      return BUS_GPIO;
        default:                                                    return BUS_GPIO;
    }
}

// ---- ConfigManager implementation ----

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

    // WiFi
    strlcpy(cfg.wifi_ssid, doc["wifi_ssid"] | "", sizeof(cfg.wifi_ssid));
    strlcpy(cfg.wifi_pass, doc["wifi_pass"] | "", sizeof(cfg.wifi_pass));

    // MQTT
    strlcpy(cfg.mqtt_host, doc["mqtt_host"] | "", sizeof(cfg.mqtt_host));
    cfg.mqtt_port = doc["mqtt_port"] | 1883;
    strlcpy(cfg.mqtt_user, doc["mqtt_user"] | "", sizeof(cfg.mqtt_user));
    strlcpy(cfg.mqtt_pass, doc["mqtt_pass"] | "", sizeof(cfg.mqtt_pass));
    strlcpy(cfg.mqtt_topic, doc["mqtt_topic"] | "esp-hub/telemetry", sizeof(cfg.mqtt_topic));
    cfg.mqtt_interval_s = doc["mqtt_interval"] | 10;

    // Sensors
    cfg.sensor_interval_s = doc["sensor_interval"] | 60;

    // AP
    strlcpy(cfg.ap_ssid, doc["ap_ssid"] | "ESP-HUB", sizeof(cfg.ap_ssid));
    strlcpy(cfg.ap_pass, doc["ap_pass"] | "12345678", sizeof(cfg.ap_pass));
    cfg.ap_nat = doc["ap_nat"] | false;
    {
        size_t apPassLen = strlen(cfg.ap_pass);
        if (apPassLen > 0 && apPassLen < 8) {
            Serial.printf("[CFG] AP password too short (%u), fallback to default\n", (unsigned)apPassLen);
            strlcpy(cfg.ap_pass, "12345678", sizeof(cfg.ap_pass));
        }
    }

    // Device
    strlcpy(cfg.device_name, doc["device_name"] | "ESP-HUB", sizeof(cfg.device_name));

    // BLE
    cfg.ble_enabled = doc["ble_en"] | false;
    strlcpy(cfg.ble_name, doc["ble_name"] | "", sizeof(cfg.ble_name));

    // Mesh
    cfg.mesh_enabled = doc["mesh_en"] | false;
    cfg.mesh_master_node = doc["mesh_master"] | false;
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
            Serial.printf("[CFG] Mesh password length=%u is invalid, fallback to default\n", (unsigned)meshPassLen);
            strlcpy(cfg.mesh_pass, "1234567890", sizeof(cfg.mesh_pass));
        }
    }
    if (cfg.mesh_port == 0) cfg.mesh_port = 5555;
    if (cfg.mesh_channel < 1 || cfg.mesh_channel > 13) cfg.mesh_channel = 6;

    cfg.cpu_freq_mhz = doc["cpu_freq"] | 240;
    cfg.serial_baud  = doc["serial_baud"] | 115200;

    // ESP-CAM
    strlcpy(cfg.cam_url, doc["cam_url"] | "", sizeof(cfg.cam_url));
    cfg.cam_record_dev = doc["cam_rec"] | 0;

    // Rate Limiter
    cfg.rl_enabled  = doc["rl_en"]     | false;
    cfg.rl_max_hour = doc["rl_maxhr"]  | 20;
    cfg.rl_max_day  = doc["rl_maxday"] | 100;

    // CRON
    cfg.cron_enabled = doc["cron_en"] | true;
    strlcpy(cfg.cron_tz, doc["cron_tz"] | "UTC0", sizeof(cfg.cron_tz));

    // Sensors
    JsonArray arr = doc["sensors"].as<JsonArray>();
    for (int i = 0; i < MAX_SENSORS; i++) {
        cfg.sensors[i] = SensorConfig();
        if (i < (int)arr.size()) {
            JsonObject s = arr[i];
            cfg.sensors[i].enabled  = s["en"]  | false;
            cfg.sensors[i].type     = (SensorType)(s["type"].as<uint8_t>());
            cfg.sensors[i].bus      = (BusType)(s["bus"].as<uint8_t>());
            cfg.sensors[i].outProto = (OutProtocol)(s["out"].as<uint8_t>());
            cfg.sensors[i].pin      = s["pin"]  | 0;
            cfg.sensors[i].pin2     = s["pin2"] | 0;
            cfg.sensors[i].uartNum  = s["uart"] | 1;
            cfg.sensors[i].canId    = s["cid"]  | 0x100UL;
            cfg.sensors[i].canDlc   = s["cdlc"] | 8;
            cfg.sensors[i].baud     = s["baud"]  | 9600UL;
            strlcpy(cfg.sensors[i].httpUrl, s["hurl"] | "", sizeof(cfg.sensors[i].httpUrl));
            strlcpy(cfg.sensors[i].label,   s["label"] | "", sizeof(cfg.sensors[i].label));
        }
    }

    // GPIO Timers
    JsonArray tarr = doc["gpio_timers"].as<JsonArray>();
    for (int i = 0; i < MAX_GPIO_TIMERS; i++) {
        cfg.gpio_timers[i] = GpioTimerConfig();
        if (i < (int)tarr.size()) {
            JsonObject t = tarr[i];
            cfg.gpio_timers[i].enabled     = t["en"]  | false;
            cfg.gpio_timers[i].pin         = t["pin"] | 0;
            cfg.gpio_timers[i].action      = (GpioTimerAction)(t["act"].as<uint8_t>());
            cfg.gpio_timers[i].hours       = t["h"]   | 0;
            cfg.gpio_timers[i].minutes     = t["m"]   | 0;
            cfg.gpio_timers[i].seconds     = t["s"]   | 10;
            cfg.gpio_timers[i].duration_ms = t["dur"] | 500;
            strlcpy(cfg.gpio_timers[i].label, t["lbl"] | "", sizeof(cfg.gpio_timers[i].label));
            cfg.gpio_timers[i].active_low  = t["inv"] | false;
        }
    }

    // Fixture (светильник)
    JsonObject fix = doc["fixture"];
    cfg.fixture.enabled            = fix["en"] | true;
    cfg.fixture.red_brightness     = fix["red"] | 0;
    cfg.fixture.far_red_brightness = fix["fr"] | 0;
    cfg.fixture.blue_brightness    = fix["blue"] | 0;
    cfg.fixture.white_brightness   = fix["white"] | 0;
    cfg.fixture.uart_tx_pin        = fix["tx"] | 4;   // ESP32-C3: GPIO4
    cfg.fixture.uart_rx_pin        = fix["rx"] | 3;   // ESP32-C3: GPIO3
    cfg.fixture.uart_baud          = fix["baud"] | 9600UL;

    // Migrate legacy default wiring TX=4/RX=5 -> TX=4/RX=3 for ESP32-C3 Super Mini.
    if (cfg.fixture.uart_tx_pin == 4 && cfg.fixture.uart_rx_pin == 5) {
        cfg.fixture.uart_rx_pin = 3;
    }

    JsonArray scarr = fix["scenarios"].as<JsonArray>();
    for (int i = 0; i < MAX_FIXTURE_SCENARIOS; i++) {
        if (i < (int)scarr.size()) {
            JsonObject sc = scarr[i];
            cfg.fixture.scenarios[i].enabled = sc["en"] | false;
            cfg.fixture.scenarios[i].start_hour = sc["h"] | 0;
            cfg.fixture.scenarios[i].start_minute = sc["m"] | 0;
            cfg.fixture.scenarios[i].start_second = sc["s"] | 0;
            cfg.fixture.scenarios[i].red = sc["r"] | 0;
            cfg.fixture.scenarios[i].far_red = sc["fr"] | 0;
            cfg.fixture.scenarios[i].blue = sc["b"] | 0;
            cfg.fixture.scenarios[i].white = sc["w"] | 0;
        } else {
            cfg.fixture.scenarios[i] = FixtureScenario();
        }
    }

    JsonArray ftarr = fix["timers"].as<JsonArray>();
    for (int i = 0; i < MAX_FIXTURE_TIMERS; i++) {
        if (i < (int)ftarr.size()) {
            JsonObject t = ftarr[i];
            cfg.fixture.timers[i].enabled = t["en"] | false;
            strlcpy(cfg.fixture.timers[i].label, t["lbl"] | "", sizeof(cfg.fixture.timers[i].label));
            cfg.fixture.timers[i].hours = t["h"] | 0;
            cfg.fixture.timers[i].minutes = t["m"] | 0;
            cfg.fixture.timers[i].seconds = t["s"] | 0;
            cfg.fixture.timers[i].duration_ms = t["dur"] | 500;
            cfg.fixture.timers[i].action = (FixtureTimerAction)constrain((int)(t["act"] | 0), 0, FIX_TIMER_ACTION_COUNT - 1);
            cfg.fixture.timers[i].red = t["r"] | 0;
            cfg.fixture.timers[i].far_red = t["fr"] | 0;
            cfg.fixture.timers[i].blue = t["b"] | 0;
            cfg.fixture.timers[i].white = t["w"] | 0;
            cfg.fixture.timers[i].run_hours = t["rh"] | 0;
            cfg.fixture.timers[i].run_minutes = t["rm"] | 0;
            cfg.fixture.timers[i].run_seconds = t["rs"] | 0;
        } else {
            cfg.fixture.timers[i] = FixtureTimerConfig();
        }
    }

    Serial.println(F("[CFG] Config loaded"));
    return true;
}

bool ConfigManager::save() {
    JsonDocument doc;

    doc["wifi_ssid"]    = cfg.wifi_ssid;
    doc["wifi_pass"]    = cfg.wifi_pass;
    doc["mqtt_host"]    = cfg.mqtt_host;
    doc["mqtt_port"]    = cfg.mqtt_port;
    doc["mqtt_user"]    = cfg.mqtt_user;
    doc["mqtt_pass"]    = cfg.mqtt_pass;
    doc["mqtt_topic"]   = cfg.mqtt_topic;
    doc["mqtt_interval"]= cfg.mqtt_interval_s;

    // Sensors
    doc["sensor_interval"] = cfg.sensor_interval_s;

    doc["ap_ssid"]      = cfg.ap_ssid;
    doc["ap_pass"]      = cfg.ap_pass;
    doc["ap_nat"]       = cfg.ap_nat;
    doc["device_name"]  = cfg.device_name;
    doc["ble_en"]       = cfg.ble_enabled;
    doc["ble_name"]     = cfg.ble_name;
    doc["mesh_en"]      = cfg.mesh_enabled;
    doc["mesh_master"]  = cfg.mesh_master_node;
    doc["mesh_ssid"]    = cfg.mesh_ssid;
    doc["mesh_pass"]    = cfg.mesh_pass;
    doc["mesh_port"]    = cfg.mesh_port;
    doc["mesh_ch"]      = cfg.mesh_channel;
    doc["cpu_freq"]     = cfg.cpu_freq_mhz;
    doc["serial_baud"]  = cfg.serial_baud;
    doc["cam_url"]      = cfg.cam_url;
    doc["cam_rec"]      = cfg.cam_record_dev;

    // Rate Limiter
    doc["rl_en"]     = cfg.rl_enabled;
    doc["rl_maxhr"]  = cfg.rl_max_hour;
    doc["rl_maxday"] = cfg.rl_max_day;

    // CRON
    doc["cron_en"] = cfg.cron_enabled;
    doc["cron_tz"] = cfg.cron_tz;

    JsonArray arr = doc["sensors"].to<JsonArray>();
    for (int i = 0; i < MAX_SENSORS; i++) {
        JsonObject s = arr.add<JsonObject>();
        s["en"]    = cfg.sensors[i].enabled;
        s["type"]  = (uint8_t)cfg.sensors[i].type;
        s["bus"]   = (uint8_t)cfg.sensors[i].bus;
        s["out"]   = (uint8_t)cfg.sensors[i].outProto;
        s["pin"]   = cfg.sensors[i].pin;
        s["pin2"]  = cfg.sensors[i].pin2;
        s["uart"]  = cfg.sensors[i].uartNum;
        s["cid"]   = cfg.sensors[i].canId;
        s["cdlc"]  = cfg.sensors[i].canDlc;
        s["baud"]  = cfg.sensors[i].baud;
        s["hurl"]  = cfg.sensors[i].httpUrl;
        s["label"] = cfg.sensors[i].label;
    }

    JsonArray tarr = doc["gpio_timers"].to<JsonArray>();
    for (int i = 0; i < MAX_GPIO_TIMERS; i++) {
        JsonObject t = tarr.add<JsonObject>();
        t["en"]  = cfg.gpio_timers[i].enabled;
        t["pin"] = cfg.gpio_timers[i].pin;
        t["act"] = (uint8_t)cfg.gpio_timers[i].action;
        t["h"]   = cfg.gpio_timers[i].hours;
        t["m"]   = cfg.gpio_timers[i].minutes;
        t["s"]   = cfg.gpio_timers[i].seconds;
        t["dur"] = cfg.gpio_timers[i].duration_ms;
        t["lbl"] = cfg.gpio_timers[i].label;
        t["inv"] = cfg.gpio_timers[i].active_low;
    }

    // Fixture (светильник)
    JsonObject fix = doc["fixture"].to<JsonObject>();
    fix["en"]   = cfg.fixture.enabled;
    fix["red"]  = cfg.fixture.red_brightness;
    fix["fr"]   = cfg.fixture.far_red_brightness;
    fix["blue"] = cfg.fixture.blue_brightness;
    fix["white"]= cfg.fixture.white_brightness;
    fix["tx"]   = cfg.fixture.uart_tx_pin;
    fix["rx"]   = cfg.fixture.uart_rx_pin;
    fix["baud"] = cfg.fixture.uart_baud;
    
    JsonArray scarr = fix["scenarios"].to<JsonArray>();
    for (int i = 0; i < MAX_FIXTURE_SCENARIOS; i++) {
        JsonObject sc = scarr.add<JsonObject>();
        sc["en"] = cfg.fixture.scenarios[i].enabled;
        sc["h"]  = cfg.fixture.scenarios[i].start_hour;
        sc["m"]  = cfg.fixture.scenarios[i].start_minute;
        sc["s"]  = cfg.fixture.scenarios[i].start_second;
        sc["r"]  = cfg.fixture.scenarios[i].red;
        sc["fr"] = cfg.fixture.scenarios[i].far_red;
        sc["b"]  = cfg.fixture.scenarios[i].blue;
        sc["w"]  = cfg.fixture.scenarios[i].white;
    }

    JsonArray ftarr = fix["timers"].to<JsonArray>();
    for (int i = 0; i < MAX_FIXTURE_TIMERS; i++) {
        JsonObject t = ftarr.add<JsonObject>();
        t["en"] = cfg.fixture.timers[i].enabled;
        t["lbl"] = cfg.fixture.timers[i].label;
        t["h"] = cfg.fixture.timers[i].hours;
        t["m"] = cfg.fixture.timers[i].minutes;
        t["s"] = cfg.fixture.timers[i].seconds;
        t["dur"] = cfg.fixture.timers[i].duration_ms;
        t["act"] = (uint8_t)cfg.fixture.timers[i].action;
        t["r"] = cfg.fixture.timers[i].red;
        t["fr"] = cfg.fixture.timers[i].far_red;
        t["b"] = cfg.fixture.timers[i].blue;
        t["w"] = cfg.fixture.timers[i].white;
        t["rh"] = cfg.fixture.timers[i].run_hours;
        t["rm"] = cfg.fixture.timers[i].run_minutes;
        t["rs"] = cfg.fixture.timers[i].run_seconds;
    }

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
    Serial.printf("  MQTT:   %s:%d  topic=%s  interval=%ds\n",
        cfg.mqtt_host, cfg.mqtt_port, cfg.mqtt_topic, cfg.mqtt_interval_s);
    Serial.printf("  Mesh:   %s  role=%s ssid='%s' pass_len=%d port=%u ch=%u\n",
                  cfg.mesh_enabled ? "ENABLED" : "DISABLED",
                  cfg.mesh_master_node ? "MASTER" : "NODE",
                  cfg.mesh_ssid,
                  (int)strlen(cfg.mesh_pass),
                  (unsigned)cfg.mesh_port,
                  (unsigned)cfg.mesh_channel);
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (cfg.sensors[i].type != SENSOR_NONE) {
            Serial.printf("  Sensor[%d]: %s (%s) pin=%d %s\n", i,
                cfg.sensors[i].label,
                sensorTypeName(cfg.sensors[i].type),
                cfg.sensors[i].pin,
                cfg.sensors[i].enabled ? "ON" : "OFF");
        }
    }
    // Fixture
    Serial.printf("  Fixture: %s R=%d FR=%d B=%d W=%d\n",
        cfg.fixture.enabled ? "ON" : "OFF",
        cfg.fixture.red_brightness,
        cfg.fixture.far_red_brightness,
        cfg.fixture.blue_brightness,
        cfg.fixture.white_brightness);
    Serial.println(F("======================"));
}
