// ============== AI Agent for ESP-HUB ==============
#include "ai_agent.h"
#include "cron_manager.h"
#include "rate_limiter.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <driver/gpio.h>
#include <esp_system.h>
#include <rom/rtc.h>
#include <WiFi.h>
#include <Wire.h>

AiAgent aiAgent;

// ============================================================================
// Provider helpers
// ============================================================================

const char* AiAgent::llmUrl() const {
    if (_cfg->cfg.ai_api_url[0]) return _cfg->cfg.ai_api_url;
    switch (_cfg->cfg.ai_provider) {
        case AI_PROV_OLLAMA:      return "http://127.0.0.1:11434/v1/chat/completions";
        case AI_PROV_OPENAI:      return "https://api.openai.com/v1/chat/completions";
        case AI_PROV_OPENROUTER:  return "https://openrouter.ai/api/v1/chat/completions";
        case AI_PROV_ANTHROPIC:   return "https://api.anthropic.com/v1/messages";
        default:                  return "http://192.168.1.125:1234/v1/chat/completions"; // LM Studio
    }
}

const char* AiAgent::llmModel() const {
    if (_cfg->cfg.ai_model[0]) return _cfg->cfg.ai_model;
    switch (_cfg->cfg.ai_provider) {
        case AI_PROV_OLLAMA:      return "qwen3:8b";
        case AI_PROV_OPENAI:      return "gpt-4o-mini";
        case AI_PROV_OPENROUTER:  return "qwen/qwen3-coder:free";
        case AI_PROV_ANTHROPIC:   return "claude-3-5-haiku-20241022";
        default:                  return "qwen/qwen3-vl-8b"; // LM Studio
    }
}

bool AiAgent::llmNeedsKey()    const { return _cfg->cfg.ai_provider >= AI_PROV_OPENAI; }
bool AiAgent::llmIsAnthropic() const { return _cfg->cfg.ai_provider == AI_PROV_ANTHROPIC; }
bool AiAgent::llmIsLocal()     const { return _cfg->cfg.ai_provider <= AI_PROV_OLLAMA; }
bool AiAgent::llmIsLMStudio()  const { return _cfg->cfg.ai_provider == AI_PROV_LM_STUDIO; }

const char* AiAgent::buildSysPrompt(char* buf, size_t len) const {
    // Priority 1: config system prompt override
    if (_cfg->cfg.ai_sys_prompt[0]) {
        strlcpy(buf, _cfg->cfg.ai_sys_prompt, len);
        return buf;
    }
    // Note: AI memory (persona) disabled to prevent /ai_mem.json filesystem errors
    // Default built-in prompt
    snprintf(buf, len,
        "You are ESP-HUB AI, an intelligent assistant running on an ESP32 microcontroller. "
        "Device: %s. "
        "You have tools for complete control: GPIO pins, sensors, 4-channel LED fixture "
        "(R/FR/B/W channels 0-200), MQTT publishing, persistent memory, system diagnostics. "
        "Be concise. Plain text only — no markdown, no code fences, no bullet lists. "
        "Use tools immediately without asking permission. "
        "Respond in Russian by default unless the user asks for another language.",
        _cfg->cfg.device_name);
    return buf;
}

// ============================================================================
// Lifecycle
// ============================================================================

void AiAgent::begin(ConfigManager* cfg, SensorManager* sensors,
                    FixtureManager* fixture, GpioScheduler* gpio,
                    MQTTClient* mqtt) {
    _cfg = cfg; _sensors = sensors; _fixture = fixture;
    _gpio = gpio; _mqtt = mqtt;

    _mutex    = xSemaphoreCreateMutex();
    _work_sem = xSemaphoreCreateBinary();
    memset(_last_resp, 0, sizeof(_last_resp));
    for (int i = 0; i < AI_MAX_HIST; i++) _hist[i] = AiHistMsg{};

    xTaskCreatePinnedToCore(s_taskFn, "ai_agent", 32768, this, 5, &_task, 1);
    Serial.println(F("[AI] Agent task started (core 1)"));
}

void AiAgent::s_taskFn(void* pv) { ((AiAgent*)pv)->taskBody(); }

void AiAgent::taskBody() {
    for (;;) {
        // Wake on signal or Telegram poll interval
        xSemaphoreTake(_work_sem, pdMS_TO_TICKS(AI_TG_POLL_MS));

        if (!_cfg || !_cfg->cfg.ai_enabled) continue;

        // Poll Telegram if enabled, token is configured, and WiFi is connected
        if (_cfg->cfg.ai_tg_enabled && _cfg->cfg.ai_tg_token[0] != '\0' && WiFi.isConnected()) {
            pollTelegram();
        }

        // Drain the pending message slot
        bool has_work = false;
        char msg_buf[AI_PENDING_LEN];
        long long chat_id = 0;
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (_has_pending) {
                strlcpy(msg_buf, _pending, sizeof(msg_buf));
                chat_id     = _pending_chat_id;
                _has_pending = false;
                has_work     = true;
            }
            xSemaphoreGive(_mutex);
        }
        if (has_work) processMsg(msg_buf, chat_id);
    }
}

void AiAgent::tick() {
    // Periodically wake task for Telegram polling
    static unsigned long last_ms = 0;
    if (millis() - last_ms >= AI_TG_POLL_MS) {
        last_ms = millis();
        if (_work_sem) xSemaphoreGive(_work_sem);
    }
}

bool AiAgent::submitMessage(const char* text, long long tg_chat_id) {
    if (!_cfg || !_cfg->cfg.ai_enabled) return false;
    if (_processing) return false;
    if (!text || !text[0]) return false;

    bool ok = false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        strlcpy(_pending, text, sizeof(_pending));
        _pending_chat_id = tg_chat_id;
        _has_pending     = true;
        _processing      = true; // set before waking task
        ok = true;
        xSemaphoreGive(_mutex);
    }
    if (ok && _work_sem) xSemaphoreGive(_work_sem);
    return ok;
}

void AiAgent::clearHistory() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(300)) == pdTRUE) {
        _hist_len = 0;
        for (int i = 0; i < AI_MAX_HIST; i++) _hist[i] = AiHistMsg{};
        xSemaphoreGive(_mutex);
    }
}

// ============================================================================
// History management
// ============================================================================

void AiAgent::histAdd(const char* role, const char* content,
                      bool is_tc, bool is_tr,
                      const char* tc_id, const char* tc_name) {
    // Trim oldest when full
    if (_hist_len >= AI_MAX_HIST) {
        memmove(_hist, _hist + 1, (AI_MAX_HIST - 1) * sizeof(AiHistMsg));
        _hist_len--;
    }
    AiHistMsg& m = _hist[_hist_len++];
    m = AiHistMsg{};
    strlcpy(m.role,    role,    sizeof(m.role));
    strlcpy(m.content, content, sizeof(m.content));
    m.is_tool_call   = is_tc;
    m.is_tool_result = is_tr;
    if (tc_id)   strlcpy(m.tool_id,   tc_id,   sizeof(m.tool_id));
    if (tc_name) strlcpy(m.tool_name, tc_name, sizeof(m.tool_name));
}

// ============================================================================
// Tool JSON schemas — shared helper
// ============================================================================

void AiAgent::addOneTool(JsonArray& arr, bool anthropic,
                         const char* name, const char* desc,
                         const char* params_json) {
    if (anthropic) {
        JsonObject t = arr.add<JsonObject>();
        t["name"] = name;
        t["description"] = desc;
        JsonDocument pd;
        deserializeJson(pd, params_json);
        t["input_schema"] = pd;
    } else {
        JsonObject t  = arr.add<JsonObject>();
        t["type"] = "function";
        JsonObject fn = t["function"].to<JsonObject>();
        fn["name"] = name;
        fn["description"] = desc;
        JsonDocument pd;
        deserializeJson(pd, params_json);
        fn["parameters"] = pd;
    }
}

#define ADDTOOL(arr, anth, n, d, p)  addOneTool(arr, anth, n, d, p)

// Forward declarations for free functions defined in this TU
static void addToolsCommon(AiAgent* self, JsonArray& arr, bool anth);
// addToolsImpl is a friend (declared in ai_agent.h), must not be static
void addToolsImpl(AiAgent* self, JsonArray& arr, bool anth);

void addToolsImpl(AiAgent* self, JsonArray& arr, bool anth) {
    static const struct { const char* n; const char* d; const char* p; } T[] = {
        {"gpio_write",   "Set GPIO pin HIGH(1) or LOW(0). Controls LEDs, relays.",
         "{\"type\":\"object\",\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"GPIO pin\"},\"state\":{\"type\":\"integer\",\"description\":\"0=LOW 1=HIGH\"}},\"required\":[\"pin\",\"state\"]}"},
        {"gpio_read",    "Read GPIO pin digital state (HIGH/LOW).",
         "{\"type\":\"object\",\"properties\":{\"pin\":{\"type\":\"integer\"}},\"required\":[\"pin\"]}"},
        {"gpio_read_all","Read all configured GPIO timer pin states.",
         "{\"type\":\"object\",\"properties\":{}}"},
        {"sensors_read", "Get current readings from all active sensors.",
         "{\"type\":\"object\",\"properties\":{}}"},
        {"light_set",    "Set LED fixture channels brightness 0-200 (200=100%). Channels: red, far_red, blue, white.",
         "{\"type\":\"object\",\"properties\":{\"red\":{\"type\":\"integer\",\"description\":\"Red 0-200\"},\"far_red\":{\"type\":\"integer\",\"description\":\"Far-Red 0-200\"},\"blue\":{\"type\":\"integer\",\"description\":\"Blue 0-200\"},\"white\":{\"type\":\"integer\",\"description\":\"White 0-200\"}},\"required\":[\"red\",\"far_red\",\"blue\",\"white\"]}"},
        {"light_preset", "Apply named light preset: off, grow, full, red, blue, white.",
         "{\"type\":\"object\",\"properties\":{\"preset\":{\"type\":\"string\",\"enum\":[\"off\",\"grow\",\"full\",\"red\",\"blue\",\"white\"]}},\"required\":[\"preset\"]}"},
        {"light_status", "Get current LED fixture channel values and status.",
         "{\"type\":\"object\",\"properties\":{}}"},
        {"system_info",  "Get ESP32 system info: free heap, uptime, WiFi, CPU freq.",
         "{\"type\":\"object\",\"properties\":{}}"},
        {"mqtt_publish", "Publish a message to the configured MQTT topic.",
         "{\"type\":\"object\",\"properties\":{\"message\":{\"type\":\"string\",\"description\":\"Payload to publish\"}},\"required\":[\"message\"]}"},
        {"memory_set",   "Store a key-value in persistent AI memory (survives reboot). Key max 30 chars, value max 200 chars.",
         "{\"type\":\"object\",\"properties\":{\"key\":{\"type\":\"string\"},\"value\":{\"type\":\"string\"}},\"required\":[\"key\",\"value\"]}"},
        {"memory_get",   "Retrieve a value from persistent AI memory by key.",
         "{\"type\":\"object\",\"properties\":{\"key\":{\"type\":\"string\"}},\"required\":[\"key\"]}"},
        {"memory_list",  "List all keys in persistent AI memory.",
         "{\"type\":\"object\",\"properties\":{}}"},
        {"config_get",   "Get ESP-HUB configuration: device name, WiFi, MQTT, sensor count, fixture status.",
         "{\"type\":\"object\",\"properties\":{}}"},
        {"esp_restart",  "Restart the ESP32. Use only when explicitly requested.",
         "{\"type\":\"object\",\"properties\":{}}"},
        {"memory_delete","Delete a key from persistent AI memory.",
         "{\"type\":\"object\",\"properties\":{\"key\":{\"type\":\"string\",\"description\":\"Key to delete\"}},\"required\":[\"key\"]}"},
        {"get_time",     "Get current device date and time (requires NTP sync).",
         "{\"type\":\"object\",\"properties\":{}}"},
        {"get_diagnostics","Get detailed diagnostics: heap, uptime, WiFi RSSI, reboot reason.",
         "{\"type\":\"object\",\"properties\":{}}"},
        {"delay",        "Wait for a specified number of milliseconds (max 10000).",
         "{\"type\":\"object\",\"properties\":{\"ms\":{\"type\":\"integer\",\"description\":\"Milliseconds to wait (1-10000)\"}},\"required\":[\"ms\"]}"},
        {"persona_set",  "Set AI persona/system prompt override stored in persistent memory.",
         "{\"type\":\"object\",\"properties\":{\"persona\":{\"type\":\"string\",\"description\":\"Custom persona text\"}},\"required\":[\"persona\"]}"},
        {"persona_get",  "Get current AI persona override (from persistent memory).",
         "{\"type\":\"object\",\"properties\":{}}"},
        {"persona_reset","Reset AI persona to default.",
         "{\"type\":\"object\",\"properties\":{}}"},
        // ── CRON ──────────────────────────────────────────────────────────────
        {"cron_set",     "Add a scheduled CRON task. Types: periodic (every N seconds), daily (at HH:MM), once (after N seconds).",
         "{\"type\":\"object\",\"properties\":{\"type\":{\"type\":\"string\",\"enum\":[\"periodic\",\"daily\",\"once\"],\"description\":\"Task type\"},\"interval_sec\":{\"type\":\"integer\",\"description\":\"Interval in seconds (periodic/once)\"},\"hour\":{\"type\":\"integer\",\"description\":\"Hour 0-23 (daily)\"},\"minute\":{\"type\":\"integer\",\"description\":\"Minute 0-59 (daily)\"},\"action\":{\"type\":\"string\",\"description\":\"Task text sent as AI message when triggered\"}},\"required\":[\"type\",\"action\"]}"},
        {"cron_list",    "List all CRON tasks with their IDs, schedules, and actions.",
         "{\"type\":\"object\",\"properties\":{}}"},
        {"cron_delete",  "Delete a CRON task by ID.",
         "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\",\"description\":\"Task ID\"}},\"required\":[\"id\"]}"},
        {"cron_tz",      "Set timezone for CRON scheduler (POSIX string, e.g. 'MSK-3' or 'UTC0').",
         "{\"type\":\"object\",\"properties\":{\"tz\":{\"type\":\"string\",\"description\":\"POSIX timezone string\"}},\"required\":[\"tz\"]}"},
        // ── Rate Limiter ───────────────────────────────────────────────────────
        {"rl_status",    "Get current AI request rate limiter stats: hourly and daily usage.",
         "{\"type\":\"object\",\"properties\":{}}"},
        {"rl_reset",     "Reset AI request rate limiter counters.",
         "{\"type\":\"object\",\"properties\":{}}"},
        // ── ADC / I2C ──────────────────────────────────────────────────────────
        {"adc_read",     "Read raw ADC value from an ESP32 GPIO pin. Returns 0-4095.",
         "{\"type\":\"object\",\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"ADC-capable GPIO pin\"}},\"required\":[\"pin\"]}"},
        {"i2c_scan",     "Scan I2C bus and return list of found device addresses.",
         "{\"type\":\"object\",\"properties\":{}}"},
        {"adc_smooth",   "Compute moving average of an array of ADC values.",
         "{\"type\":\"object\",\"properties\":{\"values\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"description\":\"Array of ADC readings\"},\"window\":{\"type\":\"integer\",\"description\":\"Window size (2-20)\"}},\"required\":[\"values\"]}"},
        // ── Config write ──────────────────────────────────────────────────────
        {"config_set",   "Set a device configuration parameter and save it. Keys: device_name, ai_sys_prompt, ai_model, ai_max_tokens, ai_ctx_size, cron_tz, mqtt_topic, ap_nat.",
         "{\"type\":\"object\",\"properties\":{\"key\":{\"type\":\"string\",\"description\":\"Config key\"},\"value\":{\"type\":\"string\",\"description\":\"New value\"}},\"required\":[\"key\",\"value\"]}"},
        {"gpio_timer_list","List all configured GPIO interval timers (pin, action, interval, label).",
         "{\"type\":\"object\",\"properties\":{}}"},
        {"gpio_timer_set", "Configure GPIO interval timer slot 0-7. action: 0=HIGH,1=LOW,2=TOGGLE,3=PULSE_HIGH,4=PULSE_LOW.",
         "{\"type\":\"object\",\"properties\":{\"index\":{\"type\":\"integer\",\"description\":\"Slot 0-7\"},\"enabled\":{\"type\":\"boolean\"},\"pin\":{\"type\":\"integer\"},\"action\":{\"type\":\"integer\",\"description\":\"0=HIGH,1=LOW,2=TOGGLE,3=PULSE_HIGH,4=PULSE_LOW\"},\"hours\":{\"type\":\"integer\"},\"minutes\":{\"type\":\"integer\"},\"seconds\":{\"type\":\"integer\"},\"duration_ms\":{\"type\":\"integer\"},\"label\":{\"type\":\"string\"},\"active_low\":{\"type\":\"boolean\"}},\"required\":[\"index\"]}"},
        {"fixture_timer_list","List all configured fixture (LED) interval timers.",
         "{\"type\":\"object\",\"properties\":{}}"},
        {"fixture_timer_set", "Configure fixture LED interval timer slot 0-3. action: 0=OFF,1=GROW,2=FULL,3=RED,4=BLUE,7=CUSTOM,8=PULSE_CUSTOM. run_* = auto-off after N h:m:s.",
         "{\"type\":\"object\",\"properties\":{\"index\":{\"type\":\"integer\",\"description\":\"Slot 0-3\"},\"enabled\":{\"type\":\"boolean\"},\"action\":{\"type\":\"integer\"},\"hours\":{\"type\":\"integer\"},\"minutes\":{\"type\":\"integer\"},\"seconds\":{\"type\":\"integer\"},\"duration_ms\":{\"type\":\"integer\"},\"run_hours\":{\"type\":\"integer\"},\"run_minutes\":{\"type\":\"integer\"},\"run_seconds\":{\"type\":\"integer\"},\"red\":{\"type\":\"integer\"},\"far_red\":{\"type\":\"integer\"},\"blue\":{\"type\":\"integer\"},\"white\":{\"type\":\"integer\"},\"label\":{\"type\":\"string\"}},\"required\":[\"index\"]}"},
        {"fixture_scenario_list","List all time-based fixture (LED) scenarios (activate at clock time).",
         "{\"type\":\"object\",\"properties\":{}}"},
        {"fixture_scenario_set", "Configure fixture scenario slot 0-7: time of day + LED channel values 0-200.",
         "{\"type\":\"object\",\"properties\":{\"index\":{\"type\":\"integer\",\"description\":\"Slot 0-7\"},\"enabled\":{\"type\":\"boolean\"},\"start_hour\":{\"type\":\"integer\"},\"start_minute\":{\"type\":\"integer\"},\"start_second\":{\"type\":\"integer\"},\"red\":{\"type\":\"integer\"},\"far_red\":{\"type\":\"integer\"},\"blue\":{\"type\":\"integer\"},\"white\":{\"type\":\"integer\"}},\"required\":[\"index\"]}"},
    };
    for (size_t i = 0; i < sizeof(T)/sizeof(T[0]); i++) {
        self->addOneTool(arr, anth, T[i].n, T[i].d, T[i].p);
    }
}

// Redirect through static function to avoid C++ member-pointer complexity
static void addToolsCommon(AiAgent* self, JsonArray& arr, bool anth) {
    addToolsImpl(self, arr, anth);
}

// ============================================================================
// Request building
// ============================================================================

String AiAgent::buildReqOpenAI(const char* sys) {
    // Use larger document for history + tools. Allocate enough for Cyrillic text.
    JsonDocument doc;  // ArduinoJson allocates dynamically up to available heap
    doc["model"]       = llmModel();
    doc["max_tokens"]  = _cfg->cfg.ai_max_tokens;
    doc["temperature"] = _cfg->cfg.ai_temperature / 100.0f;
    if (llmIsLocal()) doc["num_ctx"] = _cfg->cfg.ai_ctx_size;

    JsonArray msgs = doc["messages"].to<JsonArray>();
    // System
    { JsonObject m = msgs.add<JsonObject>(); m["role"]="system"; m["content"]=sys; }

    // History
    for (int i = 0; i < _hist_len; i++) {
        AiHistMsg& h = _hist[i];
        if (h.is_tool_call) {
            JsonObject m  = msgs.add<JsonObject>();
            m["role"]     = "assistant";
            m["content"]  = nullptr;
            JsonArray tcs = m["tool_calls"].to<JsonArray>();
            JsonObject tc = tcs.add<JsonObject>();
            tc["id"]      = h.tool_id;
            tc["type"]    = "function";
            JsonObject fn = tc["function"].to<JsonObject>();
            fn["name"]      = h.tool_name;
            fn["arguments"] = h.content; // already a JSON string
        } else if (h.is_tool_result) {
            JsonObject m     = msgs.add<JsonObject>();
            m["role"]        = "tool";
            m["tool_call_id"]= h.tool_id;
            m["content"]     = h.content;
        } else {
            JsonObject m = msgs.add<JsonObject>();
            m["role"]    = h.role;
            m["content"] = h.content;
        }
    }

    JsonArray tools = doc["tools"].to<JsonArray>();
    addToolsCommon(this, tools, false);

    String out; serializeJson(doc, out); return out;
}

String AiAgent::buildReqAnthropic(const char* sys) {
    JsonDocument doc;
    doc["model"]       = llmModel();
    doc["max_tokens"]  = _cfg->cfg.ai_max_tokens;
    doc["temperature"] = _cfg->cfg.ai_temperature / 100.0f;
    doc["system"]      = sys;

    JsonArray msgs = doc["messages"].to<JsonArray>();
    for (int i = 0; i < _hist_len; i++) {
        AiHistMsg& h = _hist[i];
        if (h.is_tool_call) {
            JsonObject m     = msgs.add<JsonObject>();
            m["role"]        = "assistant";
            JsonArray cnt    = m["content"].to<JsonArray>();
            JsonObject tu    = cnt.add<JsonObject>();
            tu["type"]       = "tool_use";
            tu["id"]         = h.tool_id;
            tu["name"]       = h.tool_name;
            JsonDocument inp;
            deserializeJson(inp, h.content);
            tu["input"]      = inp;
        } else if (h.is_tool_result) {
            // Anthropic: tool result goes under role=user
            JsonObject m     = msgs.add<JsonObject>();
            m["role"]        = "user";
            JsonArray cnt    = m["content"].to<JsonArray>();
            JsonObject tr    = cnt.add<JsonObject>();
            tr["type"]       = "tool_result";
            tr["tool_use_id"]= h.tool_id;
            tr["content"]    = h.content;
        } else {
            JsonObject m = msgs.add<JsonObject>();
            m["role"]    = h.role;
            m["content"] = h.content;
        }
    }

    JsonArray tools = doc["tools"].to<JsonArray>();
    addToolsCommon(this, tools, true);

    String out; serializeJson(doc, out); return out;
}

String AiAgent::buildReqLMStudio(const char* sys) {
    // LM Studio API is OpenAI-compatible, use standard message format with function calling
    JsonDocument doc;
    doc["model"]       = llmModel();
    doc["max_tokens"]  = _cfg->cfg.ai_max_tokens;
    doc["temperature"] = _cfg->cfg.ai_temperature / 100.0f;
    if (llmIsLocal()) doc["num_ctx"] = _cfg->cfg.ai_ctx_size;

    JsonArray msgs = doc["messages"].to<JsonArray>();
    // System
    if (sys && sys[0]) {
        JsonObject m = msgs.add<JsonObject>();
        m["role"]="system";
        m["content"]=sys;
    }

    // History with tool calls support
    for (int i = 0; i < _hist_len; i++) {
        AiHistMsg& h = _hist[i];
        if (h.is_tool_call) {
            JsonObject m  = msgs.add<JsonObject>();
            m["role"]     = "assistant";
            m["content"]  = nullptr;
            JsonArray tcs = m["tool_calls"].to<JsonArray>();
            JsonObject tc = tcs.add<JsonObject>();
            tc["id"]      = h.tool_id;
            tc["type"]    = "function";
            JsonObject fn = tc["function"].to<JsonObject>();
            fn["name"]      = h.tool_name;
            fn["arguments"] = h.content; // already a JSON string
        } else if (h.is_tool_result) {
            JsonObject m     = msgs.add<JsonObject>();
            m["role"]        = "tool";
            m["tool_call_id"]= h.tool_id;
            m["content"]     = h.content;
        } else {
            JsonObject m = msgs.add<JsonObject>();
            m["role"]    = h.role;
            m["content"] = h.content;
        }
    }
    
    // Include tools array for function calling support
    JsonArray tools = doc["tools"].to<JsonArray>();
    addToolsCommon(this, tools, false);  // false = OpenAI format (not Anthropic)

    String out; 
    serializeJson(doc, out);
    return out;
}

// ============================================================================
// HTTP → LLM call
// ============================================================================

bool AiAgent::callLlm(const String& body,
                      String& out_text, bool& out_has_tc,
                      char* tc_name, size_t tc_name_len,
                      char* tc_id,   size_t tc_id_len,
                      char* tc_args, size_t tc_args_len) {
    out_text   = "";
    out_has_tc = false;
    tc_name[0] = tc_id[0] = tc_args[0] = '\0';

    const char* url = llmUrl();
    bool is_https   = (strncmp(url, "https://", 8) == 0);
    
    Serial.printf("[AI] URL: %s, размер запроса: %d байт\n", url, body.length());

    HTTPClient       http;
    WiFiClientSecure sec;
    if (is_https) { sec.setInsecure(); http.begin(sec, url); }
    else          { http.begin(url); }

    http.setTimeout(30000);
    http.addHeader("Content-Type", "application/json");
    if (llmIsAnthropic()) {
        http.addHeader("x-api-key", _cfg->cfg.ai_api_key);
        http.addHeader("anthropic-version", "2023-06-01");
    } else if (llmNeedsKey() && _cfg->cfg.ai_api_key[0]) {
        char hdr[264];
        snprintf(hdr, sizeof(hdr), "Bearer %s", _cfg->cfg.ai_api_key);
        http.addHeader("Authorization", hdr);
    }

    int code = http.POST((String&)body);
    if (code != 200) {
        Serial.printf("[AI] HTTP ошибка: %d\n", code);
        String err = http.getString();
        if (err.length() > 0) {
            Serial.printf("[AI] Ошибка ответа: %.200s\n", err.c_str());
        }
        http.end();
        return false;
    }

    String resp = http.getString();
    http.end();
    
    Serial.printf("[AI] Ответ получен: %d байт\n", resp.length());

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, resp);
    if (err != DeserializationError::Ok) {
        Serial.printf("[AI] Ошибка парсинга JSON: %s\n", err.c_str());
        Serial.printf("[AI] Первые 200 символов ответа: %.200s\n", resp.c_str());
        return false;
    }

    if (llmIsAnthropic()) {
        // Anthropic response: {"content":[{"type":"text","text":"..."},{"type":"tool_use",...}]}
        JsonArray content = doc["content"].as<JsonArray>();
        for (JsonObject item : content) {
            const char* type = item["type"] | "";
            if (strcmp(type, "text") == 0) {
                const char* t = item["text"] | "";
                if (t[0]) out_text += t;
            } else if (strcmp(type, "tool_use") == 0) {
                out_has_tc = true;
                strlcpy(tc_name, item["name"] | "", tc_name_len);
                strlcpy(tc_id,   item["id"]   | "", tc_id_len);
                String inp_s; serializeJson(item["input"], inp_s);
                strlcpy(tc_args, inp_s.c_str(), tc_args_len);
            }
        }
    } else {
        // OpenAI-compatible response (OpenAI, OpenRouter, Ollama, LM Studio)
        // Response format: {"choices":[{"message":{"content":"..."}}]}
        if (doc["choices"].is<JsonArray>()) {
            JsonArray choices = doc["choices"].as<JsonArray>();
            if (choices.size() > 0) {
                JsonObject choice = choices[0];
                
                // Get message content
                JsonObject msg = choice["message"].as<JsonObject>();
                const char* c  = msg["content"] | "";
                if (c && c[0]) {
                    out_text = c;
                }
                
                // Check for tool calls (OpenAI function calling format)
                JsonArray tcs  = msg["tool_calls"].as<JsonArray>();
                if (!tcs.isNull() && tcs.size() > 0) {
                    out_has_tc = true;
                    JsonObject tc = tcs[0];
                    JsonObject fn = tc["function"];
                    strlcpy(tc_name, fn["name"]      | "", tc_name_len);
                    strlcpy(tc_id,   tc["id"]         | "", tc_id_len);
                    strlcpy(tc_args, fn["arguments"]  | "", tc_args_len);
                }
            }
        }
    }
    return true;
}

// ============================================================================
// Process message (runs inside the AI FreeRTOS task)
// ============================================================================

void AiAgent::processMsg(const char* text, long long tg_chat_id) {
    Serial.println("\n╔════════════════════════════════════════════════════╗");
    Serial.printf("║  👤 ПОЛЬЗОВАТЕЛЬ: %s\n", text);
    Serial.println("╚════════════════════════════════════════════════════╝\n");

    // Check rate limiter before making LLM request
    char rl_reason[80];
    if (rateLimiter.isEnabled() && !rateLimiter.check(rl_reason, sizeof(rl_reason))) {
        Serial.printf("[AI] Rate limited: %s\n", rl_reason);
        histAdd("user", text);
        histAdd("assistant", rl_reason);
        strlcpy(_last_resp, rl_reason, sizeof(_last_resp));
        _resp_seq++;
        _processing = false;
        if (tg_chat_id != 0 && _cfg->cfg.ai_tg_token[0]) tgSend(_last_resp, tg_chat_id);
        return;
    }

    // Add user turn to history
    histAdd("user", text);

    char sys_buf[1024];
    buildSysPrompt(sys_buf, sizeof(sys_buf));

    char tc_name[40], tc_id[40], tc_args[512];
    char tool_result[AI_TOOL_RES_LEN];
    String resp_text;
    bool  has_tc  = false;
    bool  success = false;

    int tool_rounds = _cfg->cfg.ai_tool_rounds > 0 ? _cfg->cfg.ai_tool_rounds : AI_MAX_TOOL_ROUNDS;
    for (int round = 0; round < tool_rounds; round++) {
        String req;
        if (llmIsAnthropic()) {
            req = buildReqAnthropic(sys_buf);
        } else if (llmIsLMStudio()) {
            req = buildReqLMStudio(sys_buf);
        } else {
            req = buildReqOpenAI(sys_buf);
        }

        Serial.printf("[AI] Раунд %d: отправка запроса к LLM (роль=%d)\n", round + 1, _cfg->cfg.ai_provider);
        if (!callLlm(req, resp_text, has_tc,
                     tc_name, sizeof(tc_name),
                     tc_id,   sizeof(tc_id),
                     tc_args, sizeof(tc_args))) {
            Serial.println("[AI] Ошибка вызова LLM");
            break;
        }

        Serial.printf("[AI] Ответ получен: has_tc=%d, len=%d\n", has_tc ? 1 : 0, resp_text.length());
        if (resp_text.length() > 0) {
            Serial.printf("[AI] Текст ответа: %.100s...\n", resp_text.c_str());
        }

        if (!has_tc) { 
            success = true; 
            rateLimiter.recordRequest();
            Serial.printf("[AI] Нет вызова инструмента, завершение обработки\n");
            break; 
        }

        // Выполнение инструмента
        Serial.printf("[AI] Обнаружен инструмент: '%s' (ID: %s)\n", tc_name, tc_id);
        Serial.printf("[AI] Параметры: %.100s\n", tc_args);
        histAdd("assistant", tc_args, true, false, tc_id, tc_name);
        memset(tool_result, 0, sizeof(tool_result));
        doTool(tc_name, tc_args, tool_result, sizeof(tool_result));
        Serial.printf("\n✅ РЕЗУЛЬТАТ: %s\n\n", tool_result);
        histAdd("tool", tool_result, false, true, tc_id, nullptr);
        resp_text = "";
    }

    const char* final_text = success && resp_text.length() > 0
        ? resp_text.c_str()
        : (success ? "(нет ответа)" : "Ошибка: не удалось получить ответ от LLM.");

    if (success && resp_text.length() > 0) {
        histAdd("assistant", resp_text.c_str());
    }

    strlcpy(_last_resp, final_text, sizeof(_last_resp));
    _resp_seq++;
    _processing = false;
    
    Serial.println("\n╔════════════════════════════════════════════════════╗");
    Serial.printf("║  🤖 ОТВЕТ AI: %s\n", final_text);
    Serial.println("╚════════════════════════════════════════════════════╝\n");

    // Forward to Telegram
    if (tg_chat_id != 0 && _cfg->cfg.ai_tg_token[0]) {
        tgSend(_last_resp, tg_chat_id);
    }
}

// REMOVED: Rest of message processing code for debugging

// ============================================================================
// Tool execution
// ============================================================================

bool AiAgent::doTool(const char* name, const char* args, char* r, size_t l) {
    if (!name) { snprintf(r, l, "Error: null tool name"); return false; }
    
    // Log the command being executed
    Serial.printf("════════════════════════════════════════════════════\n");
    Serial.printf("⚡ КОМАНДА: %s\n", name);
    if (args && args[0]) {
        Serial.printf("📋 ПАРАМЕТРЫ: %s\n", args);
    }
    Serial.printf("════════════════════════════════════════════════════\n");
    
    if (strcmp(name, "gpio_write")    == 0) return toolGpioWrite(args, r, l);
    if (strcmp(name, "gpio_read")     == 0) return toolGpioRead(args, r, l);
    if (strcmp(name, "gpio_read_all") == 0) return toolGpioReadAll(args, r, l);
    if (strcmp(name, "sensors_read")  == 0) return toolSensors(args, r, l);
    if (strcmp(name, "light_set")     == 0) return toolLightSet(args, r, l);
    if (strcmp(name, "light_preset")  == 0) return toolLightPreset(args, r, l);
    if (strcmp(name, "light_status")  == 0) return toolLightStatus(args, r, l);
    if (strcmp(name, "system_info")   == 0) return toolSysInfo(args, r, l);
    if (strcmp(name, "mqtt_publish")  == 0) return toolMqttPub(args, r, l);
    if (strcmp(name, "memory_set")    == 0) return toolMemSet(args, r, l);
    if (strcmp(name, "memory_get")    == 0) return toolMemGet(args, r, l);
    if (strcmp(name, "memory_list")   == 0) return toolMemList(args, r, l);
    if (strcmp(name, "config_get")    == 0) return toolConfigGet(args, r, l);
    if (strcmp(name, "esp_restart")   == 0) return toolRestart(args, r, l);
    if (strcmp(name, "memory_delete") == 0) return toolMemDelete(args, r, l);
    if (strcmp(name, "get_time")      == 0) return toolGetTime(args, r, l);
    if (strcmp(name, "get_diagnostics")== 0) return toolGetDiag(args, r, l);
    if (strcmp(name, "delay")         == 0) return toolDelay(args, r, l);
    if (strcmp(name, "persona_set")   == 0) return toolPersonaSet(args, r, l);
    if (strcmp(name, "persona_get")   == 0) return toolPersonaGet(args, r, l);
    if (strcmp(name, "persona_reset") == 0) return toolPersonaReset(args, r, l);
    if (strcmp(name, "cron_set")      == 0) return toolCronSet(args, r, l);
    if (strcmp(name, "cron_list")     == 0) return toolCronList(args, r, l);
    if (strcmp(name, "cron_delete")   == 0) return toolCronDelete(args, r, l);
    if (strcmp(name, "cron_tz")       == 0) return toolCronTz(args, r, l);
    if (strcmp(name, "rl_status")     == 0) return toolRlStatus(args, r, l);
    if (strcmp(name, "rl_reset")      == 0) return toolRlReset(args, r, l);
    if (strcmp(name, "adc_read")      == 0) return toolAdcRead(args, r, l);
    if (strcmp(name, "i2c_scan")      == 0) return toolI2cScan(args, r, l);
    if (strcmp(name, "adc_smooth")    == 0) return toolAdcSmooth(args, r, l);
    if (strcmp(name, "config_set")           == 0) return toolConfigSet(args, r, l);
    if (strcmp(name, "gpio_timer_list")      == 0) return toolGpioTimerList(args, r, l);
    if (strcmp(name, "gpio_timer_set")       == 0) return toolGpioTimerSet(args, r, l);
    if (strcmp(name, "fixture_timer_list")   == 0) return toolFixtureTimerList(args, r, l);
    if (strcmp(name, "fixture_timer_set")    == 0) return toolFixtureTimerSet(args, r, l);
    if (strcmp(name, "fixture_scenario_list") == 0) return toolFixtureScenarioList(args, r, l);
    if (strcmp(name, "fixture_scenario_set")  == 0) return toolFixtureScenarioSet(args, r, l);
    snprintf(r, l, "Error: unknown tool '%s'", name);
    return false;
}

bool AiAgent::toolGpioWrite(const char* a, char* r, size_t l) {
    JsonDocument doc; deserializeJson(doc, a);
    int pin   = doc["pin"]   | -1;
    int state = doc["state"] | -1;
    if (pin < 0 || state < 0) { snprintf(r, l, "Error: missing pin or state"); return false; }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, state ? HIGH : LOW);
    snprintf(r, l, "GPIO %d set to %s", pin, state ? "HIGH" : "LOW");
    return true;
}

bool AiAgent::toolGpioRead(const char* a, char* r, size_t l) {
    JsonDocument doc; deserializeJson(doc, a);
    int pin = doc["pin"] | -1;
    if (pin < 0) { snprintf(r, l, "Error: missing pin"); return false; }
    int val = digitalRead(pin);
    snprintf(r, l, "GPIO %d = %s", pin, val ? "HIGH" : "LOW");
    return true;
}

bool AiAgent::toolGpioReadAll(const char* a, char* r, size_t l) {
    (void)a;
    char buf[AI_TOOL_RES_LEN];
    buf[0] = '\0';
    char* p = buf;
    size_t rem = sizeof(buf);
    bool any = false;
    if (_cfg) {
        for (int i = 0; i < MAX_GPIO_TIMERS; i++) {
            const GpioTimerConfig& t = _cfg->cfg.gpio_timers[i];
            if (!t.enabled) continue;
            int val = digitalRead(t.pin);
            int wrote = snprintf(p, rem, "GPIO%d=%s ", t.pin, val ? "HIGH" : "LOW");
            if (wrote > 0 && (size_t)wrote < rem) { p += wrote; rem -= wrote; }
            any = true;
        }
    }
    if (!any) strlcpy(buf, "No GPIO timers configured", sizeof(buf));
    strlcpy(r, buf, l);
    return true;
}

bool AiAgent::toolSensors(const char* a, char* r, size_t l) {
    (void)a;
    if (!_sensors) { snprintf(r, l, "Sensor manager not available"); return true; }
    String result;
    result.reserve(256);
    bool any = false;
    for (uint8_t i = 0; i < _sensors->slotCount(); i++) {
        SensorBase* s = _sensors->getSensor(i);
        if (!s || !s->isReady()) continue;
        const SensorConfig& sc = _cfg->cfg.sensors[i];
        for (uint8_t j = 0; j < s->valueCount(); j++) {
            const SensorValue& v = s->getValue(j);
            if (!v.valid) continue;
            const char* lbl = (sc.label[0]) ? sc.label : s->typeName();
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%s.%s=%.2f%s ", lbl, v.name, v.value, v.unit);
            result += tmp;
            any = true;
        }
    }
    if (!any) result = "No sensor data available";
    strlcpy(r, result.c_str(), l);
    return true;
}

bool AiAgent::toolLightSet(const char* a, char* r, size_t l) {
    JsonDocument doc; deserializeJson(doc, a);
    int red = constrain((int)(doc["red"]     | 0), 0, 200);
    int fr  = constrain((int)(doc["far_red"] | 0), 0, 200);
    int bl  = constrain((int)(doc["blue"]    | 0), 0, 200);
    int wh  = constrain((int)(doc["white"]   | 0), 0, 200);
    if (_fixture && _fixture->isEnabled()) {
        _fixture->setChannels(red, fr, bl, wh);
        snprintf(r, l, "Light set R=%d FR=%d B=%d W=%d", red, fr, bl, wh);
    } else {
        snprintf(r, l, "Fixture not enabled. R=%d FR=%d B=%d W=%d (not applied)", red, fr, bl, wh);
    }
    return true;
}

bool AiAgent::toolLightPreset(const char* a, char* r, size_t l) {
    JsonDocument doc; deserializeJson(doc, a);
    const char* preset = doc["preset"] | "";
    if (!preset[0]) { snprintf(r, l, "Error: missing preset"); return false; }
    uint8_t p = 0;
    if      (strcmp(preset, "grow")  == 0) p = 1;
    else if (strcmp(preset, "full")  == 0) p = 2;
    else if (strcmp(preset, "red")   == 0) p = 3;
    else if (strcmp(preset, "blue")  == 0) p = 4;
    else if (strcmp(preset, "white") == 0) p = 5;
    else if (strcmp(preset, "off")   != 0) { snprintf(r, l, "Error: unknown preset '%s'", preset); return false; }
    if (_fixture && _fixture->isEnabled()) {
        _fixture->setPreset(p);
        snprintf(r, l, "Preset '%s' applied", preset);
    } else {
        snprintf(r, l, "Fixture not enabled, preset '%s' not applied", preset);
    }
    return true;
}

bool AiAgent::toolLightStatus(const char* a, char* r, size_t l) {
    (void)a;
    if (!_fixture) { snprintf(r, l, "Fixture not available"); return true; }
    snprintf(r, l, "enabled=%s R=%d FR=%d B=%d W=%d ack=%s",
        _fixture->isEnabled() ? "yes" : "no",
        _fixture->getRed(), _fixture->getFarRed(),
        _fixture->getBlue(), _fixture->getWhite(),
        _fixture->isLastAckOk() ? "ok" : "err");
    return true;
}

bool AiAgent::toolSysInfo(const char* a, char* r, size_t l) {
    (void)a;
    snprintf(r, l,
        "heap_free=%lu heap_min=%lu uptime_ms=%lu cpu_mhz=%u wifi=%s ip=%s",
        (unsigned long)ESP.getFreeHeap(),
        (unsigned long)ESP.getMinFreeHeap(),
        (unsigned long)millis(),
        getCpuFrequencyMhz(),
        WiFi.isConnected() ? "connected" : "disconnected",
        WiFi.localIP().toString().c_str());
    return true;
}

bool AiAgent::toolMqttPub(const char* a, char* r, size_t l) {
    JsonDocument doc; deserializeJson(doc, a);
    const char* msg = doc["message"] | "";
    if (!msg[0]) { snprintf(r, l, "Error: missing message"); return false; }
    if (!_mqtt || !_mqtt->isConnected()) {
        snprintf(r, l, "Error: MQTT not connected"); return false;
    }
    bool ok = _mqtt->publish(msg);
    snprintf(r, l, ok ? "Published to MQTT topic: %s" : "MQTT publish failed: %s", msg);
    return ok;
}

bool AiAgent::toolConfigGet(const char* a, char* r, size_t l) {
    (void)a;
    if (!_cfg) { snprintf(r, l, "Config not available"); return true; }
    int active_sensors = 0;
    for (int i = 0; i < MAX_SENSORS; i++)
        if (_cfg->cfg.sensors[i].enabled) active_sensors++;
    snprintf(r, l,
        "device=%s wifi_ssid=%s wifi=%s mqtt_host=%s mqtt=%s "
        "active_sensors=%d fixture=%s cpu_mhz=%u",
        _cfg->cfg.device_name,
        _cfg->cfg.wifi_ssid,
        WiFi.isConnected() ? "on" : "off",
        _cfg->cfg.mqtt_host,
        (_mqtt && _mqtt->isConnected()) ? "on" : "off",
        active_sensors,
        _cfg->cfg.fixture.enabled ? "on" : "off",
        getCpuFrequencyMhz());
    return true;
}

bool AiAgent::toolRestart(const char* a, char* r, size_t l) {
    (void)a;
    snprintf(r, l, "Restarting ESP32...");
    _processing = false;
    delay(500);
    ESP.restart();
    return true;
}

// ── AI Memory (DISABLED - /ai_mem.json) ──────────────────────────────────────
// Memory functions disabled to prevent filesystem errors
bool AiAgent::aiMemSet(const char* key, const char* val) {
    (void)key; (void)val;
    return false;  // Memory disabled
}

bool AiAgent::aiMemGet(const char* key, char* val, size_t vlen) {
    (void)key;
    if (vlen > 0) val[0] = '\0';
    return false;  // Memory disabled
}

void AiAgent::aiMemList(char* out, size_t olen) {
    strlcpy(out, "(memory disabled)", olen);
}

bool AiAgent::toolMemSet(const char* a, char* r, size_t l) {
    (void)a;
    snprintf(r, l, "Error: memory storage disabled");
    return false;
}

bool AiAgent::toolMemGet(const char* a, char* r, size_t l) {
    (void)a;
    snprintf(r, l, "Error: memory storage disabled");
    return false;
}

bool AiAgent::toolMemList(const char* a, char* r, size_t l) {
    (void)a;
    snprintf(r, l, "Error: memory storage disabled");
    return false;
}

bool AiAgent::toolMemDelete(const char* a, char* r, size_t l) {
    (void)a;
    snprintf(r, l, "Error: memory storage disabled");
    return false;
}

bool AiAgent::toolGetTime(const char* a, char* r, size_t l) {
    (void)a;
    struct tm ti;
    if (!getLocalTime(&ti, 100)) {
        snprintf(r, l, "NTP not synced");
    } else {
        snprintf(r, l, "%04d-%02d-%02d %02d:%02d:%02d",
            ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
            ti.tm_hour, ti.tm_min, ti.tm_sec);
    }
    return true;
}

bool AiAgent::toolGetDiag(const char* a, char* r, size_t l) {
    (void)a;
    uint32_t reason = rtc_get_reset_reason(0);
    snprintf(r, l,
        "heap_free=%lu heap_min=%lu uptime_s=%lu rssi=%d "
        "wifi=%s ip=%s cpu_mhz=%u reset_reason=%u heap_largest=%lu",
        (unsigned long)ESP.getFreeHeap(),
        (unsigned long)ESP.getMinFreeHeap(),
        (unsigned long)(millis() / 1000),
        WiFi.isConnected() ? WiFi.RSSI() : 0,
        WiFi.isConnected() ? "connected" : "disconnected",
        WiFi.localIP().toString().c_str(),
        getCpuFrequencyMhz(),
        (unsigned)reason,
        (unsigned long)ESP.getMaxAllocHeap());
    return true;
}

bool AiAgent::toolDelay(const char* a, char* r, size_t l) {
    JsonDocument doc; deserializeJson(doc, a);
    int ms = constrain((int)(doc["ms"] | 1000), 1, 10000);
    vTaskDelay(pdMS_TO_TICKS(ms));
    snprintf(r, l, "Waited %d ms", ms);
    return true;
}

bool AiAgent::toolPersonaSet(const char* a, char* r, size_t l) {
    (void)a;
    snprintf(r, l, "Error: persona persistence disabled");
    return false;
}

bool AiAgent::toolPersonaGet(const char* a, char* r, size_t l) {
    (void)a;
    strlcpy(r, "(default persona - custom personas disabled)", l);
    return true;
}

bool AiAgent::toolPersonaReset(const char* a, char* r, size_t l) {
    (void)a;
    strlcpy(r, "Persona already at default", l);
    return true;
}

// ============================================================================
// CRON tools
// ============================================================================

bool AiAgent::toolCronSet(const char* a, char* r, size_t l) {
    JsonDocument doc; deserializeJson(doc, a);
    const char* typeStr = doc["type"]   | "periodic";
    uint32_t isec       = doc["interval_sec"] | 60;
    uint8_t  hr         = doc["hour"]   | 0;
    uint8_t  mn         = doc["minute"] | 0;
    const char* action  = doc["action"] | "";
    if (!action[0]) { snprintf(r, l, "Error: action is required"); return false; }

    CronType ct = CRON_PERIODIC;
    if      (strcmp(typeStr, "daily") == 0) ct = CRON_DAILY;
    else if (strcmp(typeStr, "once")  == 0) ct = CRON_ONCE;

    uint32_t p1 = (ct == CRON_DAILY) ? hr : isec;
    uint8_t id = cronMgr.add(ct, p1, mn, action);
    if (id == 0) {
        snprintf(r, l, "Error: CRON list full (max %d)", CRON_MAX_ENTRIES);
    } else {
        snprintf(r, l, "CRON task added with id=%d type=%s action='%s'", id, typeStr, action);
    }
    return id != 0;
}

bool AiAgent::toolCronList(const char* a, char* r, size_t l) {
    (void)a;
    cronMgr.listJson(r, l);
    return true;
}

bool AiAgent::toolCronDelete(const char* a, char* r, size_t l) {
    JsonDocument doc; deserializeJson(doc, a);
    int id = doc["id"] | -1;
    if (id < 0) { snprintf(r, l, "Error: id required"); return false; }
    bool ok = cronMgr.remove((uint8_t)id);
    if (ok) snprintf(r, l, "CRON task %d deleted", id);
    else    snprintf(r, l, "Error: task %d not found", id);
    return ok;
}

bool AiAgent::toolCronTz(const char* a, char* r, size_t l) {
    JsonDocument doc; deserializeJson(doc, a);
    const char* tz = doc["tz"] | "";
    if (!tz[0]) { snprintf(r, l, "Error: tz is required"); return false; }
    cronMgr.setTimezone(tz);
    if (_cfg) {
        strlcpy(_cfg->cfg.cron_tz, tz, sizeof(_cfg->cfg.cron_tz));
        _cfg->save();
    }
    snprintf(r, l, "Timezone set to '%s'", tz);
    return true;
}

// ============================================================================
// Rate Limiter tools
// ============================================================================

bool AiAgent::toolRlStatus(const char* a, char* r, size_t l) {
    (void)a;
    snprintf(r, l, "RateLimit: hour=%u/%u day=%u/%u enabled=%s",
             rateLimiter.requestsThisHour(), rateLimiter.maxPerHour(),
             rateLimiter.requestsToday(),    rateLimiter.maxPerDay(),
             rateLimiter.isEnabled() ? "true" : "false");
    return true;
}

bool AiAgent::toolRlReset(const char* a, char* r, size_t l) {
    (void)a;
    rateLimiter.reset();
    strlcpy(r, "Rate limiter counters reset", l);
    return true;
}

// ============================================================================
// ADC / I2C / signal tools
// ============================================================================

bool AiAgent::toolAdcRead(const char* a, char* r, size_t l) {
    JsonDocument doc; deserializeJson(doc, a);
    int pin = doc["pin"] | -1;
    if (pin < 0) { snprintf(r, l, "Error: pin required"); return false; }
    int raw = analogRead(pin);
    snprintf(r, l, "ADC pin=%d raw=%d voltage_mv=%d", pin, raw, (int)((raw * 3300LL) / 4095));
    return true;
}

bool AiAgent::toolI2cScan(const char* a, char* r, size_t l) {
    (void)a;
    char buf[400]; buf[0]='\0';
    char* p = buf; size_t rem = sizeof(buf);
    int found = 0;
    int n = snprintf(p, rem, "I2C scan: "); p += n; rem -= n;
    Wire.begin();
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            n = snprintf(p, rem, "0x%02X ", addr); p += n; rem -= n;
            found++;
        }
    }
    if (!found) { int k = snprintf(p, rem, "no devices found"); p += k; rem -= k; }
    n = snprintf(p, rem, "(%d devices)", found);
    strlcpy(r, buf, l);
    return true;
}

bool AiAgent::toolAdcSmooth(const char* a, char* r, size_t l) {
    JsonDocument doc; deserializeJson(doc, a);
    JsonArray vals = doc["values"].as<JsonArray>();
    int window = doc["window"] | 5;
    if (window < 1) window = 1;
    if (window > 32) window = 32;
    if (!vals.size()) { snprintf(r, l, "Error: values array required"); return false; }

    // Moving average
    char buf[256]; buf[0]='\0'; char* p=buf; size_t rem=sizeof(buf);
    int n = snprintf(p, rem, "smoothed=["); p+=n; rem-=n;
    double acc=0; int cnt=0;
    for (size_t i=0; i<vals.size(); i++) {
        acc += (double)vals[i];
        cnt++;
        if (cnt >= window) {
            n = snprintf(p, rem, "%.1f,", acc/cnt); p+=n; rem-=n;
            acc=0; cnt=0;
        }
    }
    if (cnt>0) { n = snprintf(p, rem, "%.1f,", acc/cnt); p+=n; rem-=n; }
    if (p>buf && *(p-1)==',') *(p-1)=']'; else { n=snprintf(p,rem,"]"); p+=n; rem-=n; }
    strlcpy(r, buf, l);
    return true;
}

// ============================================================================
// Config write tool
// ============================================================================

bool AiAgent::toolConfigSet(const char* a, char* r, size_t l) {
    if (!_cfg) { snprintf(r, l, "Error: no config"); return false; }
    JsonDocument doc; deserializeJson(doc, a);
    const char* key = doc["key"]   | "";
    const char* val = doc["value"] | "";
    if (!key[0]) { snprintf(r, l, "Error: key required"); return false; }

    bool changed = false;
    if (strcmp(key, "device_name") == 0) {
        strlcpy(_cfg->cfg.device_name, val, sizeof(_cfg->cfg.device_name));
        changed = true;
    } else if (strcmp(key, "ai_sys_prompt") == 0) {
        strlcpy(_cfg->cfg.ai_sys_prompt, val, sizeof(_cfg->cfg.ai_sys_prompt));
        changed = true;
    } else if (strcmp(key, "cron_tz") == 0) {
        strlcpy(_cfg->cfg.cron_tz, val, sizeof(_cfg->cfg.cron_tz));
        cronMgr.setTimezone(val);
        changed = true;
    } else if (strcmp(key, "mqtt_topic") == 0) {
        strlcpy(_cfg->cfg.mqtt_topic, val, sizeof(_cfg->cfg.mqtt_topic));
        changed = true;
    } else if (strcmp(key, "ap_nat") == 0) {
        _cfg->cfg.ap_nat = (strcmp(val,"true")==0 || strcmp(val,"1")==0);
        changed = true;
    } else if (strcmp(key, "ai_model") == 0) {
        strlcpy(_cfg->cfg.ai_model, val, sizeof(_cfg->cfg.ai_model));
        changed = true;
    } else if (strcmp(key, "ai_max_tokens") == 0) {
        _cfg->cfg.ai_max_tokens = (uint16_t)constrain(atoi(val), 64, 8192);
        changed = true;
    } else if (strcmp(key, "ai_ctx_size") == 0) {
        _cfg->cfg.ai_ctx_size = (uint16_t)constrain(atoi(val), 1024, 128000);
        changed = true;
    } else {
        snprintf(r, l, "Error: unknown key '%s'. Keys: device_name, ai_sys_prompt, ai_model, ai_max_tokens, ai_ctx_size, cron_tz, mqtt_topic, ap_nat", key);
        return false;
    }
    if (changed) _cfg->save();
    snprintf(r, l, "Config '%s' = '%s' saved", key, val);
    return true;
}

bool AiAgent::toolGpioTimerList(const char* a, char* r, size_t l) {
    (void)a;
    if (!_cfg) { snprintf(r, l, "Error: no config"); return false; }
    String out;
    for (int i = 0; i < MAX_GPIO_TIMERS; i++) {
        GpioTimerConfig& t = _cfg->cfg.gpio_timers[i];
        if (!t.enabled) continue;
        char buf[96];
        snprintf(buf, sizeof(buf), "[%d] pin=%d act=%d every=%02d:%02d:%02d dur=%dms label=%s\n",
            i, t.pin, (int)t.action, t.hours, t.minutes, t.seconds, t.duration_ms, t.label);
        out += buf;
    }
    if (out.isEmpty()) out = "No GPIO timers enabled";
    strlcpy(r, out.c_str(), l);
    return true;
}

bool AiAgent::toolGpioTimerSet(const char* a, char* r, size_t l) {
    JsonDocument doc; deserializeJson(doc, a);
    int idx = doc["index"] | -1;
    if (idx < 0 || idx >= MAX_GPIO_TIMERS) {
        snprintf(r, l, "Error: index 0-%d required", MAX_GPIO_TIMERS - 1); return false;
    }
    GpioTimerConfig& t = _cfg->cfg.gpio_timers[idx];
    if (doc["enabled"].is<bool>())     t.enabled     = doc["enabled"].as<bool>();
    if (doc["pin"].is<int>())           t.pin         = (uint8_t)doc["pin"].as<int>();
    if (doc["action"].is<int>())        t.action      = (GpioTimerAction)constrain(doc["action"].as<int>(), 0, (int)TIMER_ACTION_COUNT - 1);
    if (doc["hours"].is<int>())         t.hours       = (uint8_t)constrain(doc["hours"].as<int>(), 0, 23);
    if (doc["minutes"].is<int>())       t.minutes     = (uint8_t)constrain(doc["minutes"].as<int>(), 0, 59);
    if (doc["seconds"].is<int>())       t.seconds     = (uint8_t)constrain(doc["seconds"].as<int>(), 0, 59);
    if (doc["duration_ms"].is<int>())   t.duration_ms = (uint16_t)doc["duration_ms"].as<int>();
    if (doc["active_low"].is<bool>())   t.active_low  = doc["active_low"].as<bool>();
    if (doc["label"].is<const char*>()) strlcpy(t.label, doc["label"].as<const char*>(), sizeof(t.label));
    _cfg->save();
    if (_gpio) _gpio->reload();
    snprintf(r, l, "GPIO timer %d updated (pin=%d action=%d enabled=%s)", idx, t.pin, (int)t.action, t.enabled ? "yes" : "no");
    return true;
}

bool AiAgent::toolFixtureTimerList(const char* a, char* r, size_t l) {
    (void)a;
    if (!_cfg) { snprintf(r, l, "Error: no config"); return false; }
    String out;
    for (int i = 0; i < MAX_FIXTURE_TIMERS; i++) {
        FixtureTimerConfig& t = _cfg->cfg.fixture.timers[i];
        if (!t.enabled) continue;
        char buf[112];
        snprintf(buf, sizeof(buf), "[%d] act=%d every=%02d:%02d:%02d run=%02d:%02d:%02d R=%d FR=%d B=%d W=%d label=%s\n",
            i, (int)t.action, t.hours, t.minutes, t.seconds,
            t.run_hours, t.run_minutes, t.run_seconds,
            t.red, t.far_red, t.blue, t.white, t.label);
        out += buf;
    }
    if (out.isEmpty()) out = "No fixture timers enabled";
    strlcpy(r, out.c_str(), l);
    return true;
}

bool AiAgent::toolFixtureTimerSet(const char* a, char* r, size_t l) {
    JsonDocument doc; deserializeJson(doc, a);
    int idx = doc["index"] | -1;
    if (idx < 0 || idx >= MAX_FIXTURE_TIMERS) {
        snprintf(r, l, "Error: index 0-%d required", MAX_FIXTURE_TIMERS - 1); return false;
    }
    FixtureTimerConfig& t = _cfg->cfg.fixture.timers[idx];
    if (doc["enabled"].is<bool>())     t.enabled    = doc["enabled"].as<bool>();
    if (doc["action"].is<int>())        t.action     = (FixtureTimerAction)constrain(doc["action"].as<int>(), 0, (int)FIX_TIMER_ACTION_COUNT - 1);
    if (doc["hours"].is<int>())         t.hours      = (uint8_t)constrain(doc["hours"].as<int>(), 0, 23);
    if (doc["minutes"].is<int>())       t.minutes    = (uint8_t)constrain(doc["minutes"].as<int>(), 0, 59);
    if (doc["seconds"].is<int>())       t.seconds    = (uint8_t)constrain(doc["seconds"].as<int>(), 0, 59);
    if (doc["duration_ms"].is<int>())   t.duration_ms= (uint16_t)doc["duration_ms"].as<int>();
    if (doc["run_hours"].is<int>())     t.run_hours  = (uint8_t)constrain(doc["run_hours"].as<int>(), 0, 23);
    if (doc["run_minutes"].is<int>())   t.run_minutes= (uint8_t)constrain(doc["run_minutes"].as<int>(), 0, 59);
    if (doc["run_seconds"].is<int>())   t.run_seconds= (uint8_t)constrain(doc["run_seconds"].as<int>(), 0, 59);
    if (doc["red"].is<int>())           t.red        = (uint8_t)constrain(doc["red"].as<int>(), 0, 200);
    if (doc["far_red"].is<int>())       t.far_red    = (uint8_t)constrain(doc["far_red"].as<int>(), 0, 200);
    if (doc["blue"].is<int>())          t.blue       = (uint8_t)constrain(doc["blue"].as<int>(), 0, 200);
    if (doc["white"].is<int>())         t.white      = (uint8_t)constrain(doc["white"].as<int>(), 0, 200);
    if (doc["label"].is<const char*>()) strlcpy(t.label, doc["label"].as<const char*>(), sizeof(t.label));
    _cfg->save();
    if (_fixture) _fixture->reloadTimers();
    snprintf(r, l, "Fixture timer %d updated (action=%d enabled=%s)", idx, (int)t.action, t.enabled ? "yes" : "no");
    return true;
}

bool AiAgent::toolFixtureScenarioList(const char* a, char* r, size_t l) {
    (void)a;
    if (!_cfg) { snprintf(r, l, "Error: no config"); return false; }
    String out;
    for (int i = 0; i < MAX_FIXTURE_SCENARIOS; i++) {
        FixtureScenario& s = _cfg->cfg.fixture.scenarios[i];
        if (!s.enabled) continue;
        char buf[80];
        snprintf(buf, sizeof(buf), "[%d] at %02d:%02d:%02d R=%d FR=%d B=%d W=%d\n",
            i, s.start_hour, s.start_minute, s.start_second, s.red, s.far_red, s.blue, s.white);
        out += buf;
    }
    if (out.isEmpty()) out = "No fixture scenarios enabled";
    strlcpy(r, out.c_str(), l);
    return true;
}

bool AiAgent::toolFixtureScenarioSet(const char* a, char* r, size_t l) {
    JsonDocument doc; deserializeJson(doc, a);
    int idx = doc["index"] | -1;
    if (idx < 0 || idx >= MAX_FIXTURE_SCENARIOS) {
        snprintf(r, l, "Error: index 0-%d required", MAX_FIXTURE_SCENARIOS - 1); return false;
    }
    FixtureScenario& s = _cfg->cfg.fixture.scenarios[idx];
    if (doc["enabled"].is<bool>())       s.enabled      = doc["enabled"].as<bool>();
    if (doc["start_hour"].is<int>())     s.start_hour   = (uint8_t)constrain(doc["start_hour"].as<int>(), 0, 23);
    if (doc["start_minute"].is<int>())   s.start_minute = (uint8_t)constrain(doc["start_minute"].as<int>(), 0, 59);
    if (doc["start_second"].is<int>())   s.start_second = (uint8_t)constrain(doc["start_second"].as<int>(), 0, 59);
    if (doc["red"].is<int>())            s.red          = (uint8_t)constrain(doc["red"].as<int>(), 0, 200);
    if (doc["far_red"].is<int>())        s.far_red      = (uint8_t)constrain(doc["far_red"].as<int>(), 0, 200);
    if (doc["blue"].is<int>())           s.blue         = (uint8_t)constrain(doc["blue"].as<int>(), 0, 200);
    if (doc["white"].is<int>())          s.white        = (uint8_t)constrain(doc["white"].as<int>(), 0, 200);
    _cfg->save();
    snprintf(r, l, "Fixture scenario %d updated (at %02d:%02d enabled=%s)", idx, s.start_hour, s.start_minute, s.enabled ? "yes" : "no");
    return true;
}

String AiAgent::tgApiUrl(const char* method) {
    String u = "https://api.telegram.org/bot";
    u += _cfg->cfg.ai_tg_token;
    u += '/'; u += method;
    return u;
}

void AiAgent::pollTelegram() {
    // With CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=8192 + OUT=4096, TLS needs ~13KB heap.
    // Keep threshold high enough for HTTPClient, String, JsonDocument overhead.
    if (ESP.getFreeHeap() < 45000) {
        Serial.printf("[AI] Telegram poll skipped: low heap (%u)\n", ESP.getFreeHeap());
        return;
    }
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    String url = tgApiUrl("getUpdates");
    url += "?limit=5&timeout=1&offset=";
    url += String((long long)(_tg_last_id + 1));
    http.begin(client, url);
    http.setTimeout(5000);
    int code = http.GET();
    if (code != 200) { http.end(); return; }
    String resp = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return;
    if (!doc["ok"].as<bool>()) return;

    for (JsonObject upd : doc["result"].as<JsonArray>()) {
        long long uid = upd["update_id"] | 0LL;
        if (uid > _tg_last_id) _tg_last_id = uid;

        JsonObject msg = upd["message"].as<JsonObject>();
        if (msg.isNull()) msg = upd["edited_message"].as<JsonObject>();
        if (msg.isNull()) continue;

        const char* text = msg["text"]; if (!text) continue;
        long long chat_id = msg["chat"]["id"] | 0LL;

        // Whitelist filter
        if (_cfg->cfg.ai_tg_chat_id[0] != '\0') {
            char allowed[24];
            snprintf(allowed, sizeof(allowed), "%lld", chat_id);
            if (strcmp(allowed, _cfg->cfg.ai_tg_chat_id) != 0) continue;
        }

        // Don't queue if already processing
        if (_processing) { tgSend("Занят, повторите позже.", chat_id); continue; }

        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            strlcpy(_pending, text, sizeof(_pending));
            _pending_chat_id = chat_id;
            _has_pending     = true;
            _processing      = true;
            xSemaphoreGive(_mutex);
        }
    }
}

void AiAgent::notifyEnabled() {
    if (!_cfg || !_cfg->cfg.ai_tg_token[0] || !_cfg->cfg.ai_tg_chat_id[0]) return;
    if (!WiFi.isConnected()) return;
    long long chat_id = atoll(_cfg->cfg.ai_tg_chat_id);
    if (chat_id == 0) return;
    tgSend("Я снова в сети, чем могу помочь?", chat_id);
}

void AiAgent::sendTelegram(const char* text, long long chat_id) {
    tgSend(text, chat_id);
}

void AiAgent::tgSend(const char* text, long long chat_id) {
    if (!text || !text[0]) return;
    if (ESP.getFreeHeap() < 35000) {
        Serial.printf("[AI] tgSend skipped: low heap (%u)\n", ESP.getFreeHeap());
        return;
    }
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.begin(client, tgApiUrl("sendMessage"));
    http.setTimeout(10000);
    http.addHeader("Content-Type", "application/json");

    // Telegram max message length ~4096; we split just in case
    const size_t MAX_CHUNK = 3800;
    size_t total = strlen(text), offset = 0;
    while (offset < total) {
        size_t chunk = min((size_t)(total - offset), MAX_CHUNK);
        JsonDocument body;
        body["chat_id"] = chat_id;
        char buf[MAX_CHUNK + 1];
        memcpy(buf, text + offset, chunk); buf[chunk] = '\0';
        body["text"] = buf;
        String s; serializeJson(body, s);
        http.POST(s);
        offset += chunk;
    }
    http.end();
}

void AiAgent::addToolsOpenAI(JsonArray& arr)    { addToolsCommon(this, arr, false); }
void AiAgent::addToolsAnthropic(JsonArray& arr) { addToolsCommon(this, arr, true);  }
