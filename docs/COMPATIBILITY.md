# Compatibility Policy

Version 0.1.0 targets C++20 static libraries on Linux with GCC or Clang, macOS with AppleClang, and Windows with MSVC. Core-only consumers do not require Lua. Solid Scope is a separate optional Unix-only development application.

Before 1.0, minor releases may make documented source-breaking changes; patch releases preserve source compatibility for the documented public surface. No stable binary ABI is promised. The event file format is versioned independently: v1 readers reject unknown file, batch, record, or canonical-value versions unless an explicitly documented forward-compatible envelope permits skipping them.

Installed package compatibility uses CMake `SameMinorVersion`. Supported behavior is defined by installed public headers and exported targets, not by implementation headers reachable in a source checkout.
