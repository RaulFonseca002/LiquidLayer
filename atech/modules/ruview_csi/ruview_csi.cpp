/**
 * @file ruview_csi.cpp
 * @brief RuView-compatible WiFi CSI sensing node as an Atech module (Milestone A).
 *
 * Port notes (github.com/ruvnet/RuView firmware/esp32-csi-node, MIT):
 *  - CSI config is the ESP32-S3 legacy layout: lltf/htltf/stbc/ltf_merge on,
 *    channel_filter and manu_scale off, shift 0 (csi_collector.c).
 *  - Promiscuous MGMT-only filter: DATA frames can push the callback to
 *    100-500 Hz and race SPI-flash cache on core 0 (RuView issue #396); with
 *    two SPI displays on this board we keep MGMT only, as RuView does when a
 *    display is present.
 *  - 50 Hz early gate before any work in the callback.
 *  - RuView's NDP injection is a TODO placeholder upstream; its real frame
 *    rate comes from beacons plus probe requests. We do the same: a UDP pump
 *    to the gateway, and probe-request injection if the rate stays low.
 */
#include "ruview_csi.h"
#include "atech_actions.h"
#include "ruview_wire.h"
#include <string.h>
#include <math.h>
#include <ping/ping_sock.h>   // IDF ping session: the CSI traffic source (esp-csi does the same)

static RuViewCsi* s_instance = nullptr;  // one radio, one sensing module

static void promiscNoop(void* buf, wifi_promiscuous_pkt_type_t type) { (void)buf; (void)type; }

static void csiTrampoline(void* ctx, wifi_csi_info_t* info) {
    RuViewCsi* self = static_cast<RuViewCsi*>(ctx);
    if (self && info) self->_onCsi(info);
}

RuViewCsi::RuViewCsi(const char* instanceName) {
    strncpy(_name, instanceName ? instanceName : "csi", sizeof _name - 1);
    _name[sizeof _name - 1] = 0;
}

// ------------------------------------------------------------------ lifecycle

void RuViewCsi::begin() {
    s_instance = this;
    loadConfig();
    atech_actions::subscribe(_name, &RuViewCsi::onActionStatic, this);
    if (_ssid[0]) startWifi();
    else _state = State::Unconfigured;
    _lastTickMs = millis();
}

void RuViewCsi::loadConfig() {
    _prefs.begin("ruview", false);
    String s = _prefs.getString("ssid", "");
    String p = _prefs.getString("pass", "");
    String ip = _prefs.getString("sink_ip", _sinkIp);
    _sinkPort = (uint16_t)_prefs.getUShort("sink_port", ruview_wire::DEFAULT_PORT);
    _nodeId = (uint8_t)_prefs.getUChar("node_id", 1);
    strncpy(_ssid, s.c_str(), sizeof _ssid - 1);
    strncpy(_pass, p.c_str(), sizeof _pass - 1);
    strncpy(_sinkIp, ip.c_str(), sizeof _sinkIp - 1);
    _sinkValid = _sinkAddr.fromString(_sinkIp);
}

void RuViewCsi::startWifi() {
    disableCsi();
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);            // modem sleep would starve the CSI stream
    WiFi.disconnect(false, true);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);   // esp-csi examples require HT20 for CSI
    esp_wifi_set_ps(WIFI_PS_NONE);
    armCsi();                                            // arm before association, like esp-csi
    WiFi.begin(_ssid, _pass);
    _state = State::Connecting;
    _connectStartMs = millis();
    _probeInject = false;
}

void RuViewCsi::enableCsi() {
    if (_csiOn) return;
    esp_err_t eFilt = ESP_OK, eProm = ESP_OK;
    if (_promisc) {
        // RuView registers a no-op promiscuous RX callback; without one the
        // driver appears to discard promiscuous frames before the CSI hook.
        eProm = esp_wifi_set_promiscuous(true);
        esp_wifi_set_promiscuous_rx_cb(&promiscNoop);
        wifi_promiscuous_filter_t filter = {};
        filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;   // beacons + probe responses only
        eFilt = esp_wifi_set_promiscuous_filter(&filter);
    }

    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_err_t eCfg, eCb, eOn;
    armCsi(&eCfg, &eCb, &eOn);
    _csiOn = true;

    wifi_ps_type_t ps; esp_wifi_get_ps(&ps);
    wifi_bandwidth_t bw; esp_wifi_get_bandwidth(WIFI_IF_STA, &bw);
    uint8_t proto = 0; esp_wifi_get_protocol(WIFI_IF_STA, &proto);
    char msg[120];
    snprintf(msg, sizeof msg, "csi on: promisc=%d filt=%d prom=%d cfg=%d cb=%d csi=%d ch=%d ps=%d bw=%d proto=%d", (int)_promisc,
             (int)eFilt, (int)eProm, (int)eCfg, (int)eCb, (int)eOn, (int)WiFi.channel(), (int)ps, (int)bw, (int)proto);
    logEvent(msg);

    _udp.begin(0);
    startPump();
    _rateWindowStartMs = millis();
    _framesAtWindowStart = _framesTotal;
    _edge.reset();
}

void RuViewCsi::armCsi(esp_err_t* eCfg, esp_err_t* eCb, esp_err_t* eOn) {
    wifi_csi_config_t cfg = {};
    cfg.lltf_en = true;
    cfg.htltf_en = true;
    cfg.stbc_htltf2_en = true;
    cfg.ltf_merge_en = true;
    cfg.channel_filter_en = false;
    cfg.manu_scale = false;
    cfg.shift = 0;
    esp_err_t a = esp_wifi_set_csi_config(&cfg);
    esp_err_t b = esp_wifi_set_csi_rx_cb(&csiTrampoline, this);
    esp_err_t d = esp_wifi_set_csi(true);
    if (eCfg) *eCfg = a;
    if (eCb) *eCb = b;
    if (eOn) *eOn = d;
}

void RuViewCsi::disableCsi() {
    if (!_csiOn) return;
    stopPump();
    esp_wifi_set_csi(false);
    esp_wifi_set_csi_rx_cb(nullptr, nullptr);
    if (_promisc) { esp_wifi_set_promiscuous(false); esp_wifi_set_promiscuous_rx_cb(nullptr); }
    _udp.stop();
    _csiOn = false;
}

// ------------------------------------------------------------------ callback (WiFi task)

void RuViewCsi::_onCsi(const wifi_csi_info_t* info) {
    int64_t now = esp_timer_get_time();
    if (now - _lastProcessUs < (int64_t)MIN_PROCESS_US) { _dropped++; return; }
    _lastProcessUs = now;
    if (!info->buf || info->len == 0) return;

    _framesTotal++;
    _lastRssi = info->rx_ctrl.rssi;

    uint8_t next = (uint8_t)((_head + 1) % RING_SLOTS);
    if (next == _tail) { _dropped++; return; }  // consumer behind: drop, never block
    Slot& s = _ring[_head];
    uint16_t len = info->len;
    if (len > ruview_wire::MAX_IQ_BYTES) len = ruview_wire::MAX_IQ_BYTES;
    memcpy(s.iq, info->buf, len);
    s.len = len;
    s.rssi = (int8_t)info->rx_ctrl.rssi;
    s.noise = (int8_t)info->rx_ctrl.noise_floor;
    s.channel = (uint8_t)info->rx_ctrl.channel;
    s.nAnt = 1;
    s.seq = _seq++;
    __sync_synchronize();
    _head = next;
}

// ------------------------------------------------------------------ loop side

void RuViewCsi::update() {
    atech_actions::poll();
    uint32_t now = millis();

    switch (_state) {
        case State::Unconfigured:
            return;
        case State::Connecting:
            if (WiFi.status() == WL_CONNECTED) {
                _state = State::Streaming;
                enableCsi();
            } else if (now - _connectStartMs > 30000) {
                startWifi();  // retry association every 30 s
            }
            return;
        case State::Lost:
            if (WiFi.status() == WL_CONNECTED) { _state = State::Streaming; enableCsi(); }
            else if (now - _connectStartMs > 15000) startWifi();
            return;
        case State::Streaming:
            if (WiFi.status() != WL_CONNECTED) {
                disableCsi();
                _state = State::Lost;
                _connectStartMs = now;
                return;
            }
            break;
    }

    pumpTraffic(now);
    drainRing(now);
    updateRate(now);
    if (_streaming && _sinkValid && now - _lastVitalsMs >= 1000) {
        _lastVitalsMs = now;
        sendVitalsPacket(now);
    }
}

void RuViewCsi::startPump() {
    // Finding (Arduino core 2.0.17 / IDF 4.4, ESP32-S3): promiscuous mode
    // silences the CSI callback entirely, so CSI only comes from frames
    // addressed to this station. A 20 Hz ICMP ping to the gateway makes the
    // AP answer 20 times a second; each reply carries CSI.
    if (_ping) return;
    IPAddress gw = WiFi.gatewayIP();
    if ((uint32_t)gw == 0) return;
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.count = ESP_PING_COUNT_INFINITE;
    cfg.interval_ms = PUMP_INTERVAL_MS;
    cfg.timeout_ms = 500;
    cfg.data_size = 8;
    cfg.task_stack_size = 3072;
    IP_ADDR4(&cfg.target_addr, gw[0], gw[1], gw[2], gw[3]);
    esp_ping_callbacks_t cbs = {};
    if (esp_ping_new_session(&cfg, &cbs, &_ping) == ESP_OK) esp_ping_start(_ping);
    else _ping = nullptr;
}

void RuViewCsi::stopPump() {
    if (!_ping) return;
    esp_ping_stop(_ping);
    esp_ping_delete_session(_ping);
    _ping = nullptr;
}

void RuViewCsi::pumpTraffic(uint32_t now) {
    if (!_ping) startPump();
    // Fallback when the AP does not answer pings: probe requests, answered by every AP on the channel.
    if (_probeInject && now - _lastProbeMs >= PROBE_INTERVAL_MS) {
        _lastProbeMs = now;
        injectProbe();
    }
}

void RuViewCsi::injectProbe() {
    // Broadcast 802.11 probe request; every AP on the channel answers with a
    // probe response addressed to us, which the CSI engine sees as a MGMT frame.
    uint8_t mac[6];
    WiFi.macAddress(mac);
    uint8_t frame[64];
    size_t n = 0;
    frame[n++] = 0x40; frame[n++] = 0x00;                 // FC: mgmt, probe request
    frame[n++] = 0x00; frame[n++] = 0x00;                 // duration
    memset(frame + n, 0xFF, 6); n += 6;                   // DA broadcast
    memcpy(frame + n, mac, 6);  n += 6;                   // SA
    memset(frame + n, 0xFF, 6); n += 6;                   // BSSID broadcast
    frame[n++] = 0x00; frame[n++] = 0x00;                 // seq ctl (sys seq when en_sys_seq)
    frame[n++] = 0x00; frame[n++] = 0x00;                 // SSID IE, wildcard
    frame[n++] = 0x01; frame[n++] = 0x08;                 // Supported rates IE
    const uint8_t rates[8] = {0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24};
    memcpy(frame + n, rates, 8); n += 8;
    esp_wifi_80211_tx(WIFI_IF_STA, frame, (int)n, true);
}

void RuViewCsi::drainRing(uint32_t now) {
    int budget = RING_SLOTS;  // never spend more than one ring per loop
    while (_tail != _head && budget-- > 0) {
        Slot& s = _ring[_tail];
        _edge.push(s.iq, s.len, now);
        if (_streaming && _sinkValid) {
            size_t n = ruview_wire::serializeCsi(_txBuf, sizeof _txBuf, _nodeId, s.nAnt, s.channel,
                                                 s.seq, s.rssi, s.noise, s.iq, s.len);
            if (n && _udp.beginPacket(_sinkAddr, _sinkPort)) {
                _udp.write(_txBuf, n);
                if (_udp.endPacket()) _packetsSent++;
            }
        }
        __sync_synchronize();
        _tail = (uint8_t)((_tail + 1) % RING_SLOTS);
    }
}

void RuViewCsi::updateRate(uint32_t now) {
    uint32_t dt = now - _rateWindowStartMs;
    if (dt >= 2000) {
        uint32_t frames = _framesTotal - _framesAtWindowStart;
        _rateHz = (float)frames * 1000.0f / (float)dt;
        _framesAtWindowStart = _framesTotal;
        _rateWindowStartMs = now;
        if (_probeForce < 0) {
            if (!_probeInject && _rateHz < LOW_RATE_HZ && now - _connectStartMs > 6000) _probeInject = true;
            if (_probeInject && _rateHz > LOW_RATE_HZ * 3) _probeInject = false;  // beacons alone suffice
        }
    }
}

void RuViewCsi::sendVitalsPacket(uint32_t nowMs) {
    ruview_wire::Vitals v;
    ruview_wire::fillVitals(v, _nodeId, _edge.presence(), _edge.fall(), _edge.motionEnergy() > 0.02f,
                            _edge.breathingBpm(), _edge.heartRateBpm(), (int8_t)_lastRssi,
                            _edge.presence() ? 1 : 0, _edge.motionEnergy(), _edge.presenceScore(), nowMs);
    if (_udp.beginPacket(_sinkAddr, _sinkPort)) {
        _udp.write((const uint8_t*)&v, sizeof v);
        _udp.endPacket();
    }
}

bool RuViewCsi::tick1Hz() {
    uint32_t now = millis();
    if (now - _lastTickMs >= 1000) { _lastTickMs = now; return true; }
    return false;
}

bool RuViewCsi::nextEvent(char* out, size_t cap) {
    uint32_t now = millis();
    if (now - _lastEvMs < 200 || cap < 96) return false;
    _lastEvMs = now;
    if (_logCount > 0) {
        int n = snprintf(out, cap, "{\"type\":\"event\",\"payload\":{\"event_type\":\"log\",\"key\":\"%s_log\",\"value\":\"%s\",\"source\":\"ruview_csi\"}}",
                         _name, _logQ[_logHead]);
        _logHead = (uint8_t)((_logHead + 1) % LOGQ);
        _logCount--;
        return n > 0 && (size_t)n < cap;
    }
    // Cycle: frame_rate, rssi, heart_rate, breathing_rate, activity, presence, link
    static const char* keys[7] = {"frame_rate", "rssi", "heart_rate", "breathing_rate", "activity", "presence", "link"};
    const char* type = (_evIdx >= 5) ? "state" : "sensor";
    int n = snprintf(out, cap, "{\"type\":\"event\",\"payload\":{\"event_type\":\"%s\",\"key\":\"%s_%s\",\"value\":",
                     type, _name, keys[_evIdx]);
    if (n < 0 || (size_t)n >= cap) return false;
    size_t left = cap - (size_t)n;
    int m = 0;
    switch (_evIdx) {
        case 0: m = snprintf(out + n, left, "%.1f,\"unit\":\"Hz\"", _rateHz); break;
        case 1: m = snprintf(out + n, left, "%d,\"unit\":\"dBm\"", _lastRssi); break;
        case 2: m = snprintf(out + n, left, "%.1f,\"unit\":\"bpm\"", _edge.heartRateBpm()); break;
        case 3: m = snprintf(out + n, left, "%.1f,\"unit\":\"bpm\"", _edge.breathingBpm()); break;
        case 4: m = snprintf(out + n, left, "%.3f", _edge.motionEnergy()); break;
        case 5: m = snprintf(out + n, left, "%d", _edge.presence() ? 1 : 0); break;
        default: m = snprintf(out + n, left, "\"%s\"", stateName()); break;
    }
    if (m < 0 || (size_t)m >= left) return false;
    n += m; left = cap - (size_t)n;
    m = snprintf(out + n, left, ",\"source\":\"ruview_csi\"}}");
    _evIdx = (uint8_t)((_evIdx + 1) % 7);
    return m > 0 && (size_t)m < left;
}

const char* RuViewCsi::stateName() const {
    switch (_state) {
        case State::Unconfigured: return "unconfigured";
        case State::Connecting:   return "connecting";
        case State::Streaming:    return "streaming";
        case State::Lost:         return "lost";
    }
    return "?";
}

// ------------------------------------------------------------------ control

void RuViewCsi::setWifi(const char* ssid, const char* pass) {
    if (!ssid || !*ssid) return;
    strncpy(_ssid, ssid, sizeof _ssid - 1);
    strncpy(_pass, pass ? pass : "", sizeof _pass - 1);
    _prefs.putString("ssid", _ssid);
    _prefs.putString("pass", _pass);
    startWifi();
}

void RuViewCsi::setSink(const char* ip, uint16_t port, uint8_t nodeId) {
    if (ip && *ip) { strncpy(_sinkIp, ip, sizeof _sinkIp - 1); _prefs.putString("sink_ip", _sinkIp); }
    if (port) { _sinkPort = port; _prefs.putUShort("sink_port", port); }
    if (nodeId) { _nodeId = nodeId; _prefs.putUChar("node_id", nodeId); }
    _sinkValid = _sinkAddr.fromString(_sinkIp);
}

void RuViewCsi::calibrate() {
    _edge.forceCalibrate();
}

void RuViewCsi::scanNetworks() {
    // Blocking 2.4 GHz scan at the ESP-IDF level (does not depend on the Arduino
    // event loop). Results go out as log events.
    bool wasStreaming = (_state == State::Streaming);
    if (wasStreaming) disableCsi();
    WiFi.mode(WIFI_STA);
    delay(300);
    // A leftover station config (e.g. from the hosted firmware) makes the driver
    // auto-connect at start, and scans are refused while connecting. Clear it.
    esp_wifi_disconnect();
    wifi_config_t blank = {};
    esp_wifi_set_config(WIFI_IF_STA, &blank);
    delay(300);
    // Arduino's event handler consumes the IDF result list on SCAN_DONE, so read
    // the results through the Arduino accessors after a blocking scan.
    uint32_t t0 = millis();
    int n = WiFi.scanNetworks(false, false, false, 300);
    char msg[110];
    snprintf(msg, sizeof msg, "scan: %d networks in %lums (2.4 GHz only; strongest first)", n, (unsigned long)(millis() - t0));
    logEvent(msg);
    for (int i = 0; i < n && i < 20; ++i) {
        String ssid = WiFi.SSID(i);
        snprintf(msg, sizeof msg, "%4d dBm  ch%-2d  %s  %s", (int)WiFi.RSSI(i), (int)WiFi.channel(i),
                 WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "wpa ", ssid.length() ? ssid.c_str() : "<hidden>");
        logEvent(msg);
    }
    WiFi.scanDelete();
    if (wasStreaming) enableCsi();
}

void RuViewCsi::logEvent(const char* msg) {
    // Queue the message; nextEvent() emits it as one paced JSON line
    // (event_type "log", key "<instance>_log"). Bursting lines straight to
    // Serial truncates them: the SDK sets a zero USB-CDC TX timeout.
    if (_logCount >= LOGQ) return;  // drop newest when full
    uint8_t idx = (uint8_t)((_logHead + _logCount) % LOGQ);
    char* dst = _logQ[idx];
    size_t i = 0;
    for (const char* p = msg; *p && i + 1 < LOGQ_LEN; ++p) {
        char ch = *p;
        if (ch == '"' || ch == '\\') ch = '\'';
        if ((unsigned char)ch < 0x20) ch = ' ';
        dst[i++] = ch;
    }
    dst[i] = 0;
    _logCount++;
}

void RuViewCsi::onActionStatic(const char* action, const char* value, void* ctx) {
    static_cast<RuViewCsi*>(ctx)->onAction(action, value);
}

void RuViewCsi::onAction(const char* action, const char* value) {
    const char* sub = action + strlen(_name);
    if (*sub == '_') ++sub;
    if (strcmp(sub, "set_wifi") == 0) {
        char ssid[33] = {0}, pass[65] = {0};
        if (atech_actions::getString(value, "ssid", ssid, sizeof ssid)) {
            atech_actions::getString(value, "pass", pass, sizeof pass);
            setWifi(ssid, pass);
            logEvent("wifi credentials stored, connecting");
        }
    } else if (strcmp(sub, "set_sink") == 0) {
        char ip[16] = {0};
        double port = 0, node = 0;
        atech_actions::getString(value, "ip", ip, sizeof ip);
        atech_actions::getNumber(value, "port", port);
        atech_actions::getNumber(value, "node_id", node);
        setSink(ip, (uint16_t)port, (uint8_t)node);
        char msg[64];
        snprintf(msg, sizeof msg, "sink %s:%u node %u", _sinkIp, (unsigned)_sinkPort, (unsigned)_nodeId);
        logEvent(msg);
    } else if (strcmp(sub, "scan") == 0) {
        scanNetworks();
    } else if (strcmp(sub, "diag") == 0) {
        char msg[120];
        snprintf(msg, sizeof msg, "diag: frames=%lu dropped=%lu tx=%lu rate=%.1f pump=%d probe=%d promisc=%d ch=%d rssi=%d",
                 (unsigned long)_framesTotal, (unsigned long)_dropped, (unsigned long)_packetsSent, _rateHz,
                 (int)(_ping != nullptr), (int)_probeInject, (int)_promisc, (int)WiFi.channel(), (int)WiFi.RSSI());
        logEvent(msg);
        snprintf(msg, sizeof msg, "diag: ip=%s gw=%s sink=%s:%u heap=%u state=%s",
                 WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(), _sinkIp,
                 (unsigned)_sinkPort, (unsigned)ESP.getFreeHeap(), stateName());
        logEvent(msg);
    } else if (strcmp(sub, "promisc") == 0) {
        bool want = strtod(value, nullptr) != 0;
        if (_csiOn) { disableCsi(); _promisc = want; enableCsi(); }
        else { _promisc = want; logEvent(_promisc ? "promisc: on (applies when CSI starts)" : "promisc: off"); }
    } else if (strcmp(sub, "probe") == 0) {
        double v = strtod(value, nullptr);
        _probeForce = (int8_t)(v < 0 ? -1 : (v != 0 ? 1 : 0));
        _probeInject = (_probeForce == 1);
        logEvent(_probeForce < 0 ? "probe: auto" : (_probeForce ? "probe: forced on" : "probe: forced off"));
    } else if (strcmp(sub, "calibrate") == 0) {
        calibrate();
    } else if (strcmp(sub, "stream") == 0) {
        double on = 1;
        // value is a bare number for this action
        char* end = nullptr;
        on = strtod(value, &end);
        setStreaming(on != 0);
    }
}
