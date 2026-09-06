// Arduino.h — host stand-in for the Arduino/ESP32 core, used only by the
// atech pre-flight simulator. Enough surface for the SDK's GPIO drivers and
// generated main.cpp; anything exotic should be mocked at the module level.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <string>
#include "sim_runtime.h"

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define OPEN_DRAIN 4
#define LSBFIRST 0
#define MSBFIRST 1
#define CHANGE 1
#define FALLING 2
#define RISING 3
#ifndef PI
#define PI 3.14159265358979323846
#endif
#define IRAM_ATTR
#define PROGMEM
#define F(x) (x)
#define bitRead(v, b) (((v) >> (b)) & 1)
#define bitSet(v, b) ((v) |= (1UL << (b)))
#define bitClear(v, b) ((v) &= ~(1UL << (b)))
#define lowByte(w) ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))

typedef bool boolean;
typedef uint8_t byte;
typedef unsigned int word;

inline unsigned long millis() { return (unsigned long)sim::now_ms(); }
inline unsigned long micros() { return (unsigned long)(sim::S().now_us); }
inline void delay(unsigned long ms) { sim::delay_ms(ms); }
inline void delayMicroseconds(unsigned int us) { sim::delay_us(us); }
inline void yield() {}

inline void pinMode(uint8_t pin, uint8_t mode) { sim::pin_mode(pin, mode); }
inline int digitalRead(uint8_t pin) { return sim::pin_read(pin); }
inline void digitalWrite(uint8_t pin, uint8_t val) { sim::pin_write(pin, val); }
inline int analogRead(uint8_t pin) { return sim::analog_read(pin); }
inline void analogWrite(uint8_t pin, int val) { sim::analog_write(pin, val); }
inline void analogReadResolution(uint8_t) {}
inline void analogWriteResolution(uint8_t) {}
inline void analogWriteFrequency(uint32_t) {}

inline double ledcSetup(uint8_t ch, double freq, uint8_t bits) { sim::trace("ledc  ", "setup ch" + std::to_string(ch) + " " + std::to_string((int)freq) + "Hz " + std::to_string(bits) + "bit"); return freq; }
inline void ledcAttachPin(uint8_t pin, uint8_t ch) { sim::trace("ledc  ", "attach GPIO" + std::to_string(pin) + " -> ch" + std::to_string(ch)); }
inline void ledcDetachPin(uint8_t) {}
inline void ledcWrite(uint8_t ch, uint32_t duty) { sim::pwm_write(ch, (int)duty); }
inline bool ledcAttach(uint8_t pin, uint32_t freq, uint8_t bits) { sim::trace("ledc  ", "attach GPIO" + std::to_string(pin) + " " + std::to_string(freq) + "Hz " + std::to_string(bits) + "bit"); return true; }
inline void tone(uint8_t pin, unsigned int freq, unsigned long dur = 0) { sim::trace("tone  ", "GPIO" + std::to_string(pin) + " " + std::to_string(freq) + "Hz " + std::to_string(dur) + "ms"); }
inline void noTone(uint8_t pin) { sim::trace("tone  ", "GPIO" + std::to_string(pin) + " off"); }
inline void attachInterrupt(uint8_t, void (*)(), int) {}
inline void detachInterrupt(uint8_t) {}
inline uint8_t digitalPinToInterrupt(uint8_t p) { return p; }

inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    if (in_max == in_min) return out_min;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
inline long random(long mx) { return mx > 0 ? std::rand() % mx : 0; }
inline long random(long mn, long mx) { return mx > mn ? mn + std::rand() % (mx - mn) : mn; }
inline void randomSeed(unsigned long s) { std::srand((unsigned)s); }

// ---- String (subset) ----
class String : public std::string {
public:
    using std::string::string;
    String() : std::string() {}
    String(const std::string& s) : std::string(s) {}
    String(const char* s) : std::string(s ? s : "") {}
    String(char c) : std::string(1, c) {}
    String(int v) : std::string(std::to_string(v)) {}
    String(unsigned int v) : std::string(std::to_string(v)) {}
    String(long v) : std::string(std::to_string(v)) {}
    String(unsigned long v) : std::string(std::to_string(v)) {}
    String(long long v) : std::string(std::to_string(v)) {}
    String(unsigned long long v) : std::string(std::to_string(v)) {}
    String(float v, int d = 2) { char b[48]; std::snprintf(b, sizeof b, "%.*f", d, (double)v); assign(b); }
    String(double v, int d = 2) { char b[48]; std::snprintf(b, sizeof b, "%.*f", d, v); assign(b); }
    long toInt() const { return std::atol(c_str()); }
    float toFloat() const { return (float)std::atof(c_str()); }
    double toDouble() const { return std::atof(c_str()); }
    bool equals(const String& o) const { return *this == o; }
    bool startsWith(const String& p) const { return rfind(p, 0) == 0; }
    bool endsWith(const String& p) const { return size() >= p.size() && compare(size() - p.size(), p.size(), p) == 0; }
    int indexOf(char c) const { auto i = find(c); return i == npos ? -1 : (int)i; }
    int indexOf(const String& s) const { auto i = find(s); return i == npos ? -1 : (int)i; }
    String substring(unsigned from) const { return from < size() ? String(substr(from)) : String(); }
    String substring(unsigned from, unsigned to) const { return from < size() ? String(substr(from, to > from ? to - from : 0)) : String(); }
    void trim() { while (!empty() && std::isspace((unsigned char)back())) pop_back(); size_t i = 0; while (i < size() && std::isspace((unsigned char)(*this)[i])) ++i; erase(0, i); }
    char charAt(unsigned i) const { return i < size() ? (*this)[i] : 0; }
    void toUpperCase() { for (auto& c : *this) c = (char)std::toupper((unsigned char)c); }
    void toLowerCase() { for (auto& c : *this) c = (char)std::tolower((unsigned char)c); }
    template <class T> String& operator+=(const T& v) { append(String(v)); return *this; }
};
template <class T> inline String operator+(const String& a, const T& b) { String r(a); r += b; return r; }

// ---- Serial ----
class HardwareSerial {
public:
    void begin(unsigned long, uint32_t = 0, int8_t = -1, int8_t = -1) {}
    void end() {}
    void setTxTimeoutMs(uint32_t) {}
    void setRxBufferSize(size_t) {}
    int available() { return 0; }
    int read() { return -1; }
    int peek() { return -1; }
    void flush() {}
    size_t write(uint8_t c) { sim::serial_out(std::string(1, (char)c)); return 1; }
    size_t write(const uint8_t* b, size_t n) { sim::serial_out(std::string((const char*)b, n)); return n; }
    size_t write(const char* s) { sim::serial_out(s); return std::strlen(s); }
    template <class T> size_t print(const T& v) { std::string s = sim::arg_str(v); if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2); sim::serial_out(s); return s.size(); }
    size_t print(const String& v) { sim::serial_out(v); return v.size(); }
    size_t print(const char* v) { sim::serial_out(v ? v : ""); return v ? std::strlen(v) : 0; }
    size_t print(char c) { sim::serial_out(std::string(1, c)); return 1; }
    size_t print(double v, int d) { char b[48]; std::snprintf(b, sizeof b, "%.*f", d, v); sim::serial_out(b); return std::strlen(b); }
    template <class T> size_t println(const T& v) { size_t n = print(v); sim::serial_out("\n"); return n + 1; }
    size_t println(double v, int d) { size_t n = print(v, d); sim::serial_out("\n"); return n + 1; }
    size_t println() { sim::serial_out("\n"); return 1; }
    size_t printf(const char* fmt, ...) { char b[1024]; va_list ap; va_start(ap, fmt); int n = std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap); if (n > 0) sim::serial_out(std::string(b, (size_t)std::min<int>(n, (int)sizeof b - 1))); return n > 0 ? (size_t)n : 0; }
    operator bool() const { return true; }
    // atech_usb.h routes to this object in the simulator; keep its extra API present.
    void update() {}
    bool ready() const { return true; }
};
inline HardwareSerial Serial;
inline HardwareSerial Serial1;
inline HardwareSerial Serial2;

// ---- loop watchdog + RTC memory (instrumentation used by the node firmware) ----
inline void enableLoopWDT() { sim::trace("wdt   ", "loop WDT enabled"); }
inline void disableLoopWDT() {}
inline void feedLoopWDT() {}
#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif
#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
#endif

// ---- ESP object ----
struct EspClass {
    uint32_t getFreeHeap() { return 250000; }
    uint32_t getSketchSize() { return 276 * 1024; }
    uint32_t getFreeSketchSpace() { return 3 * 1024 * 1024; }
    uint32_t getMinFreeHeap() { return 200000; }
    uint32_t getCpuFreqMHz() { return 240; }
    void restart() { sim::trace("esp   ", "restart() requested"); }
    uint64_t getEfuseMac() { return 0x1122334455ULL; }
};
inline EspClass ESP;

// Arduino-style min/max/constrain/abs are macros there; keep them macros here
// so mixed-type calls in drivers compile. Defined last so the std:: uses above
// are unaffected.
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define sq(x) ((x) * (x))
