// sim_runtime.h — host-side recorder used by the atech pre-flight simulator.
//
// Everything the firmware does that would touch the outside world lands here:
// virtual clock, GPIO pin table, PWM writes, Serial output, and calls into
// mocked module classes. simulate.py drives it and prints the trace.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <type_traits>
#include <algorithm>

namespace sim {

struct Event { uint64_t t_ms; std::string kind; std::string a; std::string b; };

struct State {
    uint64_t now_us = 0;
    bool in_loop = false;
    bool delay_in_loop = false;
    uint64_t delay_total_ms = 0;
    std::map<int, int> pins, modes, analog, pwm;
    std::string serial_buf;
    std::vector<std::string> trace;
    std::map<std::string, size_t> counts;
    std::map<const void*, std::string> names;
    std::map<std::string, double> rets;
    std::vector<Event> events;
    size_t next_event = 0;
    size_t max_trace = 400;
    size_t dropped = 0;
};

inline State& S() { static State s; return s; }

inline uint64_t now_ms() { return S().now_us / 1000; }
inline void advance_us(uint64_t us) { S().now_us += us; }

inline void trace(const std::string& kind, const std::string& msg) {
    State& s = S();
    if (s.trace.size() >= s.max_trace) { s.dropped++; return; }
    char buf[40];
    std::snprintf(buf, sizeof buf, "t=%-7llu", (unsigned long long)now_ms());
    s.trace.push_back(std::string(buf) + kind + " " + msg);
}

inline void delay_ms(unsigned long ms) {
    if (S().in_loop) { S().delay_in_loop = true; S().delay_total_ms += ms; }
    advance_us((uint64_t)ms * 1000);
}
inline void delay_us(unsigned long us) { advance_us(us); }

// ---- pins ----
inline void pin_mode(int pin, int mode) {
    S().modes[pin] = mode;
    if (!S().pins.count(pin)) S().pins[pin] = (mode == 2 /*INPUT_PULLUP*/) ? 1 : 0;
}
inline int pin_read(int pin) {
    auto it = S().pins.find(pin);
    if (it != S().pins.end()) return it->second;
    auto m = S().modes.find(pin);
    return (m != S().modes.end() && m->second == 2) ? 1 : 0;
}
inline void pin_write(int pin, int val) {
    val = val ? 1 : 0;
    auto it = S().pins.find(pin);
    if (it == S().pins.end() || it->second != val) trace("pin   ", "GPIO" + std::to_string(pin) + "=" + std::to_string(val));
    S().pins[pin] = val;
}
inline int analog_read(int pin) { auto it = S().analog.find(pin); return it == S().analog.end() ? 0 : it->second; }
inline void analog_write(int pin, int val) {
    auto it = S().analog.find(pin);
    if (it == S().analog.end() || it->second != val) trace("pwm   ", "GPIO" + std::to_string(pin) + " duty=" + std::to_string(val));
    S().analog[pin] = val;
}
inline void pwm_write(int ch, int duty) {
    auto it = S().pwm.find(ch);
    if (it == S().pwm.end() || it->second != duty) trace("pwm   ", "ch" + std::to_string(ch) + " duty=" + std::to_string(duty));
    S().pwm[ch] = duty;
}

// ---- serial ----
inline void serial_out(const std::string& s) {
    State& st = S();
    st.serial_buf += s;
    size_t nl;
    while ((nl = st.serial_buf.find('\n')) != std::string::npos) {
        std::string line = st.serial_buf.substr(0, nl);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        trace("serial", line);
        st.serial_buf.erase(0, nl + 1);
    }
}

// ---- mocked module calls ----
inline void register_instance(const void* p, const std::string& name) { S().names[p] = name; }
inline std::string name_of(const void* p) {
    auto it = S().names.find(p);
    return it == S().names.end() ? std::string("?") : it->second;
}

template <class T> inline std::string arg_str(const T& v) {
    if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
    else if constexpr (std::is_same_v<T, char>) return std::string("'") + v + "'";
    else if constexpr (std::is_floating_point_v<T>) { char b[32]; std::snprintf(b, sizeof b, "%g", (double)v); return b; }
    else if constexpr (std::is_arithmetic_v<T>) return std::to_string(v);
    else if constexpr (std::is_convertible_v<T, const char*>) return v ? std::string("\"") + (const char*)v + "\"" : "null";
    else if constexpr (std::is_convertible_v<T, std::string>) return std::string("\"") + std::string(v) + "\"";
    else return "?";
}
inline std::string join_args() { return ""; }
template <class T, class... R> inline std::string join_args(const T& t, const R&... r) {
    std::string rest = join_args(r...);
    return arg_str(t) + (rest.empty() ? "" : ", " + rest);
}
template <class... A> inline void call(const void* self, const char* method, const A&... args) {
    std::string key = name_of(self) + "." + method;
    S().counts[key]++;
    trace("call  ", key + "(" + join_args(args...) + ")");
}
inline double ret(const void* self, const char* method, double dflt) {
    auto it = S().rets.find(name_of(self) + "." + method);
    return it == S().rets.end() ? dflt : it->second;
}

// ---- scenario ----
// File lines:  <ms> pin <gpio> <0|1>   |  <ms> ret <inst.method> <number>  |  # comment
inline void load_scenario(const char* path) {
    if (!path || !*path) return;
    FILE* f = std::fopen(path, "r");
    if (!f) { std::fprintf(stderr, "cannot open scenario %s\n", path); std::exit(2); }
    char line[512];
    while (std::fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char kind[16] = {0}, a[128] = {0}, b[128] = {0};
        unsigned long long t = 0;
        if (std::sscanf(line, "%llu %15s %127s %127s", &t, kind, a, b) >= 3)
            S().events.push_back({t, kind, a, b});
    }
    std::fclose(f);
    std::stable_sort(S().events.begin(), S().events.end(), [](const Event& x, const Event& y) { return x.t_ms < y.t_ms; });
}
inline void apply_events() {
    State& s = S();
    while (s.next_event < s.events.size() && s.events[s.next_event].t_ms <= now_ms()) {
        const Event& e = s.events[s.next_event++];
        if (e.kind == "pin") { s.pins[std::atoi(e.a.c_str())] = std::atoi(e.b.c_str()); trace("input ", "GPIO" + e.a + "=" + e.b); }
        else if (e.kind == "ret") { s.rets[e.a] = std::atof(e.b.c_str()); trace("input ", e.a + " -> " + e.b); }
        else if (e.kind == "analog") { s.analog[std::atoi(e.a.c_str())] = std::atoi(e.b.c_str()); trace("input ", "analog GPIO" + e.a + "=" + e.b); }
    }
}
inline uint64_t last_event_ms() { return S().events.empty() ? 0 : S().events.back().t_ms; }

inline void dump() {
    State& s = S();
    if (!s.serial_buf.empty()) trace("serial", s.serial_buf + "  (no newline)");
    for (const auto& l : s.trace) std::printf("%s\n", l.c_str());
    if (s.dropped) std::printf("# ... %zu more trace lines dropped\n", s.dropped);
    std::printf("# summary: %zu ms simulated\n", (size_t)now_ms());
    for (const auto& kv : s.counts) std::printf("# calls: %-32s x%zu\n", kv.first.c_str(), kv.second);
    if (s.delay_in_loop) std::printf("# WARNING: delay() used inside loop() (%llu ms total) - events and inputs stall while it runs\n", (unsigned long long)s.delay_total_ms);
}

}  // namespace sim
