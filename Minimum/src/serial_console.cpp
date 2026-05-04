#include "serial_console.h"

// Forward declarations for Inventronics helpers (defined later in this file)
static bool setInvSpectrum(uint8_t red, uint8_t farRed, uint8_t blue, uint8_t white);
static bool sendInvCommand(uint8_t red, uint8_t farRed, uint8_t blue, uint8_t white);
static bool readInvResponse();

#if SC_ANSI
  #define C_RST  "\033[0m"
  #define C_BLD  "\033[1m"
  #define C_CYN  "\033[36m"
  #define C_GRN  "\033[32m"
  #define C_YLW  "\033[33m"
  #define C_RED  "\033[31m"
#else
  #define C_RST  ""
  #define C_BLD  ""
  #define C_CYN  ""
  #define C_GRN  ""
  #define C_YLW  ""
  #define C_RED  ""
#endif

void SerialConsole::begin(ConfigManager*  cfg,
                       WiFiManager*   wifi,
                       void* sensors,
                       void* ble,
                       FixtureManager* fixture,
                       MeshManager*   mesh) {
    _cfg     = cfg;
    _wifi    = wifi;
    _fixture = fixture;
    _mesh    = mesh;
    _lastAuto = 0;
    _buf.reserve(128);
    _autoSec = 0;
    _autoLast = millis();

    if (!_initialized) {
        _initialized = true;
        printBanner();
    }
}

void SerialConsole::tick() {
    if (_autoSec > 0 && _initialized) {
        uint32_t now = millis();
        if (now - _autoLast >= (uint32_t)_autoSec * 1000UL) {
            _autoLast = now;
            cmdStatus();
            prompt();
        }
    }

    while (Serial.available()) {
        char c = (char)Serial.read();
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
            if (_buf.length()) _buf.remove(_buf.length() - 1);
        } else {
            if (!_initialized) { _initialized = true; printBanner(); }
            _buf += c;
        }
    }
}

void SerialConsole::executeCommand(const String& line, bool fromMesh) {
    String cmd = line;
    cmd.trim();
    if (cmd.length() == 0) return;
    processLine(cmd, fromMesh);
}

void SerialConsole::processLine(const String& line, bool fromMesh) {
    if (_mesh && _cfg && _cfg->cfg.mesh_enabled && line.length() > 0 && !fromMesh) {
        String rec = "EXEC LOCAL role=PEER node=" + String(_mesh->getNodeId()) + " cmd=" + line;
        _mesh->addLogEntry(rec);
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

    // --- Direct Inventronics Commands ---
    String sCmd = line;
    sCmd.trim();
    sCmd.toUpperCase();

    if (sCmd == "OFF") {
        Serial.println("[CMD] All OFF");
        setInvSpectrum(0, 0, 0, 0);
        return;
    }
    if (sCmd == "RED") {
        Serial.println("[CMD] Red 100%");
        setInvSpectrum(200, 0, 0, 0);
        return;
    }
    if (sCmd == "FARRED") {
        Serial.println("[CMD] Far Red 100%");
        setInvSpectrum(0, 200, 0, 0);
        return;
    }
    if (sCmd == "BLUE") {
        Serial.println("[CMD] Blue 100%");
        setInvSpectrum(0, 0, 200, 0);
        return;
    }
    if (sCmd == "WHITE") {
        Serial.println("[CMD] White 100%");
        setInvSpectrum(0, 0, 0, 200);
        return;
    }
    if (sCmd == "FULL") {
        Serial.println("[CMD] All channels 100%");
        setInvSpectrum(200, 200, 200, 200);
        return;
    }
    if (sCmd == "GROW") {
        Serial.println("[CMD] Grow preset: R70% FR50% B50% W30%");
        setInvSpectrum(140, 100, 100, 60);
        return;
    }
    if (sCmd == "DEMO") {
        Serial.println("[CMD] Running demo sequence...");
        Serial.println("--- Demo: All OFF ---"); setInvSpectrum(0, 0, 0, 0); delay(2000);
        Serial.println("--- Demo: Red 100% ---"); setInvSpectrum(200, 0, 0, 0); delay(2000);
        Serial.println("--- Demo: Far Red 100% ---"); setInvSpectrum(0, 200, 0, 0); delay(2000);
        Serial.println("--- Demo: Blue 100% ---"); setInvSpectrum(0, 0, 200, 0); delay(2000);
        Serial.println("--- Demo: White 100% ---"); setInvSpectrum(0, 0, 0, 200); delay(2000);
        Serial.println("--- Demo: Red + Far Red 100% ---"); setInvSpectrum(200, 200, 0, 0); delay(2000);
        Serial.println("--- Demo: Grow (R70 FR50 B50 W30) ---"); setInvSpectrum(140, 100, 100, 60); delay(2000);
        Serial.println("--- Demo: Full 100% ---"); setInvSpectrum(200, 200, 200, 200); delay(2000);
        Serial.println("--- Demo: All OFF ---"); setInvSpectrum(0, 0, 0, 0);
        Serial.println("--- Demo complete ---");
        return;
    }

    if (sCmd.indexOf("R") >= 0 || sCmd.indexOf("B") >= 0 || sCmd.indexOf("W") >= 0) {
        int r = 0, fr = 0, b = 0, w = 0;
        bool parsed = false;

        // Simplify extraction: support "RED 10" or "R 10" or "R10"
        String extract = sCmd;
        extract.replace("FARRED", "F"); // Map FarRed explicitly to F
        extract.replace("FAR RED", "F");
        extract.replace("FR", "F");     // Map FR to F
        extract.replace("RED", "R");
        extract.replace("BLUE", "B");
        extract.replace("WHITE", "W");

        // Now extract numbers behind F, R, B, W
        int pos = extract.indexOf('F');
        if (pos >= 0) { fr = extract.substring(pos + 1).toInt(); parsed = true; }
        
        pos = extract.indexOf('R');
        if (pos >= 0) { r = extract.substring(pos + 1).toInt(); parsed = true; }
        
        pos = extract.indexOf('B');
        if (pos >= 0) { b = extract.substring(pos + 1).toInt(); parsed = true; }
        
        pos = extract.indexOf('W');
        if (pos >= 0) { w = extract.substring(pos + 1).toInt(); parsed = true; }
        
        if (parsed) {
            Serial.print(C_GRN "[CMD] Inv: R=" C_RST); Serial.print(r);
            Serial.print(C_GRN " FR=" C_RST); Serial.print(fr);
            Serial.print(C_GRN " B=" C_RST); Serial.print(b);
            Serial.print(C_GRN " W=" C_RST); Serial.println(w);
            setInvSpectrum((uint8_t)r, (uint8_t)fr, (uint8_t)b, (uint8_t)w);
            return;
        }
    }

    if (_fixture && _fixture->isEnabled()) {
        if (parseLightCommand(line)) return;
    }

    if      (cmd == "help"   || cmd == "?")     cmdHelp();
    else if (cmd == "status" || cmd == "s")     cmdStatus();
    else if (cmd == "config" || cmd == "cfg")   cmdConfig();
    else if (cmd == "set")                      cmdSet(args);
    else if (cmd == "save")  {
        if (_cfg) { _cfg->save(); Serial.println(C_GRN "[OK] Config saved" C_RST); }
    }
    else if (cmd == "wifi")                     cmdWifi(args);
    else if (cmd == "mesh")                     cmdMesh(args);
    else if (cmd == "light" || cmd == "l")      cmdLight(args);
    else if (cmd == "scenario" || cmd == "sc")  cmdScenario(args);
    else if (cmd == "timer" || cmd == "tm")     cmdTimer(args);
    else if (cmd == "dim")                      cmdDim(args);
    else if (cmd == "reboot" || cmd == "restart" || cmd == "rst") {
        Serial.println(C_YLW "Rebooting..." C_RST);
        delay(200);
        ESP.restart();
    }
    else {
        Serial.printf(C_RED "Unknown command: %s" C_RST "\n", cmd.c_str());
    }
}

bool SerialConsole::parseLightCommand(const String& line) {
    String s = line;
    s.trim();
    s.toLowerCase();
    
    if (s == "off") {
        return _fixture->setChannels(0, 0, 0, 0);
    }
    if (s == "red") {
        return _fixture->setPreset(3);
    }
    if (s == "blue") {
        return _fixture->setPreset(4);
    }
    if (s == "white") {
        return _fixture->setPreset(2);
    }
    if (s == "full") {
        return _fixture->setPreset(2);
    }
    if (s == "grow") {
        return _fixture->setPreset(1);
    }
    if (s == "demo" || s == "test") {
        _fixture->runDemo();
        return true;
    }

    if (s.length() >= 2) {
        char prefix = s[0];
        uint8_t val = 0;
        if (s.substring(1).toInt() > 0) {
            val = (uint8_t)s.substring(1).toInt();
        }
        
        if (prefix == 'r' || prefix == 'R') {
            return _fixture->setChannels(val, _fixture->getFarRed(), _fixture->getBlue(), _fixture->getWhite());
        }
        if (prefix == 'f' || prefix == 'F') {
            return _fixture->setChannels(_fixture->getRed(), val, _fixture->getBlue(), _fixture->getWhite());
        }
        if (prefix == 'b' || prefix == 'B') {
            return _fixture->setChannels(_fixture->getRed(), _fixture->getFarRed(), val, _fixture->getWhite());
        }
        if (prefix == 'w' || prefix == 'W') {
            return _fixture->setChannels(_fixture->getRed(), _fixture->getFarRed(), _fixture->getBlue(), val);
        }
    }
    return false;
}

void SerialConsole::cmdHelp() {
    hr();
    Serial.println(F("  ======= COMMANDS ======="));
    Serial.println(F("  help, ?    - This help"));
    Serial.println(F("  status, s  - Show status"));
    Serial.println(F("  config     - Show config"));
    Serial.println(F("  set <k>=<v> - Set config"));
    Serial.println(F("  save       - Save config"));
    Serial.println(F("  wifi       - WiFi control"));
    Serial.println(F("  mesh       - Mesh control"));
    Serial.println(F("  light      - Light control"));
    Serial.println(F("  l <cmd>    - Light short"));
    Serial.println(F("  scenario   - Scene control"));
    Serial.println(F("  timer      - Timer control"));
    Serial.println(F("  dim        - Dimmer control"));
    Serial.println(F("  reboot     - Reboot device"));
    Serial.println(F("  INVENTRONICS Commands:"));
    Serial.println(F("  OFF, RED, FARRED, BLUE, WHITE, FULL, GROW"));
    Serial.println(F("  R<n> FR<n> B<n> W<n> - Custom (0-200)"));
    Serial.println(F("  DEMO - Run demo sequence"));
    Serial.println(F("  ======================="));
    prompt();
}

void SerialConsole::cmdStatus() {
    hr();
    Serial.println(F("  ======= STATUS ======="));
    
    if (_wifi) {
        Serial.printf("  WiFi:   %s IP: %s\n", 
            _wifi->isConnected() ? "CONNECTED" : "AP MODE",
            _wifi->isAP() ? _wifi->apIP().c_str() : _wifi->localIP().c_str());
    }
    
    if (_mesh && _cfg && _cfg->cfg.mesh_enabled) {
        Serial.printf("  Mesh:   %s nodes=%u\n",
            _mesh->isConnected() ? "CONNECTED" : "DISCONNECTED",
            (unsigned)_mesh->getConnectedCount());
    }
    
    if (_fixture && _fixture->isEnabled()) {
        Serial.printf("  Light:  R=%d FR=%d B=%d W=%d\n",
            _fixture->getRed(),
            _fixture->getFarRed(),
            _fixture->getBlue(),
            _fixture->getWhite());
    }
    
    Serial.println(F("  ===================="));
}

void SerialConsole::cmdConfig() {
    hr();
    if (!_cfg) return;
    Serial.println(F("  ======= CONFIG ======"));
    Serial.printf("  Device:  %s\n", _cfg->cfg.device_name);
    Serial.printf("  Mesh:   %s ssid='%s'\n", 
        _cfg->cfg.mesh_enabled ? "ON" : "OFF",
        _cfg->cfg.mesh_ssid);
    Serial.printf("  Fixture: %s\n", 
        _cfg->cfg.fixture.enabled ? "ON" : "OFF");
    Serial.println(F("  ==================="));
}

void SerialConsole::cmdSet(const String& args) {
    if (!_cfg || args.length() == 0) {
        Serial.println(C_RED "Usage: set <key>=<value>" C_RST);
        return;
    }
    
    int eq = args.indexOf('=');
    if (eq <= 0) {
        Serial.println(C_RED "Invalid format. Use key=value" C_RST);
        return;
    }
    
    String key = args.substring(0, eq);
    String val = args.substring(eq + 1);
    key.trim();
    val.trim();
    key.toLowerCase();
    
    if (key == "mesh") {
        _cfg->cfg.mesh_enabled = (val == "1" || val == "on" || val == "true");
        Serial.printf(C_GRN "[OK] mesh=%s" C_RST "\n", _cfg->cfg.mesh_enabled ? "enabled" : "disabled");
    } else if (key == "fixture" || key == "light") {
        _cfg->cfg.fixture.enabled = (val == "1" || val == "on" || val == "true");
        if (_cfg->cfg.fixture.enabled) _fixture->enable(true);
        Serial.printf(C_GRN "[OK] fixture=%s" C_RST "\n", _cfg->cfg.fixture.enabled ? "enabled" : "disabled");
    } else {
        Serial.printf(C_YLW "Unknown key: %s" C_RST "\n", key.c_str());
    }
}

void SerialConsole::cmdWifi(const String& args) {
    if (!_wifi) return;
    String a = args;
    a.trim();
    a.toLowerCase();
    
    if (a == "status" || a == "") {
        Serial.printf("WiFi: %s\n  IP: %s\n  AP: %s\n",
            _wifi->isConnected() ? "CONNECTED" : "DISCONNECTED",
            _wifi->localIP().c_str(),
            _wifi->apIP().c_str());
    } else {
        Serial.printf(C_YLW "Use 'wifi status'" C_RST "\n");
    }
}

void SerialConsole::cmdMesh(const String& args) {
    if (!_mesh || !_cfg) return;
    String a = args;
    a.trim();
    a.toLowerCase();
    
    if (a == "status" || a == "") {
        Serial.printf("Mesh: %s\n  Nodes: %u\n  IP: %s\n  ID: %u\n",
            _mesh->isConnected() ? "CONNECTED" : "DISCONNECTED",
            (unsigned)_mesh->getConnectedCount(),
            _mesh->getMeshIP().c_str(),
            (unsigned)_mesh->getNodeId());
    } else if (a == "nodes") {
        Serial.println(F("Nodes:"));
        Serial.println(_mesh->getNodeListJson());
    } else if (a == "log") {
        Serial.println(_mesh->getLogJson(10));
    } else if (a == "clear") {
        _mesh->clearLog();
        Serial.println(C_GRN "[OK] Log cleared" C_RST);
    } else {
        Serial.printf(C_YLW "Use: mesh status|nodes|log|clear" C_RST "\n");
    }
}

void SerialConsole::cmdLight(const String& args) {
    if (!_fixture || !_fixture->isEnabled()) {
        Serial.println(C_RED "Fixture not enabled" C_RST);
        return;
    }
    
    String a = args;
    a.trim();
    String cmd = a;
    cmd.toLowerCase();
    
    // Inventronics DEMO command
    if (cmd == "demo" || cmd == "test") {
        Serial.println(C_GRN "--- Inv Demo: All OFF ---" C_RST);
        setInvSpectrum(0, 0, 0, 0);
        delay(2000);
        
        Serial.println(C_GRN "--- Inv Demo: Red 100% ---" C_RST);
        setInvSpectrum(INV_BRIGHTNESS_100, 0, 0, 0);
        delay(2000);
        
        Serial.println(C_GRN "--- Inv Demo: Far Red 100% ---" C_RST);
        setInvSpectrum(0, INV_BRIGHTNESS_100, 0, 0);
        delay(2000);
        
        Serial.println(C_GRN "--- Inv Demo: Blue 100% ---" C_RST);
        setInvSpectrum(0, 0, INV_BRIGHTNESS_100, 0);
        delay(2000);
        
        Serial.println(C_GRN "--- Inv Demo: White 100% ---" C_RST);
        setInvSpectrum(0, 0, 0, INV_BRIGHTNESS_100);
        delay(2000);
        
        Serial.println(C_GRN "--- Inv Demo: Grow (R70 FR50 B50 W30) ---" C_RST);
        setInvSpectrum(140, 100, 100, 60);
        delay(2000);
        
        Serial.println(C_GRN "--- Inv Demo: Full 100% ---" C_RST);
        setInvSpectrum(INV_BRIGHTNESS_100, INV_BRIGHTNESS_100, INV_BRIGHTNESS_100, INV_BRIGHTNESS_100);
        delay(2000);
        
        Serial.println(C_GRN "--- Inv Demo: All OFF ---" C_RST);
        setInvSpectrum(0, 0, 0, 0);
        
        Serial.println(C_GRN "--- Demo complete ---" C_RST);
        return;
    }
    
    // Inventronics HELP
    if (cmd == "help") {
        Serial.println(F("=== Inventronics Commands ==="));
        Serial.println(F("  OFF       - All channels off"));
        Serial.println(F("  RED       - Red 100%"));
        Serial.println(F("  FARRED    - Far Red 100%"));
        Serial.println(F("  BLUE      - Blue 100%"));
        Serial.println(F("  WHITE     - White 100%"));
        Serial.println(F("  FULL      - All channels 100%"));
        Serial.println(F("  GROW      - Grow preset (R70 FR50 B50 W30)"));
        Serial.println(F("  R<n> FR<n> B<n> W<n> - Custom (0-200)"));
        Serial.println(F("  Example: R200 FR0 B100 W60"));
        Serial.println(F("  DEMO      - Run demo sequence"));
        return;
    }
    
    // Check if there's a value argument (e.g., "red 10", "blue 50")
    int sp = a.indexOf(' ');
    if (sp > 0) {
        String subCmd = a.substring(0, sp);
        String valStr = a.substring(sp + 1);
        subCmd.toLowerCase();
        uint8_t val = (uint8_t)constrain(valStr.toInt(), 0, 200);
        
        if (subCmd == "red") {
            setInvSpectrum(val, 0, 0, 0);
            Serial.printf(C_GRN "[OK] Red=%d%%" C_RST "\n", val * 0.5);
            return;
        }
        if (subCmd == "farred" || subCmd == "fr") {
            setInvSpectrum(0, val, 0, 0);
            Serial.printf(C_GRN "[OK] FarRed=%d%%" C_RST "\n", val * 0.5);
            return;
        }
        if (subCmd == "blue") {
            setInvSpectrum(0, 0, val, 0);
            Serial.printf(C_GRN "[OK] Blue=%d%%" C_RST "\n", val * 0.5);
            return;
        }
        if (subCmd == "white") {
            setInvSpectrum(0, 0, 0, val);
            Serial.printf(C_GRN "[OK] White=%d%%" C_RST "\n", val * 0.5);
            return;
        }
    }
    
    a.toLowerCase();
    
    if (a == "off") {
        setInvSpectrum(0, 0, 0, 0);
    } else if (a == "red") {
        setInvSpectrum(INV_BRIGHTNESS_100, 0, 0, 0);
    } else if (a == "farred") {
        setInvSpectrum(0, INV_BRIGHTNESS_100, 0, 0);
    } else if (a == "blue") {
        setInvSpectrum(0, 0, INV_BRIGHTNESS_100, 0);
    } else if (a == "white") {
        setInvSpectrum(0, 0, 0, INV_BRIGHTNESS_100);
    } else if (a == "full") {
        setInvSpectrum(INV_BRIGHTNESS_100, INV_BRIGHTNESS_100, INV_BRIGHTNESS_100, INV_BRIGHTNESS_100);
    } else if (a == "grow") {
        setInvSpectrum(140, 100, 100, 60);
    } else if (a == "status") {
        Serial.printf("R=%d FR=%d B=%d W=%d\n",
            _fixture->getRed(),
            _fixture->getFarRed(),
            _fixture->getBlue(),
            _fixture->getWhite());
    } else {
        Serial.printf(C_YLW "Use: light on|off|grow|full|red|blue|demo|status OR red <0-200>|blue <0-200>|..." C_RST "\n");
    }
}

void SerialConsole::cmdScenario(const String& args) {
    Serial.printf(C_YLW "Scenarios not implemented in minimal version" C_RST "\n");
}

void SerialConsole::cmdTimer(const String& args) {
    Serial.printf(C_YLW "Timers not implemented in minimal version" C_RST "\n");
}

void SerialConsole::cmdDim(const String& args) {
    if (!_fixture || !_fixture->isEnabled()) {
        Serial.println(C_RED "Fixture not enabled" C_RST);
        return;
    }
    
    String a = args;
    a.trim();
    a.toLowerCase();
    
    int delta = 0;
    if (a == "increase" || a == "up") {
        delta = 20;
    } else if (a == "decrease" || a == "down") {
        delta = -20;
    } else if (a.length() > 0) {
        delta = a.toInt();
    }
    
    if (delta != 0) {
        uint8_t r = _fixture->getRed();
        uint8_t fr = _fixture->getFarRed();
        uint8_t b = _fixture->getBlue();
        uint8_t w = _fixture->getWhite();
        
        r = (uint8_t)constrain((int)r + delta, 0, 200);
        fr = (uint8_t)constrain((int)fr + delta, 0, 200);
        b = (uint8_t)constrain((int)b + delta, 0, 200);
        w = (uint8_t)constrain((int)w + delta, 0, 200);
        
        _fixture->setChannels(r, fr, b, w);
        Serial.printf(C_GRN "[OK] dim %d: R=%d FR=%d B=%d W=%d" C_RST "\n",
            delta, r, fr, b, w);
    } else {
        Serial.printf(C_YLW "Use: dim increase|decrease|<step>" C_RST "\n");
    }
}

void SerialConsole::cmdReboot() {
    Serial.println(C_YLW "Rebooting..." C_RST);
    delay(200);
    ESP.restart();
}

void SerialConsole::printBanner() {
    Serial.println(F("\033[36m" 
        "  ______  _____  _____       _    _  _    _  ____   \n"
        " |  ____|/ ____||  __ \\     | |  | || |  | ||  _ \\  \n"
        " | |__  | (___  | |__) |____| |__| || |  | || |_) | \n"
        " |  __|  \\___ \\ |  ___/_____|  __  || |  | ||  _ <  \n"
        " | |____ ____) || |         | |  | || |__| || |_) | \n"
        " |______|_____/ |_|         |_|  |_| \\____/ |____/  \n"
        "\033[0m"
        "\033[92m  Minimal Hub\033[0m - Fixtures + Mesh + Console"));
    Serial.println(F("Type 'help' for commands."));
    hr();
}

void SerialConsole::prompt() {
    Serial.print(C_CYN);
    Serial.print("> ");
    Serial.print(C_RST);
}

// =============================================================================
// Inventronics NSM-1k2q200mg Protocol Support
// =============================================================================

static uint8_t calculateInvChecksum(uint8_t cmd, uint8_t offset, uint8_t length,
                                    uint8_t mask, uint8_t ch1, uint8_t ch2, uint8_t ch3,
                                    uint8_t ch4) {
    uint16_t sum = cmd + offset + length + mask + ch1 + ch2 + ch3 + ch4;
    return (uint8_t)(sum & 0xFF);
}

static bool sendInvCommand(uint8_t red, uint8_t farRed, uint8_t blue, uint8_t white) {
    // Limit values to 0-200
    red = (uint8_t)constrain(red, 0, 200);
    farRed = (uint8_t)constrain(farRed, 0, 200);
    blue = (uint8_t)constrain(blue, 0, 200);
    white = (uint8_t)constrain(white, 0, 200);
    
    uint8_t checksum = calculateInvChecksum(INV_CMD_SET, INV_OFFSET_MULTI, INV_DATA_LENGTH,
                                            INV_CHANNEL_MASK, red, blue, farRed, white);
    
    // Build frame (12 bytes)
    uint8_t frame[12] = {
        INV_FRAME_HEADER,     // 0x3A
        INV_CMD_SET,          // 0x3C
        INV_OFFSET_MULTI,     // 0xEE
        INV_DATA_LENGTH,      // 0x05
        INV_CHANNEL_MASK,     // 0x0F
        red,                  // CH1: Red
        blue,                 // CH2: Blue
        farRed,               // CH3: Far Red
        white,                // CH4: White
        checksum,             // Checksum
        INV_FRAME_END_CR,     // 0x0D
        INV_FRAME_END_LF      // 0x0A
    };
    
    // Send via Serial1 (ESP32-C3 UART1)
    Serial1.write(frame, 12);
    Serial1.flush();
    
    // Debug output
    Serial.print(C_CYN "[TX] " C_RST);
    for (int i = 0; i < 12; i++) {
        if (frame[i] < 0x10) Serial.print("0");
        Serial.print(frame[i], HEX);
        Serial.print(" ");
    }
    Serial.print(" | R="); Serial.print(red * 0.5, 1); Serial.print("% ");
    Serial.print("FR="); Serial.print(farRed * 0.5, 1); Serial.print("% ");
    Serial.print("B="); Serial.print(blue * 0.5, 1); Serial.print("% ");
    Serial.print("W="); Serial.print(white * 0.5, 1); Serial.println("%");
    
    return true;
}

static bool readInvResponse() {
    unsigned long startTime = millis();
    uint8_t response[8];
    int idx = 0;
    
    while (millis() - startTime < INV_RESPONSE_TIMEOUT_MS) {
        if (Serial1.available()) {
            uint8_t b = Serial1.read();
            if (idx == 0 && b != INV_FRAME_HEADER) continue;
            response[idx++] = b;
            if (idx >= 8) break;
        }
        yield();  // Feed watchdog during timeout wait
    }
    
    if (idx >= 8) {
        Serial.print(C_CYN "[RX] " C_RST);
        for (int i = 0; i < 8; i++) {
            if (response[i] < 0x10) Serial.print("0");
            Serial.print(response[i], HEX);
            Serial.print(" ");
        }
        
        if (response[1] == INV_CMD_SET_RESP && response[4] == INV_ACK_OK) {
            Serial.println(C_GRN "-> ACK OK" C_RST);
            return true;
        } else {
            Serial.println(C_RED "-> ACK FAIL" C_RST);
            return false;
        }
    }
    
    Serial.println(C_RED "[RX] Timeout - no response" C_RST);
    return false;
}

static bool setInvSpectrum(uint8_t red, uint8_t farRed, uint8_t blue, uint8_t white) {
    sendInvCommand(red, farRed, blue, white);
    bool ack = readInvResponse();
    delay(INV_CMD_INTERVAL_MS);
    return ack;
}

void SerialConsole::hr() {
    Serial.println(F("------------------------"));
}

const char* SerialConsole::col(const char* code) {
    return code;
}

String SerialConsole::padR(const String& s, int w) {
    String r = s;
    while ((int)r.length() < w) r += ' ';
    return r;
}