// atech_usb.h — deferred USB-CDC bring-up for Atech ESP32-S3 firmware.
//
// WHY THIS EXISTS
//   With ARDUINO_USB_CDC_ON_BOOT=1 (the Atech SDK default) the USB-Serial-JTAG
//   driver is initialised in the first lines of setup(). In firmware that links
//   the WiFi stack this intermittently deadlocks the main task before loop()
//   ever runs (screen stuck on the splash, nothing else happens). Proven by a
//   matched control on 2026-09-06: identical firmware, only that flag changed,
//   went from freezing reliably to 15 clean cold resets. See atech/STAGES.md.
//
//   Building with CDC_ON_BOOT=0 removes the deadlock but turns `Serial` into
//   UART0 — whose pins are exposed on port 11 of the 14port board — and breaks
//   USB events. This wrapper keeps USB events working:
//     * the build pipeline (scripts/build_flash.py) compiles with CDC_ON_BOOT=0
//       and routes the generated code's `Serial` to atechUsb();
//     * atechUsb().update() runs first in loop() and brings the real USB-CDC
//       (USBSerial) up once the boot has settled (SETTLE_MS);
//     * until then writes are dropped and reads return nothing;
//     * UART0 is never started, so no port pin is touched.
//
//   The canonical copy lives in the skill (scripts/atech_usb.h); modules that
//   talk USB ship an identical copy next to their sources. Keep them identical
//   (include guard, not pragma once: main.cpp may see both copies).
#ifndef ATECH_USB_H
#define ATECH_USB_H
#include <Arduino.h>

#if defined(ATECH_SIM)
// Host simulator: the fake core's Serial records every write; nothing to defer.
inline HardwareSerial& atechUsb() { return Serial; }
#else

#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
#include <HWCDC.h>
#define ATECH_USB_DEV      USBSerial   // the USB-CDC object when boot-init is off
#define ATECH_USB_DEFERRED 1
#else
#define ATECH_USB_DEV      Serial      // unpatched build: Serial already is the USB-CDC, begun by setup()
#define ATECH_USB_DEFERRED 0
#endif

class AtechUsb : public Print {
public:
    static constexpr uint32_t SETTLE_MS = 2000;      // boot settle before the CDC driver starts
    static constexpr uint32_t TX_TIMEOUT_MS = 10;    // bounded; MUST be > 0 (see setTxTimeoutMs below)

    // API-compatible no-ops so the SDK's generated `Serial.begin(115200);
    // Serial.setTxTimeoutMs(0);` compile unchanged once Serial is routed here.
    void begin(unsigned long baud = 0) { (void)baud; }
    void end() {}
    void setTxTimeoutMs(uint32_t) {}
    void setDebugOutput(bool) {}

    // Call once per loop() iteration (the pipeline injects this as loop's first line).
    void update() {
        if (_up) return;
#if ATECH_USB_DEFERRED
        if (millis() < SETTLE_MS) return;
        ATECH_USB_DEV.begin();
        // NOT zero. The HWCDC write loop seeds its unplug-detection counter from
        // this value; with 0 the counter underflows and, when the TX buffer fills
        // because no host is reading, the loop spins forever (the whole "frozen
        // board" bug — see atech/STAGES.md). A small non-zero value lets the
        // driver give up after a bounded wait and drop the data instead of hanging.
        ATECH_USB_DEV.setTxTimeoutMs(TX_TIMEOUT_MS);
#endif
        _up = true;
    }
    bool ready() const { return _up; }
    operator bool() const { return _up && (bool)ATECH_USB_DEV; }   // up AND a host connected

    int  available() { return _up ? ATECH_USB_DEV.available() : 0; }
    int  read()      { return _up ? ATECH_USB_DEV.read() : -1; }
    int  peek()      { return _up ? ATECH_USB_DEV.peek() : -1; }
    void flush()     { if (_up) ATECH_USB_DEV.flush(); }
    size_t write(uint8_t c) override { return _up ? ATECH_USB_DEV.write(c) : 0; }
    size_t write(const uint8_t* b, size_t n) override { return _up ? ATECH_USB_DEV.write(b, n) : 0; }
    using Print::write;

private:
    bool _up = false;
};

// One instance for the whole firmware (function-local static: no global
// definition needed, so every module can include this header).
inline AtechUsb& atechUsb() { static AtechUsb u; return u; }

#endif  // ATECH_SIM
#endif  // ATECH_USB_H
