# Package wiring contract

The root build configures `LiquidConfig.cmake.in` with `configure_package_config_file` and generates `LiquidConfigVersion.cmake` with `write_basic_package_version_file(... COMPATIBILITY SameMinorVersion)`.

Each optional component has its own export file so a Core-only consumer does not load Lua or Simulation:

- `LiquidCoreTargets.cmake` exports `Liquid::Core`;
- `LiquidLuaTargets.cmake` exports `Liquid::Lua` and depends on `Liquid::Core`;
- `LiquidSimulationTargets.cmake` exports `Liquid::Simulation` and depends on both `Liquid::Core` and `Liquid::Lua`;
- `LiquidAuthoringTargets.cmake` exports `Liquid::Authoring` and depends on `Liquid::Simulation` (and through it on Lua and Core).

Authoring ([L1](../docs/LIQUID_L1_IMPLEMENTATION_SPEC.md); unreleased, awaiting
review) is opt-in: `LIQUID_BUILD_AUTHORING` is `OFF` by default, and enabling it
requires `LIQUID_BUILD_LUA` and `LIQUID_BUILD_SIMULATION` (configuration fails
otherwise). Only an Authoring build installs `LiquidAuthoringTargets.cmake` and
the `liquid/authoring/` headers; the generic header install excludes that
directory. A consumer requests it with `find_package(Liquid CONFIG REQUIRED
COMPONENTS Authoring)`, and the package config loads Core, Lua and Simulation
before it. Requesting Authoring from a package built without it fails with
"Liquid package built without Authoring". Existing Core, Lua and Simulation
consumers are unchanged.

All installed target include paths use relocatable `INSTALL_INTERFACE`
locations. The package config treats Core as mandatory, rejects unknown
required components, and reports a configured-but-unavailable component
through CMake's standard `check_required_components` behavior.

The source-tree and installed-package projects under `examples/` are executable
contract consumers. Their Core path uses Runtime, World, and MemoryEventStore;
the optional paths execute Lua and the in-memory simulation adapter. CI also
installs a Core-only package and verifies that Lua and Simulation targets are
not exposed to that consumer.
