# Support Boundaries

Supported in Solid v0.1:

- private cross-project C++20 use in repositories controlled by the owner;
- static Core, Lua, and Simulation libraries;
- GCC/Clang on Linux, AppleClang on macOS, and MSVC on Windows;
- single-thread-confined runtime state with concurrent bounded feedback producers;
- one local-filesystem event-log writer per session;
- deterministic simulation, projection replay, and host-assisted verification;
- optional Unix-only Solid Scope development visualization.

Not supported in v0.1: shared-library ABI stability, hostile native plugins, network filesystems, multi-writer logs, encrypted or tamper-evident logs, real hardware, MQTT, LLM integration, voice, biosignals, or final Liquid Layer application behavior.

No third-party license grant is made for Liquid itself. Bundled third-party code retains its own license and is listed in third-party notices.

## Verification matrix

Linux is the developed and continuously verified platform. The standing CI
matrix runs on every landing to `main`: strict warnings-as-errors Release
builds with GCC and Clang, ASan/UBSan, TSan, the Core-only build/install
boundary, the Core coverage thresholds, and installed consumers of
`Liquid::Core`, `Liquid::Lua`, and `Liquid::Simulation`. Keeping this matrix
green is a standing release gate.

macOS AppleClang and Windows MSVC remain source-compatibility targets but are
verified only on demand through the manual Portability workflow, which builds,
tests, installs, and runs the consumers on both platforms. The full
four-platform pass last completed on 17 August 2026; rerun the Portability
workflow before a release candidate or after changing platform-conditional
code.

Solid Scope's Python bridge regressions are Linux-verified: they are
registered only on Linux because the bridge is an owner-operated Linux
development instrument and its suites hang under the macOS kqueue selector on
hosted CI.
