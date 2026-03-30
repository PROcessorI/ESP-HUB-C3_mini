#pragma once
// ============== ESP-HUB Configuration Manager ==============
// Stores settings in JSON on LittleFS
// Target: ESP32-C3 Super Mini (RISC-V, 1 core, UART0+UART1, no TWAI/CAN)

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <soc/soc_caps.h>

#define CONFIG_PATH "/config.json"
#define MAX_SENSORS     8
#define MAX_GPIO_TIMERS 8
#define MAX_FIXTURE_SCENARIOS 8

// ---- GPIO scheduler action ----
enum GpioTimerAction : uint8_t {
    TIMER_HIGH = 0,       // set pin HIGH
    TIMER_LOW,            // set pin LOW
    TIMER_TOGGLE,         // toggle pin state
    TIMER_PULSE_HIGH,     // pulse HIGH for duration_ms, then LOW
    TIMER_PULSE_LOW,      // pulse LOW for duration_ms, then HIGH
    TIMER_ACTION_COUNT
};

// ---- Per-timer config ----
struct GpioTimerConfig {
    bool             enabled      = false;
    uint8_t          pin          = 0;         // GPIO 0–48
    GpioTimerAction  action       = TIMER_HIGH;
    uint8_t          hours        = 0;
    uint8_t          minutes      = 0;
    uint8_t          seconds      = 10;        // default: every 10s
    uint16_t         duration_ms  = 500;       // pulse duration (ms)
    char             label[20]    = "";        // user name
    bool             active_low   = false;     // true = inverted relay (active LOW)
};

// ---- Sensor type identifiers ----
enum SensorType : uint8_t {
    SENSOR_NONE = 0,
    // GPIO sensors
    SENSOR_DHT11,
    SENSOR_DHT22,
    SENSOR_DS18B20,
    SENSOR_ANALOG,
    // I2C sensors
    SENSOR_BMP280,
    SENSOR_BH1750,
    // UART sensors
    SENSOR_MHZ19,       // MH-Z19B CO2
    SENSOR_SDS011,      // SDS011 PM2.5/PM10
    SENSOR_UART_GENERIC,// Generic UART line reader (JSON/CSV)
#if SOC_TWAI_SUPPORTED
    // CAN sensors
    SENSOR_CAN_RAW,     // Read raw CAN frame → values
#endif
    // Generic analog modules
    SENSOR_MH_SERIES,   // MH-Sensor Series (LDR/flame/rain/moisture/hall/IR/etc.)
    SENSOR_TYPE_COUNT
};

// ---- Physical bus type ----
enum BusType : uint8_t {
    BUS_AUTO = 0,   // auto-detect from SensorType
    BUS_GPIO,       // digital/PWM GPIO
    BUS_I2C,        // I2C (default SDA/SCL = 6/7 on ESP32-C3)
    BUS_ONEWIRE,    // 1-Wire
    BUS_UART,       // Hardware UART (pin=RX, pin2=TX, uartNum=1)
#if SOC_TWAI_SUPPORTED
    BUS_CAN,        // CAN bus via TWAI (ESP32 only)
#endif
    BUS_TYPE_COUNT
};

// ---- Output/transport protocol ----
enum OutProtocol : uint8_t {
    OUT_MQTT = 0,   // publish to MQTT topic (default)
    OUT_HTTP,       // HTTP POST JSON to webhook URL
#if SOC_TWAI_SUPPORTED
    OUT_CAN,        // transmit on CAN bus (canId field, ESP32 only)
#endif
    OUT_SERIAL,     // print to Serial monitor only
    OUT_PROTOCOL_COUNT
};

const char* sensorTypeName(SensorType t);
SensorType  sensorTypeFromIndex(uint8_t idx);
BusType     defaultBusForType(SensorType t); // auto-detect
const char* busTypeName(BusType b);
const char* outProtoName(OutProtocol p);

// ---- Per-sensor config ----
struct SensorConfig {
    bool        enabled  = false;
    SensorType  type     = SENSOR_NONE;
    BusType     bus      = BUS_AUTO;      // bus interface
    OutProtocol outProto = OUT_MQTT;      // output protocol

    uint8_t     pin      = 0;    // GPIO / I2C addr / UART RX pin
    uint8_t     pin2     = 0;    // UART TX pin
    uint8_t     uartNum  = 1;    // HW UART index (1 or 2)

    uint32_t    canId    = 0x100;         // CAN ID to filter (IN) or transmit (OUT)
    uint8_t     canDlc   = 8;             // CAN frame DLC (0..8)
    uint32_t    baud     = 9600;          // UART baud rate (for UART_GENERIC)
    char        httpUrl[65] = "";         // HTTP POST endpoint
    char        label[24]   = "";         // user-friendly name
};


#ifndef MAX_FIXTURE_TIMERS
#define MAX_FIXTURE_TIMERS 8
#endif

enum FixtureTimerAction {
    FIX_TIMER_OFF = 0,
    FIX_TIMER_GROW = 1,
    FIX_TIMER_FULL = 2,
    FIX_TIMER_RED = 3,
    FIX_TIMER_BLUE = 4,
    FIX_TIMER_PULSE_GROW = 5,
    FIX_TIMER_PULSE_FULL = 6,
    FIX_TIMER_CUSTOM = 7,        // включить свои каналы R/FR/B/W (остаётся)
    FIX_TIMER_PULSE_CUSTOM = 8,  // включить на duration_ms, потом выкл
    FIX_TIMER_ACTION_COUNT = 9
};

struct FixtureTimerConfig {
    bool enabled = false;
    char label[16] = "";
    uint8_t hours = 0;
    uint8_t minutes = 0;
    uint8_t seconds = 0;
    uint16_t duration_ms = 500;  // для PULSE: время включения (мс)
    FixtureTimerAction action = FIX_TIMER_OFF;
    // Кастомные каналы (для FIX_TIMER_CUSTOM / FIX_TIMER_PULSE_CUSTOM)
    uint8_t red = 0;
    uint8_t far_red = 0;
    uint8_t blue = 0;
    uint8_t white = 0;
    // Время работы светильников (часы:минуты:секунды до автовыключения)
    uint8_t run_hours = 0;
    uint8_t run_minutes = 0;
    uint8_t run_seconds = 0;
};

// ---- Fixture Scenario ----
struct FixtureScenario {
    bool    enabled = false;
    uint8_t start_hour = 0;   // 0-23
    uint8_t start_minute = 0; // 0-59
    uint8_t start_second = 0; // 0-59
    uint8_t red = 0;          // 0-200
    uint8_t far_red = 0;      // 0-200
    uint8_t blue = 0;         // 0-200
    uint8_t white = 0;        // 0-200
};

// ---- Fixture (светильник) config ----
struct FixtureConfig {
    bool     enabled          = true;
    uint8_t  red_brightness   = 0;      // 0-200 (0.5% шаг)
    uint8_t  far_red_brightness = 0;    // 0-200
    uint8_t  blue_brightness  = 0;      // 0-200
    uint8_t  white_brightness = 0;      // 0-200
    uint8_t  uart_tx_pin      = 4;      // UART1 TX pin (ESP32-C3: GPIO4)
    uint8_t  uart_rx_pin      = 5;      // UART1 RX pin (ESP32-C3: GPIO5)
    uint32_t uart_baud        = 9600;   // UART baud rate
    
    // Сценарии работы по времени:
    FixtureScenario scenarios[MAX_FIXTURE_SCENARIOS];
    FixtureTimerConfig timers[MAX_FIXTURE_TIMERS];
};

// ---- Global hub config ----
struct HubConfig {
    // WiFi
    char wifi_ssid[33]  = "";
    char wifi_pass[65]  = "";

    // MQTT
    char mqtt_host[65]  = "";
    uint16_t mqtt_port  = 1883;
    char mqtt_user[33]  = "";
    char mqtt_pass[65]  = "";
    char mqtt_topic[65] = "esp-hub/telemetry";
    uint16_t mqtt_interval_s = 10;  // seconds between publishes

    // AP fallback
    char ap_ssid[33]    = "ESP-HUB";
    char ap_pass[33]    = "12345678";
    bool ap_nat         = false; // NAT for AP clients when STA is connected

    // Device
    char device_name[33] = "ESP-HUB";

    // CPU clock
    uint16_t cpu_freq_mhz = 160;  // 80 / 160 MHz (ESP32-C3 max = 160)

    // Serial baud rate
    uint32_t serial_baud = 115200;

    // Bluetooth BLE
    bool ble_enabled   = false;
    char ble_name[33]  = "";   // empty = inherit device_name

    // Mesh Network
    bool mesh_enabled  = false;  // Enable/disable painlessMesh
    bool mesh_master_node = false; // Main node for managing other mesh devices
    char mesh_ssid[33] = "ESP-HUB-MESH";
    char mesh_pass[65] = "1234567890";
    uint16_t mesh_port = 5555;
    uint8_t mesh_channel = 6;

    // ESP-CAM
    char cam_url[128]      = ""; // Base URL of ESP32-CAM, e.g. http://192.168.1.100
    uint8_t cam_record_dev = 0;  // 0=None 1=SD Card 2=MQTT

    // AI Agent
    bool     ai_enabled       = false;
    uint8_t  ai_provider      = 0;      // 0=LM Studio, 1=Ollama, 2=OpenAI, 3=OpenRouter, 4=Anthropic
    char     ai_lms_url[64]   = "";     // LM Studio base URL, e.g. http://192.168.1.125:1234
    char     ai_api_url[128]  = "";     // custom endpoint URL (overrides provider default)
    char     ai_model[64]     = "qwen/qwen3-vl-8b";  // model name (empty = provider default)
    char     ai_api_key[256]  = "";     // API key (cloud providers)
    bool     ai_tg_enabled    = false;  // Enable/disable Telegram polling
    char     ai_tg_token[128] = "";     // Telegram bot token
    char     ai_tg_chat_id[24]= "";     // allowed Telegram chat ID whitelist
    uint16_t ai_max_tokens    = 1024;   // max tokens in LLM response
    uint16_t ai_ctx_size      = 20000;  // context window (local models only)
    uint8_t  ai_temperature   = 70;     // 0-100 → 0.0-1.0 (70 = 0.7)
    uint8_t  ai_tool_rounds   = 5;      // max tool-call iterations per request
    char     ai_sys_prompt[256] = "";   // system prompt override (empty = built-in default)

    // Rate Limiter
    bool     rl_enabled   = false;
    uint16_t rl_max_hour  = 0;        // max AI requests per hour (0 = unlimited)
    uint16_t rl_max_day   = 0;       // max AI requests per day  (0 = unlimited)

    // CRON scheduler
    bool cron_enabled = true;
    char cron_tz[64]  = "UTC0";        // POSIX TZ string, e.g. "MSK-3" or "UTC0"

    // Sensors
    SensorConfig sensors[MAX_SENSORS];
    uint16_t sensor_interval_s = 60;  // interval for sensor readings output

    // GPIO Scheduler
    GpioTimerConfig gpio_timers[MAX_GPIO_TIMERS];

    // Fixture (светильник)
    FixtureConfig fixture;
};

// ---- Config manager ----
class ConfigManager {
public:
    HubConfig cfg;

    bool begin();           // mount FS and load
    bool load();            // read from file
    bool save();            // write to file
    void resetDefaults();   // restore factory defaults
    void printConfig();     // serial dump
};
