#pragma once
#include <Arduino.h>
#include <WiFiUdp.h>

// Selective DNS proxy for captive portal + internet coexistence.
//
//  Wildcard mode  (upstream = 0.0.0.0, default):
//    All A-record queries → apIP.  Used when there is no STA internet.
//
//  Forward mode   (upstream = e.g. 1.1.1.1):
//    Captive-portal probe hosts → apIP  (triggers OS "Sign In" notification).
//    All other queries         → forwarded to upstream DNS and relayed back.
//    This lets internet keep working while the captive portal still auto-opens.
//
// Usage in wifi_manager.cpp:
//   _dns.begin(apIP);                        // start (wildcard)
//   _dns.setUpstream(IPAddress(1,1,1,1));    // switch to forward mode (NAT on)
//   _dns.setUpstream(IPAddress(0u));         // back to wildcard (NAT off)
//   _dns.stop();
//   _dns.tick();                             // call in loop()

class CaptiveDnsProxy {
public:
    void begin(IPAddress apIP);
    void setUpstream(IPAddress upstream);   // 0.0.0.0 = wildcard
    void stop();
    void tick();
    bool isRunning() const { return _running; }

private:
    static constexpr int FWD_PORT  = 15353; // local port for upstream socket
    static constexpr int MAX_FWD   = 8;     // concurrent forward slots
    static constexpr uint32_t FWD_TIMEOUT_MS = 5000;

    struct Pending {
        IPAddress srcIP;
        uint16_t  srcPort = 0;
        uint16_t  origId  = 0;
        uint16_t  fwdId   = 0;
        uint32_t  sentMs  = 0;
        bool      active  = false;
    } _fwd[MAX_FWD];

    WiFiUDP   _udp;     // listens on port 53
    WiFiUDP   _fwdUdp;  // forwarding socket (forward mode only)
    IPAddress _apIP;
    IPAddress _upstream;
    bool      _wildcard = true;
    bool      _running  = false;
    uint16_t  _idCtr    = 0x4000;

    static bool isCaptiveHost(const char* host);
    uint16_t parseQname(const uint8_t* pkt, int plen, int off, char* buf, int blen);
    void sendAReply(IPAddress dst, uint16_t port,
                    const uint8_t* query, int qlen, const uint8_t ip4[4]);
    void processIncoming();
    void processFwdReply();
    void cleanFwd();
};
