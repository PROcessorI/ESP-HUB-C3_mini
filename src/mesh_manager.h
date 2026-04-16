#pragma once

#include <Arduino.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <painlessMesh.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifndef MESH_PAINLESS_SUPPORTED
#define MESH_PAINLESS_SUPPORTED 1
#endif

#ifndef MESH_SUPPORTED
#define MESH_SUPPORTED MESH_PAINLESS_SUPPORTED
#endif

// Forward declaration to avoid TaskScheduler.h inclusion horror
class Scheduler;

class MeshManager {
public:
    MeshManager();
    ~MeshManager();
    
    void begin(const char* prefix, const char* password, uint16_t port, uint8_t channel);
    void tick();
    bool isConnected();
    
    void sendMessage(String message);
    void broadcastMessage(String message);
    void sendToNode(uint32_t nodeId, const String& message);
    void sendChatMessage(const String& fromName, const String& text, const String& target = "all");
    void sendDataMessage(const String& topic, const String& payload);
    void sendCommandMessage(const String& commandLine, const String& target = "all", uint32_t id = 0);
    void sendTimeSync(time_t currentTime);  // Broadcast time to all nodes
    
    // Get mesh status
    uint32_t getNodeId();
    uint32_t getConnectedCount();
    String getNodeListJson();
    String getNodeWebListJson(bool includeSelf = false);
    String getMeshIP();  // Get mesh network IP address
    
    // Set callback for received messages
    void setReceiveCallback(void (*callback)(uint32_t from, String &msg));

    // Message log (for web microchat/debug)
    String getLogJson(uint8_t limit = 20);
    void clearLog();
    void addLogEntry(const String& line);
    
private:
    painlessMesh mesh;
    Scheduler* userSched; // Scheduler for mesh tasks (pointer to avoid header bloat)
    bool _initialized = false;
    
    // Callback storage
    void (*userReceiveCallback)(uint32_t from, String &msg) = nullptr;

    // Compact ring-buffer log for mesh traffic
    static const uint8_t LOG_CAPACITY = 64;
    String _log[LOG_CAPACITY];
    uint8_t _logHead = 0;
    uint8_t _logCount = 0;

    static String jsonEscape(const String& s);
    static IPAddress nodeIdToApIp(uint32_t nodeId);
    void appendLog(const String& line);
    
    // Static instance pointer for callbacks
    static MeshManager* instance;
    
    // Static wrapper callbacks that painlessMesh will call
    static void receivedCallback(uint32_t from, String &msg);
    static void newConnectionCallback(uint32_t nodeId);
    static void changedConnectionCallback();
    static void nodeTimeAdjustedCallback(int32_t offset);
    
};