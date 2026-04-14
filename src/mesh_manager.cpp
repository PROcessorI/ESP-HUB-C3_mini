#include "mesh_manager.h"
#include <TaskScheduler.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>

// Initialize static instance pointer
MeshManager* MeshManager::instance = nullptr;

MeshManager::MeshManager() {
    // Constructor
    instance = this;
    userSched = new Scheduler();
}

MeshManager::~MeshManager() {
    // Destructor
    instance = nullptr;
    if (userSched) {
        delete userSched;
        userSched = nullptr;
    }
}

void MeshManager::begin(const char* prefix, const char* password, uint16_t port, uint8_t channel) {
    if (_initialized) return;

    const char* meshPrefix = (prefix && prefix[0]) ? prefix : "ESP-HUB-MESH";
    const char* meshPass = (password && password[0]) ? password : "1234567890";
    uint16_t meshPort = (port > 0) ? port : 5555;
    uint8_t meshChannel = (channel >= 1 && channel <= 13) ? channel : 6;

    // Keep mesh on the currently active Wi-Fi channel ONLY if STA is already connected.
    // Otherwise, we get garbage channels during scan/connecting phases which break the mesh!
    if (WiFi.status() == WL_CONNECTED) {
        uint8_t activeChannel = WiFi.channel();
        if (activeChannel >= 1 && activeChannel <= 13) {
            if (meshChannel != activeChannel) {
                Serial.printf("[MESH] WiFi STA connected explicitly on CH %u. Overriding MESH config (%u).\n",
                              (unsigned)activeChannel,
                              (unsigned)meshChannel);
                meshChannel = activeChannel;
            }
        }
    } else {
        Serial.printf("[MESH] Forcing Wi-Fi to configured MESH CH %u (STA not yet connected)\n", (unsigned)meshChannel);
        // Force the radio to the configured mesh channel so that early mesh packets go to the right place
        // even if STA hasn't connected to the home router yet.
        esp_wifi_set_channel(meshChannel, WIFI_SECOND_CHAN_NONE);
    }

    // Initialize mesh network
    mesh.setDebugMsgTypes(ERROR | STARTUP);  // Set before init() so that you can see startup messages
    
    mesh.init(meshPrefix, meshPass, userSched, meshPort, WIFI_AP_STA, meshChannel);
    mesh.onReceive(&receivedCallback);
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onChangedConnections(&changedConnectionCallback);
    mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

    _initialized = true;
    
    Serial.println("[MESH] Mesh network initialized");
    Serial.printf("[MESH] SSID: %s, Port: %u, Channel: %u\n", meshPrefix, (unsigned)meshPort, (unsigned)meshChannel);
    IPAddress apIp = WiFi.softAPIP();
    if (apIp == IPAddress(0, 0, 0, 0)) {
        apIp = WiFi.localIP();
    }
    Serial.printf("[MESH] AP IP: %s\n", apIp.toString().c_str());
    Serial.printf("[MESH] Web URL: http://%s/\n", apIp.toString().c_str());
}

void MeshManager::tick() {
    if (!_initialized) return;

    // Update mesh network
    mesh.update();
}

bool MeshManager::isConnected() {
    if (!_initialized) return false;

    return mesh.getNodeList().size() > 0;
}

void MeshManager::sendMessage(String message) {
    // Broadcast message to all nodes
    broadcastMessage(message);
}

void MeshManager::broadcastMessage(String message) {
    if (!_initialized) return;

    mesh.sendBroadcast(message);
    appendLog(String("TX ") + message);
}

void MeshManager::sendToNode(uint32_t nodeId, const String& message) {
    if (!_initialized) return;

    mesh.sendSingle(nodeId, message);
    appendLog(String("TX[") + String(nodeId) + "] " + message);
}

void MeshManager::sendChatMessage(const String& fromName, const String& text, const String& target) {
    String json = "{\"type\":\"chat\",\"from\":\"";
    json += jsonEscape(fromName);
    json += "\",\"target\":\"";
    json += jsonEscape(target);
    json += "\",\"text\":\"";
    json += jsonEscape(text);
    json += "\",\"ts\":";
    json += millis();
    json += "}";

    if (target.startsWith("node:")) {
        uint32_t nodeId = (uint32_t)strtoul(target.substring(5).c_str(), nullptr, 10);
        if (nodeId > 0) {
            sendToNode(nodeId, json);
            return;
        }
    }

    broadcastMessage(json);
}

void MeshManager::sendDataMessage(const String& topic, const String& payload) {
    String json = "{\"type\":\"data\",\"topic\":\"";
    json += jsonEscape(topic);
    json += "\",\"payload\":\"";
    json += jsonEscape(payload);
    json += "\",\"ts\":";
    json += millis();
    json += "}";
    broadcastMessage(json);
}

void MeshManager::sendCommandMessage(const String& commandLine, const String& target, uint32_t id) {
    uint32_t cmdId = id ? id : (uint32_t)millis();
    String json = "{\"type\":\"cmd\",\"id\":";
    json += cmdId;
    json += ",\"target\":\"";
    json += jsonEscape(target);
    json += "\",\"cmd\":\"";
    json += jsonEscape(commandLine);
    json += "\",\"ts\":";
    json += millis();
    json += "}";

    if (target.startsWith("node:")) {
        uint32_t nodeId = (uint32_t)strtoul(target.substring(5).c_str(), nullptr, 10);
        if (nodeId > 0) {
            sendToNode(nodeId, json);
            return;
        }
    }

    broadcastMessage(json);
}

void MeshManager::sendTimeSync(time_t currentTime) {
    if (!_initialized) return;
    
    String json = "{\"type\":\"timesync\",\"time\":";
    json += (uint32_t)currentTime;
    json += ",\"ts\":";
    json += millis();
    json += "}";
    
    broadcastMessage(json);
    Serial.printf("[MESH] Time sync broadcast: %u\n", (uint32_t)currentTime);
}

// Static wrapper callbacks that painlessMesh will call
void MeshManager::receivedCallback(uint32_t from, String &msg) {
    Serial.printf("[MESH] Received from %u: %s\n", from, msg.c_str());

    if (instance) {
        instance->appendLog(String("RX[") + String(from) + "] " + msg);
    }
    
    // Call user callback if set
    if (instance && instance->userReceiveCallback) {
        instance->userReceiveCallback(from, msg);
    }
}

void MeshManager::newConnectionCallback(uint32_t nodeId) {
    Serial.printf("[MESH] New Connection, nodeId = %u\n", nodeId);
    // Call user callback if set (we don't have one for this yet)
}

void MeshManager::changedConnectionCallback() {
    Serial.printf("[MESH] Changed connections\n");
    // Print the connection list
    if (instance) {
        std::list<uint32_t> nodes = instance->mesh.getNodeList();
        Serial.printf("Number of connections: %lu\n", (unsigned long)nodes.size());
        // Call user callback if set (we don't have one for this yet)
    }
}

void MeshManager::nodeTimeAdjustedCallback(int32_t offset) {
    Serial.printf("[MESH] Adjusted time. Offset = %d\n", offset);
    // Call user callback if set (we don't have one for this yet)
}

void MeshManager::setReceiveCallback(void (*callback)(uint32_t from, String &msg)) {
    userReceiveCallback = callback;
}

uint32_t MeshManager::getNodeId() {
    if (!_initialized) return 0;

    return mesh.getNodeId();
}

uint32_t MeshManager::getConnectedCount() {
    if (!_initialized) return 0;

    return (uint32_t)mesh.getNodeList().size();
}

String MeshManager::getNodeListJson() {
    if (!_initialized) return "[]";

    String json = "[";
    std::list<uint32_t> nodes = mesh.getNodeList();
    bool first = true;
    for (uint32_t nodeId : nodes) {
        if (!first) json += ",";
        json += nodeId;
        first = false;
    }
    json += "]";
    return json;
}

String MeshManager::getMeshIP() {
    if (!_initialized) return "";
    return mesh.getAPIP().toString();
}

String MeshManager::jsonEscape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}

void MeshManager::appendLog(const String& line) {
    _log[_logHead] = line;
    _logHead = (uint8_t)((_logHead + 1) % LOG_CAPACITY);
    if (_logCount < LOG_CAPACITY) _logCount++;
}

String MeshManager::getLogJson(uint8_t limit) {
    if (limit == 0) limit = 1;
    if (limit > LOG_CAPACITY) limit = LOG_CAPACITY;

    uint8_t take = (_logCount < limit) ? _logCount : limit;
    String json = "[";
    bool first = true;
    uint8_t start = (_logHead + LOG_CAPACITY - take) % LOG_CAPACITY;

    for (uint8_t i = 0; i < take; i++) {
        uint8_t idx = (uint8_t)((start + i) % LOG_CAPACITY);
        if (!first) json += ",";
        json += "\"";
        json += jsonEscape(_log[idx]);
        json += "\"";
        first = false;
    }
    json += "]";
    return json;
}

void MeshManager::clearLog() {
    _logHead = 0;
    _logCount = 0;
}

void MeshManager::addLogEntry(const String& line) {
    appendLog(line);
}