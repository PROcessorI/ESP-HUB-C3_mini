#include "mesh_manager.h"

#if MESH_SUPPORTED

#include <TaskScheduler.h>
#include <WiFi.h>

// Initialize static instance pointer
MeshManager* MeshManager::instance = nullptr;

MeshManager::MeshManager() {
    instance = this;
    userSched = new Scheduler();
}

MeshManager::~MeshManager() {
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

    mesh.setDebugMsgTypes(ERROR | STARTUP);
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
    mesh.update();
}

bool MeshManager::isConnected() {
    if (!_initialized) return false;
    return mesh.getNodeList().size() > 0;
}

void MeshManager::sendMessage(String message) {
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

void MeshManager::receivedCallback(uint32_t from, String &msg) {
    Serial.printf("[MESH] Received from %u: %s\n", from, msg.c_str());
    if (instance) {
        instance->appendLog(String("RX[") + String(from) + "] " + msg);
    }
    if (instance && instance->userReceiveCallback) {
        instance->userReceiveCallback(from, msg);
    }
}

void MeshManager::newConnectionCallback(uint32_t nodeId) {
    Serial.printf("[MESH] New Connection, nodeId = %u\n", nodeId);
}

void MeshManager::changedConnectionCallback() {
    Serial.printf("[MESH] Changed connections\n");
    if (instance) {
        std::list<uint32_t> nodes = instance->mesh.getNodeList();
        Serial.printf("Number of connections: %lu\n", (unsigned long)nodes.size());
    }
}

void MeshManager::nodeTimeAdjustedCallback(int32_t offset) {
    Serial.printf("[MESH] Adjusted time. Offset = %d\n", offset);
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

#endif // MESH_SUPPORTED