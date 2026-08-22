# Integration Guide

Status: released v0.1.0 guide. The S5 and S7 package evidence is complete in
the traceability matrix.

## Requirements

- CMake 3.20 or newer
- A C++20 compiler: GCC or Clang on Linux, AppleClang on macOS, or MSVC on Windows
- Static linking

## Source-tree use

Add Liquid below the consuming project and link only the components required by the application:

```cmake
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/liquid)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE Liquid::Core)
```

The runnable contract test is in `examples/source-tree`:

```sh
cmake -S examples/source-tree -B build/source-consumer \
    -DLIQUID_SOURCE_DIR="$PWD"
cmake --build build/source-consumer --config Release
```

## Installed package use

After building and installing Liquid to a private prefix, configure the consumer against that prefix:

```sh
cmake --install build --prefix "$PWD/build/prefix" --config Release
cmake -S examples/installed-package -B build/installed-consumer \
    -DCMAKE_PREFIX_PATH="$PWD/build/prefix"
cmake --build build/installed-consumer --config Release
```

The consumer contract is:

```cmake
find_package(Liquid 0.1 CONFIG REQUIRED COMPONENTS Core)
target_link_libraries(my_app PRIVATE Liquid::Core)
```

Request `Lua` or `Simulation` only when needed. A Core-only consumer must configure and link without Lua. Unknown or unavailable required components make package discovery fail with a bounded diagnostic.

The installed package exports exactly `Liquid::Core`, `Liquid::Lua`, and
`Liquid::Simulation` when their components are built. Vendored Lua objects are
folded into `Liquid::Lua`; no private vendor target is exported.

## Compatibility and ownership

Public code includes only installed `liquid/` headers and refers only to symbols under `liquid::`. Do not include registries, coordinator, world-state, storage, raw-slot, or other implementation headers even when a source checkout makes them reachable.

Runtime/world state is confined to its owner thread. Only the bounded feedback sender is intended for concurrent producers. Treat returned const component references as short-lived borrows and use transactional update/replace APIs for mutation.

See [PUBLIC_API.md](PUBLIC_API.md), [THREADING.md](THREADING.md), and [COMPATIBILITY.md](COMPATIBILITY.md) before upgrading a 0.x dependency.
