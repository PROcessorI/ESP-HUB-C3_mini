#include "web_portal.h"
#include <HTTPClient.h>       // посредник API камеры
#include <esp_system.h>      // esp_reset_reason()
#include <esp32-hal-cpu.h>   // getApbFrequency()
#include "gpio_scheduler.h"
#include "serial_console.h"
#include <HTTPClient.h>       // посредник API камеры
extern GpioScheduler gpioSched;  // определено в main.cpp
extern SerialConsole serialCon;  // определено в main.cpp

static bool isLoopProneMeshCommand(const String& cmd) {
    String s = cmd;
    s.trim();
    s.toLowerCase();
    if (!s.startsWith("mesh ")) return false;
    return s.startsWith("mesh cmd") || s.startsWith("mesh chat") || s.startsWith("mesh data");
}

// ================================================================
//                       ИНИЦИАЛИЗАЦИЯ
// ================================================================

void WebPortal::begin(ConfigManager* cfg, WiFiManager* wifi,
                      MQTTClient* mqtt, SensorManager* sensors, BLEManager* ble,
                      FixtureManager* fixture, MeshManager* mesh) {
    _cfg = cfg;
    _wifi = wifi;
    _mqtt = mqtt;
    _sensors = sensors;
    _ble = ble;
    _fixture = fixture;
    _mesh = mesh;

    // Страницы
    _server.on("/",           [this]() { handleRoot(); });
    _server.on("/sensors",   [this]() { handleSensors(); });
    _server.on("/mqtt",      [this]() { handleMqtt(); });
    _server.on("/system",    [this]() { handleSystem(); });
    _server.on("/bluetooth", [this]() { handleBluetooth(); });
    _server.on("/fixtures",  [this]() { handleFixtures(); });
    _server.on("/mesh",      [this]() { handleMesh(); });
    _server.on("/camera",    [this]() { handleCamera(); });
    _server.on("/api/camera/relay", HTTP_GET, [this]() { handleApiCameraRelay(); });
    _server.on("/ai",               [this]() { handleAI(); });
    _server.on("/api/ai/chat",      HTTP_POST, [this]() { handleApiAiChat(); });
    _server.on("/api/ai/status",    HTTP_GET,  [this]() { handleApiAiStatus(); });
    _server.on("/api/ai/history/clear", HTTP_POST, [this]() { handleApiAiClearHistory(); });
    // Загрузка/выгрузка модели (прямо в LM Studio, без получения списка)
    _server.on("/api/ai/lms/load",   HTTP_POST, [this]() { handleApiAiLmsLoad(); });
    _server.on("/api/ai/lms/unload", HTTP_POST, [this]() { handleApiAiLmsUnload(); });
    _server.on("/cron",             [this]() { handleCron(); });
    _server.on("/api/cron",         HTTP_GET,  [this]() { handleApiCronList(); });
    _server.on("/api/cron/add",     HTTP_POST, [this]() { handleApiCronAdd(); });
    _server.on("/api/cron/delete",  HTTP_POST, [this]() { handleApiCronDelete(); });
    _server.on("/api/cron/enable",  HTTP_POST, [this]() { handleApiCronEnable(); });
    _server.on("/api/cron/tz",      HTTP_POST, [this]() { handleApiCronTz(); });
    _server.on("/api/nat/toggle",   HTTP_POST, [this]() { handleApiNatToggle(); });
    _server.on("/api",       HTTP_GET, [this]() { handleApiDocs(); });
    _server.on("/api/data",  [this]() { handleApiData(); });
    _server.on("/api/scan",  HTTP_GET, [this]() { handleApiScan(); });
    _server.on("/api/wifi",        HTTP_GET,  [this]() { handleApiWifiStatus(); });
    _server.on("/api/mqtt",        HTTP_GET,  [this]() { handleApiMqttStatus(); });
    _server.on("/api/system",      HTTP_GET,  [this]() { handleApiSystemStatus(); });
    _server.on("/api/mesh",        HTTP_GET,  [this]() { handleApiMeshStatus(); });
    _server.on("/api/mesh/chat",   HTTP_POST, [this]() { handleApiMeshSendChat(); });
    _server.on("/api/mesh/data",   HTTP_POST, [this]() { handleApiMeshSendData(); });
    _server.on("/api/mesh/cmd",    HTTP_POST, [this]() { handleApiMeshSendCmd(); });
    _server.on("/api/mesh/log",    HTTP_GET,  [this]() { handleApiMeshLog(); });
    _server.on("/api/mesh/log/clear", HTTP_POST, [this]() { handleApiMeshLogClear(); });
    _server.on("/api/ble",         HTTP_GET,  [this]() { handleApiBleStatus(); });
    _server.on("/api/ble/send",    HTTP_POST, [this]() { handleApiBleSend(); });
    _server.on("/api/ble/clear-log",HTTP_POST,[this]() { handleApiBlearLog(); });
    _server.on("/api/fixture",    HTTP_GET,  [this]() { handleApiFixtureStatus(); });
    _server.on("/api/fixture/set", HTTP_POST, [this]() { handleApiFixtureSet(); });
    _server.on("/api/fixture/on",  HTTP_POST, [this]() { handleApiFixtureOn(); });
    _server.on("/api/fixture/off", HTTP_POST, [this]() { handleApiFixtureOff(); });
    _server.on("/api/fixture/color", HTTP_POST, [this]() { handleApiFixtureColor(); });
    _server.on("/api/fixture/timers", HTTP_GET, [this]() { handleApiFixtureTimers(); });
    _server.on("/api/fixture/timers/set", HTTP_POST, [this]() { handleApiFixtureTimersSet(); });
    _server.on("/api/fixture/scenarios", HTTP_GET, [this]() { handleApiFixtureScenarios(); });
    _server.on("/api/fixture/scenarios/set", HTTP_POST, [this]() { handleApiFixtureScenariosSet(); });
    _server.on("/api/fixture/toggle",          HTTP_POST, [this]() { handleApiFixtureToggle(); });
    _server.on("/api/fixture/dim",             HTTP_POST, [this]() { handleApiFixtureDim(); });
    _server.on("/api/fixture/timer/enable",    HTTP_POST, [this]() { handleApiFixtureTimerEnable(); });
    _server.on("/api/fixture/scenario/enable", HTTP_POST, [this]() { handleApiFixtureScenarioEnable(); });
    _server.on("/api/fixture/timer/trigger",   HTTP_POST, [this]() { handleApiFixtureTimerTrigger(); });
    _server.on("/api/fixture/enable",          HTTP_POST, [this]() { handleApiFixtureEnable(); });
    _server.on("/api/fixture/demo",            HTTP_POST, [this]() { handleApiFixtureDemo(); });
    _server.on("/api/gpio",          HTTP_GET,  [this]() { handleGpioApi(); });
    _server.on("/api/gpio/set",       HTTP_POST, [this]() { handleGpioSet(); });
    _server.on("/api/gpio/timers",    HTTP_GET,  [this]() { handleGpioTimersApi(); });
    _server.on("/save/gpio-timers",   HTTP_POST, [this]() { handleSaveGpioTimers(); });

    // Действия (сохранение / сброс)
    _server.on("/save/wifi",    HTTP_POST, [this]() { handleSaveWifi(); });
    _server.on("/save/mqtt",    HTTP_POST, [this]() { handleSaveMqtt(); });
    _server.on("/save/sensors", HTTP_POST, [this]() { handleSaveSensors(); });
    _server.on("/save/ap",      HTTP_POST, [this]() { handleSaveAp(); });
    _server.on("/save/ble",     HTTP_POST, [this]() { handleSaveBle(); });
    _server.on("/save/mesh",    HTTP_POST, [this]() { handleSaveMesh(); });
    _server.on("/api/mesh/toggle", HTTP_POST, [this]() { handleApiMeshToggle(); });
    _server.on("/save/camera",  HTTP_POST, [this]() { handleSaveCamera(); });
    _server.on("/save/ai",      HTTP_POST, [this]() { handleSaveAI(); });
    _server.on("/save/fixture", HTTP_POST, [this]() { handleSaveFixture(); });
    _server.on("/save/fixture-scenarios", HTTP_POST, [this]() { handleSaveScenarios(); });
    _server.on("/save/fixture-timers", HTTP_POST, [this]() { handleSaveFixtureTimers(); });
    _server.on("/save/system",  HTTP_POST, [this]() { handleSaveSystem(); });
    _server.on("/reset/wifi",   HTTP_POST, [this]() { handleResetWifi(); });
    _server.on("/reboot",       HTTP_POST, [this]() { handleReboot(); });
    _server.on("/reset",        HTTP_POST, [this]() { handleReset(); });

    _server.onNotFound([this]() { handleNotFound(); });

    // Простая страница настройки WiFi (captive portal / первичная настройка)
    _server.on("/wifi", HTTP_GET, [this]() { handleWifiSetup(); });

    // Обработчики captive portal: редиректим probe-запросы на текущий AP root.
    auto captive = [this]() {
        String apRoot = String("http://") + _wifi->apIP() + "/";
        _server.sendHeader("Location", apRoot, true);
        _server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
        _server.send(302, "text/plain", "Redirecting...");
    };
    // Favicon (избежать пустых ответов в логах)
    _server.on("/favicon.ico", [this]() {
        _server.send(404, "text/plain", "Not found");
    });

    // Обработчики captive portal (iOS, Android, Windows)
    _server.on("/generate_204",             captive);  // Android
    _server.on("/gen_204",                  captive);  // Android (alt)
    _server.on("/fwlink",                   captive);  // Microsoft
    _server.on("/hotspot-detect.html",      captive);  // iOS
    _server.on("/library/test/success.html",captive);  // iOS
    _server.on("/canonical.html",           captive);  // Firefox
    _server.on("/connecttest.txt",          captive);  // Windows
    _server.on("/ncsi.txt",                 captive);  // Windows NCSI
    _server.on("/success.txt",              captive);  // Common probe path

    _server.begin();
    Serial.println(F("[WEB] Portal started on port 80"));
}

void WebPortal::tick() {
    _server.handleClient();
}

// ================================================================
//                       CSS СТИЛИ (как PROGMEM)
// ================================================================

// CSS хранится в PROGMEM и отправляется напрямую — без выделения кучи
void WebPortal::sendCssStyles() {
    _server.sendContent(F("<style>"
        ":root{--bg:#0f1117;--bg2:#161b22;--bg3:#0d1117;--brd:#30363d;--brd2:#21262d;"
        "--txt:#e1e4e8;--txt2:#8b949e;--acc:#58a6ff;--grn:#238636;--grn-h:#2ea043;"
        "--red:#da3633;--red-h:#f85149;--bgo:#21262d;--sel:#c9d1d9}"
        "html.light{--bg:#f6f8fa;--bg2:#ffffff;--bg3:#f0f2f5;--brd:#d0d7de;--brd2:#e1e4e8;"
        "--txt:#24292f;--txt2:#57606a;--acc:#0969da;--grn:#1a7f37;--grn-h:#2da44e;"
        "--red:#cf222e;--red-h:#f85149;--bgo:#f0f2f5;--sel:#24292f}"
        "*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--txt);line-height:1.6;transition:background .3s,color .3s}"
        ".wrap{max-width:800px;margin:0 auto;padding:10px}"
        "nav{background:var(--bg2);border-bottom:1px solid var(--brd);padding:8px 16px;display:flex;align-items:center;flex-wrap:wrap;gap:8px}"
        "nav .brand{color:var(--acc);font-weight:700;font-size:18px;margin-right:16px;text-decoration:none}"
        "nav a{color:var(--txt2);text-decoration:none;padding:6px 12px;border-radius:6px;font-size:14px;transition:.2s}"
        "nav a:hover,nav a.active{color:var(--txt);background:var(--bgo)}"
        "nav a.active{color:var(--acc);border-bottom:2px solid var(--acc)}"
        ".nt-btn{background:none;border:1px solid var(--brd);color:var(--txt2);padding:4px 10px;border-radius:6px;cursor:pointer;font-size:13px;transition:.2s}"
        ".nt-btn:hover{background:var(--bgo);color:var(--txt)}"
        ".nav-end{margin-left:auto;display:flex;gap:6px;align-items:center}"
        ".card{background:var(--bg2);border:1px solid var(--brd);border-radius:10px;padding:16px;margin:12px 0}"
        ".card h3{color:var(--acc);margin-bottom:10px;font-size:16px;border-bottom:1px solid var(--brd2);padding-bottom:6px}"
        "label{display:block;color:var(--txt2);font-size:13px;margin:8px 0 3px}"
        "input[type=text],input[type=password],input[type=number],select{"
        "width:100%;padding:8px 10px;background:var(--bg3);border:1px solid var(--brd);"
        "border-radius:6px;color:var(--txt);font-size:14px;outline:none}"
        "input:focus,select:focus{border-color:var(--acc)}"
        ".btn{padding:8px 20px;border:none;border-radius:6px;cursor:pointer;font-size:14px;font-weight:600;transition:.2s;margin:4px}"
        ".btn-primary{background:var(--grn);color:#fff}.btn-primary:hover{background:var(--grn-h)}"
        ".btn-danger{background:var(--red);color:#fff}.btn-danger:hover{background:var(--red-h)}"
        ".btn-secondary{background:var(--bgo);color:var(--sel);border:1px solid var(--brd)}.btn-secondary:hover{background:var(--brd)}"
        "table{width:100%;border-collapse:collapse}"
        "th,td{text-align:left;padding:8px 10px;border-bottom:1px solid var(--brd2)}"
        "th{color:var(--txt2);font-size:12px;text-transform:uppercase}"
        ".badge{display:inline-block;padding:2px 8px;border-radius:12px;font-size:12px;font-weight:600}"
        ".badge-green{background:#1b4332;color:#2ea043}"
        ".badge-red{background:#3d1114;color:#f85149}"
        ".badge-blue{background:#0c2d6b;color:#58a6ff}"
        ".badge-yellow{background:#3d2e00;color:#d29922}"
        "html.light .badge-green{background:#dafbe1;color:#1a7f37}"
        "html.light .badge-red{background:#ffebe9;color:#cf222e}"
        "html.light .badge-blue{background:#ddf4ff;color:#0550ae}"
        "html.light .badge-yellow{background:#fff8c5;color:#9a6700}"
        ".grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
        "@media(max-width:600px){.grid2{grid-template-columns:1fr}}"
        ".s-row{background:var(--bg3);border:1px solid var(--brd);border-radius:8px;padding:12px;margin:8px 0}"
        ".s-row .s-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:6px}"
        ".s-val{font-size:28px;font-weight:700;color:var(--acc)}"
        ".s-unit{font-size:14px;color:var(--txt2);margin-left:4px}"
        ".s-label{font-size:13px;color:var(--txt2)}"
        ".toggle{position:relative;display:inline-block;width:40px;height:22px}"
        ".toggle input{opacity:0;width:0;height:0}"
        ".toggle .slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:var(--brd);border-radius:22px;transition:.3s}"
        ".toggle .slider:before{content:'';position:absolute;height:16px;width:16px;left:3px;bottom:3px;background:var(--txt2);border-radius:50%;transition:.3s}"
        ".toggle input:checked+.slider{background:var(--grn)}"
        ".toggle input:checked+.slider:before{transform:translateX(18px);background:#fff}"
        ".mt{margin-top:12px}.mb{margin-bottom:12px}"
        ".text-center{text-align:center}"
        ".text-muted{color:var(--txt2);font-size:13px}"
        ".flex-between{display:flex;justify-content:space-between;align-items:center}"
        ".wifi-item{padding:8px 12px;border:1px solid var(--brd);border-radius:6px;margin:4px 0;"
        "cursor:pointer;display:flex;justify-content:space-between;align-items:center;"
        "background:var(--bg3);transition:.2s}"
        ".wifi-item:hover{border-color:var(--acc);background:var(--bgo)}"
        "</style>"));
}

// ================================================================
//                       HTML HELPERS
// ================================================================

// pageHeader отправляет напрямую — нет промежуточной String для большого блока CSS
void WebPortal::sendPageHeader(const String& title) {
    // Один большой кусок: meta + ранний JS (предотвращение «мигания» темы/языка)
    // ПРИМЕЧ.: <!DOCTYPE html><html><head> уже отправлен startPage()
    _server.sendContent(F("<meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<script>var _th=localStorage.getItem('theme')||'dark';"
           "if(_th==='light')document.documentElement.classList.add('light');"
           "var lang=localStorage.getItem('lang')||'ru';"
           "var theme=_th;"
           "function toggleTheme(){theme=theme==='dark'?'light':'dark';localStorage.setItem('theme',theme);"
           "if(theme==='light')document.documentElement.classList.add('light');"
           "else document.documentElement.classList.remove('light');"
           "var b=document.getElementById('theme-btn');if(b)b.innerHTML=theme==='dark'?'Light':'Dark';}"
           "function toggleLang(){lang=lang==='en'?'ru':'en';localStorage.setItem('lang',lang);"
           "if(typeof applyLang==='function')applyLang();}"
           "</script>"));
    // CSS — одно крупное чтение из flash (~3.8 КБ)
    sendCssStyles();
    // Заголовок + закрытие head — объединено в один кусок
    String th; th.reserve(title.length() + 40);
    th += F("<title>"); th += title; th += F(" - ESP-HUB</title></head><body>");
    _server.sendContent(th);
}

// pageFooter стримит ~6 КБ JS напрямую из flash — без построения String в куче
void WebPortal::sendPageFooter() {
    _server.sendContent(F(
        "<div class='text-center text-muted mt' style='padding:20px'>"
        "ESP-HUB v1.0 &bull; <span data-t='uptime'>Uptime</span>: <span id='up'></span>"
        "</div><script>"));

    // --- Переводы ---
    _server.sendContent(F(
        "var TR={"
        "en:{dashboard:'Dashboard',sensors:'Sensors',system:'System',mqtt:'MQTT',api:'API',bluetooth:'Bluetooth',"
        "camera:'ESP-CAM',cron:'CRON',ai:'AI',mesh:'Mesh',"
        "fixtures:'Fixtures',status:'Status',livetel:'Live Telemetry',sensconf:'Sensor Configuration',"
        "mqttconf:'MQTT Configuration',wificonf:'WiFi Configuration',"
        "sysinfo:'System Info',actions:'Actions',enabled:'Enabled',"
        "'sensor-type':'Sensor Type','gpio-addr':'GPIO / I2C Addr',label:'Label',"
        "broker:'Broker Host',port:'Port',user:'Username',pass:'Password',"
        "topic:'Topic',interval:'Interval (sec)',ssid:'SSID',scan:'Scan WiFi',"
        "'scan-hint':'Click to select','save-apply':'Save & Apply',"
        "'save-mqtt':'Save MQTT','save-wifi':'Save WiFi & Reboot',"
        "reboot:'Reboot','factory-reset':'Factory Reset',device:'Device',"
        "chip:'Chip','cpu-freq':'CPU Freq',heap:'Free Heap',sketch:'Sketch Size',"
        "flash:'Flash',mode:'WiFi Mode',mac:'MAC',ip:'IP',rssi:'RSSI',"
        "uptime:'Uptime','net-found':'networks found',scanning:'Scanning...',"
        "password:'Password','wifi-connected':'Connected','ap-mode':'AP Mode','no-conn':'No connection',"
        "'wifi-saved':'WiFi credentials saved!','rebooting':'Rebooting & connecting to',"
        "'after-connect':'Open web interface at','mdns-hint':'If mDNS does not work, find the IP in your router DHCP table',"
        "'slot-label':'Slot','slot-active':'Active','slot-error':'Error','slot-free':'Free',"
        "'slot-title':'Independent channel: own sensor, GPIO, bus and protocol',"
        "'slot-hint-text':'Slot (Slot) — independent sensor channel. 8 slots total, each configured independently: sensor type, bus (I\\u00B2C/UART/CAN), GPIO and output protocol.',"
        "'field-enabled':'Enabled','field-type':'Sensor Type','field-bus':'Bus','field-out':'Output',"
        "'color-red':'Red','color-far-red':'Far Red','color-blue':'Blue','color-white':'White',"
        "'fixture-title':'Fixture','fixture-control':'Control','fixture-settings':'Settings',"
        "'fixture-enable':'Enable fixture control','fixture-apply':'Apply','fixture-save':'Save',"
        "'fixture-save-reboot':'Save & Reboot','fixture-status':'Status','fixture-current':'Current Brightness',"
        "'fixture-disabled':'Disabled','fixture-enabled':'Enabled','fixture-ack-ok':'ACK OK','fixture-ack-fail':'ACK FAIL',"
        "'fixture-timer-run':'Run Duration','fixture-timer-run-hint':'Auto-shutoff after N hours:minutes:seconds',"
        "'timer-title':'Timers','timer-add':'Add Timer','timer-edit':'Edit','timer-delete':'Delete',"
        "'timer-on':'On Time','timer-off':'Off Time','timer-hours':'Hours','timer-minutes':'Minutes','timer-seconds':'Seconds',"
        "'timer-days':'Days','timer-active':'Active','scenario-title':'Scenarios','scenario-add':'Add Scenario',"
        "'scenario-interval':'Interval (min)','scenario-action':'Action','scenario-run':'Run',"
        "'sc-en-all':'Enable all','sc-dis-all':'Disable all',"
        "'field-gpio':'GPIO / I2C Addr','field-label':'Sensor label','field-uart-tx':'UART TX pin',"
        "'field-uart-num':'UART number (1 or 2)','field-can-id':'CAN ID (hex, e.g. 0x100)',"
        "'field-can-dlc':'CAN DLC (bytes)','field-http-url':'HTTP POST URL target',"
        "'gpio-monitor':'GPIO Monitor','gpio-refresh':'Refresh','gpio-auto':'Auto (3s)',"
        "'gpio-updated':'Updated:','gpio-error':'Error',"
        "'gpio-warn-reserved':'Reserved (Flash/PSRAM) \\u2014 do not use!',"
        "'gpio-warn-special':'Caution: special function pin',"
        "'gpio-mode':'Mode','gpio-level':'Level','gpio-apply':'Apply','gpio-reset-pin':'Reset',"
        "'gpio-unset':'Off (unset)','gpio-in':'INPUT (read)','gpio-pu':'INPUT_PULLUP',"
        "'gpio-pd':'INPUT_PULLDOWN','gpio-out-ctrl':'OUTPUT (control)',"
        "'gpio-legend-hi':'INPUT HIGH','gpio-legend-lo':'INPUT LOW',"
        "'gpio-legend-out-hi':'OUTPUT HIGH','gpio-legend-out-lo':'OUTPUT LOW',"
        "'gpio-legend-warn':'Warning','gpio-legend-danger':'Do not use',"
        "'gpio-load':'Loading...',"
        "'timer-card':'GPIO Timers / Scheduler',"
        "'timer-hint':'Up to 23 hours — user time. 0\\u00D70\\u00D700 = disabled. Actions: HIGH/LOW — set level, TOGGLE — invert, PULSE — pulse with duration.',"
        "'timer-col-on':'On','timer-col-name':'Name','timer-col-gpio':'GPIO',"
        "'timer-col-action':'Action','timer-col-dur':'Dur.(ms)','timer-col-next':'Next run','timer-col-reset':'Reset',"
        "'timer-save':'Save Timers','timer-saving':'Saving...','timer-saved':'\\u2713 Saved',"
        "'timer-err':'\\u274C Error','timer-conn-err':'\\u274C Connection error',"
        "'timer-confirm-reset':'Reset timer ','timer-now':'\\u2713 now',"
        "'timer-reset-btn':'Reset timer','placeholder-pump':'Kitchen / Street / Server',"
        "'ft-title':'Interval Timers',"
        "'ft-desc':'\\u2014 repeat every N seconds without NTP. PULSE_CUSTOM: turn on for dur ms, then off.',"
        "'ft-en-all':'Enable all','ft-dis-all':'Disable all','ft-save':'Save Timers',"
        "'ft-col-en':'En','ft-col-name':'Name','ft-col-act':'Action','ft-col-ctrl':'Ctrl',"
        "'ft-dur':'dur(ms)','ft-rh':'Rh','ft-rm':'Rm','ft-rs':'Rs',"
        "'fixture-preset-off':'Off','fixture-preset-full':'Full','fixture-preset-grow':'Grow',"
        "'sens-save':'Save \\u0026 Apply'},"));
    _server.sendContent(F(
        "ru:{dashboard:'\u041f\u0430\u043d\u0435\u043b\u044c',sensors:'\u0414\u0430\u0442\u0447\u0438\u043a\u0438',system:'\u0421\u0438\u0441\u0442\u0435\u043c\u0430',mqtt:'MQTT',api:'API',"
        "fixtures:'\u0421\u0432\u0435\u0442\u0438\u043b\u044c\u043d\u0438\u043a\u0438',bluetooth:'Bluetooth',mesh:'\u041c\u0435\u0448',"
        "camera:'ESP-CAM',cron:'\u041f\u043b\u0430\u043d\u0438\u0440\u043e\u0432\u0449\u0438\u043a',ai:'\u0418\u0418',"
        "status:'\u0421\u0442\u0430\u0442\u0443\u0441',livetel:'\u0422\u0435\u043b\u0435\u043c\u0435\u0442\u0440\u0438\u044f',sensconf:'\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0430 \u0434\u0430\u0442\u0447\u0438\u043a\u043e\u0432',"
        "mqttconf:'\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 MQTT',wificonf:'\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 WiFi',"
        "sysinfo:'\u041e \u0441\u0438\u0441\u0442\u0435\u043c\u0435',actions:'\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u044f',enabled:'\u0412\u043a\u043b\u044e\u0447\u0451\u043d',"
        "'sensor-type':'\u0422\u0438\u043f \u0434\u0430\u0442\u0447\u0438\u043a\u0430','gpio-addr':'GPIO / I2C \u0430\u0434\u0440\u0435\u0441',label:'\u041c\u0435\u0442\u043a\u0430',"
        "broker:'\u0425\u043e\u0441\u0442 \u0431\u0440\u043e\u043a\u0435\u0440\u0430',port:'\u041f\u043e\u0440\u0442',user:'\u041b\u043e\u0433\u0438\u043d',pass:'\u041f\u0430\u0440\u043e\u043b\u044c',"
        "topic:'\u0422\u043e\u043f\u0438\u043a',interval:'\u0418\u043d\u0442\u0435\u0440\u0432\u0430\u043b (\u0441\u0435\u043a)',ssid:'SSID',scan:'\u0421\u043a\u0430\u043d\u0438\u0440\u043e\u0432\u0430\u0442\u044c',"
        "'scan-hint':'\u041d\u0430\u0436\u043c\u0438\u0442\u0435 \u0434\u043b\u044f \u0432\u044b\u0431\u043e\u0440\u0430','save-apply':'\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u0438 \u043f\u0440\u0438\u043c\u0435\u043d\u0438\u0442\u044c',"
        "'save-mqtt':'\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c MQTT','save-wifi':'\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c WiFi \u0438 \u043f\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044c',"
        "reboot:'\u041f\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044c','factory-reset':'\u0421\u0431\u0440\u043e\u0441 \u043d\u0430\u0441\u0442\u0440\u043e\u0435\u043a',device:'\u0423\u0441\u0442\u0440\u043e\u0439\u0441\u0442\u0432\u043e',"
        "chip:'\u0427\u0438\u043f','cpu-freq':'\u0427\u0430\u0441\u0442\u043e\u0442\u0430 CPU',heap:'\u0421\u0432\u043e\u0431\u043e\u0434\u043d\u0430\u044f \u043f\u0430\u043c\u044f\u0442\u044c',sketch:'\u041f\u0440\u043e\u0448\u0438\u0432\u043a\u0430',"
        "flash:'Flash',mode:'\u0420\u0435\u0436\u0438\u043c WiFi',mac:'MAC',ip:'IP',rssi:'RSSI',"
        "uptime:'\u0410\u043f\u0442\u0430\u0439\u043c','net-found':'\u0441\u0435\u0442\u0435\u0439 \u043d\u0430\u0439\u0434\u0435\u043d\u043e',scanning:'\u0421\u043a\u0430\u043d\u0438\u0440\u043e\u0432\u0430\u043d\u0438\u0435...',bluetooth:'Bluetooth',"
        "password:'\u041f\u0430\u0440\u043e\u043b\u044c','wifi-connected':'\u041f\u043e\u0434\u043a\u043b\u044e\u0447\u0451\u043d','ap-mode':'AP \u0440\u0435\u0436\u0438\u043c','no-conn':'\u041d\u0435\u0442 \u0441\u0432\u044f\u0437\u0438',"
        "'wifi-saved':'\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 WiFi \u0441\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u044b!','rebooting':'\u041f\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u043a\u0430, \u043f\u043e\u0434\u043a\u043b\u044e\u0447\u0435\u043d\u0438\u0435 \u043a',"
        "'after-connect':'\u041e\u0442\u043a\u0440\u043e\u0439\u0442\u0435 \u0438\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441 \u043f\u043e \u0430\u0434\u0440\u0435\u0441\u0443','mdns-hint':'\u0415\u0441\u043b\u0438 mDNS \u043d\u0435 \u0440\u0430\u0431\u043e\u0442\u0430\u0435\u0442, \u043d\u0430\u0439\u0434\u0438\u0442\u0435 IP \u0432 DHCP-\u0442\u0430\u0431\u043b\u0438\u0446\u0435 \u0440\u043e\u0443\u0442\u0435\u0440\u0430',"
        "'slot-label':'\u0421\u043b\u043e\u0442','slot-active':'\u0410\u043a\u0442\u0438\u0432\u0435\u043d','slot-error':'\u041e\u0448\u0438\u0431\u043a\u0430','slot-free':'\u0421\u0432\u043e\u0431\u043e\u0434\u0435\u043d',"
        "'slot-title':'\u041d\u0435\u0437\u0430\u0432\u0438\u0441\u0438\u043c\u044b\u0439 \u043a\u0430\u043d\u0430\u043b: \u0441\u0432\u043e\u0439 \u0434\u0430\u0442\u0447\u0438\u043a, GPIO, \u0448\u0438\u043d\u0430 \u0438 \u043f\u0440\u043e\u0442\u043e\u043a\u043e\u043b',"
        "'slot-hint-text':'\u0421\u043b\u043e\u0442 (\u0421\u043b\u043e\u0442) \u2014 \u043d\u0435\u0437\u0430\u0432\u0438\u0441\u0438\u043c\u044b\u0439 \u043a\u0430\u043d\u0430\u043b \u0434\u0430\u0442\u0447\u0438\u043a\u0430. \u0412\u0441\u0435\u0433\u043e 8 \u0441\u043b\u043e\u0442\u043e\u0432 \u2014 \u043a\u0430\u0436\u0434\u044b\u0439 \u043d\u0430\u0441\u0442\u0440\u0430\u0438\u0432\u0430\u0435\u0442\u0441\u044f \u043d\u0435\u0437\u0430\u0432\u0438\u0441\u0438\u043c\u043e: \u0442\u0438\u043f \u0434\u0430\u0442\u0447\u0438\u043a\u0430, \u0448\u0438\u043d\u0430 (I\u00B2C/UART/CAN), GPIO \u0438 \u043f\u0440\u043e\u0442\u043e\u043a\u043e\u043b \u0432\u044b\u0432\u043e\u0434\u0430.',"
        "'field-enabled':'\u0412\u043a\u043b\u044e\u0447\u0451\u043d','field-type':'\u0422\u0438\u043f \u0434\u0430\u0442\u0447\u0438\u043a\u0430','field-bus':'\u0428\u0438\u043d\u0430','field-out':'\u0412\u044b\u0432\u043e\u0434',"
        "'color-red':'\u041a\u0440\u0430\u0441\u043d\u044b\u0439','color-far-red':'\u0414\u0430\u043b\u044c\u043d\u0438\u0439 \u043a\u0440\u0430\u0441\u043d\u044b\u0439','color-blue':'\u0421\u0438\u043d\u0438\u0439','color-white':'\u0411\u0435\u043b\u044b\u0439',"
        "'fixture-title':'\u0421\u0432\u0435\u0442\u0438\u043b\u044c\u043d\u0438\u043a','fixture-control':'\u0423\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u0435','fixture-settings':'\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438',"
        "'fixture-enable':'\u0412\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u0443\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u0435 \u0441\u0432\u0435\u0442\u0438\u043b\u044c\u043d\u0438\u043a\u043e\u043c','fixture-apply':'\u041f\u0440\u0438\u043c\u0435\u043d\u0438\u0442\u044c','fixture-save':'\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c',"
        "'fixture-save-reboot':'\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u0438 \u043f\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044c','fixture-status':'\u0421\u0442\u0430\u0442\u0443\u0441','fixture-current':'\u0422\u0435\u043a\u0443\u0449\u0430\u044f \u044f\u0440\u043a\u043e\u0441\u0442\u044c',"
        "'fixture-disabled':'\u041e\u0442\u043a\u043b\u044e\u0447\u0451\u043d','fixture-enabled':'\u0412\u043a\u043b\u044e\u0447\u0451\u043d','fixture-ack-ok':'ACK OK','fixture-ack-fail':'ACK FAIL',"
        "'fixture-timer-run':'\u0412\u0440\u0435\u043c\u044f \u0440\u0430\u0431\u043e\u0442\u044b','fixture-timer-run-hint':'\u0410\u0432\u0442\u043e\u0432\u044b\u043a\u043b\u044e\u0447\u0435\u043d\u0438\u0435 \u0447\u0435\u0440\u0435\u0437 N \u0447\u0430\u0441\u043e\u0432:\u043c\u0438\u043d\u0443\u0442:\u0441\u0435\u043a',"
        "'timer-title':'\u0422\u0430\u0439\u043c\u0435\u0440\u044b','timer-add':'\u0414\u043e\u0431\u0430\u0432\u0438\u0442\u044c \u0442\u0430\u0439\u043c\u0435\u0440','timer-edit':'\u0418\u0437\u043c\u0435\u043d\u0438\u0442\u044c','timer-delete':'\u0423\u0434\u0430\u043b\u0438\u0442\u044c',"
        "'timer-on':'\u0412\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u0432','timer-off':'\u0412\u044b\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u0432','timer-hours':'\u0427\u0430\u0441\u044b','timer-minutes':'\u041c\u0438\u043d\u0443\u0442\u044b','timer-seconds':'\u0421\u0435\u043a\u0443\u043d\u0434\u044b',"
        "'timer-days':'\u0414\u043d\u0438','timer-active':'\u0410\u043a\u0442\u0438\u0432\u0435\u043d','scenario-title':'\u0421\u0446\u0435\u043d\u0430\u0440\u0438\u0438','scenario-add':'\u0414\u043e\u0431\u0430\u0432\u0438\u0442\u044c \u0441\u0446\u0435\u043d\u0430\u0440\u0438\u0439',"
        "'scenario-interval':'\u0418\u043d\u0442\u0435\u0440\u0432\u0430\u043b (\u043c\u0438\u043d)','scenario-action':'\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u0435','scenario-run':'\u0417\u0430\u043f\u0443\u0441\u043a',"
        "'sc-en-all':'\u0412\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u0432\u0441\u0435','sc-dis-all':'\u0412\u044b\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u0432\u0441\u0435',"
        "'field-gpio':'GPIO / I2C \u0430\u0434\u0440\u0435\u0441','field-label':'\u041d\u0430\u0437\u0432\u0430\u043d\u0438\u0435 \u0434\u0430\u0442\u0447\u0438\u043a\u0430','field-uart-tx':'UART TX \u043f\u0438\u043d',"
        "'field-uart-num':'\u041d\u043e\u043c\u0435\u0440 UART (1 \u0438\u043b\u0438 2)','field-can-id':'CAN ID (hex, \u043d\u0430\u043f\u0440. 0x100)',"
        "'field-can-dlc':'CAN DLC (\u0431\u0430\u0439\u0442)','field-http-url':'HTTP POST URL \u0446\u0435\u043b\u044c',"
        "'gpio-monitor':'GPIO Monitor \u2014 ESP32 / ESP32-S3',"
        "'gpio-refresh':'\u041e\u0431\u043d\u043e\u0432\u0438\u0442\u044c','gpio-auto':'\u0410\u0432\u0442\u043e (3\u0441)',"
        "'gpio-updated':'\u041e\u0431\u043d\u043e\u0432\u043b\u0435\u043d\u043e:','gpio-error':'\u041e\u0448\u0438\u0431\u043a\u0430',"
        "'gpio-warn-reserved':'\u0417\u0430\u0440\u0435\u0437\u0435\u0440\u0432\u0438\u0440\u043e\u0432\u0430\u043d (Flash/PSRAM) \u2014 \u043d\u0435 \u0438\u0441\u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u044c!',"
        "'gpio-warn-special':'\u041e\u0441\u0442\u043e\u0440\u043e\u0436\u043d\u043e: \u0441\u043f\u0435\u0446\u0438\u0430\u043b\u044c\u043d\u0430\u044f \u0444\u0443\u043d\u043a\u0446\u0438\u044f',"
        "'gpio-mode':'\u0420\u0435\u0436\u0438\u043c','gpio-level':'\u0423\u0440\u043e\u0432\u0435\u043d\u044c','gpio-apply':'\u041f\u0440\u0438\u043c\u0435\u043d\u0438\u0442\u044c','gpio-reset-pin':'\u0421\u0431\u0440\u043e\u0441',"
        "'gpio-unset':'\u0412\u044b\u043a\u043b (unset)','gpio-in':'INPUT (\u0447\u0438\u0442\u0430\u0442\u044c)','gpio-pu':'INPUT_PULLUP',"
        "'gpio-pd':'INPUT_PULLDOWN','gpio-out-ctrl':'OUTPUT (\u0443\u043f\u0440\u0430\u0432\u043b\u044f\u0442\u044c)',"
        "'gpio-legend-hi':'INPUT HIGH','gpio-legend-lo':'INPUT LOW',"
        "'gpio-legend-out-hi':'OUTPUT HIGH','gpio-legend-out-lo':'OUTPUT LOW',"
        "'gpio-legend-warn':'\u041f\u0440\u0435\u0434\u0443\u043f\u0440\u0435\u0436\u0434\u0435\u043d\u0438\u0435','gpio-legend-danger':'\u041d\u0435\u043b\u044c\u0437\u044f \u0438\u0441\u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u044c',"
        "'gpio-load':'\u0417\u0430\u0433\u0440\u0443\u0437\u043a\u0430...',"
        "'timer-card':'GPIO \u0422\u0430\u0439\u043c\u0435\u0440\u044b / \u0420\u0430\u0441\u043f\u0438\u0441\u0430\u043d\u0438\u0435',"
        "'timer-hint':'\u0414\u043e \u0447\u0430\u0441\u043e\u0432 23 \u2014 \u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u0435\u043b\u044c\u0441\u043a\u043e\u0435 \u0432\u0440\u0435\u043c\u044f. 0\u00d70\u00d700 = \u0432\u044b\u043a\u043b\u044e\u0447\u0435\u043d\u043e. \u0414\u0435\u0439\u0441\u0442\u0432\u0438\u044f: HIGH/LOW \u2014 \u0443\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u044c \u0443\u0440\u043e\u0432\u0435\u043d\u044c, TOGGLE \u2014 \u0438\u043d\u0432\u0435\u0440\u0441\u0438\u044f, PULSE \u2014 \u0438\u043c\u043f\u0443\u043b\u044c\u0441 \u0437\u0430\u0434\u0430\u043d\u043d\u043e\u0439 \u0434\u043b\u0438\u0442\u0435\u043b\u044c\u043d\u043e\u0441\u0442\u0438.',"
        "'timer-col-on':'\u0412\u043a\u043b','timer-col-name':'\u041d\u0430\u0437\u0432\u0430\u043d\u0438\u0435','timer-col-gpio':'GPIO',"
        "'timer-col-action':'\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u0435','timer-col-dur':'\u0414\u043b\u0438\u0442. (ms)','timer-col-next':'\u0421\u043b\u0435\u0434. \u0437\u0430\u043f\u0443\u0441\u043a','timer-col-reset':'\u0421\u0431\u0440\u043e\u0441',"
        "'timer-save':'\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u0442\u0430\u0439\u043c\u0435\u0440\u044b','timer-saving':'\u0421\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u0438\u0435...','timer-saved':'\u2713 \u0421\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u043e',"
        "'timer-err':'\u274C \u041e\u0448\u0438\u0431\u043a\u0430','timer-conn-err':'\u274C \u041e\u0448\u0438\u0431\u043a\u0430 \u0441\u0432\u044f\u0437\u0438',"
        "'timer-confirm-reset':'\u0421\u0431\u0440\u043e\u0441\u0438\u0442\u044c \u0442\u0430\u0439\u043c\u0435\u0440 ','timer-now':'\u2713 \u0441\u0435\u0439\u0447\u0430\u0441',"
        "'timer-reset-btn':'\u0421\u0431\u0440\u043e\u0441 \u0442\u0430\u0439\u043c\u0435\u0440\u0430','placeholder-pump':'\u041a\u0443\u0445\u043d\u044f / \u0423\u043b\u0438\u0446\u0430 / \u0421\u0435\u0440\u0432\u0435\u0440',"
        "'ft-title':'\u0418\u043d\u0442\u0435\u0440\u0432\u0430\u043b\u044c\u043d\u044b\u0435 \u0442\u0430\u0439\u043c\u0435\u0440\u044b',"
        "'ft-desc':'\u2014 \u043f\u043e\u0432\u0442\u043e\u0440\u044f\u044e\u0442\u0441\u044f \u043a\u0430\u0436\u0434\u044b\u0435 N \u0441\u0435\u043a\u0443\u043d\u0434 \u0431\u0435\u0437 NTP. PULSE_CUSTOM: \u0432\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u043d\u0430 dur.\u043c\u0441, \u0437\u0430\u0442\u0435\u043c \u0432\u044b\u043a\u043b\u044e\u0447\u0438\u0442\u044c.',"
        "'ft-en-all':'\u0412\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u0432\u0441\u0435','ft-dis-all':'\u0412\u044b\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u0432\u0441\u0435','ft-save':'\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u0442\u0430\u0439\u043c\u0435\u0440\u044b',"
        "'ft-col-en':'\u0412\u043a\u043b','ft-col-name':'\u041d\u0430\u0437\u0432\u0430\u043d\u0438\u0435','ft-col-act':'\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u0435','ft-col-ctrl':'\u0414\u0435\u0439\u0441\u0442\u0432.',"
        "'ft-dur':'dur(\u043c\u0441)','ft-rh':'\u0420\u0447','ft-rm':'\u0420\u043c','ft-rs':'\u0420\u0441',"
        "'fixture-preset-off':'\u0412\u044b\u043a\u043b','fixture-preset-full':'\u041f\u043e\u043b\u043d\u044b\u0439','fixture-preset-grow':'\u0420\u043e\u0441\u0442',"
        "'sens-save':'\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u0438 \u043f\u0440\u0438\u043c\u0435\u043d\u0438\u0442\u044c'}"
        "};"));

    // --- Язык + Тема + Сканирование WiFi + Аптайм ---
    _server.sendContent(F(
        "var lang=localStorage.getItem('lang')||'ru';"
        "function t(k){return(TR[lang]&&TR[lang][k])||TR.en[k]||k;}"
        "function applyLang(){"
        "  document.querySelectorAll('[data-t]').forEach(function(el){"
        "    var v=t(el.dataset.t);"
        "    if(el.tagName==='INPUT')el.value=v;"
        "    else el.textContent=v;"
        "  });"
        "  document.querySelectorAll('[data-t-title]').forEach(function(el){"
        "    el.title=t(el.dataset.tTitle);"
        "  });"
        "  var lb=document.getElementById('lang-btn');"
        "  if(lb)lb.textContent=lang==='en'?'RU':'EN';"
        "}"
        "function toggleLang(){lang=lang==='en'?'ru':'en';localStorage.setItem('lang',lang);applyLang();}"
        "var theme=localStorage.getItem('theme')||'dark';"
        "function applyTheme(){"
        "  if(theme==='light')document.documentElement.classList.add('light');"
        "  else document.documentElement.classList.remove('light');"
        "  var tb=document.getElementById('theme-btn');"
        "  if(tb)tb.innerHTML=theme==='dark'?'Light':'Dark';"
        "}"
        "function toggleTheme(){theme=theme==='dark'?'light':'dark';localStorage.setItem('theme',theme);applyTheme();}"
        "function fillSsid(s){"
        "  document.querySelectorAll('[name=\"wifi_ssid\"]').forEach(function(el){el.value=s;});"
        "  document.querySelectorAll('.wifi-item').forEach(function(el){"
        "    el.style.borderColor=el.dataset.ssid===s?'var(--acc)':'';"
        "    el.style.background=el.dataset.ssid===s?'var(--bgo)':'';"
        "  });"
        "}"
        "function scanWifi(){"
        "  var el=document.getElementById('scan-res');"
        "  var btn=document.getElementById('scan-btn');"
        "  if(!el||!btn)return;"
        "  el.innerHTML='<span class=text-muted>'+t('scanning')+'</span>';"
        "  btn.disabled=true;"
        "  fetch('/api/scan').then(r=>r.json()).then(nets=>{"
        "    btn.disabled=false;"
        "    if(!nets.length){el.innerHTML='<span class=text-muted>No networks</span>';return;}"
        "    var h='<div class=text-muted style=margin-bottom:6px>'+nets.length+' '+t('net-found')+'</div>';"
        "    nets.forEach(n=>{"
        "      var sc=n.rssi>-60?'badge-green':n.rssi>-80?'badge-yellow':'badge-red';"
        "      var lock=n.enc?'':'';"
        "      var ssid=n.ssid.replace(/\"/g,'&quot;');"
        "      h+='<div class=wifi-item data-ssid=\"'+ssid+'\" onclick=\"fillSsid(this.dataset.ssid)\">';"
        "      h+='<span>'+lock+n.ssid+'</span>';"
        "      h+='<span class=\"badge '+sc+'\">'+n.rssi+'dBm</span></div>';"
        "    });"
        "    el.innerHTML=h;"
        "  }).catch(()=>{btn.disabled=false;el.innerHTML='<span class=text-muted>Error</span>';});"
        "}"
        "function upd(){"
        "  fetch('/api/data').then(r=>r.json()).then(d=>{"
        "    var u=d.uptime;var h=Math.floor(u/3600);var m=Math.floor(u%3600/60);var sec=u%60;"
        "    document.getElementById('up').textContent=h+'h '+m+'m '+sec+'s';"
        "    if(document.getElementById('live'))updateLive(d);"
        "  }).catch(e=>{});"
        "}"
        "setInterval(upd,5000);upd();"
        "applyLang();applyTheme();"
        "</script></body></html>"));
}


String WebPortal::navBar(const String& active) {
    String n;
    n.reserve(700);
    n += F("<nav>");
    n += F("<a href='/' class='brand'>ESP-HUB</a>");

    const char* items[][3] = {
        {"/",           "Dashboard",  "dashboard"},
        {"/sensors",   "Sensors",    "sensors"},
        {"/fixtures",  "Fixtures",   "fixtures"},
        {"/mesh",      "Mesh",       "mesh"},
        {"/mqtt",      "MQTT",       "mqtt"},
        {"/camera",    "ESP-CAM",    "camera"},
        {"/cron",      "CRON",       "cron"},
        {"/ai",        "AI",         "ai"},
        {"/system",    "System",     "system"},
        {"/bluetooth", "Bluetooth",  "bluetooth"},
        {"/api",       "API",        "api"}
    };
    for (int i = 0; i < 11; i++) {
        n += F("<a href='");
        n += items[i][0];
        n += F("' data-t='");
        n += items[i][2];
        n += "'";
        if (active == items[i][0]) n += F(" class='active'");
        n += ">";
        n += items[i][1];
        n += F("</a>");
    }
    n += F("<div class='nav-end'>"
           "<button class='nt-btn' id='lang-btn' onclick='toggleLang()'>EN</button>"
           "<button class='nt-btn' id='theme-btn' onclick='toggleTheme()'></button>"
           "</div>");
    n += F("</nav>");
    return n;
}

// Экранирует одинарные и двойные кавычки в значениях атрибутов HTML
static String htmlAttr(const char* s) {
    String r = s;
    r.replace("'", "&#39;");
    r.replace("\"", "&quot;");
    return r;
}

String WebPortal::inputField(const char* label, const char* name, const char* value,
                             const char* type, const char* placeholder) {
    String s;
    s += F("<label>");
    s += label;
    s += F("</label><input type='");
    s += type;
    s += F("' id='");
    s += name;
    s += F("' name='");
    s += name;
    s += F("' value='");
    s += htmlAttr(value);
    s += F("' placeholder='");
    s += htmlAttr(placeholder);
    s += F("'>");
    return s;
}

String WebPortal::numberField(const char* label, const char* name, int value,
                              int minv, int maxv) {
    String s;
    s += F("<label>");
    s += label;
    s += F("</label><input type='number' id='");
    s += name;
    s += F("' name='");
    s += name;
    s += F("' value='");
    s += value;
    s += F("' min='");
    s += minv;
    s += F("' max='");
    s += maxv;
    s += F("'>");
    return s;
}

String WebPortal::selectField(const char* label, const char* name,
                              const char** options, int count, int selected) {
    String s;
    s += F("<label>");
    s += label;
    s += F("</label><select name='");
    s += name;
    s += F("'>");
    for (int i = 0; i < count; i++) {
        s += F("<option value='");
        s += i;
        s += "'";
        if (i == selected) s += F(" selected");
        s += ">";
        s += options[i];
        s += F("</option>");
    }
    s += F("</select>");
    return s;
}

String WebPortal::checkboxField(const char* label, const char* name, bool checked) {
    String s;
    s += F("<div style='display:flex;align-items:center;gap:10px;margin:8px 0'>"
           "<label class='toggle'><input type='checkbox' name='");
    s += name;
    s += F("' value='1'");
    if (checked) s += F(" checked");
    s += F("><span class='slider'></span></label><span>");
    s += label;
    s += F("</span></div>");
    return s;
}

String WebPortal::submitButton(const char* text, const char* cls) {
    String s;
    s += F("<div class='mt'><button type='submit' class='btn ");
    s += cls;
    s += F("'>");
    s += text;
    s += F("</button></div>");
    return s;
}

String WebPortal::card(const char* title, const String& content) {
    String s;
    s += F("<div class='card'><h3>");
    s += title;
    s += F("</h3>");
    s += content;
    s += F("</div>");
    return s;
}

String WebPortal::badge(const char* text, const char* cls) {
    String s;
    s += F("<span class='badge ");
    s += cls;
    s += F("'>");
    s += text;
    s += F("</span>");
    return s;
}

void WebPortal::startPage(const String& title) {
    String path = _server.uri();
    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.sendHeader("Connection", "close");
    _server.sendHeader("Content-Security-Policy", "script-src 'self' 'unsafe-inline'");
    _server.sendHeader("Permissions-Policy", "geolocation=(), microphone=(), camera=()");
    _server.send(200, "text/html", "<!DOCTYPE html><html><head>");
    sendPageHeader(title);
    _server.sendContent(navBar(path));
    _server.sendContent(F("<div class='wrap'>"));
}

void WebPortal::endPage() {
    _server.sendContent(F("</div>"));
    sendPageFooter();  // стримит ~6 КБ JS напрямую из flash, без сборки String в куче
    _server.sendContent("");  // Финальный пустой кусок — завершает chunked encoding
    _server.client().stop();  // Корректное закрытие: lwIP отправляет буфер и FIN
}

void WebPortal::sendPage(const String& title, const String& body) {
    startPage(title);
    _server.sendContent(body);
    endPage();
}

// ================================================================
//                       СТРАНИЦА: ПАНЕЛЬ (Dashboard)
// ================================================================

void WebPortal::handleRoot() {
    // ── Карточка статуса ───────────────────────────────────────────────────
    String status;
    status.reserve(1800);
    status += F("<div class='grid2'>");

    // WiFi + NAT
    status += F("<div>");
    status += F("<div class='flex-between'><span>WiFi</span>");
    if (_wifi->isConnected()) {
        status += badge("Подключён", "badge-green");
    } else if (_wifi->isAP()) {
        status += badge("AP режим", "badge-yellow");
    } else {
        status += badge("Нет связи", "badge-red");
    }
    status += F("</div>");
    if (_wifi->isConnected()) {
        status += F("<div class='text-muted'>IP: ");
        status += _wifi->localIP();
        status += F(" &bull; RSSI: ");
        status += _wifi->rssi();
        status += F(" dBm</div>");
    } else if (_wifi->isAP()) {
        status += F("<div class='text-muted'>AP IP: ");
        status += _wifi->apIP();
        if (_wifi->apClientsCount() > 0) {
            status += F(" &bull; ");
            status += _wifi->apClientsCount();
            status += F(" кл.");
        }
        status += F("</div>");
    }
    status += F("</div>");

    // MQTT
    status += F("<div>");
    status += F("<div class='flex-between'><span>MQTT</span>");
    if (_mqtt->isConnected()) {
        status += badge("Подключён", "badge-green");
    } else if (strlen(_cfg->cfg.mqtt_host) > 0) {
        status += badge("Нет связи", "badge-red");
    } else {
        status += badge("Не настроен", "badge-blue");
    }
    status += F("</div>");
    if (strlen(_cfg->cfg.mqtt_host) > 0) {
        status += F("<div class='text-muted'>");
        status += _cfg->cfg.mqtt_host;
        status += ':';
        status += _cfg->cfg.mqtt_port;
        status += F("</div>");
    }
    status += F("</div>");

    // BLE
    status += F("<div>");
    status += F("<div class='flex-between'><span>Bluetooth</span>");
    if (!_ble->isEnabled()) {
        status += badge("Отключён", "badge-blue");
    } else if (_ble->isConnected()) {
        status += badge("Клиент подключён", "badge-green");
    } else {
        status += badge("Ожидание", "badge-yellow");
    }
    status += F("</div>");
    if (_ble->isEnabled()) {
        status += F("<div class='text-muted'>");
        status += (strlen(_cfg->cfg.ble_name) > 0) ? _cfg->cfg.ble_name : _cfg->cfg.device_name;
        status += F("</div>");
    }
    status += F("</div>");

    // NAT
    status += F("<div>");
    status += F("<div class='flex-between'><span>NAT</span>");
    if (_wifi->isNatEnabled()) {
        status += badge("Включён", "badge-green");
    } else if (_cfg->cfg.ap_nat) {
        status += badge("Ожидает WiFi", "badge-yellow");
    } else {
        status += badge("Выключен", "badge-blue");
    }
    status += F("</div>");
    status += F("<div class='text-muted' style='font-size:11px'>"
                "<a href='/system'>Настройки AP</a> &bull; ");
    status += F("<button class='btn btn-secondary' style='padding:2px 8px;font-size:11px' "
                "onclick='natToggle()'>Переключить</button></div>");
    status += F("</div>");

    // Mesh
    status += F("<div>");
    status += F("<div class='flex-between'><span>Mesh</span>");
    if (!_cfg->cfg.mesh_enabled) {
        status += badge("Отключен", "badge-blue");
    } else if (meshMgr.isConnected()) {
        uint32_t nodes = meshMgr.getConnectedCount();
        char nb[40];
        snprintf(nb, sizeof(nb), "%u узлов", nodes);
        status += badge(nb, "badge-green");
    } else {
        status += badge("Ожидание", "badge-yellow");
    }
    status += F("</div>");
    if (_cfg->cfg.mesh_enabled) {
        status += F("<div class='text-muted'>");
        status += F("ID: 0x");
        char idb[16];
        snprintf(idb, sizeof(idb), "%X", meshMgr.getNodeId());
        status += idb;
        status += F("</div>");
    }
    status += F("</div>");

    // AI Agent
    static const char* provNames[] = {"LM Studio","Ollama","OpenAI","OpenRouter","Anthropic"};
    status += F("<div>");
    status += F("<div class='flex-between'><span>ИИ Агент</span>");
    if (_cfg->cfg.ai_enabled) {
        status += badge("Включён", "badge-green");
    } else {
        status += badge("Выключен", "badge-blue");
    }
    status += F("</div>");
    if (_cfg->cfg.ai_enabled) {
        status += F("<div class='text-muted'>");
        uint8_t p = _cfg->cfg.ai_provider;
        status += (p < 5) ? provNames[p] : "?";
        if (_cfg->cfg.ai_model[0]) {
            status += F(" / ");
            status += _cfg->cfg.ai_model;
        }
        status += F("</div>");
    }
    status += F("</div>");

    // CRON
    status += F("<div>");
    status += F("<div class='flex-between'><span>CRON</span>");
    if (_cfg->cfg.cron_enabled) {
        int nc = cronMgr.count();
        if (nc > 0) {
            char nb[32]; snprintf(nb, sizeof(nb), "%d задач", nc);
            status += badge(nb, "badge-green");
        } else {
            status += badge("Нет задач", "badge-yellow");
        }
    } else {
        status += badge("Выключен", "badge-blue");
    }
    status += F("</div>");
    if (_cfg->cfg.cron_enabled) {
        status += F("<div class='text-muted'>");
        char tzb[32]; cronMgr.getTimezone(tzb, sizeof(tzb));
        status += tzb;
        if (!cronMgr.isTimeSynced()) status += F(" <span style='color:#d29922'>⚠ NTP не синхр.</span>");
        status += F("</div>");
    }
    status += F("</div>");

    // Rate Limiter
    if (_cfg->cfg.rl_enabled) {
        status += F("<div>");
        status += F("<div class='flex-between'><span>Rate Limiter</span>");
        status += badge("Включён", "badge-yellow");
        status += F("</div>");
        status += F("<div class='text-muted'>");
        status += rateLimiter.requestsThisHour();
        status += '/';
        status += _cfg->cfg.rl_max_hour;
        status += F(" час &bull; ");
        status += rateLimiter.requestsToday();
        status += '/';
        status += _cfg->cfg.rl_max_day;
        status += F(" день</div>");
        status += F("</div>");
    }

    status += F("</div>"); // end grid2

    // NAT toggle JS
    status += F("<script>"
                "function natToggle(){"
                "  if(!confirm('Переключить NAT и перезагрузить?'))return;"
                "  fetch('/api/nat/toggle',{method:'POST'})"
                "  .then(function(r){return r.json();})"
                "  .then(function(d){alert(d.message||'OK');});"
                "}"
                "</script>");

    // ── Overview card ────────────────────────────────────────────────────────
    String ov;
    ov.reserve(900);
    ov += F("<div class='grid2'>");

    // Sensors
    ov += F("<div><div class='flex-between'><span>Датчики</span>");
    int sActive = 0;
    for (int i = 0; i < MAX_SENSORS; i++)
        if (_cfg->cfg.sensors[i].enabled) sActive++;
    if (sActive > 0) {
        char sb[40]; snprintf(sb, sizeof(sb), "%d активных", sActive);
        ov += badge(sb, "badge-green");
    } else {
        ov += badge("Не настроены", "badge-blue");
    }
    ov += F("</div><div class='text-muted' style='font-size:11px'>");
    bool first = true;
    for (int i = 0; i < MAX_SENSORS; i++) {
        const SensorConfig& sc = _cfg->cfg.sensors[i];
        if (!sc.enabled) continue;
        if (!first) ov += F(", ");
        ov += (sc.label[0] ? sc.label : sensorTypeName(sc.type));
        first = false;
    }
    if (first) ov += F("&mdash;");
    ov += F("</div></div>");

    // ESP-CAM
    ov += F("<div><div class='flex-between'><span>ESP-CAM</span>");
    if (_cfg->cfg.cam_url[0]) {
        ov += badge("Настроена", "badge-green");
    } else {
        ov += badge("Не настроена", "badge-blue");
    }
    ov += F("</div><div class='text-muted' style='font-size:11px'>");
    if (_cfg->cfg.cam_url[0]) {
        ov += F("<a href='/camera'>");
        ov += _cfg->cfg.cam_url;
        ov += F("</a>");
    } else {
        ov += F("<a href='/camera'>Настроить</a>");
    }
    ov += F("</div></div>");

    // Lighting
    ov += F("<div><div class='flex-between'><span>Светильник</span>");
    if (_cfg->cfg.fixture.enabled) {
        if (_fixture && _fixture->isEnabled()) {
            ov += badge("Активен", "badge-green");
        } else {
            ov += badge("Настроен", "badge-yellow");
        }
    } else {
        ov += badge("Отключён", "badge-blue");
    }
    ov += F("</div><div class='text-muted' style='font-size:11px'>");
    if (_cfg->cfg.fixture.enabled && _fixture && _fixture->isEnabled()) {
        ov += F("R:");  ov += _fixture->getRed();
        ov += F(" FR:"); ov += _fixture->getFarRed();
        ov += F(" B:");  ov += _fixture->getBlue();
        ov += F(" W:");  ov += _fixture->getWhite();
        ov += F(" <a href='/fixtures'>&#9998;</a>");
    } else {
        ov += F("<a href='/fixtures'>Управление</a>");
    }
    ov += F("</div></div>");

    // AI Rate Limiter (quick stats when disabled show nothing)
    ov += F("<div><div class='flex-between'><span>AI запросы</span>");
    if (_cfg->cfg.ai_enabled) {
        ov += badge("Активен", "badge-green");
    } else {
        ov += badge("Выключен", "badge-blue");
    }
    ov += F("</div><div class='text-muted' style='font-size:11px'>");
    if (_cfg->cfg.ai_enabled) {
        ov += F("Сегодня: "); ov += rateLimiter.requestsToday();
        ov += F(" &bull; <a href='/ai'>Чат</a>");
    } else {
        ov += F("<a href='/ai'>Включить</a>");
    }
    ov += F("</div></div>");

    ov += F("</div>"); // end grid2

    // ── Live Telemetry ───────────────────────────────────────────────────────
    String live;
    live += F("<div id='live' class='grid2'>");

    int sensorCount = 0;
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        SensorBase* s = _sensors->getSensor(i);
        if (!s || !s->isReady()) continue;

        for (uint8_t v = 0; v < s->valueCount(); v++) {
            const SensorValue& sv = s->getValue(v);
            if (!sv.valid) continue;

            sensorCount++;
            live += F("<div class='s-row'><div class='s-hdr'>"
                      "<span class='s-label'>");
            live += _cfg->cfg.sensors[i].label;
            live += F(" &middot; ");
            live += sv.name;
            live += F("</span></div><div><span class='s-val' id='v_");
            live += i;
            live += '_';
            live += sv.name;
            live += F("'>");
            live += String(sv.value, 1);
            live += F("</span><span class='s-unit'>");
            live += sv.unit;
            live += F("</span></div></div>");
        }
    }

    if (sensorCount == 0) {
        live += F("<div style='padding:20px;text-align:center;color:var(--txt2)'>"
                  "<p>Датчики не подключены</p>"
                  "<p style='font-size:12px'>Настройте датчики во вкладке Sensors</p>"
                  "</div>");
    }

    live += F("</div>");
    live += F("<script>"
              "function updateLive(d){"
              "  if(!d.sensors)return;"
              "  for(var k in d.sensors){"
              "    var el=document.getElementById('v_'+k.replace('_','_'));"
              "    if(el)el.textContent=parseFloat(d.sensors[k]).toFixed(1);"
              "  }"
              "}"
              "</script>");

    startPage("Dashboard");
    _server.sendContent(card("<span data-t='status'>Status</span>", status));
    _server.sendContent(card("&#128202; Overview", ov));
    _server.sendContent(card("<span data-t='livetel'>Live Telemetry</span>", live));
    _server.sendContent(wifiConnectCard());
    endPage();
}

// ================================================================
//                       WIFI CONNECT CARD (shared widget)
// ================================================================

String WebPortal::wifiConnectCard() {
    String w;
    w.reserve(800);

    // Current status row
    w += F("<div class='flex-between mb'>");
    w += F("<div>");
    if (_wifi->isConnected()) {
        w += badge("<span data-t='wifi-connected'>\u041F\u043E\u0434\u043A\u043B\u044E\u0447\u0435\u043D</span>", "badge-green");
        w += F(" <span class='text-muted'>");
        w += _cfg->cfg.wifi_ssid;
        w += F(" &bull; ");
        w += _wifi->localIP();
        w += F(" &bull; ");
        w += _wifi->rssi();
        w += F(" dBm</span>");
    } else if (_wifi->isAP()) {
        w += badge("<span data-t='ap-mode'>AP \u0440\u0435\u0436\u0438\u043C</span>", "badge-yellow");
        w += F(" <span class='text-muted'>192.168.4.1</span>");
    } else {
        w += badge("<span data-t='no-conn'>\u041D\u0435\u0442 \u0441\u0432\u044F\u0437\u0438</span>", "badge-red");
    }
    w += F("</div></div>");

    // Scan button + results
    w += F("<div class='mb flex-between'>"
           "<div>"
           "<button type='button' class='btn btn-secondary' id='scan-btn' onclick='scanWifi()'>"
           "<span data-t='scan'>Scan WiFi</span></button>"
           " <span class='text-muted' data-t='scan-hint'>Click to select</span>"
           "</div></div>"
           "<div id='scan-res' class='mb'></div>");

    // SSID + pass form
    w += F("<form method='POST' action='/save/wifi'>");
    w += inputField("SSID", "wifi_ssid", _cfg->cfg.wifi_ssid, "text", "\u0418\u043C\u044F \u0441\u0435\u0442\u0438");
    // Never pre-fill password — forces user to type actual password, prevents browser autofill corruption
    {
        const char* phPass = (strlen(_cfg->cfg.wifi_pass) > 0)
            ? "\u041E\u0441\u0442\u0430\u0432\u044C\u0442\u0435 \u043F\u0443\u0441\u0442\u044B\u043C \u2014 \u043D\u0435 \u043C\u0435\u043D\u044F\u0442\u044C \u043F\u0430\u0440\u043E\u043B\u044C"
            : "\u041F\u0430\u0440\u043E\u043B\u044C";
        w += inputField("<span data-t='password'>\u041F\u0430\u0440\u043E\u043B\u044C</span>",
                        "wifi_pass", "", "password", phPass);
    }
    w += submitButton("<span data-t='save-wifi'>\u0421\u043E\u0445\u0440\u0430\u043D\u0438\u0442\u044C WiFi \u0438 \u043F\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044C</span>");
    w += F("</form>");

    return card("<span data-t='wificonf'>WiFi Configuration</span>", w);
}

// ================================================================
//                       GPIO PIN TABLES  (used by timer & GPIO pages)
// ================================================================

struct GpioInfo { uint8_t pin; const char* note; uint8_t w; };

// ESP32-S3: 45 GPIO pins (GPIO0-21, GPIO26-48)
static const GpioInfo kGpioS3[] = {
    {0,"Strap/RTC",1},{1,"ADC1-CH0/RTC",0},{2,"ADC1-CH1/RTC",0},
    {3,"ADC1-CH2/Strap",1},{4,"ADC1-CH3/RTC",0},{5,"ADC1-CH4/RTC",0},
    {6,"ADC1-CH5/RTC",0},{7,"ADC1-CH6/RTC",0},{8,"ADC1-CH7/RTC",0},
    {9,"ADC1-CH8/RTC",0},{10,"ADC1-CH9/RTC",0},{11,"ADC2-CH0/RTC",0},
    {12,"ADC2-CH1/RTC",0},{13,"ADC2-CH2/RTC",0},{14,"ADC2-CH3/RTC",0},
    {15,"ADC2-CH4/RTC",0},{16,"ADC2-CH5/RTC",0},{17,"ADC2-CH6/RTC",0},
    {18,"ADC2-CH7/RTC",0},{19,"ADC2-CH8/USB-JTAG",1},{20,"ADC2-CH9/USB-JTAG",1},
    {21,"RTC",0},
    {26,"SPI-Flash",2},{27,"SPI-Flash",2},{28,"SPI-Flash",2},
    {29,"SPI-Flash",2},{30,"SPI-Flash",2},{31,"SPI-Flash",2},
    {32,"SPI-Flash",2},{33,"SPI-Octal",2},{34,"SPI-Octal",2},
    {35,"SPI-Octal",2},{36,"SPI-Octal",2},{37,"SPI-Octal",2},
    {38,"GPIO",0},{39,"GPIO",0},{40,"GPIO",0},{41,"GPIO",0},{42,"GPIO",0},
    {43,"UART0-TX",1},{44,"UART0-RX",1},{45,"Strap",1},{46,"Strap",1},
    {47,"GPIO",0},{48,"GPIO",0}
};

// Original ESP32: GPIO0-39
static const GpioInfo kGpioESP32[] = {
    {0,"Boot/Strap",1},{1,"UART0-TX",1},{2,"ADC/Strap",1},{3,"UART0-RX",1},
    {4,"ADC2/Touch",0},{5,"SPI-SS/Strap",1},
    {6,"SPI-Flash",2},{7,"SPI-Flash",2},{8,"SPI-Flash",2},
    {9,"SPI-Flash",2},{10,"SPI-Flash",2},{11,"SPI-Flash",2},
    {12,"JTAG/ADC",1},{13,"JTAG/ADC",1},{14,"JTAG/ADC",1},{15,"JTAG/ADC",1},
    {16,"PSRAM",1},{17,"PSRAM",1},{18,"SPI-SCK",0},{19,"SPI-MISO",0},
    {21,"I2C-SDA",0},{22,"I2C-SCL",0},{23,"SPI-MOSI",0},
    {25,"DAC1",0},{26,"DAC2",0},{27,"ADC/Touch",0},
    {32,"ADC/Touch",0},{33,"ADC/Touch",0},
    {34,"ADC/IN-ONLY",1},{35,"ADC/IN-ONLY",1},
    {36,"ADC/IN-ONLY",1},{37,"ADC/IN-ONLY",1},
    {38,"ADC/IN-ONLY",1},{39,"ADC/IN-ONLY",1}
};

// ================================================================
//                       PAGE: SENSORS
// ================================================================

void WebPortal::handleSensors() {
    const char* sensorTypes[] = {
        "None",
        "DHT11","DHT22","DS18B20","Analog",
        "BMP280","BH1750",
        "MH-Z19 CO2","SDS011 Dust","UART Generic",
        "CAN Raw",
        "MH-Sensor"
    };
    const char* busTypes[]  = {"Auto","GPIO","I2C","1-Wire","UART","CAN"};
    const char* outProtos[] = {"MQTT","HTTP POST","CAN TX","Serial"};

    startPage("Sensors");
    // Card open + CSS for slot accordion
    _server.sendContent(F("<div class='card'><h3><span data-t='sensconf'>Sensor Configuration</span></h3>"));
    // Hint: explain what a Slot is
    _server.sendContent(F("<div class='text-muted mb' style='font-size:12px;padding:4px 2px 8px'>"
        ""
        "<span data-t='slot-hint-text'>\u0421\u043b\u043e\u0442 \u2014 \u043d\u0435\u0437\u0430\u0432\u0438\u0441\u0438\u043c\u044b\u0439 \u043a\u0430\u043d\u0430\u043b \u0434\u0430\u0442\u0447\u0438\u043a\u0430. \u0412\u0441\u0435\u0433\u043e 8 \u0441\u043b\u043e\u0442\u043e\u0432 \u2014 \u043a\u0430\u0436\u0434\u044b\u0439 \u043d\u0430\u0441\u0442\u0440\u0430\u0438\u0432\u0430\u0435\u0442\u0441\u044f \u043d\u0435\u0437\u0430\u0432\u0438\u0441\u0438\u043c\u043e: \u0442\u0438\u043f \u0434\u0430\u0442\u0447\u0438\u043a\u0430, \u0448\u0438\u043d\u0430 (I\u00B2C/UART/CAN), GPIO \u0438 \u043f\u0440\u043e\u0442\u043e\u043a\u043e\u043b \u0432\u044b\u0432\u043e\u0434\u0430.</span>"
        "</div>"));
    _server.sendContent(F("<style>"
        ".slot{background:var(--bg2);border:1px solid var(--brd);border-radius:8px;margin:8px 0}"
        ".slot-hdr{display:flex;align-items:center;gap:10px;padding:10px 14px;"
        "cursor:pointer;user-select:none;flex-wrap:wrap}"
        ".slot-hdr:hover{background:var(--bgo)}"
        ".slot-num{font-weight:700;font-size:13px;min-width:54px;color:var(--txt2)}"
        ".slot-type{font-size:13px;flex:1;color:var(--txt)}"
        ".slot-lbl{font-size:12px;color:var(--txt3);font-style:italic}"
        ".slot-body{padding:14px;border-top:1px solid var(--brd2);display:none}"
        ".slot-body.open{display:block}"
        ".chevron{margin-left:auto;font-size:11px;color:var(--txt2);transition:transform .2s}"
        ".badge-unused{background:var(--bg3);color:var(--txt2);border:1px solid var(--brd2);"
        "font-size:11px;padding:1px 7px;border-radius:10px}"
        "</style>"));
    _server.sendContent(F("<form id='sensors-form'>"));

    // Global sensor settings
    _server.sendContent(numberField("\u0418\u043d\u0442\u0435\u0440\u0432\u0430\u043b \u0432\u044b\u0432\u043e\u0434\u0430 (\u0441\u0435\u043a)", "sensor_interval", _cfg->cfg.sensor_interval_s, 1, 3600));

    for (int i = 0; i < MAX_SENSORS; i++) {
        String form;
        SensorConfig& sc = _cfg->cfg.sensors[i];
        String si = String(i);
        bool configured = sc.enabled && sc.type != SENSOR_NONE;
        SensorBase* sensor = _sensors->getSensor(i);
        bool active = sensor && sensor->isReady();

        // Slot header
        form += F("<div class='slot'>"
                  "<div class='slot-hdr' onclick='slotToggle(");
        form += i;
        form += F(")'>"
                  "<span class='slot-num' title='\u041d\u0435\u0437\u0430\u0432\u0438\u0441\u0438\u043c\u044b\u0439 \u043a\u0430\u043d\u0430\u043b: \u0441\u0432\u043e\u0439 \u0434\u0430\u0442\u0447\u0438\u043a, GPIO, \u0448\u0438\u043d\u0430 \u0438 \u043f\u0440\u043e\u0442\u043e\u043a\u043e\u043b'><span data-t='slot-label'>\u0421\u043b\u043e\u0442</span>\u00A0");
        form += i;
        form += F("</span>");

        // Status badge
        if (active)            form += badge("<span data-t='slot-active'>\u0410\u043A\u0442\u0438\u0432\u0435\u043D</span>", "badge-green");
        else if (configured)   form += badge("<span data-t='slot-error'>\u041E\u0448\u0438\u0431\u043A\u0430</span>",  "badge-red");
        else                   form += F("<span class='badge-unused'><span data-t='slot-free'>\u0421\u0432\u043E\u0431\u043E\u0434\u0435\u043D</span></span>");

        // Current type label
        form += F("<span class='slot-type'>");
        if (sc.type > 0 && sc.type < SENSOR_TYPE_COUNT) {
            form += sensorTypes[sc.type];
        } else {
            form += F("&mdash;");
        }
        form += F("</span>");

        // User label
        if (strlen(sc.label) > 0) {
            form += F("<span class='slot-lbl'>&ldquo;");
            form += sc.label;
            form += F("&rdquo;</span>");
        }

        form += F("<span class='chevron' id='ch");
        form += i;
        form += F("'></span>"
                  "</div>"); // slot-hdr

        // Slot body (open by default if configured)
        form += F("<div class='slot-body");
        if (configured) form += F(" open");
        form += F("' id='sb");
        form += i;
        form += F("'><div class='grid2'>");

        // Enable toggle
        form += checkboxField("\u0412\u043A\u043B\u044E\u0447\u0451\u043D", ("en_"+si).c_str(), sc.enabled);

        // Sensor type
        form += selectField("\u0422\u0438\u043F \u0434\u0430\u0442\u0447\u0438\u043A\u0430", ("type_"+si).c_str(),
                            sensorTypes, SENSOR_TYPE_COUNT, sc.type);

        // Bus type
        form += selectField("\u0428\u0438\u043D\u0430", ("bus_"+si).c_str(),
                            busTypes, BUS_TYPE_COUNT, sc.bus);

        // Output protocol
        form += selectField("\u0412\u044B\u0432\u043E\u0434", ("out_"+si).c_str(),
                            outProtos, OUT_PROTOCOL_COUNT, sc.outProto);

        // Pin / I2C addr
        form += numberField("GPIO / I2C \u0430\u0434\u0440\u0435\u0441", ("pin_"+si).c_str(), sc.pin, 0, 255);

        // Label
        form += inputField("\u041D\u0430\u0437\u0432\u0430\u043D\u0438\u0435 \u0434\u0430\u0442\u0447\u0438\u043A\u0430", ("label_"+si).c_str(), sc.label, "text", "\u041A\u0443\u0445\u043D\u044F / \u0443\u043B\u0438\u0446\u0430 / \u0441\u0435\u0440\u0432\u0435\u0440");

        form += F("</div>"); // grid2

        // ADC2 warning (скрыт по умолчанию, показывается JS когда тип=Analog + ADC2 GPIO)
        form += F("<div id='adc2warn_");
        form += i;
        form += F("' style='display:none;margin:6px 0;padding:8px 12px;"
                  "background:#2d1f00;border:1px solid #d29922;border-radius:6px;"
                  "font-size:12px;color:#e3b341'>"
                  "&#9888; <b>ADC2 GPIO \u2014 \u043d\u0435 \u0440\u0430\u0431\u043e\u0442\u0430\u0435\u0442 \u0441 WiFi!</b><br>"
                  "\u041f\u0438\u043d\u044b ADC2: GPIO&nbsp;0,2,4,12-15,25-27 \u2014 \u0447\u0442\u0435\u043d\u0438\u0435 \u0431\u043b\u043e\u043a\u0438\u0440\u0443\u0435\u0442\u0441\u044f \u043f\u0440\u0438 \u0432\u043a\u043b\u044e\u0447\u0451\u043d\u043d\u043e\u043c WiFi.<br>"
                  "\u0414\u043b\u044f \u0430\u043d\u0430\u043b\u043e\u0433\u043e\u0432\u044b\u0445 \u0434\u0430\u0442\u0447\u0438\u043a\u043e\u0432 (\u0444\u043e\u0442\u043e\u0440\u0435\u0437\u0438\u0441\u0442\u043e\u0440, LDR) \u0438\u0441\u043f\u043e\u043b\u044c\u0437\u0443\u0439\u0442\u0435 <b>GPIO&nbsp;32\u201339 (ADC1)</b>."
                  "</div>");

        // MH-Sensor DO pin field (shown only when type = MH-Sensor)
        form += F("<div id='mhdo_");
        form += i;
        form += F("' style='display:none'><div class='grid2'>");
        form += numberField("DO \u043f\u0438\u043d (\u0446\u0438\u0444\u0440\u043e\u0432\u043e\u0439, 0 = \u043d\u0435 \u043f\u043e\u0434\u043a\u043b\u044e\u0447\u0451\u043d)",
                            ("pin2_"+si).c_str(), sc.pin2, 0, 39);
        form += F("</div><p style='margin:2px 6px 6px;font-size:11px;color:var(--txt2)'>"
                  "AO \u2192 GPIO&nbsp;<b>pin</b> (\u0430\u043d\u0430\u043b\u043e\u0433), "
                  "DO \u2192 GPIO&nbsp;<b>DO pin</b> (\u0446\u0438\u0444\u0440\u043e\u0432\u043e\u0439 \u043f\u043e\u0440\u043e\u0433)</p>");
        form += F("</div>");

        // UART fields
        form += F("<div id='uart_");
        form += i;
        form += F("' style='display:none'><div class='grid2'>");
        form += numberField("UART TX \u043F\u0438\u043D", ("pin2_"+si).c_str(), sc.pin2, 0, 39);
        form += numberField("\u041D\u043E\u043C\u0435\u0440 UART (1 \u0438\u043B\u0438 2)", ("uart_"+si).c_str(), sc.uartNum, 1, 2);
        form += F("</div></div>");

        // CAN fields
        form += F("<div id='can_");
        form += i;
        form += F("' style='display:none'><div class='grid2'>");
        char canIdBuf[12];
        snprintf(canIdBuf, sizeof(canIdBuf), "0x%lX", (unsigned long)sc.canId);
        form += inputField("CAN ID (hex, \u043d\u0430\u043f\u0440. 0x100)", ("cid_"+si).c_str(), canIdBuf);
        form += numberField("CAN DLC (\u0431\u0430\u0439\u0442)", ("cdlc_"+si).c_str(), sc.canDlc, 1, 8);
        form += F("</div></div>");

        // HTTP URL
        form += F("<div id='http_");
        form += i;
        form += F("' style='display:none'>");
        form += inputField("HTTP POST URL \u0446\u0435\u043B\u044C", ("hurl_"+si).c_str(), sc.httpUrl,
                           "text", "http://192.168.1.10/webhook");
        form += F("</div>");

        form += F("</div></div>"); // slot-body + slot
        _server.sendContent(form);
    }

    // JS + submit button
    String jsSubmit;
    jsSubmit.reserve(600);
    jsSubmit += F("<script>"
        "function slotToggle(i){"
        "  var b=document.getElementById('sb'+i);"
        "  var c=document.getElementById('ch'+i);"
        "  b.classList.toggle('open');"
        "  c.textContent=b.classList.contains('open')?'\u25B2':'\u25BC';"
        "}"
        "function sUpd(i){"
        "  var b=document.querySelector('[name=bus_'+i+']').value;"
        "  var o=document.querySelector('[name=out_'+i+']').value;"
        "  var tp=document.querySelector('[name=type_'+i+']').value;"
        "  var pin=parseInt(document.querySelector('[name=pin_'+i+']').value||'0');"
        "  document.getElementById('uart_'+i).style.display=(b=='4'?'block':'none');"
        "  document.getElementById('can_'+i).style.display=(b=='5'||o=='2'?'block':'none');"
        "  document.getElementById('http_'+i).style.display=(o=='1'?'block':'none');"
        "  var mhEl=document.getElementById('mhdo_'+i);"
        "  if(mhEl) mhEl.style.display=(tp=='11'?'block':'none');"
        // ADC2 warning: Analog (4) или MH-Sensor (11) + ADC2 GPIO
        "  var adc2=[0,2,4,12,13,14,15,25,26,27];"
        "  var isAnalog=(tp=='4'||tp=='11');"
        "  var isAdc2=isAnalog&&adc2.indexOf(pin)>=0;"
        "  var w=document.getElementById('adc2warn_'+i);"
        "  if(w) w.style.display=isAdc2?'block':'none';"
        "}"
    );
    for (int i = 0; i < MAX_SENSORS; i++) {
        jsSubmit += "document.querySelector(\"[name=bus_";
        jsSubmit += i;
        jsSubmit += "]\").addEventListener('change',function(){sUpd(";
        jsSubmit += i;
        jsSubmit += ")});";
        jsSubmit += "document.querySelector(\"[name=out_";
        jsSubmit += i;
        jsSubmit += "]\").addEventListener('change',function(){sUpd(";
        jsSubmit += i;
        jsSubmit += ")});";
        jsSubmit += "document.querySelector(\"[name=type_";
        jsSubmit += i;
        jsSubmit += "]\").addEventListener('change',function(){sUpd(";
        jsSubmit += i;
        jsSubmit += ")});";
        jsSubmit += "document.querySelector(\"[name=pin_";
        jsSubmit += i;
        jsSubmit += "]\").addEventListener('input',function(){sUpd(";
        jsSubmit += i;
        jsSubmit += ")});";
        jsSubmit += "sUpd(";
        jsSubmit += i;
        jsSubmit += ");";
    }
    jsSubmit += F("</script>");
    jsSubmit += F("<div style='margin-top:14px;display:flex;align-items:center;gap:12px'>");
    jsSubmit += F("<button type='button' class='btn' onclick='saveSensors()'><span data-t='sens-save'>\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u0438 \u043f\u0440\u0438\u043c\u0435\u043d\u0438\u0442\u044c</span></button>");
    jsSubmit += F("<span id='sens-save-status' style='font-size:13px'></span>");
    jsSubmit += F("</div>");
    jsSubmit += F("<script>"
        "function saveSensors(){"
        "  var st=document.getElementById('sens-save-status');"
        "  if(st){st.textContent=t('timer-saving');st.style.color='var(--txt2)';}"
        "  var fd=new URLSearchParams(new FormData(document.getElementById('sensors-form')));"
        "  fetch('/save/sensors',{method:'POST',body:fd,headers:{'Content-Type':'application/x-www-form-urlencoded'}})"
        "  .then(function(r){return r.json();})"
        "  .then(function(d){"
        "    if(st){st.textContent=d.ok?t('timer-saved'):t('timer-err');"
        "           st.style.color=d.ok?'#3fb950':'#f85149';}"
        "  })"
        "  .catch(function(){"
        "    if(st){st.textContent=t('timer-conn-err');st.style.color='#f85149';}"
        "  });"
        "}"
        "</script>");
    _server.sendContent(jsSubmit);
    _server.sendContent(F("</form></div>"));

    // ---- GPIO Monitor card ----
    _server.sendContent(F("<div class='card'><h3><span data-t='gpio-monitor'>GPIO Monitor &mdash; ESP32 / ESP32-S3</span></h3>"));
    _server.sendContent(F(
        "<style>"
        "#gpio-grid{display:flex;flex-wrap:wrap;gap:5px;margin:10px 0}"
        ".gp{width:58px;min-height:46px;border-radius:6px;cursor:pointer;display:flex;"
        "flex-direction:column;align-items:center;justify-content:center;font-size:11px;"
        "font-weight:600;border:2px solid transparent;transition:border-color .15s,background .15s;"
        "user-select:none;padding:3px 2px;text-align:center}"
        ".gp:hover{border-color:#58a6ff}"
        ".gp.high{background:#1c4d2e;color:#3fb950}"
        ".gp.low{background:#1c2a3a;color:#79c0ff}"
        ".gp.out-high{background:#2d4a1e;color:#7ee787;border-style:dashed}"
        ".gp.out-low{background:#1a1e24;color:#6e7681;border-style:dashed}"
        ".gp.warn{background:#3d2e14;color:#e3b341}"
        ".gp.danger{background:#3d1414;color:#f85149}"
        ".gp.unset{background:var(--bg3);color:var(--txt2)}"
        ".gp .gpn{font-size:13px;font-weight:800}"
        ".gp .gpnt{font-size:9px;font-weight:400;opacity:.7;line-height:1.1}"
        "#gpio-detail{background:var(--bg2);border:1px solid var(--brd);border-radius:8px;"
        "padding:14px;margin:8px 0;display:none}"
        "#gpio-detail h4{margin:0 0 10px;font-size:14px}"
        "#gpio-detail .gd-row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}"
        ".gd-lbl{font-size:12px;color:var(--txt2)}"
        "#gpio-legend{display:flex;flex-wrap:wrap;gap:8px;font-size:11px;margin-bottom:8px}"
        "#gpio-legend span{display:inline-flex;align-items:center;gap:4px}"
        "#gpio-legend .lg{width:12px;height:12px;border-radius:3px}"
        "</style>"
    ));
    // Legend
    _server.sendContent(F(
        "<div id='gpio-legend'>"
        "<span><span class='lg' style='background:#1c4d2e'></span>INPUT HIGH</span>"
        "<span><span class='lg' style='background:#1c2a3a'></span>INPUT LOW</span>"
        "<span><span class='lg' style='background:#2d4a1e;border:1px dashed #7ee787'></span>OUTPUT HIGH</span>"
        "<span><span class='lg' style='background:#1a1e24;border:1px dashed #6e7681'></span>OUTPUT LOW</span>"
        "<span><span class='lg' style='background:#3d2e14'></span><span data-t='gpio-legend-warn'>\u041f\u0440\u0435\u0434\u0443\u043f\u0440\u0435\u0436\u0434\u0435\u043d\u0438\u0435</span></span>"
        "<span><span class='lg' style='background:#3d1414'></span><span data-t='gpio-legend-danger'>\u041d\u0435\u043b\u044c\u0437\u044f \u0438\u0441\u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u044c</span></span>"
        "</div>"
    ));
    _server.sendContent(F(
        "<div style='display:flex;gap:8px;align-items:center;margin-bottom:8px;flex-wrap:wrap'>"
        "<button class='btn btn-secondary' onclick='gpioRefresh()'><span data-t='gpio-refresh'>\u041e\u0431\u043d\u043e\u0432\u0438\u0442\u044c</span></button>"
        "<label style='display:flex;align-items:center;gap:6px;font-size:13px;cursor:pointer'>"
        "<input type='checkbox' id='gpio-auto' onchange='gpioAutoToggle(this.checked)'> <span data-t='gpio-auto'>\u0410\u0432\u0442\u043e (3\u0441)</span></label>"
        "<span id='gpio-status' style='font-size:11px;color:var(--txt2)'></span>"
        "</div>"
        "<div id='gpio-grid'><span style='color:var(--txt2);font-size:13px' data-t='gpio-load'>\u0417\u0430\u0433\u0440\u0443\u0437\u043a\u0430...</span></div>"
        "<div id='gpio-detail'></div>"
    ));
    // JS
    _server.sendContent(F(
        "<script>"
        "var gpioData=[];"
        "var gpioTimer=null;"
        "function gpioAutoToggle(on){"
        "  if(on){if(!gpioTimer)gpioTimer=setInterval(gpioRefresh,3000);}"
        "  else{if(gpioTimer){clearInterval(gpioTimer);gpioTimer=null;}}"
        "}"
        "function gpioRefresh(){"
        "  fetch('/api/gpio').then(r=>r.json()).then(function(d){"
        "    gpioData=d.pins;"
        "    var g=document.getElementById('gpio-grid');"
        "    var html='';"
        "    d.pins.forEach(function(p){"
        "      var cls='unset';"
        "      if(p.w===2) cls='danger';"
        "      else if(p.dir==='out') cls=(p.lvl?'out-high':'out-low');"
        "      else if(p.dir==='in') cls=(p.lvl?'high':'low');"
        "      else if(p.w===1) cls='warn';"
        "      html+='<div class=\"gp '+cls+'\" onclick=\"gpioSelect('+p.n+')\">'"
        "           +'<span class=\"gpn\">'+p.n+'</span>'"
        "           +(p.note?'<span class=\"gpnt\">'+p.note+'</span>':'')"
        "           +'<span style=\"font-size:10px\">'+(p.dir==='in'||p.dir==='out'?'['+p.lvl+']':'')+'</span>'"
        "           +'</div>';"
        "    });"
        "    g.innerHTML=html;"
        "    document.getElementById('gpio-status').textContent=t('gpio-updated')+' '+new Date().toLocaleTimeString();"
        "  }).catch(function(){document.getElementById('gpio-status').textContent=t('gpio-error');});"
        "}"
        "function gpioSelect(n){"
        "  var p=gpioData.find(x=>x.n===n);"
        "  if(!p)return;"
        "  var d=document.getElementById('gpio-detail');"
        "  var warnHtml='';"
        "  if(p.w===2)warnHtml='<div style=\"color:#f85149;font-size:12px\">'+t('gpio-warn-reserved')+'</div>';"
        "  else if(p.w===1)warnHtml='<div style=\"color:#e3b341;font-size:12px\">'+t('gpio-warn-special')+'</div>';"
        "  d.innerHTML='<h4>GPIO '+n+(p.note?' &mdash; <small>'+p.note+'</small>':'')+'</h4>'"
        "    +warnHtml"
        "    +'<div class=\"gd-row\" style=\"margin-bottom:10px\">'  "
        "    +'<span class=\"gd-lbl\">'+t('gpio-mode')+':</span>'"
        "    +'<select id=\"gd-mode\" style=\"padding:4px 8px;background:var(--bg3);color:var(--txt);border:1px solid var(--brd);border-radius:4px\">'"
        "    +'<option value=\"unset\"'+(p.dir===\"unset\"?' selected':'')+'>'+t('gpio-unset')+'</option>'"
        "    +'<option value=\"in\"'+(p.dir===\"in\"?' selected':'')+'>'+t('gpio-in')+'</option>'"
        "    +'<option value=\"pu\"'+(p.dir===\"pu\"?' selected':'')+'>'+t('gpio-pu')+'</option>'"
        "    +'<option value=\"pd\"'+(p.dir===\"pd\"?' selected':'')+'>'+t('gpio-pd')+'</option>'"
        "    +'<option value=\"out\"'+(p.dir===\"out\"?' selected':'')+'>'+t('gpio-out-ctrl')+'</option>'"
        "    +'</select>'"
        "    +'<span class=\"gd-lbl\">'+t('gpio-level')+':</span>'"
        "    +'<button class=\"btn btn-secondary\" style=\"padding:4px 12px\" onclick=\"gpioSetVal('+n+',0)\">LOW&nbsp;0</button>'"
        "    +'<button class=\"btn btn-secondary\" style=\"padding:4px 12px\" onclick=\"gpioSetVal('+n+',1)\">HIGH&nbsp;1</button>'"
        "    +'<button class=\"btn btn-secondary\" style=\"padding:4px 12px\" onclick=\"gpioApplyMode('+n+')\">OK '+t('gpio-apply')+'</button>'"
        "    +'<button class=\"btn btn-secondary\" style=\"padding:4px 12px;color:#e3b341;border-color:#e3b341\" onclick=\"gpioSend('+n+',\\'unset\\',null)\">RESET '+t('gpio-reset-pin')+'</button>'"
        "    +'</div>'"
        "    +'<div id=\"gd-result\" style=\"font-size:12px;color:var(--txt2)\"></div>';"
        "  d.style.display='block';"
        "}"
        "function gpioApplyMode(n){"
        "  var m=document.getElementById('gd-mode').value;"
        "  gpioSend(n,m,null);"
        "}"
        "function gpioSetVal(n,v){"
        "  var m=document.getElementById('gd-mode')?document.getElementById('gd-mode').value:'';"
        "  gpioSend(n,m||'out',v);"
        "}"
        "function gpioSend(n,mode,val){"
        "  var body='pin='+n+'&mode='+mode;"
        "  if(val!==null&&val!==undefined)body+='&val='+val;"
        "  fetch('/api/gpio/set',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})"
        "  .then(r=>r.json()).then(function(d){"
        "    var el=document.getElementById('gd-result');"
        "    if(el)el.textContent=d.ok?'GPIO '+d.n+': mode='+d.dir+' level='+d.lvl:t('gpio-error')+': '+d.err;"
        "    setTimeout(gpioRefresh,200);"
        "  });"
        "}"
        "gpioRefresh();"
        "</script>"
    ));
    _server.sendContent(F("</div>"));  // end gpio card

    // ---- GPIO Timer / Scheduler card ----
    _server.sendContent(F("<div class='card'><h3><span data-t='timer-card'>GPIO \u0422\u0430\u0439\u043c\u0435\u0440\u044b / \u0420\u0430\u0441\u043f\u0438\u0441\u0430\u043d\u0438\u0435</span></h3>"));
    _server.sendContent(F(
        "<p style='color:var(--txt2);font-size:13px;margin-top:0'><span data-t='timer-hint'>"
        "\u0414\u043e \u0447\u0430\u0441\u043e\u0432 23 \u2014 \u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u0435\u043b\u044c\u0441\u043a\u043e\u0435 \u0432\u0440\u0435\u043c\u044f."
        " 0\u00d70\u00d700 = \u0432\u044b\u043a\u043b\u044e\u0447\u0435\u043d\u043e. "
        "\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u044f: HIGH/LOW \u2014 \u0443\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u044c \u0443\u0440\u043e\u0432\u0435\u043d\u044c, "
        "TOGGLE \u2014 \u0438\u043d\u0432\u0435\u0440\u0441\u0438\u044f, PULSE \u2014 \u0438\u043c\u043f\u0443\u043b\u044c\u0441 \u0437\u0430\u0434\u0430\u043d\u043d\u043e\u0439 \u0434\u043b\u0438\u0442\u0435\u043b\u044c\u043d\u043e\u0441\u0442\u0438.</span></p>"
    ));
    // Table header
    // Build GPIO_OPTS JS array for timer pin selects
    {
        bool isS3 = String(ESP.getChipModel()).indexOf("S3") >= 0;
        const GpioInfo* tbl = isS3 ? kGpioS3 : kGpioESP32;
        int cnt = isS3 ? (int)(sizeof(kGpioS3)/sizeof(kGpioS3[0]))
                       : (int)(sizeof(kGpioESP32)/sizeof(kGpioESP32[0]));
        String gpioJs;
        gpioJs.reserve(1800);
        gpioJs += F("<script>var GPIO_OPTS=[");
        for (int i = 0; i < cnt; i++) {
            if (i) gpioJs += ',';
            gpioJs += F("{v:"); gpioJs += tbl[i].pin;
            gpioJs += F(",l:\""); gpioJs += tbl[i].note;
            gpioJs += F("\",w:"); gpioJs += tbl[i].w;
            gpioJs += '}';
        }
        gpioJs += F("];</script>");
        _server.sendContent(gpioJs);
    }
    _server.sendContent(F(
        "<form id='timer-form'>"
        "<div style='overflow-x:auto'>"
        "<table style='width:100%;border-collapse:collapse;font-size:13px'>"
        "<thead><tr style='color:var(--txt2);border-bottom:1px solid var(--brd)'>"
        "<th style='padding:4px 6px;text-align:center'>#</th>"
        "<th style='padding:4px 6px;text-align:center'><span data-t='timer-col-on'>\u0412\u043a\u043b</span></th>"
        "<th style='padding:4px 6px'><span data-t='timer-col-name'>\u041d\u0430\u0437\u0432\u0430\u043d\u0438\u0435</span></th>"
        "<th style='padding:4px 6px;text-align:center'><span data-t='timer-col-gpio'>GPIO</span></th>"
        "<th style='padding:4px 6px;text-align:center'>Invert</th>"
        "<th style='padding:4px 6px'><span data-t='timer-col-action'>\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u0435</span></th>"
        "<th style='padding:4px 6px;text-align:center'>H</th>"
        "<th style='padding:4px 6px;text-align:center'>M</th>"
        "<th style='padding:4px 6px;text-align:center'>S</th>"
        "<th style='padding:4px 6px;text-align:center'><span data-t='timer-col-dur'>\u0414\u043b\u0438\u0442. (ms)</span></th>"
        "<th style='padding:4px 6px;text-align:center'><span data-t='timer-col-next'>\u0421\u043b\u0435\u0434. \u0437\u0430\u043f\u0443\u0441\u043a</span></th>"
        "<th style='padding:4px 6px;text-align:center'><span data-t='timer-col-reset'>\u0421\u0431\u0440\u043e\u0441</span></th>"
        "</tr></thead><tbody id='timer-tbody'>"
    ));

    // One row per timer slot
    for (int i = 0; i < MAX_GPIO_TIMERS; i++) {
        const GpioTimerConfig& t = _cfg->cfg.gpio_timers[i];
        String si = String(i);
        String row;
        row.reserve(700);

        const char* tdS = "style='padding:4px 6px;border-bottom:1px solid var(--brd2)'";
        const char* inS = "style='width:100%;padding:3px 6px;background:var(--bg3);"
                          "color:var(--txt);border:1px solid var(--brd);border-radius:4px'";

        row += F("<tr>");
        // # index
        row += F("<td "); row += tdS; row += F(" style='text-align:center;color:var(--txt2);font-weight:700'>"); row += i; row += F("</td>");
        // enable
        row += F("<td "); row += tdS; row += F(" style='text-align:center'><input type='checkbox' name='ten_"); row += si; row += '\'';
        if (t.enabled) row += F(" checked");
        row += F("></td>");
        // label
        row += F("<td "); row += tdS; row += F("><input type='text' name='tlbl_"); row += si;
        row += F("' value='"); row += htmlAttr(t.label); row += F("' placeholder='\u041f\u043e\u043c\u043f\u0430' maxlength='19' "); row += inS; row += F("></td>");
        // pin — populated by populateGpioSelects() JS
        row += F("<td "); row += tdS; row += F(" style='text-align:center'><select name='tpin_"); row += si;
        row += F("' class='gpio-sel' data-val='"); row += t.pin;
        row += F("' "); row += inS; row += F(" style='width:110px'></select></td>");
        // invert checkbox
        row += F("<td "); row += tdS; row += F(" style='text-align:center'>");
        row += F("<input type='checkbox' name='tinv_"); row += si; row += F("' ");
        if (t.active_low) row += F(" checked");
        row += F("></td>");
        // action
        row += F("<td "); row += tdS; row += F("><select name='tact_"); row += si; row += F("' "); row += inS; row += F(" style='width:auto'>");
        const char* actOpts[] = {
            "HIGH \u2014 \u0412\u043a\u043b\u044e\u0447\u0438\u0442\u044c",
            "LOW \u2014 \u0412\u044b\u043a\u043b\u044e\u0447\u0438\u0442\u044c",
            "\u0418\u043d\u0432\u0435\u0440\u0441\u0438\u044f (TOGGLE)",
            "\u0418\u043c\u043f\u0443\u043b\u044c\u0441 HIGH",
            "\u0418\u043c\u043f\u0443\u043b\u044c\u0441 LOW"
        };
        for (int a = 0; a < TIMER_ACTION_COUNT; a++) {
            row += F("<option value='"); row += a; row += '\'';
            if ((int)t.action == a) row += F(" selected");
            row += '>'; row += actOpts[a]; row += F("</option>");
        }
        row += F("</select></td>");
        // H
        row += F("<td "); row += tdS; row += F("><input type='number' name='th_"); row += si;
        row += F("' value='"); row += t.hours; row += F("' min='0' max='23' style='width:44px;padding:3px 4px;background:var(--bg3);color:var(--txt);border:1px solid var(--brd);border-radius:4px'></td>");
        // M
        row += F("<td "); row += tdS; row += F("><input type='number' name='tm_"); row += si;
        row += F("' value='"); row += t.minutes; row += F("' min='0' max='59' style='width:44px;padding:3px 4px;background:var(--bg3);color:var(--txt);border:1px solid var(--brd);border-radius:4px'></td>");
        // S
        row += F("<td "); row += tdS; row += F("><input type='number' name='ts_"); row += si;
        row += F("' value='"); row += t.seconds; row += F("' min='0' max='59' style='width:44px;padding:3px 4px;background:var(--bg3);color:var(--txt);border:1px solid var(--brd);border-radius:4px'></td>");
        // duration
        row += F("<td "); row += tdS; row += F("><input type='number' name='tdur_"); row += si;
        row += F("' value='"); row += t.duration_ms; row += F("' min='1' max='60000' ");
        row += inS; row += F(" style='width:80px'></td>");
        // next trigger (filled by JS)
        row += F("<td "); row += tdS; row += F(" style='text-align:center;color:var(--txt2);font-size:12px' id='tnxt_"); row += i; row += F("'>--</td>");
        // reset button с крестиком для сброса таймера
        row += F("<td "); row += tdS; row += F(" style='text-align:center'><button type='button' "
            "class='btn btn-secondary' style='padding:2px 8px;font-size:14px;color:#f85149;border-color:#f85149;font-weight:bold;min-width:32px'"
            " onclick='timerReset("); row += i; row += F(")' title='\u0421\u0431\u0440\u043e\u0441 \u0442\u0430\u0439\u043c\u0435\u0440\u0430'>\u00D7</button></td>");
        row += F("</tr>");
        _server.sendContent(row);
    }

    // Footer: submit + JS countdown
    _server.sendContent(F(
        "</tbody></table></div>"
        "<div style='margin-top:14px;display:flex;align-items:center;gap:12px'>"
        "<button type='button' class='btn' onclick='saveTimers()'><span data-t='timer-save'>\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u0442\u0430\u0439\u043c\u0435\u0440\u044b</span></button>"
        "<button type='button' class='btn btn-ghost' onclick='resetAllGpio()' style='background:transparent;border:1px solid var(--brd);padding:6px 10px'>Reset all GPIO</button>"
        "<span id='timer-save-status' style='font-size:13px'></span>"
        "</div></form>"
        "<script>"
        "function saveTimers(){"
        "  var st=document.getElementById('timer-save-status');"
        "  if(st){st.textContent=t('timer-saving');st.style.color='var(--txt2)';}"
        "  var fd=new URLSearchParams(new FormData(document.getElementById('timer-form')));"
        "  fetch('/save/gpio-timers',{method:'POST',body:fd,headers:{'Content-Type':'application/x-www-form-urlencoded'}})"
        "  .then(function(r){return r.json();})"
        "  .then(function(d){"
        "    if(st){st.textContent=d.ok?t('timer-saved'):t('timer-err');"
        "           st.style.color=d.ok?'#3fb950':'#f85149';}"
        "    if(d.ok)timerTick();"
        "  })"
        "  .catch(function(){"
        "    if(st){st.textContent=t('timer-conn-err');st.style.color='#f85149';}"
        "  });"
        "}"
        "function populateGpioSelects(){"
        "  document.querySelectorAll('.gpio-sel').forEach(function(sel){"
        "    var cur=parseInt(sel.dataset.val||'0');"
        "    GPIO_OPTS.forEach(function(g){"
        "      var o=document.createElement('option');"
        "      o.value=g.v;"
        "      var lbl='GPIO '+g.v;"
        "      if(g.l)lbl+=' \\u2014 '+g.l;"
        "      if(g.w>=2)lbl='\\u26A0 '+lbl;"
        "      o.textContent=lbl;"
        "      if(g.w>=2)o.style.color='#f85149';"
        "      else if(g.w===1)o.style.color='#d29922';"
        "      if(g.v===cur)o.selected=true;"
        "      sel.appendChild(o);"
        "    });"
        "  });"
        "}"
        "populateGpioSelects();"
        "var INITIAL_GPIO_MODES = {}; var INITIAL_GPIO_LEVELS = {};"
        "fetch('/api/gpio').then(r=>r.json()).then(function(d){"
        "  d.pins.forEach(function(p){ INITIAL_GPIO_MODES[p.n]=p.dir; INITIAL_GPIO_LEVELS[p.n]=p.lvl; });"
        "  document.querySelectorAll('.gpio-sel').forEach(function(sel){"
        "    sel.dataset.prev = sel.dataset.val || '0';"
        "    sel.addEventListener('change', function(e){"
        "      var prev = parseInt(this.dataset.prev||'0');"
        "      var cur = parseInt(this.value||'0');"
        "      if(prev && prev !== cur){"
        "        var im = INITIAL_GPIO_MODES[prev] || 'unset';"
        "        var il = INITIAL_GPIO_LEVELS[prev] || 0;"
        "        var mode = 'unset';"
        "        if(im==='out') mode='out'; else if(im==='in') mode='in'; else if(im==='pu') mode='pu'; else if(im==='pd') mode='pd';"
        "        var fd = new URLSearchParams(); fd.append('pin', String(prev)); fd.append('mode', mode); if(mode!='unset') fd.append('val', String(il));"
        "        fetch('/api/gpio/set',{method:'POST',body:fd,headers:{'Content-Type':'application/x-www-form-urlencoded'}});"
        "      }"
        "      this.dataset.prev = String(cur);"
        "    });"
        "  });"
        "});"
        "function timerReset(i){"
        "  var ok=confirm(t('timer-confirm-reset')+i+'?');"
        "  if(!ok)return;"
        "  var r=i;"
        "  var el=function(n){return document.querySelector('[name='+n+'_'+r+']');};"
        "  var cb=document.querySelector('[name=ten_'+r+']');"
        "  if(cb)cb.checked=false;"
        "  if(el('tlbl'))el('tlbl').value='';"
        "  if(el('tpin'))el('tpin').value='0';"
        "  if(el('tact'))el('tact').value='0';"
        "  if(el('th'))el('th').value='0';"
        "  if(el('tm'))el('tm').value='0';"
        "  if(el('ts'))el('ts').value='0';"
        "  if(el('tdur'))el('tdur').value='500';"
        "  var nx=document.getElementById('tnxt_'+r);"
        "  if(nx)nx.textContent='--';"
        "}"
        "function timerTick(){"
        "  fetch('/api/gpio/timers').then(r=>r.json()).then(function(d){"
        "    d.timers.forEach(function(t){"
        "      var el=document.getElementById('tnxt_'+t.i);"
        "      if(!el)return;"
        "      if(!t.en){el.textContent='--';return;}"
        "      var ms=t.next_ms;"
        "      if(ms<=0){el.textContent=t('timer-now');return;}"
        "      var s=Math.floor(ms/1000);"
        "      var m=Math.floor(s/60);s%=60;"
        "      var h=Math.floor(m/60);m%=60;"
        "      el.textContent=(h?h+'h ':'')"
        "                    +(m?m+'m ':'')"
        "                    +s+'s';"
        "    });"
        "  });"
        "}"
        "timerTick();"
        "setInterval(timerTick,2000);"
        "function resetAllGpio(){"
        "  if(!confirm(t('timer-confirm-reset')+' all?')) return;"
        "  for(var p in INITIAL_GPIO_MODES){"
        "    var im = INITIAL_GPIO_MODES[p]; var il = INITIAL_GPIO_LEVELS[p]||0;"
        "    var mode = (im==='out'?'out': (im==='in'?'in': (im==='pu'?'pu': (im==='pd'?'pd':'unset'))));"
        "    var fd = new URLSearchParams(); fd.append('pin', String(p)); fd.append('mode', mode); if(mode!='unset') fd.append('val', String(il));"
        "    fetch('/api/gpio/set',{method:'POST',body:fd,headers:{'Content-Type':'application/x-www-form-urlencoded'}});"
        "  }"
        "}"
        "</script>"
    ));
    _server.sendContent(F("</div>"));  // end timer card

    endPage();
}

// ================================================================
//                       PAGE: MQTT
// ================================================================

void WebPortal::handleMqtt() {
    // MQTT settings
    String form;
    form += F("<form method='POST' action='/save/mqtt'>");
    form += F("<div class='grid2'>");
    form += inputField("\u0425\u043E\u0441\u0442 \u0431\u0440\u043E\u043A\u0435\u0440\u0430", "mqtt_host", _cfg->cfg.mqtt_host, "text", "192.168.1.100");
    form += numberField("\u041F\u043E\u0440\u0442 \u0431\u0440\u043E\u043A\u0435\u0440\u0430", "mqtt_port", _cfg->cfg.mqtt_port, 1, 65535);
    form += F("</div>");
    form += inputField("\u041B\u043E\u0433\u0438\u043D", "mqtt_user", _cfg->cfg.mqtt_user, "text", "\u043D\u0435\u043E\u0431\u044F\u0437\u0430\u0442\u0435\u043B\u044C\u043D\u043E");
    form += inputField("\u041F\u0430\u0440\u043E\u043B\u044C", "mqtt_pass", _cfg->cfg.mqtt_pass, "password", "\u043D\u0435\u043E\u0431\u044F\u0437\u0430\u0442\u0435\u043B\u044C\u043D\u043E");
    form += inputField("\u0422\u043E\u043F\u0438\u043A", "mqtt_topic", _cfg->cfg.mqtt_topic, "text", "esp-hub/telemetry");
    form += numberField("\u0418\u043D\u0442\u0435\u0440\u0432\u0430\u043B (\u0441\u0435\u043A)", "mqtt_interval", _cfg->cfg.mqtt_interval_s, 1, 3600);
    form += submitButton("<span data-t='save-mqtt'>Save MQTT</span>");
    form += F("</form>");
    startPage("MQTT");
    _server.sendContent(card("<span data-t='mqttconf'>MQTT Configuration</span>", form));

    // WiFi settings with scanner
    String wform;
    // Scan button + results
    wform += F("<div class='mb flex-between'>"
               "<div>"
               "<button type='button' class='btn btn-secondary' id='scan-btn' onclick='scanWifi()'>"
               "<span data-t='scan'>Scan WiFi</span></button>"
               " <span class='text-muted' data-t='scan-hint'>Click to select</span>"
               "</div></div>"
               "<div id='scan-res' class='mb'></div>");
    // Form
    wform += F("<form method='POST' action='/save/wifi'>");
    wform += inputField("SSID", "wifi_ssid", _cfg->cfg.wifi_ssid, "text", "\u0418\u043C\u044F \u0441\u0435\u0442\u0438");
    {
        const char* phPass = (strlen(_cfg->cfg.wifi_pass) > 0)
            ? "\u041E\u0441\u0442\u0430\u0432\u044C\u0442\u0435 \u043F\u0443\u0441\u0442\u044B\u043C \u2014 \u043D\u0435 \u043C\u0435\u043D\u044F\u0442\u044C \u043F\u0430\u0440\u043E\u043B\u044C"
            : "\u041F\u0430\u0440\u043E\u043B\u044C";
        wform += inputField("\u041F\u0430\u0440\u043E\u043B\u044C", "wifi_pass", "", "password", phPass);
    }
    wform += submitButton("<span data-t='save-wifi'>Save WiFi &amp; Reboot</span>");
    wform += F("</form>");
    _server.sendContent(card("<span data-t='wificonf'>WiFi Configuration</span>", wform));
    endPage();
}

// ================================================================
//                       PAGE: SYSTEM
// ================================================================

void WebPortal::handleSystem() {
    String info;
    info.reserve(1600);
    info += F("<table>");

    // Device
    info += F("<tr><th colspan='2'>\u0423\u0441\u0442\u0440\u043E\u0439\u0441\u0442\u0432\u043E</th></tr>");
    info += F("<tr><td>\u0418\u043C\u044F</td><td>");
    info += htmlAttr(_cfg->cfg.device_name);
    info += F("</td></tr>");
    info += F("<tr><td>\u0427\u0438\u043F</td><td>ESP32 (");
    info += ESP.getChipModel();
    info += F(", rev ");
    info += ESP.getChipRevision();
    info += F(", ");
    info += ESP.getChipCores();
    info += F(" \u044F\u0434\u0440\u0430)</td></tr>");
    info += F("<tr><td>SDK</td><td>");
    info += ESP.getSdkVersion();
    info += F("</td></tr>");
    info += F("<tr><td>Serial</td><td>");
    info += String(_cfg->cfg.serial_baud);
    info += F(" baud</td></tr>");

    // Clocks (RCC)
    info += F("<tr><th colspan='2'>\u0422\u0430\u043A\u0442\u043E\u0432\u044B\u0435 \u0447\u0430\u0441\u0442\u043E\u0442\u044B (RCC)</th></tr>");
    info += F("<tr><td>CPU</td><td><b>");
    info += ESP.getCpuFreqMHz();
    info += F(" \u041C\u0413\u0446</b></td></tr>");
    info += F("<tr><td>APB Bus</td><td>");
    info += String(getApbFrequency() / 1000000);
    info += F(" \u041C\u0413\u0446</td></tr>");
    info += F("<tr><td>XTAL</td><td>40 \u041C\u0413\u0446</td></tr>");
    info += F("<tr><td>Flash \u0448\u0438\u043D\u0430</td><td>");
    info += String(ESP.getFlashChipSpeed() / 1000000);
    info += F(" \u041C\u0413\u0446</td></tr>");

    // Memory
    info += F("<tr><th colspan='2'>\u041F\u0430\u043C\u044F\u0442\u044C</th></tr>");
    info += F("<tr><td>RAM \u0432\u0441\u0435\u0433\u043E</td><td>");
    info += String(ESP.getHeapSize() / 1024.0, 1);
    info += F(" \u043A\u0411</td></tr>");
    info += F("<tr><td>RAM \u0441\u0432\u043E\u0431\u043E\u0434\u043D\u043E</td><td>");
    info += String(ESP.getFreeHeap() / 1024.0, 1);
    info += F(" \u043A\u0411</td></tr>");
    info += F("<tr><td>RAM \u043C\u0438\u043D. \u0441\u0432\u043E\u0431.</td><td>");
    info += String(ESP.getMinFreeHeap() / 1024.0, 1);
    info += F(" \u043A\u0411</td></tr>");
    if (ESP.getPsramSize() > 0) {
        info += F("<tr><td>PSRAM</td><td>");
        info += String(ESP.getPsramSize() / 1024.0, 0);
        info += F(" \u043A\u0411 (\u0441\u0432\u043E\u0431. ");
        info += String(ESP.getFreePsram() / 1024.0, 0);
        info += F(" \u043A\u0411)</td></tr>");
    }
    info += F("<tr><td>Flash</td><td>");
    info += String(ESP.getFlashChipSize() / (1024.0 * 1024.0), 1);
    info += F(" \u041C\u0411</td></tr>");
    info += F("<tr><td>\u041F\u0440\u043E\u0448\u0438\u0432\u043A\u0430</td><td>");
    info += String(ESP.getSketchSize() / 1024.0, 1);
    info += F(" \u043A\u0411 (\u0441\u0432\u043E\u0431. ");
    info += String(ESP.getFreeSketchSpace() / 1024.0, 1);
    info += F(" \u043A\u0411)</td></tr>");

    // Uptime
    info += F("<tr><th colspan='2'>\u0412\u0440\u0435\u043C\u044F \u0440\u0430\u0431\u043E\u0442\u044B</th></tr>");
    {
        uint32_t s = millis() / 1000;
        uint32_t m = s / 60; s %= 60;
        uint32_t h = m / 60; m %= 60;
        uint32_t d = h / 24; h %= 24;
        char buf[32];
        snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu",
                 (unsigned long)d, (unsigned long)h, (unsigned long)m, (unsigned long)s);
        info += F("<tr><td>Uptime</td><td>");
        info += buf;
        info += F("</td></tr>");
    }
    info += F("<tr><td>\u041F\u0440\u0438\u0447\u0438\u043D\u0430 \u0441\u0431\u0440\u043E\u0441\u0430</td><td>");
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   info += F("\u0412\u043A\u043B\u044E\u0447\u0435\u043D\u0438\u0435 \u043F\u0438\u0442\u0430\u043D\u0438\u044F"); break;
        case ESP_RST_EXT:       info += F("\u0412\u043D\u0435\u0448\u043D\u0438\u0439 \u0441\u0431\u0440\u043E\u0441"); break;
        case ESP_RST_SW:        info += F("\u041F\u0440\u043E\u0433\u0440\u0430\u043C\u043C\u043D\u044B\u0439 \u0441\u0431\u0440\u043E\u0441"); break;
        case ESP_RST_PANIC:     info += F("Guru Meditation"); break;
        case ESP_RST_INT_WDT:   info += F("WDT \u043F\u0440\u0435\u0440\u044B\u0432\u0430\u043D\u0438\u044F"); break;
        case ESP_RST_TASK_WDT:  info += F("WDT \u0437\u0430\u0434\u0430\u0447\u0438"); break;
        case ESP_RST_WDT:       info += F("WDT"); break;
        case ESP_RST_DEEPSLEEP: info += F("Deep Sleep"); break;
        case ESP_RST_BROWNOUT:  info += F("Brownout"); break;
        default:                info += F("\u041D\u0435\u0438\u0437\u0432\u0435\u0441\u0442\u043D\u043E"); break;
    }
    info += F("</td></tr>");

    // Network
    info += F("<tr><th colspan='2'>\u0421\u0435\u0442\u044C</th></tr>");
    info += F("<tr><td>MAC</td><td>");
    info += _wifi->macAddress();
    info += F("</td></tr>");
    // Saved credentials
    info += F("<tr><td>\u0421\u043E\u0445\u0440. SSID</td><td><b>");
    info += (strlen(_cfg->cfg.wifi_ssid) > 0) ? htmlAttr(_cfg->cfg.wifi_ssid) : "(\u043D\u0435 \u0437\u0430\u0434\u0430\u043D)";
    info += F("</b></td></tr>");
    info += F("<tr><td>\u0421\u043E\u0445\u0440. \u043F\u0430\u0440\u043E\u043B\u044C</td><td>");
    {
        int plen = strlen(_cfg->cfg.wifi_pass);
        if (plen > 0) {
            info += String(plen);
            info += F(" \u0441\u0438\u043C\u0432. (");
            // show first + last char for debugging
            info += _cfg->cfg.wifi_pass[0];
            info += F("***");
            info += _cfg->cfg.wifi_pass[plen - 1];
            info += F(")");
        } else {
            info += F("<span style='color:#d29922'>(\u043F\u0443\u0441\u0442\u043E \u2014 AUTH_FAIL!)</span>");
        }
    }
    info += F("</td></tr>");
    info += F("<tr><td>\u0420\u0435\u0436\u0438\u043C WiFi</td><td>");
    info += _wifi->isConnected() ? F("STA (\u043F\u043E\u0434\u043A\u043B\u044E\u0447\u0451\u043D)") : (_wifi->isAP() ? F("AP (\u0442.\u0434\u043E\u0441\u0442\u0443\u043F\u0430)") : F("\u041D\u0435\u0430\u043A\u0442\u0438\u0432\u0435\u043D"));
    info += F("</td></tr>");

    if (_wifi->isConnected()) {
        info += F("<tr><td>IP</td><td>");
        info += _wifi->localIP();
        info += F("</td></tr>");
        info += F("<tr><td>RSSI</td><td>");
        info += _wifi->rssi();
        info += F(" dBm</td></tr>");
    }

    // WiFi Clients
    if (_wifi->apClientsCount() > 0) {
        info += F("<tr><td>\u041A\u043B\u0438\u0435\u043D\u0442\u044B WiFi</td><td>");
        info += _wifi->apClientsCount();
        info += F(" (");
        info += _wifi->apClientsMACs();
        info += F(")</td></tr>");
    }

    // Bluetooth BLE
    info += F("<tr><th colspan='2'>Bluetooth BLE</th></tr>");
    info += F("<tr><td>\u0421\u043e\u0441\u0442\u043e\u044f\u043d\u0438\u0435</td><td>");
    if (!_cfg->cfg.ble_enabled) {
        info += F("\u041e\u0442\u043a\u043b\u044e\u0447\u0451\u043d");
    } else if (_ble->isConnected()) {
        info += F("\u041a\u043b\u0438\u0435\u043d\u0442 \u043f\u043e\u0434\u043a\u043b\u044e\u0447\u0451\u043d");
        if (_ble->connectedClientMAC().length() > 0) {
            info += F("<br><small>");
            info += _ble->connectedClientMAC();
            info += F("</small>");
        }
    } else {
        info += F("Ожидание подключения");
    }
    info += F("</td></tr>");
    if (_cfg->cfg.ble_enabled) {
        info += F("<tr><td>\u0418\u043c\u044f BLE</td><td>");
        info += (strlen(_cfg->cfg.ble_name) > 0) ? _cfg->cfg.ble_name : _cfg->cfg.device_name;
        info += F("</td></tr>");
    }

    info += F("</table>");
    // Передаём английский "System", реальный заголовок страницы будет переведён JavaScript
    startPage("System");
    _server.sendContent(card("Системная информация", info));
    _server.sendContent(wifiConnectCard());

    // ---- CPU frequency and Serial selector ----
    {
        uint16_t cur = ESP.getCpuFreqMHz();
        String cf;
        cf.reserve(800);
        cf += F("<form method='POST' action='/save/system'>"
                "<p style='color:var(--txt2);font-size:13px;margin-top:0'>"
                "\u0418\u0437\u043C\u0435\u043D\u0435\u043D\u0438\u0435 \u0447\u0430\u0441\u0442\u043E\u0442\u044B CPU \u0432\u043B\u0438\u044F\u0435\u0442 \u043D\u0430 "
                "\u043F\u043E\u0442\u0440\u0435\u0431\u043B\u0435\u043D\u0438\u0435 \u0438 \u0431\u044B\u0441\u0442\u0440\u043E\u0434\u0435\u0439\u0441\u0442\u0432\u0438\u0435</p>"
                "<div style='display:flex;gap:20px;flex-wrap:wrap;margin-bottom:14px'>");
        const uint16_t freqs[] = {80, 160, 240};
        for (uint16_t f : freqs) {
            cf += F("<label style='display:flex;align-items:center;gap:6px;cursor:pointer'>"
                    "<input type='radio' name='cpu_freq' value='");
            cf += String(f);
            cf += F("'");
            if (cur == f) cf += F(" checked");
            cf += F("> <b>");
            cf += String(f);
            cf += F("</b>&nbsp;\u041C\u0413\u0446</label>");
        }
        cf += F("</div>");

        cf += F("<hr style='border:0;border-top:1px solid var(--border);margin:15px 0'>");
        cf += F("<p style='color:var(--txt2);font-size:13px;margin-top:0'>\u0421\u043A\u043E\u0440\u043E\u0441\u0442\u044C Serial \u043F\u043E\u0440\u0442\u0430 (baud)</p>");
        cf += F("<select name='serial_baud' class='input' style='max-width:200px;margin-bottom:14px'>");
        const uint32_t bauds[] = {9600, 19200, 38400, 57600, 74880, 115200, 230400, 460800, 921600};
        for (uint32_t b : bauds) {
            cf += F("<option value='");
            cf += String(b);
            cf += F("'");
            if (_cfg->cfg.serial_baud == b) cf += F(" selected");
            cf += F(">");
            cf += String(b);
            cf += F("</option>");
        }
        cf += F("</select>");

        cf += F("<hr style='border:0;border-top:1px solid var(--border);margin:15px 0'>");
        cf += F("<p style='color:var(--txt2);font-size:13px;margin-top:0'><b>⚙️ ИИ Агент</b></p>");
        cf += F("<div style='margin-bottom:14px'>");
        cf += F("<label class='toggle' style='margin-bottom:8px'>");
        cf += F("<input type='checkbox' name='ai_en' value='1' onchange='if(this.checked!==");
        cf += (_cfg->cfg.ai_enabled ? '1' : '0');
        cf += F("){if(confirm(\"Изменение статуса ИИ требует перезагрузки. Продолжить?\")){this.closest(\"form\").submit();} else {this.checked=");
        cf += (_cfg->cfg.ai_enabled ? '1' : '0');
        cf += F(";}}'");
        if (_cfg->cfg.ai_enabled) cf += F(" checked");
        cf += F("><span class='slider'></span> <span style='margin-left:8px'>ИИ Агент ");
        cf += (_cfg->cfg.ai_enabled ? F("<b style='color:var(--green)'>Включен</b>") : F("<b style='color:var(--muted)'>Отключен</b>"));
        cf += F("</span></label>");
        cf += F("</div>");

        cf += submitButton("\u041F\u0440\u0438\u043C\u0435\u043D\u0438\u0442\u044C \u0438 \u043F\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044C");
        cf += F("</form>");
        _server.sendContent(card("\u0427\u0430\u0441\u0442\u043E\u0442\u0430 CPU \u0438 Serial", cf));
    }

    // Actions
    String actions;
    actions += F("<div style='display:flex;gap:10px;flex-wrap:wrap'>");
    actions += F("<form method='POST' action='/reboot'>"
                 "<button class='btn btn-secondary' onclick=\"return confirm('\u041F\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044C?')\">" 
                 "\u041F\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044C</button></form>");
    // WiFi reset — clears saved credentials so user can re-enter them
    {
        String wifiInfo = F("ssid='");
        wifiInfo += (strlen(_cfg->cfg.wifi_ssid) > 0) ? _cfg->cfg.wifi_ssid : "(empty)";
        wifiInfo += F("' pass_len=");
        wifiInfo += strlen(_cfg->cfg.wifi_pass);
        actions += F("<form method='POST' action='/reset/wifi'>"
                     "<button class='btn btn-secondary' style='border-color:#d29922;color:#d29922'"
                     " onclick=\"return confirm('\u0421\u0431\u0440\u043E\u0441\u0438\u0442\u044C \u043A\u0440\u0435\u0434\u0435\u043D\u0446\u0438\u0430\u043B\u044B Wi-Fi?')\">"
                     "\u0421\u0431\u0440\u043E\u0441 Wi-Fi");
        actions += F(" <small style='display:block;font-size:11px;font-weight:400'>");
        actions += wifiInfo;
        actions += F("</small></button></form>");
    }
    actions += F("<form method='POST' action='/reset'>"
                 "<button class='btn btn-danger' onclick=\"return confirm('\u0421\u0431\u0440\u043E\u0441 \u043D\u0430\u0441\u0442\u0440\u043E\u0435\u043A?')\">"
                 "\u0421\u0431\u0440\u043E\u0441 \u043D\u0430\u0441\u0442\u0440\u043E\u0435\u043A</button></form>");
    actions += F("</div>");
    _server.sendContent(card("\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u044F", actions));

    // AP / Hotspot settings
    String apForm;
    apForm += F("<form method='POST' action='/save/ap'>");
    apForm += inputField("AP SSID (\u0438\u043C\u044F \u0442\u043E\u0447\u043A\u0438 \u0434\u043E\u0441\u0442\u0443\u043F\u0430)", "ap_ssid", _cfg->cfg.ap_ssid, "text", "ESP-HUB");
    apForm += inputField("AP \u043F\u0430\u0440\u043E\u043B\u044C (\u043C\u0438\u043D. 8 \u0441\u0438\u043C\u0432\u043E\u043B\u043E\u0432)", "ap_pass", _cfg->cfg.ap_pass, "password", "12345678");

    apForm += F("<label class='toggle'><input type='checkbox' name='ap_nat'");
    if (_cfg->cfg.ap_nat) apForm += F(" checked");
    apForm += F("><span class='slider'></span></label> "
                "<span style='font-size:14px'>\u0412\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u0438\u043d\u0442\u0435\u0440\u043d\u0435\u0442 (NAT, \u043e\u0442\u043a\u043b\u044e\u0447\u0430\u0435\u0442 \u0430\u0432\u0442\u043e-\u043e\u0442\u043a\u0440\u044b\u0442\u0438\u0435 \u043c\u0435\u043d\u044e)</span><br><br>");

    apForm += inputField("\u0418\u043C\u044F \u0443\u0441\u0442\u0440\u043E\u0439\u0441\u0442\u0432\u0430", "device_name", _cfg->cfg.device_name, "text", "ESP-HUB");
    apForm += submitButton("\u0421\u043E\u0445\u0440\u0430\u043D\u0438\u0442\u044C \u0438 \u043F\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044C");
    apForm += F("</form>");
    _server.sendContent(card("\u0422\u043E\u0447\u043A\u0430 \u0434\u043E\u0441\u0442\u0443\u043F\u0430 \u0438 \u0438\u043C\u044F \u0443\u0441\u0442\u0440\u043E\u0439\u0441\u0442\u0432\u0430", apForm));
    endPage();
}

// ================================================================
//                       PAGE: BLUETOOTH
// ================================================================

void WebPortal::handleBluetooth() {
    // Status card
    String st;
    st += F("<div class='grid2'>");

    st += F("<div><div class='flex-between'><span>\u0421\u0442\u0430\u0442\u0443\u0441</span>");
    if (!_ble->isEnabled()) {
        st += badge("\u041e\u0442\u043a\u043b\u044e\u0447\u0451\u043d", "badge-blue");
    } else if (_ble->isConnected()) {
        st += badge("\u041a\u043b\u0438\u0435\u043d\u0442 \u043f\u043e\u0434\u043a\u043b\u044e\u0447\u0451\u043d", "badge-green");
        if (_ble->connectedClientMAC().length() > 0) {
            st += F("<div class='text-muted' style='margin-top:4px'>MAC: ");
            st += _ble->connectedClientMAC();
            st += F("</div>");
        }
    } else {
        st += badge("Ожидание подключения", "badge-yellow");
    }
    st += F("</div>");
    if (_ble->isEnabled()) {
        st += F("<div class='text-muted'>\u0418\u043c\u044f: ");
        st += (strlen(_cfg->cfg.ble_name) > 0) ? _cfg->cfg.ble_name : _cfg->cfg.device_name;
        st += F("</div>");
    }
    st += F("</div>");

    st += F("<div><div class='flex-between'><span>Service UUID</span>");
    st += F("</div><div class='text-muted' style='font-size:11px;word-break:break-all'>"
            BLE_SERVICE_UUID "</div></div>");
    st += F("</div>"); // grid2
    startPage("Bluetooth");
    _server.sendContent(card("Bluetooth BLE", st));

    // Settings form
    String sform;
    sform += F("<form method='POST' action='/save/ble'>");
    sform += checkboxField("\u0412\u043a\u043b\u044e\u0447\u0438\u0442\u044c BLE", "ble_en", _cfg->cfg.ble_enabled);
    sform += inputField("\u0418\u043c\u044f BLE-\u0443\u0441\u0442\u0440\u043e\u0439\u0441\u0442\u0432\u0430",
                        "ble_name", _cfg->cfg.ble_name, "text",
                        _cfg->cfg.device_name);
    sform += submitButton("\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u0438 \u043f\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044c");
    sform += F("</form>");
    _server.sendContent(card("\u2699\uFE0F \u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0430 BLE", sform));

    // RX log — messages received from BLE clients
    String log;
    log += F("<div style='margin-bottom:8px;display:flex;gap:8px'>");
    log += F("<button class='btn btn-secondary' onclick=\"fetch('/api/ble').then(r=>r.json()).then(updateBleLog)\">"
             "\u041e\u0431\u043d\u043e\u0432\u0438\u0442\u044c</button>");
    log += F("<button class='btn btn-secondary' onclick=\"fetch('/api/ble/send',{method:'POST',"
             "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
             "body:'msg='+encodeURIComponent(document.getElementById('ble-out').value)})"
             ".then(()=>document.getElementById('ble-out').value='')\">"
             "\u041e\u0442\u043f\u0440\u0430\u0432\u0438\u0442\u044c</button>");
    log += F("</div>");
    log += F("<input id='ble-out' class='text-input' style='margin-bottom:8px' "
             "placeholder='\u0421\u043e\u043e\u0431\u0449\u0435\u043d\u0438\u0435 \u043a\u043b\u0438\u0435\u043d\u0442\u0443...' maxlength='180'>");
    log += F("<div id='ble-log' style='font-family:monospace;font-size:12px;background:var(--bg3);"
             "border:1px solid var(--brd);border-radius:6px;padding:10px;min-height:80px'>");
    if (_ble->logCount() == 0) {
        log += F("<span class='text-muted'>\u041d\u0435\u0442 \u0441\u043e\u043e\u0431\u0449\u0435\u043d\u0438\u0439</span>");
    } else {
        for (int i = 0; i < _ble->logCount(); i++) {
            log += "&gt; ";
            log += _ble->logLine(i);
            log += F("<br>");
        }
    }
    log += F("</div>");
    log += F("<script>"
        "function updateBleLog(d){"
        "  var el=document.getElementById('ble-log');"
        "  if(!d.log||!d.log.length){el.innerHTML=\"<span class='text-muted'>\u041d\u0435\u0442 \u0441\u043e\u043e\u0431\u0449\u0435\u043d\u0438\u0439</span>\";return;}"
        "  el.innerHTML=d.log.map(function(l){return '&gt; '+l}).join('<br>');"
        "}"
        "setInterval(function(){fetch('/api/ble').then(r=>r.json()).then(updateBleLog)},3000);"
        "</script>");
    _server.sendContent(card("RX \u0436\u0443\u0440\u043d\u0430\u043b (\u043f\u0440\u0438\u043d\u044f\u0442\u043e \u043e\u0442 \u043a\u043b\u0438\u0435\u043d\u0442\u0430)", log));

    // UUIDs reference card
    String uuids;
    uuids += F("<table>"
        "<tr><th>\u041d\u0430\u0437\u043d\u0430\u0447\u0435\u043d\u0438\u0435</th><th>UUID</th><th>\u0421\u0432\u043e\u0439\u0441\u0442\u0432\u0430</th></tr>"
        "<tr><td>Service</td><td style='font-size:11px'>" BLE_SERVICE_UUID "</td><td>\u2014</td></tr>"
        "<tr><td>TX (ESP32 \u2192 \u043a\u043b\u0438\u0435\u043d\u0442)</td>"
            "<td style='font-size:11px'>" BLE_CHAR_TX_UUID "</td><td>READ, NOTIFY</td></tr>"
        "<tr><td>RX (\u043a\u043b\u0438\u0435\u043d\u0442 \u2192 ESP32)</td>"
            "<td style='font-size:11px'>" BLE_CHAR_RX_UUID "</td><td>WRITE</td></tr>"
        "</table>");
    _server.sendContent(card("GATT UUID\u2019\u044b", uuids));
    endPage();
}

// ================================================================
//                       PAGE: MESH NETWORK (Сетевые узлы)
// ================================================================

void WebPortal::handleMesh() {
    startPage("Mesh");
    bool staConnected = _wifi && _wifi->isConnected();
    
    // Settings form
    String sform;
    sform += F("<form method='POST' action='/save/mesh'>");
    sform += checkboxField("\u0412\u043a\u043b\u044e\u0447\u0438\u0442\u044c Mesh \u0441\u0435\u0442\u044c", "mesh_en", _cfg->cfg.mesh_enabled);
    sform += inputField("SSID \u0441\u0435\u0442\u0438 Mesh", "mesh_ssid", _cfg->cfg.mesh_ssid, "text", "ESP-HUB-MESH");
    sform += inputField("\u041f\u0430\u0440\u043e\u043b\u044c Mesh (8-63 \u0441\u0438\u043c\u0432.)", "mesh_pass", _cfg->cfg.mesh_pass, "password", "1234567890");
    sform += numberField("UDP \u043f\u043e\u0440\u0442", "mesh_port", _cfg->cfg.mesh_port, 1, 65535);
    sform += numberField("Wi-Fi \u043a\u0430\u043d\u0430\u043b", "mesh_ch", _cfg->cfg.mesh_channel, 1, 13);
    if (!staConnected && _cfg->cfg.mesh_master_node) {
        sform += checkboxField("\u0413\u043b\u0430\u0432\u043d\u044b\u0439 \u0443\u0437\u0435\u043b (\u0443\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u0435 \u0432\u0441\u0435\u043c\u0438 \u0443\u0437\u043b\u0430\u043c\u0438)", "mesh_master", _cfg->cfg.mesh_master_node);
    } else {
        if (staConnected) {
            sform += F("<p class='text-muted' style='font-size:12px'>\u041f\u0435\u0440\u0435\u043a\u043b\u044e\u0447\u0430\u0442\u0435\u043b\u044c '\u0413\u043b\u0430\u0432\u043d\u044b\u0439 \u0443\u0437\u0435\u043b' \u0441\u043a\u0440\u044b\u0442, \u0442\u0430\u043a \u043a\u0430\u043a \u0443\u0441\u0442\u0440\u043e\u0439\u0441\u0442\u0432\u043e \u043f\u043e\u0434\u043a\u043b\u044e\u0447\u0435\u043d\u043e \u043a \u0432\u043d\u0435\u0448\u043d\u0435\u0439 Wi-Fi \u0441\u0435\u0442\u0438.</p>");
        } else {
            sform += F("<p class='text-muted' style='font-size:12px'>\u0414\u0430\u043d\u043d\u044b\u0439 \u0443\u0437\u0435\u043b \u0432\u0435\u0434\u043e\u043c\u044b\u0439 (NODE), \u043f\u0435\u0440\u0435\u043a\u043b\u044e\u0447\u0430\u0442\u0435\u043b\u044c '\u0413\u043b\u0430\u0432\u043d\u044b\u0439 \u0443\u0437\u0435\u043b' \u0434\u043e\u0441\u0442\u0443\u043f\u0435\u043d \u0442\u043e\u043b\u044c\u043a\u043e \u043d\u0430 \u0433\u043b\u0430\u0432\u043d\u043e\u043c \u0443\u0437\u043b\u0435.</p>");
        }
    }
    sform += F("<p class='text-muted' style='font-size:12px'>\u0414\u043b\u044f \u043f\u043e\u0434\u043a\u043b\u044e\u0447\u0435\u043d\u0438\u044f \u0432\u0442\u043e\u0440\u043e\u0433\u043e \u0443\u0441\u0442\u0440\u043e\u0439\u0441\u0442\u0432\u0430 \u0443\u043a\u0430\u0436\u0438\u0442\u0435 \u0442\u043e\u0447\u043d\u043e \u0442\u0435 \u0436\u0435 SSID, \u043f\u0430\u0440\u043e\u043b\u044c, \u043f\u043e\u0440\u0442 \u0438 \u043a\u0430\u043d\u0430\u043b \u043d\u0430 \u043e\u0431\u043e\u0438\u0445 \u0443\u0437\u043b\u0430\u0445.</p>");
    sform += submitButton("\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u0438 \u043f\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044c");
    sform += F("</form>");
    _server.sendContent(card("\u2699\uFE0F \u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 Mesh", sform));
    
    // Status card
    String st;
    st.reserve(500);
    st += F("<div class='grid2'>");
    
    // Node ID
    st += F("<div><div class='flex-between'><span>Node ID</span>");
    bool meshEnabled = _cfg->cfg.mesh_enabled;
    st += badge(meshEnabled ? "Active" : "Disabled", meshEnabled ? "badge-green" : "badge-yellow");
    st += F("</div><div class='text-muted' style='font-size:20px;font-weight:bold'>");
    if (_mesh && meshEnabled) {
        char nodeid[20];
        snprintf(nodeid, sizeof(nodeid), "0x%X", _mesh->getNodeId());
        st += nodeid;
    } else {
        st += F("--");
    }
    st += F("</div><div class='text-muted' style='margin-top:4px'>Role: ");
    st += (_cfg->cfg.mesh_master_node ? "MASTER" : "NODE");
    st += F("</div></div>");
    
    // Connected nodes
    st += F("<div><div class='flex-between'><span>\u041f\u043e\u0434\u043a\u043b\u044e\u0447\u0435\u043d\u043d\u044b\u0435 \u0443\u0437\u043b\u044b</span>");
    if (_mesh && meshEnabled && _mesh->isConnected()) {
        st += badge("Connected", "badge-green");
    } else if (meshEnabled) {
        st += badge("Offline", "badge-yellow");
    } else {
        st += badge("Disabled", "badge-blue");
    }
    st += F("</div><div class='text-muted' style='font-size:20px;font-weight:bold'>");
    if (_mesh && meshEnabled) {
        st += _mesh->getConnectedCount();
    } else {
        st += "0";
    }
    st += F("</div></div>");
    
    st += F("</div>"); // end grid2
    
    _server.sendContent(card("\u0421\u0442\u0430\u0442\u0443\u0441 \u0441\u0435\u0442\u0438", st));
    
    // Node list
    String nodes;
    nodes += F("<div id='mesh-nodes' style='overflow-x:auto'>"
              "<table style='width:100%;border-collapse:collapse;font-size:13px'>"
              "<tr><th>Node ID</th><th>\u0421\u0442\u0430\u0442\u0443\u0441</th></tr>");
    if (_mesh && meshEnabled) {
        String nodeList = _mesh->getNodeListJson();
        nodes += F("<tr><td>\u0423\u0437\u043b\u044b \u0441\u0435\u0442\u0438</td><td>");
        nodes += nodeList;
        nodes += F("</td></tr>");
    } else {
        nodes += F("<tr><td colspan='2'><span class='text-muted'>\u041c\u0435\u0448 \u0432\u044b\u043a\u043b\u044e\u0447\u0435\u043d\u0430</span></td></tr>");
    }
    nodes += F("</table></div>");
    
    _server.sendContent(card("\u0423\u0437\u043b\u044b \u0441\u0435\u0442\u0438", nodes));
    
    // Mesh configuration info
    String info;
    info += F("<table style='width:100%;border-collapse:collapse;font-size:13px'>");
    info += F("<tr><td style='width:160px'>SSID \u0441\u0435\u0442\u0438</td><td><code>");
    info += _cfg->cfg.mesh_ssid;
    info += F("</code></td></tr>");
    info += F("<tr><td>\u041f\u0430\u0440\u043e\u043b\u044c</td><td><code>");
    info += (strlen(_cfg->cfg.mesh_pass) > 0) ? "********" : "(empty)";
    info += F("</code></td></tr>");
    info += F("<tr><td>\u041f\u043e\u0440\u0442 (UDP)</td><td><code>");
    info += String(_cfg->cfg.mesh_port);
    info += F("</code></td></tr>");
    info += F("<tr><td>\u041a\u0430\u043d\u0430\u043b</td><td><code>");
    info += String(_cfg->cfg.mesh_channel);
    info += F("</code></td></tr>");
    info += F("<tr><td>\u0428\u0438\u0440\u043e\u043a\u043e\u0432\u0435\u0449\u0430\u0442\u0435\u043b\u044c</td><td>");
    if (meshEnabled) {
        info += F("<span class='badge badge-green'>\u0412\u043a\u043b\u044e\u0447\u0435\u043d</span>");
    } else {
        info += F("<span class='badge badge-blue'>\u0412\u044b\u043a\u043b\u044e\u0447\u0435\u043d</span>");
    }
    info += F("</td></tr>");
    info += F("</table>");
    
    _server.sendContent(card("\u041a\u043e\u043d\u0444\u0438\u0433\u0443\u0440\u0430\u0446\u0438\u044f Mesh", info));

    // Microchat + relay tools
    String tools;
    tools += F("<div style='display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-bottom:8px'>");
    tools += F("<button class='btn btn-secondary' type='button' onclick='meshToggleHelp()' data-i18n-ru='\u0421\u043f\u0440\u0430\u0432\u043a\u0430' data-i18n-en='Help'>\u0421\u043f\u0440\u0430\u0432\u043a\u0430</button>");
    tools += F("<div class='text-muted' id='mesh-mode-hint' data-i18n-ru='\u0427\u0430\u0442 \u0438 \u043a\u043e\u043c\u0430\u043d\u0434\u044b: all \u0438\u043b\u0438 \u043a\u043e\u043d\u043a\u0440\u0435\u0442\u043d\u044b\u0439 node' data-i18n-en='Chat and commands: all or a selected node'>\u0427\u0430\u0442 \u0438 \u043a\u043e\u043c\u0430\u043d\u0434\u044b: all \u0438\u043b\u0438 \u043a\u043e\u043d\u043a\u0440\u0435\u0442\u043d\u044b\u0439 node</div></div>");

    tools += F("<div id='mesh-help' style='display:none;background:var(--bg3);border:1px solid var(--brd);border-radius:8px;padding:10px;margin-bottom:10px'>");
    tools += F("<div id='mesh-help-content' style='white-space:pre-wrap;margin:0;font-size:12px'></div>");
    tools += F("</div>");

    tools += F("<div class='grid2'>");
    tools += F("<div><label>\u041c\u0438\u043a\u0440\u043e\u0447\u0430\u0442</label><select id='mesh-chat-target'><option value='all'>all</option></select><input type='text' id='mesh-chat-text' placeholder='\u0421\u043e\u043e\u0431\u0449\u0435\u043d\u0438\u0435 \u0432 mesh' class='mt'><button class='btn btn-secondary mt' onclick='meshSendChat()' data-i18n-ru='\u041e\u0442\u043f\u0440\u0430\u0432\u0438\u0442\u044c' data-i18n-en='Send'>\u041e\u0442\u043f\u0440\u0430\u0432\u0438\u0442\u044c</button></div>");
    tools += F("<div><label>\u041f\u0435\u0440\u0435\u0434\u0430\u0447\u0430 \u0434\u0430\u043d\u043d\u044b\u0445</label><input type='text' id='mesh-data-topic' placeholder='topic'><input type='text' id='mesh-data-payload' placeholder='payload' class='mt'><button class='btn btn-secondary mt' onclick='meshSendData()' data-i18n-ru='\u041e\u0442\u043f\u0440\u0430\u0432\u0438\u0442\u044c \u0434\u0430\u043d\u043d\u044b\u0435' data-i18n-en='Send data'>\u041e\u0442\u043f\u0440\u0430\u0432\u0438\u0442\u044c \u0434\u0430\u043d\u043d\u044b\u0435</button></div>");
    tools += F("</div>");

    tools += F("<label class='mt'>\u0423\u0434\u0430\u043b\u0451\u043d\u043d\u0430\u044f \u043a\u043e\u043c\u0430\u043d\u0434\u0430 (Serial syntax)</label>");
    tools += F("<select id='mesh-cmd-target'><option value='all'>all</option></select>");
    tools += F("<label class='mt' style='display:flex;align-items:center;gap:8px'><input type='checkbox' id='mesh-run-local' checked><span data-i18n-ru='\u0412\u044b\u043f\u043e\u043b\u043d\u044f\u0442\u044c \u043d\u0430 \u0433\u043b\u0430\u0432\u043d\u043e\u043c \u0443\u0437\u043b\u0435' data-i18n-en='Run on main node'>\u0412\u044b\u043f\u043e\u043b\u043d\u044f\u0442\u044c \u043d\u0430 \u0433\u043b\u0430\u0432\u043d\u043e\u043c \u0443\u0437\u043b\u0435</span></label>");
    tools += F("<input type='text' id='mesh-cmd-line' placeholder='\u041d\u0430\u043f\u0440\u0438\u043c\u0435\u0440: light red' class='mt'>");
    tools += F("<button class='btn btn-primary mt' onclick='meshSendCmd()' data-i18n-ru='\u0412\u044b\u043f\u043e\u043b\u043d\u0438\u0442\u044c \u043a\u043e\u043c\u0430\u043d\u0434\u0443' data-i18n-en='Run command'>\u0412\u044b\u043f\u043e\u043b\u043d\u0438\u0442\u044c \u043a\u043e\u043c\u0430\u043d\u0434\u0443</button>");

    tools += F("<div class='mt' style='display:grid;gap:8px'>");
    tools += F("<details><summary data-i18n-ru='\u041f\u0440\u0435\u0441\u0435\u0442\u044b: \u0421\u0432\u0435\u0442\u0438\u043b\u044c\u043d\u0438\u043a\u0438' data-i18n-en='Presets: Fixtures'>\u041f\u0440\u0435\u0441\u0435\u0442\u044b: \u0421\u0432\u0435\u0442\u0438\u043b\u044c\u043d\u0438\u043a\u0438</summary><div style='display:flex;gap:6px;flex-wrap:wrap;margin-top:8px'>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('light on')\" data-i18n-ru='\u0412\u043a\u043b' data-i18n-en='On'>\u0412\u043a\u043b</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('light off')\" data-i18n-ru='\u0412\u044b\u043a\u043b' data-i18n-en='Off'>\u0412\u044b\u043a\u043b</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunLightPreset('red')\" data-i18n-ru='\u041a\u0440\u0430\u0441\u043d\u044b\u0439' data-i18n-en='Red'>\u041a\u0440\u0430\u0441\u043d\u044b\u0439</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunLightPreset('blue')\" data-i18n-ru='\u0421\u0438\u043d\u0438\u0439' data-i18n-en='Blue'>\u0421\u0438\u043d\u0438\u0439</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunLightPreset('white')\" data-i18n-ru='\u0411\u0435\u043b\u044b\u0439' data-i18n-en='White'>\u0411\u0435\u043b\u044b\u0439</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunLightPreset('grow')\" data-i18n-ru='\u0420\u043e\u0441\u0442' data-i18n-en='Grow'>\u0420\u043e\u0441\u0442</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('light status')\" data-i18n-ru='\u0421\u0442\u0430\u0442\u0443\u0441' data-i18n-en='Status'>\u0421\u0442\u0430\u0442\u0443\u0441</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('dim up 10')\" data-i18n-ru='\u042f\u0440\u0447\u0435' data-i18n-en='Brighter'>\u042f\u0440\u0447\u0435</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('dim down 10')\" data-i18n-ru='\u0422\u0435\u043c\u043d\u0435\u0435' data-i18n-en='Darker'>\u0422\u0435\u043c\u043d\u0435\u0435</button>");
    tools += F("<div style='width:100%;margin-top:8px'><label data-i18n-ru='\u042f\u0440\u043a\u043e\u0441\u0442\u044c \u043f\u0440\u0435\u0441\u0435\u0442\u0430 (%)' data-i18n-en='Preset brightness (%)'>\u042f\u0440\u043a\u043e\u0441\u0442\u044c \u043f\u0440\u0435\u0441\u0435\u0442\u0430 (%)</label><input type='range' id='mesh-light-br' min='5' max='100' value='100'><div class='text-muted' id='mesh-light-br-text'>100%</div></div>");
    tools += F("</div></details>");

    tools += F("<details><summary data-i18n-ru='\u041f\u0440\u0435\u0441\u0435\u0442\u044b: Mesh \u0441\u0435\u0440\u0432\u0438\u0441' data-i18n-en='Presets: Mesh service'>\u041f\u0440\u0435\u0441\u0435\u0442\u044b: Mesh \u0441\u0435\u0440\u0432\u0438\u0441</summary><div style='display:flex;gap:6px;flex-wrap:wrap;margin-top:8px'>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('mesh status')\" data-i18n-ru='\u0421\u0442\u0430\u0442\u0443\u0441' data-i18n-en='Status'>\u0421\u0442\u0430\u0442\u0443\u0441</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('mesh nodes')\" data-i18n-ru='\u0423\u0437\u043b\u044b' data-i18n-en='Nodes'>\u0423\u0437\u043b\u044b</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('mesh log')\" data-i18n-ru='\u041b\u043e\u0433' data-i18n-en='Log'>\u041b\u043e\u0433</button>");
    tools += F("</div></details>");

    tools += F("<details><summary data-i18n-ru='\u041f\u0440\u0435\u0441\u0435\u0442\u044b: \u0421\u0435\u0442\u044c \u0438 \u0442\u0435\u043b\u0435\u043c\u0435\u0442\u0440\u0438\u044f' data-i18n-en='Presets: Network & telemetry'>\u041f\u0440\u0435\u0441\u0435\u0442\u044b: \u0421\u0435\u0442\u044c \u0438 \u0442\u0435\u043b\u0435\u043c\u0435\u0442\u0440\u0438\u044f</summary><div style='display:flex;gap:6px;flex-wrap:wrap;margin-top:8px'>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('status')\" data-i18n-ru='\u0421\u0442\u0430\u0442\u0443\u0441' data-i18n-en='Status'>\u0421\u0442\u0430\u0442\u0443\u0441</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('sensors')\" data-i18n-ru='\u0414\u0430\u0442\u0447\u0438\u043a\u0438' data-i18n-en='Sensors'>\u0414\u0430\u0442\u0447\u0438\u043a\u0438</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('read')\" data-i18n-ru='\u0421\u0447\u0438\u0442\u0430\u0442\u044c \u0441\u0435\u0439\u0447\u0430\u0441' data-i18n-en='Read now'>\u0421\u0447\u0438\u0442\u0430\u0442\u044c \u0441\u0435\u0439\u0447\u0430\u0441</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('clock')\" data-i18n-ru='\u0427\u0430\u0441\u044b' data-i18n-en='Clock'>\u0427\u0430\u0441\u044b</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('wifi status')\" data-i18n-ru='WiFi \u0441\u0442\u0430\u0442\u0443\u0441' data-i18n-en='WiFi'>WiFi \u0441\u0442\u0430\u0442\u0443\u0441</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('wifi scan')\" data-i18n-ru='\u0421\u043a\u0430\u043d WiFi' data-i18n-en='WiFi scan'>\u0421\u043a\u0430\u043d WiFi</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('mqtt status')\">MQTT</button>");
    tools += F("</div></details>");

    tools += F("<details><summary data-i18n-ru='\u041f\u0440\u0435\u0441\u0435\u0442\u044b: \u0421\u0446\u0435\u043d\u0430\u0440\u0438\u0438 \u0438 \u0442\u0430\u0439\u043c\u0435\u0440\u044b' data-i18n-en='Presets: Scenarios & Timers'>\u041f\u0440\u0435\u0441\u0435\u0442\u044b: \u0421\u0446\u0435\u043d\u0430\u0440\u0438\u0438 \u0438 \u0442\u0430\u0439\u043c\u0435\u0440\u044b</summary><div style='display:flex;gap:6px;flex-wrap:wrap;margin-top:8px'>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('timer list')\" data-i18n-ru='\u0422\u0430\u0439\u043c\u0435\u0440\u044b' data-i18n-en='Timers'>\u0422\u0430\u0439\u043c\u0435\u0440\u044b</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('timer enable 0')\" data-i18n-ru='\u0422\u0430\u0439\u043c\u0435\u0440 0 \u0432\u043a\u043b' data-i18n-en='Timer 0 on'>\u0422\u0430\u0439\u043c\u0435\u0440 0 \u0432\u043a\u043b</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('timer disable 0')\" data-i18n-ru='\u0422\u0430\u0439\u043c\u0435\u0440 0 \u0432\u044b\u043a\u043b' data-i18n-en='Timer 0 off'>\u0422\u0430\u0439\u043c\u0435\u0440 0 \u0432\u044b\u043a\u043b</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('scenario list')\" data-i18n-ru='\u0421\u0446\u0435\u043d\u0430\u0440\u0438\u0438' data-i18n-en='Scenarios'>\u0421\u0446\u0435\u043d\u0430\u0440\u0438\u0438</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('scenario enable 0')\" data-i18n-ru='\u0421\u0446\u0435\u043d\u0430\u0440\u0438\u0439 0 \u0432\u043a\u043b' data-i18n-en='Scenario 0 on'>\u0421\u0446\u0435\u043d\u0430\u0440\u0438\u0439 0 \u0432\u043a\u043b</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('scenario disable 0')\" data-i18n-ru='\u0421\u0446\u0435\u043d\u0430\u0440\u0438\u0439 0 \u0432\u044b\u043a\u043b' data-i18n-en='Scenario 0 off'>\u0421\u0446\u0435\u043d\u0430\u0440\u0438\u0439 0 \u0432\u044b\u043a\u043b</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('json')\">JSON</button>");
    tools += F("</div></details>");

    tools += F("<details><summary data-i18n-ru='\u041f\u0440\u0435\u0441\u0435\u0442\u044b: AI \u0438 Bluetooth' data-i18n-en='Presets: AI & Bluetooth'>\u041f\u0440\u0435\u0441\u0435\u0442\u044b: AI \u0438 Bluetooth</summary><div style='display:flex;gap:6px;flex-wrap:wrap;margin-top:8px'>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('ai status')\" data-i18n-ru='AI \u0441\u0442\u0430\u0442\u0443\u0441' data-i18n-en='AI status'>AI \u0441\u0442\u0430\u0442\u0443\u0441</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('ai history')\" data-i18n-ru='AI \u0438\u0441\u0442\u043e\u0440\u0438\u044f' data-i18n-en='AI history'>AI \u0438\u0441\u0442\u043e\u0440\u0438\u044f</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('ai clear')\" data-i18n-ru='AI \u043e\u0447\u0438\u0441\u0442\u0438\u0442\u044c' data-i18n-en='AI clear'>AI \u043e\u0447\u0438\u0441\u0442\u0438\u0442\u044c</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('ble status')\" data-i18n-ru='BLE \u0441\u0442\u0430\u0442\u0443\u0441' data-i18n-en='BLE status'>BLE \u0441\u0442\u0430\u0442\u0443\u0441</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('ble on')\" data-i18n-ru='BLE \u0432\u043a\u043b' data-i18n-en='BLE on'>BLE \u0432\u043a\u043b</button>");
    tools += F("<button class='btn btn-secondary' type='button' onclick=\"meshRunPreset('ble off')\" data-i18n-ru='BLE \u0432\u044b\u043a\u043b' data-i18n-en='BLE off'>BLE \u0432\u044b\u043a\u043b</button>");
    tools += F("</div></details>");
    tools += F("</div>");

    tools += F("<div id='mesh-send-status' class='text-muted mt'></div>");
    _server.sendContent(card("Mesh Чат", tools));

    String logCard;
    logCard += F("<div style='display:flex;gap:8px;flex-wrap:wrap'><button class='btn btn-secondary' onclick='meshRefreshLog()'>\u041e\u0431\u043d\u043e\u0432\u0438\u0442\u044c</button><button class='btn btn-danger' onclick='meshClearLog()'>\u041e\u0447\u0438\u0441\u0442\u0438\u0442\u044c</button></div>");
    logCard += F("<pre id='mesh-log' style='margin-top:10px;max-height:260px;overflow:auto;background:var(--bg3);border:1px solid var(--brd);padding:10px;border-radius:8px'>[]</pre>");
    _server.sendContent(card("Mesh Log", logCard));
    
    // Auto-refresh JavaScript
    _server.sendContent(F("<script>"
        "function updateMeshTargetSelect(id,nodes){"
        "  var sel=document.getElementById(id);if(!sel)return;"
        "  var prev=sel.value||'all';"
        "  var opts='<option value=\"all\">all</option>';"
        "  (nodes||[]).forEach(function(n){opts+='<option value=\"node:'+n+'\">node:'+n+'</option>';});"
        "  sel.innerHTML=opts;"
        "  sel.value=prev;"
        "  if(sel.value!==prev) sel.value='all';"
        "}"
        "function meshLang(){return localStorage.getItem('lang')||'ru';}"
        "function meshApplyI18n(){"
        "  var l=meshLang();"
        "  document.querySelectorAll('[data-i18n-ru]').forEach(function(el){"
        "    var t=el.getAttribute(l==='en'?'data-i18n-en':'data-i18n-ru');"
        "    if(t) el.textContent=t;"
        "  });"
        "}"
        "function meshRenderHelp(){"
        "  var l=meshLang();"
        "  var txt='';"
        "  if(l==='en'){"
        "    txt+='Quick command guide\\n';"
        "    txt+='1) Mesh: mesh status | mesh nodes | mesh log\\n';"
        "    txt+='2) Chat: mesh chat <text> | mesh chat node:<id> <text>\\n';"
        "    txt+='3) Commands: mesh cmd <line> | mesh cmd node:<id> <line>\\n';"
        "    txt+='4) Light: light on/off/status, dim up 10, dim down 10\\n';"
        "    txt+='Examples:\\n';"
        "    txt+='- mesh chat node:123456 hello\\n';"
        "    txt+='- mesh cmd light red\\n';"
        "    txt+='- mesh cmd node:123456 timer enable 0\\n';"
        "    txt+='Note: with target=all and switch ON, command executes on main node too.';"
        "  } else {"
        "    txt+='\u041a\u0440\u0430\u0442\u043a\u0430\u044f \u0441\u043f\u0440\u0430\u0432\u043a\u0430 \u043f\u043e \u043a\u043e\u043c\u0430\u043d\u0434\u0430\u043c\\n';"
        "    txt+='1) Mesh: mesh status | mesh nodes | mesh log\\n';"
        "    txt+='2) \u0427\u0430\u0442: mesh chat <\u0442\u0435\u043a\u0441\u0442> | mesh chat node:<id> <\u0442\u0435\u043a\u0441\u0442>\\n';"
        "    txt+='3) \u041a\u043e\u043c\u0430\u043d\u0434\u044b: mesh cmd <\u043a\u043e\u043c\u0430\u043d\u0434\u0430> | mesh cmd node:<id> <\u043a\u043e\u043c\u0430\u043d\u0434\u0430>\\n';"
        "    txt+='4) \u0421\u0432\u0435\u0442: light on/off/status, dim up 10, dim down 10\\n';"
        "    txt+='\u041f\u0440\u0438\u043c\u0435\u0440\u044b:\\n';"
        "    txt+='- mesh chat node:123456 \u041f\u0440\u0438\u0432\u0435\u0442\\n';"
        "    txt+='- mesh cmd light red\\n';"
        "    txt+='- mesh cmd node:123456 timer enable 0\\n';"
        "    txt+='\u041f\u0440\u0438\u043c\u0435\u0447\u0430\u043d\u0438\u0435: при target=all и включенном переключателе команда выполняется и на главном узле.';"
        "  }"
        "  var el=document.getElementById('mesh-help-content');"
        "  if(el) el.textContent=txt;"
        "}"
        "function meshUpdateBrightnessLabel(){"
        "  var s=document.getElementById('mesh-light-br');"
        "  var t=document.getElementById('mesh-light-br-text');"
        "  if(s&&t) t.textContent=(s.value||100)+'%';"
        "}"
        "function updateMeshStatus(){"
        "  fetch('/api/mesh').then(r=>r.json()).then(d=>{"
        "    updateMeshTargetSelect('mesh-chat-target',d.nodes||[]);"
        "    updateMeshTargetSelect('mesh-cmd-target',d.nodes||[]);"
        "    meshApplyI18n();"
        "    meshRenderHelp();"
        "    meshUpdateBrightnessLabel();"
        "  });"
        "}"
        "function meshPost(url,data){"
        "  return fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:data}).then(function(r){return r.json();});"
        "}"
        "function meshSetStatus(t){var el=document.getElementById('mesh-send-status');if(el)el.textContent=t;}"
        "function meshToggleHelp(){"
        "  var el=document.getElementById('mesh-help');if(!el)return;"
        "  el.style.display=(el.style.display==='none'||!el.style.display)?'block':'none';"
        "}"
        "function meshSendChat(){"
        "  var text=(document.getElementById('mesh-chat-text')||{}).value||'';"
        "  var target=(document.getElementById('mesh-chat-target')||{}).value||'all';"
        "  if(!text.trim()) return;"
        "  meshPost('/api/mesh/chat','target='+encodeURIComponent(target)+'&text='+encodeURIComponent(text)).then(function(d){meshSetStatus(d.ok?'chat sent':'chat error');meshRefreshLog();});"
        "}"
        "function meshSendData(){"
        "  var topic=(document.getElementById('mesh-data-topic')||{}).value||'raw';"
        "  var payload=(document.getElementById('mesh-data-payload')||{}).value||'';"
        "  meshPost('/api/mesh/data','topic='+encodeURIComponent(topic)+'&payload='+encodeURIComponent(payload)).then(function(d){meshSetStatus(d.ok?'data sent':'data error');meshRefreshLog();});"
        "}"
        "function meshSendCmd(){"
        "  var cmd=(document.getElementById('mesh-cmd-line')||{}).value||'';"
        "  var target=(document.getElementById('mesh-cmd-target')||{}).value||'all';"
        "  var runLocal=((document.getElementById('mesh-run-local')||{}).checked)?'1':'0';"
        "  if(!cmd.trim()) return;"
        "  meshPost('/api/mesh/cmd','target='+encodeURIComponent(target)+'&run_local='+runLocal+'&cmd='+encodeURIComponent(cmd)).then(function(d){meshSetStatus(d.ok?'command done':'command error');meshRefreshLog();});"
        "}"
        "function meshRunPreset(cmd){"
        "  var inp=document.getElementById('mesh-cmd-line');"
        "  if(inp) inp.value=cmd;"
        "  meshSendCmd();"
        "}"
        "function meshRunLightPreset(mode){"
        "  var s=document.getElementById('mesh-light-br');"
        "  var p=parseInt((s&&s.value)?s.value:'100',10);"
        "  if(isNaN(p)||p<1)p=100;"
        "  var v=Math.max(1,Math.min(200,Math.round(p*2)));"
        "  var cmd='';"
        "  if(mode==='red') cmd='R'+v;"
        "  else if(mode==='blue') cmd='B'+v;"
        "  else if(mode==='white') cmd='W'+v;"
        "  else if(mode==='grow'){"
        "    var k=v/200.0;"
        "    var r=Math.max(1,Math.round(140*k));"
        "    var fr=Math.max(1,Math.round(100*k));"
        "    var b=Math.max(1,Math.round(100*k));"
        "    var w=Math.max(1,Math.round(60*k));"
        "    cmd='R'+r+' FR'+fr+' B'+b+' W'+w;"
        "  }"
        "  if(cmd) meshRunPreset(cmd);"
        "}"
        "function meshRefreshLog(){"
        "  fetch('/api/mesh/log').then(r=>r.json()).then(d=>{"
        "    var arr=d.log||[];"
        "    var el=document.getElementById('mesh-log');"
        "    if(el) el.textContent=arr.join('\\n');"
        "  });"
        "}"
        "function meshClearLog(){"
        "  meshPost('/api/mesh/log/clear','').then(()=>meshRefreshLog());"
        "}"
        "(function(){var s=document.getElementById('mesh-light-br');if(s)s.addEventListener('input',meshUpdateBrightnessLabel);})();"
        "meshApplyI18n();"
        "meshRenderHelp();"
        "meshUpdateBrightnessLabel();"
        "updateMeshStatus();"
        "meshRefreshLog();"
        "setInterval(updateMeshStatus,5000);"
        "setInterval(meshRefreshLog,4000);"
        "</script>"));
    
    endPage();
}

void WebPortal::handleApiMeshStatus() {
    String json = "{";
    json += F("\"enabled\":");
    json += _cfg->cfg.mesh_enabled ? "true" : "false";
    json += F(",\"master\":");
    json += _cfg->cfg.mesh_master_node ? "true" : "false";
    json += F(",\"ssid\":\"");
    json += _cfg->cfg.mesh_ssid;
    json += F("\"");
    json += F(",\"port\":");
    json += _cfg->cfg.mesh_port;
    json += F(",\"channel\":");
    json += _cfg->cfg.mesh_channel;
    json += F(",\"masterSwitchVisible\":");
    json += (_wifi && _wifi->isConnected()) ? "false" : "true";
    json += F(",\"status\":\"");
    if (_mesh && _cfg->cfg.mesh_enabled) {
        json += (_mesh->isConnected() ? "connected" : "offline");
        json += F("\",\"nodeId\":");
        json += _mesh->getNodeId();
        json += F(",\"connectedCount\":");
        json += _mesh->getConnectedCount();
        json += F(",\"nodes\":");
        json += _mesh->getNodeListJson();
    } else {
        json += F("unavailable\",\"nodeId\":0,\"connectedCount\":0,\"nodes\":[]");
    }
    json += "}";
    _server.send(200, "application/json", json);
}

void WebPortal::handleApiMeshSendChat() {
    if (!_mesh || !_cfg->cfg.mesh_enabled) {
        _server.send(400, "application/json", "{\"ok\":false,\"error\":\"mesh disabled\"}");
        return;
    }
    String target = _server.arg("target");
    target.trim();
    if (target.length() == 0) target = "all";

    String text = _server.arg("text");
    text.trim();
    if (text.length() == 0) {
        _server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty text\"}");
        return;
    }
    const char* from = (_cfg && strlen(_cfg->cfg.device_name)) ? _cfg->cfg.device_name : "ESP-HUB";
    _mesh->sendChatMessage(from, text, target);
    _server.send(200, "application/json", "{\"ok\":true}");
}

void WebPortal::handleApiMeshSendData() {
    if (!_mesh || !_cfg->cfg.mesh_enabled) {
        _server.send(400, "application/json", "{\"ok\":false,\"error\":\"mesh disabled\"}");
        return;
    }
    String topic = _server.arg("topic");
    String payload = _server.arg("payload");
    topic.trim();
    if (topic.length() == 0) topic = "raw";
    _mesh->sendDataMessage(topic, payload);
    _server.send(200, "application/json", "{\"ok\":true}");
}

void WebPortal::handleApiMeshSendCmd() {
    if (!_mesh || !_cfg->cfg.mesh_enabled) {
        _server.send(400, "application/json", "{\"ok\":false,\"error\":\"mesh disabled\"}");
        return;
    }
    String target = _server.arg("target");
    target.trim();
    if (target.length() == 0) target = "all";

    bool runLocalFlag = true;
    if (_server.hasArg("run_local")) {
        String runLocalArg = _server.arg("run_local");
        runLocalArg.trim();
        runLocalFlag = (runLocalArg != "0" && runLocalArg != "false" && runLocalArg != "off");
    }

    String cmd = _server.arg("cmd");
    cmd.trim();
    if (cmd.length() == 0) {
        _server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty cmd\"}");
        return;
    }
    if (isLoopProneMeshCommand(cmd)) {
        _server.send(400, "application/json", "{\"ok\":false,\"error\":\"loop-prone mesh command blocked\"}");
        return;
    }

    uint32_t myNode = _mesh->getNodeId();
    String selfTarget = String("node:") + String(myNode);
    bool runLocal = runLocalFlag && (target == "all" || target == selfTarget);
    const char* myRole = _cfg->cfg.mesh_master_node ? "MAIN" : "NODE";
    _mesh->addLogEntry(String("CMD API role=") + myRole + " node=" + String(myNode) + " target=" + target + " run_local=" + (runLocalFlag ? "1" : "0") + " cmd=" + cmd);

    if (runLocal) {
        Serial.printf("[MESH] EXEC API LOCAL role=%s node=%u target=%s cmd=%s\n", myRole, (unsigned)myNode, target.c_str(), cmd.c_str());
        serialCon.executeCommand(cmd);
    }

    if (target != selfTarget) {
        _mesh->sendCommandMessage(cmd, target, (uint32_t)millis());
    }

    _server.send(200, "application/json", "{\"ok\":true}");
}

void WebPortal::handleApiMeshLog() {
    String json = "{\"log\":";
    if (_mesh && _cfg->cfg.mesh_enabled) {
        json += _mesh->getLogJson(24);
    } else {
        json += "[]";
    }
    json += "}";
    _server.send(200, "application/json", json);
}

void WebPortal::handleApiMeshLogClear() {
    if (_mesh) _mesh->clearLog();
    _server.send(200, "application/json", "{\"ok\":true}");
}

// ================================================================
//                       PAGE: FIXTURES (Светильники)
// ================================================================

void WebPortal::handleFixtures() {
    startPage("\u0421\u0432\u0435\u0442\u0438\u043b\u044c\u043d\u0438\u043a\u0438");
    
    // Status card
    String st;
    st += F("<div class='grid2'>");

    st += F("<div><div class='flex-between'><span data-t='fixture-status'>Status</span>");
    if (!_fixture->isEnabled()) {
        st += badge("<span data-t='fixture-disabled'>Disabled</span>", "badge-blue");
    } else {
        st += badge("<span data-t='fixture-enabled'>Enabled</span>", "badge-green");
        bool is_on = (_cfg->cfg.fixture.red_brightness > 0 || _cfg->cfg.fixture.far_red_brightness > 0 || _cfg->cfg.fixture.blue_brightness > 0 || _cfg->cfg.fixture.white_brightness > 0);
        st += F("<label class='toggle' style='margin:0'><input type='checkbox' id='fix-pwr' onchange='togglePower(this)'");
        if (is_on) st += F(" checked");
        st += F("><span class='slider'></span></label>");
    }
    st += F("</div>");
    if (_fixture->isEnabled()) {
        st += F("<div class='text-muted'>UART2: TX=17, RX=16, 9600 baud</div>");
        if (_fixture->isLastAckOk()) {
            st += F("<div class='badge badge-green' style='margin-top:4px'><span data-t='fixture-ack-ok'>ACK OK</span></div>");
        } else {
            st += F("<div class='badge badge-red' style='margin-top:4px'><span data-t='fixture-ack-fail'>ACK FAIL</span></div>");
        }
    }
    st += F("</div>");

    // Current brightness values
    st += F("<div><div class='flex-between'><span data-t='fixture-current'>Current Brightness</span></div>");
    st += F("<div class='grid2' style='margin-top:8px'>");
    st += F("<div class='s-row'><span class='s-label'><span data-t='color-red'>Red</span> (CH1)</span><br>");
    st += F("<span class='s-val' id='cur-red'>");
    st += String(_fixture->getRed() * 0.5, 1);
    st += F("%</span></div>");
    st += F("<div class='s-row'><span class='s-label'><span data-t='color-far-red'>Far Red</span> (CH2)</span><br>");
    st += F("<span class='s-val' id='cur-fr'>");
    st += String(_fixture->getFarRed() * 0.5, 1);
    st += F("%</span></div>");
    st += F("<div class='s-row'><span class='s-label'><span data-t='color-blue'>Blue</span> (CH3)</span><br>");
    st += F("<span class='s-val' id='cur-blue'>");
    st += String(_fixture->getBlue() * 0.5, 1);
    st += F("%</span></div>");
    st += F("<div class='s-row'><span class='s-label'><span data-t='color-white'>White</span> (CH4)</span><br>");
    st += F("<span class='s-val' id='cur-white'>");
    st += String(_fixture->getWhite() * 0.5, 1);
    st += F("%</span></div>");
    st += F("</div></div>");
    
    st += F("</div>"); // close grid2

    _server.sendContent(card("<span data-t='fixture-title'>Fixture</span>", st));

    // Control form
    String ctrl;
    ctrl += F("<form id='fixture-form' onsubmit='return false'>");
    ctrl += F("<div class='grid2'>");

    // Red slider with number input
    ctrl += F("<div style='display:flex;flex-direction:column;gap:4px'>");
    ctrl += F("<label><span data-t='color-red'>Red</span> (CH1): <span id='red-val'>");
    ctrl += String(_cfg->cfg.fixture.red_brightness * 0.5, 1);
    ctrl += F("%</span></label>");
    ctrl += F("<div style='display:flex;gap:8px;align-items:center'>");
    ctrl += F("<input type='range' id='red' name='red' min='0' max='200' value='");
    ctrl += _cfg->cfg.fixture.red_brightness;
    ctrl += F("' oninput='syncSlider(\"red\",this.value)' style='flex:1'>");
    ctrl += F("<input type='number' id='red-num' min='0' max='100' step='0.5' value='");
    ctrl += String(_cfg->cfg.fixture.red_brightness * 0.5, 1);
    ctrl += F("' oninput='syncNum(\"red\",this.value)' style='width:70px'>");
    ctrl += F("<span style='min-width:30px'>%</span>");
    ctrl += F("</div></div>");

    // Far Red slider with number input
    ctrl += F("<div style='display:flex;flex-direction:column;gap:4px'>");
    ctrl += F("<label><span data-t='color-far-red'>Far Red</span> (CH2): <span id='fr-val'>");
    ctrl += String(_cfg->cfg.fixture.far_red_brightness * 0.5, 1);
    ctrl += F("%</span></label>");
    ctrl += F("<div style='display:flex;gap:8px;align-items:center'>");
    ctrl += F("<input type='range' id='fr' name='fr' min='0' max='200' value='");
    ctrl += _cfg->cfg.fixture.far_red_brightness;
    ctrl += F("' oninput='syncSlider(\"fr\",this.value)' style='flex:1'>");
    ctrl += F("<input type='number' id='fr-num' min='0' max='100' step='0.5' value='");
    ctrl += String(_cfg->cfg.fixture.far_red_brightness * 0.5, 1);
    ctrl += F("' oninput='syncNum(\"fr\",this.value)' style='width:70px'>");
    ctrl += F("<span style='min-width:30px'>%</span>");
    ctrl += F("</div></div>");

    // Blue slider with number input
    ctrl += F("<div style='display:flex;flex-direction:column;gap:4px'>");
    ctrl += F("<label><span data-t='color-blue'>Blue</span> (CH3): <span id='blue-val'>");
    ctrl += String(_cfg->cfg.fixture.blue_brightness * 0.5, 1);
    ctrl += F("%</span></label>");
    ctrl += F("<div style='display:flex;gap:8px;align-items:center'>");
    ctrl += F("<input type='range' id='blue' name='blue' min='0' max='200' value='");
    ctrl += _cfg->cfg.fixture.blue_brightness;
    ctrl += F("' oninput='syncSlider(\"blue\",this.value)' style='flex:1'>");
    ctrl += F("<input type='number' id='blue-num' min='0' max='100' step='0.5' value='");
    ctrl += String(_cfg->cfg.fixture.blue_brightness * 0.5, 1);
    ctrl += F("' oninput='syncNum(\"blue\",this.value)' style='width:70px'>");
    ctrl += F("<span style='min-width:30px'>%</span>");
    ctrl += F("</div></div>");

    // White slider with number input
    ctrl += F("<div style='display:flex;flex-direction:column;gap:4px'>");
    ctrl += F("<label><span data-t='color-white'>White</span> (CH4): <span id='white-val'>");
    ctrl += String(_cfg->cfg.fixture.white_brightness * 0.5, 1);
    ctrl += F("%</span></label>");
    ctrl += F("<div style='display:flex;gap:8px;align-items:center'>");
    ctrl += F("<input type='range' id='white' name='white' min='0' max='200' value='");
    ctrl += _cfg->cfg.fixture.white_brightness;
    ctrl += F("' oninput='syncSlider(\"white\",this.value)' style='flex:1'>");
    ctrl += F("<input type='number' id='white-num' min='0' max='100' step='0.5' value='");
    ctrl += String(_cfg->cfg.fixture.white_brightness * 0.5, 1);
    ctrl += F("' oninput='syncNum(\"white\",this.value)' style='width:70px'>");
    ctrl += F("<span style='min-width:30px'>%</span>");
    ctrl += F("</div></div>");

    ctrl += F("</div>"); // close grid2

    // Preset buttons
    ctrl += F("<div style='margin-top:12px;display:flex;gap:6px;flex-wrap:wrap'>");
    ctrl += F("<button class='btn btn-secondary' type='button' onclick='setFixture(0)'><span data-t='fixture-preset-off'>\u0412\u044b\u043a\u043b</span></button>");
    ctrl += F("<button class='btn btn-secondary' type='button' onclick='setFixture(1)'><span data-t='color-red'>Red</span></button>");
    ctrl += F("<button class='btn btn-secondary' type='button' onclick='setFixture(2)'><span data-t='color-far-red'>Far Red</span></button>");
    ctrl += F("<button class='btn btn-secondary' type='button' onclick='setFixture(3)'><span data-t='color-blue'>Blue</span></button>");
    ctrl += F("<button class='btn btn-secondary' type='button' onclick='setFixture(4)'><span data-t='color-white'>White</span></button>");
    ctrl += F("<button class='btn btn-secondary' type='button' onclick='setFixture(5)'><span data-t='fixture-preset-full'>\u041f\u043e\u043b\u043d\u044b\u0439</span></button>");
    ctrl += F("<button class='btn btn-secondary' type='button' onclick='setFixture(6)'><span data-t='fixture-preset-grow'>\u0420\u043e\u0441\u0442</span></button>");
    ctrl += F("</div>");

    // Action buttons
    ctrl += F("<div style='margin-top:12px;display:flex;gap:8px'>");
    ctrl += F("<button class='btn btn-primary' type='button' onclick='applyFixture()' data-t='fixture-apply'>\u041f\u0440\u0438\u043c\u0435\u043d\u0438\u0442\u044c</button>");
    ctrl += F("<button class='btn btn-secondary' type='button' onclick='saveFixture()' data-t='fixture-save'>\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c</button>");
    ctrl += F("</div>");
    ctrl += F("</form>");

    _server.sendContent(card("<span data-t='fixture-control'>Control</span>", ctrl));

    // Settings form
    String settings;
    settings += F("<form method='POST' action='/save/fixture'>");
    settings += checkboxField("<span data-t='fixture-enable'>Enable fixture control</span>", "fixture_en", _cfg->cfg.fixture.enabled);
    settings += F("<div class='grid2'>");
    settings += numberField("UART TX pin", "fixture_tx", _cfg->cfg.fixture.uart_tx_pin, 0, 48);
    settings += numberField("UART RX pin", "fixture_rx", _cfg->cfg.fixture.uart_rx_pin, 0, 48);
    settings += numberField("Baud rate", "fixture_baud", _cfg->cfg.fixture.uart_baud, 1200, 115200);
    settings += F("</div>");
    settings += submitButton("<span data-t='fixture-save-reboot'>Save & Reboot</span>");
    settings += F("</form>");
    _server.sendContent(card("<span data-t='fixture-settings'>Settings</span>", settings));

    // Fixture Scenarios
    String scen;
    scen += F("<form method='POST' action='/save/fixture-scenarios'>");
    scen += F("<div style='overflow-x:auto;'>");
    scen += F("<table style='width:100%;text-align:left;border-collapse:collapse;min-width:600px;'>");
    scen += F("<tr>");
    scen += F("<th style='padding:8px'>En</th>");
    scen += F("<th style='padding:8px'>Time (H:M:S)</th>");
    scen += F("<th style='padding:8px'><span data-t='color-red'>Red</span> %</th>");
    scen += F("<th style='padding:8px'><span data-t='color-far-red'>FR</span> %</th>");
    scen += F("<th style='padding:8px'><span data-t='color-blue'>Blue</span> %</th>");
    scen += F("<th style='padding:8px'><span data-t='color-white'>White</span> %</th>");
    scen += F("<th style='padding:8px'>Action</th>");
    scen += F("</tr>");

    // Show all fixture scenarios (8 total)
    int visibleSc = MAX_FIXTURE_SCENARIOS;

    for (int i = 0; i < MAX_FIXTURE_SCENARIOS; i++) {
        FixtureScenario& sc = _cfg->cfg.fixture.scenarios[i];
        scen += F("<tr id='sc_row_");
        scen += String(i);
        if (i < visibleSc) {
            scen += F("' style='border-bottom:1px solid var(--brd);'>");
        } else {
            scen += F("' style='border-bottom:1px solid var(--brd);display:none'>");
        }

        scen += F("<td style='padding:8px 0'><label class='toggle'><input type='checkbox' name='sc_en_");
        scen += String(i);
        scen += F("'");
        if (sc.enabled) scen += F(" checked");
        scen += F("><span class='slider'></span></label></td>");

        scen += F("<td style='padding:8px 0'>");
        scen += F("<input type='number' name='sc_h_");
        scen += String(i);
        scen += F("' min='0' max='23' value='");
        scen += String(sc.start_hour);
        scen += F("' style='width:50px;background:var(--bg2);color:var(--txt);border:1px solid var(--brd);border-radius:4px;padding:4px'> : ");
        scen += F("<input type='number' name='sc_m_");
        scen += String(i);
        scen += F("' min='0' max='59' value='");
        scen += String(sc.start_minute);
        scen += F("' style='width:50px;background:var(--bg2);color:var(--txt);border:1px solid var(--brd);border-radius:4px;padding:4px'> : ");
        scen += F("<input type='number' name='sc_s_");
        scen += String(i);
        scen += F("' min='0' max='59' value='");
        scen += String(sc.start_second);
        scen += F("' style='width:50px;background:var(--bg2);color:var(--txt);border:1px solid var(--brd);border-radius:4px;padding:4px'>");
        scen += F("</td>");

        scen += F("<td style='padding:8px 0'><input type='number' name='sc_r_");
        scen += String(i);
        scen += F("' min='0' max='100' step='0.5' value='");
        scen += String(sc.red * 0.5, 1);
        scen += F("' style='width:70px;background:var(--bg2);color:var(--txt);border:1px solid var(--brd);border-radius:4px;padding:4px'></td>");

        scen += F("<td style='padding:8px 0'><input type='number' name='sc_fr_");
        scen += String(i);
        scen += F("' min='0' max='100' step='0.5' value='");
        scen += String(sc.far_red * 0.5, 1);
        scen += F("' style='width:70px;background:var(--bg2);color:var(--txt);border:1px solid var(--brd);border-radius:4px;padding:4px'></td>");

        scen += F("<td style='padding:8px 0'><input type='number' name='sc_b_");
        scen += String(i);
        scen += F("' min='0' max='100' step='0.5' value='");
        scen += String(sc.blue * 0.5, 1);
        scen += F("' style='width:70px;background:var(--bg2);color:var(--txt);border:1px solid var(--brd);border-radius:4px;padding:4px'></td>");

        scen += F("<td style='padding:8px 0'><input type='number' name='sc_w_");
        scen += String(i);
        scen += F("' min='0' max='100' step='0.5' value='");
        scen += String(sc.white * 0.5, 1);
        scen += F("' style='width:70px;background:var(--bg2);color:var(--txt);border:1px solid var(--brd);border-radius:4px;padding:4px'></td>");

        scen += F("<td style='padding:8px 0;text-align:center;white-space:nowrap'>");
        scen += F("<button type='button' class='btn btn-sm btn-secondary dup-sc-btn' data-idx='");
        scen += String(i);
        scen += F("' style='padding:4px 7px;font-size:12px;margin-right:2px' title='Дублировать'>+</button>");
        scen += F("<button type='button' class='btn btn-sm btn-danger del-sc-btn' data-idx='");
        scen += String(i);
        scen += F("' style='padding:4px 7px;font-size:12px' title='Удалить'>&#8722;</button>");
        scen += F("</td>");

        scen += F("</tr>");
    }

    scen += F("</table>");
    scen += F("<div style='margin-top:10px;display:flex;gap:6px;flex-wrap:wrap'>");
    scen += F("<button type='button' class='btn btn-secondary' onclick='enableAllScenarios()'>");
    scen += F("<span data-t='sc-en-all'>Включить все</span></button>");
    scen += F("<button type='button' class='btn btn-secondary' onclick='disableAllScenarios()'>");
    scen += F("<span data-t='sc-dis-all'>Выключить все</span></button>");
    scen += submitButton("<span data-t='save-apply'>Save & Apply</span>");
    scen += F("</div></div></form>");

    _server.sendContent(card("<span data-t='scenario-title'>Scenarios</span>", scen));

    // ---- Fixture Interval Timers ----
    _server.sendContent(F("<div class='ft-section'><div class='card'><h3><span data-t='ft-title'>&#9201; Интервальные таймеры</span></h3>"));
    _server.sendContent(F("<p style='color:var(--txt2);font-size:13px;margin-top:0;background:var(--bg2);padding:8px;border-left:3px solid #ff9800'>"));
    _server.sendContent(F("<b><span data-t='ft-title'>Интервальные таймеры</span></b> (MAX="));
    _server.sendContent(String(MAX_FIXTURE_TIMERS));
    _server.sendContent(F(")<br/><span data-t='ft-desc'>— повторяются каждые N секунд <b>без NTP</b>. PULSE_CUSTOM: включить на dur.мс, затем выключить.</span></p>"));
    _server.sendContent(F("<form method='POST' action='/save/fixture-timers'><div style='overflow-x:auto'><table style='width:100%;text-align:left;border-collapse:collapse;min-width:600px;font-size:13px;border:1px solid var(--brd)'>"));
    _server.sendContent(F("<tr style='color:var(--txt2);border-bottom:1px solid var(--brd)'><th style='padding:6px 4px'><span data-t='ft-col-en'>Вкл</span></th><th style='padding:6px 4px'><span data-t='ft-col-name'>Название</span></th><th style='padding:6px 4px'><span data-t='ft-col-act'>Действие</span></th><th style='padding:6px 4px'>H</th><th style='padding:6px 4px'>M</th><th style='padding:6px 4px'>S</th><th style='padding:6px 4px'><span data-t='ft-dur'>dur(мс)</span></th><th style='padding:6px 4px'><span data-t='ft-rh'>Rч</span></th><th style='padding:6px 4px'><span data-t='ft-rm'>Rм</span></th><th style='padding:6px 4px'><span data-t='ft-rs'>Rс</span></th><th style='padding:6px 4px'>R%</th><th style='padding:6px 4px'>FR%</th><th style='padding:6px 4px'>B%</th><th style='padding:6px 4px'>W%</th><th style='padding:6px 4px'><span data-t='ft-col-ctrl'>Действ.</span></th></tr>"));

    const char* inp = "style='width:48px;background:var(--bg2);color:var(--txt);border:1px solid var(--brd);border-radius:4px;padding:3px'";
    const char* actNames[] = {
        "OFF","GROW","FULL","RED","BLUE","PULSE_GROW","PULSE_FULL","CUSTOM","PULSE_CUSTOM"
    };

    for (int i = 0; i < MAX_FIXTURE_TIMERS; i++) {
        FixtureTimerConfig& t = _cfg->cfg.fixture.timers[i];
        String si = String(i);
        String row;
        row.reserve(1800);
        row += F("<tr id='ft_row_");
        row += si;
        row += F("' style='border-bottom:1px solid var(--brd2);'>");
        row += F("<td style='padding:6px 4px'><label class='toggle'><input type='checkbox' name='ften_"); row += si; row += "'";
        if (t.enabled) row += F(" checked");
        row += F("><span class='slider'></span></label></td>");
        row += F("<td style='padding:6px 4px'><input type='text' name='ftlbl_"); row += si;
        row += F("' value='"); row += htmlAttr(t.label);
        row += F("' maxlength='15' style='width:90px;background:var(--bg2);color:var(--txt);border:1px solid var(--brd);border-radius:4px;padding:3px'></td>");
        row += F("<td style='padding:6px 4px'><select name='ftact_"); row += si;
        row += F("' style='background:var(--bg2);color:var(--txt);border:1px solid var(--brd);border-radius:4px;padding:3px'>");
        for (int a = 0; a < FIX_TIMER_ACTION_COUNT; a++) {
            row += F("<option value='"); row += a; row += "'";
            if ((int)t.action == a) row += F(" selected");
            row += ">"; row += actNames[a]; row += F("</option>");
        }
        row += F("</select></td>");
        row += F("<td style='padding:6px 4px'><input type='number' name='fth_"); row += si; row += F("' value='"); row += t.hours; row += F("' min='0' max='23' "); row += inp; row += F("></td>");
        row += F("<td style='padding:6px 4px'><input type='number' name='ftm_"); row += si; row += F("' value='"); row += t.minutes; row += F("' min='0' max='59' "); row += inp; row += F("></td>");
        row += F("<td style='padding:6px 4px'><input type='number' name='fts_"); row += si; row += F("' value='"); row += t.seconds; row += F("' min='0' max='59' "); row += inp; row += F("></td>");
        row += F("<td style='padding:6px 4px'><input type='number' name='ftdur_"); row += si; row += F("' value='"); row += t.duration_ms; row += F("' min='1' max='60000' style='width:70px;background:var(--bg2);color:var(--txt);border:1px solid var(--brd);border-radius:4px;padding:3px'></td>");
        row += F("<td style='padding:6px 4px'><input type='number' name='ftrun_h_"); row += si; row += F("' value='"); row += t.run_hours; row += F("' min='0' max='23' "); row += inp; row += F("></td>");
        row += F("<td style='padding:6px 4px'><input type='number' name='ftrun_m_"); row += si; row += F("' value='"); row += t.run_minutes; row += F("' min='0' max='59' "); row += inp; row += F("></td>");
        row += F("<td style='padding:6px 4px'><input type='number' name='ftrun_s_"); row += si; row += F("' value='"); row += t.run_seconds; row += F("' min='0' max='59' "); row += inp; row += F("></td>");
        auto pct = [](uint8_t v){ return String(v * 0.5f, 1); };
        row += F("<td style='padding:6px 4px'><input type='number' name='ftr_"); row += si; row += F("' value='"); row += pct(t.red); row += F("' min='0' max='100' step='0.5' "); row += inp; row += F("></td>");
        row += F("<td style='padding:6px 4px'><input type='number' name='ftfr_"); row += si; row += F("' value='"); row += pct(t.far_red); row += F("' min='0' max='100' step='0.5' "); row += inp; row += F("></td>");
        row += F("<td style='padding:6px 4px'><input type='number' name='ftb_"); row += si; row += F("' value='"); row += pct(t.blue); row += F("' min='0' max='100' step='0.5' "); row += inp; row += F("></td>");
        row += F("<td style='padding:6px 4px'><input type='number' name='ftw_"); row += si; row += F("' value='"); row += pct(t.white); row += F("' min='0' max='100' step='0.5' "); row += inp; row += F("></td>");
        row += F("<td style='padding:6px 4px;text-align:center;white-space:nowrap'>");
        row += F("<button type='button' class='btn btn-sm btn-secondary dup-tm-btn' data-idx='"); row += si;
        row += F("' style='padding:3px 5px;font-size:11px;margin-right:2px' title='Дублировать'>+</button>");
        row += F("<button type='button' class='btn btn-sm btn-danger del-tm-btn' data-idx='"); row += si;
        row += F("' style='padding:3px 5px;font-size:11px' title='Удалить'>&#8722;</button></td></tr>");
        _server.sendContent(row);
    }

    _server.sendContent(F("</table></div><div style='margin-top:10px;display:flex;gap:6px;flex-wrap:wrap'>"));
    _server.sendContent(F("<button type='button' class='btn btn-secondary' onclick='enableAllTimers()'><span data-t='ft-en-all'>Включить все</span></button>"));
    _server.sendContent(F("<button type='button' class='btn btn-secondary' onclick='disableAllTimers()'><span data-t='ft-dis-all'>Выключить все</span></button>"));
    _server.sendContent(F("<button type='submit' class='btn btn-primary'><span data-t='ft-save'>Сохранить таймеры</span></button></div></form></div></div>"));
    _server.sendContent(F("<script>console.log('[SERVER] Generated fixture timers, MAX="));
    _server.sendContent(String(MAX_FIXTURE_TIMERS));
    _server.sendContent(F("');</script></div>"));

    // JavaScript for live control
    _server.sendContent(F("<script>"
        "function togglePower(checkbox){"
        "  if(checkbox.checked){"
        "    var red=parseInt(document.getElementById('red').value);"
        "    var fr=parseInt(document.getElementById('fr').value);"
        "    var blue=parseInt(document.getElementById('blue').value);"
        "    var white=parseInt(document.getElementById('white').value);"
        "    console.log('[POWER] ON: R='+red+' FR='+fr+' B='+blue+' W='+white);"
        "    if(red===0 && fr===0 && blue===0 && white===0) {"
        "      white = 20;"
        "      document.getElementById('white').value = 20;"
        "      if(document.getElementById('white-num')) document.getElementById('white-num').value = '10.0';"
        "      console.log('[POWER] All zero, setting white=20');"
        "    }"
        "    fetch('/api/fixture/set',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "      body:'red='+red+'&fr='+fr+'&blue='+blue+'&white='+white})"
        "      .then(r=>{console.log('[POWER] ON sent, response:',r.status);"
        "        document.getElementById('cur-red').textContent=(red*0.5).toFixed(1)+'%';"
        "        document.getElementById('cur-fr').textContent=(fr*0.5).toFixed(1)+'%';"
        "        document.getElementById('cur-blue').textContent=(blue*0.5).toFixed(1)+'%';"
        "        document.getElementById('cur-white').textContent=(white*0.5).toFixed(1)+'%';"
        "      })"
        "      .catch(e=>{console.log('[POWER] Error:',e);checkbox.checked=false;});"
        "  }else{"
        "    console.log('[POWER] OFF');"
        "    fetch('/api/fixture/set',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "      body:'red=0&fr=0&blue=0&white=0'})"
        "      .then(r=>{console.log('[POWER] OFF sent, response:',r.status);"
        "        document.getElementById('cur-red').textContent='0.0%';"
        "        document.getElementById('cur-fr').textContent='0.0%';"
        "        document.getElementById('cur-blue').textContent='0.0%';"
        "        document.getElementById('cur-white').textContent='0.0%';"
        "      })"
        "      .catch(()=>checkbox.checked=true);"
        "  }"
        "}"
        "function syncSlider(id,val){"
        "  var slider=document.getElementById(id);"
        "  var num=document.getElementById(id+'-num');"
        "  var lbl=document.getElementById(id+'-val');"
        "  if(slider){slider.value=val;}"
        "  if(num){num.value=(val*0.5).toFixed(1);}"
        "  if(lbl){lbl.textContent=(val*0.5).toFixed(1)+'%';}"
        "  console.log('[SLIDER] '+id+' = '+val+' (0-200)');"
        "}"
        "function syncNum(id,val){"
        "  var slider=document.getElementById(id);"
        "  var lbl=document.getElementById(id+'-val');"
        "  if(slider){slider.value=Math.round(val*2);}"
        "  if(lbl){lbl.textContent=(val*0.5).toFixed(1)+'%';}"
        "  console.log('[NUMBER] '+id+' = '+(val*0.5).toFixed(1)+'% (0-100)');"
        "}"
        "function applyFixture(){"
        "  var red=parseInt(document.getElementById('red').value);"
        "  var fr=parseInt(document.getElementById('fr').value);"
        "  var blue=parseInt(document.getElementById('blue').value);"
        "  var white=parseInt(document.getElementById('white').value);"
        "  var checkbox=document.getElementById('fix-pwr');"
        "  var is_on=(red>0||fr>0||blue>0||white>0);"
        "  console.log('[FIXTURE] Setting colors: R='+red+' FR='+fr+' B='+blue+' W='+white);"
        "  fetch('/api/fixture/set',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "    body:'red='+red+'&fr='+fr+'&blue='+blue+'&white='+white})"
        "  .then(r=>{"
        "    console.log('[FIXTURE] Response:', r.status);"
        "    document.getElementById('cur-red').textContent=(red*0.5).toFixed(1)+'%';"
        "    document.getElementById('cur-fr').textContent=(fr*0.5).toFixed(1)+'%';"
        "    document.getElementById('cur-blue').textContent=(blue*0.5).toFixed(1)+'%';"
        "    document.getElementById('cur-white').textContent=(white*0.5).toFixed(1)+'%';"
        "    if(checkbox){checkbox.checked=is_on;}"
        "  })"
        "  .catch(e=>console.log('[FIXTURE] Error:', e));"
        "}"
        "function saveFixture(){"
        "  var red=parseInt(document.getElementById('red').value);"
        "  var fr=parseInt(document.getElementById('fr').value);"
        "  var blue=parseInt(document.getElementById('blue').value);"
        "  var white=parseInt(document.getElementById('white').value);"
        "  console.log('[FIXTURE] Saving: R='+red+' FR='+fr+' B='+blue+' W='+white);"
        "  fetch('/save/fixture',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "    body:'fixture_en=1&red='+red+'&fr='+fr+'&blue='+blue+'&white='+white})"
        "  .then(r=>{console.log('[FIXTURE] Saved, response:',r.status);})"
        "  .catch(e=>{console.log('[FIXTURE] Save error:',e);});"
        "}"
        "function setFixture(preset){"
        "  var presets=[[0,0,0,0],[200,0,0,0],[0,200,0,0],[0,0,200,0],[0,0,0,200],[200,200,200,200],[140,100,100,60]];"
        "  var names=['OFF','RED','FAR_RED','BLUE','WHITE','FULL','GROW'];"
        "  var v=presets[preset];"
        "  console.log('[FIXTURE] Preset '+preset+' ('+names[preset]+'): R='+v[0]+' FR='+v[1]+' B='+v[2]+' W='+v[3]);"
        "  syncSlider('red',v[0]);"
        "  syncSlider('fr',v[1]);"
        "  syncSlider('blue',v[2]);"
        "  syncSlider('white',v[3]);"
        "  applyFixture();"
        "}"
        "function enableAllScenarios(){"
        "  for(var i=0;i<8;i++){"
        "    var cb=document.querySelector('input[name=\"sc_en_'+i+'\"]');"
        "    if(cb)cb.checked=true;"
        "  }"
        "}"
        "function disableAllScenarios(){"
        "  for(var i=0;i<8;i++){"
        "    var cb=document.querySelector('input[name=\"sc_en_'+i+'\"]');"
        "    if(cb)cb.checked=false;"
        "  }"
        "}"
        "function enableAllTimers(){"
        "  for(var i=0;i<8;i++){"
        "    var cb=document.querySelector('input[name=\"ften_'+i+'\"]');"
        "    if(cb)cb.checked=true;"
        "  }"
        "}"
        "function disableAllTimers(){"
        "  for(var i=0;i<8;i++){"
        "    var cb=document.querySelector('input[name=\"ften_'+i+'\"]');"
        "    if(cb)cb.checked=false;"
        "  }"
        "}"
        "function duplicateScenario(idx){"
        "  console.log('[add S] idx=',idx);"
        "  idx = parseInt(idx);"
        "  for(var i=0; i<8; i++) {"
        "    var row = document.getElementById('sc_row_'+i);"
        "    if(row && row.style.display === 'none') {"
        "      row.style.display = '';"
        "      saveVisibilityState();"
        "      console.log('[add S] showed row',i);"
        "      return;"
        "    }"
        "  }"
        "  alert('Достигнут максимум');"
        "}"
        "function removeScenario(idx){"
        "  console.log('[remove S] idx=',idx);"
        "  idx = parseInt(idx);"
        "  var count=0;"
        "  for(var i=0;i<8;i++){"
        "    var r=document.getElementById('sc_row_'+i);"
        "    if(r&&r.style.display!=='none')count++;"
        "  }"
        "  if(count<=1){return;}"
        "  var row = document.getElementById('sc_row_'+idx);"
        "  if(row) {"
        "    row.style.display = 'none';"
        "    saveVisibilityState();"
        "    console.log('[remove S] hidden row',idx);"
        "  }"
        "}"
        "function duplicateTimer(idx){"
        "  console.log('[add T] idx=',idx);"
        "  idx = parseInt(idx);"
        "  for(var i=0; i<8; i++) {"
        "    var row = document.getElementById('ft_row_'+i);"
        "    if(row && row.style.display === 'none') {"
        "      row.style.display = '';"
        "      saveVisibilityState();"
        "      console.log('[add T] showed row',i);"
        "      return;"
        "    }"
        "  }"
        "  alert('Достигнут максимум');"
        "}"
        "function removeTimer(idx){"
        "  console.log('[remove T] idx=',idx);"
        "  idx = parseInt(idx);"
        "  var count=0;"
        "  for(var i=0;i<8;i++){"
        "    var r=document.getElementById('ft_row_'+i);"
        "    if(r&&r.style.display!=='none')count++;"
        "  }"
        "  if(count<=1){return;}"
        "  var row = document.getElementById('ft_row_'+idx);"
        "  if(row) {"
        "    row.style.display = 'none';"
        "    saveVisibilityState();"
        "    console.log('[remove T] hidden row',idx);"
        "  }"
        "}"
        "function saveVisibilityState(){"
        "  var sc=[],tm=[];"
        "  for(var i=0;i<8;i++){"
        "    var sr=document.getElementById('sc_row_'+i);"
        "    if(sr&&sr.style.display!=='none')sc.push(i);"
        "    var tr=document.getElementById('ft_row_'+i);"
        "    if(tr&&tr.style.display!=='none')tm.push(i);"
        "  }"
        "  localStorage.setItem('sc_visible',JSON.stringify(sc));"
        "  localStorage.setItem('tm_visible',JSON.stringify(tm));"
        "  console.log('[STATE] Saved sc:',sc,'tm:',tm);"
        "}"
        "function restoreVisibilityState(){"
        "  var sc=JSON.parse(localStorage.getItem('sc_visible')||'[0,1,2,3,4,5,6,7]');"
        "  var tm=JSON.parse(localStorage.getItem('tm_visible')||'[0,1,2,3,4,5,6,7]');"
        "  if(!Array.isArray(sc)) sc=[0,1,2,3,4,5,6,7];"
        "  if(!Array.isArray(tm)) tm=[0,1,2,3,4,5,6,7];"
        "  console.log('[STATE] Restoring sc:',sc,'tm:',tm);"
        "  var tmCount = 0;"
        "  for(var i=0;i<8;i++){"
        "    var sr=document.getElementById('sc_row_'+i);"
        "    if(sr)sr.style.display=sc.includes(i)?'':'none';"
        "    var tr=document.getElementById('ft_row_'+i);"
        "    if(tr){"
        "      tr.style.display=tm.includes(i)?'':'none';"
        "      tmCount++;"
        "    }"
        "  }"
        "  console.log('[STATE] Timer rows found:',tmCount);"
        "}"
        "document.addEventListener('DOMContentLoaded', function(){"
        "  restoreVisibilityState();"
        "  document.querySelectorAll('.dup-sc-btn').forEach(btn => {"
        "    btn.addEventListener('click', function() { duplicateScenario(this.getAttribute('data-idx')); });"
        "  });"
        "  document.querySelectorAll('.del-sc-btn').forEach(btn => {"
        "    btn.addEventListener('click', function() { removeScenario(this.getAttribute('data-idx')); });"
        "  });"
        "  document.querySelectorAll('.dup-tm-btn').forEach(btn => {"
        "    btn.addEventListener('click', function() { duplicateTimer(this.getAttribute('data-idx')); });"
        "  });"
        "  document.querySelectorAll('.del-tm-btn').forEach(btn => {"
        "    btn.addEventListener('click', function() { removeTimer(this.getAttribute('data-idx')); });"
        "  });"
        "  console.log('[INIT] Button listeners attached');"
        "});"
        "</script>"));
    
    endPage();
}

// ================================================================
//               FORM HANDLER: SAVE BLE CONFIG
// ================================================================

void WebPortal::handleSaveBle() {
    _cfg->cfg.ble_enabled = _server.hasArg("ble_en");
    if (_server.hasArg("ble_name"))
        strlcpy(_cfg->cfg.ble_name, _server.arg("ble_name").c_str(), sizeof(_cfg->cfg.ble_name));
    _cfg->save();
    // Restart required to apply BLE changes
    _server.sendHeader("Location", "/bluetooth");
    _server.send(303, "text/plain", "Redirecting...");
    delay(300);
    ESP.restart();
}

// ================================================================
//               FORM HANDLER: SAVE MESH CONFIG
// ================================================================

void WebPortal::handleSaveMesh() {
    _cfg->cfg.mesh_enabled = _server.hasArg("mesh_en");

    if (_server.hasArg("mesh_ssid")) {
        String ssid = _server.arg("mesh_ssid");
        ssid.trim();
        if (ssid.length() == 0) ssid = "ESP-HUB-MESH";
        strlcpy(_cfg->cfg.mesh_ssid, ssid.c_str(), sizeof(_cfg->cfg.mesh_ssid));
    }

    if (_server.hasArg("mesh_pass")) {
        String pass = _server.arg("mesh_pass");
        pass.trim();
        if (pass.length() >= 8 && pass.length() <= 63) {
            strlcpy(_cfg->cfg.mesh_pass, pass.c_str(), sizeof(_cfg->cfg.mesh_pass));
        }
    }

    if (_server.hasArg("mesh_port")) {
        int port = _server.arg("mesh_port").toInt();
        if (port >= 1 && port <= 65535) {
            _cfg->cfg.mesh_port = (uint16_t)port;
        }
    }

    if (_server.hasArg("mesh_ch")) {
        int channel = _server.arg("mesh_ch").toInt();
        if (channel >= 1 && channel <= 13) {
            _cfg->cfg.mesh_channel = (uint8_t)channel;
        }
    }

    // Master role can only be changed when device is not connected to external Wi-Fi.
    if (!_wifi || !_wifi->isConnected()) {
        _cfg->cfg.mesh_master_node = _server.hasArg("mesh_master");
    }
    _cfg->save();
    // Restart required to apply Mesh changes
    _server.sendHeader("Location", "/mesh");
    _server.send(303, "text/plain", "Redirecting...");
    delay(300);
    ESP.restart();
}

// ================================================================
//               API HANDLER: MESH TOGGLE
// ================================================================

void WebPortal::handleApiMeshToggle() {
    // Quick toggle without full restart (optional, for future use)
    bool newState = !_cfg->cfg.mesh_enabled;
    _cfg->cfg.mesh_enabled = newState;
    _cfg->save();
    
    String json = "{\"mesh_enabled\":";
    json += (newState ? "true" : "false");
    json += ",\"message\":\"";
    json += (newState ? "Mesh enabled. Please reboot to apply." : "Mesh disabled. Please reboot to apply.");
    json += "\"}";
    _server.send(200, "application/json", json);
}

// ================================================================
//               FORM HANDLER: SAVE FIXTURE CONFIG
// ================================================================

void WebPortal::handleSaveFixture() {
    _cfg->cfg.fixture.enabled = _server.hasArg("fixture_en");
    if (_server.hasArg("fixture_tx"))
        _cfg->cfg.fixture.uart_tx_pin = _server.arg("fixture_tx").toInt();
    if (_server.hasArg("fixture_rx"))
        _cfg->cfg.fixture.uart_rx_pin = _server.arg("fixture_rx").toInt();
    if (_server.hasArg("fixture_baud"))
        _cfg->cfg.fixture.uart_baud = _server.arg("fixture_baud").toInt();
    if (_server.hasArg("red"))
        _cfg->cfg.fixture.red_brightness = _server.arg("red").toInt();
    if (_server.hasArg("fr"))
        _cfg->cfg.fixture.far_red_brightness = _server.arg("fr").toInt();
    if (_server.hasArg("blue"))
        _cfg->cfg.fixture.blue_brightness = _server.arg("blue").toInt();
    if (_server.hasArg("white"))
        _cfg->cfg.fixture.white_brightness = _server.arg("white").toInt();
    _cfg->save();
    // Restart required to apply UART changes
    _server.sendHeader("Location", "/fixtures");
    _server.send(303, "text/plain", "Redirecting...");
    delay(300);
    ESP.restart();
}

// ================================================================
//               FORM HANDLER: SAVE FIXTURE SCENARIOS
// ================================================================

void WebPortal::handleSaveScenarios() {
    for (int i = 0; i < MAX_FIXTURE_SCENARIOS; i++) {
        String p_en = "sc_en_" + String(i);
        String p_h  = "sc_h_" + String(i);
        String p_m  = "sc_m_" + String(i);
        String p_s  = "sc_s_" + String(i);
        String p_r  = "sc_r_" + String(i);
        String p_fr = "sc_fr_" + String(i);
        String p_b  = "sc_b_" + String(i);
        String p_w  = "sc_w_" + String(i);

        _cfg->cfg.fixture.scenarios[i].enabled = _server.hasArg(p_en);
        if (_server.hasArg(p_h))  _cfg->cfg.fixture.scenarios[i].start_hour   = _server.arg(p_h).toInt();
        if (_server.hasArg(p_m))  _cfg->cfg.fixture.scenarios[i].start_minute = _server.arg(p_m).toInt();
        if (_server.hasArg(p_s))  _cfg->cfg.fixture.scenarios[i].start_second = _server.arg(p_s).toInt();
        if (_server.hasArg(p_r))  _cfg->cfg.fixture.scenarios[i].red          = _server.arg(p_r).toInt() * 2;
        if (_server.hasArg(p_fr)) _cfg->cfg.fixture.scenarios[i].far_red      = _server.arg(p_fr).toInt() * 2;
        if (_server.hasArg(p_b))  _cfg->cfg.fixture.scenarios[i].blue         = _server.arg(p_b).toInt() * 2;
        if (_server.hasArg(p_w))  _cfg->cfg.fixture.scenarios[i].white        = _server.arg(p_w).toInt() * 2;
    }
    _cfg->save();
    _server.sendHeader("Location", "/fixtures");
    _server.send(303, "text/plain", "Redirecting...");
}

// ================================================================
//               FORM HANDLER: SAVE FIXTURE TIMERS
// ================================================================

void WebPortal::handleSaveFixtureTimers() {
    for (int i = 0; i < MAX_FIXTURE_TIMERS; i++) {
        String si = String(i);
        FixtureTimerConfig& t = _cfg->cfg.fixture.timers[i];
        t.enabled = _server.hasArg("ften_" + si);
        if (_server.hasArg("ftact_" + si))
            t.action = (FixtureTimerAction)constrain(_server.arg("ftact_" + si).toInt(), 0, FIX_TIMER_ACTION_COUNT-1);
        if (_server.hasArg("fth_" + si))
            t.hours   = (uint8_t)constrain(_server.arg("fth_" + si).toInt(), 0, 23);
        if (_server.hasArg("ftm_" + si))
            t.minutes = (uint8_t)constrain(_server.arg("ftm_" + si).toInt(), 0, 59);
        if (_server.hasArg("fts_" + si))
            t.seconds = (uint8_t)constrain(_server.arg("fts_" + si).toInt(), 0, 59);
        if (_server.hasArg("ftdur_" + si))
            t.duration_ms = (uint16_t)constrain(_server.arg("ftdur_" + si).toInt(), 1, 60000);
        // Runtime duration (auto-shutoff)
        if (_server.hasArg("ftrun_h_" + si))
            t.run_hours = (uint8_t)constrain(_server.arg("ftrun_h_" + si).toInt(), 0, 23);
        if (_server.hasArg("ftrun_m_" + si))
            t.run_minutes = (uint8_t)constrain(_server.arg("ftrun_m_" + si).toInt(), 0, 59);
        if (_server.hasArg("ftrun_s_" + si))
            t.run_seconds = (uint8_t)constrain(_server.arg("ftrun_s_" + si).toInt(), 0, 59);
        if (_server.hasArg("ftlbl_" + si)) {
            String lbl = _server.arg("ftlbl_" + si);
            strlcpy(t.label, lbl.c_str(), sizeof(t.label));
        }
        // Custom channels
        if (_server.hasArg("ftr_" + si))
            t.red     = (uint8_t)constrain((int)(_server.arg("ftr_"  + si).toFloat() * 2), 0, 200);
        if (_server.hasArg("ftfr_" + si))
            t.far_red = (uint8_t)constrain((int)(_server.arg("ftfr_" + si).toFloat() * 2), 0, 200);
        if (_server.hasArg("ftb_" + si))
            t.blue    = (uint8_t)constrain((int)(_server.arg("ftb_"  + si).toFloat() * 2), 0, 200);
        if (_server.hasArg("ftw_" + si))
            t.white   = (uint8_t)constrain((int)(_server.arg("ftw_"  + si).toFloat() * 2), 0, 200);
    }
    _cfg->save();
    if (_fixture) _fixture->reloadTimers();
    _server.sendHeader("Location", "/fixtures");
    _server.send(303, "text/plain", "Redirecting...");
}

// ================================================================

void WebPortal::handleApiFixtureStatus() {
    String j;
    j.reserve(256);
    j += F("{\"enabled\":");
    j += _fixture->isEnabled() ? F("true") : F("false");
    j += F(",\"red\":");
    j += _fixture->getRed();
    j += F(",\"fr\":");
    j += _fixture->getFarRed();
    j += F(",\"blue\":");
    j += _fixture->getBlue();
    j += F(",\"white\":");
    j += _fixture->getWhite();
    j += F(",\"ack\":");
    j += _fixture->isLastAckOk() ? F("true") : F("false");
    j += F("}");
    _server.sendHeader("Connection", "close");
    _server.send(200, "application/json", j);
}

// ================================================================
//               API: POST /api/fixture/set — set brightness
// ================================================================

void WebPortal::handleApiFixtureSet() {
    if (!_fixture->isEnabled()) {
        _server.send(503, "application/json", F("{\"error\":\"fixture disabled\"}"));
        return;
    }
    uint8_t red = _server.hasArg("red") ? _server.arg("red").toInt() : 0;
    uint8_t fr = _server.hasArg("fr") ? _server.arg("fr").toInt() : 0;
    uint8_t blue = _server.hasArg("blue") ? _server.arg("blue").toInt() : 0;
    uint8_t white = _server.hasArg("white") ? _server.arg("white").toInt() : 0;
    
    bool ok = _fixture->setChannels(red, fr, blue, white);
    
    if (ok) {
        _server.send(200, "application/json", F("{\"ok\":true}"));
    } else {
        _server.send(500, "application/json", F("{\"error\":\"ACK failed\"}"));
    }
}

// ================================================================
//               API: POST /api/fixture/on — turn on
// ================================================================

void WebPortal::handleApiFixtureOn() {
    if (!_fixture->isEnabled()) {
        _server.send(503, "application/json", F("{\"error\":\"fixture disabled\"}"));
        return;
    }
    
    // Default parameter: 0 if not provided
    uint8_t red = _server.hasArg("red") ? _server.arg("red").toInt() : 0;
    uint8_t fr = _server.hasArg("fr") ? _server.arg("fr").toInt() : 0;
    uint8_t blue = _server.hasArg("blue") ? _server.arg("blue").toInt() : 0;
    uint8_t white = _server.hasArg("white") ? _server.arg("white").toInt() : 0;
    
    bool ok = _fixture->setChannels(red, fr, blue, white);
    
    if (ok) {
        _server.send(200, "application/json", F("{\"ok\":true,\"status\":\"on\"}"));
    } else {
        _server.send(500, "application/json", F("{\"error\":\"ACK failed\"}"));
    }
}

// ================================================================
//               API: POST /api/fixture/off — turn off
// ================================================================

void WebPortal::handleApiFixtureOff() {
    if (!_fixture->isEnabled()) {
        _server.send(503, "application/json", F("{\"error\":\"fixture disabled\"}"));
        return;
    }
    
    bool ok = _fixture->setChannels(0, 0, 0, 0);
    
    if (ok) {
        _server.send(200, "application/json", F("{\"ok\":true,\"status\":\"off\"}"));
    } else {
        _server.send(500, "application/json", F("{\"error\":\"ACK failed\"}"));
    }
}

// ================================================================
//               API: POST /api/fixture/color — set color with power
// ================================================================

void WebPortal::handleApiFixtureColor() {
    if (!_fixture->isEnabled()) {
        _server.send(503, "application/json", F("{\"error\":\"fixture disabled\"}"));
        return;
    }
    
    // Parse named color presets or accept raw values
    String preset = _server.hasArg("preset") ? _server.arg("preset") : "";
    uint8_t red = 0, fr = 0, blue = 0, white = 0;
    
    if (preset == "off") {
        red = fr = blue = white = 0;
    } else if (preset == "red") {
        red = 200; fr = 0; blue = 0; white = 0;
    } else if (preset == "far_red") {
        red = 0; fr = 200; blue = 0; white = 0;
    } else if (preset == "blue") {
        red = 0; fr = 0; blue = 200; white = 0;
    } else if (preset == "white") {
        red = 0; fr = 0; blue = 0; white = 200;
    } else if (preset == "full") {
        red = 200; fr = 200; blue = 200; white = 200;
    } else if (preset == "grow") {
        red = 140; fr = 100; blue = 100; white = 60;
    } else {
        // Accept individual color values
        red = _server.hasArg("red") ? _server.arg("red").toInt() : 0;
        fr = _server.hasArg("fr") ? _server.arg("fr").toInt() : 0;
        blue = _server.hasArg("blue") ? _server.arg("blue").toInt() : 0;
        white = _server.hasArg("white") ? _server.arg("white").toInt() : 0;
    }
    
    if (red > 200) red = 200;
    if (fr > 200) fr = 200;
    if (blue > 200) blue = 200;
    if (white > 200) white = 200;
    
    bool ok = _fixture->setChannels(red, fr, blue, white);
    
    if (ok) {
        String resp = F("{\"ok\":true,\"red\":");
        resp += String(red);
        resp += F(",\"fr\":");
        resp += String(fr);
        resp += F(",\"blue\":");
        resp += String(blue);
        resp += F(",\"white\":");
        resp += String(white);
        resp += F("}");
        _server.send(200, "application/json", resp);
    } else {
        _server.send(500, "application/json", F("{\"error\":\"ACK failed\"}"));
    }
}

// ================================================================
//               API: GET /api/fixture/timers — list timers as JSON
// ================================================================

void WebPortal::handleApiFixtureTimers() {
    String j;
    j.reserve(1024);
    j += F("{\"timers\":[");
    
    for (int i = 0; i < MAX_FIXTURE_TIMERS; i++) {
        FixtureTimerConfig& t = _cfg->cfg.fixture.timers[i];
        if (i > 0) j += F(",");
        j += F("{\"id\":");
        j += String(i);
        j += F(",\"enabled\":");
        j += t.enabled ? F("true") : F("false");
        j += F(",\"label\":\"");
        j += String(t.label);
        j += F("\",\"action\":");
        j += String(t.action);
        j += F(",\"hours\":");
        j += String(t.hours);
        j += F(",\"minutes\":");
        j += String(t.minutes);
        j += F(",\"seconds\":");
        j += String(t.seconds);
        j += F(",\"duration_ms\":");
        j += String(t.duration_ms);
        j += F(",\"red\":"); j += String(t.red);
        j += F(",\"far_red\":"); j += String(t.far_red);
        j += F(",\"blue\":"); j += String(t.blue);
        j += F(",\"white\":"); j += String(t.white);
        j += F("}");
    }
    
    j += F("]}");
    _server.send(200, "application/json", j);
}

// ================================================================
//               API: POST /api/fixture/timers/set — set single timer
// ================================================================

void WebPortal::handleApiFixtureTimersSet() {
    int id = _server.hasArg("id") ? _server.arg("id").toInt() : -1;
    if (id < 0 || id >= MAX_FIXTURE_TIMERS) {
        _server.send(400, "application/json", F("{\"error\":\"invalid timer id\"}"));
        return;
    }
    
    FixtureTimerConfig& t = _cfg->cfg.fixture.timers[id];
    
    if (_server.hasArg("enabled"))
        t.enabled = (_server.arg("enabled") == "true" || _server.arg("enabled") == "1");
    if (_server.hasArg("label")) {
        String lbl = _server.arg("label");
        strlcpy(t.label, lbl.c_str(), sizeof(t.label));
    }
    if (_server.hasArg("action"))
        t.action = (FixtureTimerAction)constrain(_server.arg("action").toInt(), 0, FIX_TIMER_ACTION_COUNT-1);
    if (_server.hasArg("hours"))
        t.hours = (uint8_t)constrain(_server.arg("hours").toInt(), 0, 23);
    if (_server.hasArg("minutes"))
        t.minutes = (uint8_t)constrain(_server.arg("minutes").toInt(), 0, 59);
    if (_server.hasArg("seconds"))
        t.seconds = (uint8_t)constrain(_server.arg("seconds").toInt(), 0, 59);
    if (_server.hasArg("duration_ms"))
        t.duration_ms = (uint16_t)constrain(_server.arg("duration_ms").toInt(), 1, 60000);
    
    _cfg->save();
    if (_fixture) _fixture->reloadTimers();
    
    String resp = F("{\"ok\":true,\"id\":");
    resp += String(id);
    resp += F("}");
    _server.send(200, "application/json", resp);
}

// ================================================================
//               API: GET /api/fixture/scenarios — list scenarios as JSON
// ================================================================

void WebPortal::handleApiFixtureScenarios() {
    String j;
    j.reserve(1024);
    j += F("{\"scenarios\":[");
    
    for (int i = 0; i < MAX_FIXTURE_SCENARIOS; i++) {
        FixtureScenario& sc = _cfg->cfg.fixture.scenarios[i];
        if (i > 0) j += F(",");
        j += F("{\"id\":");
        j += String(i);
        j += F(",\"enabled\":");
        j += sc.enabled ? F("true") : F("false");
        j += F(",\"start_hour\":");
        j += String(sc.start_hour);
        j += F(",\"start_minute\":");
        j += String(sc.start_minute);
        j += F(",\"start_second\":");
        j += String(sc.start_second);
        j += F(",\"red\":");
        j += String(sc.red);
        j += F(",\"far_red\":");
        j += String(sc.far_red);
        j += F(",\"blue\":");
        j += String(sc.blue);
        j += F(",\"white\":");
        j += String(sc.white);
        j += F("}");
    }
    
    j += F("]}");
    _server.send(200, "application/json", j);
}

// ================================================================
//               API: POST /api/fixture/scenarios/set — set single scenario
// ================================================================

void WebPortal::handleApiFixtureScenariosSet() {
    int id = _server.hasArg("id") ? _server.arg("id").toInt() : -1;
    if (id < 0 || id >= MAX_FIXTURE_SCENARIOS) {
        _server.send(400, "application/json", F("{\"error\":\"invalid scenario id\"}"));
        return;
    }
    
    FixtureScenario& sc = _cfg->cfg.fixture.scenarios[id];
    
    if (_server.hasArg("enabled"))
        sc.enabled = (_server.arg("enabled") == "true" || _server.arg("enabled") == "1");
    if (_server.hasArg("start_hour"))
        sc.start_hour = (uint8_t)constrain(_server.arg("start_hour").toInt(), 0, 23);
    if (_server.hasArg("start_minute"))
        sc.start_minute = (uint8_t)constrain(_server.arg("start_minute").toInt(), 0, 59);
    if (_server.hasArg("start_second"))
        sc.start_second = (uint8_t)constrain(_server.arg("start_second").toInt(), 0, 59);
    if (_server.hasArg("red"))
        sc.red = (uint8_t)constrain(_server.arg("red").toInt(), 0, 200);
    if (_server.hasArg("far_red"))
        sc.far_red = (uint8_t)constrain(_server.arg("far_red").toInt(), 0, 200);
    if (_server.hasArg("blue"))
        sc.blue = (uint8_t)constrain(_server.arg("blue").toInt(), 0, 200);
    if (_server.hasArg("white"))
        sc.white = (uint8_t)constrain(_server.arg("white").toInt(), 0, 200);
    
    _cfg->save();
    
    String resp = F("{\"ok\":true,\"id\":");
    resp += String(id);
    resp += F("}");
    _server.send(200, "application/json", resp);
}

// ================================================================
//    API: POST /api/fixture/toggle — toggle on/off
//    If any channel is on → all off.
//    If all off → restore saved config brightness (white=20 fallback).
// ================================================================

void WebPortal::handleApiFixtureToggle() {
    if (!_fixture->isEnabled()) {
        _server.send(503, "application/json", F("{\"error\":\"fixture disabled\"}"));
        return;
    }
    uint8_t r  = _fixture->getRed();
    uint8_t fr = _fixture->getFarRed();
    uint8_t bl = _fixture->getBlue();
    uint8_t w  = _fixture->getWhite();
    bool wasOn = (r | fr | bl | w) != 0;
    bool ok;
    const char* status;
    if (wasOn) {
        ok = _fixture->setChannels(0, 0, 0, 0);
        status = "off";
    } else {
        r  = _cfg->cfg.fixture.red_brightness;
        fr = _cfg->cfg.fixture.far_red_brightness;
        bl = _cfg->cfg.fixture.blue_brightness;
        w  = _cfg->cfg.fixture.white_brightness;
        if (!(r | fr | bl | w)) w = 20;
        ok = _fixture->setChannels(r, fr, bl, w);
        status = "on";
    }
    if (!ok) { _server.send(500, "application/json", F("{\"error\":\"ACK failed\"}")); return; }
    String resp = F("{\"ok\":true,\"status\":\"");
    resp += status;
    resp += F("\",\"red\":"); resp += _fixture->getRed();
    resp += F(",\"fr\":"); resp += _fixture->getFarRed();
    resp += F(",\"blue\":"); resp += _fixture->getBlue();
    resp += F(",\"white\":"); resp += _fixture->getWhite();
    resp += F("}");
    _server.send(200, "application/json", resp);
}

// ================================================================
//    API: POST /api/fixture/dim — adjust active channels by step
//    step=N (int, ±, 0-200 scale).  Only affects channels > 0.
// ================================================================

void WebPortal::handleApiFixtureDim() {
    if (!_fixture->isEnabled()) {
        _server.send(503, "application/json", F("{\"error\":\"fixture disabled\"}"));
        return;
    }
    if (!_server.hasArg("step")) {
        _server.send(400, "application/json", F("{\"error\":\"missing step\"}"));
        return;
    }
    int step = _server.arg("step").toInt();
    int r  = _fixture->getRed();
    int fr = _fixture->getFarRed();
    int bl = _fixture->getBlue();
    int w  = _fixture->getWhite();
    if (r  > 0) r  = constrain(r  + step, 0, 200);
    if (fr > 0) fr = constrain(fr + step, 0, 200);
    if (bl > 0) bl = constrain(bl + step, 0, 200);
    if (w  > 0) w  = constrain(w  + step, 0, 200);
    bool ok = _fixture->setChannels((uint8_t)r, (uint8_t)fr, (uint8_t)bl, (uint8_t)w);
    if (!ok) { _server.send(500, "application/json", F("{\"error\":\"ACK failed\"}")); return; }
    String resp = F("{\"ok\":true,\"red\":"); resp += _fixture->getRed();
    resp += F(",\"fr\":"); resp += _fixture->getFarRed();
    resp += F(",\"blue\":"); resp += _fixture->getBlue();
    resp += F(",\"white\":"); resp += _fixture->getWhite();
    resp += F("}");
    _server.send(200, "application/json", resp);
}

// ================================================================
//    API: POST /api/fixture/timer/enable — enable/disable timer
//    id=N (0-7), enabled=true/false/1/0
// ================================================================

void WebPortal::handleApiFixtureTimerEnable() {
    int id = _server.hasArg("id") ? _server.arg("id").toInt() : -1;
    if (id < 0 || id >= MAX_FIXTURE_TIMERS) {
        _server.send(400, "application/json", F("{\"error\":\"invalid id\"}"));
        return;
    }
    if (!_server.hasArg("enabled")) {
        _server.send(400, "application/json", F("{\"error\":\"missing enabled\"}"));
        return;
    }
    bool en = (_server.arg("enabled") == "true" || _server.arg("enabled") == "1");
    _cfg->cfg.fixture.timers[id].enabled = en;
    _cfg->save();
    _fixture->reloadTimers();
    String resp = F("{\"ok\":true,\"id\":");
    resp += id;
    resp += F(",\"enabled\":"); resp += en ? F("true") : F("false");
    resp += F("}");
    _server.send(200, "application/json", resp);
}

// ================================================================
//    API: POST /api/fixture/scenario/enable — enable/disable scenario
//    id=N (0-7), enabled=true/false/1/0
// ================================================================

void WebPortal::handleApiFixtureScenarioEnable() {
    int id = _server.hasArg("id") ? _server.arg("id").toInt() : -1;
    if (id < 0 || id >= MAX_FIXTURE_SCENARIOS) {
        _server.send(400, "application/json", F("{\"error\":\"invalid id\"}"));
        return;
    }
    if (!_server.hasArg("enabled")) {
        _server.send(400, "application/json", F("{\"error\":\"missing enabled\"}"));
        return;
    }
    bool en = (_server.arg("enabled") == "true" || _server.arg("enabled") == "1");
    _cfg->cfg.fixture.scenarios[id].enabled = en;
    _cfg->save();
    String resp = F("{\"ok\":true,\"id\":");
    resp += id;
    resp += F(",\"enabled\":"); resp += en ? F("true") : F("false");
    resp += F("}");
    _server.send(200, "application/json", resp);
}

// ================================================================
//    API: POST /api/fixture/timer/trigger — manually fire a timer
//    id=N (0-7).  Immediately applies the timer's channel values.
// ================================================================

void WebPortal::handleApiFixtureTimerTrigger() {
    if (!_fixture->isEnabled()) {
        _server.send(503, "application/json", F("{\"error\":\"fixture disabled\"}"));
        return;
    }
    int id = _server.hasArg("id") ? _server.arg("id").toInt() : -1;
    if (id < 0 || id >= MAX_FIXTURE_TIMERS) {
        _server.send(400, "application/json", F("{\"error\":\"invalid id\"}"));
        return;
    }
    FixtureTimerConfig& t = _cfg->cfg.fixture.timers[id];
    bool ok = _fixture->setChannels(t.red, t.far_red, t.blue, t.white);
    if (!ok) { _server.send(500, "application/json", F("{\"error\":\"ACK failed\"}")); return; }
    String resp = F("{\"ok\":true,\"id\":"); resp += id;
    resp += F(",\"red\":"); resp += t.red;
    resp += F(",\"fr\":"); resp += t.far_red;
    resp += F(",\"blue\":"); resp += t.blue;
    resp += F(",\"white\":"); resp += t.white;
    resp += F("}");
    _server.send(200, "application/json", resp);
}

// ================================================================
//    API: GET /api/wifi — WiFi runtime status
// ================================================================

void WebPortal::handleApiWifiStatus() {
    String j;
    j.reserve(256);
    j += F("{\"connected\":");
    j += _wifi->isConnected() ? F("true") : F("false");
    j += F(",\"ap\":");
    j += _wifi->isAP() ? F("true") : F("false");
    j += F(",\"enabled\":");
    j += _wifi->isWifiEnabled() ? F("true") : F("false");
    j += F(",\"ip\":\"");
    j += _wifi->localIP();
    j += F("\",\"mac\":\"");
    j += _wifi->macAddress();
    j += F("\",\"rssi\":");
    j += _wifi->rssi();
    j += F(",\"ssid\":\"");
    String ssid = String(_cfg->cfg.wifi_ssid);
    ssid.replace("\\", "\\\\"); ssid.replace("\"", "\\\"");
    j += ssid;
    j += F("\",\"ap_clients\":");
    j += _wifi->apClientsCount();
    j += F(",\"ap_macs\":\"");
    j += _wifi->apClientsMACs();
    j += F("\"}");
    _server.send(200, "application/json", j);
}

// ================================================================
//    API: GET /api/mqtt — MQTT connection status
// ================================================================

void WebPortal::handleApiMqttStatus() {
    String j;
    j.reserve(256);
    j += F("{\"connected\":");
    j += _mqtt->isConnected() ? F("true") : F("false");
    j += F(",\"host\":\"");
    String host = String(_cfg->cfg.mqtt_host);
    host.replace("\\", "\\\\"); host.replace("\"", "\\\"");
    j += host;
    j += F("\",\"port\":");
    j += _cfg->cfg.mqtt_port;
    j += F(",\"topic\":\"");
    String topic = String(_cfg->cfg.mqtt_topic);
    topic.replace("\\", "\\\\"); topic.replace("\"", "\\\"");
    j += topic;
    j += F("\",\"interval_s\":");
    j += (_mqtt->interval() / 1000);
    j += F("}");
    _server.send(200, "application/json", j);
}

// ================================================================
//    API: GET /api/system — runtime system info
// ================================================================

void WebPortal::handleApiSystemStatus() {
    String j;
    j.reserve(384);
    j += F("{\"heap\":");
    j += ESP.getFreeHeap();
    j += F(",\"heap_min\":");
    j += ESP.getMinFreeHeap();
    j += F(",\"uptime_s\":");
    j += (millis() / 1000UL);
    j += F(",\"cpu_freq_mhz\":");
    j += ESP.getCpuFreqMHz();
    j += F(",\"flash_size\":");
    j += ESP.getFlashChipSize();
    j += F(",\"sketch_size\":");
    j += ESP.getSketchSize();
    j += F(",\"device\":\"");
    String dev = String(_cfg->cfg.device_name);
    dev.replace("\\", "\\\\"); dev.replace("\"", "\\\"");
    j += dev;
    j += F("\",\"mesh_enabled\":");
    j += _cfg->cfg.mesh_enabled ? F("true") : F("false");
    j += F(",\"mesh_connected\":");
    j += (_mesh && _cfg->cfg.mesh_enabled && _mesh->isConnected()) ? F("true") : F("false");
    j += F(",\"mesh_nodes\":");
    j += (_mesh && _cfg->cfg.mesh_enabled) ? _mesh->getConnectedCount() : 0;
    j += F("}");
    _server.send(200, "application/json", j);
}

// ================================================================
//    API: POST /api/ble/clear-log — clear BLE RX log
// ================================================================

void WebPortal::handleApiBlearLog() {
    _ble->clearLog();
    _server.send(200, "application/json", F("{\"ok\":true}"));
}

// ================================================================
//    API: POST /api/fixture/enable — enable/disable fixture manager
//    enabled=true/false/1/0
// ================================================================

void WebPortal::handleApiFixtureEnable() {
    if (!_server.hasArg("enabled")) {
        _server.send(400, "application/json", F("{\"error\":\"missing enabled\"}"));
        return;
    }
    bool en = (_server.arg("enabled") == "true" || _server.arg("enabled") == "1");
    _fixture->enable(en);
    _cfg->cfg.fixture.enabled = en;
    _cfg->save();
    String resp = F("{\"ok\":true,\"enabled\":");
    resp += en ? F("true") : F("false");
    resp += F("}");
    _server.send(200, "application/json", resp);
}

// ================================================================
//    API: POST /api/fixture/demo — run fixture demo sequence
// ================================================================

void WebPortal::handleApiFixtureDemo() {
    if (!_fixture->isEnabled()) {
        _server.send(503, "application/json", F("{\"error\":\"fixture disabled\"}"));
        return;
    }
    _fixture->runDemo();
    _server.send(200, "application/json", F("{\"ok\":true}"));
}

// ================================================================
//               API: GET /api/ble  — status + RX log JSON
// ================================================================

void WebPortal::handleApiBleStatus() {
    String j;
    j.reserve(512);
    j += F("{\"enabled\":");
    j += _ble->isEnabled() ? F("true") : F("false");
    j += F(",\"connected\":");
    j += _ble->isConnected() ? F("true") : F("false");
    j += F(",\"log\":[");
    for (int i = 0; i < _ble->logCount(); i++) {
        if (i) j += ',';
        j += '"';
        String line = _ble->logLine(i);
        line.replace("\\", "\\\\");
        line.replace("\"", "\\\"");
        j += line;
        j += '"';
    }
    j += F("]}");
    _server.send(200, "application/json", j);
}

// ================================================================
//               API: POST /api/ble/send  — push message to client
// ================================================================

void WebPortal::handleApiBleSend() {
    if (!_ble->isEnabled() || !_ble->isConnected()) {
        _server.send(503, "application/json", F("{\"error\":\"not connected\"}"));
        return;
    }
    String msg = _server.hasArg("msg") ? _server.arg("msg") : _server.arg("plain");
    msg.trim();
    if (msg.length() == 0) {
        _server.send(400, "application/json", F("{\"error\":\"empty message\"}"));
        return;
    }
    _ble->notify(msg);
    _server.send(200, "application/json", F("{\"ok\":true}"));
}
// ================================================================

void WebPortal::handleApiDocs() {
    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.client().setNoDelay(true);
    _server.sendHeader("Connection", "close");
    _server.send(200, "text/html", "");
    sendPageHeader("API");
    _server.sendContent(navBar("/api"));
    _server.sendContent(F("<div class='wrap'>"));

    // ----- CSS -----
    _server.sendContent(F("<style>"
        ".ep{background:var(--bg3);border:1px solid var(--brd);border-radius:8px;margin:10px 0}"
        ".ep-hdr{display:flex;align-items:center;gap:10px;padding:10px 14px;"
        "cursor:pointer;user-select:none}"
        ".ep-hdr:hover{background:var(--bgo)}"
        ".method{font-weight:700;font-size:12px;padding:2px 8px;border-radius:4px;"
        "min-width:50px;text-align:center;flex-shrink:0}"
        ".m-get{background:#0c2d6b;color:#58a6ff}"
        ".m-post{background:#1b3a1f;color:#3fb950}"
        ".ep-url{font-family:monospace;font-size:14px;flex:1}"
        ".ep-desc{color:var(--txt2);font-size:13px}"
        ".ep-body{padding:12px 14px;border-top:1px solid var(--brd2);display:none}"
        ".ep-body.open{display:block}"
        ".ep-resp{background:var(--bg);border:1px solid var(--brd2);border-radius:6px;"
        "padding:10px;font-family:monospace;font-size:12px;white-space:pre-wrap;"
        "max-height:220px;overflow-y:auto;margin-top:8px;color:var(--txt)}"
        ".ep-tbl{width:100%;border-collapse:collapse;margin:8px 0}"
        ".ep-tbl th{font-size:11px;text-transform:uppercase;color:var(--txt2);padding:4px 8px}"
        ".ep-tbl td{font-size:13px;padding:4px 8px}"
        ".ep-tbl td:first-child{font-family:monospace;white-space:nowrap}"
        ".try-panel{background:var(--bg2);border:1px solid var(--brd2);border-radius:6px;"
        "padding:12px;margin-top:10px}"
        ".try-panel input{margin-bottom:6px}"
        ".scode{display:inline-block;padding:1px 7px;border-radius:4px;font-size:12px;"
        "font-weight:700;margin-left:6px}"
        ".sc2{background:#1b3a1f;color:#3fb950}"
        ".sc3{background:#3d2e00;color:#d29922}"
        ".sc4,.sc5{background:#3d1114;color:#f85149}"
        "code{background:var(--bg3);border:1px solid var(--brd2);border-radius:3px;"
        "padding:1px 5px;font-size:12px}"
        ".chevron{margin-left:auto;font-size:12px;transition:transform .2s}"
        "</style>"));

    // ----- JS -----
    _server.sendContent(F("<script>"
        "function epToggle(id){"
        "  var b=document.getElementById('eb_'+id);"
        "  b.classList.toggle('open');"
        "  var ch=document.getElementById('ch_'+id);"
        "  if(ch)ch.textContent=b.classList.contains('open')?'\u25B2':'\u25BC';"
        "}"
        "function swSend(method,url,formId,respId){"
        "  var el=document.getElementById(respId);"
        "  var sc=document.getElementById(respId+'_sc');"
        "  el.textContent='...';"
        "  if(sc)sc.className='scode';"
        "  var opts={method:method};"
        "  if(formId){"
        "    var fd=new FormData(document.getElementById(formId));"
        "    opts.body=new URLSearchParams(fd).toString();"
        "    opts.headers={'Content-Type':'application/x-www-form-urlencoded'};"
        "  }"
        "  fetch(url,opts)"
        "    .then(function(r){"
        "      var code=r.status;"
        "      if(sc){sc.textContent='HTTP '+code;sc.className='scode sc'+(Math.floor(code/100));}"
        "      return r.text();"
        "    })"
        "    .then(function(d){"
        "      try{d=JSON.stringify(JSON.parse(d),null,2);}catch(e){}"
        "      el.textContent=d;"
        "    })"
        "    .catch(function(e){el.textContent='Error: '+e;});"
        "}"
        "</script>"));

    String host = _wifi->isConnected()
        ? String("http://") + _wifi->localIP()
        : String("http://esp-hub.local");

    // --- helper lambdas emulated as inline strings ---
    // Base URL info card
    String base;
    base += F("<table><tr><td style='width:160px'>\u0410\u0434\u0440\u0435\u0441 \u0443\u0441\u0442\u0440\u043E\u0439\u0441\u0442\u0432\u0430</td><td><code>");
    base += host;
    base += F("</code></td></tr>"
              "<tr><td>mDNS</td><td><code>http://esp-hub.local/</code></td></tr>"
              "<tr><td>\u0424\u043E\u0440\u043C\u0430\u0442 POST</td><td><code>application/x-www-form-urlencoded</code></td></tr>"
              "</table>");
    _server.sendContent(card("\u0411\u0430\u0437\u043E\u0432\u044B\u0439 URL", base));

    // ----------------------------------------------------------------
    // Macro: build one collapsible endpoint block with inline try-panel
    // ----------------------------------------------------------------
    auto ep = [&](const char* id, const char* method, const char* cls,
                  const char* url, const char* summary,
                  const String& paramsTable,   // html <tr>...</tr> rows
                  const String& tryFields,      // html inputs inside try-panel form
                  const char*   respInit,
                  bool isGet) -> String
    {
        String e;
        // header (clickable)
        e += F("<div class='ep'>"
               "<div class='ep-hdr' onclick='epToggle(\"");
        e += id;
        e += F("\")'>"
               "<span class='method ");
        e += cls;
        e += F("'>");
        e += method;
        e += F("</span>"
               "<span class='ep-url'>");
        e += url;
        e += F("</span>"
               "<span class='ep-desc'>&nbsp;&mdash;&nbsp;");
        e += summary;
        e += F("</span>"
               "<span class='chevron' id='ch_");
        e += id;
        e += F("'></span>"
               "</div>");
        // body
        e += F("<div class='ep-body' id='eb_");
        e += id;
        e += F("'>");
        // parameters table
        if (paramsTable.length()) {
            e += F("<table class='ep-tbl'>"
                   "<tr><th>\u041D\u0430\u0437\u0432\u0430\u043D\u0438\u0435</th><th>\u0422\u0438\u043F</th><th>\u041E\u043F\u0438\u0441\u0430\u043D\u0438\u0435</th></tr>");
            e += paramsTable;
            e += F("</table>");
        }
        // try panel
        e += F("<div class='try-panel'>"
               "<b style='font-size:13px'>\u0422\u0435\u0441\u0442\u0438\u0440\u043E\u0432\u0430\u0442\u044C</b>");
        String fid = String("f") + id;
        String rid = String("r") + id;
        if (isGet) {
            // GET: just a send button, no form
            e += F("<div style='margin-top:8px'>"
                   "<button class='btn btn-secondary' onclick=\"");
            e += "swSend('GET','";
            e += url;
            e += "',null,'";
            e += rid;
            e += F("')\"> \u041E\u0442\u043F\u0440\u0430\u0432\u0438\u0442\u044C</button>");
        } else {
            // POST: inline form fields
            e += F("<form id='");
            e += fid;
            e += F("' onsubmit='return false' style='margin-top:8px'>");
            e += tryFields;
            e += F("</form>"
                   "<button class='btn btn-secondary try-btn' onclick=\"");
            e += "swSend('POST','";
            e += url;
            e += "','";
            e += fid;
            e += "','";
            e += rid;
            e += F("')\"> \u041E\u0442\u043F\u0440\u0430\u0432\u0438\u0442\u044C</button>");
        }
        e += F(" <span id='");
        e += rid;
        e += F("_sc'></span>"
               "<div class='ep-resp' id='");
        e += rid;
        e += F("'>");
        e += respInit;
        e += F("</div></div></div></div>");
        return e;
    };

    // ===== GET /api/data =====
    _server.sendContent(ep("1", "GET", "m-get", "/api/data",
        "\u0422\u0435\u043A\u0443\u0449\u0438\u0435 \u0434\u0430\u043D\u043D\u044B\u0435 \u0434\u0430\u0442\u0447\u0438\u043A\u043E\u0432 + \u0430\u043F\u0442\u0430\u0439\u043C",
        F("<tr><td>-</td><td>-</td><td>\u041F\u0430\u0440\u0430\u043C\u0435\u0442\u0440\u044B \u043D\u0435 \u0442\u0440\u0435\u0431\u0443\u044E\u0442\u0441\u044F</td></tr>"),
        "",
        "{\"device\":\"ESP-HUB\",\"uptime\":120,\"heap\":220000,"
        "\"sensors\":{\"0_temperature\":\"23.5\"}}",
        true));

    // ===== GET /api/scan =====
    _server.sendContent(ep("2", "GET", "m-get", "/api/scan",
        "\u0421\u043A\u0430\u043D\u0438\u0440\u043E\u0432\u0430\u043D\u0438\u0435 WiFi \u0441\u0435\u0442\u0435\u0439 (~4\u0441)",
        F("<tr><td>ssid</td><td>string</td><td>\u041D\u0430\u0437\u0432\u0430\u043D\u0438\u0435 \u0441\u0435\u0442\u0438</td></tr>"
          "<tr><td>rssi</td><td>int</td><td>\u0423\u0440\u043E\u0432\u0435\u043D\u044C \u0441\u0438\u0433\u043D\u0430\u043B\u0430 (dBm)</td></tr>"
          "<tr><td>enc</td><td>bool</td><td>true = \u0437\u0430\u0449\u0438\u0449\u0435\u043D\u0430 \u043F\u0430\u0440\u043E\u043B\u0435\u043C</td></tr>"),
        "",
        "[{\"ssid\":\"HomeWiFi\",\"rssi\":-55,\"enc\":true}]",
        true));

    // ===== POST /save/wifi =====
    _server.sendContent(ep("3", "POST", "m-post", "/save/wifi",
        "\u0421\u043E\u0445\u0440\u0430\u043D\u0438\u0442\u044C \u0434\u0430\u043D\u043D\u044B\u0435 WiFi &amp; \u043F\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044C",
        F("<tr><td>wifi_ssid</td><td>string</td><td>SSID \u0441\u0435\u0442\u0438</td></tr>"
          "<tr><td>wifi_pass</td><td>string</td><td>\u041F\u0430\u0440\u043E\u043B\u044C (\u043D\u0435\u043E\u0431\u044F\u0437\u0430\u0442\u0435\u043B\u044C\u043D\u043E)</td></tr>"),
        F("<label>wifi_ssid</label>"
          "<input type='text' name='wifi_ssid' placeholder='\u041C\u043E\u044F\u0421\u0435\u0442\u044C'>"
          "<label>wifi_pass</label>"
          "<input type='text' name='wifi_pass' placeholder='\u043F\u0430\u0440\u043E\u043B\u044C'>"),
        "(\u043F\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u043A\u0430)", false));

    // ===== POST /save/mqtt =====
    _server.sendContent(ep("4", "POST", "m-post", "/save/mqtt",
        "\u0421\u043E\u0445\u0440\u0430\u043D\u0438\u0442\u044C \u043D\u0430\u0441\u0442\u0440\u043E\u0439\u043A\u0438 MQTT-\u0431\u0440\u043E\u043A\u0435\u0440\u0430",
        F("<tr><td>mqtt_host</td><td>string</td><td>IP \u0438\u043B\u0438 \u0445\u043E\u0441\u0442 \u0431\u0440\u043E\u043A\u0435\u0440\u0430</td></tr>"
          "<tr><td>mqtt_port</td><td>int</td><td>\u041F\u043E\u0440\u0442 (\u043F\u043E \u0443\u043C\u043E\u043B\u0447\u0430\u043D\u0438\u044E: 1883)</td></tr>"
          "<tr><td>mqtt_user</td><td>string</td><td>\u041B\u043E\u0433\u0438\u043D (\u043D\u0435\u043E\u0431\u044F\u0437\u0430\u0442\u0435\u043B\u044C\u043D\u043E)</td></tr>"
          "<tr><td>mqtt_pass</td><td>string</td><td>\u041F\u0430\u0440\u043E\u043B\u044C (\u043D\u0435\u043E\u0431\u044F\u0437\u0430\u0442\u0435\u043B\u044C\u043D\u043E)</td></tr>"
          "<tr><td>mqtt_topic</td><td>string</td><td>\u0422\u043E\u043F\u0438\u043A \u043F\u0443\u0431\u043B\u0438\u043A\u0430\u0446\u0438\u0438</td></tr>"
          "<tr><td>mqtt_interval</td><td>int</td><td>\u0418\u043D\u0442\u0435\u0440\u0432\u0430\u043B \u0432 \u0441\u0435\u043A\u0443\u043D\u0434\u0430\u0445</td></tr>"),
        F("<label>mqtt_host</label><input type='text' name='mqtt_host' placeholder='192.168.1.100'>"
          "<label>mqtt_port</label><input type='number' name='mqtt_port' value='1883'>"
          "<label>mqtt_user</label><input type='text' name='mqtt_user' placeholder='\u043D\u0435\u043E\u0431\u044F\u0437\u0430\u0442\u0435\u043B\u044C\u043D\u043E'>"
          "<label>mqtt_pass</label><input type='text' name='mqtt_pass' placeholder='\u043D\u0435\u043E\u0431\u044F\u0437\u0430\u0442\u0435\u043B\u044C\u043D\u043E'>"
          "<label>mqtt_topic</label><input type='text' name='mqtt_topic' placeholder='esp-hub/telemetry'>"
          "<label>mqtt_interval</label><input type='number' name='mqtt_interval' value='10'>"),
        "\u0421\u043E\u0445\u0440\u0430\u043D\u0435\u043D\u043E", false));

    // ===== POST /save/ap =====
    _server.sendContent(ep("5", "POST", "m-post", "/save/ap",
        "\u0418\u0437\u043C\u0435\u043D\u0438\u0442\u044C SSID/\u043F\u0430\u0440\u043E\u043B\u044C \u0442\u043E\u0447\u043A\u0438 \u0434\u043E\u0441\u0442\u0443\u043F\u0430 &amp; \u0438\u043C\u044F \u0443\u0441\u0442\u0440\u043E\u0439\u0441\u0442\u0432\u0430",
        F("<tr><td>ap_ssid</td><td>string</td><td>\u0418\u043C\u044F \u0442\u043E\u0447\u043A\u0438 \u0434\u043E\u0441\u0442\u0443\u043F\u0430</td></tr>"
          "<tr><td>ap_pass</td><td>string</td><td>\u041F\u0430\u0440\u043E\u043B\u044C (\u043C\u0438\u043D. 8 \u0441\u0438\u043C\u0432\u043E\u043B\u043E\u0432)</td></tr>"
          "<tr><td>device_name</td><td>string</td><td>\u0418\u043C\u044F \u0432 MQTT/UI</td></tr>"),
        F("<label>ap_ssid</label><input type='text' name='ap_ssid' placeholder='ESP-HUB'>"
          "<label>ap_pass</label><input type='text' name='ap_pass' placeholder='12345678'>"
          "<label>device_name</label><input type='text' name='device_name' placeholder='ESP-HUB'>"),
        "(\u043F\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u043A\u0430)", false));

    // ===== POST /save/sensors (abbreviated) =====
    _server.sendContent(ep("6", "POST", "m-post", "/save/sensors",
        "\u0421\u043E\u0445\u0440\u0430\u043D\u0438\u0442\u044C \u043A\u043E\u043D\u0444\u0438\u0433\u0443\u0440\u0430\u0446\u0438\u044E \u0441\u043B\u043E\u0442\u043E\u0432 \u0434\u0430\u0442\u0447\u0438\u043A\u043E\u0432",
        F("<tr><td>en_N</td><td>flag</td><td>\u041F\u0440\u0438\u0441\u0443\u0442\u0441\u0442\u0432\u0438\u0435 = \u0432\u043A\u043B\u044E\u0447\u0451\u043D (N = \u0441\u043B\u043E\u0442 0-7)</td></tr>"
          "<tr><td>type_N</td><td>int</td><td>0=\u041D\u0435\u0442 1=DHT11 2=DHT22 3=DS18B20 4=Analog 5=BMP280 6=BH1750 7=MHZ19 8=SDS011 9=UART 10=CAN 11=MH-Sensor</td></tr>"
          "<tr><td>bus_N</td><td>int</td><td>0=\u0410\u0432\u0442\u043E 1=GPIO 2=I2C 3=1-Wire 4=UART 5=CAN</td></tr>"
          "<tr><td>out_N</td><td>int</td><td>0=MQTT 1=HTTP POST 2=CAN TX 3=Serial</td></tr>"
          "<tr><td>pin_N</td><td>int</td><td>GPIO \u043F\u0438\u043D \u0438\u043B\u0438 I2C \u0430\u0434\u0440\u0435\u0441</td></tr>"
          "<tr><td>pin2_N</td><td>int</td><td>UART TX \u043F\u0438\u043D</td></tr>"
          "<tr><td>uart_N</td><td>int</td><td>\u041D\u043E\u043C\u0435\u0440 UART: 1 \u0438\u043B\u0438 2</td></tr>"
          "<tr><td>cid_N</td><td>int</td><td>CAN frame ID</td></tr>"
          "<tr><td>cdlc_N</td><td>int</td><td>CAN DLC (1-8)</td></tr>"
          "<tr><td>hurl_N</td><td>string</td><td>HTTP POST URL</td></tr>"
          "<tr><td>label_N</td><td>string</td><td>\u041C\u0435\u0442\u043A\u0430</td></tr>"),
        F("<div class='text-muted' style='font-size:12px;margin-bottom:6px'>"
          "\u041F\u0440\u0438\u043C\u0435\u0440: \u0432\u043A\u043B\u044E\u0447\u0438\u0442\u044C \u0441\u043B\u043E\u0442 0 \u043A\u0430\u043A DHT22 \u043D\u0430 GPIO 4</div>"
          "<label>en_0</label><input type='text' name='en_0' value='1'>"
          "<label>type_0</label><input type='number' name='type_0' value='2'>"
          "<label>pin_0</label><input type='number' name='pin_0' value='4'>"
          "<label>label_0</label><input type='text' name='label_0' placeholder='\u041A\u0443\u0445\u043D\u044F'>"),
        "\u0421\u043E\u0445\u0440\u0430\u043D\u0435\u043D\u043E", false));

    // ===== POST /reboot =====
    _server.sendContent(ep("7", "POST", "m-post", "/reboot",
        "\u041F\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044C \u0443\u0441\u0442\u0440\u043E\u0439\u0441\u0442\u0432\u043E",
        F("<tr><td>-</td><td>-</td><td>\u041F\u0430\u0440\u0430\u043C\u0435\u0442\u0440\u044B \u043D\u0435 \u0442\u0440\u0435\u0431\u0443\u044E\u0442\u0441\u044F</td></tr>"),
        "",
        "\u041F\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u043A\u0430...", false));

    // ===== POST /reset =====
    _server.sendContent(ep("8", "POST", "m-post", "/reset",
        "\u0421\u0431\u0440\u043E\u0441 \u043D\u0430\u0441\u0442\u0440\u043E\u0435\u043A &amp; \u043F\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u043A\u0430",
        F("<tr><td>-</td><td>-</td><td>\u041F\u0430\u0440\u0430\u043C\u0435\u0442\u0440\u043E\u0432 \u043D\u0435\u0442. \u0423\u0434\u0430\u043B\u044F\u0435\u0442 config.json.</td></tr>"),
        "",
        "\u0421\u0431\u0440\u043E\u0441...", false));

    // ===== Fixture API section header =====
    _server.sendContent(F("<h3 style='margin:18px 0 6px;color:var(--txt2);font-size:14px;text-transform:uppercase;letter-spacing:.05em'>"
        "\u0421\u0432\u0435\u0442\u0438\u043b\u044c\u043d\u0438\u043a\u0438 (Fixture)</h3>"));

    // ===== GET /api/fixture =====
    _server.sendContent(ep("f1", "GET", "m-get", "/api/fixture",
        "\u0422\u0435\u043a\u0443\u0449\u0435\u0435 \u0441\u043e\u0441\u0442\u043e\u044f\u043d\u0438\u0435: \u043a\u0430\u043d\u0430\u043b\u044b + enabled + ack",
        F("<tr><td>-</td><td>-</td><td>\u041f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u044b \u043d\u0435 \u0442\u0440\u0435\u0431\u0443\u044e\u0442\u0441\u044f</td></tr>"),
        "",
        "{\"enabled\":true,\"red\":0,\"fr\":0,\"blue\":0,\"white\":20,\"ack\":true}",
        true));

    // ===== POST /api/fixture/toggle =====
    _server.sendContent(ep("f2", "POST", "m-post", "/api/fixture/toggle",
        "\u041f\u0435\u0440\u0435\u043a\u043b\u044e\u0447\u0438\u0442\u044c: \u0435\u0441\u043b\u0438 \u0432\u043a\u043b\u044e\u0447\u0435\u043d\u043e \u2014 \u0432\u044b\u043a\u043b\u044e\u0447\u0438\u0442\u044c, \u0435\u0441\u043b\u0438 \u0432\u044b\u043a\u043b\u044e\u0447\u0435\u043d\u043e \u2014 \u0432\u043e\u0441\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u044c \u0441\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u043d\u0443\u044e \u044f\u0440\u043a\u043e\u0441\u0442\u044c",
        F("<tr><td>-</td><td>-</td><td>\u041f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u044b \u043d\u0435 \u0442\u0440\u0435\u0431\u0443\u044e\u0442\u0441\u044f</td></tr>"),
        "",
        "{\"ok\":true,\"status\":\"on\",\"red\":0,\"fr\":0,\"blue\":0,\"white\":20}",
        false));

    // ===== POST /api/fixture/set =====
    _server.sendContent(ep("f3", "POST", "m-post", "/api/fixture/set",
        "\u0423\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u044c \u044f\u0440\u043a\u043e\u0441\u0442\u044c \u043a\u0430\u043d\u0430\u043b\u043e\u0432 (0\u2013200)",
        F("<tr><td>red</td><td>int 0-200</td><td>\u041a\u0440\u0430\u0441\u043d\u044b\u0439 \u043a\u0430\u043d\u0430\u043b</td></tr>"
          "<tr><td>fr</td><td>int 0-200</td><td>\u0414\u0430\u043b\u044c\u043d\u0438\u0439 \u043a\u0440\u0430\u0441\u043d\u044b\u0439</td></tr>"
          "<tr><td>blue</td><td>int 0-200</td><td>\u0421\u0438\u043d\u0438\u0439 \u043a\u0430\u043d\u0430\u043b</td></tr>"
          "<tr><td>white</td><td>int 0-200</td><td>\u0411\u0435\u043b\u044b\u0439 \u043a\u0430\u043d\u0430\u043b</td></tr>"),
        F("<label>red</label><input type='number' name='red' min='0' max='200' value='0'>"
          "<label>fr</label><input type='number' name='fr' min='0' max='200' value='0'>"
          "<label>blue</label><input type='number' name='blue' min='0' max='200' value='0'>"
          "<label>white</label><input type='number' name='white' min='0' max='200' value='20'>"),
        "{\"ok\":true}", false));

    // ===== POST /api/fixture/dim =====
    _server.sendContent(ep("f4", "POST", "m-post", "/api/fixture/dim",
        "\u0418\u0437\u043c\u0435\u043d\u0438\u0442\u044c \u044f\u0440\u043a\u043e\u0441\u0442\u044c \u0430\u043a\u0442\u0438\u0432\u043d\u044b\u0445 \u043a\u0430\u043d\u0430\u043b\u043e\u0432 \u043d\u0430 step (0\u2013200, \u043c\u043e\u0436\u0435\u0442 \u0431\u044b\u0442\u044c \u043e\u0442\u0440\u0438\u0446\u0430\u0442\u0435\u043b\u044c\u043d\u044b\u043c)",
        F("<tr><td>step</td><td>int</td><td>\u0428\u0430\u0433 (0-200 \u0448\u043a\u0430\u043b\u0430). \u041e\u0442\u0440\u0438\u0446\u0430\u0442\u0435\u043b\u044c\u043d\u044b\u0439 \u2014 \u0443\u043c\u0435\u043d\u044c\u0448\u0430\u0435\u0442. \u041f\u0440\u0438\u043c\u0435\u043d\u044f\u0435\u0442\u0441\u044f \u0442\u043e\u043b\u044c\u043a\u043e \u043a \u043a\u0430\u043d\u0430\u043b\u0430\u043c > 0.</td></tr>"),
        F("<label>step</label><input type='number' name='step' value='20'>"),
        "{\"ok\":true,\"red\":0,\"fr\":0,\"blue\":0,\"white\":40}",
        false));

    // ===== POST /api/fixture/color =====
    _server.sendContent(ep("f5", "POST", "m-post", "/api/fixture/color",
        "\u0423\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u044c \u043f\u0440\u0435\u0441\u0435\u0442 \u0438\u043b\u0438 \u0440\u0430\u0437\u043e\u0432\u044b\u0435 \u0437\u043d\u0430\u0447\u0435\u043d\u0438\u044f",
        F("<tr><td>preset</td><td>string</td><td>off / red / far_red / blue / white / full / grow</td></tr>"
          "<tr><td>red</td><td>int 0-200</td><td>\u0418\u0441\u043f\u043e\u043b\u044c\u0437\u0443\u0435\u0442\u0441\u044f \u0435\u0441\u043b\u0438 preset \u043d\u0435 \u0443\u043a\u0430\u0437\u0430\u043d</td></tr>"
          "<tr><td>fr / blue / white</td><td>int 0-200</td><td>\u0410\u043d\u0430\u043b\u043e\u0433\u0438\u0447\u043d\u043e</td></tr>"),
        F("<label>preset</label><input type='text' name='preset' placeholder='red / full / ...'>"
          "<label>red (raw)</label><input type='number' name='red' min='0' max='200' value='0'>"
          "<label>white (raw)</label><input type='number' name='white' min='0' max='200' value='0'>"),
        "{\"ok\":true,\"red\":200,\"fr\":0,\"blue\":0,\"white\":0}",
        false));

    // ===== POST /api/fixture/timer/enable =====
    _server.sendContent(ep("f6", "POST", "m-post", "/api/fixture/timer/enable",
        "\u0412\u043a\u043b/\u0432\u044b\u043a\u043b \u043a\u043e\u043d\u043a\u0440\u0435\u0442\u043d\u044b\u0439 \u0442\u0430\u0439\u043c\u0435\u0440 (0\u20137) \u0431\u0435\u0437 \u0438\u0437\u043c\u0435\u043d\u0435\u043d\u0438\u044f \u043e\u0441\u0442\u0430\u043b\u044c\u043d\u044b\u0445 \u043f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u043e\u0432",
        F("<tr><td>id</td><td>int 0-7</td><td>\u0418\u043d\u0434\u0435\u043a\u0441 \u0442\u0430\u0439\u043c\u0435\u0440\u0430</td></tr>"
          "<tr><td>enabled</td><td>bool</td><td>true/1 \u2014 \u0432\u043a\u043b\u044e\u0447\u0438\u0442\u044c, false/0 \u2014 \u0432\u044b\u043a\u043b\u044e\u0447\u0438\u0442\u044c</td></tr>"),
        F("<label>id</label><input type='number' name='id' min='0' max='7' value='0'>"
          "<label>enabled</label><input type='text' name='enabled' value='true'>"),
        "{\"ok\":true,\"id\":0,\"enabled\":true}",
        false));

    // ===== POST /api/fixture/scenario/enable =====
    _server.sendContent(ep("f7", "POST", "m-post", "/api/fixture/scenario/enable",
        "\u0412\u043a\u043b/\u0432\u044b\u043a\u043b \u043a\u043e\u043d\u043a\u0440\u0435\u0442\u043d\u044b\u0439 \u0441\u0446\u0435\u043d\u0430\u0440\u0438\u0439 (0\u20137)",
        F("<tr><td>id</td><td>int 0-7</td><td>\u0418\u043d\u0434\u0435\u043a\u0441 \u0441\u0446\u0435\u043d\u0430\u0440\u0438\u044f</td></tr>"
          "<tr><td>enabled</td><td>bool</td><td>true/1 \u2014 \u0432\u043a\u043b\u044e\u0447\u0438\u0442\u044c, false/0 \u2014 \u0432\u044b\u043a\u043b\u044e\u0447\u0438\u0442\u044c</td></tr>"),
        F("<label>id</label><input type='number' name='id' min='0' max='7' value='0'>"
          "<label>enabled</label><input type='text' name='enabled' value='true'>"),
        "{\"ok\":true,\"id\":0,\"enabled\":true}",
        false));

    // ===== POST /api/fixture/timer/trigger =====
    _server.sendContent(ep("f8", "POST", "m-post", "/api/fixture/timer/trigger",
        "\u0412\u0440\u0443\u0447\u043d\u043e\u0435 \u0441\u0440\u0430\u0431\u0430\u0442\u044b\u0432\u0430\u043d\u0438\u0435 \u0442\u0430\u0439\u043c\u0435\u0440\u0430 \u2014 \u043d\u0435\u043c\u0435\u0434\u043b\u0435\u043d\u043d\u043e \u043f\u0440\u0438\u043c\u0435\u043d\u044f\u0435\u0442 \u0435\u0433\u043e \u043a\u0430\u043d\u0430\u043b\u044b",
        F("<tr><td>id</td><td>int 0-7</td><td>\u0418\u043d\u0434\u0435\u043a\u0441 \u0442\u0430\u0439\u043c\u0435\u0440\u0430</td></tr>"),
        F("<label>id</label><input type='number' name='id' min='0' max='7' value='0'>"),
        "{\"ok\":true,\"id\":0,\"red\":140,\"fr\":100,\"blue\":100,\"white\":60}",
        false));

    // ===== System/Network section header =====
    _server.sendContent(F("<h3 style='margin:18px 0 6px;color:var(--txt2);font-size:14px;text-transform:uppercase;letter-spacing:.05em'>"
        "\u0421\u0438\u0441\u0442\u0435\u043c\u0430 / \u0421\u0435\u0442\u044c</h3>"));

    // ===== GET /api/system =====
    _server.sendContent(ep("s1", "GET", "m-get", "/api/system",
        "\u0420\u0430\u043d\u0442\u0430\u0439\u043c \u0438\u043d\u0444\u043e: heap, uptime, CPU, flash",
        F("<tr><td>-</td><td>-</td><td>\u041f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u044b \u043d\u0435 \u0442\u0440\u0435\u0431\u0443\u044e\u0442\u0441\u044f</td></tr>"),
        "",
        "{\"heap\":180000,\"heap_min\":160000,\"uptime_s\":3600,\"cpu_freq_mhz\":240,"
        "\"flash_size\":4194304,\"sketch_size\":1564101,\"device\":\"ESP-HUB\"}",
        true));

    // ===== GET /api/wifi =====
    _server.sendContent(ep("s2", "GET", "m-get", "/api/wifi",
        "\u0421\u0442\u0430\u0442\u0443\u0441 WiFi: \u043f\u043e\u0434\u043a\u043b\u044e\u0447\u0435\u043d\u0438\u0435, IP, RSSI, MAC, AP \u043a\u043b\u0438\u0435\u043d\u0442\u044b",
        F("<tr><td>-</td><td>-</td><td>\u041f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u044b \u043d\u0435 \u0442\u0440\u0435\u0431\u0443\u044e\u0442\u0441\u044f</td></tr>"),
        "",
        "{\"connected\":true,\"ap\":false,\"enabled\":true,\"ip\":\"192.168.1.50\","
        "\"mac\":\"AA:BB:CC:DD:EE:FF\",\"rssi\":-62,\"ssid\":\"HomeWiFi\","
        "\"ap_clients\":0,\"ap_macs\":\"\"}",
        true));

    // ===== GET /api/mqtt =====
    _server.sendContent(ep("s3", "GET", "m-get", "/api/mqtt",
        "\u0421\u0442\u0430\u0442\u0443\u0441 MQTT-\u0431\u0440\u043e\u043a\u0435\u0440\u0430",
        F("<tr><td>-</td><td>-</td><td>\u041f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u044b \u043d\u0435 \u0442\u0440\u0435\u0431\u0443\u044e\u0442\u0441\u044f</td></tr>"),
        "",
        "{\"connected\":true,\"host\":\"192.168.1.100\",\"port\":1883,"
        "\"topic\":\"esp-hub/telemetry\",\"interval_s\":10}",
        true));

    // ===== GET /api/ble =====
    _server.sendContent(ep("s4", "GET", "m-get", "/api/ble",
        "\u0421\u0442\u0430\u0442\u0443\u0441 BLE + \u0436\u0443\u0440\u043d\u0430\u043b \u043f\u0440\u0438\u043d\u044f\u0442\u044b\u0445 \u0441\u043e\u043e\u0431\u0449\u0435\u043d\u0438\u0439",
        F("<tr><td>-</td><td>-</td><td>\u041f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u044b \u043d\u0435 \u0442\u0440\u0435\u0431\u0443\u044e\u0442\u0441\u044f</td></tr>"),
        "",
        "{\"enabled\":true,\"connected\":false,\"log\":[]}",
        true));

    // ===== POST /api/ble/clear-log =====
    _server.sendContent(ep("s5", "POST", "m-post", "/api/ble/clear-log",
        "\u041e\u0447\u0438\u0441\u0442\u0438\u0442\u044c \u0436\u0443\u0440\u043d\u0430\u043b BLE RX",
        F("<tr><td>-</td><td>-</td><td>\u041f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u044b \u043d\u0435 \u0442\u0440\u0435\u0431\u0443\u044e\u0442\u0441\u044f</td></tr>"),
        "",
        "{\"ok\":true}", false));

    // ===== POST /api/fixture/enable =====
    _server.sendContent(ep("f9", "POST", "m-post", "/api/fixture/enable",
        "\u0412\u043a\u043b/\u0432\u044b\u043a\u043b \u043c\u0435\u043d\u0435\u0434\u0436\u0435\u0440 \u0441\u0432\u0435\u0442\u0438\u043b\u044c\u043d\u0438\u043a\u043e\u0432 (\u0441\u043e\u0445\u0440\u0430\u043d\u044f\u0435\u0442 \u0432 \u043a\u043e\u043d\u0444\u0438\u0433)",
        F("<tr><td>enabled</td><td>bool</td><td>true/1 \u2014 \u0432\u043a\u043b\u044e\u0447\u0438\u0442\u044c, false/0 \u2014 \u0432\u044b\u043a\u043b\u044e\u0447\u0438\u0442\u044c</td></tr>"),
        F("<label>enabled</label><input type='text' name='enabled' value='true'>"),
        "{\"ok\":true,\"enabled\":true}", false));

    // ===== POST /api/fixture/demo =====
    _server.sendContent(ep("fa", "POST", "m-post", "/api/fixture/demo",
        "\u0417\u0430\u043f\u0443\u0441\u0442\u0438\u0442\u044c \u0434\u0435\u043c\u043e-\u043f\u043e\u0441\u043b\u0435\u0434\u043e\u0432\u0430\u0442\u0435\u043b\u044c\u043d\u043e\u0441\u0442\u044c \u0441\u0432\u0435\u0442\u0438\u043b\u044c\u043d\u0438\u043a\u0430",
        F("<tr><td>-</td><td>-</td><td>\u041f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u044b \u043d\u0435 \u0442\u0440\u0435\u0431\u0443\u044e\u0442\u0441\u044f</td></tr>"),
        "",
        "{\"ok\":true}", false));

    // ===== AI Agent section header =====
    _server.sendContent(F("<h3 style='margin:18px 0 6px;color:var(--txt2);font-size:14px;text-transform:uppercase;letter-spacing:.05em'>"
        "\xF0\x9F\xA4\x96 \xD0\x98\xD0\x98 \xD0\x90\xD0\xB3\xD0\xB5\xD0\xBD\xD1\x82</h3>"));

    // ===== POST /api/ai/chat =====
    _server.sendContent(ep("ai1", "POST", "m-post", "/api/ai/chat",
        "\xD0\x9E\xD1\x82\xD0\xBF\xD1\x80\xD0\xB0\xD0\xB2\xD0\xB8\xD1\x82\xD1\x8C \xD1\x81\xD0\xBE\xD0\xBE\xD0\xB1\xD1\x89\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 \xD0\xB0\xD0\xB3\xD0\xB5\xD0\xBD\xD1\x82\xD1\x83",
        F("<tr><td>message</td><td>string</td><td>\xD0\xA2\xD0\xB5\xD0\xBA\xD1\x81\xD1\x82 \xD0\xB7\xD0\xB0\xD0\xBF\xD1\x80\xD0\xBE\xD1\x81\xD0\xB0</td></tr>"),
        F("<label>message</label><input type='text' name='message' placeholder='\xD0\xA1\xD0\xBA\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xBA\xD0\xBE \xD1\x81\xD0\xB5\xD0\xB9\xD1\x87\xD0\xB0\xD1\x81 \xD0\xB3\xD1\x80\xD0\xB0\xD0\xB4\xD1\x83\xD1\x81\xD0\xBE\xD0\xB2?'>"),
        "{\"ok\":true}", false));

    // ===== GET /api/ai/status =====
    _server.sendContent(ep("ai2", "GET", "m-get", "/api/ai/status",
        "\xD0\xA1\xD1\x82\xD0\xB0\xD1\x82\xD1\x83\xD1\x81 \xD0\xB0\xD0\xB3\xD0\xB5\xD0\xBD\xD1\x82\xD0\xB0: \xD0\xBE\xD0\xB1\xD1\x80\xD0\xB0\xD0\xB1\xD0\xBE\xD1\x82\xD0\xBA\xD0\xB0, \xD0\xBF\xD0\xBE\xD1\x81\xD0\xBB\xD0\xB5\xD0\xB4\xD0\xBD\xD0\xB8\xD0\xB9 \xD0\xBE\xD1\x82\xD0\xB2\xD0\xB5\xD1\x82, seq",
        F("<tr><td>-</td><td>-</td><td>\xD0\x9F\xD0\xB0\xD1\x80\xD0\xB0\xD0\xBC\xD0\xB5\xD1\x82\xD1\x80\xD1\x8B \xD0\xBD\xD0\xB5 \xD1\x82\xD1\x80\xD0\xB5\xD0\xB1\xD1\x83\xD1\x8E\xD1\x82\xD1\x81\xD1\x8F</td></tr>"),
        "",
        "{\"processing\":false,\"seq\":3,\"response\":\"\xD0\xA2\xD0\xB5\xD0\xBC\xD0\xBF\xD0\xB5\xD1\x80\xD0\xB0\xD1\x82\xD1\x83\xD1\x80\xD0\xB0 22.5\\u00b0C\"}",
        true));

    // ===== POST /api/ai/history/clear =====
    _server.sendContent(ep("ai3", "POST", "m-post", "/api/ai/history/clear",
        "\xD0\x9E\xD1\x87\xD0\xB8\xD1\x81\xD1\x82\xD0\xB8\xD1\x82\xD1\x8C \xD0\xB8\xD1\x81\xD1\x82\xD0\xBE\xD1\x80\xD0\xB8\xD1\x8E \xD0\xB4\xD0\xB8\xD0\xB0\xD0\xBB\xD0\xBE\xD0\xB3\xD0\xB0",
        F("<tr><td>-</td><td>-</td><td>\xD0\x9F\xD0\xB0\xD1\x80\xD0\xB0\xD0\xBC\xD0\xB5\xD1\x82\xD1\x80\xD1\x8B \xD0\xBD\xD0\xB5 \xD1\x82\xD1\x80\xD0\xB5\xD0\xB1\xD1\x83\xD1\x8E\xD1\x82\xD1\x81\xD1\x8F</td></tr>"),
        "",
        "{\"ok\":true}", false));

    // ===== LM Studio sub-header =====
    _server.sendContent(F("<h3 style='margin:18px 0 6px;color:var(--txt2);font-size:14px;text-transform:uppercase;letter-spacing:.05em'>"
        "\xF0\x9F\x96\xA5 LM Studio</h3>"));

    // ===== POST /api/ai/lms/load =====
    _server.sendContent(ep("lms2", "POST", "m-post", "/api/ai/lms/load",
        "\xD0\x97\xD0\xB0\xD0\xB3\xD1\x80\xD1\x83\xD0\xB7\xD0\xB8\xD1\x82\xD1\x8C \xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB5\xD0\xBB\xD1\x8C (JSON body)",
        F("<tr><td>model</td><td>string</td><td>ID \xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB5\xD0\xBB\xD0\xB8</td></tr>"
          "<tr><td>context_length</td><td>int</td><td>\xD0\xA0\xD0\xB0\xD0\xB7\xD0\xBC\xD0\xB5\xD1\x80 \xD0\xBA\xD0\xBE\xD0\xBD\xD1\x82\xD0\xB5\xD0\xBA\xD1\x81\xD1\x82\xD0\xB0 (\xD1\x82\xD0\xBE\xD0\xBA\xD0\xB5\xD0\xBD\xD1\x8B)</td></tr>"),
        F("<label>model</label><input type='text' name='model' placeholder='qwen3-8b'>"
          "<label>context_length</label><input type='number' name='context_length' value='20000'>"),
        "{\"ok\":true}", false));

    // ===== POST /api/ai/lms/unload =====
    _server.sendContent(ep("lms3", "POST", "m-post", "/api/ai/lms/unload",
        "\xD0\x92\xD1\x8B\xD0\xB3\xD1\x80\xD1\x83\xD0\xB7\xD0\xB8\xD1\x82\xD1\x8C \xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB5\xD0\xBB\xD1\x8C (JSON body)",
        F("<tr><td>instance_id</td><td>string</td><td>ID \xD1\x8D\xD0\xBA\xD0\xB7\xD0\xB5\xD0\xBC\xD0\xBF\xD0\xBB\xD1\x8F\xD1\x80\xD0\xB0 (\xD0\xB8\xD0\xB7 /api/ai/lms/models)</td></tr>"),
        F("<label>instance_id</label><input type='text' name='instance_id' placeholder='instance-0'>"),
        "{\"ok\":true}", false));

    // ===== CRON section header =====
    _server.sendContent(F("<h3 style='margin:18px 0 6px;color:var(--txt2);font-size:14px;text-transform:uppercase;letter-spacing:.05em'>"
        "\xE2\x8F\xB0 CRON</h3>"));

    // ===== GET /api/cron =====
    _server.sendContent(ep("cr1", "GET", "m-get", "/api/cron",
        "\xD0\xA1\xD0\xBF\xD0\xB8\xD1\x81\xD0\xBE\xD0\xBA \xD0\xB7\xD0\xB0\xD0\xB4\xD0\xB0\xD1\x87 CRON",
        F("<tr><td>-</td><td>-</td><td>\xD0\x9F\xD0\xB0\xD1\x80\xD0\xB0\xD0\xBC\xD0\xB5\xD1\x82\xD1\x80\xD1\x8B \xD0\xBD\xD0\xB5 \xD1\x82\xD1\x80\xD0\xB5\xD0\xB1\xD1\x83\xD1\x8E\xD1\x82\xD1\x81\xD1\x8F</td></tr>"),
        "",
        "{\"tasks\":[{\"id\":0,\"expr\":\"0 8 * * *\",\"action\":\"light full\",\"enabled\":true}]}",
        true));

    // ===== POST /api/cron/add =====
    _server.sendContent(ep("cr2", "POST", "m-post", "/api/cron/add",
        "\xD0\x94\xD0\xBE\xD0\xB1\xD0\xB0\xD0\xB2\xD0\xB8\xD1\x82\xD1\x8C \xD0\xB7\xD0\xB0\xD0\xB4\xD0\xB0\xD1\x87\xD1\x83 CRON",
        F("<tr><td>expr</td><td>string</td><td>CRON-\xD0\xB2\xD1\x8B\xD1\x80\xD0\xB0\xD0\xB6\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 (5 \xD0\xBF\xD0\xBE\xD0\xBB\xD0\xB5\xD0\xB9, \xD0\xBD\xD0\xB0\xD0\xBF\xD1\x80. 0 8 * * *)</td></tr>"
          "<tr><td>action</td><td>string</td><td>\xD0\x94\xD0\xB5\xD0\xB9\xD1\x81\xD1\x82\xD0\xB2\xD0\xB8\xD0\xB5: light full / light off / sensor read / ai &lt;text&gt;</td></tr>"),
        F("<label>expr</label><input type='text' name='expr' placeholder='0 8 * * *'>"
          "<label>action</label><input type='text' name='action' placeholder='light full'>"),
        "{\"ok\":true,\"id\":1}", false));

    // ===== POST /api/cron/delete =====
    _server.sendContent(ep("cr3", "POST", "m-post", "/api/cron/delete",
        "\xD0\xA3\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xB8\xD1\x82\xD1\x8C \xD0\xB7\xD0\xB0\xD0\xB4\xD0\xB0\xD1\x87\xD1\x83 CRON",
        F("<tr><td>id</td><td>int</td><td>\xD0\x98\xD0\xBD\xD0\xB4\xD0\xB5\xD0\xBA\xD1\x81 \xD0\xB7\xD0\xB0\xD0\xB4\xD0\xB0\xD1\x87\xD0\xB8</td></tr>"),
        F("<label>id</label><input type='number' name='id' value='0'>"),
        "{\"ok\":true}", false));

    // ===== POST /api/cron/enable =====
    _server.sendContent(ep("cr4", "POST", "m-post", "/api/cron/enable",
        "\xD0\x92\xD0\xBA\xD0\xBB/\xD0\xB2\xD1\x8B\xD0\xBA\xD0\xBB \xD0\xB7\xD0\xB0\xD0\xB4\xD0\xB0\xD1\x87\xD1\x83 CRON",
        F("<tr><td>id</td><td>int</td><td>\xD0\x98\xD0\xBD\xD0\xB4\xD0\xB5\xD0\xBA\xD1\x81 \xD0\xB7\xD0\xB0\xD0\xB4\xD0\xB0\xD1\x87\xD0\xB8</td></tr>"
          "<tr><td>enabled</td><td>bool</td><td>true \xE2\x80\x94 \xD0\xB2\xD0\xBA\xD0\xBB, false \xE2\x80\x94 \xD0\xB2\xD1\x8B\xD0\xBA\xD0\xBB</td></tr>"),
        F("<label>id</label><input type='number' name='id' value='0'>"
          "<label>enabled</label><input type='text' name='enabled' value='true'>"),
        "{\"ok\":true}", false));

    // ===== POST /api/cron/tz =====
    _server.sendContent(ep("cr5", "POST", "m-post", "/api/cron/tz",
        "\xD0\xA3\xD1\x81\xD1\x82\xD0\xB0\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xB8\xD1\x82\xD1\x8C \xD1\x87\xD0\xB0\xD1\x81\xD0\xBE\xD0\xB2\xD0\xBE\xD0\xB9 \xD0\xBF\xD0\xBE\xD1\x8F\xD1\x81 POSIX",
        F("<tr><td>tz</td><td>string</td><td>POSIX TZ (\xD0\xBD\xD0\xB0\xD0\xBF\xD1\x80. MSK-3)</td></tr>"),
        F("<label>tz</label><input type='text' name='tz' placeholder='MSK-3'>"),
        "{\"ok\":true}", false));

    // ===== System section addition =====
    _server.sendContent(F("<h3 style='margin:18px 0 6px;color:var(--txt2);font-size:14px;text-transform:uppercase;letter-spacing:.05em'>"
        "\xF0\x9F\x8C\x90 \xD0\xA1\xD0\xB5\xD1\x82\xD1\x8C</h3>"));

    // ===== POST /api/nat/toggle =====
    _server.sendContent(ep("nat1", "POST", "m-post", "/api/nat/toggle",
        "\xD0\x92\xD0\xBA\xD0\xBB/\xD0\xB2\xD1\x8B\xD0\xBA\xD0\xBB NAT (\xD0\xBC\xD0\xBE\xD1\x81\xD1\x82 AP \xE2\x86\x92 STA)",
        F("<tr><td>-</td><td>-</td><td>\xD0\x9F\xD0\xB0\xD1\x80\xD0\xB0\xD0\xBC\xD0\xB5\xD1\x82\xD1\x80\xD1\x8B \xD0\xBD\xD0\xB5 \xD1\x82\xD1\x80\xD0\xB5\xD0\xB1\xD1\x83\xD1\x8E\xD1\x82\xD1\x81\xD1\x8F</td></tr>"),
        "",
        "{\"ok\":true,\"nat\":true}", false));

    // ===== Voice Service section header =====
    _server.sendContent(F("<h3 style='margin:18px 0 6px;color:var(--txt2);font-size:14px;text-transform:uppercase;letter-spacing:.05em'>"
        "\xF0\x9F\x8E\x99 \xD0\x93\xD0\xBE\xD0\xBB\xD0\xBE\xD1\x81\xD0\xBE\xD0\xB2\xD0\xBE\xD0\xB9 \xD1\x81\xD0\xB5\xD1\x80\xD0\xB2\xD0\xB8\xD1\x81</h3>"));
    _server.sendContent(F("<div style='background:var(--bg3);border:1px solid var(--brd);border-radius:8px;"
        "padding:12px 14px;margin:10px 0;font-size:13px'>"
        "<b>voice_service</b> \xE2\x80\x94 \xD0\xB2\xD0\xBD\xD0\xB5\xD1\x88\xD0\xBD\xD0\xB8\xD0\xB9 Python-\xD1\x81\xD0\xB5\xD1\x80\xD0\xB2\xD0\xB8\xD1\x81, \xD0\xBF\xD0\xBE\xD0\xBB\xD0\xBD\xD0\xBE\xD1\x81\xD1\x82\xD1\x8C\xD1\x8E \xD1\x81\xD0\xBE\xD0\xB2\xD0\xBC\xD0\xB5\xD1\x81\xD1\x82\xD0\xB8\xD0\xBC \xD1\x81 \xD0\x98\xD0\x98-\xD0\xB0\xD0\xB3\xD0\xB5\xD0\xBD\xD1\x82\xD0\xBE\xD0\xBC.<br>"
        "\xD0\x90\xD1\x80\xD1\x85\xD0\xB8\xD1\x82\xD0\xB5\xD0\xBA\xD1\x82\xD1\x83\xD1\x80\xD0\xB0: "
        "<code>\xD0\x93\xD0\xBE\xD0\xBB\xD0\xBE\xD1\x81 TG</code> \xe2\x86\x92 "
        "<code>Vosk STT</code> \xe2\x86\x92 "
        "<code>\xD0\xA2\xD0\xB5\xD0\xBA\xD1\x81\xD1\x82 \xD0\xB2 TG</code> \xe2\x86\x92 "
        "<code>ESP32 \xD0\xBF\xD0\xBE\xD0\xBB\xD0\xBB\xD0\xB8\xD0\xBD\xD0\xB3</code> \xe2\x86\x92 "
        "<code>\xD0\x98\xD0\x98-\xD0\xB0\xD0\xB3\xD0\xB5\xD0\xBD\xD1\x82</code><br>"
        "HTTP API: <code>:5000/health</code>, <code>:5000/transcribe</code>, <code>:5000/transcribe_telegram</code>"
        "</div>"));

    // MQTT payload example
    String mqttCard;
    mqttCard += F("<div class='text-muted' style='margin-bottom:8px'>\u0422\u043E\u043F\u0438\u043A: <code>");
    mqttCard += _cfg->cfg.mqtt_topic;
    mqttCard += F("</code></div>"
                  "<div class='ep-resp' style='max-height:none'>"
                  "{\n"
                  "  &quot;device&quot;:  &quot;ESP-HUB&quot;,\n"
                  "  &quot;uptime&quot;:  3600,\n"
                  "  &quot;heap&quot;:    215040,\n"
                  "  &quot;sensors&quot;: {\n"
                  "    &quot;0_temperature&quot;: &quot;23.50&quot;,\n"
                  "    &quot;0_humidity&quot;:    &quot;45.20&quot;,\n"
                  "    &quot;2_co2&quot;:         &quot;412.00&quot;,\n"
                  "    &quot;5_pm2.5&quot;:       &quot;12.30&quot;\n"
                  "  }\n"
                  "}\n"
                  "</div>"
                  "<div class='text-muted' style='margin-top:6px;font-size:12px'>"
                  "\u0424\u043E\u0440\u043C\u0430\u0442 \u043A\u043B\u044E\u0447\u0430: <code>{\u0441\u043B\u043E\u0442}_{\u043F\u043E\u043B\u0435}</code>"
                  "</div>");
    _server.sendContent(card("\u0424\u043E\u0440\u043C\u0430\u0442 MQTT \u0441\u043E\u043E\u0431\u0449\u0435\u043D\u0438\u044F", mqttCard));

    _server.sendContent(F("</div>"));
    sendPageFooter();
    _server.sendContent("");
    _server.client().stop();
}

// ================================================================
//                       API ENDPOINTS
// ================================================================

void WebPortal::handleApiData() {
    JsonDocument doc;
    _sensors->buildJson(doc, _cfg->cfg.device_name);
    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    _server.send(200, "application/json", buf);
}

void WebPortal::handleNotFound() {
    String dest = String("http://") + _wifi->apIP();
    dest += _wifi->isConnected() ? "/" : "/wifi";
    _server.sendHeader("Location", dest, true);
    _server.sendHeader("Connection", "close");
    _server.send(302, "text/plain", "Redirecting...");
}

// ================================================================
//                    LIGHTWEIGHT WIFI SETUP PAGE
// For captive portal / first boot. No heavy JS, minimal memory.
// ================================================================
void WebPortal::handleWifiSetup() {
    String p;
    p.reserve(2000);
    p += F("<!DOCTYPE html><html><head>"
           "<meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>ESP-HUB \u2014 Wi-Fi</title>"
           "<style>"
           "body{background:#0f1117;color:#e1e4e8;font-family:sans-serif;"
           "display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0}"
           ".box{background:#161b22;border:1px solid #30363d;border-radius:12px;"
           "padding:28px 24px;width:100%;max-width:360px}"
           "h2{margin:0 0 6px;font-size:18px;color:#58a6ff;text-align:center}"
           ".sub{text-align:center;color:#8b949e;font-size:12px;margin-bottom:16px}"
           "label{display:block;font-size:13px;color:#8b949e;margin:12px 0 4px}"
           "input[type=text],input[type=password]{"
           "width:100%;box-sizing:border-box;padding:9px 12px;background:#0d1117;"
           "border:1px solid #30363d;border-radius:6px;color:#e1e4e8;font-size:15px}"
           ".eye{position:relative}"
           ".eye input{padding-right:40px}"
           ".eye button{position:absolute;right:8px;top:50%;transform:translateY(-50%);"
           "background:none;border:none;color:#8b949e;cursor:pointer;font-size:16px;padding:4px}"
           "button[type=submit]{width:100%;margin-top:18px;padding:11px;"
           "background:#1f6feb;border:none;border-radius:8px;"
           "color:#fff;font-size:15px;font-weight:600;cursor:pointer}"
           ".status{font-size:13px;color:#8b949e;text-align:center;margin-bottom:14px;"
           "background:#0d1117;border:1px solid #30363d;border-radius:8px;padding:8px 12px}"
           ".ok{color:#3fb950}.warn{color:#d29922}"
           ".full{width:100%;margin-top:12px;padding:8px;"
           "background:#21262d;border:1px solid #30363d;border-radius:6px;"
           "color:#58a6ff;font-size:13px;text-align:center;text-decoration:none;display:block}"
           "</style></head><body><div class='box'>"
           "<h2>ESP-HUB Wi-Fi</h2>"
           "<div class='sub'>\u041D\u0430\u0441\u0442\u0440\u043E\u0439\u043A\u0430 \u043F\u043E\u0434\u043A\u043B\u044E\u0447\u0435\u043D\u0438\u044F</div>");

    // Status badge
    p += F("<div class='status'>");
    if (_wifi->isConnected()) {
        p += F("<span class='ok'>\u041F\u043E\u0434\u043A\u043B\u044E\u0447\u0435\u043D\u043E: </span><b>");
        p += htmlAttr(_cfg->cfg.wifi_ssid);
        p += F("</b><br><span style='color:#8b949e;font-size:12px'>IP: ");
        p += _wifi->localIP();
        p += F("</span>");
    } else {
        p += F("<span class='warn'>\u0420\u0435\u0436\u0438\u043C AP &bull; ");
        p += _wifi->apIP();
        p += F("</span>");
        if (strlen(_cfg->cfg.wifi_ssid) > 0) {
            p += F("<br><span style='font-size:12px'>\u041F\u043E\u0441\u043B\u0435\u0434\u043D\u044F\u044F \u0441\u0435\u0442\u044C: ");
            p += htmlAttr(_cfg->cfg.wifi_ssid);
            p += F("</span>");
        }
    }
    p += F("</div>"
           "<form method='POST' action='/save/wifi'>"
           "<label>\u0418\u043C\u044F Wi-Fi \u0441\u0435\u0442\u0438 (SSID)</label>"
           "<input type='text' name='wifi_ssid' value='");
    p += htmlAttr(_cfg->cfg.wifi_ssid);
    p += F("' placeholder='\u041D\u0430\u0437\u0432\u0430\u043D\u0438\u0435 \u0432\u0430\u0448\u0435\u0439 \u0441\u0435\u0442\u0438' autocomplete='off' autocorrect='off' autocapitalize='none'>"
           "<label>\u041F\u0430\u0440\u043E\u043B\u044C</label>"
           "<div class='eye'>"
           "<input type='password' id='wp' name='wifi_pass' value=''"
           " autocomplete='new-password'");
    // Show how many chars are saved so user knows to retype
    if (strlen(_cfg->cfg.wifi_pass) > 0) {
        p += F(" placeholder='\u0421\u043E\u0445\u0440\u0430\u043D\u0451\u043D (\u043E\u0441\u0442\u0430\u0432\u044C\u0442\u0435 \u043F\u0443\u0441\u0442\u044B\u043C \u2014 \u043D\u0435 \u043C\u0435\u043D\u044F\u0442\u044C)'");
    } else {
        p += F(" placeholder='\u041F\u0430\u0440\u043E\u043B\u044C Wi-Fi'");
    }
    p += F(">"
           "<button type='button' onclick=\"var i=document.getElementById('wp');"
           "i.type=i.type==='password'?'text':'password'\"></button>"
           "</div>"
           "<button type='submit'>\u0421\u043E\u0445\u0440\u0430\u043D\u0438\u0442\u044C \u0438 \u043F\u043E\u0434\u043A\u043B\u044E\u0447\u0438\u0442\u044C</button>"
           "</form>"
           "<a class='full' href='/'>\u041E\u0442\u043A\u0440\u044B\u0442\u044C \u0438\u043D\u0442\u0435\u0440\u0444\u0435\u0439\u0441</a>"
           "</div></body></html>");

    _server.send(200, "text/html", p);
}

// ================================================================
//                       FORM HANDLERS
// ================================================================

// ================================================================
//  STATUS PAGE helper — bilingual reboot/save confirmation
// ================================================================

String WebPortal::statusPage(const char* icon,
                             const char* headRu, const char* headEn,
                             const char* bodyHtml,
                             const char* redirect, int delaySec) {
    String p;
    p.reserve(2200);
    p += F("<!DOCTYPE html><html lang='ru'><head>"
           "<meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>ESP-HUB</title>"
           "<style>"
           ":root{--bg:#0f1117;--bg2:#161b22;--brd:#30363d;--txt:#e1e4e8;"
           "--txt2:#8b949e;--acc:#58a6ff}"
           ":root.light{--bg:#f6f8fa;--bg2:#ffffff;--brd:#d0d7de;--txt:#1f2328;"
           "--txt2:#656d76;--acc:#0969da}"
           "body{background:var(--bg);color:var(--txt);text-align:center;"
           "padding:60px 20px;font-family:-apple-system,sans-serif;margin:0}"
           ".box{display:inline-block;background:var(--bg2);border:1px solid var(--brd);"
           "border-radius:12px;padding:28px 44px;margin-top:20px;max-width:480px}"
           "a{color:var(--acc)} h2{margin-top:0}"
           ".muted{color:var(--txt2);font-size:13px}"
           ".tb{position:fixed;top:12px;right:14px;display:flex;gap:6px}"
           ".nt{background:var(--bg2);border:1px solid var(--brd);color:var(--txt);"
           "border-radius:6px;padding:3px 10px;cursor:pointer;font-size:13px}"
           "</style></head><body>");
    // theme + lang buttons
    p += F("<div class='tb'>"
           "<button class='nt' onclick='tL()' id='lb'></button>"
           "<button class='nt' onclick='tT()' id='tb2'></button>"
           "</div>");
    // content
    p += F("<div style='font-size:48px'>");
    p += icon;
    p += F("</div><h2 id='hd'></h2><div class='box'>");
    p += bodyHtml;
    p += F("</div>");
    // countdown
    p += F("<p class='muted' id='ct'></p>");
    p += F("<p><a href='/'>&#8592; ");
    p += F("\u0413\u043b\u0430\u0432\u043d\u0430\u044f"); // Главная
    p += F("</a></p>");

    // script: theme + lang + countdown
    p += F("<script>"
           "var L=localStorage,lang=L.getItem('lang')||'ru',"
           "theme=L.getItem('theme')||'dark';"
           "var RU='");
    p += headRu;
    p += F("',EN='");
    p += headEn;
    p += F("';"
           "function apT(){if(theme==='light')document.documentElement.classList.add('light');"
           "else document.documentElement.classList.remove('light');"
           "document.getElementById('tb2').textContent=theme==='dark'?'Light':'Dark';}"
           "function apL(){document.getElementById('hd').textContent=lang==='ru'?RU:EN;"
           "document.getElementById('lb').textContent=lang==='ru'?'EN':'RU';}"
           "function tT(){theme=theme==='dark'?'light':'dark';L.setItem('theme',theme);apT();}"
           "function tL(){lang=lang==='ru'?'en':'ru';L.setItem('lang',lang);apL();}"
           "apT();apL();");
    if (delaySec > 0) {
        p += F("var s=");
        p += delaySec;
        p += F(",ct=document.getElementById('ct');"
               "var iv=setInterval(function(){"
               "ct.textContent=(lang==='ru'?"
               "'\\u0237\\u0435\\u0440\\u0435\\u0437\\u0430\\u0433\\u0440\\u0443\\u0437\\u043a\\u0430 \\u0447\\u0435\\u0440\\u0435\\u0437 ':"
               "'Redirecting in ')+s+'s...';"
               "if(--s<0){clearInterval(iv);location.href='");
        p += redirect;
        p += F("';}},1000);");
    }
    p += F("</script></body></html>");
    return p;
}

void WebPortal::handleSaveWifi() {
    String ssid = _server.arg("wifi_ssid");
    String pass = _server.arg("wifi_pass");
    ssid.trim();
    Serial.printf("[WEB] Saving WiFi: ssid='%s' ssid_len=%d pass_len=%d\n",
                    ssid.c_str(), ssid.length(), pass.length());

    if (ssid.length() > 0) {
        strlcpy(_cfg->cfg.wifi_ssid, ssid.c_str(), sizeof(_cfg->cfg.wifi_ssid));
        // Only overwrite password if user actually typed something
        if (pass.length() > 0) {
            pass.trim();
            strlcpy(_cfg->cfg.wifi_pass, pass.c_str(), sizeof(_cfg->cfg.wifi_pass));
            int plen = strlen(_cfg->cfg.wifi_pass);
            Serial.printf("[WEB] Password updated (len=%d) hex: ", plen);
            for (int i = 0; i < plen; i++) Serial.printf("%02X ", (uint8_t)_cfg->cfg.wifi_pass[i]);
            Serial.println();
        } else {
            Serial.printf("[WEB] Password empty — keeping existing (len=%d)\n", (int)strlen(_cfg->cfg.wifi_pass));
        }
        bool ok = _cfg->save();
        Serial.printf("[WEB] Config save: %s  ssid='%s' pass_len=%d\n",
                      ok ? "OK" : "FAILED",
                      _cfg->cfg.wifi_ssid,
                      (int)strlen(_cfg->cfg.wifi_pass));
    } else {
        Serial.println(F("[WEB] Empty SSID — skipping save"));
    }

    // Minimal confirmation page — Russian
    String pg;
    pg.reserve(700);
    pg += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>\xD0\xA1\xD0\xBE\xD1\x85\xD1\x80\xD0\xB0\xD0\xBD\xD0\xB5\xD0\xBD\xD0\xBE</title>"
            "<style>body{background:#0f1117;color:#e1e4e8;font-family:sans-serif;"
            "display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0}"
            ".box{background:#161b22;border:1px solid #30363d;border-radius:12px;"
            "padding:28px 24px;text-align:center;max-width:380px}"
            "a{color:#58a6ff}</style></head><body><div class='box'>"
            "<div style='font-size:52px'></div>"
            "<h2 style='margin:12px 0 8px'>Wi-Fi \xD1\x81\xD0\xBE\xD1\x85\xD1\x80\xD0\xB0\xD0\xBD\xD1\x91\xD0\xBD!</h2>"
            "<p style='color:#8b949e;margin:0 0 4px'>\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 \xD0\xBA:</p>"
            "<b style='color:#58a6ff;font-size:18px'>");
    pg += htmlAttr((ssid.length() > 0 ? ssid : String(_cfg->cfg.wifi_ssid)).c_str());
    pg += F("</b>"
            "<p style='color:#8b949e;font-size:13px;margin-top:16px'>"
            "\xD0\x9F\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB7\xD0\xB0\xD0\xB3\xD1\x80\xD1\x83\xD0\xB7\xD0\xBA\xD0\xB0&hellip; \xD0\x9F\xD0\xBE\xD1\x81\xD0\xBB\xD0\xB5 \xD0\xBF\xD0\xBE\xD0\xB4\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F \xD0\xBE\xD1\x82\xD0\xBA\xD1\x80\xD0\xBE\xD0\xB9\xD1\x82\xD0\xB5:<br>"
            "<a href='http://esp-hub.local/'>http://esp-hub.local/</a><br>"
            "<span style='font-size:12px'>\xD0\xB8\xD0\xBB\xD0\xB8 \xD0\xBD\xD0\xB0\xD0\xB9\xD0\xB4\xD0\xB8\xD1\x82\xD0\xB5 IP \xD0\xB2 DHCP-\xD1\x82\xD0\xB0\xD0\xB1\xD0\xBB\xD0\xB8\xD1\x86\xD0\xB5 \xD1\x80\xD0\xBE\xD1\x83\xD1\x82\xD0\xB5\xD1\x80\xD0\xB0</span></p>"
            "</div></body></html>");

    _server.send(200, "text/html", pg);
    delay(1000);
    ESP.restart();
}

void WebPortal::handleSaveAp() {
    if (_server.hasArg("ap_ssid"))
        strlcpy(_cfg->cfg.ap_ssid, _server.arg("ap_ssid").c_str(), sizeof(_cfg->cfg.ap_ssid));
    if (_server.hasArg("ap_pass")) {
        // enforce minimum 8 chars for WPA2
        String p = _server.arg("ap_pass");
        if (p.length() >= 8 || p.length() == 0) // allow empty
            strlcpy(_cfg->cfg.ap_pass, p.c_str(), sizeof(_cfg->cfg.ap_pass));
    }
    _cfg->cfg.ap_nat = _server.hasArg("ap_nat") && _server.arg("ap_nat") == "on";
    if (_server.hasArg("device_name"))
        strlcpy(_cfg->cfg.device_name, _server.arg("device_name").c_str(), sizeof(_cfg->cfg.device_name));
    _cfg->save();
    _server.send(200, "text/html",
        F("<html><head><meta charset='utf-8'></head>"
          "<body style='background:#0f1117;color:#e1e4e8;text-align:center;padding:50px;font-family:sans-serif'>"
          "<h2>Hotspot settings saved! Rebooting...</h2>"
          "<script>setTimeout(()=>location.href='/',8000)</script>"
          "</body></html>"));
    delay(500);
    ESP.restart();
}

void WebPortal::handleSaveMqtt() {
    if (_server.hasArg("mqtt_host"))
        strlcpy(_cfg->cfg.mqtt_host, _server.arg("mqtt_host").c_str(), sizeof(_cfg->cfg.mqtt_host));
    if (_server.hasArg("mqtt_port"))
        _cfg->cfg.mqtt_port = _server.arg("mqtt_port").toInt();
    if (_server.hasArg("mqtt_user"))
        strlcpy(_cfg->cfg.mqtt_user, _server.arg("mqtt_user").c_str(), sizeof(_cfg->cfg.mqtt_user));
    if (_server.hasArg("mqtt_pass"))
        strlcpy(_cfg->cfg.mqtt_pass, _server.arg("mqtt_pass").c_str(), sizeof(_cfg->cfg.mqtt_pass));
    if (_server.hasArg("mqtt_topic"))
        strlcpy(_cfg->cfg.mqtt_topic, _server.arg("mqtt_topic").c_str(), sizeof(_cfg->cfg.mqtt_topic));
    if (_server.hasArg("mqtt_interval"))
        _cfg->cfg.mqtt_interval_s = _server.arg("mqtt_interval").toInt();

    _cfg->save();
    _server.sendHeader("Location", "/mqtt");
    _server.send(303, "text/plain", "Redirecting...");
}

void WebPortal::handleSaveSensors() {
    if (_server.hasArg("sensor_interval"))
        _cfg->cfg.sensor_interval_s = constrain(_server.arg("sensor_interval").toInt(), 1, 3600);

    for (int i = 0; i < MAX_SENSORS; i++) {
        String si = String(i);
        SensorConfig& sc = _cfg->cfg.sensors[i];

        sc.enabled  = _server.hasArg("en_" + si);
        if (_server.hasArg("type_" + si))
            sc.type = sensorTypeFromIndex(_server.arg("type_" + si).toInt());
        if (_server.hasArg("bus_" + si))
            sc.bus  = (BusType)constrain(_server.arg("bus_" + si).toInt(), 0, BUS_TYPE_COUNT-1);
        if (_server.hasArg("out_" + si))
            sc.outProto = (OutProtocol)constrain(_server.arg("out_" + si).toInt(), 0, OUT_PROTOCOL_COUNT-1);
        if (_server.hasArg("pin_" + si))
            sc.pin  = _server.arg("pin_" + si).toInt();
        if (_server.hasArg("pin2_" + si))
            sc.pin2 = _server.arg("pin2_" + si).toInt();
        if (_server.hasArg("uart_" + si))
            sc.uartNum = constrain(_server.arg("uart_" + si).toInt(), 1, 2);
        if (_server.hasArg("cid_" + si))
            sc.canId  = strtoul(_server.arg("cid_" + si).c_str(), nullptr, 0); // auto hex/dec
        if (_server.hasArg("cdlc_" + si))
            sc.canDlc = constrain(_server.arg("cdlc_" + si).toInt(), 1, 8);
        if (_server.hasArg("baud_" + si))
            sc.baud = (uint32_t)constrain(_server.arg("baud_" + si).toInt(), 1200, 921600);
        if (_server.hasArg("hurl_" + si))
            strlcpy(sc.httpUrl, _server.arg("hurl_" + si).c_str(), sizeof(sc.httpUrl));
        if (_server.hasArg("label_" + si))
            strlcpy(sc.label, _server.arg("label_" + si).c_str(), sizeof(sc.label));
    }
    yield();
    _cfg->save();
    _sensors->begin(_cfg->cfg);

    _server.send(200, "application/json", F("{\"ok\":true}"));
}

void WebPortal::handleSaveSystem() {
    // Обработка сохранения системных настроек из веб-интерфейса
    bool changed = false;
    bool ai_changed = false;
    
    // Проверка состояния AI агента
    bool prev_ai_enabled = _cfg->cfg.ai_enabled;
    _cfg->cfg.ai_enabled = _server.hasArg("ai_en");
    if (_cfg->cfg.ai_enabled != prev_ai_enabled) {
        ai_changed = true;
        changed = true;
        Serial.printf("[WEB] ИИ Агент %s\n", _cfg->cfg.ai_enabled ? "включен" : "отключен");
    }
    
    // Сохранение частоты процессора (80 / 160 / 240 МГц)
    if (_server.hasArg("cpu_freq")) {
        uint16_t freq = (uint16_t)_server.arg("cpu_freq").toInt();
        if (freq == 80 || freq == 160 || freq == 240) {
            _cfg->cfg.cpu_freq_mhz = freq;
            changed = true;
            Serial.printf("[WEB] Частота CPU установлена на %d МГц\n", freq);
        }
    }
    
    // Сохранение скорости Serial (UART порта для отладки)
    if (_server.hasArg("serial_baud")) {
        uint32_t baud = (uint32_t)_server.arg("serial_baud").toInt();
        if (baud > 0) {
            _cfg->cfg.serial_baud = baud;
            changed = true;
            Serial.printf("[WEB] Скорость Serial установлена на %u baud\n", baud);
        }
    }
    
    // Если что-то изменилось - сохраняем в LittleFS и перезагружаемся
    if (changed) {
        _cfg->save();
        if (ai_changed && _cfg->cfg.ai_enabled) {
            aiAgent.notifyEnabled();  // notify via Telegram if enabled
        }
    }
    _server.send(200, "text/html",
        F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'>"
          "<title>OK</title>"
          "<style>body{background:#0f1117;color:#e1e4e8;text-align:center;"
          "padding:60px 20px;font-family:sans-serif}</style></head><body>"
          "<div style='font-size:48px'></div>"
          "<h2>Настройки сохранены. Перезагрузка...</h2>"
          "</body></html>"));
    delay(500);
    ESP.restart();
}

// ================================================================
//                       GPIO API
// ================================================================

// Configured pin modes: 0=unset 1=in 2=pullup 3=pulldown 4=out
static uint8_t s_gpioMode[49] = {0};

void WebPortal::handleGpioApi() {
    bool isS3 = String(ESP.getChipModel()).indexOf("S3") >= 0;
    const GpioInfo* tbl = isS3 ? kGpioS3 : kGpioESP32;
    int cnt = isS3 ? (int)(sizeof(kGpioS3)/sizeof(kGpioS3[0]))
                   : (int)(sizeof(kGpioESP32)/sizeof(kGpioESP32[0]));

    String json;
    json.reserve(3200);
    json += F("{\"chip\":\"");
    json += ESP.getChipModel();
    json += F("\",\"count\":");
    json += cnt;
    json += F(",\"pins\":[");
    for (int i = 0; i < cnt; i++) {
        const GpioInfo& g = tbl[i];
        uint8_t m = (g.pin < 49) ? s_gpioMode[g.pin] : 0;
        const char* d = (m==1?"in": m==2?"pu": m==3?"pd": m==4?"out":"unset");
        int lvl = (g.w < 2 && m > 0) ? digitalRead(g.pin) : 0;
        if (i) json += ',';
        json += F("{\"n\":");  json += g.pin;
        json += F(",\"note\":\""); json += g.note; json += '"';
        json += F(",\"w\":"); json += g.w;
        json += F(",\"dir\":\""); json += d; json += '"';
        json += F(",\"lvl\":"); json += lvl;
        json += '}';
    }
    json += F("]}");
    _server.send(200, "application/json", json);
}

void WebPortal::handleGpioSet() {
    if (!_server.hasArg("pin") || !_server.hasArg("mode")) {
        _server.send(400, "application/json",
                     F("{\"ok\":false,\"err\":\"pin and mode required\"}"));
        return;
    }
    int pin = _server.arg("pin").toInt();
    String mode = _server.arg("mode");
    int val = _server.hasArg("val") ? _server.arg("val").toInt() : -1;

    if (pin < 0 || pin > 48) {
        _server.send(400, "application/json",
                     F("{\"ok\":false,\"err\":\"invalid pin (0-48)\"}"));
        return;
    }
    uint8_t mCode = 0;
    if      (mode == "in")    { pinMode(pin, INPUT);          mCode = 1; }
    else if (mode == "pu")    { pinMode(pin, INPUT_PULLUP);   mCode = 2; }
    else if (mode == "pd")    { pinMode(pin, INPUT_PULLDOWN); mCode = 3; }
    else if (mode == "out")   { pinMode(pin, OUTPUT);         mCode = 4; }
    else if (mode == "unset") {
        // Release pin back to floating input (default unset state)
        pinMode(pin, INPUT);
        mCode = 0;  // marks as unset in our table
    }
    if (mCode > 0) s_gpioMode[pin] = mCode;
    else            s_gpioMode[pin] = 0;  // reset to unset for "unset" mode

    if (val >= 0 && mode != "unset") {
        if (s_gpioMode[pin] != 4) { pinMode(pin, OUTPUT); s_gpioMode[pin] = 4; }
        digitalWrite(pin, val ? HIGH : LOW);
    }
    uint8_t m = s_gpioMode[pin];
    const char* d = (m==1?"in": m==2?"pu": m==3?"pd": m==4?"out":"unset");
    int lvl = digitalRead(pin);
    String resp = F("{\"ok\":true,\"n\":");
    resp += pin;
    resp += F(",\"dir\":\""); resp += d; resp += F("\",\"lvl\":"); resp += lvl; resp += '}';
    _server.send(200, "application/json", resp);
}

// ----------------------------------------------------------------
//  GET /api/gpio/timers  — JSON status + countdown for each timer
// ----------------------------------------------------------------
void WebPortal::handleGpioTimersApi() {
    static const char* actName[] = {"HIGH","LOW","TOGGLE","PULSE_HIGH","PULSE_LOW"};
    String json;
    json.reserve(1200);
    json += F("{\"timers\":[");
    for (int i = 0; i < MAX_GPIO_TIMERS; i++) {
        const GpioTimerConfig& t = _cfg->cfg.gpio_timers[i];
        if (i) json += ',';
        json += F("{\"i\":"); json += i;
        json += F(",\"en\":"); json += (t.enabled ? "true" : "false");
        json += F(",\"pin\":"); json += t.pin;
        json += F(",\"act\":"); json += (uint8_t)t.action;
        json += F(",\"act_name\":\""); json += actName[min((int)t.action, 4)]; json += '"';
        json += F(",\"h\":"); json += t.hours;
        json += F(",\"m\":"); json += t.minutes;
        json += F(",\"s\":"); json += t.seconds;
        json += F(",\"dur\":"); json += t.duration_ms;
        json += F(",\"inv\":"); json += (t.active_low ? "true" : "false");
        json += F(",\"lbl\":\"");
        // escape label
        for (int c = 0; t.label[c]; c++) {
            if (t.label[c]=='"'||t.label[c]=='\\') json += '\\';
            json += t.label[c];
        }
        json += '"';
        json += F(",\"next_ms\":"); json += gpioSched.timeToNext(i);
        json += '}';
    }
    json += F("]}");
    _server.send(200, "application/json", json);
}

// ----------------------------------------------------------------
//  POST /save/gpio-timers  — save all 8 timer configs
// ----------------------------------------------------------------
void WebPortal::handleSaveGpioTimers() {
    for (int i = 0; i < MAX_GPIO_TIMERS; i++) {
        String si = String(i);
        GpioTimerConfig& t = _cfg->cfg.gpio_timers[i];
        t.enabled = _server.hasArg("ten_" + si);
        if (_server.hasArg("tpin_" + si))
            t.pin = (uint8_t)constrain(_server.arg("tpin_" + si).toInt(), 0, 48);
        if (_server.hasArg("tact_" + si))
            t.action = (GpioTimerAction)constrain(_server.arg("tact_" + si).toInt(), 0, TIMER_ACTION_COUNT-1);
        if (_server.hasArg("th_" + si))
            t.hours   = (uint8_t)constrain(_server.arg("th_" + si).toInt(), 0, 23);
        if (_server.hasArg("tm_" + si))
            t.minutes = (uint8_t)constrain(_server.arg("tm_" + si).toInt(), 0, 59);
        if (_server.hasArg("ts_" + si))
            t.seconds = (uint8_t)constrain(_server.arg("ts_" + si).toInt(), 0, 59);
        if (_server.hasArg("tdur_" + si))
            t.duration_ms = (uint16_t)constrain(_server.arg("tdur_" + si).toInt(), 1, 60000);
        if (_server.hasArg("tlbl_" + si))
            strlcpy(t.label, _server.arg("tlbl_" + si).c_str(), sizeof(t.label));
        // inverted relay flag (active LOW)
        t.active_low = _server.hasArg("tinv_" + si);
    }
    yield();           // let WiFi stack run before blocking flash write
    _cfg->save();
    gpioSched.reload();
    _server.send(200, "application/json", F("{\"ok\":true}"));
}

void WebPortal::handleResetWifi() {
    Serial.println(F("[WEB] WiFi credentials reset requested"));
    _cfg->cfg.wifi_ssid[0] = '\0';
    _cfg->cfg.wifi_pass[0] = '\0';
    _cfg->save();
    _server.send(200, "text/html",
        F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'>"
          "<title>Wi-Fi reset</title>"
          "<style>body{background:#0f1117;color:#e1e4e8;text-align:center;"
          "padding:60px 20px;font-family:sans-serif}a{color:#58a6ff}</style>"
          "</head><body>"
          "<div style='font-size:48px'></div>"
          "<h2>\u041A\u0440\u0435\u0434\u0435\u043D\u0446\u0438\u0430\u043B\u044B Wi-Fi \u0441\u0431\u0440\u043E\u0448\u0435\u043D\u044B</h2>"
          "<p style='color:#8b949e'>\u041F\u0435\u0440\u0435\u0437\u0430\u0433\u0440\u0443\u0437\u043A\u0430... \u041F\u043E\u0434\u043A\u043B\u044E\u0447\u0438\u0442\u0435\u0441\u044C \u043A AP <b>ESP-HUB</b><br>"
          "\u0438 \u043E\u0442\u043A\u0440\u043E\u0439\u0442\u0435 <a href='http://192.168.4.1/wifi'>http://192.168.4.1/wifi</a></p>"
          "</body></html>"));
    delay(800);
    ESP.restart();
}

void WebPortal::handleReboot() {
    _server.send(200, "text/html",
        F("<html><body style='background:#0f1117;color:#e1e4e8;text-align:center;padding:50px;font-family:sans-serif'>"
          "<h2>Rebooting...</h2>"
          "<script>setTimeout(()=>location.href='/',5000)</script>"
          "</body></html>"));
    delay(500);
    ESP.restart();
}

void WebPortal::handleReset() {
    _cfg->resetDefaults();
    _cfg->save();
    _server.send(200, "text/html",
        F("<html><body style='background:#0f1117;color:#e1e4e8;text-align:center;padding:50px;font-family:sans-serif'>"
          "<h2>Factory reset complete! Rebooting...</h2>"
          "<script>setTimeout(()=>location.href='/',8000)</script>"
          "</body></html>"));
    delay(500);
    ESP.restart();
}

// ================================================================
//                       API: WIFI SCAN
// ================================================================

void WebPortal::handleApiScan() {
    int n = WiFi.scanNetworks(false, true); // async=false, show_hidden=true
    String json;
    json.reserve(1024);
    json += "[";
    for (int i = 0; i < n && i < 20; i++) {
        if (i > 0) json += ',';
        String ssid = WiFi.SSID(i);
        ssid.replace("\\", "\\\\");
        ssid.replace("\"", "\\\"");
        json += F("{\"ssid\":\"");
        json += ssid;
        json += F("\",\"rssi\":");
        json += WiFi.RSSI(i);
        json += F(",\"enc\":");
        json += (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ? F("true") : F("false");
        json += '}';
    }
    json += ']';
    WiFi.scanDelete();
    _server.send(200, "application/json", json);
}

// ================================================================
// ================================================================
//                        ESP-CAM PAGE
// ================================================================

void WebPortal::handleCamera() {
    String camBase = String(_cfg->cfg.cam_url);
    bool hasCam = camBase.length() > 0;
    uint8_t recDev = _cfg->cfg.cam_record_dev;

    // ── Stream card ──────────────────────────────────────────────
    String st;
    st.reserve(2400);
    st += F("<div id='cam-container' style='position:relative;background:#000;"
            "border-radius:8px;overflow:hidden;min-height:200px;margin-bottom:12px'>");
    if (hasCam) {
        st += F("<img id='cam-stream' "
                "style='width:100%;display:block;max-height:480px;object-fit:contain' "
                "alt='Загрузка...' />");
    } else {
        st += F("<div style='color:#8b949e;padding:40px;text-align:center'>"
                "&#128247; Настройте URL камеры ниже &darr;</div>");
    }
    st += F("</div>");

    // Controls row
    st += F("<div style='display:flex;justify-content:space-between;flex-wrap:wrap;"
            "align-items:center;gap:8px;margin-bottom:12px'>");
    st += F("<div style='display:flex;gap:8px'>");
    if (hasCam) {
        st += F("<button class='btn btn-primary' onclick='startStream()'>&#9654; Трансляция</button>");
        st += F("<button class='btn btn-secondary' onclick='stopStream()'>&#9209; Стоп</button>");
        st += F("<button class='btn btn-secondary' onclick='takeSnapshot()'>&#128247; Снимок</button>");
        st += F("<button class='btn btn-secondary' id='flash-btn' style='opacity:.5' "
                "onclick='toggleFlash()'>&#9889; Вспышка</button>");
    }
    st += F("</div>");
    st += F("<span id='cam-status' class='badge badge-gray'>&#8226; Не подключено</span>");
    st += F("</div>");

    if (hasCam) {
        // Resolution + quality
        st += F("<div class='grid2' style='gap:12px;margin-bottom:12px'>");
        st += F("<div><label style='font-size:13px'>Разрешение</label>"
                "<select id='cam-fs' class='input-field' onchange=\"camCtrl('framesize',this.value)\">"
                "<option value='0'>96x96</option>"
                "<option value='1'>QQVGA 160x120</option>"
                "<option value='3'>HQVGA 240x176</option>"
                "<option value='5' selected>QVGA 320x240</option>"
                "<option value='6'>CIF 400x296</option>"
                "<option value='7'>HVGA 480x320</option>"
                "<option value='8'>VGA 640x480</option>"
                "<option value='9'>SVGA 800x600</option>"
                "<option value='10'>XGA 1024x768</option>"
                "<option value='11'>HD 1280x720</option>"
                "<option value='12'>SXGA 1280x1024</option>"
                "<option value='13'>UXGA 1600x1200</option>"
                "</select></div>");
        st += F("<div><label style='font-size:13px'>Качество JPEG (4=лучше, 63=хуже)</label>"
                "<div style='display:flex;align-items:center;gap:8px'>"
                "<input type='range' id='cam-q' min='4' max='63' value='12' style='flex:1'"
                " oninput=\"document.getElementById('cam-qv').textContent=this.value\">"
                "<span id='cam-qv' style='min-width:24px'>12</span>"
                "<button class='btn btn-secondary' style='padding:4px 10px'"
                " onclick=\"camCtrl('quality',document.getElementById('cam-q').value)\">OK</button>"
                "</div></div>");
        st += F("</div>"); // grid2

        // Flip/mirror
        st += F("<div class='grid2' style='gap:12px'>");
        st += F("<div style='display:flex;align-items:center;gap:8px'>"
                "<input type='checkbox' id='hmirror' "
                " onchange=\"camCtrl('hmirror',this.checked?1:0)\">"
                "<label for='hmirror'>Горизонтальное зеркало</label></div>");
        st += F("<div style='display:flex;align-items:center;gap:8px'>"
                "<input type='checkbox' id='vflip' "
                " onchange=\"camCtrl('vflip',this.checked?1:0)\">"
                "<label for='vflip'>Вертикальное зеркало</label></div>");
        st += F("</div>");
    }

    startPage("ESP-CAM");
    _server.sendContent(card("\xF0\x9F\x93\xB7 Трансляция ESP32-CAM", st));

    // ── Settings form ────────────────────────────────────────────
    String sf;
    sf.reserve(700);
    sf += F("<form method='POST' action='/save/camera'>");
    sf += inputField("URL камеры (ESP32-CAM)", "cam_url", _cfg->cfg.cam_url,
                     "text", "http://192.168.1.100");
    sf += F("<p class='text-muted' style='margin:-8px 0 12px'>"
            "Только базовый URL без слеша в конце, например: http://192.168.1.100</p>");
    const char* recOpts[] = { "Не записывать", "SD Card (на ESP32-CAM)", "MQTT (JPEG base64)" };
    sf += selectField("Устройство записи", "cam_rec", recOpts, 3, recDev);
    sf += submitButton("Сохранить");
    sf += F("</form>");
    _server.sendContent(card("\xE2\x9A\x99\xEF\xB8\x8F Настройка", sf));

    // ── Help card ─────────────────────────────────────────────────
    String help;
    help += F("<table>");
    help += F("<tr><td><code>/stream</code></td><td>Поток MJPEG</td></tr>");
    help += F("<tr><td><code>/capture</code></td><td>Одиночный JPEG снимок</td></tr>");
    help += F("<tr><td><code>/status</code></td><td>Статус камеры (JSON)</td></tr>");
    help += F("<tr><td><code>/control?var=&amp;val=</code></td><td>Управление параметрами</td></tr>");
    help += F("</table>");
    help += F("<div style='margin-top:8px;line-height:1.8'>"
              "<b>Параметры управления:</b><br>"
              "<code>framesize</code> 0-13 &nbsp; <code>quality</code> 4-63 &nbsp; "
              "<code>brightness</code> -2..2 &nbsp; <code>contrast</code> -2..2 &nbsp; "
              "<code>hmirror</code> 0/1 &nbsp; <code>vflip</code> 0/1 &nbsp; "
              "<code>flash</code> 0/1"
              "</div>");
    _server.sendContent(card("\xF0\x9F\x93\x96 ESP32-CAM API", help));

    endPage();

    // ── Inline JS ────────────────────────────────────────────────
    String sc;
    sc.reserve(1100);
    sc += F("<script>var CAM_URL='");
    sc += camBase;
    sc += F("';"
            "var _flashOn=false;"
            "function startStream(){"
            "  if(!CAM_URL){alert('URL камеры не задан');return;}"
            "  var el=document.getElementById('cam-stream');"
            "  if(!el)return;"
            "  document.getElementById('cam-status').className='badge badge-gray';"
            "  document.getElementById('cam-status').textContent='\\u2022 \\u041f\\u043e\\u0434\\u043a\\u043b\\u044e\\u0447\\u0435\\u043d\\u0438\\u0435...';"
            "  el.src=CAM_URL+'/stream?_='+Date.now();"
            "  el.onerror=function(){document.getElementById('cam-status').className='badge badge-red';"
            "    document.getElementById('cam-status').textContent='\\u2022 \\u041e\\u0448\\u0438\\u0431\\u043a\\u0430';};"
            "}"
            "function stopStream(){"
            "  var el=document.getElementById('cam-stream');if(el)el.src='';"
            "  document.getElementById('cam-status').className='badge badge-gray';"
            "  document.getElementById('cam-status').textContent='\\u2022 \\u041e\\u0441\\u0442\\u0430\\u043d\\u043e\\u0432\\u043b\\u0435\\u043d\\u043e';"
            "}"
            "function takeSnapshot(){"
            "  if(!CAM_URL){alert('URL камеры не задан');return;}"
            "  window.open(CAM_URL+'/capture','_blank');"
            "}"
            "function toggleFlash(){"
            "  _flashOn=!_flashOn;"
            "  camCtrl('flash',_flashOn?1:0);"
            "  document.getElementById('flash-btn').style.opacity=_flashOn?'1':'0.5';"
            "}"
            "function camCtrl(v,val){"
            "  fetch('/api/camera/relay?path=/control%3Fvar%3D'+v+'%26val%3D'+val)"
            "  .then(function(r){return r.json();})"
            "  .then(function(d){if(d.error)console.warn('cam:',d.error);});"
            "}");
    if (hasCam) {
        sc += F("window.addEventListener('load',function(){startStream();});");
    }
    sc += F("</script>");
    _server.sendContent(sc);
}

// ── Proxy a request to the ESP32-CAM (avoids CORS) ───────────────
void WebPortal::handleApiCameraRelay() {
    if (_cfg->cfg.cam_url[0] == '\0') {
        _server.send(503, "application/json", "{\"error\":\"no_cam_url\"}");
        return;
    }
    String path = _server.hasArg("path") ? _server.arg("path") : "/status";
    // Allow only safe ESP32-CAM paths
    if (!path.startsWith("/control") && !path.startsWith("/status") && !path.startsWith("/capture")) {
        _server.send(400, "application/json", "{\"error\":\"invalid_path\"}");
        return;
    }
    String url = String(_cfg->cfg.cam_url) + path;
    HTTPClient http;
    http.begin(url);
    http.setTimeout(3000);
    int code = http.GET();
    if (code > 0) {
        String body = http.getString();
        _server.send(200, "application/json",
                     body.length() > 0 ? body : "{\"ok\":true}");
    } else {
        _server.send(502, "application/json", "{\"error\":\"cam_unreachable\"}");
    }
    http.end();
}

// ── Save camera settings ──────────────────────────────────────────
void WebPortal::handleSaveCamera() {
    if (_server.method() != HTTP_POST) { _server.send(405); return; }
    String url = _server.hasArg("cam_url") ? _server.arg("cam_url") : "";
    url.trim();
    while (url.endsWith("/")) url.remove(url.length() - 1);
    strlcpy(_cfg->cfg.cam_url, url.c_str(), sizeof(_cfg->cfg.cam_url));
    _cfg->cfg.cam_record_dev =
        (uint8_t)(_server.hasArg("cam_rec") ? _server.arg("cam_rec").toInt() : 0);
    _cfg->save();
    _server.sendHeader("Location", "/camera");
    _server.send(302);
}

// ================================================================
//                        AI AGENT PAGE
// ================================================================

void WebPortal::handleAI() {
    // Default models per provider (shown as placeholder when model field is empty)
    // Index matches provider ID: 0=LM Studio, 1=Ollama, 2=OpenAI, 3=OpenRouter, 4=Anthropic
    static const char* provDefUrls[] = {
        "http://192.168.1.125:1234/v1/chat/completions",
        "http://127.0.0.1:11434/v1/chat/completions",
        "https://api.openai.com/v1/chat/completions",
        "https://openrouter.ai/api/v1/chat/completions",
        "https://api.anthropic.com/v1/messages"
    };
    static const char* provDefModels[] = {
        "qwen/qwen3-vl-8b",
        "qwen3:8b",
        "gpt-4o-mini",
        "qwen/qwen3-coder:free",
        "claude-3-5-haiku-20241022"
    };
    static const char* provNames[] = {
        "LM Studio (local)", "Ollama (local)",
        "OpenAI", "OpenRouter", "Anthropic"
    };

    uint8_t prov = _cfg->cfg.ai_provider;

    // ── Card 1: Basic settings ────────────────────────────────────
    String sf;
    sf += F("<form method='POST' action='/save/ai'>");

    // Enable toggle
    sf += F("<div class='row-between' style='margin-bottom:14px'>"
            "<b>ИИ Агент</b>"
            "<label class='toggle'>"
            "<input type='checkbox' name='ai_en' value='1' onchange='if(this.checked!==");
    sf += (_cfg->cfg.ai_enabled ? '1' : '0');
    sf += F("){if(confirm(\"Изменение статуса требует перезагрузки. Продолжить?\")){var f=this.closest(\"form\");var inp=document.createElement(\"input\");inp.type=\"hidden\";inp.name=\"sys_reboot\";inp.value=\"1\";f.appendChild(inp);f.submit();} else {this.checked=");
    sf += (_cfg->cfg.ai_enabled ? '1' : '0');
    sf += F(";}}'");
    if (_cfg->cfg.ai_enabled) sf += F(" checked");
    sf += F("><span class='slider'></span></label></div>");

    // Provider
    sf += F("<label>Провайдер</label><select name='ai_prov' id='ai-prov' class='input-field' "
            "onchange='aiProvChange()'>");
    for (int i = 0; i < 5; i++) {
        sf += F("<option value='"); sf += i; sf += "'";
        if (prov == i) sf += F(" selected");
        sf += ">"; sf += provNames[i]; sf += F("</option>");
    }
    sf += F("</select>");

    // LM Studio-only: dedicated base URL for model browser + API
    sf += F("<div id='lms-url-row'");
    if (prov != 0) sf += F(" style='display:none'");
    sf += F("><label>LM Studio URL "
            "<span style='font-weight:normal;font-size:0.82em;color:var(--muted)'>"
            "(адрес сервера, напр. http://192.168.1.125:1234)</span></label>"
            "<input type='text' name='ai_lms' id='ai-lms-url' class='input-field'"
            " placeholder='http://192.168.1.125:1234' value='");
    sf += htmlAttr(_cfg->cfg.ai_lms_url);
    sf += F("'></div>");

    // Model + Load/Unload buttons (LM Studio only)
    sf += F("<label>Модель <span id='ai-mod-hint' style='font-weight:normal;"
            "font-size:0.82em;color:var(--muted)'>(по умолчанию: ");
    sf += provDefModels[prov];
    sf += F(") <b style='color:var(--warn)'>⚠ При изменении требуется перезагрузка</b></span></label>"
            "<input type='text' name='ai_model' id='ai-model' class='input-field'"
            " placeholder='");
    sf += provDefModels[prov];
    sf += F("' value='");
    // Show current model or default if empty
    if (_cfg->cfg.ai_model[0]) {
        sf += htmlAttr(_cfg->cfg.ai_model);
    } else {
        sf += provDefModels[prov];
    }
    sf += F("' style='margin-bottom:8px' title='Имя модели для загрузки (сохраняется в конфиг)'>"
            "<div style='display:flex;gap:4px' id='lms-outer'");
    if (prov != 0) sf += F(" style='display:none'");
    sf += F(">"
            "<button type='button' class='btn btn-primary' id='lms-btn-load' "
            "onclick='lmsLoad()' style='white-space:nowrap;flex:1' "
            "title='Загрузить эту модель в LM Studio прямо сейчас'");
    if (prov != 0) sf += F(" style='display:none'");
    sf += F(">Загрузить</button>"
            "<button type='button' class='btn btn-secondary' id='lms-btn-unload' "
            "onclick='lmsUnload()' style='white-space:nowrap;flex:1' "
            "title='Выгрузить модель из памяти LM Studio'");
    if (prov != 0) sf += F(" style='display:none'");
    sf += F(">Выгрузить</button></div>"
            "<div style='font-size:0.82em;color:var(--muted);margin-top:4px'>"
            "Поле сохраняется в конфиг и применяется после перезагрузки. "
            "Кнопки Load/Unload управляют моделью в LM Studio прямо сейчас.</div>");

    // API URL
    sf += F("<label>API URL <span id='ai-url-hint' style='font-weight:normal;"
            "font-size:0.82em;color:var(--muted)'>(по умолчанию: ");
    sf += provDefUrls[prov];
    sf += F(")</span></label>"
            "<input type='text' name='ai_url' id='ai-url' class='input-field'"
            " placeholder='");
    sf += provDefUrls[prov];
    sf += F("' value='");
    sf += htmlAttr(_cfg->cfg.ai_api_url);
    sf += F("'>");

    // API Key
    sf += F("<label>API Ключ <span style='font-weight:normal;font-size:0.82em;"
            "color:var(--muted)'>(OpenAI / OpenRouter / Anthropic)</span></label>"
            "<input type='password' name='ai_key' class='input-field' "
            "placeholder='");
    sf += _cfg->cfg.ai_api_key[0] ? F("(сохранён, оставьте пустым для сохранения)") : F("sk-...");
    sf += F("'>");

    // Max tokens + context size in a 2-col grid
    sf += F("<div class='grid2' style='gap:12px'>");
    sf += F("<div><label>Макс. токенов <span style='font-weight:normal;"
            "font-size:0.82em;color:var(--muted)'>(длина ответа)</span></label>"
            "<input type='number' name='ai_maxtok' class='input-field'"
            " min='64' max='8192' value='");
    sf += _cfg->cfg.ai_max_tokens;
    sf += F("'></div>");
    sf += F("<div><label>Размер контекста <span style='font-weight:normal;"
            "font-size:0.82em;color:var(--muted)'>(локальные модели, токены)</span></label>"
            "<input type='number' name='ai_ctx' class='input-field'"
            " min='1024' max='128000' value='");
    sf += _cfg->cfg.ai_ctx_size;
    sf += F("'></div></div>");

    // Temperature + Tool rounds
    sf += F("<div class='grid2' style='gap:12px'>");
    sf += F("<div><label>Температура <span style='font-weight:normal;"
            "font-size:0.82em;color:var(--muted)' id='ai-temp-val'>(");
    sf += (_cfg->cfg.ai_temperature / 10);
    sf += F(".");
    sf += (_cfg->cfg.ai_temperature % 10);
    sf += F(")</span></label>"
            "<input type='range' name='ai_temp' id='ai-temp-range'"
            " min='0' max='100' value='");
    sf += _cfg->cfg.ai_temperature;
    sf += F("' oninput=\"document.getElementById('ai-temp-val').textContent="
            "'('+Math.floor(this.value/10)+'.'+this.value%10+')'\">"
            "</div>");
    sf += F("<div><label>Макс. раундов инструментов</label>"
            "<input type='number' name='ai_rounds' class='input-field'"
            " min='1' max='20' value='");
    sf += _cfg->cfg.ai_tool_rounds;
    sf += F("'></div></div>");

    sf += F("<hr>");

    // System prompt
    sf += F("<label>Системный промпт <span style='font-weight:normal;font-size:0.82em;"
            "color:var(--muted)'>(пусто = встроенный по умолчанию)</span></label>"
            "<textarea name='ai_sysprompt' class='input-field' rows='4'"
            " placeholder='Вы — ИИ ESP-HUB...'>");
    sf += htmlAttr(_cfg->cfg.ai_sys_prompt);
    sf += F("</textarea>");

    sf += F("<hr>");

    // Telegram
    sf += F("<div class='row-between' style='margin-bottom:6px'>"
            "<b>Telegram Бот</b></div>");
    sf += checkboxField("Включить", "ai_tg_en", _cfg->cfg.ai_tg_enabled);
    sf += inputField("Токен бота", "ai_tg_tok", _cfg->cfg.ai_tg_token, "text",
                     "1234567890:AAF...");
    sf += inputField("Allowed Chat ID (белый список, пусто = все)", "ai_tg_cid",
                     _cfg->cfg.ai_tg_chat_id, "text", "-100123456789");

    sf += submitButton("Сохранить");
    sf += F("</form>");

    // JS: update placeholders when provider changes
    sf += F("<script>");
    sf += F("var AI_MOD=['qwen/qwen3-vl-8b','qwen3:8b','gpt-4o-mini',"
            "'qwen/qwen3-coder:free','claude-3-5-haiku-20241022'];");
    sf += F("var AI_URL=['http://127.0.0.1:1234/v1/chat/completions',"
            "'http://127.0.0.1:11434/v1/chat/completions',"
            "'https://api.openai.com/v1/chat/completions',"
            "'https://openrouter.ai/api/v1/chat/completions',"
            "'https://api.anthropic.com/v1/messages'];");
    sf += F("function aiProvChange(){"
            "  var p=parseInt(document.getElementById('ai-prov').value);"
            "  document.getElementById('ai-model').placeholder=AI_MOD[p];"
            "  document.getElementById('ai-url').placeholder=AI_URL[p];"
            "  document.getElementById('ai-mod-hint').textContent='(по умолчанию: '+AI_MOD[p]+')';"
            "  document.getElementById('ai-url-hint').textContent='(по умолчанию: '+AI_URL[p]+')';"
            "  var lo=document.getElementById('lms-outer');"
            "  if(lo)lo.style.display=p===0?'flex':'none';"
            "  var lr=document.getElementById('lms-url-row');"
            "  if(lr)lr.style.display=p===0?'':'none';"
            "  document.getElementById('lms-btn-load').style.display=p===0?'':'none';"
            "  document.getElementById('lms-btn-unload').style.display=p===0?'':'none';"
            "}"
            "function lmsLoad(){"
            "  var m=document.getElementById('ai-model').value.trim()||document.getElementById('ai-model').placeholder;"
            "  var btn=document.getElementById('lms-btn-load');"
            "  var txt=btn.textContent;btn.textContent='Загружаю...';btn.disabled=true;"
            "  var ctx=parseInt(document.querySelector('input[name=\"ai_ctx\"]').value)||4096;"
            "  var ctrl=new AbortController();"
            "  var timeout=setTimeout(()=>ctrl.abort(),180000);"
            "  fetch('/api/ai/lms/load',{"
            "    method:'POST',"
            "    headers:{'Content-Type':'application/json'},"
            "    body:JSON.stringify({model:m,context_length:ctx}),"
            "    signal:ctrl.signal})"
            "  .then(r=>{clearTimeout(timeout);if(r.ok)return r.json();throw new Error('HTTP '+r.status);})"
            "  .then(function(d){"
            "    btn.textContent=txt;btn.disabled=false;"
            "    if(d.error){alert('Ошибка загрузки: '+d.error);}"
            "    else if(d.status==='loaded'){alert('Успешно! Модель '+d.instance_id+' загружена за '+d.load_time_seconds+'с');}"
            "    else{alert('Загрузка начата. Проверьте статус в LM Studio.');}"
            "  }).catch(function(e){"
            "    btn.textContent=txt;btn.disabled=false;"
            "    if(e.name==='AbortError')alert('Таймаут (3 мин). Загрузка может все еще идти в LM Studio.');"
            "    else if(e.message.includes('Failed'))alert('Нет связи с LM Studio. Проверьте адрес: '+document.getElementById('ai-lms-url').value);"
            "    else alert('Ошибка: '+e.message);"
            "  });"
            "}"
            "function lmsUnload(){"
            "  var m=document.getElementById('ai-model').value.trim()||document.getElementById('ai-model').placeholder;"
            "  var btn=document.getElementById('lms-btn-unload');"
            "  var txt=btn.textContent;btn.textContent='Выгружаю...';btn.disabled=true;"
            "  var ctrl=new AbortController();"
            "  var timeout=setTimeout(()=>ctrl.abort(),30000);"
            "  fetch('/api/ai/lms/unload',{"
            "    method:'POST',"
            "    headers:{'Content-Type':'application/json'},"
            "    body:JSON.stringify({model:m}),"
            "    signal:ctrl.signal})"
            "  .then(r=>{clearTimeout(timeout);if(r.ok)return r.json();throw new Error('HTTP '+r.status);})"
            "  .then(function(d){"
            "    btn.textContent=txt;btn.disabled=false;"
            "    if(d.error){alert('Ошибка выгрузки: '+d.error);}"
            "    else{alert('Модель '+m+' успешно выгружена.');}"
            "  }).catch(function(e){"
            "    btn.textContent=txt;btn.disabled=false;"
            "    if(e.name==='AbortError')alert('⏱️ Таймаут подключения');"
            "    else alert('Ошибка: '+e.message);"
            "  });"
            "}"
            "</script>");

    // ── Card 2: Chat ──────────────────────────────────────────────
    String ch;
    ch += F("<div id='ai-resp' style='min-height:70px;padding:10px;"
            "background:var(--card-bg);border:1px solid var(--border);"
            "border-radius:6px;margin-bottom:8px;white-space:pre-wrap;"
            "font-size:0.92em;max-height:400px;overflow-y:auto'>&#8203;</div>"
            "<div style='display:flex;flex-direction:column;gap:8px;margin-bottom:6px'>"
            "<textarea id='ai-msg' class='input-field' style='width:100%;min-height:80px;resize:vertical;margin:0;font-family:inherit' "
            "placeholder='Введите сообщение... (Enter - отправить, Shift+Enter - перенос)' "
            "onkeydown='if(event.key==\"Enter\" && !event.shiftKey){event.preventDefault();aiSend()}'></textarea>"
            "<div style='display:flex;gap:8px'>"
            "<button class='btn btn-primary' style='flex:1;padding:10px;font-size:1.1em' onclick='aiSend()'>Отправить</button>"
            "<button class='btn btn-secondary' style='padding:10px' onclick='aiClear()'>Очистить</button>"
            "</div></div>"
            "<div id='ai-status' style='font-size:0.82em;color:var(--muted)'></div>"
            "<script>"
            "var _aiPollId=null,_aiSeq=0;"
            "function aiSend(){"
            "  var m=document.getElementById('ai-msg').value.trim();"
            "  if(!m)return;"
            "  document.getElementById('ai-msg').value='';"
            "  document.getElementById('ai-msg').disabled=true;"
            "  document.getElementById('ai-status').textContent='Обработка...';"
            "  fetch('/api/ai/chat',{method:'POST',"
            "    headers:{'Content-Type':'application/json'},"
            "    body:JSON.stringify({message:m})})"
            "  .then(r=>r.json()).then(function(d){"
            "    document.getElementById('ai-msg').disabled=false;"
            "    if(d.error){document.getElementById('ai-status').textContent=d.error;return;}"
            "    if(_aiPollId)clearInterval(_aiPollId);"
            "    _aiPollId=setInterval(aiPoll,1200);"
            "  }).catch(function(){"
            "    document.getElementById('ai-msg').disabled=false;"
            "    document.getElementById('ai-status').textContent='Ошибка';"
            "  });"
            "}"
            "function aiPoll(){"
            "  fetch('/api/ai/status').then(r=>r.json()).then(function(d){"
            "    if(!d.processing&&d.seq!==_aiSeq){"
            "      _aiSeq=d.seq;"
            "      var el=document.getElementById('ai-resp');"
            "      el.textContent=d.response||'';"
            "      el.scrollTop=el.scrollHeight;"
            "      document.getElementById('ai-status').textContent='';"
            "      clearInterval(_aiPollId);_aiPollId=null;"
            "    }"
            "  });"
            "}"
            "function aiClear(){"
            "  fetch('/api/ai/history/clear',{method:'POST'})"
            "  .then(function(){"
            "    document.getElementById('ai-resp').textContent='';"
            "    document.getElementById('ai-status').textContent='История очищена';"
            "    _aiSeq=0;"
            "  });"
            "}"
            "</script>");

    // ── Model status card ──
    String ms;
    ms += F("<div class='row-between' style='margin-bottom:10px'>");
    ms += F("<div><b>Текущая модель:</b><br>");
    if (_cfg->cfg.ai_model[0]) {
        ms += F("<code style='background:var(--bg2);padding:4px 8px;border-radius:4px;font-size:0.9em'>");
        ms += htmlAttr(_cfg->cfg.ai_model);
        ms += F("</code>");
    } else {
        ms += F("<span style='color:var(--muted);font-style:italic'>Не установлена</span>");
    }
    ms += F("</div>");
    ms += F("<div style='text-align:right'>");
    if (_cfg->cfg.ai_enabled) {
        ms += badge("AI Включен", "badge-green");
    } else {
        ms += badge("AI Отключен", "badge-gray");
    }
    ms += F("</div></div>");
    ms += F("<hr><div style='font-size:0.85em;color:var(--muted)'>");
    ms += F("Провайдер: <b>");
    static const char* provs[] = {"LM Studio", "Ollama", "OpenAI", "OpenRouter", "Anthropic"};
    ms += provs[_cfg->cfg.ai_provider];
    ms += F("</b></div>");

    startPage("AI");
    _server.sendContent(card("<b>Статус модели</b>", ms));
    _server.sendContent(card("<b>Настройки ИИ агента</b>", sf));
    _server.sendContent(card("<b>Чат</b>", ch));
    _server.sendContent(F("<script>applyLang();</script>"));
    endPage();
}

void WebPortal::handleSaveAI() {
    if (_server.method() != HTTP_POST) { _server.send(405); return; }

    bool prev_enabled = _cfg->cfg.ai_enabled;
    bool model_changed = false;
    bool sys_reboot_requested = _server.hasArg("sys_reboot");
    
    // Check if model will change
    if (_server.hasArg("ai_model")) {
        const char* new_model = _server.arg("ai_model").c_str();
        if (strcmp(_cfg->cfg.ai_model, new_model) != 0) {
            model_changed = true;
        }
    }

    // Обработать ai_en ДО проверки перезагрузки
    _cfg->cfg.ai_enabled  = _server.hasArg("ai_en");
    _cfg->cfg.ai_provider = (uint8_t)(_server.hasArg("ai_prov")
                            ? _server.arg("ai_prov").toInt() : 0);

    if (_server.hasArg("ai_lms"))
        strlcpy(_cfg->cfg.ai_lms_url, _server.arg("ai_lms").c_str(),
                sizeof(_cfg->cfg.ai_lms_url));
    if (_server.hasArg("ai_url"))
        strlcpy(_cfg->cfg.ai_api_url, _server.arg("ai_url").c_str(),
                sizeof(_cfg->cfg.ai_api_url));
    if (_server.hasArg("ai_model"))
        strlcpy(_cfg->cfg.ai_model, _server.arg("ai_model").c_str(),
                sizeof(_cfg->cfg.ai_model));
    // Only overwrite key when user submitted a non-empty value
    if (_server.hasArg("ai_key") && _server.arg("ai_key").length() > 0)
        strlcpy(_cfg->cfg.ai_api_key, _server.arg("ai_key").c_str(),
                sizeof(_cfg->cfg.ai_api_key));
    if (_server.hasArg("ai_tg_tok"))
        strlcpy(_cfg->cfg.ai_tg_token, _server.arg("ai_tg_tok").c_str(),
                sizeof(_cfg->cfg.ai_tg_token));
    if (_server.hasArg("ai_tg_cid"))
        strlcpy(_cfg->cfg.ai_tg_chat_id, _server.arg("ai_tg_cid").c_str(),
                sizeof(_cfg->cfg.ai_tg_chat_id));
    _cfg->cfg.ai_tg_enabled = _server.hasArg("ai_tg_en");
    if (_server.hasArg("ai_sysprompt"))
        strlcpy(_cfg->cfg.ai_sys_prompt, _server.arg("ai_sysprompt").c_str(),
                sizeof(_cfg->cfg.ai_sys_prompt));

    if (_server.hasArg("ai_maxtok"))
        _cfg->cfg.ai_max_tokens  = (uint16_t)constrain(_server.arg("ai_maxtok").toInt(), 64, 8192);
    if (_server.hasArg("ai_ctx"))
        _cfg->cfg.ai_ctx_size    = (uint16_t)constrain(_server.arg("ai_ctx").toInt(), 1024, 128000);
    if (_server.hasArg("ai_temp"))
        _cfg->cfg.ai_temperature = (uint8_t)constrain(_server.arg("ai_temp").toInt(), 0, 100);
    if (_server.hasArg("ai_rounds"))
        _cfg->cfg.ai_tool_rounds = (uint8_t)constrain(_server.arg("ai_rounds").toInt(), 1, 20);

    _cfg->save();
    if (!prev_enabled && _cfg->cfg.ai_enabled)
        aiAgent.notifyEnabled();
    
    // Если была запрошена перезагрузка системы — перезагружаемся
    if (sys_reboot_requested) {
        _server.sendHeader("Location", "/ai");
        _server.send(302);
        delay(400);
        ESP.restart();
        return;
    }
    
    // If model was changed and AI is enabled, require restart to apply new model
    if (model_changed && _cfg->cfg.ai_enabled) {
        // Return a page with auto-redirect/reboot message with proper theme and encoding
        String html = F("<html><head><meta charset='utf-8'>"
                        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                        "<script>var _th=localStorage.getItem('theme')||'dark';"
                        "if(_th==='light')document.documentElement.classList.add('light');</script>"
                        "<style>"
                        ":root{--bg:#0f1117;--txt:#e1e4e8;--acc:#58a6ff}"
                        "html.light{--bg:#f6f8fa;--txt:#24292f;--acc:#0969da}"
                        "body{background:var(--bg);color:var(--txt);font-family:sans-serif;"
                        "padding:60px 20px;text-align:center;margin:0}"
                        "h2{color:var(--acc)}"
                        "</style>"
                        "<title>ESP-HUB</title></head><body>"
                        "<h2>Модель или настройки изменены</h2>"
                        "<p>Перезагружаю устройство...</p>"
                        "<script>setTimeout(function(){location.href='/ai';}, 2000);</script>"
                        "</body></html>");
        _server.sendHeader("Connection", "close");
        _server.send(200, "text/html; charset=utf-8", html);
        delay(600);
        ESP.restart();
        return;
    }
    
    _server.sendHeader("Location", "/ai");
    _server.send(302);
}

void WebPortal::handleApiAiChat() {
    if (_server.method() != HTTP_POST) { _server.send(405); return; }
    if (!_cfg->cfg.ai_enabled) {
        _server.send(200, "application/json", "{\"error\":\"AI disabled\"}");
        return;
    }
    String body = _server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok || !doc["message"].is<const char*>()) {
        _server.send(400, "application/json", "{\"error\":\"bad request\"}");
        return;
    }
    const char* msg = doc["message"];
    if (!aiAgent.submitMessage(msg)) {
        _server.send(200, "application/json", "{\"error\":\"busy\"}");
    } else {
        _server.send(200, "application/json", "{\"queued\":true}");
    }
}

void WebPortal::handleApiAiStatus() {
    JsonDocument doc;
    doc["processing"] = aiAgent.isProcessing();
    doc["seq"]        = (int)aiAgent.responseSeq();
    doc["response"]   = aiAgent.lastResponse();
    String out; serializeJson(doc, out);
    _server.sendHeader("Connection", "close");
    _server.send(200, "application/json", out);
}

void WebPortal::handleApiAiClearHistory() {
    aiAgent.clearHistory();
    _server.send(200, "application/json", "{\"ok\":true}");
}

// ── LM Studio model manager proxies ──────────────────────────────────────────
static String lmsBaseUrl(ConfigManager* cfg) {
    String base;
    // 1. Dedicated LMS URL field takes priority
    if (cfg->cfg.ai_lms_url[0]) {
        base = String(cfg->cfg.ai_lms_url);
        Serial.printf("[LMS] base from ai_lms_url: %s\n", base.c_str());
        return base;
    }
    // 2. Extract base from full chat API URL if set
    if (cfg->cfg.ai_api_url[0]) {
        String url = String(cfg->cfg.ai_api_url);
        int proto = url.indexOf("://");
        if (proto >= 0) {
            int slash = url.indexOf('/', proto + 3);
            base = (slash >= 0) ? url.substring(0, slash) : url;
            Serial.printf("[LMS] base from ai_api_url: %s\n", base.c_str());
            return base;
        }
    }
    base = String("http://127.0.0.1:1234");
    Serial.printf("[LMS] base fallback: %s\n", base.c_str());
    return base;
}

void WebPortal::handleApiAiLmsLoad() {
    if (_server.method() != HTTP_POST) { _server.send(405); return; }
    String body = _server.arg("plain");
    Serial.printf("[LMS] Load request body: %s\n", body.c_str());
    if (!body.length()) {
        _server.send(400, "application/json", "{\"error\":\"missing body\"}");
        return;
    }
    
    // Send immediate success response (202 Accepted) to avoid client timeout
    _server.send(202, "application/json", "{\"status\":\"starting\"}");
    
    // Extract model name for notifications
    String modelKey = "";
    {
        JsonDocument bd;
        if (deserializeJson(bd, body) == DeserializationError::Ok)
            modelKey = bd["model"].as<String>();
    }
    
    // Do the actual load in background
    String base = lmsBaseUrl(_cfg);
    String url = base + "/api/v1/models/load";
    Serial.printf("[LMS] POST %s\n", url.c_str());
    HTTPClient http;
    http.begin(url);
    http.setTimeout(120000);   // model load can take a while (max 120s)
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    String resp = (code > 0) ? http.getString() : "";
    Serial.printf("[LMS] Load resp code=%d body=%s\n", code, resp.c_str());
    http.end();
    
    if (code == 200) {
        Serial.printf("[LMS] \xE2\x9C\x85 Модель загружена: %s\n", modelKey.c_str());
        // Telegram notification
        if (_cfg->cfg.ai_tg_token[0] && _cfg->cfg.ai_tg_chat_id[0]) {
            String tgMsg = String("\xF0\x9F\x96\xA5 LM Studio: модель загружена\n\xF0\x9F\xA4\x96 ") + modelKey;
            aiAgent.sendTelegram(tgMsg.c_str(), atoll(_cfg->cfg.ai_tg_chat_id));
        }
    } else {
        Serial.printf("[LMS] \xE2\x9D\x8C Ошибка загрузки модели: %s (code=%d)\n", modelKey.c_str(), code);
    }
}

void WebPortal::handleApiAiLmsUnload() {
    if (_server.method() != HTTP_POST) { _server.send(405); return; }
    String body = _server.arg("plain");
    Serial.printf("[LMS] Unload request body: %s\n", body.c_str());
    if (!body.length()) {
        _server.send(400, "application/json", "{\"error\":\"missing body\"}");
        return;
    }
    
    // Send immediate success response
    _server.send(200, "application/json", "{\"status\":\"unloading\"}");
    
    String base = lmsBaseUrl(_cfg);
    String url = base + "/api/v1/models/unload";
    Serial.printf("[LMS] POST %s\n", url.c_str());
    HTTPClient http;
    http.begin(url);
    http.setTimeout(15000);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    String resp = (code > 0) ? http.getString() : "";
    Serial.printf("[LMS] Unload resp code=%d\n", code);
    http.end();
    if (code == 200) {
        Serial.printf("[LMS] \xE2\x9C\x85 Модель выгружена\n");
    } else {
        Serial.printf("[LMS] \xE2\x9D\x8C Ошибка выгрузки модели (code=%d)\n", code);
    }
}

// ════════════════════════════════════════════════════════════════
// NAT TOGGLE API
// ════════════════════════════════════════════════════════════════

void WebPortal::handleApiNatToggle() {
    if (_server.method() != HTTP_POST) { _server.send(405); return; }
    _cfg->cfg.ap_nat = !_cfg->cfg.ap_nat;
    _cfg->save();
    bool val = _cfg->cfg.ap_nat;
    String msg = val
        ? "NAT включён. NAT заработает после подключения к WiFi."
        : "NAT выключен. Перезагрузка...";
    String resp = String("{\"ap_nat\":") + (val ? "true" : "false")
                + ",\"message\":\"" + msg + "\",\"reboot\":true}";
    _server.send(200, "application/json", resp);
    delay(400);
    ESP.restart();
}

// ════════════════════════════════════════════════════════════════
// CRON API
// ════════════════════════════════════════════════════════════════

void WebPortal::handleApiCronList() {
    char buf[1400];
    cronMgr.listJson(buf, sizeof(buf));
    _server.send(200, "application/json", buf);
}

void WebPortal::handleApiCronAdd() {
    if (_server.method() != HTTP_POST) { _server.send(405); return; }
    String body = _server.arg("plain");
    if (!body.length()) { _server.send(400, "application/json", "{\"error\":\"empty\"}"); return; }

    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        _server.send(400, "application/json", "{\"error\":\"bad json\"}");
        return;
    }
    const char* typeStr  = doc["type"]   | "periodic";
    uint32_t isec        = doc["interval_sec"] | 60;
    uint8_t  hr          = doc["hour"]   | 0;
    uint8_t  mn          = doc["minute"] | 0;
    const char* action   = doc["action"] | "";

    CronType ct = CRON_PERIODIC;
    if      (strcmp(typeStr, "daily") == 0) ct = CRON_DAILY;
    else if (strcmp(typeStr, "once")  == 0) ct = CRON_ONCE;

    uint8_t id = cronMgr.add(ct, (ct == CRON_PERIODIC || ct == CRON_ONCE) ? isec : hr, mn, action);
    if (id == 0) {
        _server.send(507, "application/json", "{\"error\":\"cron full\"}");
    } else {
        _server.send(200, "application/json", String("{\"id\":") + id + "}");
    }
}

void WebPortal::handleApiCronDelete() {
    if (_server.method() != HTTP_POST) { _server.send(405); return; }
    String body = _server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body)) { _server.send(400, "application/json", "{\"error\":\"bad json\"}"); return; }
    int id = doc["id"] | -1;
    bool ok = cronMgr.remove((uint8_t)id);
    _server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"not found\"}");
}

void WebPortal::handleApiCronEnable() {
    if (_server.method() != HTTP_POST) { _server.send(405); return; }
    String body = _server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body)) { _server.send(400, "application/json", "{\"error\":\"bad json\"}"); return; }
    int id = doc["id"]       | -1;
    bool en = doc["enabled"] | true;
    bool ok = cronMgr.setEnabled((uint8_t)id, en);
    _server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"not found\"}");
}

void WebPortal::handleApiCronTz() {
    if (_server.method() != HTTP_POST) { _server.send(405); return; }
    String body = _server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body)) { _server.send(400, "application/json", "{\"error\":\"bad json\"}"); return; }
    const char* tz = doc["tz"] | "";
    if (!tz[0]) { _server.send(400, "application/json", "{\"error\":\"empty tz\"}"); return; }
    cronMgr.setTimezone(tz);
    strlcpy(_cfg->cfg.cron_tz, tz, sizeof(_cfg->cfg.cron_tz));
    _cfg->save();
    _server.send(200, "application/json", "{\"ok\":true}");
}

// ════════════════════════════════════════════════════════════════
// CRON PAGE
// ════════════════════════════════════════════════════════════════

void WebPortal::handleCron() {
    // Header card: timezone + status
    String hdr;
    hdr.reserve(400);
    hdr += F("<div class='grid2'>");
    hdr += F("<div><b>Планировщик задач</b><br><span class='text-muted'>Задачи запускаются AI-агентом</span></div>");
    hdr += F("<div><div class='flex-between'><span>Часовой пояс</span></div>");
    char tzb[64]; cronMgr.getTimezone(tzb, sizeof(tzb));
    hdr += F("<div style='display:flex;gap:8px;margin-top:4px'>");
    hdr += F("<input id='tz_in' type='text' class='input' value='");
    hdr += tzb;
    hdr += F("' placeholder='UTC0' style='max-width:140px'>");
    hdr += F("<button class='btn btn-primary' onclick='setTz()'>Сохранить</button>");
    hdr += F("</div>");
    hdr += F("<div class='text-muted' style='font-size:11px'>Формат POSIX: UTC0 / MSK-3 / EET-2EEST,M3.5.0,M10.5.0/3</div>");
    hdr += F("</div></div>");

    // Add job form
    String form;
    form.reserve(600);
    form += F("<form onsubmit='addJob(event)'>");
    form += F("<div class='grid2'>");
    form += F("<div>");
    form += F("<label>Тип</label>");
    form += F("<select id='c_type' class='input' onchange='typeChange()'>"
              "<option value='periodic'>Периодический</option>"
              "<option value='daily'>Ежедневный</option>"
              "<option value='once'>Однократный</option>"
              "</select>");
    form += F("</div>");
    form += F("<div id='row_interval'>");
    form += F("<label>Интервал (сек)</label>");
    form += F("<input id='c_isec' type='number' class='input' value='60' min='1'>");
    form += F("</div>");
    form += F("<div id='row_time' style='display:none'>");
    form += F("<label>Время HH:MM</label>");
    form += F("<div style='display:flex;gap:4px'>"
              "<input id='c_hr' type='number' class='input' value='0' min='0' max='23' style='max-width:70px'>"
              "<span style='line-height:36px'>:</span>"
              "<input id='c_mn' type='number' class='input' value='0' min='0' max='59' style='max-width:70px'>"
              "</div>");
    form += F("</div>");
    form += F("<div style='grid-column:span 2'>");
    form += F("<label>Действие (текст для AI)</label>");
    form += F("<input id='c_act' type='text' class='input' placeholder='Свет R:80% W:20%' required>");
    form += F("</div>");
    form += F("</div>");
    form += F("<div style='margin-top:8px'>");
    form += F("<button class='btn btn-primary' type='submit'>Добавить задачу</button>");
    form += F("</div></form>");

    // Table with jobs (rendered by JS)
    String tbl;
    tbl += F("<div id='cron_list'><div style='text-align:center;padding:20px;"
             "color:var(--txt2)'>Загрузка...</div></div>");

    // JS
    String js;
    js.reserve(1200);
    js += F("<script>");
    js += F("function typeChange(){"
            "  var t=document.getElementById('c_type').value;"
            "  document.getElementById('row_interval').style.display=(t==='daily')?'none':'';"
            "  document.getElementById('row_time').style.display=(t==='daily')?'':'none';"
            "}");
    js += F("function loadJobs(){"
            "  fetch('/api/cron').then(function(r){return r.json();}).then(function(d){"
            "    var html='<table style=\"width:100%;border-collapse:collapse;font-size:13px\">';"
            "    html+='<tr style=\"border-bottom:1px solid var(--border)\">"
                  "<th style=\"text-align:left;padding:4px 8px\">ID</th>"
                  "<th style=\"text-align:left;padding:4px 8px\">Тип</th>"
                  "<th style=\"text-align:left;padding:4px 8px\">Расписание</th>"
                  "<th style=\"text-align:left;padding:4px 8px\">Действие</th>"
                  "<th style=\"text-align:left;padding:4px 8px\">Статус</th>"
                  "<th style=\"padding:4px 8px\">&#9881;</th></tr>';"
            "    var types=['Период','Ежедн.','Один раз'];"
            "    (d.entries||[]).forEach(function(e){"
            "      var sched=e.type===0?e.interval_sec+'с':''+e.hour+':'+('0'+e.minute).slice(-2);"
            "      html+='<tr style=\"border-bottom:1px solid var(--border)\">';"
            "      html+='<td style=\"padding:4px 8px\">'+e.id+'</td>';"
            "      html+='<td style=\"padding:4px 8px\">'+types[e.type]+'</td>';"
            "      html+='<td style=\"padding:4px 8px\">'+sched+'</td>';"
            "      html+='<td style=\"padding:4px 8px\">'+e.action+'</td>';"
            "      html+='<td style=\"padding:4px 8px\">'+(e.enabled?'<span style=\"color:var(--green)\">&#9679; Вкл</span>':'<span style=\"color:var(--txt2)\">&#9679; Выкл</span>')+'</td>';"
            "      html+='<td style=\"padding:4px 8px;white-space:nowrap\">';"
            "      html+='<button class=\"btn btn-secondary\" style=\"padding:2px 6px;font-size:11px\" onclick=\"enJob('+e.id+','+!e.enabled+')\">'+(e.enabled?'Выкл':'Вкл')+'</button> ';"
            "      html+='<button class=\"btn btn-danger\" style=\"padding:2px 6px;font-size:11px\" onclick=\"delJob('+e.id+')\">&times;</button>';"
            "      html+='</td></tr>';"
            "    });"
            "    if(!(d.entries||[]).length) html+='<tr><td colspan=\"6\" style=\"text-align:center;padding:16px;color:var(--txt2)\">Нет задач</td></tr>';"
            "    html+='</table>';"
            "    document.getElementById('cron_list').innerHTML=html;"
            "  });"
            "}");
    js += F("function addJob(ev){"
            "  ev.preventDefault();"
            "  var t=document.getElementById('c_type').value;"
            "  var body={type:t,action:document.getElementById('c_act').value};"
            "  if(t==='daily'){body.hour=parseInt(document.getElementById('c_hr').value);"
            "    body.minute=parseInt(document.getElementById('c_mn').value);}"
            "  else body.interval_sec=parseInt(document.getElementById('c_isec').value);"
            "  fetch('/api/cron/add',{method:'POST',body:JSON.stringify(body)})"
            "  .then(function(r){return r.json();}).then(function(d){"
            "    if(d.id!==undefined){document.getElementById('c_act').value='';loadJobs();}"
            "    else alert(d.error||'Ошибка');"
            "  });"
            "}");
    js += F("function delJob(id){"
            "  if(!confirm('Удалить задачу #'+id+'?'))return;"
            "  fetch('/api/cron/delete',{method:'POST',body:JSON.stringify({id:id})})"
            "  .then(function(){loadJobs();});"
            "}");
    js += F("function enJob(id,en){"
            "  fetch('/api/cron/enable',{method:'POST',body:JSON.stringify({id:id,enabled:en})})"
            "  .then(function(){loadJobs();});"
            "}");
    js += F("function setTz(){"
            "  var tz=document.getElementById('tz_in').value.trim();"
            "  if(!tz)return;"
            "  fetch('/api/cron/tz',{method:'POST',body:JSON.stringify({tz:tz})})"
            "  .then(function(r){return r.json();}).then(function(d){"
            "    if(d.ok)alert('Сохранено. Часовой пояс: '+tz);"
            "    else alert(d.error||'Ошибка');"
            "  });"
            "}");
    js += F("loadJobs();");
    js += F("</script>");

    startPage("CRON");
    _server.sendContent(card("&#128336; Планировщик", hdr));
    _server.sendContent(card("&#10010; Добавить задачу", form));
    _server.sendContent(card("&#128203; Список задач", tbl + js));
    endPage();
}

// ════════════════════════════════════════════════════════════════
