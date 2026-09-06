#include "wifi_link.h"
#include "atech_actions.h"
#include <string.h>

WifiLink::WifiLink(const char* n) { strncpy(_name, n ? n : "wifi", sizeof _name - 1); _name[sizeof _name - 1] = 0; }

void WifiLink::begin() {
    loadConfig();
    atech_actions::subscribe(_name, &WifiLink::onActionStatic, this);
    _bootMs = millis();
    _state = _ssid[0] ? State::Idle : State::Unconfigured;
}

void WifiLink::loadConfig() {
    _prefs.begin("ruview", false);   // same namespace the earlier firmware used: creds already stored
    String s = _prefs.getString("ssid", ""), p = _prefs.getString("pass", ""), ip = _prefs.getString("sink_ip", _sinkIp);
    _sinkPort = (uint16_t)_prefs.getUShort("sink_port", 5005);
    strncpy(_ssid, s.c_str(), sizeof _ssid - 1);
    strncpy(_pass, p.c_str(), sizeof _pass - 1);
    strncpy(_sinkIp, ip.c_str(), sizeof _sinkIp - 1);
    _sinkValid = _sinkAddr.fromString(_sinkIp);
}

void WifiLink::startWifi() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(false, true);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    esp_wifi_set_ps(WIFI_PS_NONE);
    WiFi.begin(_ssid, _pass);
    _state = State::Connecting;
    _connectStartMs = millis();
}

void WifiLink::update() {
    if (Serial) atech_actions::poll();
    uint32_t now = millis();
    switch (_state) {
        case State::Unconfigured: break;
        case State::Idle: if (now - _bootMs >= LATE_START_MS) startWifi(); break;
        case State::Connecting:
            if (WiFi.status() == WL_CONNECTED) {
                _state = State::Connected;
                strncpy(_ip, WiFi.localIP().toString().c_str(), sizeof _ip - 1);
                if (!_udpUp) { _udp.begin(0); _udpUp = true; }
            } else if (now - _connectStartMs > 30000) startWifi();
            break;
        case State::Connected:
            if (WiFi.status() != WL_CONNECTED) { _state = State::Lost; _connectStartMs = now; break; }
            _rssi = WiFi.RSSI();
            sendBeacon(now);
            break;
        case State::Lost:
            if (WiFi.status() == WL_CONNECTED) _state = State::Connected;
            else if (now - _connectStartMs > 15000) startWifi();
            break;
    }
}

void WifiLink::sendBeacon(uint32_t now) {
    if (!_sinkValid || now - _lastBeaconMs < 1000) return;
    _lastBeaconMs = now;
    // 16 bytes: magic, seq, uptime_ms, free heap. Sent from loop(): proves loop liveness.
    uint32_t pkt[4] = { BEACON_MAGIC, _seq++, now, (uint32_t)ESP.getFreeHeap() };
    if (_udp.beginPacket(_sinkAddr, _sinkPort)) { _udp.write((const uint8_t*)pkt, sizeof pkt); if (_udp.endPacket()) _beacons++; }
}

bool WifiLink::nextEvent(char* out, size_t cap) {
    uint32_t now = millis();
    if (now - _lastEvMs < 250 || cap < 96) return false;
    _lastEvMs = now;
    if (_logCount > 0) {
        int n = snprintf(out, cap, "{\"type\":\"event\",\"payload\":{\"event_type\":\"log\",\"key\":\"%s_log\",\"value\":\"%s\",\"source\":\"wifi_link\"}}", _name, _logQ[_logHead]);
        _logHead = (uint8_t)((_logHead + 1) % LOGQ); _logCount--;
        return n > 0 && (size_t)n < cap;
    }
    int n;
    if (_evIdx == 0) n = snprintf(out, cap, "{\"type\":\"event\",\"payload\":{\"event_type\":\"state\",\"key\":\"%s_link\",\"value\":\"%s\",\"source\":\"wifi_link\"}}", _name, stateName());
    else             n = snprintf(out, cap, "{\"type\":\"event\",\"payload\":{\"event_type\":\"sensor\",\"key\":\"%s_rssi\",\"value\":%d,\"unit\":\"dBm\",\"source\":\"wifi_link\"}}", _name, _rssi);
    _evIdx ^= 1;
    return n > 0 && (size_t)n < cap;
}

const char* WifiLink::stateName() const {
    switch (_state) {
        case State::Unconfigured: return "unconfigured";
        case State::Idle: return "idle";
        case State::Connecting: return "connecting";
        case State::Connected: return "connected";
        case State::Lost: return "lost";
    }
    return "?";
}

void WifiLink::setWifi(const char* ssid, const char* pass) {
    if (!ssid || !*ssid) return;
    strncpy(_ssid, ssid, sizeof _ssid - 1); strncpy(_pass, pass ? pass : "", sizeof _pass - 1);
    _prefs.putString("ssid", _ssid); _prefs.putString("pass", _pass);
    startWifi();
}

void WifiLink::setSink(const char* ip, uint16_t port) {
    if (ip && *ip) { strncpy(_sinkIp, ip, sizeof _sinkIp - 1); _prefs.putString("sink_ip", _sinkIp); }
    if (port) { _sinkPort = port; _prefs.putUShort("sink_port", port); }
    _sinkValid = _sinkAddr.fromString(_sinkIp);
}

void WifiLink::scanNetworks() {
    WiFi.mode(WIFI_STA); delay(300);
    esp_wifi_disconnect(); wifi_config_t blank = {}; esp_wifi_set_config(WIFI_IF_STA, &blank); delay(300);
    int n = WiFi.scanNetworks(false, false, false, 300);
    char msg[110];
    snprintf(msg, sizeof msg, "scan: %d networks (2.4 GHz, strongest first)", n); logEvent(msg);
    for (int i = 0; i < n && i < 20; ++i) {
        String ssid = WiFi.SSID(i);
        snprintf(msg, sizeof msg, "%4d dBm  ch%-2d  %s  %s", (int)WiFi.RSSI(i), (int)WiFi.channel(i),
                 WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "wpa ", ssid.length() ? ssid.c_str() : "<hidden>");
        logEvent(msg);
    }
    WiFi.scanDelete();
    if (_ssid[0]) startWifi();
}

void WifiLink::logEvent(const char* msg) {
    if (_logCount >= LOGQ) return;
    char* dst = _logQ[(_logHead + _logCount) % LOGQ]; size_t i = 0;
    for (const char* p = msg; *p && i + 1 < LOGQ_LEN; ++p) { char ch = *p; if (ch == '"' || ch == '\\') ch = '\''; if ((unsigned char)ch < 0x20) ch = ' '; dst[i++] = ch; }
    dst[i] = 0; _logCount++;
}

void WifiLink::onActionStatic(const char* a, const char* v, void* ctx) { static_cast<WifiLink*>(ctx)->onAction(a, v); }

void WifiLink::onAction(const char* action, const char* value) {
    const char* sub = action + strlen(_name); if (*sub == '_') ++sub;
    if (strcmp(sub, "set_wifi") == 0) {
        char ssid[33] = {0}, pass[65] = {0};
        if (atech_actions::getString(value, "ssid", ssid, sizeof ssid)) { atech_actions::getString(value, "pass", pass, sizeof pass); setWifi(ssid, pass); logEvent("wifi credentials stored, connecting"); }
    } else if (strcmp(sub, "set_sink") == 0) {
        char ip[16] = {0}; double port = 0;
        atech_actions::getString(value, "ip", ip, sizeof ip); atech_actions::getNumber(value, "port", port);
        setSink(ip, (uint16_t)port);
        char msg[64]; snprintf(msg, sizeof msg, "sink %s:%u", _sinkIp, (unsigned)_sinkPort); logEvent(msg);
    } else if (strcmp(sub, "scan") == 0) scanNetworks();
    else if (strcmp(sub, "diag") == 0) {
        char msg[120];
        snprintf(msg, sizeof msg, "diag: state=%s ip=%s gw=%s sink=%s:%u rssi=%d beacons=%lu heap=%u", stateName(), _ip,
                 WiFi.gatewayIP().toString().c_str(), _sinkIp, (unsigned)_sinkPort, _rssi, (unsigned long)_beacons, (unsigned)ESP.getFreeHeap());
        logEvent(msg);
    }
}
