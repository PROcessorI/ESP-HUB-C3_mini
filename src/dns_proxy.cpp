#include "dns_proxy.h"

// ── Captive-portal probe hostnames ──────────────────────────────────────────
// Queries for these hosts are answered with apIP so the OS shows
// a "Sign In to Network" notification and auto-opens the portal page.
static const char* const CAPTIVE_HOSTS[] = {
    "connectivitycheck.gstatic.com",
    "connectivitycheck.android.com",
    "clients1.google.com",
    "clients2.google.com",
    "clients3.google.com",
    "captive.apple.com",
    "www.apple.com",
    "www.msftconnecttest.com",
    "www.msftncsi.com",
    "dns.msftncsi.com",
    nullptr
};

bool CaptiveDnsProxy::isCaptiveHost(const char* host) {
    for (int i = 0; CAPTIVE_HOSTS[i]; i++) {
        if (strcmp(host, CAPTIVE_HOSTS[i]) == 0) return true;
    }
    return false;
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void CaptiveDnsProxy::begin(IPAddress apIP) {
    _apIP     = apIP;
    _upstream = IPAddress(0,0,0,0);
    _wildcard = true;
    _running  = false;
    _udp.stop();
    _fwdUdp.stop();
    for (auto& f : _fwd) f.active = false;
    if (_udp.begin(53)) {
        _running = true;
    }
}

void CaptiveDnsProxy::setUpstream(IPAddress upstream) {
    _upstream = upstream;
    _wildcard = ((uint32_t)upstream == 0u);
    for (auto& f : _fwd) f.active = false;
    _fwdUdp.stop();
    if (!_wildcard) {
        _fwdUdp.begin(FWD_PORT);
    }
}

void CaptiveDnsProxy::stop() {
    _udp.stop();
    _fwdUdp.stop();
    _running = false;
}

void CaptiveDnsProxy::tick() {
    if (!_running) return;
    processIncoming();
    processFwdReply();
    cleanFwd();
}

// ── DNS helpers ──────────────────────────────────────────────────────────────

// Parse a DNS QNAME starting at pkt[off].
// Fills buf with dot-separated lowercase hostname.
// Returns the offset of the first byte AFTER the QNAME (= QTYPE position),
// or 0 on error/pointer (DNS compression not expected in queries).
uint16_t CaptiveDnsProxy::parseQname(const uint8_t* pkt, int plen, int off,
                                     char* buf, int blen) {
    int idx = 0;
    bool first = true;
    while (off < plen) {
        uint8_t ll = pkt[off++];
        if (ll == 0) { buf[idx] = 0; return (uint16_t)off; }
        if ((ll & 0xC0) == 0xC0) { buf[idx] = 0; return 0; } // pointer – stop
        if (!first && idx < blen - 1) buf[idx++] = '.';
        first = false;
        for (uint8_t i = 0; i < ll && off < plen && idx < blen - 1; i++, off++) {
            buf[idx++] = (char)tolower(pkt[off]);
        }
    }
    buf[idx] = 0;
    return 0;
}

// Build and send a DNS A-record response for the given query.
// ip4[4] is the IPv4 address in network byte order (big-endian octets).
void CaptiveDnsProxy::sendAReply(IPAddress dst, uint16_t port,
                                  const uint8_t* query, int qlen,
                                  const uint8_t ip4[4]) {
    if (qlen < 12 || qlen + 16 > 512) return;
    uint8_t resp[512];
    memcpy(resp, query, qlen);
    // Header: QR=1 AA=1 RD=1 RA=1 RCODE=0; ANCOUNT=1; NSCOUNT=ARCOUNT=0
    resp[2] = 0x81; resp[3] = 0x80;
    resp[6] = 0x00; resp[7] = 0x01;
    resp[8] = 0x00; resp[9] = 0x00;
    resp[10]= 0x00; resp[11]= 0x00;
    // Answer record
    uint8_t* a = resp + qlen;
    a[0]=0xC0; a[1]=0x0C;               // Name: pointer to offset 12
    a[2]=0x00; a[3]=0x01;               // Type: A
    a[4]=0x00; a[5]=0x01;               // Class: IN
    a[6]=0x00; a[7]=0x00; a[8]=0x00; a[9]=0x3C; // TTL: 60 s
    a[10]=0x00; a[11]=0x04;             // RDLENGTH: 4
    memcpy(a + 12, ip4, 4);             // RDATA: IPv4 address
    _udp.beginPacket(dst, port);
    _udp.write(resp, qlen + 16);
    _udp.endPacket();
}

// ── Main processing ──────────────────────────────────────────────────────────

void CaptiveDnsProxy::processIncoming() {
    int plen;
    while ((plen = _udp.parsePacket()) > 0) {
        uint8_t buf[512];
        if (plen > (int)sizeof(buf)) plen = (int)sizeof(buf);
        int rlen = _udp.read(buf, plen);
        if (rlen < 12) continue;

        // Must be a standard query (QR=0, Opcode=0)
        if ((buf[2] & 0xF8) != 0x00) continue;
        if ((buf[4] == 0 && buf[5] == 0)) continue;  // QDCOUNT = 0

        IPAddress srcIP   = _udp.remoteIP();
        uint16_t  srcPort = _udp.remotePort();

        char host[128] = "";
        uint16_t qtypeOff = parseQname(buf, rlen, 12, host, sizeof(host));

        // QTYPE (1=A, 28=AAAA, …)
        uint16_t qtype = 0;
        if (qtypeOff > 0 && qtypeOff + 1 < rlen) {
            qtype = ((uint16_t)buf[qtypeOff] << 8) | buf[qtypeOff + 1];
        }

        uint8_t ip4[4] = { _apIP[0], _apIP[1], _apIP[2], _apIP[3] };

        if (_wildcard) {
            // Wildcard mode: respond to all A queries with our IP
            if (qtype == 1) {
                sendAReply(srcIP, srcPort, buf, rlen, ip4);
            }
            // Drop AAAA / other types silently (no internet in this mode)

        } else {
            // Forward mode
            if (qtype == 1 && isCaptiveHost(host)) {
                // Intercept captive probe A query → respond with our IP
                sendAReply(srcIP, srcPort, buf, rlen, ip4);
            } else {
                // Forward everything else to upstream DNS
                int slot = -1;
                for (int i = 0; i < MAX_FWD; i++) {
                    if (!_fwd[i].active) { slot = i; break; }
                }
                if (slot < 0) continue;  // table full, drop packet

                _fwd[slot].srcIP   = srcIP;
                _fwd[slot].srcPort = srcPort;
                _fwd[slot].origId  = ((uint16_t)buf[0] << 8) | buf[1];
                _fwd[slot].fwdId   = _idCtr++;
                _fwd[slot].sentMs  = millis();
                _fwd[slot].active  = true;

                uint8_t fwd[512];
                memcpy(fwd, buf, rlen);
                fwd[0] = (_fwd[slot].fwdId >> 8) & 0xFF;
                fwd[1] =  _fwd[slot].fwdId        & 0xFF;

                _fwdUdp.beginPacket(_upstream, 53);
                _fwdUdp.write(fwd, rlen);
                _fwdUdp.endPacket();
            }
        }
    }
}

void CaptiveDnsProxy::processFwdReply() {
    if (_wildcard) return;
    int plen;
    while ((plen = _fwdUdp.parsePacket()) > 0) {
        uint8_t buf[512];
        if (plen > (int)sizeof(buf)) plen = (int)sizeof(buf);
        int rlen = _fwdUdp.read(buf, plen);
        if (rlen < 2) continue;

        uint16_t fwdId = ((uint16_t)buf[0] << 8) | buf[1];
        for (int i = 0; i < MAX_FWD; i++) {
            if (_fwd[i].active && _fwd[i].fwdId == fwdId) {
                buf[0] = (_fwd[i].origId >> 8) & 0xFF;
                buf[1] =  _fwd[i].origId        & 0xFF;
                _udp.beginPacket(_fwd[i].srcIP, _fwd[i].srcPort);
                _udp.write(buf, rlen);
                _udp.endPacket();
                _fwd[i].active = false;
                break;
            }
        }
    }
}

void CaptiveDnsProxy::cleanFwd() {
    uint32_t now = millis();
    for (auto& f : _fwd) {
        if (f.active && now - f.sentMs > FWD_TIMEOUT_MS) f.active = false;
    }
}
