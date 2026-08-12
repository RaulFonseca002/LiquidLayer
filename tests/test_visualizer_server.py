#!/usr/bin/env python3

from __future__ import annotations

import http.client
import importlib.util
import json
from pathlib import Path
import re
import stat
import tempfile
import threading
import unittest


ROOT = Path(__file__).resolve().parents[1]
SERVER_PATH = ROOT / "apps" / "visualizer" / "server.py"
SPEC = importlib.util.spec_from_file_location("solid_scope_server", SERVER_PATH)
assert SPEC is not None and SPEC.loader is not None
server_module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(server_module)


FAKE_TRACE = """#!/usr/bin/env python3
import json
import pathlib
import sys
import time

args = sys.argv[1:]
script_path = pathlib.Path(args[args.index("--script") + 1])
source = script_path.read_text(encoding="utf-8")
if source == "bad sequence":
    print(json.dumps({"schema":"liquid.trace.v1","seq":2,"event":"run_started"}), flush=True)
    raise SystemExit(0)
print(json.dumps({"schema":"liquid.trace.v1","seq":0,"event":"run_started",
                  "script_path":str(script_path),
                  "script_mode":oct(script_path.stat().st_mode & 0o777)}), flush=True)
if source == "child failure":
    print("deliberate child failure", file=sys.stderr, flush=True)
    raise SystemExit(7)
if source == "post terminal":
    print(json.dumps({"schema":"liquid.trace.v1","seq":1,"event":"run_completed","outcome":"success"}), flush=True)
    print(json.dumps({"schema":"liquid.trace.v1","seq":2,"event":"frame_started"}), flush=True)
    raise SystemExit(0)
if source == "wrong terminal status":
    print(json.dumps({"schema":"liquid.trace.v1","seq":1,"event":"run_completed","outcome":"success"}), flush=True)
    raise SystemExit(7)
if source == "terminal then sleep":
    print(json.dumps({"schema":"liquid.trace.v1","seq":1,"event":"run_completed","outcome":"success"}), flush=True)
    time.sleep(5)
    raise SystemExit(0)
if source == "sleep":
    time.sleep(5)
print(json.dumps({"schema":"liquid.trace.v1","seq":1,"event":"run_completed","outcome":"success"}), flush=True)
"""


class VisualizerServerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="solid-scope-server-test-")
        root = Path(self.temporary.name)
        self.static = root / "static"
        self.static.mkdir()
        (self.static / "index.html").write_text(
            '<meta name="solid-scope-token" content="__SOLID_SCOPE_TOKEN__">',
            encoding="utf-8",
        )
        (self.static / "styles.css").write_text("body {}", encoding="utf-8")
        (self.static / "app.js").write_text("'use strict';", encoding="utf-8")
        self.executable = root / "fake_trace.py"
        self.executable.write_text(FAKE_TRACE, encoding="utf-8")
        self.executable.chmod(self.executable.stat().st_mode | stat.S_IXUSR)
        self.server = server_module.make_server(
            self.executable, port=0, static_directory=self.static
        )
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.port = self.server.server_address[1]
        self.host = f"127.0.0.1:{self.port}"

        status, headers, body = self.request("GET", "/")
        self.assertEqual(status, 200)
        match = re.search(rb'content="([A-Za-z0-9_-]+)"', body)
        self.assertIsNotNone(match)
        self.token = match.group(1).decode("ascii")
        self.assertNotIn(b"__SOLID_SCOPE_TOKEN__", body)
        self.assertEqual(headers["cache-control"], "no-store")

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        self.temporary.cleanup()

    def request(
        self,
        method: str,
        path: str,
        body: bytes | None = None,
        headers: dict[str, str] | None = None,
    ) -> tuple[int, dict[str, str], bytes]:
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=3)
        connection.request(method, path, body=body, headers=headers or {})
        response = connection.getresponse()
        result = (
            response.status,
            {key.lower(): value for key, value in response.getheaders()},
            response.read(),
        )
        connection.close()
        return result

    def valid_payload(self, script: str = "return true") -> dict[str, object]:
        return {
            "initial_brightness": 40,
            "script": script,
            "frame_times": [0, 10, 10, 25],
        }

    def post(
        self,
        payload: dict[str, object] | bytes,
        headers: dict[str, str] | None = None,
    ) -> tuple[int, dict[str, str], bytes]:
        body = payload if isinstance(payload, bytes) else json.dumps(payload).encode()
        request_headers = {
            "Content-Type": "application/json",
            "Origin": f"http://{self.host}",
            "X-Solid-Scope-Token": getattr(self, "token", "missing"),
        }
        if headers:
            request_headers.update(headers)
        return self.request("POST", "/api/run", body, request_headers)

    def test_valid_request_streams_canonical_trace_and_cleans_script(self) -> None:
        status, headers, body = self.post(self.valid_payload())
        self.assertEqual(status, 200)
        self.assertEqual(
            headers["content-type"], "application/x-ndjson; charset=utf-8"
        )
        events = [json.loads(line) for line in body.splitlines()]
        self.assertEqual([event["event"] for event in events], ["run_started", "run_completed"])
        self.assertEqual([event["seq"] for event in events], [0, 1])
        self.assertFalse(Path(events[0]["script_path"]).exists())
        self.assertEqual(events[0]["script_mode"], "0o600")
        self.assertIn("default-src 'none'", headers["content-security-policy"])
        self.assertEqual(headers["x-content-type-options"], "nosniff")
        self.assertEqual(headers["x-frame-options"], "DENY")
        self.assertEqual(headers["referrer-policy"], "no-referrer")

    def test_rejects_duplicate_and_unknown_fields(self) -> None:
        duplicate = (
            b'{"initial_brightness":1,"initial_brightness":2,'
            b'"script":"","frame_times":[0]}'
        )
        self.assertEqual(self.post(duplicate)[0], 400)
        unknown = self.valid_payload()
        unknown["surprise"] = True
        self.assertEqual(self.post(unknown)[0], 400)

    def test_rejects_origin_token_content_type_and_host(self) -> None:
        cases = (
            ({"Origin": "http://example.com"}, 403),
            ({"Host": f"localhost:{self.port}"}, 403),
            ({"X-Solid-Scope-Token": "wrong"}, 403),
            ({"Content-Type": "text/plain"}, 415),
            ({"Host": "example.com"}, 403),
        )
        for headers, expected in cases:
            with self.subTest(headers=headers):
                self.assertEqual(self.post(self.valid_payload(), headers)[0], expected)

    def test_rejects_transfer_encoding_and_oversized_body(self) -> None:
        status, _, _ = self.post(
            self.valid_payload(), {"Transfer-Encoding": "chunked"}
        )
        self.assertEqual(status, 400)
        oversized = b" " * (server_module.MAX_BODY_BYTES + 1)
        self.assertEqual(self.post(oversized)[0], 413)

    def test_rejects_script_and_frame_bounds(self) -> None:
        cases = []
        for script in ("\x00", "x" * (server_module.MAX_SCRIPT_BYTES + 1)):
            payload = self.valid_payload(script)
            cases.append(payload)
        for frame_times in (
            [],
            [2, 1],
            [True],
            [server_module.MAX_INT64 + 1],
            list(range(server_module.MAX_FRAME_COUNT + 1)),
        ):
            payload = self.valid_payload()
            payload["frame_times"] = frame_times
            cases.append(payload)
        payload = self.valid_payload()
        payload["initial_brightness"] = True
        cases.append(payload)
        for payload in cases:
            with self.subTest(payload=list(payload) if len(str(payload)) > 100 else payload):
                self.assertIn(self.post(payload)[0], (400, 413))

    def test_serves_only_the_static_allowlist(self) -> None:
        for path, content_type in (
            ("/index.html", "text/html; charset=utf-8"),
            ("/?selftest=1", "text/html; charset=utf-8"),
            ("/styles.css", "text/css; charset=utf-8"),
            ("/app.js", "text/javascript; charset=utf-8"),
        ):
            with self.subTest(path=path):
                status, headers, _ = self.request("GET", path)
                self.assertEqual(status, 200)
                self.assertEqual(headers["content-type"], content_type)
        for path in ("/server.py", "/../server.py", "/styles.css?cache=1", "/api/run"):
            with self.subTest(path=path):
                self.assertEqual(self.request("GET", path)[0], 404)

    def test_child_without_terminal_event_gets_transport_failure(self) -> None:
        status, _, body = self.post(self.valid_payload("child failure"))
        self.assertEqual(status, 200)
        events = [json.loads(line) for line in body.splitlines()]
        self.assertEqual(events[-1]["event"], "transport_failed")
        self.assertEqual(events[-1]["seq"], 1)
        self.assertIn("status 7", events[-1]["diagnostic"])
        self.assertIn("deliberate child failure", events[-1]["diagnostic"])

    def test_child_timeout_is_terminated_and_script_is_cleaned(self) -> None:
        original_timeout = server_module.CHILD_TIMEOUT_SECONDS
        server_module.CHILD_TIMEOUT_SECONDS = 0.15
        try:
            status, _, body = self.post(self.valid_payload("sleep"))
        finally:
            server_module.CHILD_TIMEOUT_SECONDS = original_timeout
        self.assertEqual(status, 200)
        events = [json.loads(line) for line in body.splitlines()]
        self.assertEqual(events[-1]["event"], "transport_failed")
        self.assertIn("15-second limit", events[-1]["diagnostic"])
        self.assertFalse(Path(events[0]["script_path"]).exists())

    def test_trace_sequence_must_start_at_zero(self) -> None:
        status, _, body = self.post(self.valid_payload("bad sequence"))
        self.assertEqual(status, 200)
        events = [json.loads(line) for line in body.splitlines()]
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["event"], "transport_failed")
        self.assertEqual(events[0]["seq"], 0)

    def test_rejects_events_after_terminal_and_aggregate_event_exhaustion(self) -> None:
        status, _, body = self.post(self.valid_payload("post terminal"))
        self.assertEqual(status, 200)
        events = [json.loads(line) for line in body.splitlines()]
        self.assertEqual(events[-1]["event"], "transport_failed")
        self.assertIn("after its terminal", events[-1]["diagnostic"])

        original_limit = server_module.MAX_TRACE_EVENTS
        server_module.MAX_TRACE_EVENTS = 1
        try:
            status, _, body = self.post(self.valid_payload())
        finally:
            server_module.MAX_TRACE_EVENTS = original_limit
        self.assertEqual(status, 200)
        events = [json.loads(line) for line in body.splitlines()]
        self.assertEqual(events[-1]["event"], "transport_failed")
        self.assertIn("event count limit", events[-1]["diagnostic"])

    def test_withholds_an_untrustworthy_terminal_event(self) -> None:
        status, _, body = self.post(self.valid_payload("wrong terminal status"))
        self.assertEqual(status, 200)
        events = [json.loads(line) for line in body.splitlines()]
        self.assertEqual([event["event"] for event in events], ["run_started", "transport_failed"])
        self.assertIn("conflicts", events[-1]["diagnostic"])

        original_timeout = server_module.CHILD_TIMEOUT_SECONDS
        server_module.CHILD_TIMEOUT_SECONDS = 0.15
        try:
            status, _, body = self.post(self.valid_payload("terminal then sleep"))
        finally:
            server_module.CHILD_TIMEOUT_SECONDS = original_timeout
        self.assertEqual(status, 200)
        events = [json.loads(line) for line in body.splitlines()]
        self.assertEqual([event["event"] for event in events], ["run_started", "transport_failed"])
        self.assertIn("15-second limit", events[-1]["diagnostic"])

    def test_rejects_a_run_when_capacity_is_exhausted(self) -> None:
        self.assertTrue(self.server.run_slots.acquire(blocking=False))
        try:
            status, _, body = self.post(self.valid_payload())
        finally:
            self.server.run_slots.release()
        self.assertEqual(status, 429)
        self.assertIn(b"already active", body)

    def test_make_server_refuses_non_loopback_bind(self) -> None:
        with self.assertRaises(ValueError):
            server_module.make_server(
                self.executable, host="0.0.0.0", port=0, static_directory=self.static
            )


if __name__ == "__main__":
    unittest.main()
