#include "can_manager.h"

#if SOC_TWAI_SUPPORTED

bool CANManager::_begin(uint8_t txPin, uint8_t rxPin, uint32_t bitrate) {
    this->txPin = txPin;
    this->rxPin = rxPin;

    // Select timing config based on bitrate
    twai_timing_config_t tCfg;
    switch (bitrate) {
        case 1000000: tCfg = TWAI_TIMING_CONFIG_1MBITS();   break;
        case 800000:  tCfg = TWAI_TIMING_CONFIG_800KBITS();  break;
        case 500000:  tCfg = TWAI_TIMING_CONFIG_500KBITS();  break;
        case 250000:  tCfg = TWAI_TIMING_CONFIG_250KBITS();  break;
        case 125000:  tCfg = TWAI_TIMING_CONFIG_125KBITS();  break;
        case 100000:  tCfg = TWAI_TIMING_CONFIG_100KBITS();  break;
        default:      tCfg = TWAI_TIMING_CONFIG_500KBITS();  break;
    }

    twai_general_config_t gCfg = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)txPin, (gpio_num_t)rxPin, TWAI_MODE_NORMAL);
    gCfg.rx_queue_len = 32;

    twai_filter_config_t fCfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&gCfg, &tCfg, &fCfg) != ESP_OK) {
        Serial.println(F("[CAN] Driver install failed"));
        return false;
    }
    if (twai_start() != ESP_OK) {
        Serial.println(F("[CAN] Start failed"));
        twai_driver_uninstall();
        return false;
    }

    _running = true;
    Serial.printf("[CAN] Started TX=%d RX=%d @%ukbps\n", txPin, rxPin, (unsigned)(bitrate/1000));
    return true;
}

void CANManager::_tick() {
    if (!_running) return;
    twai_message_t msg;
    for (int i = 0; i < 32; i++) {
        if (twai_receive(&msg, 0) != ESP_OK) break;
        if (msg.rtr) continue;

        int slot = findSlot(msg.identifier);
        if (slot < 0) slot = freeSlot();
        if (slot < 0) slot = 0;

        _cache[slot].id    = msg.identifier;
        _cache[slot].dlc   = msg.data_length_code;
        _cache[slot].ts    = millis();
        _cache[slot].valid = true;
        memcpy(_cache[slot].data, msg.data, msg.data_length_code);
    }
}

void CANManager::_end() {
    if (!_running) return;
    twai_stop();
    twai_driver_uninstall();
    _running = false;
}

bool CANManager::_getFrame(uint32_t id, CanFrame& out) const {
    int slot = findSlot(id);
    if (slot < 0) return false;
    out = _cache[slot];
    return true;
}

bool CANManager::_sendFrame(uint32_t id, const uint8_t* data, uint8_t dlc) {
    if (!_running || dlc > 8) return false;
    twai_message_t msg = {};
    msg.identifier = id;
    msg.data_length_code = dlc;
    msg.extd = (id > 0x7FF) ? 1 : 0;
    memcpy(msg.data, data, dlc);
    return twai_transmit(&msg, pdMS_TO_TICKS(10)) == ESP_OK;
}

int CANManager::findSlot(uint32_t id) const {
    for (int i = 0; i < CAN_RX_CACHE_SIZE; i++)
        if (_cache[i].valid && _cache[i].id == id) return i;
    return -1;
}

int CANManager::freeSlot() const {
    for (int i = 0; i < CAN_RX_CACHE_SIZE; i++)
        if (!_cache[i].valid) return i;
    return -1;
}

#endif // SOC_TWAI_SUPPORTED
