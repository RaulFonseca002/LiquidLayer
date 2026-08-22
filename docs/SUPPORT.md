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

Linux is the developed and continuously verified platform. The v0.1.0 release
matrix passed locally on 22 August 2026: strict warnings-as-errors Release
builds with GCC 14 and Clang 19, ASan/UBSan, TSan, the Core-only build/install
boundary, the 90% line / 80% branch Core coverage thresholds, decoder fuzz
smoke, and source-tree and installed consumers. The GitHub matrix remains a
regression signal, but the owner did not require another remote run for this
release after its first full green pass on 17 August 2026.

macOS AppleClang and Windows MSVC remain source-compatibility targets but are
verified only on demand through the manual Portability workflow, which builds,
tests, installs, and runs the consumers on both platforms. The full
four-platform pass last completed on 17 August 2026. Neither platform was
rerun for the v0.1.0 tag because the final fixes were Linux-owned runtime,
test, package-boundary, and Scope changes without a portability claim beyond
that last pass.

Solid Scope's Python bridge regressions are Linux-verified: they are
registered only on Linux because the bridge is an owner-operated Linux
development instrument and its suites hang under the macOS kqueue selector on
hosted CI.

Scope has no general browser compatibility guarantee. Its dependency-free
self-test covers the owned presentation paths, while the bridge validates the
bounded trace envelope and transport framing rather than exhaustively
revalidating every trusted `liquid_sim_trace` event field. A repeat manual
browser matrix was explicitly waived for v0.1.0; browser breakage outside the
owner-selected development setup is an accepted low-severity support limit.
