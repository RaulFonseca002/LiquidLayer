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

The release CI definition exercises strict warnings as errors in Release mode
with Linux GCC, Linux Clang, macOS AppleClang, and Windows MSVC. It also builds,
installs, and runs consumers of `Liquid::Core`, `Liquid::Lua`, and
`Liquid::Simulation` on each platform.

Separate Linux jobs exercise ASan/UBSan, TSan, the Core-only build/install
boundary, and the Core coverage thresholds. Sanitizer support is limited to
the GCC/Clang configurations accepted by the corresponding CMake options.
These jobs define release gates; their presence is not evidence that a release
candidate has passed them.
