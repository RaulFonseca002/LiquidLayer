#!/usr/bin/env python3

from __future__ import annotations

import http.client
import importlib.util
import json
from pathlib import Path
import re
import sys
import threading


ROOT = Path(__file__).resolve().parents[1]
SERVER_PATH = ROOT / "apps" / "visualizer" / "server.py"
SPEC = importlib.util.spec_from_file_location("solid_scope_integration_server", SERVER_PATH)
assert SPEC is not None and SPEC.loader is not None
server_module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(server_module)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_visualizer_integration.py <liquid_sim_trace>")

    trace_executable = Path(sys.argv[1]).resolve(strict=True)
    server = server_module.make_server(trace_executable, port=0)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        port = server.server_address[1]
        host = f"127.0.0.1:{port}"
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
        connection.request("GET", "/", headers={"Host": host})
        response = connection.getresponse()
        index = response.read()
        assert response.status == 200
        assert response.headers["Content-Security-Policy"]
        token_match = re.search(rb'<meta name="solid-scope-token" content="([A-Za-z0-9_-]+)">', index)
        assert token_match is not None
        token = token_match.group(1).decode("ascii")
        connection.close()

        scenario = json.dumps(
            {
                "initial_brightness": 10,
                "script": (
                    "local light = access.Light.officeLight\n"
                    "light.propose({ value = { brightness = 30 }, priority = 'low' })\n"
                    "light.propose({ value = { brightness = 70 }, priority = 'high', duration_ms = 5 })\n"
                ),
                "frame_times": [100, 105],
            },
            separators=(",", ":"),
        ).encode("utf-8")
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=20)
        connection.request(
            "POST",
            "/api/run",
            body=scenario,
            headers={
                "Host": host,
                "Origin": f"http://{host}",
                "Content-Type": "application/json",
                "X-Solid-Scope-Token": token,
            },
        )
        response = connection.getresponse()
        body = response.read().decode("ascii")
        assert response.status == 200
        assert response.headers.get_content_type() == "application/x-ndjson"
        events = [json.loads(line) for line in body.splitlines()]
        assert [event["seq"] for event in events] == list(range(len(events)))
        assert events[0]["event"] == "run_started"
        assert events[-1]["event"] == "run_completed"
        assert events[-1]["outcome"] == "success"
        assert events[-1]["faulted"] is False
        selected = [event["desired_brightness"] for event in events if event["event"] == "intent_selected"]
        actual = [event["actual_brightness"] for event in events if event["event"] == "component_snapshot"]
        assert selected == [70, 30]
        assert actual == [10, 10]
        connection.close()
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
