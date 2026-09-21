// Fake Preferences (NVS) for the host simulator: an in-memory key/value store per namespace.
#pragma once
#include <Arduino.h>
#include <map>
#include <string>
class Preferences {
public:
    bool begin(const char* ns, bool readOnly = false, const char* = nullptr) { _ns = ns ? ns : ""; (void)readOnly; return true; }
    void end() {}
    bool clear() { store().clear(); return true; }
    bool remove(const char* k) { return store().erase(k) > 0; }
    size_t putUChar(const char* k, uint8_t v) { store()[k] = std::to_string(v); return 1; }
    size_t putUShort(const char* k, uint16_t v) { store()[k] = std::to_string(v); return 2; }
    size_t putUInt(const char* k, uint32_t v) { store()[k] = std::to_string(v); return 4; }
    size_t putString(const char* k, const char* v) { store()[k] = v ? v : ""; return store()[k].size(); }
    size_t putString(const char* k, const String& v) { store()[k] = std::string(v); return v.size(); }
    uint8_t getUChar(const char* k, uint8_t d = 0) { auto it = store().find(k); return it == store().end() ? d : (uint8_t)std::stoul(it->second); }
    uint16_t getUShort(const char* k, uint16_t d = 0) { auto it = store().find(k); return it == store().end() ? d : (uint16_t)std::stoul(it->second); }
    uint32_t getUInt(const char* k, uint32_t d = 0) { auto it = store().find(k); return it == store().end() ? d : (uint32_t)std::stoul(it->second); }
    String getString(const char* k, const String& d = String()) { auto it = store().find(k); return it == store().end() ? d : String(it->second.c_str()); }
    bool isKey(const char* k) { return store().count(k) > 0; }
private:
    static std::map<std::string, std::map<std::string, std::string>>& all() { static std::map<std::string, std::map<std::string, std::string>> m; return m; }
    std::map<std::string, std::string>& store() { return all()[_ns]; }
    std::string _ns;
};
