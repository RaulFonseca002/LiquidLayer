# L6 — Optional local MCP adapter

**Status:** specified; inactive. **Dependency:** L5 accepted and landed.
Apply [the common contract](LIQUID_IMPLEMENTATION_CONTRACT.md).

## Outcome and implementation choice

Expose discovery, proposal submission/evaluation, inspection and history through
local stdio MCP, without exposing approval, activation, replacement, stop, grants
or Runtime frame control. Native application code keeps those authorities.

Use Python 3.12 and the official Python SDK `mcp==2.1.1`, protocol revision
`2026-07-28`. Use a separate C++ `liquid_authoring_host` process linked to
`Liquid::Authoring` for semantic operations. JSON framing uses application-only
`nlohmann_json` version `3.12.0`. Neither dependency enters an installed engine
target. Do not write an MCP implementation in C++ or add a new C ABI.

These are explicit proposed dependencies, not currently installed requirements.
At L6 implementation, lock Python transitive dependencies and record hashes;
fail if the specified releases cannot be obtained, rather than silently upgrade
or use a different protocol. A later version change requires a documented
dependency update and rerun of the transport conformance tests.

## Process and authority boundary

The MCP client launches the Python stdio server. It launches the C++ host with
an exact configured executable path and fixed host-owned fixture name; no shell.
The child owns its Runtime on its main thread and serves one bounded request at
a time over stdin/stdout. Python receives serialized copies, never C++ pointers.
Logs go only to stderr; stdout contains protocol messages exclusively.

This first executable is an owner-operated hardware-free `lighting-v1` host
using L2's fixture registration and InMemoryAdapter. It proves the transport
boundary without remote networking, OAuth, arbitrary plugins or production
adapters. It prepares fixed initial frames before serving requests; it does not
implement a real-time application scheduler. Evaluation creates separate worlds.
Production application embedding remains native API use, not this test host.

The host assigns one CallerContext and allowed ScopeId for the process. Requests
can reference only records in that scope. No tool parameter can replace the
caller, select files/executables, install codecs, create grants or switch scope.
Scope identity is explicit in returned artifacts; reconnecting creates a fresh
session and invalidates old artifact IDs. Do not infer domain state from MCP
connection state.

## Tool surface

Every input object rejects unknown fields. Native operations revalidate every
argument; SDK validation is not sufficient. Names and fields below are fixed.

| Tool | Arguments | Semantic call |
| --- | --- | --- |
| `liquid_discover` | `{}` | discover current configured scope |
| `liquid_propose` | `scopeRevision`, `source`, optional `rationale`, optional complete `managedId`/`expectedRevision` pair | submit immutable proposal |
| `liquid_evaluate` | `proposalId`, `fixtureId` | evaluate an allowlisted suite |
| `liquid_inspect` | `{}` | capture current scope view |
| `liquid_history` | optional `cursor`, optional `limit` | scoped journal page |

Return structured content containing `schemaVersion: 1`, `sessionId`, and either
`value` or `error{code, diagnostic, fieldPath?}`. Semantic errors set `isError`;
malformed JSON-RPC/method/protocol errors use SDK protocol errors. Read tools are
annotated read-only; propose/evaluate are not. Annotations never authorize calls.
Render results without injecting runtime strings into trusted descriptions.

Approval tokens, policy callbacks, host routes, raw Solid IDs, fixture private
inputs and unfiltered traces are not serialized. Do not advertise an unavailable
tool for activation and then rely on the model not to call it.

## Native wire and exact values

The private child wire is NDJSON, separate from MCP. Request shape:
`{version:1, requestId:string, method:string, arguments:object}`. Response echoes
version/requestId and exactly one `value` or common `error`. Only the five
methods above are accepted. IDs/counters are canonical decimal strings except
the host session identity; never transmit uint64 values as JSON numbers.

Tagged value encoding is recursive:

| Native value | JSON representation |
| --- | --- |
| Boolean | `{kind:"boolean", value:true/false}` |
| Lua int64 / Solid signed integer | `{kind:"integer", value:"<canonical decimal>"}` |
| Solid uint64 | `{kind:"unsigned", value:"<canonical decimal>"}` |
| Finite double | `{kind:"number", value:"<round-tripping decimal>"}` |
| Lua string / Solid bytes | `{kind:"bytes", base64:"<RFC4648 padded standard base64>"}` |
| Array | `{kind:"array", items:[tagged values]}` |
| String-keyed object | `{kind:"object", fields:[{nameBase64, value}]}` |
| Solid null | `{kind:"null"}`; never a Lua schema kind |

For doubles use locale-independent `to_chars` with `max_digits10`; preserve
negative zero and the explicit Number tag even when the text is integral.
Reject nonfinite values. Object fields are sorted by unsigned key bytes and
duplicates rejected. Metadata identity names and schema keys use `nameBase64`;
an optional valid-UTF-8 display label is non-authoritative. Lua access expressions
are host-generated ASCII and remain directly readable. Descriptions/source are
validated UTF-8; arbitrary component values are data, including invalid UTF-8.

This avoids loss of int64 precision, Number/Integer identity, embedded NUL,
non-UTF-8 bytes and empty-array identity through JSON. Do not send Lua schemas
as if they were arbitrary JSON Schema or infer native value kinds from JSON.
The SDK tool schemas describe these envelopes, not native codec authority.

## Bounds, cancellation and failure

Both processes cap input/output at 4 MiB per complete line. Read incrementally
with a byte counter; ordinary unbounded `getline`/`readline` followed by a size
check is insufficient. Reject JSON nesting beyond 128 (root counts as 1), more
than 131,072 JSON nodes, and duplicate JSON object keys before building an
unrestricted DOM; use nlohmann's SAX interface for bounded
native decoding. Enforce common semantic limits after framing as well.
The larger wire-depth ceiling accommodates tagged objects/field arrays around
each native node; it does not raise the native schema/value nesting allowance.
Test maximum-depth legal native values after wrapping in a complete response,
not just shallow values or a bare tagged node.

Python serializes child access with one request in flight and no unbounded
queue. A second concurrent request returns a bounded busy error. Normal request
deadline is 30 seconds; evaluation remains bounded by its own case/frame/VM
budgets. Timeout or caller cancellation terminates and reaps the child and
invalidates the complete session. Do not automatically replay a partially
submitted request into a new session. This is safe because no live operations
or physical adapters exist in this host. A later production bridge needs its
own cancellation/continuation contract.

On EOF, malformed child output, mismatched request ID or output overflow, close
the session, terminate/reap the process and return HostError. No background child
or zombie may remain. No network listener is opened. The MVP process-management
and MCP integration tests are Linux-only; engine targets retain existing source
portability and no new platform behavior is hidden in Core.

## Implementation steps and tests

| Step | Implement after its failing test | Required oracle |
| --- | --- | --- |
| L6.1 | Bounded native wire codec and fixture host | Tagged int64/double/negative-zero/bytes/empty-array round trips; duplicate keys, depth and exact line bounds; unknown methods denied |
| L6.2 | Python SDK tools and process bridge | Every method matches the direct native semantic result; five tools only; no authority/identity injection; JSON schemas reject extra fields |
| L6.3 | Independent transport conformance | Official SDK client plus a separate raw protocol subprocess driver both discover/call; required 2026-07-28 metadata and structured errors correct |
| L6.4 | Lifecycle and isolation failures | Parallel request rejection, cancelled/timed-out/malformed child teardown, EOF, cross-session IDs, no stdout logs, no orphan process; engine-only build has no SDK/JSON dependency |

Register `authoring_wire` and Linux-only `authoring_mcp`. Add the tests to CTest
only when `LIQUID_BUILD_MCP_TOOL=ON`; default OFF, requiring Authoring ON and
exact `find_package(nlohmann_json 3.12.0 EXACT REQUIRED)`. Configure Python via
`Python3_EXECUTABLE` pointing to the locked environment. Required commands:

```sh
uv sync --project apps/liquid_mcp --frozen
cmake -S . -B build/mcp -DCMAKE_BUILD_TYPE=Release -DLIQUID_ENABLE_STRICT_WARNINGS=ON -DLIQUID_WARNINGS_AS_ERRORS=ON -DLIQUID_BUILD_AUTHORING=ON -DLIQUID_BUILD_MCP_TOOL=ON -DPython3_EXECUTABLE="$PWD/apps/liquid_mcp/.venv/bin/python"
cmake --build build/mcp --parallel 2
ctest --test-dir build/mcp -R '^authoring_(wire|mcp)$' --output-on-failure
ctest --test-dir build/mcp --output-on-failure
```

The implementation commit includes `uv.lock`; generating it precedes the frozen
verification command. Dependency provisioning must supply exact nlohmann_json
3.12.0 and its release provenance; no engine configure-time network fetch.
Run all common engine gates with MCP tooling OFF as a separate configuration.

## Allowed files and exit

New: `apps/liquid_authoring_host.cpp`, `apps/AuthoringWire.hpp`,
`apps/AuthoringWire.cpp`, `apps/liquid_mcp/server.py`, `pyproject.toml`,
`uv.lock`, `README.md` under that app directory, `tests/test_authoring_wire.cpp`,
`tests/test_authoring_mcp.py`. Existing: CMake test/tool wiring, third-party
notices and affected docs. Exclude the tool/app from installed exports.

Exit: a local external author can use the same scoped semantics through two
verified protocol paths. Hermes or Claude may be an additional manual smoke
client, but neither subscription, model response nor orchestrator is required
for deterministic acceptance.

## Primary sources checked 6 September 2026

The [2026-07-28 transport contract](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports)
defines framing/metadata separately from protocol meaning. The
[tool contract](https://modelcontextprotocol.io/specification/2026-07-28/server/tools)
defines tool schemas/results and treats annotations as insufficient authority.
The [official Python package metadata](https://pypi.org/pypi/mcp/json) reported
2.1.1 with support for this revision; its wheel SHA-256 was
`1c6c31c5d6471c58db76af3af8af67f46d11d01f0a59077d0a308cbdb3d3e915`.
The application JSON dependency is the published
[nlohmann/json 3.12.0 release](https://github.com/nlohmann/json/releases/tag/v3.12.0).
These are dated external observations; the bounded local design is a project
decision, not a claim that the protocol requires this process architecture.
