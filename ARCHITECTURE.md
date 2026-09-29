# Architecture

Status: `approved 2026-09-29 by Raul Fonseca`
Layering: `layered`

## Layers and allowed imports

Dependencies point inward: executables/Scope -> Simulation -> Lua -> Core. No cycles between CMake targets or between files. The allowed imports are the target link edges in `CMakeLists.txt`.

| Layer (CMake target) | Paths | May import |
|---|---|---|
| Core domain/runtime (`liquid`, `Liquid::Core`) | `include/liquid/{*.hpp,world,effects,events}/**`, `src/{*.cpp,world,effects,events,runtime}/**`, excluding `FileEventStore*` | Core, Core detail, stdlib. No host I/O (gate G11) |
| Core storage adapter (same target) | `include/liquid/events/FileEventStore.hpp`, `src/events/FileEventStore{,Format}.cpp`, `src/events/FileEventStore{Format,Io}.hpp` | Core, stdlib, POSIX/Win32 file APIs (`#ifdef _WIN32` in `FileEventStoreIo.hpp`) |
| Core detail (not consumer API) | `include/liquid/detail/**` (installed; reached only transitively via `world/World.hpp`); src-private `src/runtime/RuntimeInternals.hpp`, `src/events/{EventInternals,FileEventStoreFormat,FileEventStoreIo}.hpp` | Core only. Never included directly from `include/` (src-private), `apps/` or `examples/` |
| Scripting adapter (`liquid_lua_component`, `Liquid::Lua`) | `include/liquid/scripting/**`, `src/scripting/**`, vendored OBJECT lib `liquid_lua` (`third_party/lua-5.4.8`) | Core (PUBLIC). Lua C API only in `src/scripting/LuaBehaviorRunner.cpp` (PRIVATE include) |
| Simulation (`liquid_simulation`, `Liquid::Simulation`) | `include/liquid/simulation/**`, `src/simulation/**`, `apps/SimulationCli.{hpp,cpp}` | Lua, Core (PUBLIC link to `liquid_lua_component`) |
| Solid Scope (`liquid_scope_scenario`; Unix only; not installed) | `apps/Simulation{Input,Scenario,Trace}.*`, `apps/visualizer/**` (Python/JS) | Simulation. Never a second Runtime |
| Executables | `apps/liquid_sim_cli.cpp` -> `liquid_simulation`; `apps/liquid_sim_trace.cpp` -> `liquid_scope_scenario` | their linked target only |
| Tests / fuzz / examples | `tests/**` (Core, Lua or Simulation per `CMakeLists.txt:473-487`), `fuzz/**` -> Core, `examples/{core,lua,simulation}_consumer/main.cpp` | the target they link, plus Catch2; examples use public headers only |
| Future (spec only, absent) | `Liquid::Authoring` (L1, `LIQUID_BUILD_AUTHORING=OFF` by default) | Core, Lua, Simulation. Nothing links it back |

Vendored and never edited: `third_party/{lua-5.4.8,catch2-3.8.1}`.

Known, accepted quirk: `Liquid::Simulation` compiles `apps/SimulationCli.cpp` and exports `apps/` as a PUBLIC include dir.

Baseline structure (not gated): directory-level include edges loop (root<->`world/`, root<->`effects/`, e.g. `Runtime.hpp`->`world/World.hpp`->`ComponentCodec.hpp`->`effects/EffectTypes.hpp`->`Value.hpp`). The cycle gates therefore apply to the file graph and the target graph.

## Composition root

- Core is a library, so each consumer is its own root. Consumers inject through `Runtime::register_adapter`, `RuntimeOptions::eventStore`, `World::register_component/register_effect_codec/register_system` and `LuaBehaviorRunner::expose_component`.
- `apps/SimulationScenario.cpp`: the Scope root. It wires `InMemoryAdapter`, `MemoryEventStore` and `LuaLifecycleSystem` into a `Runtime`.
- `apps/SimulationCli.cpp`: registers components/systems and a `LuaBehaviorRunner`. It constructs no adapter or event store, and it uses the baseline-only legacy internal registration (`LIQUID_ENABLE_LEGACY_INTERNAL_COMPONENT_REGISTRATION`).

## I/O ports

| Port | Methods | Hides (I/O) | Adapter(s) | Fake for tests |
|---|---|---|---|---|
| `EffectAdapter` (`include/liquid/effects/EffectAdapter.hpp`) | `route()`, `capabilities()`, `dispatch(...)` | device/transport/sink commands | `src/simulation/InMemoryAdapter.cpp` | `InMemoryAdapter`; doubles in `tests/test_runtime_effects.cpp`, `tests/test_contract_edges.cpp`, `tests/test_idempotent_dispatcher.cpp` |
| `EventStore` (`include/liquid/events/EventStore.hpp`) | `metadata`, `append`, `append_batch`, `read_all`, `flush`, `checkpoint`, `retain_from_checkpoint` | durable log, filesystem, fsync | `MemoryEventStore`, `FileEventStore` (`src/events/`) | `MemoryEventStore`; `FileEventStoreFaultPoint` fault injection |
| `PersistentOutcomeCache` (`include/liquid/effects/IdempotentDispatcher.hpp`) | `find`, `store` | durable dispatch outcomes | planned production adapter (durable; not in L0) | `MemoryPersistentCache` in `tests/test_idempotent_dispatcher.cpp` |
| Clock: a frame input, not a port | the host passes monotonic ms `IntentTime now` to `Runtime::run_frame` / `FrameInput`; Lua sees it as `now_ms` | none | none | tests pass literal times |

- Traceability runs through the `EventStore` port. With `RuntimeOptions::eventStore` set, the Runtime records sessions, frames (started/completed/failed), commands, reports, observations and script execution (`EventType` in `include/liquid/events/EventTypes.hpp`), and `FileEventStore` persists them in Solid Event Format v1 (`docs/EVENT_FORMAT_V1.md`).
- Rule: new behaviour must stay written and traceable through this port. Do not add a side channel that bypasses it.
- Randomness and env: none in `include/` or `src/`. The Lua sandbox exposes no `io`/`os`/`package` and removes `math.random`/`randomseed` (`src/scripting/LuaBehaviorRunner.cpp:1036-1057`).
- Only the apps read files directly (`std::ifstream`, `apps/SimulationCli.cpp:205`, `apps/SimulationInput.cpp:209`).

## Variation points

Adding a variant = one new file plus one registration/injection line in its composition root. A variant built inside a repo target also needs its `CMakeLists.txt` source line. External consumers need no repo edits.

| Axis | Registry / injection point | Current variants | Expected second variant | Added |
|---|---|---|---|---|
| Effect adapters | `Runtime::register_adapter(std::shared_ptr<EffectAdapter>)`, keyed by `AdapterRoute`; bound with `bind_effect_component` | `InMemoryAdapter` | physical device adapter (serial/ESP32 hardware); server-telemetry metrics/alert sink; network transport bridge | 2026-09-29 |
| Event stores | `RuntimeOptions::eventStore` | `MemoryEventStore`, `FileEventStore` | remote/streaming store shipping records to a telemetry or log pipeline; embedded-DB store | 2026-09-29 |
| Component codecs | `World::register_component(schema, version, ComponentCodec<T>)`, `World::register_effect_codec`, `LuaBehaviorRunner::expose_component(..., LuaComponentCodec<T>)` | hand-written per-type codecs | L0 schema-driven codecs; telemetry metric/sample component types | 2026-09-29 |
| Systems | `World::register_system<S>(Signature[, SystemPhase])` -> `detail/SystemRegistry` | `LuaLifecycleSystem`, consumer/test systems | L1+ managed-behavior systems; telemetry collection/aggregation systems | 2026-09-29 |
| Persistent outcome caches | `IdempotentDispatcher(maxMemoryOutcomes, std::shared_ptr<PersistentOutcomeCache>, ...)` | test double only | durable production cache (file/DB-backed, after L0) | 2026-09-29 |

Openness rule: new work must not close these seams. Never hard-code a single adapter, store, codec, system or cache where the public port exists. Consumers plug in through the public ports, never through `detail/` or src-private headers. The legacy internal registration macro is baseline-only; new code uses public registration.

## Deliberately not abstracted

- Scripting language: Lua only, behind the `LuaBehaviorRunner` pimpl. No second language is expected.
- Clock: time is a frame input; Core never reads a clock.
- `FeedbackChannel`/`FeedbackSender`/`FeedbackReceiver`: one concrete cross-thread channel.
- File-store OS backend: a compile-time `#ifdef _WIN32` switch inside `FileEventStoreIo.hpp` (one closed set).

## Design gates (calibrated)

- Hermes runs the gates from the repo root, at baseline and on the current state. Ratchet: only new violations block.
- Graph gates cover the whole repo; metric gates cover changed/new files. Reports go to `$RUN_DIR/gates/`. Tools are never installed.
- A gate marked cannot_verify is an absent tool; its result is not a pass.

- G1 strict GCC — `cmake -S . -B build/strict -DCMAKE_BUILD_TYPE=Release -DLIQUID_ENABLE_STRICT_WARNINGS=ON -DLIQUID_WARNINGS_AS_ERRORS=ON && cmake --build build/strict --parallel 2 && ctest --test-dir build/strict --output-on-failure` — 0 warnings/failures — installed: yes.
- G2 strict Clang — same with `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` — cannot_verify locally (clang absent); CI `strict-release` job.
- G3 ASan/UBSan (GCC) — `cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=Debug -DLIQUID_ENABLE_SANITIZERS=ON -DLIQUID_ENABLE_STRICT_WARNINGS=ON -DLIQUID_WARNINGS_AS_ERRORS=ON`, then build and ctest — 0 failures — installed: yes; the Clang run is CI.
- G4 TSan (GCC) — as G3 with `-DLIQUID_ENABLE_THREAD_SANITIZER=ON` in `build/tsan`; never combined with G3/G5 — 0 failures — installed: yes.
- G5 Core coverage — `-DLIQUID_BUILD_LUA=OFF -DLIQUID_BUILD_SIMULATION=OFF -DLIQUID_ENABLE_COVERAGE=ON`, then `cmake --build build/coverage --target coverage` — ≥90% line / ≥80% branch, gcovr 8.3 — cannot_verify locally (gcovr absent, so the target is not generated); CI `coverage` job.
- G6 Core-only and consumers — `build/core` plus `examples/source-tree` and `examples/installed-package`, per `.github/workflows/ci.yml` — builds and passes — installed: yes.
- G7 fuzz smoke — `-DLIQUID_BUILD_FUZZERS=ON`, then `ctest --test-dir <dir> -R _smoke` — 0 failures — standalone: yes; libFuzzer cannot_verify (clang absent).
- G8 inward layering — 0 hits each; installed: yes (grep):
  - Core: `! grep -rEn '#include\s*[<"](liquid/(scripting|simulation)/|lua\.h|lauxlib\.h|lualib\.h|Simulation\w*\.hpp)' include/liquid/*.hpp include/liquid/{detail,world,effects,events} src/*.cpp src/{world,effects,events,runtime}`
  - Scripting: `! grep -rEn '#include\s*"liquid/simulation/' include/liquid/scripting src/scripting`
- G9 src-private stays private — `! grep -rEn '#include\s*"[^"]*(Internals|FileEventStoreIo|FileEventStoreFormat)\.hpp"' include apps examples` — 0 hits — installed: yes.
- G10 no direct `detail/` from apps/examples — `! grep -rEn '#\s*include\s*[<"]([^">]*/)?detail/' apps examples` — 0 hits. Baseline is 0 (checked 2026-09-29), so this is a strict gate, not a ratchet. Transitive reach via `World.hpp` is not counted — installed: yes.
- G11 Core/scripting do no host I/O, time or randomness (Q13) — `! grep -rEn --exclude='FileEventStore*' 'steady_clock|system_clock|high_resolution_clock|random_device|mt19937|\brand\(|getenv|fstream|fopen|::open\(|fsync|<fcntl\.h>|<unistd\.h>' include src` — 0 hits (baseline 0) — installed: yes.
- G12 target cycles — `cmake --graphviz="$RUN_DIR/gates/targets.dot" build/strict && python3 -c 'import re,sys,graphlib;g={};[g.setdefault(a,set()).add(b) for a,b in re.findall(r"\"(node\d+)\" -> \"(node\d+)\"",open(sys.argv[1]).read())];graphlib.TopologicalSorter(g).prepare();print("0 target cycles")' "$RUN_DIR/gates/targets.dot"` — exit 0 — installed: yes (cmake + python3; `dot` is not needed). This re-configures `build/strict`, so treat it like a build command.
- G13 file-level include cycles — the script below — prints "0 cycles"; CycleError = fail — installed: yes (python3 ≥3.9).
```sh
python3 - <<'PY'
import re, sys, graphlib, pathlib as P
g = {}
for f in [p for d in ("include", "src", "apps", "examples") for p in P.Path(d).rglob("*.[ch]pp")]:
    deps = g.setdefault(str(f.resolve()), set())
    for m in re.findall(r'#\s*include\s*"([^"]+)"', f.read_text()):
        c = next((c for c in (P.Path("include") / m, f.parent / m) if c.exists()), None)
        if c: deps.add(str(c.resolve()))
try: graphlib.TopologicalSorter(g).prepare(); print("0 cycles")
except graphlib.CycleError as e: sys.exit(f"cycle: {e.args[1]}")
PY
```
- G14 size/complexity (rule 12) — `lizard -l cpp -C 10 -L 60 -a 5 -w -x "./third_party/*" -x "./build/*" <changed files>` — CCN ≤10, ≤60 lines, ≤5 params; ratchet on changed files — cannot_verify (lizard absent).
- G15 clang-tidy — `run-clang-tidy -p build/strict <changed files>` with `readability-function-cognitive-complexity` (15), `readability-function-size` (params 5, nesting 4), `misc-include-cleaner`, `WarningsAsErrors: '*'` — 0 new — cannot_verify (clang-tidy absent).
- G16 cppcheck — `cppcheck --project=build/strict/compile_commands.json --enable=warning,style,performance,portability --inline-suppr --error-exitcode=1 -i build -i third_party` — 0 new — cannot_verify (absent).
- G17 duplication (Q17) — `npx --no jscpd --format cpp --min-tokens 70 --threshold 3 --reporters console,json --output "$RUN_DIR/gates/jscpd-<phase>" include src apps` — no new clone pair touching changed files — cannot_verify (jscpd absent; `--no` refuses to fetch it).
- Formatting — no `.clang-format` in the repo and clang-format is absent — no gate.

## Quarterly review

- Review "Variation points" every quarter. An axis that still has one implementation after two quarters loses its registry: inline it back and remove the row.
- Exception: the five declared axes are public API seams of Solid v0.1.0. Retiring one needs owner approval and a compatibility decision, not the two-quarter rule.
- Last review: not yet
