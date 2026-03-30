#pragma once
// ============== BLE Manager (NimBLE) ==============
// GATT server: two characteristics
//   TX (notify)  — ESP32 → client: sensor JSON, arbitrary strings
//   RX (write)   — client → ESP32: commands / console input
//
// Использует NimBLE-Arduino вместо стандартного Bluedroid:
//   • На 44% меньше памяти (~60-80KB вместо 100-150KB)
//   • Официально совместим с WiFi STA (статус Y у Espressif)
//
// Service UUID : 4fafc201-1fb5-459e-8fcc-c5c9c331914b
// TX char UUID : beb5483e-36e1-4688-b7f5-ea07361b26a8
// RX char UUID : beb5483f-36e1-4688-b7f5-ea07361b26a8
//
// Подключение: nRF Connect (Android/iOS) → найти устройство → подписаться на TX notify

#include <Arduino.h>
#include <NimBLEDevice.h>

#define BLE_SERVICE_UUID  "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHAR_TX_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_RX_UUID  "beb5483f-36e1-4688-b7f5-ea07361b26a8"

#define BLE_LOG_LINES  8    // ring buffer depth for received messages
#define BLE_LOG_LEN   128   // max chars per message

class BLEManager : public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks {
public:
    // Start GATT server and advertising. Returns true on success.
    bool begin(const char* deviceName);

    // Stop BLE stack entirely
    void stop();

    // Call in loop() — restarts advertising after client disconnects
    void tick();

    bool isEnabled()   const { return _started; }
    bool isConnected() const { return _connected; }

    // Push JSON/string to connected client via TX notify
    void notify(const String& payload);

    String connectedClientMAC() const { return _peerMAC; }

    // ----- RX log (messages received from BLE clients) -----
    int    logCount()    const { return _logCount; }
    String logLine(int i) const;
    void   clearLog();

    // ----- NimBLEServerCallbacks -----
    void onConnect(NimBLEServer* pServer) override;
    void onDisconnect(NimBLEServer* pServer) override;

    // ----- NimBLECharacteristicCallbacks -----
    void onWrite(NimBLECharacteristic* pCharacteristic) override;

private:
    bool                   _started    = false;
    bool                   _connected  = false;
    bool                   _doRestart  = false;  // set true on disconnect

    NimBLEServer*         _server  = nullptr;
    NimBLECharacteristic* _txChar  = nullptr;
    NimBLECharacteristic* _rxChar  = nullptr;

    char _log[BLE_LOG_LINES][BLE_LOG_LEN];
    int  _logHead  = 0;
    int  _logCount = 0;

    void addLog(const char* msg);
    String _peerMAC  = "";
};
