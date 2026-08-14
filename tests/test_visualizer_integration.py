#!/usr/bin/env python3

from __future__ import annotations

import http.client
from html.parser import HTMLParser
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


class DefaultScriptParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.in_script_source = False
        self.fragments: list[str] = []

    def handle_starttag(
        self, tag: str, attributes: list[tuple[str, str | None]]
    ) -> None:
        if tag == "textarea" and dict(attributes).get("id") == "script-source":
            self.in_script_source = True

    def handle_endtag(self, tag: str) -> None:
        if tag == "textarea" and self.in_script_source:
            self.in_script_source = False

    def handle_data(self, data: str) -> None:
        if self.in_script_source:
            self.fragments.append(data)

    def source(self) -> str:
        return "".join(self.fragments)


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
        parser = DefaultScriptParser()
        parser.feed(index.decode("utf-8"))
        default_script = parser.source()
        assert default_script
        connection.close()

        def run_scenario(payload: dict) -> list[dict]:
            scenario = json.dumps(payload, separators=(",", ":")).encode("utf-8")
            run_connection = http.client.HTTPConnection("127.0.0.1", port, timeout=20)
            run_connection.request(
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
            run_response = run_connection.getresponse()
            run_body = run_response.read().decode("ascii")
            assert run_response.status == 200
            assert run_response.headers.get_content_type() == "application/x-ndjson"
            run_events = [json.loads(line) for line in run_body.splitlines()]
            assert [event["seq"] for event in run_events] == list(range(len(run_events)))
            assert run_events[0]["event"] == "run_started"
            run_connection.close()
            return run_events

        events = run_scenario(
            {
                "initial_brightness": 10,
                "script": default_script,
                "frame_times": [100, 105, 110],
            }
        )
        assert events[-1]["event"] == "run_completed"
        assert events[-1]["outcome"] == "success"
        assert events[-1]["faulted"] is False
        selected = [event["desired_brightness"] for event in events if event["event"] == "desire_selected"]
        actual = [event["actual_brightness"] for event in events if event["event"] == "component_snapshot"]
        device = [event["device_brightness"] for event in events if event["event"] == "component_snapshot"]
        # The full loop is truthful: 70 is confirmed only after the device
        # report lands, and 30 only after the fallback command is applied.
        assert selected == [70, 30, 30]
        assert actual == [10, 70, 30]
        assert device == [70, 30, 30]
        assert events[-1]["final_brightness"] == 30
        assert events[-1]["device_brightness"] == 30

        # A rejected adapter must never fabricate confirmed state.
        rejected = run_scenario(
            {
                "initial_brightness": 10,
                "script": default_script,
                "frame_times": [100, 105, 110],
                "adapter_outcome": "rejected",
            }
        )
        assert rejected[-1]["event"] == "run_completed"
        assert rejected[-1]["outcome"] == "success"
        rejected_actual = [event["actual_brightness"] for event in rejected if event["event"] == "component_snapshot"]
        assert rejected_actual == [10, 10, 10]
        assert rejected[-1]["final_brightness"] == 10
        assert not [event for event in rejected if event["event"] == "observed_changed"]
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
