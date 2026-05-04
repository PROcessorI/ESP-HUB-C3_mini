#pragma once
// ============================================================
//  ESP-HUB Serial Console (Minimal)
// ============================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "wifi_manager.h"
#include "fixture_manager.h"
#include "mesh_manager.h"

#define SC_ANSI 0

// Inventronics NSM-1k2q200mg Protocol Constants
// ESP32-C3: Use UART1 (Serial1) - TX=7, RX=6
#define INV_TX_PIN 7
#define INV_RX_PIN 6
#define INV_BAUD 9600
#define INV_FRAME_HEADER 0x3A
#define INV_CMD_SET 0x3C
#define INV_CMD_SET_RESP 0x3D
#define INV_OFFSET_MULTI 0xEE
#define INV_DATA_LENGTH 0x05
#define INV_CHANNEL_MASK 0x0F
#define INV_FRAME_END_CR 0x0D
#define INV_FRAME_END_LF 0x0A
#define INV_ACK_OK 0x55
#define INV_BRIGHTNESS_OFF 0
#define INV_BRIGHTNESS_100 200
#define INV_CMD_INTERVAL_MS 150
#define INV_RESPONSE_TIMEOUT_MS 500

class SerialConsole {
public:
    void begin(ConfigManager*  cfg,
               WiFiManager*   wifi,
               void* sensors,
               void* ble,
               FixtureManager* fixture = nullptr,
               MeshManager*   mesh    = nullptr);

    void tick();

    void executeCommand(const String& line, bool fromMesh = false);

private:
    ConfigManager*  _cfg     = nullptr;
    WiFiManager*    _wifi    = nullptr;
    FixtureManager* _fixture = nullptr;
    MeshManager*    _mesh    = nullptr;

    String   _buf;
    uint32_t _lastAuto;
    bool     _initialized = false;

    void processLine(const String& line, bool fromMesh = false);
    bool parseLightCommand(const String& line);
    void cmdHelp();
    void cmdStatus();
    void cmdConfig();
    void cmdSet(const String& args);
    void cmdWifi(const String& args);
    void cmdMesh(const String& args);
    void cmdLight(const String& args);
    void cmdScenario(const String& args);
    void cmdTimer(const String& args);
    void cmdDim(const String& args);
    void cmdReboot();

    uint16_t _autoSec  = 0;
    uint32_t _autoLast = 0;

    bool     _monitorMode = false;
    uint32_t _monitorLast = 0;

    void printBanner();
    void prompt();
    void hr();
    static const char* col(const char* code);
    static String padR(const String& s, int w);
};