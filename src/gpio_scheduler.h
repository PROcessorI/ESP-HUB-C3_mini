#pragma once
// ============== GPIO Scheduler ==============
// Executes timed actions on GPIO pins (HIGH/LOW/TOGGLE/PULSE)
// Configured via web UI at /sensors (GPIO Timer card)

#include <Arduino.h>
#include "config.h"

class GpioScheduler {
public:
    void begin(HubConfig* cfg);     // call once in setup()
    void tick();                    // call every loop()
    void reload();                  // reset all timers after config save

    // Milliseconds until next trigger for slot i (0 if disabled/immediate)
    uint32_t timeToNext(int i) const;

private:
    HubConfig*  _cfg              = nullptr;
    uint32_t    _lastTick[MAX_GPIO_TIMERS] = {0};
    uint32_t    _pulseEnd[MAX_GPIO_TIMERS] = {0};
    bool        _pulseActive[MAX_GPIO_TIMERS] = {false};
    bool        _pinInit[MAX_GPIO_TIMERS]     = {false};
};
