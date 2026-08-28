# Liquid

**A deterministic C++20 runtime with a model-facing control layer for adaptive environments—designed to turn intent into verified, replayable state changes.**

[![CI](https://github.com/RaulFonseca002/tcc/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/RaulFonseca002/tcc/actions/workflows/ci.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/20)
[![CMake 3.20+](https://img.shields.io/badge/CMake-3.20%2B-064F8C?logo=cmake)](https://cmake.org/)
[![License: Private](https://img.shields.io/badge/license-private-7C3AED)](LICENSE-NOTICE.md)

[Get started](#build-and-test) · [Understand the runtime](#how-solid-works) · [Stage 2 plan](docs/LIQUID_STAGE2_PLAN.md) · [Integrate](#use-liquid-from-another-project) · [Read the contracts](#documentation-map)

![Liquid runtime flow: intent proposals are resolved, dispatched through bounded adapters, and committed only after validated feedback.](docs/liquid-runtime-flow.svg)

> [!IMPORTANT]
> **Current status:** Solid v0.1.0 is complete. All S0-S7 finalization gates closed on 22 August 2026. Stage 2 **Liquid** is now active with **L0 — Model-Facing Lua Capability Contract** as the first implementation milestone. L0 will add machine-readable schema/capability discovery to the existing Lua boundary; it does not add a model provider, MCP server, or application-specific adaptive policy.

| Deterministic core | Model-facing boundary | Evidence-first effects |
| --- | --- | --- |
| Immutable intents, ordered phases, world-bound handles | Capability-bounded Lua authoring/inspection grows incrementally in Stage 2 | Desired, commanded, reported, and observed state stay distinct |

## Why Liquid exists

Adaptive environments need both flexibility and a trustworthy execution boundary.

Liquid is split conceptually into three layers:

- **Solid** — the completed deterministic foundation: state, behaviors, intents, resolution, effects, feedback, event records, replay, Lua lifecycle execution, and simulation.
- **Liquid** — the reusable model-facing control and authoring layer over Solid. It makes Solid understandable and safely operable by external models/agents without putting those models inside the deterministic Runtime.
- **Liquid Layer** — the future neurodivergent-support smart-environment application. It owns user/environment meaning, sensors, AI routing, conversation/memory policy, and real device integration.

A local model, hosted model call, Hermes Agent, or another future agent runtime can all be application choices over the same Liquid surface. Liquid itself does not choose the provider or decide when inference is appropriate.

The design test is simple: if all models and agents are turned off, already approved behaviors continue to execute, lose and regain intent resolution, expire/cancel deterministically, dispatch effects, validate feedback, and preserve evidence under Solid alone.

## How Solid works

```text
trusted C++ or sandboxed Lua behavior
    → immutable intent proposal
    → deterministic phase execution and conflict resolution
    → typed effect command through a bounded adapter
    → applied / rejected / failed / pending report
    → validation on the Runtime-owning thread
    → authoritative observed state
    → durable evidence and replay
```

A selected value is only a **desire**. Solid updates externally controlled state only after a correlated, validated report or revisioned observation proves what happened.

A live intent that loses conflict resolution is not destroyed merely because another intent wins. Persistent desires remain available to win again when the competitor disappears.

### Core guarantees

- **Deterministic resolution** — intents are immutable after creation and resolve through explicit lifetime, priority, and ordering rules.
- **No false physical truth** — selected intent, dispatched command, reported result, and observed state are different evidence stages.
- **Persistent preference semantics** — a losing live intent remains live until normal expiration/cleanup/cancellation removes it.
- **Bounded authority** — adapters receive commands, not `World`, registries, component slots, or mutation access.
- **Replayable evidence** — memory and file event stores support projection replay and host-assisted verification.
- **Controlled scripting** — Lua runs in fresh, capability-limited sandboxes with transactional proposal/cancellation bundles.
- **Portable verification** — the release matrix covers strict GCC/Clang builds, sanitizers, coverage, fuzz smoke, and consumer-package gates; AppleClang and MSVC remain on-demand source-compatibility targets.

## Stage 2 direction

Liquid grows only where a real model-facing requirement is missing from Solid.

The target relationship is:

```text
                         Liquid Layer
       application context / AI selection / invocation policy
           ┌──────────────┼──────────────┐
           │              │              │
      local model     hosted model     agent runtime
           │              │              │
           │              │             MCP
           └──────────────┴───────┬──────┘
                                  ▼
                               Liquid
                  discover / author / observe
                  validate / evaluate / operate
                                  │
                                  ▼
                                Solid
                    deterministic authority
```

Current L0 remains inside the existing `Liquid::Lua` boundary. The approved milestone will add a bounded model-visible `LuaValueSchema` and immutable capability manifest built from trusted Lua bindings, current behavior permissions, exact host-generated access paths, copied readable snapshots, current monotonic time, and a stable authoring-contract marker.

No L0 code calls an LLM. No new provider/agent/MCP dependency is introduced. See [the Stage 2 implementation plan](docs/LIQUID_STAGE2_PLAN.md) for milestones, acceptance scenarios, rejected alternatives, and current external research.

## Build and test

### Requirements

- CMake 3.20 or newer
- A C++20 compiler:
  - GCC or Clang on Linux
  - AppleClang on macOS
  - MSVC on Windows

### Strict Release build

```sh
git clone https://github.com/RaulFonseca002/tcc.git
cd tcc

cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON \
    -DLIQUID_ENABLE_STRICT_WARNINGS=ON \
    -DLIQUID_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The checkout vendors the official Catch2 v3.8.1 amalgamation and Lua 5.4.8, so the default test build does not need to download either dependency. Their provenance and integration boundaries are documented under [`third_party/`](third_party/).

## Use Liquid from another project

Solid v0.1 is packaged through three static targets so consumers pay only for the boundary they need:

| Component | CMake target | Current use |
| --- | --- | --- |
| Core | `Liquid::Core` | Values, world/runtime APIs, effects, event stores, and replay |
| Lua | `Liquid::Lua` | Capability-bounded Lua behavior execution; L0 is planned to extend this same target with model-facing authoring metadata |
| Simulation | `Liquid::Simulation` | In-memory adapters and deterministic scenarios |

L0 intentionally extends `Liquid::Lua` rather than creating a speculative new exported target.

### Source-tree integration

```cmake
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/liquid)

target_link_libraries(my_app PRIVATE Liquid::Core)
```

### Installed-package integration

```cmake
find_package(Liquid 0.1 CONFIG REQUIRED COMPONENTS Core)
target_link_libraries(my_app PRIVATE Liquid::Core)
```

Core-only consumers can disable both optional components:

```sh
cmake -S . -B build/core-only \
    -DLIQUID_BUILD_LUA=OFF \
    -DLIQUID_BUILD_SIMULATION=OFF
```

Runnable source-tree and installed-package consumers cover all three exported targets in [`examples/`](examples/). See the [integration guide](docs/INTEGRATION_GUIDE.md) for the complete v0.1 contract.

## Verification profiles

The same CMake switches power local checks and CI:

```sh
# AddressSanitizer + UndefinedBehaviorSanitizer
cmake -S . -B build/asan -DLIQUID_ENABLE_SANITIZERS=ON

# ThreadSanitizer — GCC or Clang
cmake -S . -B build/tsan -DLIQUID_ENABLE_THREAD_SANITIZER=ON

# Core coverage — requires gcovr
cmake -S . -B build/coverage \
    -DLIQUID_BUILD_LUA=OFF \
    -DLIQUID_BUILD_SIMULATION=OFF \
    -DLIQUID_ENABLE_COVERAGE=ON
cmake --build build/coverage --target coverage
```

The Core coverage target enforces at least **90% line** and **80% branch** coverage over `include/liquid` and `src`. CI also installs the package and compiles independent consumers to validate the exported CMake surface.

## Architecture at a glance

| Boundary | Responsibility |
| --- | --- |
| `World` | Public Solid state and behavior/component lifecycle |
| `Runtime` | Owner-thread frame phases, resolution, dispatch, feedback, and evidence |
| `EffectAdapter` | Narrow command transport without world mutation authority |
| `EventStore` | Durable or in-memory runtime records |
| Replay | Projection and host-assisted verification from recorded evidence |
| Lua | Fresh sandbox snapshots and allowlisted proposal capabilities |
| Liquid model-facing API | Stage 2 bounded copied capability/runtime views and later proposal/evaluation operations |
| MCP (future) | Transport adapter over the Liquid semantic API; not the domain model |

Runtime state is confined to its owner thread. Only the bounded feedback sender is intended for concurrent producers. Future model/network surfaces must cross this boundary through immutable copies and owner-controlled mutation requests rather than calling `World`/`Runtime` from arbitrary threads.

## Documentation map

### Stage 2 / current work

- [Liquid Stage 2 plan](docs/LIQUID_STAGE2_PLAN.md) — accepted architecture, L0 scope, provisional roadmap, research, and rejected alternatives
- [Development tracking](DEVELOPMENT_TRACKING.md) — current milestone, allowed implementation area, evidence, and advancement rules
- [Concepts and architecture](Liquid_Concepts_and_Architecture.md) — Solid/Liquid/Liquid Layer responsibilities and vocabulary
- [AGENTS.md](AGENTS.md) — operational coding-agent scope

### Solid runtime contracts

- [Public API](docs/PUBLIC_API.md) — supported symbols and ownership rules
- [Lifecycle scripting](docs/LIFECYCLE_SCRIPTING.md) — executable Lua lifecycle contract
- [Threading model](docs/THREADING.md) — owner-thread and feedback boundary
- [Adapter contract](docs/ADAPTER_CONTRACT.md) and [adapter guide](docs/ADAPTER_GUIDE.md)
- [Event format v1](docs/EVENT_FORMAT_V1.md) and [file-store guide](docs/FILE_FORMAT_GUIDE.md)
- [Replay contract](docs/REPLAY.md) and [replay operations](docs/REPLAY_GUIDE.md)
- [Security boundary](docs/SECURITY_BOUNDARY.md)
- [Support boundaries](docs/SUPPORT.md)
- [Compatibility policy](docs/COMPATIBILITY.md)

### Project evidence

- [Solid completion audit](COMPLETE_SOLID.md)
- [Traceability matrix](docs/TRACEABILITY.md)
- [Changelog](CHANGELOG.md)

## Security and support boundaries

Solid is an in-process framework, not a sandbox for hostile native host code. Hosts, adapters, codecs, and systems are trusted integration code. The Lua boundary is capability-based and bounded, while event decoders validate size, depth, UTF-8, finite-number, version, and sequence limits before allocation or mutation.

Stage 2 does not change that authority model. Model output is untrusted candidate data even when a provider offers structured/constrained generation. Liquid/Solid validation remains authoritative.

The v0.1 file store detects accidental corruption with CRC32C; it does **not** provide encryption, authentication, or tamper evidence. Network filesystems, multiple writers, shared-library ABI stability, and hostile native plugins are unsupported.

Read [SECURITY_BOUNDARY.md](docs/SECURITY_BOUNDARY.md) before integrating external adapters or persisted event files.

## License

Copyright is reserved by the repository owner. No license is granted for Liquid or Solid beyond owner-controlled repositories and projects unless separate written permission is provided.

See [LICENSE-NOTICE.md](LICENSE-NOTICE.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the exact terms.
