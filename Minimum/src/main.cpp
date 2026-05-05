// ============================================================
//  ESP-HUB Minimal — Light Fixtures + Mesh Network + Serial Console
// ============================================================

#include <Arduino.h>
#include <ESPmDNS.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_log.h>

#include "config.h"
#include "wifi_manager.h"
#include "serial_console.h"
#include "fixture_manager.h"
#include "mesh_manager.h"
#include "web_portal.h"

ConfigManager   configMgr;
WiFiManager     wifiMgr;
SerialConsole   serialCon;
FixtureManager  fixtureMgr;
MeshManager    meshMgr;
WebPortal       webPortal;

static bool isLoopProneMeshCommand(const String& cmd) {
    String s = cmd;
    s.trim();
    s.toLowerCase();
    if (!s.startsWith("mesh ")) return false;
    if (s == "mesh" || s == "mesh cmd" || s == "mesh chat" || s == "mesh data" ||
        s == "mesh status" || s == "mesh on" || s == "mesh off" || s == "mesh nodes" ||
        s == "mesh log" || s == "mesh clear") {
        return true;
    }
    if (s.startsWith("mesh cmd ") || s.startsWith("mesh chat ") || s.startsWith("mesh data ") ||
        s.startsWith("mesh status ") || s.startsWith("mesh on ") || s.startsWith("mesh off ") ||
        s.startsWith("mesh nodes ") || s.startsWith("mesh log ") || s.startsWith("mesh clear ")) {
        return true;
    }
    return false;
}

    static void onMeshMessage(uint32_t from, String &msg) {
    if (msg.length() == 0) {
        Serial.printf("[MESH] Ignoring empty message from %u\n", from);
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, msg);
    if (err) {
        Serial.printf("[MESH] Non-JSON from %u: %s\n", from, msg.c_str());
        return;
    }

    const char* type = doc["type"] | "";
    uint32_t myNode = meshMgr.getNodeId();
    const char* myRole = "PEER";

    if (strcmp(type, "cmd") == 0) {
        const char* target = doc["target"] | "all";
        const char* command = doc["cmd"] | "";
        uint32_t cmdId = doc["id"] | 0;

        bool targetMatch = (strcmp(target, "all") == 0);
        if (!targetMatch) {
            String nodeTarget = "node:" + String(myNode);
            targetMatch = (strcmp(target, nodeTarget.c_str()) == 0);
        }

        if (strlen(command) == 0) {
            meshMgr.addLogEntry(String("EXEC SKIP role=") + myRole + " node=" + String(myNode) + " from=" + String(from) + " reason=empty_cmd");
            Serial.printf("[MESH] SKIP empty command from %u\n", from);
            return;
        }

        if (!targetMatch) {
            meshMgr.addLogEntry(String("EXEC SKIP role=") + myRole + " node=" + String(myNode) + " from=" + String(from) + " target=" + String(target) + " reason=target_mismatch");
            Serial.printf("[MESH] SKIP cmd from %u (target=%s, myNode=%u)\n", from, target, (unsigned)myNode);
            return;
        }

        String cmdLine = command;
        cmdLine.trim();
        if (isLoopProneMeshCommand(cmdLine)) {
            meshMgr.addLogEntry(String("EXEC SKIP role=") + myRole + " node=" + String(myNode) + " from=" + String(from) + " reason=loop_prone");
            Serial.printf("[MESH] SKIP loop-prone from %u: %s\n", from, cmdLine.c_str());
            return;
        }

        meshMgr.addLogEntry(String("EXEC role=") + myRole + " node=" + String(myNode) + " from=" + String(from) + " target=" + String(target) + " cmd=" + cmdLine);
        Serial.printf("[MESH] EXEC role=%s node=%u from=%u target=%s cmd=%s\n", myRole, (unsigned)myNode, from, target, cmdLine.c_str());
        serialCon.executeCommand(cmdLine, true);

        if (cmdId > 0) {
            String ack = "{\"type\":\"ack\",\"id\":" + String(cmdId) + ",\"ok\":true,\"node\":" + String(myNode) + ",\"msg\":\"executed\"}";
            meshMgr.sendToNode(from, ack);
            meshMgr.addLogEntry(String("ACK TX node=") + String(myNode) + " to=" + String(from));
        }
        return;
    }

    if (strcmp(type, "chat") == 0) {
        const char* target = doc["target"] | "all";
        bool targetMatch = (strcmp(target, "all") == 0);
        if (!targetMatch) {
            String nodeTarget = "node:" + String(myNode);
            targetMatch = (strcmp(target, nodeTarget.c_str()) == 0);
        }
        if (!targetMatch) return;

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
        const char* myRole = "PEER";
        meshMgr.addLogEntry(String("ACK RX role=") + myRole + " node=" + String(myNode) + " from=" + String(from) + " src_node=" + String(node) + " id=" + String(id) + " ok=" + (ok ? "1" : "0") + " msg=" + String(txt));
        Serial.printf("[MESH ACK] id=%u node=%u ok=%s msg=%s\n", (unsigned)id, (unsigned)node, ok ? "true" : "false", txt);
        return;
    }
}

void Setup_init_lights() {
    configMgr.cfg.fixture.red_brightness = 0;
    configMgr.cfg.fixture.far_red_brightness = 0;
    configMgr.cfg.fixture.blue_brightness = 0;
    configMgr.cfg.fixture.white_brightness = 0;
    if (fixtureMgr.isEnabled()) {
        fixtureMgr.setChannels(0, 0, 0, 0);
    }
}

void setup() {
    // Disable task watchdog to prevent resets during long operations
    esp_task_wdt_init(30, false);
    esp_task_wdt_delete(NULL);

    Serial.begin(115200);
    delay(500);

    // Initialize Serial1 for Inventronics driver (UART1)
    // ESP32-C3 has UART0 (USB) and UART1 (GPIO)
    // TX, RX, 9600 baud, 8N1
    Serial1.begin(9600, SERIAL_8N1, INV_RX_PIN, INV_TX_PIN);
    Serial.println(F("[UART1] Inventronics driver initialized (9600 baud)"));

    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);

    configMgr.begin();

    Setup_init_lights();

    Serial.println(F("\n"
        "\033[36m"
        "  ______  _____  _____       _    _  _    _  ____   \n"
        " |  ____|/ ____||  __ \\     | |  | || |  | ||  _ \\  \n"
        " | |__  | (___  | |__) |____| |__| || |  | || |_) | \n"
        " |  __|  \\___ \\ |  ___/_____|  __  || |  | ||  _ <  \n"
        " | |____ ____) || |         | |  | || |__| || |_) | \n"
        " |______|_____/ |_|         |_|  |_| \\____/ |____/  \n"
        "\033[0m"
        "\033[92m  v1.0 [ESP32-C3]\033[0m - Minimal Hub\n"
        "=================================================="));

    configMgr.printConfig();

    if (configMgr.cfg.mesh_enabled) {
        const char* apSsid = configMgr.cfg.mesh_ssid;
        const char* apPass = configMgr.cfg.mesh_pass;
        Serial.printf("[WIFI] Mesh AP mode: SSID='%s'\n", apSsid);
        wifiMgr.begin("", "", apSsid, apPass, false);
    } else {
        wifiMgr.begin(configMgr.cfg.wifi_ssid, configMgr.cfg.wifi_pass,
                    configMgr.cfg.ap_ssid, configMgr.cfg.ap_pass,
                    configMgr.cfg.ap_nat);
    }

    uint32_t wifiWait = millis();
    while (!wifiMgr.isAP() && millis() - wifiWait < 8000) {
        wifiMgr.tick();
        delay(50);
        yield();  // Feed watchdog during extended wait
    }
    Serial.println(F("[WIFI] Initialized"));

    if (MDNS.begin("esp-hub")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println(F("[mDNS] http://esp-hub.local/ registered"));
    }

    if (configMgr.cfg.fixture.enabled) {
        fixtureMgr.enable(true);
        fixtureMgr.begin(&configMgr.cfg.fixture);
        fixtureMgr.setChannels(
            configMgr.cfg.fixture.red_brightness,
            configMgr.cfg.fixture.far_red_brightness,
            configMgr.cfg.fixture.blue_brightness,
            configMgr.cfg.fixture.white_brightness
        );
    }

    serialCon.begin(&configMgr, &wifiMgr, nullptr, nullptr, &fixtureMgr, &meshMgr);

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

    Serial.println(F("\n[MAIN] Initialization complete!"));
    Serial.printf("[MAIN] Free heap: %d bytes\n", ESP.getFreeHeap());
    
    // Start web portal AFTER everything else is initialized
    webPortal.begin(&configMgr, &wifiMgr, &fixtureMgr, &meshMgr);
}

void loop() {
    wifiMgr.tick();
    webPortal.tick();
    serialCon.tick();

    if (configMgr.cfg.mesh_enabled) {
        meshMgr.tick();
    }

    if (fixtureMgr.isEnabled()) {
        fixtureMgr.tick();
    }

    yield();
}