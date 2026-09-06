// atech_actions.h — tiny serial action bus for Atech firmware.
//
// The open Atech SDK declares actions in module.yaml but generates no serial
// reader, so nothing on the board consumes `{"action":"<key>","value":<json>}`
// lines sent by `atech send`. This header-only bus fixes that: modules
// subscribe with a key prefix (their instance name), one poll() per loop reads
// Serial without blocking, and the matching handler gets the action key and
// the raw value JSON. Copy this file next to any module that needs actions;
// duplicates are harmless (include guard + inline definitions).
#pragma once
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

namespace atech_actions {

typedef void (*Handler)(const char* action, const char* valueJson, void* ctx);

struct Sub { const char* prefix; Handler fn; void* ctx; };

inline Sub* table() { static Sub t[8] = {}; return t; }
inline int& count() { static int n = 0; return n; }

inline bool subscribe(const char* prefix, Handler fn, void* ctx) {
    if (count() >= 8) return false;
    table()[count()++] = Sub{prefix, fn, ctx};
    return true;
}

// ---- minimal JSON helpers (flat objects, no nesting needed here) ----

// Find `"key":` inside `json`; returns pointer to first char of the value or nullptr.
inline const char* findKey(const char* json, const char* key) {
    if (!json) return nullptr;
    char pat[48];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char* p = strstr(json, pat);
    if (!p) return nullptr;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') ++p;
    return p;
}

inline bool getString(const char* json, const char* key, char* out, size_t n) {
    const char* p = findKey(json, key);
    if (!p || *p != '"' || n == 0) return false;
    ++p;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < n) {
        if (*p == '\\' && p[1]) ++p;  // unescape \" and \\ minimally
        out[i++] = *p++;
    }
    out[i] = 0;
    return true;
}

inline bool getNumber(const char* json, const char* key, double& out) {
    const char* p = findKey(json, key);
    if (!p) return false;
    char* end = nullptr;
    out = strtod(p, &end);
    return end && end != p;
}

// Parse a whole line `{"action":"k","value":V}` and dispatch. Returns true if handled.
inline bool dispatchLine(char* line) {
    char action[64];
    if (!getString(line, "action", action, sizeof action)) return false;
    const char* value = findKey(line, "value");
    if (!value) value = "null";
    // The SDK encodes every value as a JSON *string* holding the value's own
    // JSON text ("value":"{\"r\":255}" or "value":"1"). Unescape it so
    // handlers see plain JSON / plain numbers.
    static char decoded[640];
    if (*value == '"') {
        const char* p = value + 1;
        size_t i = 0;
        while (*p && *p != '"' && i + 1 < sizeof decoded) {
            if (*p == '\\' && p[1]) ++p;
            decoded[i++] = *p++;
        }
        decoded[i] = 0;
        value = decoded;
    }
    for (int i = 0; i < count(); ++i) {
        const Sub& s = table()[i];
        size_t pl = strlen(s.prefix);
        if (strncmp(action, s.prefix, pl) == 0 && (action[pl] == '_' || action[pl] == 0)) {
            s.fn(action, value, s.ctx);
            return true;
        }
    }
    return false;
}

// Non-blocking: read whatever is available, dispatch complete lines.
inline void poll() {
    static char buf[768];
    static size_t len = 0;
    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c < 0) break;
        if (c == '\n' || c == '\r') {
            if (len > 0) {
                buf[len] = 0;
                dispatchLine(buf);
                len = 0;
            }
            continue;
        }
        if (len + 1 < sizeof buf) buf[len++] = (char)c;
        else len = 0;  // overflow: drop the line
    }
}

}  // namespace atech_actions
