#pragma once
// ============== AI Agent for ESP-HUB ==============
// LLM agent with tool support for full ESP32 control.
// Backends: LM Studio (default), Ollama, OpenAI, OpenRouter, Anthropic.
// Context window: 20000 tokens.
// Telegram bot integration optional.

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "config.h"
#include "sensor_manager.h"
#include "fixture_manager.h"
#include "gpio_scheduler.h"
#include "mqtt_client.h"

// ── Provider IDs (matches ai_provider config field) ─────────────────────────
#define AI_PROV_LM_STUDIO    0   // local HTTP (default)
#define AI_PROV_OLLAMA       1   // local HTTP
#define AI_PROV_OPENAI       2   // HTTPS + API key
#define AI_PROV_OPENROUTER   3   // HTTPS + API key
#define AI_PROV_ANTHROPIC    4   // HTTPS + API key, different call format

// ── Limits ────────────────────────────────────────────────────────────────
#define AI_MAX_HIST         24          // stored messages (12 turns)
#define AI_HIST_MSG_LEN     512         // max chars per stored message
#define AI_RESP_LEN         2048        // user-visible response buffer
#define AI_TOOL_RES_LEN     512         // tool execution result
#define AI_PENDING_LEN      512         // pending message slot
#define AI_MAX_TOOL_ROUNDS  5           // max tool iteration cycles
#define AI_LLM_MAX_TOKENS   1024
#define AI_CTX_SIZE         20000       // context window for local models
#define AI_TG_POLL_MS       3000        // Telegram polling interval

// ── History entry ─────────────────────────────────────────────────────────
struct AiHistMsg {
    char role[12];                   // user / assistant / tool
    char content[AI_HIST_MSG_LEN];
    char tool_id[28];
    char tool_name[32];
    bool is_tool_call   = false;
    bool is_tool_result = false;
};

// ============================================================================
class AiAgent {
public:
    void begin(ConfigManager*  cfg,
               SensorManager*  sensors,
               FixtureManager* fixture,
               GpioScheduler*  gpio,
               MQTTClient*     mqtt);

    // Call from loop() — handles Telegram polling & wakeup signals
    void tick();

    // Submit a message for async processing. Thread-safe.
    // Returns false if busy or AI disabled.
    bool submitMessage(const char* text, long long tg_chat_id = 0);

    bool        isProcessing()  const { return _processing; }
    const char* lastResponse()  const { return _last_resp; }
    uint32_t    responseSeq()   const { return _resp_seq; }
    void        clearHistory();

    // Send a Telegram notification that AI was enabled (if TG is configured)
    void        notifyEnabled();

    // Send a Telegram message to a specific chat (public, for web portal notifications)
    void        sendTelegram(const char* text, long long chat_id);

private:
    // ── Dependencies ──────────────────────────────────────────────────────
    ConfigManager*  _cfg     = nullptr;
    SensorManager*  _sensors = nullptr;
    FixtureManager* _fixture = nullptr;
    GpioScheduler*  _gpio    = nullptr;
    MQTTClient*     _mqtt    = nullptr;

    // ── Conversation history ───────────────────────────────────────────────
    AiHistMsg _hist[AI_MAX_HIST];
    int       _hist_len = 0;

    // ── Single-slot pending queue ──────────────────────────────────────────
    char           _pending[AI_PENDING_LEN];
    long long      _pending_chat_id = 0;
    volatile bool  _has_pending     = false;

    // ── Response state ─────────────────────────────────────────────────────
    char           _last_resp[AI_RESP_LEN];
    volatile bool  _processing = false;
    volatile uint32_t _resp_seq = 0;

    // ── RTOS ──────────────────────────────────────────────────────────────
    SemaphoreHandle_t _mutex    = nullptr;
    SemaphoreHandle_t _work_sem = nullptr;
    TaskHandle_t      _task     = nullptr;

    // ── Telegram state ─────────────────────────────────────────────────────
    long long     _tg_last_id    = 0;
    unsigned long _tg_last_ms    = 0;

    // ── Task ───────────────────────────────────────────────────────────────
    static void s_taskFn(void* pv);
    void        taskBody();

    // ── Core logic ────────────────────────────────────────────────────────
    void processMsg(const char* text, long long tg_chat_id);

    // ── Request building ───────────────────────────────────────────────────
    String buildReqOpenAI   (const char* sys);
    String buildReqAnthropic(const char* sys);
    String buildReqLMStudio (const char* sys);
    bool   callLlm(const String& body,
                   String& out_text, bool& out_has_tc,
                   char* tc_name, size_t tc_name_len,
                   char* tc_id,   size_t tc_id_len,
                   char* tc_args, size_t tc_args_len);

    // ── Tool execution ─────────────────────────────────────────────────────
    bool doTool  (const char* name, const char* args, char* res, size_t rlen);
    bool toolGpioWrite   (const char* a, char* r, size_t l);
    bool toolGpioRead    (const char* a, char* r, size_t l);
    bool toolGpioReadAll (const char* a, char* r, size_t l);
    bool toolSensors     (const char* a, char* r, size_t l);
    bool toolLightSet    (const char* a, char* r, size_t l);
    bool toolLightPreset (const char* a, char* r, size_t l);
    bool toolLightStatus (const char* a, char* r, size_t l);
    bool toolSysInfo     (const char* a, char* r, size_t l);
    bool toolMqttPub     (const char* a, char* r, size_t l);
    bool toolMemSet      (const char* a, char* r, size_t l);
    bool toolMemGet      (const char* a, char* r, size_t l);
    bool toolMemList     (const char* a, char* r, size_t l);
    bool toolMemDelete   (const char* a, char* r, size_t l);
    bool toolConfigGet   (const char* a, char* r, size_t l);
    bool toolRestart     (const char* a, char* r, size_t l);
    bool toolGetTime     (const char* a, char* r, size_t l);
    bool toolGetDiag     (const char* a, char* r, size_t l);
    bool toolDelay       (const char* a, char* r, size_t l);
    bool toolPersonaSet  (const char* a, char* r, size_t l);
    bool toolPersonaGet  (const char* a, char* r, size_t l);
    bool toolPersonaReset(const char* a, char* r, size_t l);

    // ── CRON tools ─────────────────────────────────────────────────────────
    bool toolCronSet     (const char* a, char* r, size_t l);
    bool toolCronList    (const char* a, char* r, size_t l);
    bool toolCronDelete  (const char* a, char* r, size_t l);
    bool toolCronTz      (const char* a, char* r, size_t l);

    // ── Rate Limiter tools ─────────────────────────────────────────────────
    bool toolRlStatus    (const char* a, char* r, size_t l);
    bool toolRlReset     (const char* a, char* r, size_t l);

    // ── ADC / I2C / signal tools ───────────────────────────────────────────
    bool toolAdcRead     (const char* a, char* r, size_t l);
    bool toolI2cScan     (const char* a, char* r, size_t l);
    bool toolAdcSmooth   (const char* a, char* r, size_t l);

    // ── GPIO Timer tools ───────────────────────────────────────────────────
    bool toolGpioTimerList    (const char* a, char* r, size_t l);
    bool toolGpioTimerSet     (const char* a, char* r, size_t l);

    // ── Fixture Timer/Scenario tools ──────────────────────────────────────
    bool toolFixtureTimerList   (const char* a, char* r, size_t l);
    bool toolFixtureTimerSet    (const char* a, char* r, size_t l);
    bool toolFixtureScenarioList(const char* a, char* r, size_t l);
    bool toolFixtureScenarioSet (const char* a, char* r, size_t l);

    // ── Config write tool ──────────────────────────────────────────────────
    bool toolConfigSet   (const char* a, char* r, size_t l);

    // ── Tool JSON schemas ──────────────────────────────────────────────────
    void addToolsOpenAI   (JsonArray& arr);
    void addToolsAnthropic(JsonArray& arr);
    void addOneTool       (JsonArray& arr, bool anthropic,
                           const char* name, const char* desc,
                           const char* params_json);
    friend void addToolsImpl(AiAgent*, JsonArray&, bool);

    // ── History helpers ────────────────────────────────────────────────────
    void histAdd(const char* role, const char* content,
                 bool is_tc = false, bool is_tr = false,
                 const char* tc_id = nullptr, const char* tc_name = nullptr);

    // ── Provider helpers ───────────────────────────────────────────────────
    const char* llmUrl()        const;
    const char* llmModel()      const;
    bool        llmNeedsKey()   const;
    bool        llmIsAnthropic()const;
    bool        llmIsLocal()    const;
    bool        llmIsLMStudio() const;
    const char* buildSysPrompt(char* buf, size_t len) const;

    // ── Telegram ───────────────────────────────────────────────────────────
    void   pollTelegram();
    void   tgSend(const char* text, long long chat_id);
    String tgApiUrl(const char* method);

    // ── AI memory (/ai_mem.json on LittleFS) ───────────────────────────────
    bool aiMemSet (const char* key, const char* val);
    bool aiMemGet (const char* key, char* val, size_t vlen);
    void aiMemList(char* out, size_t olen);
};

extern AiAgent aiAgent;
