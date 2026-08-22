# Package wiring contract

The root build will configure `LiquidConfig.cmake.in` with `configure_package_config_file` and generate `LiquidConfigVersion.cmake` with `write_basic_package_version_file(... COMPATIBILITY SameMinorVersion)`.

Each optional component has its own export file so a Core-only consumer does not load Lua or Simulation:

- `LiquidCoreTargets.cmake` exports `Liquid::Core`;
- `LiquidLuaTargets.cmake` exports `Liquid::Lua` and depends on `Liquid::Core`;
- `LiquidSimulationTargets.cmake` exports `Liquid::Simulation` and depends on both `Liquid::Core` and `Liquid::Lua`.

All installed target include paths use relocatable `INSTALL_INTERFACE`
locations. The package config treats Core as mandatory, rejects unknown
required components, and reports a configured-but-unavailable component
through CMake's standard `check_required_components` behavior.

The source-tree and installed-package projects under `examples/` are executable
contract consumers. Their Core path uses Runtime, World, and MemoryEventStore;
the optional paths execute Lua and the in-memory simulation adapter. CI also
installs a Core-only package and verifies that Lua and Simulation targets are
not exposed to that consumer.
