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

## Compatibility

*(formerly `docs/COMPATIBILITY.md`, "Compatibility Policy"; text verbatim. Its platform sentence restates the platform list above.)*

Version 0.1.0 targets C++20 static libraries on Linux with GCC or Clang, macOS with AppleClang, and Windows with MSVC. Core-only consumers do not require Lua. Solid Scope is a separate optional Unix-only development application.

Solid Scope is an owner-operated internal instrument, not a supported browser
product. Version 0.1.0 makes no cross-browser or future-browser compatibility
promise for its presentation layer; the loopback bridge and dependency-free
presentation self-test are the supported regression boundary.

Before 1.0, minor releases may make documented source-breaking changes; patch releases preserve source compatibility for the documented public surface. No stable binary ABI is promised. The event file format is versioned independently: v1 readers reject unknown file, batch, record, or canonical-value versions unless an explicitly documented forward-compatible envelope permits skipping them.

Installed package compatibility uses CMake `SameMinorVersion`. Supported behavior is defined by installed public headers and exported targets, not by implementation headers reachable in a source checkout.

## Security boundary

*(formerly `docs/SECURITY_BOUNDARY.md`, "Security Boundary"; text verbatim)*

Solid is an in-process framework, not a security sandbox for hostile native host code. Hosts control files, threads, adapters, codecs, systems, and operating-system access. Event files can contain scripts and component data and must be protected with OS permissions.

The Lua boundary is capability-based and bounded: scripts receive copied snapshots and allowlisted proposal functions, never runtime internals or owner selection. Codecs are trusted validation boundaries. Decoders enforce size, depth, UTF-8, finite-number, version, and sequence limits before allocation or mutation.

The file store detects accidental corruption with CRC32C but provides no confidentiality, authentication, or tamper evidence. v0.1 supports one local-filesystem writer with an OS lock; network filesystems are unsupported. Adapters are trusted integration code but do not receive state authority. Reports are untrusted data until correlated and validated by `Runtime`.

*(formerly `README.md` § Security and support boundaries; text verbatim)*

Solid is an in-process framework, not a sandbox for hostile native host code. Hosts, adapters, codecs, and systems are trusted integration code. The Lua boundary is capability-based and bounded, while event decoders validate size, depth, UTF-8, finite-number, version, and sequence limits before allocation or mutation.

Stage 2 does not change that authority model. Model output is untrusted candidate data even when a provider offers structured/constrained generation. Liquid/Solid validation remains authoritative.

The v0.1 file store detects accidental corruption with CRC32C; it does **not** provide encryption, authentication, or tamper evidence. Network filesystems, multiple writers, shared-library ABI stability, and hostile native plugins are unsupported.
