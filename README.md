# Liquid

Liquid is a private C++20 framework for deterministic, inspectable, and replayable adaptive-environment runtimes. Its completed deterministic layer is called Solid; later Liquid stages will build adaptive behavior above that boundary.

Solid v0.1.0 headless implementation is complete through S5. Solid Scope synchronization and remote release gates remain, so this is not yet a release declaration.

## Build and test the checkout

```sh
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DLIQUID_ENABLE_STRICT_WARNINGS=ON \
    -DLIQUID_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Tests use the official Catch2 v3.8.1 amalgamated distribution and remain
individually registered with CTest. The pinned source, verification hashes, and
small local integration boundary are documented in
[`third_party/catch2-3.8.1/README.md`](third_party/catch2-3.8.1/README.md).

The v0.1 package provides static targets:

| Component | Target | Purpose |
| --- | --- | --- |
| Core | `Liquid::Core` | Values, world/runtime APIs, effects, event stores, and replay |
| Lua | `Liquid::Lua` | Sandboxed Lua behavior execution with pinned Lua 5.4.8 |
| Simulation | `Liquid::Simulation` | In-memory adapter and deterministic scenario support |

Core-only consumers do not require Lua. Source-tree consumers use `add_subdirectory`; installed consumers use:

```cmake
find_package(Liquid 0.1 CONFIG REQUIRED COMPONENTS Core)
target_link_libraries(my_app PRIVATE Liquid::Core)
```

See [the integration guide](docs/INTEGRATION_GUIDE.md) and the minimal [source-tree](examples/source-tree/CMakeLists.txt) and [installed-package](examples/installed-package/CMakeLists.txt) consumers.

The consumer projects compile and run separate Core, Lua, and Simulation
executables. A Core-only configuration disables both optional components and
fails configuration if either optional imported target becomes visible.

Optional verification configurations are owned by CMake so CI and local runs
use the same instrumentation:

```sh
# AddressSanitizer + UndefinedBehaviorSanitizer
cmake -S . -B build-asan -DLIQUID_ENABLE_SANITIZERS=ON

# ThreadSanitizer (GCC/Clang)
cmake -S . -B build-tsan -DLIQUID_ENABLE_THREAD_SANITIZER=ON

# Core coverage; creates the target when gcovr is available
cmake -S . -B build-coverage \
    -DLIQUID_BUILD_LUA=OFF \
    -DLIQUID_BUILD_SIMULATION=OFF \
    -DLIQUID_ENABLE_COVERAGE=ON
cmake --build build-coverage --target coverage
```

The coverage target runs the tests and enforces at least 90% line and 80%
branch coverage over `include/liquid` and `src`.

## Contracts and guides

- [Public API](docs/PUBLIC_API.md), [compatibility](docs/COMPATIBILITY.md), and [support boundaries](docs/SUPPORT.md)
- [Adapter contract](docs/ADAPTER_CONTRACT.md) and [adapter integration guide](docs/ADAPTER_GUIDE.md)
- [Lifecycle scripting and frame phases](docs/LIFECYCLE_SCRIPTING.md)
- [Replay contract](docs/REPLAY.md) and [replay operations guide](docs/REPLAY_GUIDE.md)
- [Event format v1](docs/EVENT_FORMAT_V1.md) and [file-store guide](docs/FILE_FORMAT_GUIDE.md)
- [Threading](docs/THREADING.md) and [security boundary](docs/SECURITY_BOUNDARY.md)
- [Completion evidence](COMPLETE_SOLID.md) and [traceability matrix](docs/TRACEABILITY.md)

## Support and licensing

The v0.1 support target is static-library use with GCC or Clang on Linux, AppleClang on macOS, and MSVC on Windows. Solid Scope is an optional Unix-only development application. Real hardware, MQTT, LLM integration, voice, biosignals, and the final Liquid Layer application are outside this release.

Liquid itself is private and has no third-party license grant. See [LICENSE-NOTICE.md](LICENSE-NOTICE.md). Third-party components retain their own terms; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
