#pragma once
// ============== Mesh Manager ==============
// painlessMesh-based mesh networking.
// NOT available on ESP32-C3 (painlessMesh requires ESP-WiFi-Mesh).
// On unsupported chips, all methods are no-op stubs.

#include <Arduino.h>

// painlessMesh only works on original ESP32
#if defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32C3)
#define MESH_SUPPORTED 1
#else
#define MESH_SUPPORTED 0
#endif

#if MESH_SUPPORTED
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <painlessMesh.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
class Scheduler;
#endif

class MeshManager {
public:
#if MESH_SUPPORTED
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

    uint32_t getNodeId();
    uint32_t getConnectedCount();
    String getNodeListJson();

    void setReceiveCallback(void (*callback)(uint32_t from, String &msg));

    String getLogJson(uint8_t limit = 20);
    void clearLog();
    void addLogEntry(const String& line);

private:
    painlessMesh mesh;
    Scheduler* userSched;
    bool _initialized = false;

    void (*userReceiveCallback)(uint32_t from, String &msg) = nullptr;

    static const uint8_t LOG_CAPACITY = 64;
    String _log[LOG_CAPACITY];
    uint8_t _logHead = 0;
    uint8_t _logCount = 0;

    static String jsonEscape(const String& s);
    void appendLog(const String& line);

    static MeshManager* instance;
    static void receivedCallback(uint32_t from, String &msg);
    static void newConnectionCallback(uint32_t nodeId);
    static void changedConnectionCallback();
    static void nodeTimeAdjustedCallback(int32_t offset);

#else
    // Stub implementation for ESP32-C3 and other unsupported chips
    MeshManager() {}
    ~MeshManager() {}

    void begin(const char*, const char*, uint16_t, uint8_t) {
        Serial.println(F("[MESH] Not supported on this chip"));
    }
    void tick() {}
    bool isConnected() { return false; }

    void sendMessage(String) {}
    void broadcastMessage(String) {}
    void sendToNode(uint32_t, const String&) {}
    void sendChatMessage(const String&, const String&, const String& = "all") {}
    void sendDataMessage(const String&, const String&) {}
    void sendCommandMessage(const String&, const String& = "all", uint32_t = 0) {}

    uint32_t getNodeId() { return 0; }
    uint32_t getConnectedCount() { return 0; }
    String getNodeListJson() { return "[]"; }

    void setReceiveCallback(void (*)(uint32_t, String &)) {}

    String getLogJson(uint8_t = 20) { return "[]"; }
    void clearLog() {}
    void addLogEntry(const String&) {}
#endif
};