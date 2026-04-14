#include "serial_console.h"
#include "driver/adc.h"

// ─────────────────── ANSI helpers ───────────────────
#if SC_ANSI
  #define C_RST  "\033[0m"
  #define C_BLD  "\033[1m"
  #define C_DIM  "\033[2m"
  #define C_CYN  "\033[36m"
  #define C_GRN  "\033[32m"
  #define C_YLW  "\033[33m"
  #define C_RED  "\033[31m"
  #define C_MGT  "\033[35m"
  #define C_WHT  "\033[97m"
#else
  #define C_RST  ""
  #define C_BLD  ""
  #define C_DIM  ""
  #define C_CYN  ""
  #define C_GRN  ""
  #define C_YLW  ""
  #define C_RED  ""
  #define C_MGT  ""
  #define C_WHT  ""
#endif

// ─────────────────── begin / tick ───────────────────

void SerialConsole::begin(ConfigManager*  cfg,
                          WiFiManager*   wifi,
                          SensorManager* sensors,
                          BLEManager*    ble,
                          FixtureManager* fixture,
                          MeshManager*   mesh) {
    _cfg     = cfg;
    _wifi    = wifi;
    _sensors = sensors;
    _ble     = ble;
    _fixture = fixture;
    _mesh    = mesh;
    _lastAuto = 0;
    _buf.reserve(128);
    // Keep serial auto-print disabled by default. It can flood the terminal
    // and make interactive command input feel unresponsive.
    _autoSec = 0;
    _autoLast = millis();

    if (!_initialized) {
        _initialized = true;
        printBanner();
    }
}

void SerialConsole::tick() {
    // Monitor mode: continuous sensor read until user presses Enter
    if (_monitorMode) {
        uint32_t now = millis();
        // First time or timer interval expired
        if (now - _monitorLast >= 2000UL || _monitorLast == 0) {
            _monitorLast = now;
            if (_sensors) {
                _sensors->readAll();
                delay(10);
                Serial.println();
                cmdSensors();
                Serial.println(C_DIM "  [Press Enter to exit monitor mode]" C_RST);
                Serial.flush();
            } else {
                Serial.println(C_RED "  ERROR: Sensor manager not available" C_RST);
                _monitorMode = false;
            }
        }
        // Check if user pressed a key to exit monitor mode
        if (Serial.available()) {
            while (Serial.available()) {
                char c = (char)Serial.read();
                if (c == '\r' || c == '\n') {
                    _monitorMode = false;
                    Serial.println(C_GRN "  [OK] Monitor mode stopped" C_RST);
                    prompt();
                    return;
                }
            }
        }
        return;
    }

    // Auto-read: print sensor data every _autoSec seconds
    if (_autoSec > 0 && _initialized) {
        uint32_t now = millis();
        if (now - _autoLast >= (uint32_t)_autoSec * 1000UL) {
            _autoLast = now;
            Serial.println();
            cmdSensors();
            prompt();
        }
    }

    // Print welcome on first character received (so it doesn't spam on boot)
    while (Serial.available()) {
        char c = (char)Serial.read();
        // Process line endings (both LF and CR-LF)
        if (c == '\r' || c == '\n') {
            if (!_initialized) {
                _initialized = true;
                printBanner();
            }
            String line = _buf;
            _buf = "";
            line.trim();
            if (line.length() > 0) processLine(line);
            prompt();
        } else if (c == 0x7F || c == '\b') {
            // Backspace
            if (_buf.length()) _buf.remove(_buf.length() - 1);
        } else {
            if (!_initialized) { _initialized = true; printBanner(); }
            _buf += c;
        }
    }
}

void SerialConsole::executeCommand(const String& line) {
    String cmd = line;
    cmd.trim();
    if (cmd.length() == 0) return;
    processLine(cmd);
}

// ─────────────────── Command dispatch ───────────────────

void SerialConsole::processLine(const String& line) {
    if (_mesh && _cfg && _cfg->cfg.mesh_enabled && line.length() > 0) {
        String rec = "EXEC LOCAL role=";
        rec += (_cfg->cfg.mesh_master_node ? "MAIN" : "NODE");
        rec += " node=";
        rec += String(_mesh->getNodeId());
        rec += " cmd=";
        rec += line;
        _mesh->addLogEntry(rec);
        Serial.printf("[MESH] EXEC LOCAL role=%s node=%u cmd=%s\n",
            _cfg->cfg.mesh_master_node ? "MAIN" : "NODE",
            (unsigned)_mesh->getNodeId(),
            line.c_str());
    }

    String cmd = line;
    cmd.toLowerCase();
    String args = "";

    int sp = cmd.indexOf(' ');
    if (sp > 0) {
        args = line.substring(sp + 1);
        args.trim();
        cmd  = cmd.substring(0, sp);
    }

    // Short light commands: R<n>, FR<n>, B<n>, W<n>, RF<n>, RW<n>, etc.
    if (_fixture && _fixture->isEnabled()) {
        if (cmd == "off" || cmd == "red" || cmd == "farred" || cmd == "blue" || 
            cmd == "white" || cmd == "full" || cmd == "grow" || cmd == "demo" || cmd == "test") {
            cmdLight(cmd);
            return;
        }
        if (parseLightCommand(line)) return;
    }

    if      (cmd == "help"   || cmd == "?")     cmdHelp();
    else if (cmd == "status" || cmd == "s")     cmdStatus();
    else if (cmd == "sensors"|| cmd == "data"
                             || cmd == "d")     cmdSensors();
    else if (cmd == "config" || cmd == "cfg")   cmdConfig();
    else if (cmd == "set")                      cmdSet(args);
    else if (cmd == "save")  {
        if (_cfg) { _cfg->save(); Serial.println(C_GRN "[OK] Config saved" C_RST); }
    }
    else if (cmd == "gpio")                     cmdGpio(args);
    else if (cmd == "wifi")                     cmdWifi(args);
    else if (cmd == "ble")                      cmdBle(args);
    else if (cmd == "mesh")                     cmdMesh(args);
    else if (cmd == "light" || cmd == "l")      cmdLight(args);
    else if (cmd == "scenario" || cmd == "sc")  cmdScenario(args);
    else if (cmd == "timer" || cmd == "tm")     cmdTimer(args);
    else if (cmd == "dim")                      cmdDim(args);
    else if (cmd == "read"  || cmd == "r")      cmdRead();
    else if (cmd == "clock" || cmd == "time")   cmdClock();
    else if (cmd == "scan")                     cmdScan();
    else if (cmd == "auto")                     cmdAuto(args);
    else if (cmd == "monitor" || cmd == "watch") cmdMonitor();
    else if (cmd == "mqtt")                     cmdMqtt(args);
    else if (cmd == "json")                     cmdJson();
    else if (cmd == "reboot" || cmd == "restart"
                             || cmd == "rst") {
        Serial.println(C_YLW "Rebooting..." C_RST);
        delay(200);
        ESP.restart();
    }
    else if (cmd == "time") {
        // Manual time set: "time HH:MM:SS" or "time" to show current
        if (args.length() == 0) {
            struct tm ti;
            if (getLocalTime(&ti, 0) && ti.tm_year > 100) {
                Serial.printf(C_GRN "  Current time: %02d:%02d:%02d %02d.%02d.%04d" C_RST "\n",
                    ti.tm_hour, ti.tm_min, ti.tm_sec,
                    ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
            } else {
                Serial.println(C_RED "  Time not synced (NTP unavailable)" C_RST);
                Serial.println(C_YLW "  Set manually: time HH:MM:SS  (e.g. time 14:30:00)" C_RST);
            }
        } else {
            // Parse HH:MM:SS
            int h = -1, m = -1, s = 0;
            if (sscanf(args.c_str(), "%d:%d:%d", &h, &m, &s) >= 2 &&
                h >= 0 && h < 24 && m >= 0 && m < 60 && s >= 0 && s < 60) {
                struct timeval tv;
                struct tm ti;
                time_t now;
                time(&now);
                localtime_r(&now, &ti);
                // Keep existing date if it's valid, otherwise use Jan 1 2026
                if (ti.tm_year < 100) { ti.tm_year = 126; ti.tm_mon = 0; ti.tm_mday = 1; }
                ti.tm_hour = h; ti.tm_min = m; ti.tm_sec = s;
                tv.tv_sec  = mktime(&ti);
                tv.tv_usec = 0;
                settimeofday(&tv, nullptr);
                Serial.printf(C_GRN "  [OK] Time set to %02d:%02d:%02d — schedule enabled" C_RST "\n", h, m, s);
            } else {
                Serial.println(C_RED "  Usage: time HH:MM:SS  (e.g. time 14:30:00)" C_RST);
            }
        }
    }
    else if (cmd == "url") {
        String ap = (_wifi) ? _wifi->apIP() : String("192.168.4.1");
        if (ap.length() == 0) ap = "192.168.4.1";
        Serial.printf(C_CYN "  AP:  http://%s/\n" C_RST, ap.c_str());
        Serial.printf(C_CYN "  STA: http://esp-hub.local/\n" C_RST);
        if (_wifi && _wifi->isConnected())
            Serial.printf(C_CYN "  STA IP: http://%s/\n" C_RST, _wifi->localIP().c_str());
    }
    else {
        Serial.printf(C_RED "  Unknown command: '%s'  (type 'help')\n" C_RST, cmd.c_str());
    }
}

// ─────────────────── wifi ───────────────────

void SerialConsole::cmdWifi(const String& args) {
    String a = args; a.trim(); a.toLowerCase();

    if (a == "off") {
        if (!_wifi) { Serial.println(C_RED "  No WiFi manager" C_RST); return; }
        if (!_wifi->isWifiEnabled()) { Serial.println(C_YLW "  WiFi already OFF" C_RST); return; }
        _wifi->wifiOff();
        Serial.println(C_GRN "  [OK] WiFi radio OFF" C_RST);
        Serial.println(C_YLW "  ADC2 pins (0,2,4,12-15,25-27) are now accessible" C_RST);
        Serial.println(C_YLW "  Use 'gpio <pin> adc2' or 'read' to get readings" C_RST);
        Serial.println(C_YLW "  Use 'wifi on' to restore network" C_RST);
    }
    else if (a == "on") {
        if (!_wifi) { Serial.println(C_RED "  No WiFi manager" C_RST); return; }
        if (_wifi->isWifiEnabled()) { Serial.println(C_YLW "  WiFi already ON" C_RST); return; }
        Serial.println("  Restoring WiFi...");
        _wifi->wifiOn();
        Serial.println(C_GRN "  [OK] WiFi radio ON" C_RST);
    }
    else if (a == "scan") {
        cmdScan();
    }
    else if (a == "status" || a == "") {
        if (!_wifi) { Serial.println(C_RED "  No WiFi manager" C_RST); return; }
        bool on  = _wifi->isWifiEnabled();
        bool sta = _wifi->isConnected();
        Serial.printf("  Radio  : %s\n", on  ? C_GRN "ON"  C_RST : C_RED  "OFF" C_RST);
        Serial.printf("  STA    : %s\n", sta ? C_GRN "Connected" C_RST : C_YLW "Offline" C_RST);
        if (sta) Serial.printf("  IP     : " C_WHT "%s" C_RST "  RSSI " C_WHT "%d dBm" C_RST "\n",
                               _wifi->localIP().c_str(), _wifi->rssi());
        Serial.printf("  AP     : %s  " C_DIM "%s" C_RST "\n",
                      _wifi->isAP() ? C_GRN "UP" C_RST : C_DIM "down" C_RST,
                      _wifi->apIP().c_str());
    }
    else {
        Serial.println(C_RED "  Usage: wifi off | on | scan | status" C_RST);
    }
}

// ─────────────────── ble ───────────────────

void SerialConsole::cmdBle(const String& args) {
    if (!_ble) { Serial.println(C_RED "  No BLE manager" C_RST); return; }
    String a = args; a.trim(); a.toLowerCase();

    if (a == "on" || a == "start") {
        if (_ble->isEnabled()) { Serial.println(C_YLW "  BLE already running" C_RST); return; }
        const char* name = (_cfg && strlen(_cfg->cfg.ble_name)) ? _cfg->cfg.ble_name
                         : (_cfg ? _cfg->cfg.device_name : "ESP-HUB");
        if (_ble->begin(name)) {
            Serial.printf(C_GRN "  [OK] BLE started as '%s'" C_RST "\n", name);
        } else {
            Serial.println(C_RED "  BLE start failed" C_RST);
        }
    }
    else if (a == "off" || a == "stop") {
        if (!_ble->isEnabled()) { Serial.println(C_YLW "  BLE already stopped" C_RST); return; }
        _ble->stop();
        Serial.println(C_GRN "  [OK] BLE stopped" C_RST);
    }
    else if (a == "status" || a == "") {
        bool en  = _ble->isEnabled();
        bool con = _ble->isConnected();
        Serial.printf("  BLE : %s", en ? C_GRN "Running" C_RST : C_DIM "Stopped" C_RST);
        if (en && con)  Serial.print("  " C_GRN "Client connected" C_RST);
        else if (en)    Serial.print("  " C_YLW "Advertising" C_RST);
        Serial.println();
    }
    else {
        Serial.println(C_RED "  Usage: ble on | off | status" C_RST);
    }
}

// ─────────────────── mesh ───────────────────

void SerialConsole::cmdMesh(const String& args) {
    String a = args; a.trim(); a.toLowerCase();

    if (!_cfg) {
        Serial.println(C_RED "  No config manager" C_RST);
        return;
    }

    if (a == "" || a == "status") {
        bool enabled = _cfg->cfg.mesh_enabled;
        Serial.println(C_BLD C_CYN "  Mesh Status" C_RST);
        Serial.printf("  Config : %s\n", enabled ? C_GRN "ENABLED" C_RST : C_DIM "DISABLED" C_RST);
        Serial.printf("  Role   : %s\n", _cfg->cfg.mesh_master_node ? C_GRN "MASTER" C_RST : C_DIM "NODE" C_RST);
        if (_mesh && enabled) {
            Serial.printf("  NodeID : 0x%X\n", _mesh->getNodeId());
            Serial.printf("  Peers  : %u\n", (unsigned)_mesh->getConnectedCount());
            Serial.printf("  State  : %s\n", _mesh->isConnected() ? C_GRN "Connected" C_RST : C_YLW "Waiting peers" C_RST);
        } else {
            Serial.println("  Runtime: not active (enable + reboot)");
        }
        return;
    }

    if (a == "on") {
        _cfg->cfg.mesh_enabled = true;
        _cfg->save();
        Serial.println(C_GRN "  [OK] Mesh enabled in config" C_RST);
        Serial.println(C_YLW "  Reboot required to start mesh stack" C_RST);
        return;
    }

    if (a == "off") {
        _cfg->cfg.mesh_enabled = false;
        _cfg->save();
        Serial.println(C_GRN "  [OK] Mesh disabled in config" C_RST);
        Serial.println(C_YLW "  Reboot required to stop mesh stack cleanly" C_RST);
        return;
    }

    if (a == "nodes") {
        if (_mesh && _cfg->cfg.mesh_enabled) {
            Serial.print("  Nodes: ");
            Serial.println(_mesh->getNodeListJson());
        } else {
            Serial.println(C_DIM "  Nodes: [] (mesh disabled)" C_RST);
        }
        return;
    }

    if (a == "log") {
        if (_mesh && _cfg->cfg.mesh_enabled) {
            Serial.print("  Mesh log: ");
            Serial.println(_mesh->getLogJson(20));
        } else {
            Serial.println(C_DIM "  Mesh log unavailable (mesh disabled)" C_RST);
        }
        return;
    }

    if (a.startsWith("chat ")) {
        if (!_mesh || !_cfg->cfg.mesh_enabled) {
            Serial.println(C_RED "  Mesh disabled" C_RST);
            return;
        }
        String text = args.substring(5);
        text.trim();
        if (text.length() == 0) {
            Serial.println(C_RED "  Usage: mesh chat <text> | mesh chat node:<id> <text>" C_RST);
            return;
        }

        String target = "all";
        if (text.startsWith("node:")) {
            int sp = text.indexOf(' ');
            if (sp <= 5) {
                Serial.println(C_RED "  Usage: mesh chat node:<id> <text>" C_RST);
                return;
            }
            target = text.substring(0, sp);
            text = text.substring(sp + 1);
            text.trim();
            if (text.length() == 0) {
                Serial.println(C_RED "  Usage: mesh chat node:<id> <text>" C_RST);
                return;
            }
        }

        const char* from = (_cfg && strlen(_cfg->cfg.device_name)) ? _cfg->cfg.device_name : "ESP-HUB";
        _mesh->sendChatMessage(from, text, target);
        Serial.println(C_GRN "  [OK] Chat message sent" C_RST);
        return;
    }

    if (a.startsWith("data ")) {
        if (!_mesh || !_cfg->cfg.mesh_enabled) {
            Serial.println(C_RED "  Mesh disabled" C_RST);
            return;
        }
        String body = args.substring(5);
        body.trim();
        int sp = body.indexOf(' ');
        if (sp <= 0) {
            Serial.println(C_RED "  Usage: mesh data <topic> <payload>" C_RST);
            return;
        }
        String topic = body.substring(0, sp);
        String payload = body.substring(sp + 1);
        payload.trim();
        _mesh->sendDataMessage(topic, payload);
        Serial.println(C_GRN "  [OK] Data message sent" C_RST);
        return;
    }

    if (a.startsWith("cmd ")) {
        if (!_mesh || !_cfg->cfg.mesh_enabled) {
            Serial.println(C_RED "  Mesh disabled" C_RST);
            return;
        }
        String remoteCmd = args.substring(4);
        remoteCmd.trim();
        if (remoteCmd.length() == 0) {
            Serial.println(C_RED "  Usage: mesh cmd <serial-command> | mesh cmd node:<id> <serial-command>" C_RST);
            return;
        }

        String target = "all";
        if (remoteCmd.startsWith("node:")) {
            int sp = remoteCmd.indexOf(' ');
            if (sp <= 5) {
                Serial.println(C_RED "  Usage: mesh cmd node:<id> <serial-command>" C_RST);
                return;
            }
            target = remoteCmd.substring(0, sp);
            remoteCmd = remoteCmd.substring(sp + 1);
            remoteCmd.trim();
            if (remoteCmd.length() == 0) {
                Serial.println(C_RED "  Usage: mesh cmd node:<id> <serial-command>" C_RST);
                return;
            }
        }

        if (target == "all") {
            executeCommand(remoteCmd);
            _mesh->sendCommandMessage(remoteCmd, "all", (uint32_t)millis());
            Serial.printf(C_GRN "  [OK] Command executed local + relayed: %s" C_RST "\n", remoteCmd.c_str());
        } else {
            _mesh->sendCommandMessage(remoteCmd, target, (uint32_t)millis());
            Serial.printf(C_GRN "  [OK] Command sent to %s: %s" C_RST "\n", target.c_str(), remoteCmd.c_str());
        }
        return;
    }

    Serial.println(C_RED "  Usage: mesh status | mesh on | mesh off | mesh nodes | mesh log | mesh chat <text> | mesh chat node:<id> <text> | mesh data <topic> <payload> | mesh cmd <command> | mesh cmd node:<id> <command>" C_RST);
}

// ─────────────────── light ───────────────────

// Parse short commands: R10, FR50, B100, W200, RF100, RW50, B10W50, etc.
bool SerialConsole::parseLightCommand(const String& line) {
    String s = line;
    s.trim();
    s.toUpperCase();
    
    // Skip if it's a known long command
    if (s.startsWith("LIGHT") || s.startsWith("L ") ||
        s.startsWith("HELP") || s.startsWith("STATUS") || s.startsWith("OFF") ||
        s.startsWith("RED") || s.startsWith("FARRED") || s.startsWith("BLUE") || 
        s.startsWith("WHITE") || s.startsWith("FULL") || s.startsWith("GROW")) {
        return false; // Let cmdLight handle it
    }
    
    int r = -1, fr = -1, b = -1, w = -1;
    bool parsed = false;
    
    // Parse FR first (to avoid confusion with R)
    int pos = s.indexOf("FR");
    if (pos == -1) pos = s.indexOf("fr");
    if (pos == -1) pos = s.indexOf("Fr");
    if (pos >= 0) {
        String val = "";
        for (int i = pos + 2; i < s.length(); i++) {
            if (isDigit(s[i])) val += s[i];
            else break;
        }
        if (val.length() > 0) {
            fr = val.toInt();
            parsed = true;
        }
    }
    
    // Parse R (not part of FR)
    for (int i = 0; i < s.length(); i++) {
        if ((s[i] == 'R' || s[i] == 'r') && (i == 0 || (s[i-1] != 'F' && s[i-1] != 'f'))) {
            String val = "";
            for (int j = i + 1; j < s.length(); j++) {
                if (isDigit(s[j])) val += s[j];
                else break;
            }
            if (val.length() > 0) {
                r = val.toInt();
                parsed = true;
            }
            break;
        }
    }
    
    // Parse B
    pos = s.indexOf('B');
    if (pos == -1) pos = s.indexOf('b');
    if (pos >= 0) {
        String val = "";
        for (int i = pos + 1; i < s.length(); i++) {
            if (isDigit(s[i])) val += s[i];
            else break;
        }
        if (val.length() > 0) {
            b = val.toInt();
            parsed = true;
        }
    }
    
    // Parse W
    pos = s.indexOf('W');
    if (pos == -1) pos = s.indexOf('w');
    if (pos >= 0) {
        String val = "";
        for (int i = pos + 1; i < s.length(); i++) {
            if (isDigit(s[i])) val += s[i];
            else break;
        }
        if (val.length() > 0) {
            w = val.toInt();
            parsed = true;
        }
    }
    
    if (parsed) {
        // Apply values (convert from % to 0-200 scale)
        // If value <= 100, treat as percentage; if > 100, treat as raw 0-200 value
        uint8_t red = (r >= 0) ? ((r <= 100) ? r * 2 : r) : _fixture->getRed();
        uint8_t farRed = (fr >= 0) ? ((fr <= 100) ? fr * 2 : fr) : _fixture->getFarRed();
        uint8_t blue = (b >= 0) ? ((b <= 100) ? b * 2 : b) : _fixture->getBlue();
        uint8_t white = (w >= 0) ? ((w <= 100) ? w * 2 : w) : _fixture->getWhite();
        
        Serial.printf(C_YLW "[LIGHT] " C_WHT "R=%d FR=%d B=%d W=%d (%.1f%% %.1f%% %.1f%% %.1f%%)" C_RST "\n",
                      red, farRed, blue, white, red*0.5, farRed*0.5, blue*0.5, white*0.5);
        bool ok = _fixture->setChannels(red, farRed, blue, white);
        if (ok && _cfg) {
            _cfg->cfg.fixture.red_brightness = red;
            _cfg->cfg.fixture.far_red_brightness = farRed;
            _cfg->cfg.fixture.blue_brightness = blue;
            _cfg->cfg.fixture.white_brightness = white;
        }
        return true;
    }
    
    return false;
}

void SerialConsole::cmdLight(const String& args) {
    if (!_fixture) { Serial.println(C_RED "  No fixture manager" C_RST); return; }
    if (!_fixture->isEnabled()) { Serial.println(C_RED "  Fixture manager disabled" C_RST); return; }

    auto applyAndRemember = [this](uint8_t red, uint8_t farRed, uint8_t blue, uint8_t white) {
        bool ok = _fixture->setChannels(red, farRed, blue, white);
        if (ok && _cfg) {
            _cfg->cfg.fixture.red_brightness = red;
            _cfg->cfg.fixture.far_red_brightness = farRed;
            _cfg->cfg.fixture.blue_brightness = blue;
            _cfg->cfg.fixture.white_brightness = white;
        }
        return ok;
    };
    
    String a = args; a.trim(); a.toLowerCase();

    if (a == "off") {
        Serial.println(C_YLW "[LIGHT] All OFF" C_RST);
        applyAndRemember(0, 0, 0, 0);
    }
    else if (a == "red") {
        Serial.println(C_YLW "[LIGHT] Red 100%" C_RST);
        applyAndRemember(FIXTURE_BRIGHTNESS_100, 0, 0, 0);
    }
    else if (a == "farred") {
        Serial.println(C_YLW "[LIGHT] Far Red 100%" C_RST);
        applyAndRemember(0, FIXTURE_BRIGHTNESS_100, 0, 0);
    }
    else if (a == "blue") {
        Serial.println(C_YLW "[LIGHT] Blue 100%" C_RST);
        applyAndRemember(0, 0, FIXTURE_BRIGHTNESS_100, 0);
    }
    else if (a == "white") {
        Serial.println(C_YLW "[LIGHT] White 100%" C_RST);
        applyAndRemember(0, 0, 0, FIXTURE_BRIGHTNESS_100);
    }
    else if (a == "full") {
        Serial.println(C_YLW "[LIGHT] Full 100%" C_RST);
        applyAndRemember(FIXTURE_BRIGHTNESS_100, FIXTURE_BRIGHTNESS_100,
                         FIXTURE_BRIGHTNESS_100, FIXTURE_BRIGHTNESS_100);
    }
    else if (a == "demo" || a == "test") {
        Serial.println(C_YLW "[LIGHT] Running Demo..." C_RST);
        _fixture->runDemo();
    }
    else if (a == "grow") {
        Serial.println(C_YLW "[LIGHT] Grow (R70% FR50% B50% W30%)" C_RST);
        applyAndRemember(FIXTURE_BRIGHTNESS_70, FIXTURE_BRIGHTNESS_50,
                         FIXTURE_BRIGHTNESS_50, FIXTURE_BRIGHTNESS_30);
    }
    else if (a == "status" || a == "") {
        Serial.println(C_BLD C_CYN "  Light Status" C_RST);
        Serial.printf("  Enabled : %s\n", _fixture->isEnabled() ? C_GRN "Yes" C_RST : C_RED "No" C_RST);
        Serial.printf("  Last ACK: %s\n", _fixture->isLastAckOk() ? C_GRN "OK" C_RST : C_RED "FAIL" C_RST);
        Serial.printf("  Red     : " C_WHT "%.1f%%" C_RST "\n", _fixture->getRed() * 0.5);
        Serial.printf("  Far Red : " C_WHT "%.1f%%" C_RST "\n", _fixture->getFarRed() * 0.5);
        Serial.printf("  Blue    : " C_WHT "%.1f%%" C_RST "\n", _fixture->getBlue() * 0.5);
        Serial.printf("  White   : " C_WHT "%.1f%%" C_RST "\n", _fixture->getWhite() * 0.5);
    }
    else if (a.startsWith("set")) {
        // Parse: set R<val> FR<val> B<val> W<val>
        // Example: set R200 FR0 B100 W60
        int r = 0, fr = 0, b = 0, w = 0;
        String s = a.substring(3); s.trim(); s.toUpperCase();
        
        int pos = s.indexOf("FR");
        if (pos == -1) pos = s.indexOf("fr");
        if (pos == -1) pos = s.indexOf("Fr");
        if (pos >= 0) { fr = s.substring(pos + 2).toInt(); }
        
        for (int i = 0; i < (int)s.length(); i++) {
            if ((s[i] == 'R' || s[i] == 'r') && (i == 0 || (s[i-1] != 'F' && s[i-1] != 'f'))) {
                r = s.substring(i + 1).toInt();
                break;
            }
        }
        
        pos = s.indexOf('B');
        if (pos == -1) pos = s.indexOf('b');
        if (pos >= 0) { b = s.substring(pos + 1).toInt(); }
        
        pos = s.indexOf('W');
        if (pos == -1) pos = s.indexOf('w');
        if (pos >= 0) { w = s.substring(pos + 1).toInt(); }
        
        Serial.printf(C_YLW "[LIGHT] Set: R=%d FR=%d B=%d W=%d (%.1f%% %.1f%% %.1f%% %.1f%%)" C_RST "\n",
                      r, fr, b, w, r*0.5, fr*0.5, b*0.5, w*0.5);
        applyAndRemember((uint8_t)r, (uint8_t)fr, (uint8_t)b, (uint8_t)w);
    }
    else if (a == "help") {
        Serial.println(C_BLD C_CYN "  Light Commands" C_RST);
        Serial.println();
        Serial.println(C_BLD "  Presets:" C_RST);
        Serial.println("    " C_YLW "light off" C_RST "       — All channels OFF");
        Serial.println("    " C_YLW "light red" C_RST "       — Red 100%");
        Serial.println("    " C_YLW "light farred" C_RST "    — Far Red 100%");
        Serial.println("    " C_YLW "light blue" C_RST "      — Blue 100%");
        Serial.println("    " C_YLW "light white" C_RST "     — White 100%");
        Serial.println("    " C_YLW "light full" C_RST "      — All channels 100%");
        Serial.println("    " C_YLW "light grow" C_RST "      — Grow preset (R70 FR50 B50 W30)");
        Serial.println();
        Serial.println(C_BLD "  Custom values:" C_RST);
        Serial.println("    " C_YLW "light set R<n>..." C_RST " — Custom (0-200)");
        Serial.println("    " C_YLW "R<n> FR<n> B<n> W<n>" C_RST "  — Short commands (0-100%)");
        Serial.println();
        Serial.println(C_BLD "  Status:" C_RST);
        Serial.println("    " C_YLW "light status" C_RST "    — Current brightness");
        Serial.println();
        Serial.println("  Examples:");
        Serial.println("    " C_DIM "light set R200 FR0 B100 W60" C_RST);
        Serial.println("    " C_DIM "R50 B100 W30" C_RST "  (50% Red, 100% Blue, 30% White)");
        Serial.println("    " C_DIM "FR75" C_RST "  (75% Far Red)");
    }
    else {
        Serial.println(C_RED "  Usage: light off | red | farred | blue | white | full | grow | set | status | help" C_RST);
    }
}

// ─────────────────── read (force sensor read) ───────────────────

void SerialConsole::cmdRead() {
    if (!_sensors) { Serial.println(C_RED "  No sensor manager" C_RST); return; }
    Serial.println(C_DIM "  Reading sensors..." C_RST);
    _sensors->readAll();
    // Small pause so ADC settling is complete
    delay(20);
    cmdSensors();
}

// ─────────────────── scan (WiFi scan) ───────────────────

void SerialConsole::cmdScan() {
    Serial.println("  Scanning WiFi networks...");
    int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);
    hr();
    if (n == 0) {
        Serial.println("  " C_DIM "(no networks found)" C_RST);
    } else {
        Serial.printf("  Found %d network(s):\n", n);
        for (int i = 0; i < n; i++) {
            int rssi = WiFi.RSSI(i);
            const char* rssiCol = rssi >= -60 ? C_GRN : rssi >= -80 ? C_YLW : C_RED;
            Serial.printf("  %2d. " C_WHT "%-28s" C_RST " %sCh%2d  %d dBm" C_RST "  %s\n",
                          i + 1,
                          WiFi.SSID(i).c_str(),
                          rssiCol,
                          (int)WiFi.channel(i),
                          rssi,
                          WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? C_DIM "open" C_RST : "\xF0\x9F\x94\x92");
        }
    }
    WiFi.scanDelete();
    hr();
}

// ─────────────────── auto ───────────────────

void SerialConsole::cmdAuto(const String& args) {
    String a = args; a.trim(); a.toLowerCase();
    if (a == "off" || a == "0") {
        _autoSec = 0;
        Serial.println(C_GRN "  [OK] Auto-read OFF" C_RST);
        return;
    }
    int sec = a.toInt();
    if (sec <= 0) { Serial.println(C_RED "  Usage: auto <seconds>  or  auto off" C_RST); return; }
    if (sec < 1)  sec = 1;
    _autoSec  = (uint16_t)sec;
    _autoLast = millis() - (uint32_t)_autoSec * 1000UL;  // fire immediately
    Serial.printf(C_GRN "  [OK] Auto-read every %d s  (auto off to stop)" C_RST "\n", sec);
}

// ──────────────────── monitor ──────────────────

void SerialConsole::cmdMonitor() {
    if (!_sensors) { Serial.println(C_RED "  No sensor manager" C_RST); return; }
    hr();
    Serial.println(C_BLD C_GRN "  Continuous Sensor Monitor" C_RST);
    hr();
    Serial.println(C_DIM "  Reading sensors every 2 seconds..." C_RST);
    Serial.println(C_DIM "  [Press Enter to exit]" C_RST);
    Serial.flush();
    
    _monitorMode = true;
    _monitorLast = millis() - 2000UL;  // fire immediately on first tick
}

// ─────────────────── help ───────────────────

void SerialConsole::cmdHelp() {
    Serial.println();
    Serial.println(C_BLD C_CYN "╔════════════════════════════════════════════════════╗" C_RST);
    Serial.println(C_BLD C_CYN "║        ESP-HUB Serial Console — Help Menu         ║" C_RST);
    Serial.println(C_BLD C_CYN "╚════════════════════════════════════════════════════╝" C_RST);
    Serial.println();
    
    Serial.println(C_BLD C_WHT "📊 Information:" C_RST);
    Serial.println("  " C_YLW "status" C_RST "   Show system status (WiFi, BLE, Mesh, sensors)");
    Serial.println("  " C_YLW "sensors" C_RST "  Show cached sensor readings (or: " C_YLW "d" C_RST ")");
    Serial.println("  " C_YLW "read" C_RST "     Force sensor read NOW (or: " C_YLW "r" C_RST ")");
    Serial.println("  " C_YLW "config" C_RST "   Full config dump in JSON");
    Serial.println();
    Serial.flush(); delay(10);

    Serial.println(C_BLD C_WHT "📡 WiFi Control:" C_RST);
    Serial.println("  " C_YLW "wifi on" C_RST "     Enable WiFi radio");
    Serial.println("  " C_YLW "wifi off" C_RST "    Disable WiFi (frees ADC2)");
    Serial.println("  " C_YLW "wifi status" C_RST "  Show WiFi state / RSSI / IP");
    Serial.println("  " C_YLW "wifi scan" C_RST "   Scan nearby networks");
    Serial.println("  " C_YLW "scan" C_RST "       Short for: wifi scan");
    Serial.println();
    Serial.flush(); delay(10);

    Serial.println(C_BLD C_WHT "🔵 Bluetooth:" C_RST);
    Serial.println("  " C_YLW "ble on" C_RST "      Start BLE GATT server");
    Serial.println("  " C_YLW "ble off" C_RST "     Stop BLE");
    Serial.println("  " C_YLW "ble status" C_RST "  Show BLE connections");
    Serial.println();
    Serial.flush(); delay(10);

    Serial.println(C_BLD C_WHT "🕸 Mesh Network:" C_RST);
    Serial.println("  " C_YLW "mesh status" C_RST "  Show mesh config/runtime status");
    Serial.println("  " C_YLW "mesh on" C_RST "      Enable mesh in config + save");
    Serial.println("  " C_YLW "mesh off" C_RST "     Disable mesh in config + save");
    Serial.println("  " C_YLW "mesh nodes" C_RST "   Show connected node list JSON");
    Serial.println("  " C_YLW "mesh chat <txt>" C_RST "  Send chat message to all mesh nodes");
    Serial.println("  " C_YLW "mesh chat node:<id> <txt>" C_RST " Direct chat to one node");
    Serial.println("  " C_YLW "mesh data <k> <v>" C_RST " Send arbitrary data packet");
    Serial.println("  " C_YLW "mesh cmd <line>" C_RST "  Execute local + relay command to mesh");
    Serial.println("  " C_YLW "mesh cmd node:<id> <line>" C_RST " Send command to one node only");
    Serial.println("  " C_YLW "mesh log" C_RST "     Show recent mesh traffic log");
    Serial.println();
    Serial.flush(); delay(10);

    Serial.println(C_BLD C_WHT "💡 Light Control:" C_RST);
    Serial.println("  " C_YLW "light off" C_RST "        All OFF");
    Serial.println("  " C_YLW "light on" C_RST "         Turn ON with last values");
    Serial.println("  " C_YLW "light red" C_RST "        Red 100%");
    Serial.println("  " C_YLW "light blue" C_RST "       Blue 100%");
    Serial.println("  " C_YLW "light white" C_RST "      White 100%");
    Serial.println("  " C_YLW "light grow" C_RST "       Grow preset (R70% FR50% B50% W30%)");
    Serial.println("  " C_YLW "light status" C_RST "     Show brightness of each channel");
    Serial.println("  " C_YLW "dim up [+n]" C_RST "      Increase brightness (default +10%)");
    Serial.println("  " C_YLW "dim down [-n]" C_RST "    Decrease brightness");
    Serial.println();
    Serial.println(C_BLD "  Quick Commands (shorthand):" C_RST);
    Serial.println("    " C_YLW "R50" C_RST "     Red 50%     " C_YLW "FR75" C_RST "   Far Red 75%");
    Serial.println("    " C_YLW "B100" C_RST "    Blue 100%    " C_YLW "W30" C_RST "    White 30%");
    Serial.println("    " C_YLW "R50 B100" C_RST " Combined: Red 50% + Blue 100%");
    Serial.println();
    Serial.flush(); delay(10);

    Serial.println(C_BLD C_WHT "⏰ GPIO & Scheduling:" C_RST);
    Serial.println("  " C_YLW "gpio <pin> read" C_RST "  Read digital input");
    Serial.println("  " C_YLW "gpio <pin> 0|1" C_RST "    Set digital output");
    Serial.println("  " C_YLW "gpio <pin> pwm <0-255>" C_RST " PWM output");
    Serial.println("  " C_YLW "gpio <pin> adc" C_RST "    Read ADC (pin 32-39)");
    Serial.println();
    Serial.println("  " C_YLW "timer list" C_RST "      Show GPIO timers");
    Serial.println("  " C_YLW "scenario list" C_RST "   Show light schedules");
    Serial.println();
    Serial.flush(); delay(10);

    Serial.println(C_BLD C_WHT "⚙️  Configuration:" C_RST);
    Serial.println("  " C_YLW "set wifi <SSID> [pass]" C_RST "  Set WiFi credentials");
    Serial.println("  " C_YLW "set name <device>" C_RST "      Device name");
    Serial.println("  " C_YLW "set mqtt <host> [port]" C_RST "  MQTT broker");
    Serial.println("  " C_YLW "set mesh <on|off>" C_RST "    Enable/disable Mesh in config");
    Serial.println("  " C_YLW "set cpu <80|160|240>" C_RST "    CPU frequency (MHz)");
    Serial.println("  " C_YLW "set sensor_interval <sec>" C_RST "  Sensor print interval (1-3600)");
    Serial.println("  " C_YLW "save" C_RST "               Write config to FLASH");
    Serial.println("  " C_YLW "reboot" C_RST "             Restart ESP32");
    Serial.println();
    Serial.flush(); delay(10);

    Serial.println(C_BLD C_WHT "📊 Monitoring:" C_RST);
    Serial.println("  " C_YLW "auto <sec>" C_RST "      Auto-print sensors every N sec");
    Serial.println("  " C_YLW "auto off" C_RST "        Stop auto-print");
    Serial.println("  " C_YLW "monitor" C_RST "         Continuous read (press Enter to exit)");
    Serial.println("  " C_YLW "clock" C_RST "          Show current time & NTP status");
    Serial.println();
    Serial.flush(); delay(10);

    Serial.println(C_BLD C_WHT "💤 System:" C_RST);
    Serial.println("  " C_YLW "reboot" C_RST "          Restart ESP32 (or: " C_YLW "rst" C_RST ")");
    Serial.println("  " C_YLW "json" C_RST "            Show full config JSON");
    Serial.println("  " C_YLW "save" C_RST "            Write config to FLASH");
    Serial.println();

    Serial.println(C_BLD C_WHT "❓ Other:" C_RST);
    Serial.println("  " C_YLW "help" C_RST "           This menu");
    Serial.println("  " C_YLW "url" C_RST "            Show web portal URLs");
    Serial.println("  " C_YLW "mqtt status" C_RST "     MQTT connection status");
    Serial.println();
    
    Serial.println(C_BLD C_CYN "╔════════════════════════════════════════════════════╗" C_RST);
    Serial.println(C_BLD C_CYN "║   Type  " C_YLW "status" C_RST C_BLD C_CYN "  for system overview    " C_RST C_BLD C_CYN "║" C_RST);
    Serial.println(C_BLD C_CYN "╚════════════════════════════════════════════════════╝" C_RST);
    Serial.println();
    Serial.flush();
}

// ─────────────────── status ───────────────────

void SerialConsole::cmdStatus() {
    Serial.println();
    Serial.println(C_BLD C_CYN "╔════════════════════════════════════════════════════╗" C_RST);
    Serial.println(C_BLD C_CYN "║              SYSTEM STATUS                        ║" C_RST);
    Serial.println(C_BLD C_CYN "╚════════════════════════════════════════════════════╝" C_RST);
    Serial.println();

    // Device Info
    if (_cfg) {
        Serial.printf("  " C_BLD "Device:" C_RST "  " C_WHT "%s" C_RST "\n", _cfg->cfg.device_name);
        Serial.printf("  " C_BLD "CPU:" C_RST "     %d MHz  |  Heap: %d bytes  |  Uptime: %u s\n",
            (int)_cfg->cfg.cpu_freq_mhz,
            (int)ESP.getFreeHeap(),
            (unsigned)(millis() / 1000UL));
    }
    Serial.println();

    // ═══ WIFI ═══════════════════════════════════════════
    if (_wifi) {
        bool sta = _wifi->isConnected();
        Serial.println(C_BLD C_WHT "┌─ WiFi" C_RST);
        
        if (sta) {
            Serial.printf("│ " C_GRN "✓" C_RST " STA Connected to: " C_WHT "%s" C_RST "\n", _cfg->cfg.wifi_ssid);
            Serial.printf("│   IP Address: " C_WHT "%s" C_RST "  |  RSSI: " C_WHT "%d dBm" C_RST "\n",
                _wifi->localIP().c_str(), _wifi->rssi());
        } else {
            Serial.printf("│ " C_YLW "○" C_RST " STA Offline (configured SSID: " C_DIM "%s" C_RST ")\n",
                strlen(_cfg->cfg.wifi_ssid) ? _cfg->cfg.wifi_ssid : "(not set)");
        }

        if (_wifi->isAP()) {
            Serial.printf("│ " C_GRN "✓" C_RST " AP      Started on " C_WHT "%s" C_RST " at " C_WHT "%s" C_RST "\n",
                _cfg->cfg.ap_ssid, _wifi->apIP().c_str());
        } else {
            Serial.printf("│ " C_DIM "·" C_RST " AP      (disabled)\n");
        }
        Serial.println("└");
        Serial.println();
    }

    // ═══ BLE ════════════════════════════════════════════
    if (_ble) {
        bool en  = _ble->isEnabled();
        bool con = _ble->isConnected();
        Serial.println(C_BLD C_WHT "┌─ Bluetooth" C_RST);
        
        if (en) {
            if (con) {
                Serial.printf("│ " C_GRN "✓" C_RST " Enabled & Client Connected\n");
            } else {
                Serial.printf("│ " C_YLW "·" C_RST " Enabled (Advertising)\n");
            }
        } else {
            Serial.printf("│ " C_DIM "·" C_RST " Disabled\n");
        }
        Serial.println("└");
        Serial.println();
    }

    // ═══ MESH ═══════════════════════════════════════════
    if (_cfg) {
        bool men = _cfg->cfg.mesh_enabled;
        Serial.println(C_BLD C_WHT "┌─ Mesh" C_RST);
        if (men) {
            if (_mesh) {
                Serial.printf("│ " C_GRN "✓" C_RST " Enabled  | Node: 0x%X  | Peers: %u\n",
                    _mesh->getNodeId(), (unsigned)_mesh->getConnectedCount());
            } else {
                Serial.printf("│ " C_YLW "·" C_RST " Enabled in config (manager unavailable)\n");
            }
        } else {
            Serial.printf("│ " C_DIM "·" C_RST " Disabled\n");
        }
        Serial.println("└");
        Serial.println();
    }

    // ═══ MQTT ═══════════════════════════════════════════
    if (_cfg && strlen(_cfg->cfg.mqtt_host)) {
        Serial.println(C_BLD C_WHT "┌─ MQTT" C_RST);
        Serial.printf("│   Broker: " C_DIM "%s:%d" C_RST "\n",
            _cfg->cfg.mqtt_host, (int)_cfg->cfg.mqtt_port);
        Serial.printf("│   Topic:  " C_DIM "%s" C_RST "\n",
            _cfg->cfg.mqtt_topic);
        Serial.println("└");
        Serial.println();
    }

    // ═══ SENSORS ════════════════════════════════════════
    if (_sensors) {
        int count = 0;
        for (int i = 0; i < MAX_SENSORS; i++) {
            if (_sensors->getSensor(i)) count++;
        }
        Serial.println(C_BLD C_WHT "┌─ Sensors" C_RST);
        Serial.printf("│   Configured: " C_WHT "%d" C_RST "\n", count);
        if (_cfg) Serial.printf("│   Print Interval: " C_WHT "%d sec" C_RST "\n", _cfg->cfg.sensor_interval_s);
        Serial.println("│   (use 'sensors' for detailed readings)");
        Serial.println("└");
        Serial.println();
    }

    // ═══ FIXTURE (LIGHT) ════════════════════════════════
    if (_fixture && _cfg) {
        bool en = _fixture->isEnabled();
        Serial.println(C_BLD C_WHT "┌─ Light Controller" C_RST);
        
        if (en) {
            const FixtureConfig& fc = _cfg->cfg.fixture;
            Serial.printf("│ " C_GRN "✓" C_RST " ON\n");
            Serial.printf("│   R=%3.0f%%  FR=%3.0f%%  B=%3.0f%%  W=%3.0f%%\n",
                fc.red_brightness * 0.5, 
                fc.far_red_brightness * 0.5, 
                fc.blue_brightness * 0.5, 
                fc.white_brightness * 0.5);
        } else {
            Serial.printf("│ " C_DIM "·" C_RST " OFF\n");
        }
        Serial.println("└");
        Serial.println();
    }

}
    
// ─────────────────── sensors ───────────────────

void SerialConsole::cmdSensors() {
    if (!_sensors) { Serial.println(C_RED "  No sensor manager" C_RST); return; }
    hr();
    Serial.println(C_BLD C_CYN "  Sensor Readings" C_RST);
    hr();
    bool any = false;
    for (int i = 0; i < MAX_SENSORS; i++) {
        SensorBase* s = _sensors->getSensor(i);
        if (!s) continue;
        any = true;
        bool ok = s->isReady();
        Serial.printf("  [%d] " C_BLD "%-12s" C_RST " %s\n",
            i, s->typeName(),
            ok ? C_GRN "OK" C_RST : C_RED "ERR" C_RST);
        if (ok) {
            for (uint8_t v = 0; v < s->valueCount(); v++) {
                const SensorValue& sv = s->getValue(v);
                if (!sv.valid) continue;
                Serial.printf("         %-10s : " C_WHT "%.2f" C_RST " %s\n",
                              sv.name, sv.value, sv.unit);
            }
        }
    }
    if (!any) Serial.println("  " C_DIM "(no sensors configured)" C_RST);
    hr();
}

// ─────────────────── config ───────────────────

void SerialConsole::cmdConfig() {
    if (!_cfg) { Serial.println(C_RED "  No config" C_RST); return; }
    JsonDocument doc;
    HubConfig& c = _cfg->cfg;

    // Build compact JSON manually to avoid heap spikes
    Serial.println(C_DIM);
    doc["device"]  = c.device_name;
    doc["wifi_ssid"] = c.wifi_ssid;
    doc["mqtt_host"] = c.mqtt_host;
    doc["mqtt_port"] = c.mqtt_port;
    doc["mqtt_topic"]= c.mqtt_topic;
    doc["ap_ssid"] = c.ap_ssid;
    doc["ble_en"]  = c.ble_enabled;
    doc["ble_name"]= c.ble_name;
    doc["mesh_en"] = c.mesh_enabled;
    doc["cpu_mhz"] = c.cpu_freq_mhz;
    doc["heap"]    = (int)ESP.getFreeHeap();

    JsonArray sensors = doc["sensors"].to<JsonArray>();
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!c.sensors[i].enabled) continue;
        JsonObject so = sensors.add<JsonObject>();
        so["slot"]  = i;
        so["type"]  = (int)c.sensors[i].type;
        so["pin"]   = c.sensors[i].pin;
        so["label"] = c.sensors[i].label;
    }

    serializeJsonPretty(doc, Serial);
    Serial.println(C_RST);
}

// ─────────────────── set ───────────────────

void SerialConsole::cmdSet(const String& args) {
    if (!_cfg) { Serial.println(C_RED "  No config" C_RST); return; }

    String a = args;
    String sub;
    int sp = a.indexOf(' ');
    if (sp > 0) {
        sub = a.substring(0, sp);
        a   = a.substring(sp + 1);
        a.trim();
    } else {
        sub = a;
        a   = "";
    }
    sub.toLowerCase();

    // ── wifi ──
    if (sub == "wifi") {
        String ssid, pass = "";
        int s2 = a.indexOf(' ');
        if (s2 > 0) { ssid = a.substring(0, s2); pass = a.substring(s2 + 1); pass.trim(); }
        else ssid = a;
        ssid.trim();
        if (ssid.length() == 0) { Serial.println(C_RED "  Usage: set wifi <ssid> [pass]" C_RST); return; }
        strlcpy(_cfg->cfg.wifi_ssid, ssid.c_str(), sizeof(_cfg->cfg.wifi_ssid));
        if (pass.length()) strlcpy(_cfg->cfg.wifi_pass, pass.c_str(), sizeof(_cfg->cfg.wifi_pass));
        Serial.printf(C_GRN "  [OK] WiFi SSID='%s' pass_len=%d\n" C_RST,
                      _cfg->cfg.wifi_ssid, (int)strlen(_cfg->cfg.wifi_pass));
        Serial.println(C_YLW "  Type 'save' then 'reboot'" C_RST);
    }
    // ── name ──
    else if (sub == "name") {
        if (!a.length()) { Serial.println(C_RED "  Usage: set name <device name>" C_RST); return; }
        strlcpy(_cfg->cfg.device_name, a.c_str(), sizeof(_cfg->cfg.device_name));
        Serial.printf(C_GRN "  [OK] Device name = '%s'\n" C_RST, _cfg->cfg.device_name);
        Serial.println(C_YLW "  Type 'save' then 'reboot'" C_RST);
    }
    // ── mqtt ──
    else if (sub == "mqtt") {
        String host; int port = 1883;
        int s2 = a.indexOf(' ');
        if (s2 > 0) { host = a.substring(0, s2); port = a.substring(s2 + 1).toInt(); }
        else host = a;
        host.trim();
        if (!host.length()) { Serial.println(C_RED "  Usage: set mqtt <host> [port]" C_RST); return; }
        strlcpy(_cfg->cfg.mqtt_host, host.c_str(), sizeof(_cfg->cfg.mqtt_host));
        _cfg->cfg.mqtt_port = (uint16_t)port;
        Serial.printf(C_GRN "  [OK] MQTT host='%s' port=%d\n" C_RST,
                      _cfg->cfg.mqtt_host, (int)_cfg->cfg.mqtt_port);
        Serial.println(C_YLW "  Type 'save' then 'reboot'" C_RST);
    }
    // ── ble ──
    else if (sub == "ble") {
        String onoff; String name = "";
        int s2 = a.indexOf(' ');
        if (s2 > 0) { onoff = a.substring(0, s2); name = a.substring(s2 + 1); name.trim(); }
        else onoff = a;
        onoff.toLowerCase(); onoff.trim();
        if (onoff == "on" || onoff == "1") {
            _cfg->cfg.ble_enabled = true;
            if (name.length()) strlcpy(_cfg->cfg.ble_name, name.c_str(), sizeof(_cfg->cfg.ble_name));
            Serial.printf(C_GRN "  [OK] BLE enabled, name='%s'\n" C_RST,
                strlen(_cfg->cfg.ble_name) ? _cfg->cfg.ble_name : _cfg->cfg.device_name);
        } else if (onoff == "off" || onoff == "0") {
            _cfg->cfg.ble_enabled = false;
            Serial.println(C_GRN "  [OK] BLE disabled" C_RST);
        } else {
            Serial.println(C_RED "  Usage: set ble on|off [name]" C_RST); return;
        }
        Serial.println(C_YLW "  Type 'save' then 'reboot'" C_RST);
    }
    // ── mesh ──
    else if (sub == "mesh") {
        String onoff = a;
        onoff.toLowerCase();
        onoff.trim();
        if (onoff == "on" || onoff == "1") {
            _cfg->cfg.mesh_enabled = true;
            Serial.println(C_GRN "  [OK] Mesh enabled" C_RST);
        } else if (onoff == "off" || onoff == "0") {
            _cfg->cfg.mesh_enabled = false;
            Serial.println(C_GRN "  [OK] Mesh disabled" C_RST);
        } else {
            Serial.println(C_RED "  Usage: set mesh on|off" C_RST); return;
        }
        Serial.println(C_YLW "  Type 'save' then 'reboot'" C_RST);
    }
    // ── cpu ──
    else if (sub == "cpu") {
        int mhz = a.toInt();
        if (mhz != 80 && mhz != 160 && mhz != 240) {
            Serial.println(C_RED "  Usage: set cpu 80|160|240" C_RST); return;
        }
        _cfg->cfg.cpu_freq_mhz = (uint16_t)mhz;
        Serial.printf(C_GRN "  [OK] CPU freq = %d MHz (active after reboot)\n" C_RST, mhz);
        Serial.println(C_YLW "  Type 'save' then 'reboot'" C_RST);
    }
    // ── sensor_interval ──
    else if (sub == "sensor_interval" || sub == "sensors_interval") {
        int sec = a.toInt();
        if (sec < 1 || sec > 3600) {
            Serial.println(C_RED "  Usage: set sensor_interval <1-3600 sec>" C_RST); return;
        }
        _cfg->cfg.sensor_interval_s = (uint16_t)sec;
        _autoSec = (uint16_t)sec;
        Serial.printf(C_GRN "  [OK] Sensor interval = %d sec\n" C_RST, sec);
        Serial.println(C_YLW "  Type 'save' to persist" C_RST);
    }
    else {
        Serial.printf(C_RED "  Unknown setting: '%s'\n" C_RST, sub.c_str());
        Serial.println("  Available: wifi  name  mqtt  ble  mesh  cpu  sensor_interval");
    }
}

// ─────────────────── gpio ───────────────────

void SerialConsole::cmdGpio(const String& args) {
    int sp = args.indexOf(' ');
    if (sp < 0) { Serial.println(C_RED "  Usage: gpio <pin> read|0|1|adc" C_RST); return; }
    int pin = args.substring(0, sp).toInt();
    String op = args.substring(sp + 1);
    op.trim(); op.toLowerCase();

    if (op == "read") {
        pinMode(pin, INPUT);
        int val = digitalRead(pin);
        Serial.printf("  GPIO%-2d  = " C_WHT "%d" C_RST " (%s)\n", pin, val,
                      val ? "HIGH" : "LOW");
    } else if (op == "0") {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        Serial.printf("  GPIO%-2d  → " C_WHT "LOW" C_RST "\n", pin);
    } else if (op == "1") {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
        Serial.printf("  GPIO%-2d  → " C_WHT "HIGH" C_RST "\n", pin);
    } else if (op == "adc") {
        analogReadResolution(12);
        int raw  = analogRead(pin);
        float v  = raw * 3.3f / 4095.0f;
        Serial.printf("  GPIO%-2d  ADC  raw=" C_WHT "%d" C_RST
                      "  voltage=" C_WHT "%.3f V" C_RST "\n", pin, raw, v);
    } else if (op == "adc2") {
        // Direct ADC2 read with retry — ESP32: pins 0,2,4,12-15,25-27; ESP32-C3: only GPIO5
        adc2_channel_t ch = ADC2_CHANNEL_MAX;
#if defined(CONFIG_IDF_TARGET_ESP32C3)
        if (pin == 5) ch = ADC2_CHANNEL_0;
#else
        switch (pin) {
            case  0: ch = ADC2_CHANNEL_1; break;
            case  2: ch = ADC2_CHANNEL_2; break;
            case  4: ch = ADC2_CHANNEL_0; break;
            case 12: ch = ADC2_CHANNEL_5; break;
            case 13: ch = ADC2_CHANNEL_4; break;
            case 14: ch = ADC2_CHANNEL_6; break;
            case 15: ch = ADC2_CHANNEL_3; break;
            case 25: ch = ADC2_CHANNEL_8; break;
            case 26: ch = ADC2_CHANNEL_9; break;
            case 27: ch = ADC2_CHANNEL_7; break;
            default: break;
        }
#endif
        if (ch == ADC2_CHANNEL_MAX) {
            Serial.printf(C_RED "  GPIO%d is not an ADC2 pin. Use 'adc' instead." C_RST "\n", pin);
            return;
        }
        adc2_config_channel_atten(ch, ADC_ATTEN_DB_12);
        int raw = -1;
        uint32_t t0 = millis();
        esp_err_t err = ESP_ERR_TIMEOUT;
        while (millis() - t0 < 500) {
            err = adc2_get_raw(ch, ADC_WIDTH_BIT_12, &raw);
            if (err == ESP_OK) break;
            delayMicroseconds(500);
        }
        if (err != ESP_OK) {
            Serial.printf(C_RED "  GPIO%d ADC2: timeout — WiFi holding lock. Try 'wifi off' first." C_RST "\n", pin);
            return;
        }
        float v = raw * 3.3f / 4095.0f;
        Serial.printf("  GPIO%-2d  ADC2  raw=" C_WHT "%d" C_RST
                      "  voltage=" C_WHT "%.3f V" C_RST "\n", pin, raw, v);
    } else if (op.startsWith("pwm")) {
        // gpio <pin> pwm <0-255>
        int sp2 = op.indexOf(' ');
        if (sp2 < 0) { Serial.println(C_RED "  Usage: gpio <pin> pwm <0-255>" C_RST); return; }
        int duty = op.substring(sp2 + 1).toInt();
        if (duty < 0) { duty = 0; }
        if (duty > 255) { duty = 255; }
        // Use LEDC channel 0, 5kHz, 8-bit
        ledcSetup(0, 5000, 8);
        ledcAttachPin(pin, 0);
        ledcWrite(0, (uint32_t)duty);
        Serial.printf("  GPIO%-2d  PWM  duty=" C_WHT "%d/255" C_RST " (~%.0f%%)\n",
                      pin, duty, duty / 255.0f * 100.0f);
    } else {
        Serial.printf(C_RED "  Unknown GPIO op: '%s'\n" C_RST, op.c_str());
    }
}

// ─────────────────── scenario ───────────────────

void SerialConsole::cmdScenario(const String& args) {
    if (!_fixture) { Serial.println(C_RED "  No fixture manager" C_RST); return; }
    String a = args;
    a.toLowerCase();
    
    if (a == "list" || a == "") {
        Serial.println(C_BLD C_CYN "  Fixture Scenarios" C_RST);
        hr();
        for (int i = 0; i < MAX_FIXTURE_SCENARIOS; i++) {
            FixtureScenario& sc = _cfg->cfg.fixture.scenarios[i];
            Serial.printf("  [%d] %s %02d:%02d:%02d " C_WHT "R%.0f%% FR%.0f%% B%.0f%% W%.0f%%" C_RST "\n",
                i,
                sc.enabled ? C_GRN "ON " C_RST : C_DIM "OFF" C_RST,
                sc.start_hour, sc.start_minute, sc.start_second,
                sc.red * 0.5, sc.far_red * 0.5, sc.blue * 0.5, sc.white * 0.5);
        }
        hr();
    }
    else if (a.startsWith("enable")) {
        String idxStr = args.substring(6);
        idxStr.trim();
        int idx = idxStr.toInt();
        if (idx < 0 || idx >= MAX_FIXTURE_SCENARIOS) { Serial.println(C_RED "  Invalid index" C_RST); return; }
        _cfg->cfg.fixture.scenarios[idx].enabled = true;
        Serial.printf(C_GRN "  [OK] Scenario %d enabled" C_RST "\n", idx);
    }
    else if (a.startsWith("disable")) {
        String idxStr = args.substring(7);
        idxStr.trim();
        int idx = idxStr.toInt();
        if (idx < 0 || idx >= MAX_FIXTURE_SCENARIOS) { Serial.println(C_RED "  Invalid index" C_RST); return; }
        _cfg->cfg.fixture.scenarios[idx].enabled = false;
        Serial.printf(C_GRN "  [OK] Scenario %d disabled" C_RST "\n", idx);
    }
    else {
        Serial.println(C_RED "  Usage: scenario [list | enable <idx> | disable <idx>]" C_RST);
    }
}

// ─────────────────── timer ───────────────────

void SerialConsole::cmdTimer(const String& args) {
    if (!_fixture) { Serial.println(C_RED "  No fixture manager" C_RST); return; }
    String a = args;
    a.toLowerCase();
    
    if (a == "list" || a == "") {
        Serial.println(C_BLD C_CYN "  Fixture Timers" C_RST);
        hr();
        const char* actions[] = {"OFF","GROW","FULL","RED","BLUE","PULSE_G","PULSE_F","CUSTOM","PULSE_C"};
        for (int i = 0; i < MAX_FIXTURE_TIMERS; i++) {
            FixtureTimerConfig& t = _cfg->cfg.fixture.timers[i];
            bool empty = !t.enabled && t.label[0] == 0;
            if (empty) { Serial.printf("  [%d] %s empty\n", i, C_DIM); continue; }
            Serial.printf("  [%d] %s %02d:%02d:%02d " C_DIM "%s" C_RST " run=%dh%dm label='%s'\n",
                i,
                t.enabled ? C_GRN "ON " C_RST : C_DIM "OFF" C_RST,
                t.hours, t.minutes, t.seconds,
                actions[t.action % 9],
                t.run_hours, t.run_minutes, t.label);
        }
        hr();
    }
    else if (a.startsWith("enable")) {
        String idxStr = args.substring(6);
        idxStr.trim();
        int idx = idxStr.toInt();
        if (idx < 0 || idx >= MAX_FIXTURE_TIMERS) { Serial.println(C_RED "  Invalid index" C_RST); return; }
        _cfg->cfg.fixture.timers[idx].enabled = true;
        Serial.printf(C_GRN "  [OK] Timer %d enabled" C_RST "\n", idx);
    }
    else if (a.startsWith("disable")) {
        String idxStr = args.substring(7);
        idxStr.trim();
        int idx = idxStr.toInt();
        if (idx < 0 || idx >= MAX_FIXTURE_TIMERS) { Serial.println(C_RED "  Invalid index" C_RST); return; }
        _cfg->cfg.fixture.timers[idx].enabled = false;
        Serial.printf(C_GRN "  [OK] Timer %d disabled" C_RST "\n", idx);
    }
    else {
        Serial.println(C_RED "  Usage: timer [list | enable <idx> | disable <idx>]" C_RST);
    }
}

// ─────────────────── dim ───────────────────

void SerialConsole::cmdDim(const String& args) {
    if (!_fixture) { Serial.println(C_RED "  No fixture manager" C_RST); return; }
    if (!_fixture->isEnabled()) { Serial.println(C_RED "  Fixture disabled" C_RST); return; }
    
    String a = args;
    a.toLowerCase();
    int step = 10;
    
    if (a.startsWith("increase") || a.startsWith("up") || a.startsWith("+")) {
        int sp = a.indexOf(' ');
        if (sp > 0) {
            String s = a.substring(sp + 1);
            s.trim();
            step = s.toInt();
        }
        
        uint8_t r = _fixture->getRed(),   fr = _fixture->getFarRed(),
                b = _fixture->getBlue(),  w = _fixture->getWhite();
        if (r > 0 || b > 0 || fr > 0 || w > 0) {
            if (r > 0) r = (r + step > 200) ? 200 : r + step;
            if (fr > 0) fr = (fr + step > 200) ? 200 : fr + step;
            if (b > 0) b = (b + step > 200) ? 200 : b + step;
            if (w > 0) w = (w + step > 200) ? 200 : w + step;
            bool ok = _fixture->setChannels(r, fr, b, w);
            if (ok && _cfg) {
                _cfg->cfg.fixture.red_brightness = (uint8_t)r;
                _cfg->cfg.fixture.far_red_brightness = (uint8_t)fr;
                _cfg->cfg.fixture.blue_brightness = (uint8_t)b;
                _cfg->cfg.fixture.white_brightness = (uint8_t)w;
            }
            Serial.printf(C_GRN "  [BRIGHTEN] R=%.0f%% FR=%.0f%% B=%.0f%% W=%.0f%%" C_RST "\n",
                r*0.5, fr*0.5, b*0.5, w*0.5);
        } else {
            Serial.println(C_YLW "  Light is OFF — turn it on first" C_RST);
        }
    }
    else if (a.startsWith("decrease") || a.startsWith("down") || a.startsWith("-")) {
        int sp = a.indexOf(' ');
        if (sp > 0) {
            String s = a.substring(sp + 1);
            s.trim();
            step = s.toInt();
        }
        
        uint8_t r = _fixture->getRed(),   fr = _fixture->getFarRed(),
                b = _fixture->getBlue(),  w = _fixture->getWhite();
        if (r > 0 || b > 0 || fr > 0 || w > 0) {
            if (r > 0) r = (r < step) ? 0 : r - step;
            if (fr > 0) fr = (fr < step) ? 0 : fr - step;
            if (b > 0) b = (b < step) ? 0 : b - step;
            if (w > 0) w = (w < step) ? 0 : w - step;
            bool ok = _fixture->setChannels(r, fr, b, w);
            if (ok && _cfg) {
                _cfg->cfg.fixture.red_brightness = (uint8_t)r;
                _cfg->cfg.fixture.far_red_brightness = (uint8_t)fr;
                _cfg->cfg.fixture.blue_brightness = (uint8_t)b;
                _cfg->cfg.fixture.white_brightness = (uint8_t)w;
            }
            Serial.printf(C_GRN "  [DARKEN] R=%.0f%% FR=%.0f%% B=%.0f%% W=%.0f%%" C_RST "\n",
                r*0.5, fr*0.5, b*0.5, w*0.5);
        } else {
            Serial.println(C_YLW "  Light is already OFF" C_RST);
        }
    }
    else {
        Serial.println(C_RED "  Usage: dim increase|up [<step>]  or  dim decrease|down [<step>]" C_RST);
    }
}

// ─────────────────── clock ───────────────────

void SerialConsole::cmdClock() {
    Serial.println(C_BLD C_CYN "  System Clock Status" C_RST);
    hr();
    
    struct tm timeinfo;
    systemClock.getLocalTime(&timeinfo);
    
    Serial.printf("  Current Time     : " C_WHT "%02d:%02d:%02d" C_RST " (%04d-%02d-%02d)\n",
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    
    if (systemClock.isSyncedWithNTP()) {
        uint32_t sinceSyncMs = systemClock.timeSinceSync();
        uint32_t sinceSyncSec = sinceSyncMs / 1000;
        Serial.printf("  NTP Status       : " C_GRN "SYNCED" C_RST "\n");
        Serial.printf("  Time Since Sync  : " C_WHT "%u seconds" C_RST " (%u ms drift possible)\n",
                      (unsigned)sinceSyncSec, (unsigned)(sinceSyncMs % 1000));
    } else {
        uint16_t days = systemClock.getBackupDays();
        Serial.printf("  NTP Status       : " C_YLW "NOT SYNCED - BACKUP TIMER ACTIVE" C_RST "\n");
        Serial.printf("  Backup Time      : " C_WHT "%ud %02d:%02d:%02d" C_RST " (uptime based)\n",
                      days, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        Serial.printf("  Time Source      : " C_DIM "Internal millis() timer (no internet)" C_RST "\n");
        Serial.printf("  Note             : Scenarios will work, but time will reset on reboot\n");
    }
    
    hr();
}

// ─────────────────── mqtt ───────────────────

void SerialConsole::cmdMqtt(const String& args) {
    if (!_cfg) { Serial.println(C_RED "  No config" C_RST); return; }
    String a = args;
    a.toLowerCase();
    
    if (a == "status" || a == "") {
        Serial.println(C_BLD C_CYN "  MQTT Configuration" C_RST);
        hr();
        Serial.printf("  Host     : " C_WHT "%s" C_RST "\n",
            strlen(_cfg->cfg.mqtt_host) ? _cfg->cfg.mqtt_host : C_DIM "(not set)");
        Serial.printf("  Port     : " C_WHT "%d" C_RST "\n", _cfg->cfg.mqtt_port);
        Serial.printf("  Topic    : " C_WHT "%s" C_RST "\n", _cfg->cfg.mqtt_topic);
        Serial.printf("  Interval : " C_WHT "%d sec" C_RST "\n", _cfg->cfg.mqtt_interval_s);
        hr();
    }
    else if (a.startsWith("host")) {
        String host = args.substring(4);
        host.trim();
        if (!host.length()) { Serial.println(C_RED "  Usage: mqtt host <hostname>" C_RST); return; }
        strlcpy(_cfg->cfg.mqtt_host, host.c_str(), sizeof(_cfg->cfg.mqtt_host));
        Serial.printf(C_GRN "  [OK] MQTT host = '%s'\n" C_RST, host.c_str());
    }
    else if (a.startsWith("port")) {
        String portStr = args.substring(4);
        portStr.trim();
        int port = portStr.toInt();
        if (port <= 0 || port > 65535) { Serial.println(C_RED "  Usage: mqtt port <1-65535>" C_RST); return; }
        _cfg->cfg.mqtt_port = (uint16_t)port;
        Serial.printf(C_GRN "  [OK] MQTT port = %d\n" C_RST, port);
    }
    else if (a.startsWith("interval")) {
        String secStr = args.substring(8);
        secStr.trim();
        int sec = secStr.toInt();
        if (sec <= 0 || sec > 3600) { Serial.println(C_RED "  Usage: mqtt interval <1-3600 sec>" C_RST); return; }
        _cfg->cfg.mqtt_interval_s = (uint16_t)sec;
        Serial.printf(C_GRN "  [OK] MQTT interval = %d sec\n" C_RST, sec);
    }
    else {
        Serial.println(C_RED "  Usage: mqtt [status | host <h> | port <p> | interval <s>]" C_RST);
    }
}

// ─────────────────── json ───────────────────

void SerialConsole::cmdJson() {
    Serial.println();
    cmdConfig();
}

// ─────────────────── banner / helpers ───────────────────

void SerialConsole::printBanner() {
    Serial.println();
    Serial.println(C_BLD C_CYN "╔═══════════════════════════════════════════╗" C_RST);
    Serial.println(C_BLD C_CYN "║     ESP-HUB  —  Serial Console            ║" C_RST);
    Serial.println(C_BLD C_CYN "╠═══════════════════════════════════════════╣" C_RST);
        String ap = (_wifi) ? _wifi->apIP() : String("192.168.4.1");
        if (ap.length() == 0) ap = "192.168.4.1";
        const char* apName = "ESP-HUB";
        if (_cfg) {
            apName = (_cfg->cfg.mesh_enabled && strlen(_cfg->cfg.mesh_ssid) > 0)
                ? _cfg->cfg.mesh_ssid
                : _cfg->cfg.ap_ssid;
        }
        Serial.printf(C_DIM "║  Web UI: " C_WHT "http://%s/ " C_DIM " (AP: %s)" C_DIM "  ║\n" C_RST,
            ap.c_str(), apName);
    if (_wifi && _wifi->isConnected())
        Serial.printf(C_DIM "║         " C_WHT "http://%s/ " C_DIM "                   ║\n" C_RST, _wifi->localIP().c_str());
    else
        Serial.println(C_DIM "║                                           ║" C_RST);
    Serial.println(C_BLD C_CYN "╚═══════════════════════════════════════════╝" C_RST);
    Serial.println();
    Serial.println("  Type " C_YLW "help" C_RST " for commands, " C_YLW "light help" C_RST " for light control.");
    hr();
    prompt();
    Serial.flush();
}

void SerialConsole::hr() {
    Serial.println(C_DIM "  ═══════════════════════════════════════════" C_RST);
}

void SerialConsole::prompt() {
    Serial.print(C_CYN "\n> " C_RST);
    Serial.flush();
}
