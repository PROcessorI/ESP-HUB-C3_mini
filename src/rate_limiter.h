#pragma once
// ============== Rate Limiter ==============
// Sliding-window hourly / daily request counter for the AI agent.
// Persists daily count to /ratelimit.json on LittleFS.

#include <Arduino.h>

class RateLimiter {
public:
    // Call once after config + LittleFS are ready
    void begin(bool enabled, int maxPerHour, int maxPerDay);

    // Returns true if request is allowed.
    // On denial, writes human-readable reason to reason[reasonLen].
    bool check(char* reason, size_t reasonLen);

    // Record that a request was made (call after successful LLM call)
    void recordRequest();

    // Manual reset
    void reset();

    // Status accessors
    bool isEnabled()          const { return _enabled; }
    int  requestsThisHour()   const { return _reqHour; }
    int  requestsToday()      const { return _reqDay; }
    int  maxPerHour()         const { return _maxPerHour; }
    int  maxPerDay()          const { return _maxPerDay; }

private:
    bool _enabled    = false;
    int  _maxPerHour = 20;
    int  _maxPerDay  = 100;
    int  _reqHour    = 0;
    int  _reqDay     = 0;
    int  _lastHour   = -1;
    int  _lastYday   = -1;

    void updateWindow();
    void persist();
    void load();
};

extern RateLimiter rateLimiter;
