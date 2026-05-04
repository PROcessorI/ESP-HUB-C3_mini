#pragma once
#include <Arduino.h>

class GpioScheduler {
public:
    void tick() {}
    void reload() {}
    uint32_t timeToNext(int i) { return 0; }
};

class RateLimiter {
public:
    RateLimiter() {}
    bool request() { return true; }
    void begin(int maxPerHour, int maxPerDay) {}
    int requestsThisHour() { return 0; }
    int requestsToday() { return 0; }
};

extern GpioScheduler gpioSched;
extern RateLimiter rateLimiter;