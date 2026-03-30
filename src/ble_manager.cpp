#include "ble_manager.h"

// ================================================================
//  BLE Manager — GATT server for ESP-HUB (NimBLE стек)
//  NimBLE использует ~60-80KB RAM вместо 100-150KB у Bluedroid
//  TX characteristic (notify): ESP32 → phone/PC
//  RX characteristic (write):  phone/PC → ESP32
// ================================================================

bool BLEManager::begin(const char* deviceName) {
    if (_started) return true;  // already running

    NimBLEDevice::init(deviceName);
    // Установить мощность передачи (опционально, меньше = экономия энергии)
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // максимальная дальность

    _server = NimBLEDevice::createServer();
    if (!_server) {
        Serial.println(F("[BLE] ERROR: Could not create NimBLE server"));
        return false;
    }
    _server->setCallbacks(this);
    // Автоматически перезапускать рекламу после отключения клиента
    _server->advertiseOnDisconnect(true);

    // Создать GATT сервис
    NimBLEService* svc = _server->createService(BLE_SERVICE_UUID);
    if (!svc) {
        Serial.println(F("[BLE] ERROR: Could not create NimBLE service"));
        return false;
    }

    // TX характеристика — ESP32 отправляет данные клиенту (notify)
    // NimBLE автоматически добавляет CCCD дескриптор — BLE2902 не нужен
    _txChar = svc->createCharacteristic(
        BLE_CHAR_TX_UUID,
        NIMBLE_PROPERTY::READ   |
        NIMBLE_PROPERTY::NOTIFY
    );
    if (!_txChar) {
        Serial.println(F("[BLE] ERROR: Could not create TX characteristic"));
        return false;
    }
    _txChar->setValue("{}");

    // RX характеристика — клиент пишет команды в ESP32
    _rxChar = svc->createCharacteristic(
        BLE_CHAR_RX_UUID,
        NIMBLE_PROPERTY::WRITE   |
        NIMBLE_PROPERTY::WRITE_NR  // без подтверждения (быстрее)
    );
    if (!_rxChar) {
        Serial.println(F("[BLE] ERROR: Could not create RX characteristic"));
        return false;
    }
    _rxChar->setCallbacks(this);
    _rxChar->setValue("");

    svc->start();

    // Запуск рекламы с UUID сервиса, чтобы клиенты могли найти устройство
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (adv) {
        adv->addServiceUUID(BLE_SERVICE_UUID);
        adv->setScanResponse(true);
        adv->setMinPreferred(0x06);  // совместимость с iPhone
        adv->setMinPreferred(0x12);
        adv->start();
    }

    _started = true;
    Serial.printf("[BLE] Started (NimBLE). Device: '%s'  Free heap: %d\n",
                  deviceName, ESP.getFreeHeap());
    Serial.printf("[BLE] Service UUID : %s\n", BLE_SERVICE_UUID);
    Serial.printf("[BLE] TX char UUID : %s\n", BLE_CHAR_TX_UUID);
    Serial.printf("[BLE] RX char UUID : %s\n", BLE_CHAR_RX_UUID);
    return true;
}

void BLEManager::stop() {
    if (!_started) return;
    NimBLEDevice::stopAdvertising();
    NimBLEDevice::deinit(false);  // false = сохранить данные связи
    _started   = false;
    _connected = false;
    _server    = nullptr;
    _txChar    = nullptr;
    _rxChar    = nullptr;
    Serial.println(F("[BLE] Stopped (NimBLE)"));
}

void BLEManager::tick() {
    if (!_started) return;
    // advertiseOnDisconnect(true) в begin() уже обрабатывает автоперезапуск
    // _doRestart оставлен для совместимости
    if (_doRestart) {
        _doRestart = false;
        // Реклама перезапускается автоматически благодаря advertiseOnDisconnect
        Serial.println(F("[BLE] Client disconnected — advertising"));
    }
}

// Отправить payload подключённому клиенту через TX notify
void BLEManager::notify(const String& payload) {
    if (!_started || !_connected || !_txChar) return;
    _txChar->setValue(payload.c_str());
    _txChar->notify();
}

// ================================================================
//  NimBLEServerCallbacks
// ================================================================

void BLEManager::onConnect(NimBLEServer* /*pServer*/) {
    _connected = true;
    _doRestart = false;
    Serial.println(F("[BLE] Клиент подключился"));
}

void BLEManager::onDisconnect(NimBLEServer* /*pServer*/) {
    _connected = false;
    _doRestart = true;
    // advertiseOnDisconnect(true) автоматически перезапускает рекламу
    Serial.println(F("[BLE] Клиент отключился, ожидаю нового..."));
}

// ================================================================
//  NimBLECharacteristicCallbacks — клиент записывает в RX характеристику
// ================================================================

void BLEManager::onWrite(NimBLECharacteristic* pCharacteristic) {
    // NimBLE getValue() возвращает NimBLEAttValue, доступно через .c_str()
    std::string raw = pCharacteristic->getValue();
    String val = String(raw.c_str());
    val.trim();
    if (val.length() == 0) return;
    addLog(val.c_str());
    Serial.printf("[BLE] RX: %s\n", val.c_str());
}

// ================================================================
//  RX log ring buffer
// ================================================================

void BLEManager::addLog(const char* msg) {
    strlcpy(_log[_logHead], msg, BLE_LOG_LEN);
    _logHead = (_logHead + 1) % BLE_LOG_LINES;
    if (_logCount < BLE_LOG_LINES) _logCount++;
}

String BLEManager::logLine(int i) const {
    if (i < 0 || i >= _logCount) return "";
    // Return oldest-first
    int idx = (_logHead - _logCount + i + BLE_LOG_LINES * 2) % BLE_LOG_LINES;
    return String(_log[idx]);
}

void BLEManager::clearLog() {
    _logHead  = 0;
    _logCount = 0;
}
