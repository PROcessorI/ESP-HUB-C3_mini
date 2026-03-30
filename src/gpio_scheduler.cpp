#include "gpio_scheduler.h"

void GpioScheduler::begin(HubConfig* cfg) {
    _cfg = cfg;
    uint32_t now = millis();
    for (int i = 0; i < MAX_GPIO_TIMERS; i++) {
        _lastTick[i]   = now;
        _pulseActive[i] = false;
        _pinInit[i]     = false;
    }
}

void GpioScheduler::reload() {
    uint32_t now = millis();
    for (int i = 0; i < MAX_GPIO_TIMERS; i++) {
        _lastTick[i]    = now;
        _pulseActive[i] = false;
        _pinInit[i]     = false;
    }
    Serial.println(F("[SCHED] Timers reloaded"));
}

uint32_t GpioScheduler::timeToNext(int i) const {
    if (!_cfg || i < 0 || i >= MAX_GPIO_TIMERS) return 0;
    const GpioTimerConfig& t = _cfg->gpio_timers[i];
    if (!t.enabled) return 0;
    uint32_t interval = ((uint32_t)t.hours * 3600 +
                         (uint32_t)t.minutes * 60 +
                         (uint32_t)t.seconds) * 1000UL;
    if (interval == 0) return 0;
    uint32_t elapsed = millis() - _lastTick[i];
    return (elapsed >= interval) ? 0 : (interval - elapsed);
}

void GpioScheduler::tick() {
    if (!_cfg) return;
    uint32_t now = millis();

    for (int i = 0; i < MAX_GPIO_TIMERS; i++) {
        GpioTimerConfig& t = _cfg->gpio_timers[i];
        if (!t.enabled) continue;

        uint32_t interval = ((uint32_t)t.hours * 3600 +
                             (uint32_t)t.minutes * 60 +
                             (uint32_t)t.seconds) * 1000UL;
        if (interval == 0) interval = 1000;  // guard: min 1 s

        // --- End of pulse ---
        if (_pulseActive[i] && now >= _pulseEnd[i]) {
            _pulseActive[i] = false;
            // restore opposite of pulse start, taking inversion into account
            if (t.action == TIMER_PULSE_HIGH) {
                bool logical = false; // pulse HIGH -> end = LOW
                digitalWrite(t.pin, t.active_low ? (logical ? LOW : HIGH) : (logical ? HIGH : LOW));
            } else if (t.action == TIMER_PULSE_LOW) {
                bool logical = true; // pulse LOW -> end = HIGH
                digitalWrite(t.pin, t.active_low ? (logical ? LOW : HIGH) : (logical ? HIGH : LOW));
            }
            Serial.printf("[SCHED] Timer %d (%s) pin%d pulse end\n",
                          i, t.label, t.pin);
        }

        // --- Interval elapsed ---
        if (now - _lastTick[i] < interval) continue;
        _lastTick[i] = now;

        // Ensure pin is output on first trigger
        if (!_pinInit[i]) {
            pinMode(t.pin, OUTPUT);
            _pinInit[i] = true;
        }

        switch (t.action) {
            case TIMER_HIGH: {
                bool logical = true;
                digitalWrite(t.pin, t.active_low ? (logical ? LOW : HIGH) : (logical ? HIGH : LOW));
                Serial.printf("[SCHED] Timer %d (%s) pin%d → HIGH\n", i, t.label, t.pin);
            } break;

            case TIMER_LOW: {
                bool logical = false;
                digitalWrite(t.pin, t.active_low ? (logical ? LOW : HIGH) : (logical ? HIGH : LOW));
                Serial.printf("[SCHED] Timer %d (%s) pin%d → LOW\n", i, t.label, t.pin);
            } break;

            case TIMER_TOGGLE: {
                // compute logical level and toggle it
                int phys = digitalRead(t.pin);
                bool logical = t.active_low ? (phys == HIGH ? false : true) : (phys == HIGH);
                bool newLogical = !logical;
                digitalWrite(t.pin, t.active_low ? (newLogical ? LOW : HIGH) : (newLogical ? HIGH : LOW));
                Serial.printf("[SCHED] Timer %d (%s) pin%d → TOGGLE → %d\n", i, t.label, t.pin, digitalRead(t.pin));
            } break;

            case TIMER_PULSE_HIGH:
                if (!_pulseActive[i]) {
                    bool logical = true;
                    digitalWrite(t.pin, t.active_low ? (logical ? LOW : HIGH) : (logical ? HIGH : LOW));
                    _pulseActive[i] = true;
                    _pulseEnd[i]    = now + t.duration_ms;
                    Serial.printf("[SCHED] Timer %d (%s) pin%d PULSE_HIGH %dms\n", i, t.label, t.pin, t.duration_ms);
                }
                break;

            case TIMER_PULSE_LOW:
                if (!_pulseActive[i]) {
                    bool logical = false;
                    digitalWrite(t.pin, t.active_low ? (logical ? LOW : HIGH) : (logical ? HIGH : LOW));
                    _pulseActive[i] = true;
                    _pulseEnd[i]    = now + t.duration_ms;
                    Serial.printf("[SCHED] Timer %d (%s) pin%d PULSE_LOW %dms\n", i, t.label, t.pin, t.duration_ms);
                }
                break;

            default: break;
        }
    }
}
