// ruview_wire.h — RuView UDP wire formats, byte-compatible with
// github.com/ruvnet/RuView firmware/esp32-csi-node (MIT).
//
//   CSI frame   (ADR-018)  magic 0xC5110001, 20-byte header + raw int8 I/Q
//   Vitals      (ADR-039)  magic 0xC5110002, 32 bytes, 1 Hz
//
// All multi-byte fields little-endian. Sink default: UDP <target_ip>:5005.
#pragma once
#include <stdint.h>
#include <string.h>

namespace ruview_wire {

constexpr uint32_t MAGIC_CSI    = 0xC5110001u;
constexpr uint32_t MAGIC_VITALS = 0xC5110002u;
constexpr uint16_t DEFAULT_PORT = 5005;
constexpr size_t   CSI_HEADER   = 20;
constexpr size_t   MAX_IQ_BYTES = 1024;  // RuView EDGE_MAX_IQ_BYTES

inline void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
inline void put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

inline uint32_t channelToMHz(uint8_t ch) {
    if (ch == 14) return 2484;
    if (ch >= 1 && ch <= 13) return 2407 + 5u * ch;
    if (ch >= 32 && ch <= 177) return 5000 + 5u * ch;
    return 0;
}

// Serialize one CSI frame. Returns bytes written (0 if out is too small).
//   out must hold CSI_HEADER + iqLen bytes.
// Byte 18 of the header carries our frame flags (RuView accepts any value there):
//   bit0 first_word_invalid, bit1 HT (sig_mode), bit2 STBC, bit3 HT40 (cwb), bit4 short GI,
//   bits 6-7 CSI config id (0 = RuView all-LTF layout, 1 = Espressif radar LLTF-only). Byte 19 = rate/MCS.
inline size_t serializeCsi(uint8_t* out, size_t outCap,
                           uint8_t nodeId, uint8_t nAntennas, uint8_t channel,
                           uint32_t seq, int8_t rssi, int8_t noiseFloor,
                           const int8_t* iq, uint16_t iqLen, uint8_t flags = 0, uint8_t mcs = 0) {
    if (nAntennas == 0) nAntennas = 1;
    if (outCap < CSI_HEADER + iqLen) return 0;
    uint16_t nSub = (uint16_t)(iqLen / (2u * nAntennas));
    put32(out + 0, MAGIC_CSI);
    out[4] = nodeId;
    out[5] = nAntennas;
    put16(out + 6, nSub);
    put32(out + 8, channelToMHz(channel));
    put32(out + 12, seq);
    out[16] = (uint8_t)rssi;
    out[17] = (uint8_t)noiseFloor;
    out[18] = flags;
    out[19] = mcs;
    memcpy(out + CSI_HEADER, iq, iqLen);
    return CSI_HEADER + iqLen;
}

// 32-byte vitals packet (edge_vitals_pkt_t in RuView edge_processing.h).
struct __attribute__((packed)) Vitals {
    uint32_t magic;          // 0xC5110002
    uint8_t  node_id;
    uint8_t  flags;          // bit0 presence, bit1 fall, bit2 motion
    uint16_t breathing_x100; // BPM * 100
    uint32_t heart_x10000;   // BPM * 10000
    int8_t   rssi;
    uint8_t  n_persons;
    uint8_t  reserved[2];
    float    motion_energy;
    float    presence_score;
    uint32_t timestamp_ms;
    uint32_t reserved2;
};
static_assert(sizeof(Vitals) == 32, "vitals packet must be 32 bytes");

inline void fillVitals(Vitals& v, uint8_t nodeId, bool presence, bool fall, bool motion,
                       float breathingBpm, float heartBpm, int8_t rssi, uint8_t nPersons,
                       float motionEnergy, float presenceScore, uint32_t tsMs) {
    v.magic = MAGIC_VITALS;
    v.node_id = nodeId;
    v.flags = (presence ? 1 : 0) | (fall ? 2 : 0) | (motion ? 4 : 0);
    v.breathing_x100 = (uint16_t)(breathingBpm < 0 ? 0 : breathingBpm * 100.0f + 0.5f);
    v.heart_x10000 = (uint32_t)(heartBpm < 0 ? 0 : heartBpm * 10000.0f + 0.5f);
    v.rssi = rssi;
    v.n_persons = nPersons;
    v.reserved[0] = v.reserved[1] = 0;
    v.motion_energy = motionEnergy;
    v.presence_score = presenceScore;
    v.timestamp_ms = tsMs;
    v.reserved2 = 0;
}

// ---- Node status (ours, not RuView's): magic 0xA11E0002, 40 bytes, 1 Hz ----
// Sent from loop() (not from the DSP task), so receiving it proves the main loop
// is alive; carries what a host (csi_sink.py, liquid_bridge.py) needs at a glance.
constexpr uint32_t MAGIC_STATUS = 0xA11E0002u;
struct __attribute__((packed)) NodeStatus {
    uint32_t magic;          // 0xA11E0002
    uint8_t  node_id;
    uint8_t  state;          // RuViewCsi::State (0 unconfigured .. 5 lost)
    uint8_t  flags;          // bit0 presence, bit1 fall, bit2 calibrating, bit3 csi_on, bits4-5 segment (0 live, 1 empty, 2 still, 3 walk)
    uint8_t  reset_reason;   // esp_reset_reason(): 1 power-on, 3 software, 6 task-WDT, 9 brownout
    uint32_t seq;
    uint32_t uptime_ms;
    uint32_t free_heap;
    float    rate_hz;        // CSI frames/s
    float    heart_bpm;      // 0 when no presence / calibrating
    float    breathing_bpm;
    float    motion;
    int8_t   rssi;
    uint8_t  layout;         // CSI frame bytes / 128 (1 LLTF only, 2 +HT-LTF, 3 +STBC-HT-LTF); 0 unknown
    uint8_t  reserved[2];
};
static_assert(sizeof(NodeStatus) == 40, "node status packet must be 40 bytes");

inline void fillStatus(NodeStatus& s, uint8_t nodeId, uint8_t state, bool presence, bool fall,
                       bool calibrating, bool csiOn, uint8_t resetReason, uint32_t seq,
                       uint32_t uptimeMs, uint32_t freeHeap, float rateHz, float hr, float br,
                       float motion, int8_t rssi, uint8_t segment = 0, uint8_t layout = 0) {
    s.magic = MAGIC_STATUS;
    s.node_id = nodeId;
    s.state = state;
    s.flags = (uint8_t)((presence ? 1 : 0) | (fall ? 2 : 0) | (calibrating ? 4 : 0) | (csiOn ? 8 : 0) | ((segment & 3) << 4));
    s.reset_reason = resetReason;
    s.seq = seq;
    s.uptime_ms = uptimeMs;
    s.free_heap = freeHeap;
    s.rate_hz = rateHz;
    s.heart_bpm = hr;
    s.breathing_bpm = br;
    s.motion = motion;
    s.rssi = rssi;
    s.layout = layout;
    s.reserved[0] = s.reserved[1] = 0;
}

}  // namespace ruview_wire
