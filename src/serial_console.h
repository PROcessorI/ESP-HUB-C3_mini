#pragma once
// ============================================================
//  ESP-HUB Serial Console
//  Полный интерфейс управления через USB-Serial (COM-порт)
//
//  Подключение: USB-кабель → Arduino Serial Monitor / PuTTY
//  Скорость:    115200 бод (или та, что задана в конфиге)
//
//  При первом подключении нажмите Enter, чтобы увидеть меню.
//  Все команды регистронезависимы.
// ============================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "sensor_manager.h"
#include "wifi_manager.h"
#include "ble_manager.h"
#include "fixture_manager.h"
#include "system_clock.h"
#include "mesh_manager.h"

#define SC_ANSI 0          // 1 = ANSI-цвета включены (PuTTY/VS Code Monitor)
                           // 0 = чистый текст (Arduino Serial Monitor)

class SerialConsole {
public:
    void begin(ConfigManager*  cfg,
               WiFiManager*   wifi,
               SensorManager* sensors,
               BLEManager*    ble,
               FixtureManager* fixture = nullptr,
               MeshManager*   mesh    = nullptr);

    // Call in loop() — reads Serial and processes commands
    void tick();

    // Execute one command line programmatically (used by mesh command relay)
    void executeCommand(const String& line);

private:
    ConfigManager*  _cfg     = nullptr;
    WiFiManager*    _wifi    = nullptr;
    SensorManager*  _sensors = nullptr;
    BLEManager*     _ble     = nullptr;
    FixtureManager* _fixture = nullptr;
    MeshManager*    _mesh    = nullptr;

    String   _buf;        // line accumulation buffer
    uint32_t _lastAuto;   // auto-print timer
    bool     _initialized = false;

    // Command handlers
    void processLine(const String& line);
    bool parseLightCommand(const String& line); // short commands: R10, FR50, B100, W200
    void cmdHelp();
    void cmdStatus();
    void cmdSensors();
    void cmdConfig();
    void cmdSet(const String& args);
    void cmdGpio(const String& args);
    void cmdWifi(const String& args);   // wifi on|off|scan|status
    void cmdBle(const String& args);    // ble on|off|status
    void cmdMesh(const String& args);   // mesh on|off|status|nodes
    void cmdLight(const String& args);  // light commands: on|off|red|blue|white|grow|set
    void cmdScenario(const String& args);  // scenario enable|disable|status|list
    void cmdTimer(const String& args);  // timer enable|disable|trigger|list|set
    void cmdDim(const String& args);    // dim increase|decrease|<step>
    void cmdRead();                     // force sensor read + print
    void cmdClock();                    // clock — show system time and backup timer status
    void cmdScan();                     // WiFi network scan
    void cmdAuto(const String& args);   // auto <sec>|off
    void cmdMonitor();                  // monitor — continuous read until Enter
    void cmdMqtt(const String& args);   // mqtt host|port|interval|status
    void cmdJson();                     // json — show full config as JSON
    void cmdReboot();

    // Auto-read state
    uint16_t _autoSec  = 0;     // 0 = disabled
    uint32_t _autoLast = 0;

    // Monitor mode (continuous read until user presses Enter)
    bool     _monitorMode = false;
    uint32_t _monitorLast = 0;

    // Output helpers
    void printBanner();
    void prompt();
    void hr();
    static const char* col(const char* code);
    static String padR(const String& s, int w);
};
