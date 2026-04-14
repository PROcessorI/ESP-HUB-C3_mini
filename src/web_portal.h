#pragma once
// ============== Web Portal (Веб-интерфейс) ==============
// Лёгкий встроенный веб-UI для конфигурации и мониторинга ESP-HUB
// Генерирует HTML из C++ кода, использует AJAX для обновлений
// Доступен на: http://esp-hub.local/ или http://192.168.4.1

#include <Arduino.h>
#include <WebServer.h>
#include "config.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "sensor_manager.h"
#include "ble_manager.h"
#include "fixture_manager.h"
#include "cron_manager.h"
#include "rate_limiter.h"
#include "mesh_manager.h"

// Forward declarations for global objects
extern MeshManager meshMgr;

class WebPortal {
public:
    void begin(ConfigManager* cfg, WiFiManager* wifi,
               MQTTClient* mqtt, SensorManager* sensors, BLEManager* ble,
               FixtureManager* fixture, MeshManager* mesh);
    void tick();

private:
    WebServer _server{80};
    ConfigManager*  _cfg     = nullptr;
    WiFiManager*    _wifi    = nullptr;
    MQTTClient*     _mqtt    = nullptr;
    SensorManager*  _sensors = nullptr;
    BLEManager*     _ble     = nullptr;
    FixtureManager* _fixture = nullptr;
    MeshManager*    _mesh    = nullptr;

    // Page generators
    void handleRoot();
    void handleSensors();
    void handleMqtt();
    void handleSystem();
    void handleBluetooth();       // NEW: Bluetooth page
    void handleFixtures();        // NEW: Fixtures page
    void handleMesh();            // NEW: Mesh network page
    void handleCamera();          // ESP-CAM viewer + settings
    void handleApiCameraRelay();  // GET /api/camera/relay?path=... (proxy to cam)
    void handleSaveCamera();      // POST /save/camera
    void handleApiMeshStatus();   // GET /api/mesh - get mesh status JSON
    void handleApiMeshSendChat(); // POST /api/mesh/chat
    void handleApiMeshSendData(); // POST /api/mesh/data
    void handleApiMeshSendCmd();  // POST /api/mesh/cmd
    void handleApiMeshLog();      // GET /api/mesh/log
    void handleApiMeshLogClear(); // POST /api/mesh/log/clear
    void handleCron();             // GET  /cron — CRON scheduler page
    void handleApiCronList();      // GET  /api/cron
    void handleApiCronAdd();       // POST /api/cron/add
    void handleApiCronDelete();    // POST /api/cron/delete
    void handleApiCronEnable();    // POST /api/cron/enable
    void handleApiCronTz();        // POST /api/cron/tz
    void handleApiNatToggle();     // POST /api/nat/toggle
    void handleApiDocs();
    void handleApiData();
    void handleApiScan();
    void handleApiWifiStatus();   // GET /api/wifi
    void handleApiMqttStatus();   // GET /api/mqtt
    void handleApiSystemStatus(); // GET /api/system
    void handleApiBleStatus();    // GET /api/ble
    void handleApiBleSend();      // POST /api/ble/send
    void handleApiBlearLog();     // POST /api/ble/clear-log
    void handleApiFixtureStatus();// NEW: GET /api/fixture → JSON brightness values
    void handleApiFixtureSet();   // NEW: POST /api/fixture/set → set brightness
    void handleSaveFixtureTimers();
    void handleApiFixtureOn();
    void handleApiFixtureOff();
    void handleApiFixtureColor();
    void handleApiFixtureTimers();
    void handleApiFixtureTimersSet();
    void handleApiFixtureScenarios();
    void handleApiFixtureScenariosSet();
    void handleApiFixtureToggle();         // POST /api/fixture/toggle
    void handleApiFixtureDim();            // POST /api/fixture/dim
    void handleApiFixtureTimerEnable();    // POST /api/fixture/timer/enable
    void handleApiFixtureScenarioEnable(); // POST /api/fixture/scenario/enable
    void handleApiFixtureTimerTrigger();   // POST /api/fixture/timer/trigger
    void handleApiFixtureEnable();         // POST /api/fixture/enable
    void handleApiFixtureDemo();           // POST /api/fixture/demo
    void handleNotFound();
    void handleWifiSetup();     // Lightweight captive portal WiFi config page

    // Form handlers
    void handleSaveWifi();
    void handleSaveMqtt();
    void handleSaveSensors();
    void handleSaveAp();
    void handleSaveBle();         // NEW: POST /save/ble
    void handleSaveMesh();        // NEW: POST /save/mesh — Mesh settings
    void handleSaveFixture();     // NEW: POST /save/fixture
    void handleSaveScenarios();
    void handleSaveSystem();      // POST /save/system — CPU freq etc
    void handleApiMeshToggle();   // POST /api/mesh/toggle — enable/disable mesh
    void handleResetWifi();       // POST /reset/wifi  — clear WiFi credentials
    void handleGpioApi();         // GET  /api/gpio          — JSON all pin states
    void handleGpioSet();         // POST /api/gpio/set       — configure/drive a pin
    void handleGpioTimersApi();   // GET  /api/gpio/timers    — JSON timer states + countdown
    void handleSaveGpioTimers();  // POST /save/gpio-timers   — save timer config
    void handleReboot();
    void handleReset();

    // HTML helpers (GyverPortal-style: build HTML in String, send chunked)
    void sendPage(const String& title, const String& body);
    // Streaming helpers: startPage() opens response + streams header/nav,
    // endPage() streams footer and closes socket. Use instead of sendPage()
    // to avoid building a large body String.
    void startPage(const String& title);
    void endPage();
    void sendPageHeader(const String& title);  // streams header from flash (no heap String)
    void sendCssStyles();                      // streams CSS from flash (no heap String)
    void sendPageFooter();                     // streams ~6 KB JS from flash (no heap String)
    String navBar(const String& active);

    String wifiConnectCard();

    String inputField(const char* label, const char* name, const char* value,
                      const char* type = "text", const char* placeholder = "");
    String numberField(const char* label, const char* name, int value,
                       int minv = 0, int maxv = 65535);
    String selectField(const char* label, const char* name,
                       const char** options, int count, int selected);
    String checkboxField(const char* label, const char* name, bool checked);
    String submitButton(const char* text, const char* cls = "btn-primary");
    String card(const char* title, const String& content);
    String badge(const char* text, const char* cls);

    // Reboot/save result page with i18n + theme + lang switcher
    String statusPage(const char* icon,
                      const char* headRu, const char* headEn,
                      const char* bodyHtml,
                      const char* redirect, int delaySec);
};
