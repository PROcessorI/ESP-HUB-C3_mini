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
    _lastConnectionCheck = 0;
    _reconnectAttempts = 0;
    _channelFixed = false;
    _fixedChannel = 6;
    _lastNodeCount = 0;
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

    // Check available memory before starting mesh
    if (ESP.getFreeHeap() < 15000) {
        Serial.println("[MESH] Low memory, skipping init");
        return;
    }

    const char* meshPrefix = (prefix && prefix[0]) ? prefix : "ESP-HUB-MESH";
    const char* meshPass = (password && password[0]) ? password : "1234567890";
    size_t meshPassLen = strlen(meshPass);
    if (meshPassLen < 8 || meshPassLen > 63) {
        Serial.printf("[MESH] Invalid password length=%u, fallback to default\n", (unsigned)meshPassLen);
        meshPass = "1234567890";
        meshPassLen = strlen(meshPass);
    }
    uint16_t meshPort = (port > 0) ? port : 5555;
    uint8_t meshChannel = (channel >= 1 && channel <= 13) ? channel : 6;

    // FIXED CHANNEL: Never change during operation!
    _fixedChannel = meshChannel;
    _channelFixed = true;

    Serial.printf("[MESH] Starting on CH%u (heap: %d)\n", (unsigned)_fixedChannel, ESP.getFreeHeap());

    // Initialize mesh network with ERROR + STARTUP messages
    mesh.setDebugMsgTypes(ERROR | STARTUP);

    // Explicitly keep SSID visible and allow max clients for mesh stability
    // WIFI_AP_STA mode allows both AP and STA simultaneously for mesh
    mesh.init(meshPrefix, meshPass, userSched, meshPort, WIFI_AP_STA, _fixedChannel, 0, 8);

    // Wait for initialization
    delay(200);

    // Ensure AP is configured correctly
    if (WiFi.softAPSSID() != meshPrefix) {
        WiFi.softAPdisconnect(true);
        delay(100);
    }

    // Configure AP manually if needed
    if (WiFi.softAPSSID().length() == 0) {
        bool apOk = WiFi.softAP(meshPrefix, meshPass, _fixedChannel, 0, 8);
        Serial.printf("[MESH] SoftAP init: %s\n", apOk ? "OK" : "FAILED");
        delay(100);
    }

    // Register callbacks
    mesh.onReceive(&receivedCallback);
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onChangedConnections(&changedConnectionCallback);
    mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

    _initialized = true;
    _lastMeshActivity = millis();
    _reconnectAttempts = 0;

    Serial.println("[MESH] Mesh network initialized");
    Serial.printf("[MESH] SSID: %s, Port: %u, Channel: %u\n", meshPrefix, (unsigned)meshPort, (unsigned)_fixedChannel);
    Serial.printf("[MESH] AP SSID: %s\n", WiFi.softAPSSID().c_str());
    Serial.printf("[MESH] AP channel: %u\n", (unsigned)WiFi.channel());
    Serial.printf("[MESH] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("[MESH] Web: http://%s/\n", WiFi.softAPIP().toString().c_str());
}

void MeshManager::tick() {
    if (!_initialized) return;

    // Update mesh network - mesh.update() can occasionally fail, just continue
    mesh.update();

    // Update health every 30 seconds (non-blocking check)
    uint32_t now = millis();
    if (now - _lastConnectionCheck >= 30000) {
        _lastConnectionCheck = now;
        checkConnectionHealth();
    }
}

void MeshManager::checkConnectionHealth() {
    uint32_t now = millis();
    uint32_t nodeCount = mesh.getNodeList().size();

    Serial.printf("[MESH] Health: nodes=%u, heap=%d, uptime=%us\n",
                (unsigned)nodeCount, ESP.getFreeHeap(), (unsigned)(now / 1000));

    // Log connection changes
    if (nodeCount != _lastNodeCount) {
        Serial.printf("[MESH] Nodes changed: %u -> %u\n", (unsigned)_lastNodeCount, (unsigned)nodeCount);
        _lastNodeCount = nodeCount;
    }

    // If no activity for 5 minutes and no nodes, mesh may be stale but don't reboot - mesh is optional
    // Just log status
    if (nodeCount == 0 && (now - _lastMeshActivity) > 300000) {
        Serial.println("[MESH] No nodes for 5min - this is normal for single-node mesh");
    }
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
    _lastMeshActivity = millis();
    appendLog(String("TX ") + message);
}

void MeshManager::sendToNode(uint32_t nodeId, const String& message) {
    if (!_initialized) return;

    // Update activity on send attempt
    _lastMeshActivity = millis();

    std::list<uint32_t> nodes = mesh.getNodeList();
    bool directlyConnected = false;
    for (uint32_t n : nodes) {
        if (n == nodeId) {
            directlyConnected = true;
            break;
        }
    }

    if (directlyConnected) {
        mesh.sendSingle(nodeId, message);
    } else {
        Serial.printf("[MESH] Node %u not directly connected, using broadcast relay\n", (unsigned)nodeId);
        mesh.sendBroadcast(message);
    }
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
        instance->_lastMeshActivity = millis();
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

String MeshManager::getNodeWebListJson(bool includeSelf) {
    if (!_initialized) return "[]";

    std::list<uint32_t> nodes = mesh.getNodeList(includeSelf);
    String json = "[";
    bool first = true;
    for (uint32_t nodeId : nodes) {
        IPAddress ip = nodeIdToApIp(nodeId);
        if (!first) json += ",";
        json += "{\"id\":";
        json += nodeId;
        json += ",\"ip\":\"";
        json += ip.toString();
        json += "\",\"url\":\"http://";
        json += ip.toString();
        json += "/mesh\"}";
        first = false;
    }
    json += "]";
    return json;
}

String MeshManager::getMeshIP() {
    if (!_initialized) return "";
    return mesh.getAPIP().toString();
}

IPAddress MeshManager::nodeIdToApIp(uint32_t nodeId) {
    return IPAddress(10,
                     (uint8_t)((nodeId & 0xFF00) >> 8),
                     (uint8_t)(nodeId & 0xFF),
                     1);
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