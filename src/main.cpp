// ============================================================
//  ESP-HUB — Unified Sensor Hub for ESP32-C3 Super Mini
//  Lightweight telemetry platform with web-based configuration
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <esp_log.h>

#include "config.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "sensor_manager.h"
#include "web_portal.h"
#include "can_manager.h"
#include "http_reporter.h"
#include "ble_manager.h"
#include "gpio_scheduler.h"
#include "serial_console.h"
#include "fixture_manager.h"
#include "system_clock.h"
#include "cron_manager.h"
#include "rate_limiter.h"
#include "mesh_manager.h"

// ---- Global objects ----
ConfigManager   configMgr;
WiFiManager     wifiMgr;
MQTTClient      mqttClient;
CANManager      canMgr;
HTTPReporter    httpReporter;
SensorManager   sensorMgr;
BLEManager      bleMgr;
WebPortal       portal;
GpioScheduler   gpioSched;
SerialConsole   serialCon;
FixtureManager  fixtureMgr;
MeshManager     meshMgr;

static bool isLoopProneMeshCommand(const String& cmd) {
    String s = cmd;
    s.trim();
    s.toLowerCase();
    if (!s.startsWith("mesh ")) return false;
    return s.startsWith("mesh cmd") || s.startsWith("mesh chat") || s.startsWith("mesh data");
}

static void onMeshMessage(uint32_t from, String &msg) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, msg);
    if (err) {
        // Non-JSON payloads are treated as plain messages/chat text.
        Serial.printf("[MESH] Text from %u: %s\n", from, msg.c_str());
        return;
    }

    const char* type = doc["type"] | "";
    if (strcmp(type, "cmd") == 0) {
        const char* target = doc["target"] | "all";
        const char* command = doc["cmd"] | "";
        uint32_t cmdId = doc["id"] | 0;
        uint32_t myNode = meshMgr.getNodeId();
        const char* myRole = configMgr.cfg.mesh_master_node ? "MAIN" : "NODE";

        bool targetMatch = (strcmp(target, "all") == 0);
        if (!targetMatch) {
            String nodeTarget = String("node:") + String(myNode);
            targetMatch = (strcmp(target, nodeTarget.c_str()) == 0);
        }

        if (!targetMatch || strlen(command) == 0) {
            return;
        }

        String cmdLine = command;
        cmdLine.trim();
        if (isLoopProneMeshCommand(cmdLine)) {
            // Prevent broadcast loops from relayed mesh commands.
            return;
        }

        meshMgr.addLogEntry(String("EXEC role=") + myRole + " node=" + String(myNode) + " from=" + String(from) + " target=" + String(target) + " cmd=" + cmdLine);
        Serial.printf("[MESH] EXEC role=%s node=%u from=%u target=%s cmd=%s\n", myRole, (unsigned)myNode, (unsigned)from, target, cmdLine.c_str());
        serialCon.executeCommand(cmdLine);

        String ack = "{\"type\":\"ack\",\"id\":";
        ack += cmdId;
        ack += ",\"ok\":true,\"node\":";
        ack += myNode;
        ack += ",\"msg\":\"executed\"}";
        meshMgr.sendToNode(from, ack);
        meshMgr.addLogEntry(String("ACK TX role=") + myRole + " node=" + String(myNode) + " to=" + String(from) + " id=" + String(cmdId));
        return;
    }

    if (strcmp(type, "chat") == 0) {
        const char* target = doc["target"] | "all";
        uint32_t myNode = meshMgr.getNodeId();
        bool targetMatch = (strcmp(target, "all") == 0);
        if (!targetMatch) {
            String nodeTarget = String("node:") + String(myNode);
            targetMatch = (strcmp(target, nodeTarget.c_str()) == 0);
        }
        if (!targetMatch) {
            return;
        }

        const char* fromName = doc["from"] | "node";
        const char* text = doc["text"] | "";
        Serial.printf("[MESH CHAT] %s: %s\n", fromName, text);
        return;
    }

    if (strcmp(type, "data") == 0) {
        const char* topic = doc["topic"] | "raw";
        const char* payload = doc["payload"] | "";
        Serial.printf("[MESH DATA] %s => %s\n", topic, payload);
        return;
    }

    if (strcmp(type, "ack") == 0) {
        uint32_t id = doc["id"] | 0;
        bool ok = doc["ok"] | false;
        uint32_t node = doc["node"] | 0;
        const char* txt = doc["msg"] | "";
        uint32_t myNode = meshMgr.getNodeId();
        const char* myRole = configMgr.cfg.mesh_master_node ? "MAIN" : "NODE";
        meshMgr.addLogEntry(String("ACK RX role=") + myRole + " node=" + String(myNode) + " from=" + String(from) + " src_node=" + String(node) + " id=" + String(id) + " ok=" + (ok ? "1" : "0") + " msg=" + String(txt));
        Serial.printf("[MESH ACK] id=%u node=%u ok=%s msg=%s\n", (unsigned)id, (unsigned)node, ok ? "true" : "false", txt);
        return;
    }
}

// ---- Timers ----
uint32_t lastSensorRead  = 0;
uint32_t sensorReadInterval = 2000; // 2 sec

void setup() {
    // Инициализация Serial на стандартной скорости для вывода логов
    Serial.begin(115200);
    delay(500);

    // Keep system network logs concise so serial console input stays readable.
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);

    // 1. Загрузка конфигурации из LittleFS (сохранённые настройки)
    configMgr.begin();

    // 1b. Инициализация системных часов (резервный таймер для сценариев)
    systemClock.begin();

    // Переинициализация Serial если пользователь изменил скорость в веб-интерфейсе
    if (configMgr.cfg.serial_baud != 115200 && configMgr.cfg.serial_baud > 0) {
        Serial.flush();
        Serial.end();
        Serial.begin(configMgr.cfg.serial_baud);
        delay(100);
    }

    Serial.println(F("\n"
        "\033[36m"
        "  ______  _____  _____       _    _  _    _  ____   \n"
        " |  ____|/ ____||  __ \\     | |  | || |  | ||  _ \\  \n"
        " | |__  | (___  | |__) |____| |__| || |  | || |_) | \n"
        " |  __|  \\___ \\ |  ___/_____|  __  || |  | ||  _ <  \n"
        " | |____ ____) || |         | |  | || |__| || |_) | \n"
        " |______|_____/ |_|         |_|  |_| \\____/ |____/  \n"
        "\033[0m"
        "\033[92m  v1.0 [ESP32-C3]\033[0m - Unified Sensor Hub\n"
        "=================================================="));

    configMgr.printConfig();

    // Применение сохранённой частоты процессора (80 / 160 МГц — ESP32-C3 макс 160)
    {
        uint16_t cf = configMgr.cfg.cpu_freq_mhz;
        if (cf == 80 || cf == 160) setCpuFrequencyMhz(cf);
    }

    // 2. Initialize I2C (ESP32-C3 Super Mini: SDA=GPIO6, SCL=GPIO7)
    Wire.begin(6, 7);

    // 3. Initialize CAN if any sensor uses CAN bus (ESP32 only, not available on C3)
#if SOC_TWAI_SUPPORTED
    bool needCan = false;
    for (int i = 0; i < MAX_SENSORS; i++) {
        const SensorConfig& sc = configMgr.cfg.sensors[i];
        if (!sc.enabled) continue;
        BusType b = (sc.bus == BUS_AUTO) ? defaultBusForType(sc.type) : sc.bus;
        if (b == BUS_CAN || sc.outProto == OUT_CAN) { needCan = true; break; }
    }
    if (needCan) {
        canMgr.begin(5, 4, 500000);
    }
#endif

    // 4. Wire managers into SensorManager
    sensorMgr.setCANManager(&canMgr);
    sensorMgr.setHTTPReporter(&httpReporter);
    sensorMgr.setMQTTClient(&mqttClient);
    sensorMgr.setWiFiManager(&wifiMgr);  // needed for ADC2 WiFi-pause feature

    // 5. Initialize sensors
    sensorMgr.begin(configMgr.cfg);

    // 5b. КРИТИЧНО: BLE контроллер нужно инициализировать ДО WiFi!
    // На ESP32 BT-контроллер должен захватить свою область памяти раньше, чем
    // WiFi стек занимает RF-ресурсы. Если сделать наоборот — abort() внутри
    // esp_bt_controller_init(). Порядок: BLE → WiFi (не наоборот).
    if (configMgr.cfg.ble_enabled) {
        Serial.printf("[BLE] Pre-WiFi init (heap: %d)...\n", ESP.getFreeHeap());
        const char* bleName = (strlen(configMgr.cfg.ble_name) > 0)
            ? configMgr.cfg.ble_name
            : configMgr.cfg.device_name;
        if (!bleMgr.begin(bleName)) {
            Serial.println(F("[BLE] ОШИБКА инициализации! BLE отключено."));
            configMgr.cfg.ble_enabled = false;
        } else {
            Serial.printf("[BLE] OK (heap after: %d)\n", ESP.getFreeHeap());
        }
    } else {
        Serial.println(F("[BLE] Отключен в конфигурации"));
    }

    // 6. Запуск WiFi (AP + STA режимы)
    // Если BLE включён — WiFi должен использовать modem sleep (WIFI_PS_MIN_MODEM)
    wifiMgr.setBluetoothCoex(configMgr.cfg.ble_enabled);

    const char* staSsid = configMgr.cfg.wifi_ssid;
    const char* staPass = configMgr.cfg.wifi_pass;
    const char* apSsid = configMgr.cfg.ap_ssid;
    const char* apPass = configMgr.cfg.ap_pass;
    bool useApNat = configMgr.cfg.ap_nat;
    if (configMgr.cfg.mesh_enabled) {
        // In mesh mode, replace the regular ESP-HUB hotspot with mesh SSID.
        if (strlen(configMgr.cfg.mesh_ssid) > 0) apSsid = configMgr.cfg.mesh_ssid;
        if (strlen(configMgr.cfg.mesh_pass) >= 8) apPass = configMgr.cfg.mesh_pass;
        // ESP32-C3 + painlessMesh is unstable with concurrent STA reconnect loops.
        // Keep WiFi in AP role for mesh and web portal stability.
        staSsid = "";
        staPass = "";
        useApNat = false;
        Serial.printf("[WIFI] Mesh AP mode: SSID='%s'\n", apSsid);
        Serial.println(F("[WIFI] Mesh mode: STA uplink disabled for radio stability"));
    }

    wifiMgr.begin(staSsid, staPass,
                  apSsid, apPass,
                  useApNat);

    // ВАЖНО: НЕ ждём здесь подключение WiFi! Это замораживает систему на 16 сек.
    // Вместо этого пускаем инициализацию сразу, WiFi подключится в фоне через loop()
    // Даём 8 секунд на включение AP режима (хватит времени на инициализацию)
    uint32_t wifiWait = millis();
    while (!wifiMgr.isAP() && millis() - wifiWait < 8000) {
        wifiMgr.tick();
        delay(50);
    }
    Serial.println(F("[WIFI] Инициализация завершена, продолжаю загрузку..."));

    // Start mDNS — works both in STA and AP mode
    if (MDNS.begin("esp-hub")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println(F("[mDNS] http://esp-hub.local/ registered"));
    } else {
        Serial.println(F("[mDNS] Failed to start"));
    }

    // 7. Start MQTT
    if (strlen(configMgr.cfg.mqtt_host) > 0) {
        mqttClient.begin(configMgr.cfg.mqtt_host,
                         configMgr.cfg.mqtt_port,
                         configMgr.cfg.mqtt_user,
                         configMgr.cfg.mqtt_pass,
                         configMgr.cfg.mqtt_topic,
                         configMgr.cfg.device_name);
        mqttClient.setInterval(configMgr.cfg.mqtt_interval_s);
    }

    // 7b. BLE инициализирован на шаге 5b (до WiFi) — важен порядок инициализации

    // 8. Запуск веб-портала (HTTP сервер на порту 80)
    // Доступен через: http://esp-hub.local/ или http://192.168.4.1
    portal.begin(&configMgr, &wifiMgr, &mqttClient, &sensorMgr, &bleMgr, &fixtureMgr, &meshMgr);

    // 9. Запуск планировщика GPIO (автоматические таймеры выводов)
    gpioSched.begin(&configMgr.cfg);

    // 10. Инициализация управления светильником (UART2)
    if (configMgr.cfg.fixture.enabled) {
        fixtureMgr.enable(true);
        fixtureMgr.begin(&configMgr.cfg.fixture);
        // Установка сохранённых значений яркости
        fixtureMgr.setChannels(
            configMgr.cfg.fixture.red_brightness,
            configMgr.cfg.fixture.far_red_brightness,
            configMgr.cfg.fixture.blue_brightness,
            configMgr.cfg.fixture.white_brightness
        );
    }

     // 11. Serial консоль — доступ к интерфейсу через USB-кабель и Mesh
    serialCon.begin(&configMgr, &wifiMgr, &sensorMgr, &bleMgr, &fixtureMgr, &meshMgr);

     // 12. Initialize Mesh Network (if enabled)
     if (configMgr.cfg.mesh_enabled) {
         meshMgr.begin(configMgr.cfg.mesh_ssid,
                       configMgr.cfg.mesh_pass,
                       configMgr.cfg.mesh_port,
                       configMgr.cfg.mesh_channel);
         meshMgr.setReceiveCallback(onMeshMessage);
         Serial.println("[MAIN] Mesh Network enabled");
     } else {
         Serial.println("[MAIN] Mesh Network disabled");
     }

     // 13. Rate Limiter (after LittleFS)
     rateLimiter.begin(configMgr.cfg.rl_enabled,
                       configMgr.cfg.rl_max_hour,
                       configMgr.cfg.rl_max_day);

    // 13. CRON scheduler
    if (configMgr.cfg.cron_enabled) {
        cronMgr.begin();
        if (configMgr.cfg.cron_tz[0])
            cronMgr.setTimezone(configMgr.cfg.cron_tz);
    }

    // Start CRON background task
    if (configMgr.cfg.cron_enabled) cronMgr.startTask();

    Serial.println(F("\n[MAIN] Инициализация завершена!"));
    Serial.printf("[MAIN] Свободной памяти: %d байт\n", ESP.getFreeHeap());
}

void loop() {
    // Обновление состояния WiFi (переподключение, синхронизация)
    wifiMgr.tick();

    // Обновление системного часа (резервный таймер и синхронизация с NTP)
    systemClock.tick();

    // Обновление BLE (перезапуск рекламирования если отключился клиент)
    bleMgr.tick();

    // Обновление CAN (обработка входящих фреймов из очереди)
    if (canMgr.isRunning()) canMgr.tick();

    // Обновление Mesh Network
    if (configMgr.cfg.mesh_enabled) meshMgr.tick();

    // Обработка HTTP запросов (веб-интерфейс и API)
    portal.tick();

    // Serial консоль — обработка команд USB-Serial
    serialCon.tick();

    // Обновление GPIO таймеров (автоматические переключения выводов)
    gpioSched.tick();

    // Обновление управления светильником
    if (fixtureMgr.isEnabled()) {
        fixtureMgr.tick();
    }

    // Обновление MQTT (отправка телеметрии)
    mqttClient.tick();

    // Poll CRON for any fired actions and execute via serial command parser.
    {
        static char _cronAction[CRON_ACTION_LEN];
        if (cronMgr.pollFired(_cronAction, sizeof(_cronAction)))
            serialCon.executeCommand(String(_cronAction));
    }

    // Periodic sensor reading
    if (millis() - lastSensorRead >= sensorReadInterval) {
        lastSensorRead = millis();
        sensorMgr.readAll();

        // Route non-MQTT outputs (HTTP, CAN TX, Serial) every read cycle
        sensorMgr.publishAll(configMgr.cfg.device_name);
    }

    // MQTT telemetry publish
    if (wifiMgr.isConnected() && mqttClient.isConnected() && mqttClient.ready()) {
        JsonDocument doc;
        sensorMgr.buildJson(doc, configMgr.cfg.device_name);
        mqttClient.publishJson(doc);
    }

    // BLE telemetry notify (reuses the same JSON payload)
    if (bleMgr.isConnected()) {
        static uint32_t lastBleNotify = 0;
        if (millis() - lastBleNotify >= (configMgr.cfg.mqtt_interval_s * 1000UL)) {
            lastBleNotify = millis();
            JsonDocument doc;
            sensorMgr.buildJson(doc, configMgr.cfg.device_name);
            String payload;
            serializeJson(doc, payload);
            bleMgr.notify(payload);
        }
    }

    yield();
}
