#!/usr/bin/env python3
"""Loopback-only HTTP bridge for the Solid Scope visualizer."""

from __future__ import annotations

import argparse
import hmac
import json
import os
from pathlib import Path
import secrets
import selectors
import signal
import socket
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from urllib.parse import urlsplit


MAX_BODY_BYTES = 80 * 1024
MAX_SCRIPT_BYTES = 65_536
MAX_FRAME_COUNT = 1_000
MAX_TRACE_LINE_BYTES = 65_536
MAX_TRACE_BYTES = 8 * 1024 * 1024
MAX_TRACE_EVENTS = 8_192
MAX_STDERR_BYTES = 4_096
MAX_CONCURRENT_RUNS = 1
CHILD_TIMEOUT_SECONDS = 15.0
MAX_INT64 = (1 << 63) - 1
TRACE_SCHEMA = "liquid.trace.v1"
TERMINAL_EVENTS = frozenset(("run_completed", "run_failed"))
STATIC_FILES = {
    "/index.html": ("index.html", "text/html; charset=utf-8"),
    "/styles.css": ("styles.css", "text/css; charset=utf-8"),
    "/app.js": ("app.js", "text/javascript; charset=utf-8"),
}
TOKEN_PLACEHOLDER = b"__SOLID_SCOPE_TOKEN__"


class RequestProblem(Exception):
    def __init__(self, status: int, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.message = message


def _reject_constant(value: str) -> None:
    raise ValueError(f"invalid JSON constant: {value}")


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON field: {key}")
        result[key] = value
    return result


def _decode_json(data: bytes) -> Any:
    try:
        text = data.decode("utf-8", errors="strict")
        return json.loads(
            text,
            object_pairs_hook=_unique_object,
            parse_constant=_reject_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, RecursionError, ValueError) as error:
        raise RequestProblem(400, f"invalid JSON: {error}") from error


def validate_scenario(data: bytes) -> tuple[int, str, list[int]]:
    payload = _decode_json(data)
    if not isinstance(payload, dict):
        raise RequestProblem(400, "request body must be a JSON object")

    expected = {"initial_brightness", "script", "frame_times"}
    fields = set(payload)
    unknown = fields - expected
    missing = expected - fields
    if unknown:
        raise RequestProblem(400, f"unknown field: {sorted(unknown)[0]}")
    if missing:
        raise RequestProblem(400, f"missing field: {sorted(missing)[0]}")

    brightness = payload["initial_brightness"]
    if type(brightness) is not int or not 0 <= brightness <= 100:
        raise RequestProblem(400, "initial_brightness must be an integer from 0 to 100")

    script = payload["script"]
    if not isinstance(script, str):
        raise RequestProblem(400, "script must be a string")
    if "\x00" in script:
        raise RequestProblem(400, "script must not contain NUL bytes")
    try:
        script_bytes = script.encode("utf-8", errors="strict")
    except UnicodeEncodeError as error:
        raise RequestProblem(400, "script must contain valid Unicode") from error
    if len(script_bytes) > MAX_SCRIPT_BYTES:
        raise RequestProblem(413, "script exceeds the 65536-byte limit")

    frame_times = payload["frame_times"]
    if not isinstance(frame_times, list):
        raise RequestProblem(400, "frame_times must be an array")
    if not 1 <= len(frame_times) <= MAX_FRAME_COUNT:
        raise RequestProblem(400, "frame_times must contain between 1 and 1000 values")
    previous = -1
    for value in frame_times:
        if type(value) is not int or not 0 <= value <= MAX_INT64:
            raise RequestProblem(400, "frame times must be integers from 0 to INT64_MAX")
        if value < previous:
            raise RequestProblem(400, "frame times must be nondecreasing")
        previous = value

    return brightness, script, frame_times


def _parse_authority(authority: str) -> tuple[str, int | None] | None:
    if not authority or any(character.isspace() for character in authority):
        return None
    try:
        parsed = urlsplit("//" + authority)
        if parsed.username is not None or parsed.password is not None:
            return None
        hostname = parsed.hostname
        port = parsed.port
    except ValueError:
        return None
    if hostname not in {"127.0.0.1", "::1", "localhost"}:
        return None
    return hostname, port


class SolidScopeServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(
        self,
        address: tuple[str, int],
        trace_executable: Path,
        static_directory: Path,
    ) -> None:
        self.trace_executable = trace_executable
        self.static_directory = static_directory
        self.csrf_token = secrets.token_urlsafe(32)
        self._children: set[subprocess.Popen[bytes]] = set()
        self._children_lock = threading.Lock()
        self.run_slots = threading.BoundedSemaphore(MAX_CONCURRENT_RUNS)
        if ":" in address[0]:
            self.address_family = socket.AF_INET6
        super().__init__(address, SolidScopeHandler)

    def register_child(self, child: subprocess.Popen[bytes]) -> None:
        with self._children_lock:
            self._children.add(child)

    def unregister_child(self, child: subprocess.Popen[bytes]) -> None:
        with self._children_lock:
            self._children.discard(child)

    def terminate_children(self) -> None:
        with self._children_lock:
            children = tuple(self._children)
        for child in children:
            _terminate_and_reap(child)

    def shutdown(self) -> None:
        self.terminate_children()
        super().shutdown()

    def server_close(self) -> None:
        self.terminate_children()
        super().server_close()


class SolidScopeHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    server: SolidScopeServer

    def log_message(self, format_string: str, *args: Any) -> None:
        # Keep the development tool quiet; callers can wrap the server if they
        # need request logging.
        del format_string, args

    def _security_headers(self) -> None:
        self.send_header(
            "Content-Security-Policy",
            "default-src 'none'; script-src 'self'; style-src 'self'; "
            "connect-src 'self'; img-src 'self'; base-uri 'none'; "
            "frame-ancestors 'none'; form-action 'none'",
        )
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("Referrer-Policy", "no-referrer")

    def _send_bytes(self, status: int, content_type: str, body: bytes) -> None:
        self.send_response(status)
        self._security_headers()
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _send_problem(self, problem: RequestProblem) -> None:
        body = json.dumps(
            {"error": problem.message}, separators=(",", ":"), ensure_ascii=True
        ).encode("ascii")
        self._send_bytes(problem.status, "application/json; charset=utf-8", body)

    def send_error(
        self,
        code: int,
        message: str | None = None,
        explain: str | None = None,
    ) -> None:
        del explain
        self._send_problem(RequestProblem(code, message or "request failed"))

    def _host_authority(self) -> tuple[str, int] | None:
        values = self.headers.get_all("Host", failobj=[])
        if len(values) != 1:
            return None
        authority = _parse_authority(values[0])
        if authority is None:
            return None
        hostname, port = authority
        expected_port = self.server.server_address[1]
        if port == expected_port or (port is None and expected_port == 80):
            return hostname, expected_port
        return None

    def _require_host(self) -> None:
        if self._host_authority() is None:
            raise RequestProblem(403, "invalid Host header")

    def _require_same_origin(self) -> None:
        values = self.headers.get_all("Origin", failobj=[])
        if len(values) != 1:
            raise RequestProblem(403, "a same-origin Origin header is required")
        parsed = urlsplit(values[0])
        if (
            parsed.scheme != "http"
            or parsed.path not in ("", "/")
            or parsed.query
            or parsed.fragment
        ):
            raise RequestProblem(403, "invalid Origin header")
        authority = _parse_authority(parsed.netloc)
        if authority is None:
            raise RequestProblem(403, "invalid Origin header")
        hostname, port = authority
        expected_port = self.server.server_address[1]
        host_authority = self._host_authority()
        if (
            port != expected_port
            or host_authority is None
            or hostname != host_authority[0]
        ):
            raise RequestProblem(403, "Origin does not match this server")

    def do_GET(self) -> None:
        try:
            self._require_host()
            parsed = urlsplit(self.path)
            selftest_query = (
                parsed.path in {"/", "/index.html"} and parsed.query == "selftest=1"
            )
            if (parsed.query and not selftest_query) or parsed.fragment:
                raise RequestProblem(404, "resource not found")
            path = "/index.html" if parsed.path == "/" else parsed.path
            static = STATIC_FILES.get(path)
            if static is None:
                raise RequestProblem(404, "resource not found")
            name, content_type = static
            try:
                body = (self.server.static_directory / name).read_bytes()
            except OSError as error:
                raise RequestProblem(500, "static resource unavailable") from error
            if name == "index.html":
                if TOKEN_PLACEHOLDER not in body:
                    raise RequestProblem(500, "index is missing its token placeholder")
                body = body.replace(
                    TOKEN_PLACEHOLDER, self.server.csrf_token.encode("ascii")
                )
            self._send_bytes(200, content_type, body)
        except RequestProblem as problem:
            self._send_problem(problem)

    def do_HEAD(self) -> None:
        self.do_GET()

    def do_POST(self) -> None:
        try:
            self._require_host()
            if self.path != "/api/run":
                raise RequestProblem(404, "resource not found")
            self._require_same_origin()
            tokens = self.headers.get_all("X-Solid-Scope-Token", failobj=[])
            if len(tokens) != 1 or not hmac.compare_digest(
                tokens[0], self.server.csrf_token
            ):
                raise RequestProblem(403, "invalid request token")
            if self.headers.get_all("Transfer-Encoding", failobj=[]):
                raise RequestProblem(400, "Transfer-Encoding is not accepted")
            content_types = self.headers.get_all("Content-Type", failobj=[])
            media_type = (
                content_types[0].split(";", 1)[0].strip().lower()
                if len(content_types) == 1
                else ""
            )
            if media_type != "application/json":
                raise RequestProblem(415, "Content-Type must be application/json")
            lengths = self.headers.get_all("Content-Length", failobj=[])
            if len(lengths) != 1:
                raise RequestProblem(411, "one Content-Length header is required")
            if not lengths[0].isascii() or not lengths[0].isdigit():
                raise RequestProblem(400, "invalid Content-Length")
            content_length = int(lengths[0], 10)
            if content_length > MAX_BODY_BYTES:
                raise RequestProblem(413, "request body exceeds the 81920-byte limit")
            self.connection.settimeout(5.0)
            try:
                body = self.rfile.read(content_length)
            except TimeoutError as error:
                raise RequestProblem(408, "request body timed out") from error
            if len(body) != content_length:
                raise RequestProblem(400, "incomplete request body")
            brightness, script, frame_times = validate_scenario(body)
        except RequestProblem as problem:
            self._send_problem(problem)
            return

        if not self.server.run_slots.acquire(blocking=False):
            self._send_problem(RequestProblem(429, "another trace run is already active"))
            return
        try:
            try:
                self._run_trace(brightness, script, frame_times)
            except (BrokenPipeError, ConnectionResetError):
                # _run_trace owns and cleans its child before this propagates.
                return
        finally:
            self.server.run_slots.release()

    def _run_trace(
        self, brightness: int, script: str, frame_times: list[int]
    ) -> None:
        with tempfile.TemporaryDirectory(prefix="solid-scope-") as temporary:
            script_path = Path(temporary) / "scenario.lua"
            descriptor = os.open(
                script_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600
            )
            with os.fdopen(descriptor, "wb") as script_file:
                script_file.write(script.encode("utf-8"))

            arguments = [
                str(self.server.trace_executable),
                "--initial-brightness",
                str(brightness),
                "--script",
                str(script_path),
            ]
            for frame_time in frame_times:
                arguments.extend(("--frame-time", str(frame_time)))

            self.send_response(200)
            self._security_headers()
            self.send_header("Content-Type", "application/x-ndjson; charset=utf-8")
            self.end_headers()
            self.wfile.flush()

            child: subprocess.Popen[bytes] | None = None
            try:
                try:
                    child = subprocess.Popen(
                        arguments,
                        stdin=subprocess.DEVNULL,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        shell=False,
                        start_new_session=True,
                    )
                except OSError as error:
                    self._write_transport_failure(0, f"could not start trace process: {error}")
                    return
                self.server.register_child(child)
                self._stream_child(child)
            finally:
                if child is not None:
                    _terminate_and_reap(child)
                    self.server.unregister_child(child)
                    if child.stdout is not None:
                        child.stdout.close()
                    if child.stderr is not None:
                        child.stderr.close()

    def _write_event(self, event: dict[str, Any]) -> None:
        encoded = json.dumps(
            event, separators=(",", ":"), ensure_ascii=True, allow_nan=False
        ).encode("ascii") + b"\n"
        self.wfile.write(encoded)
        self.wfile.flush()

    def _write_transport_failure(self, sequence: int, diagnostic: str) -> None:
        self._write_event(
            {
                "schema": TRACE_SCHEMA,
                "seq": sequence,
                "event": "transport_failed",
                "diagnostic": diagnostic[:MAX_STDERR_BYTES],
            }
        )

    def _stream_child(self, child: subprocess.Popen[bytes]) -> None:
        assert child.stdout is not None
        assert child.stderr is not None
        selector = selectors.DefaultSelector()
        for stream, name in ((child.stdout, "stdout"), (child.stderr, "stderr")):
            os.set_blocking(stream.fileno(), False)
            selector.register(stream, selectors.EVENT_READ, name)
        selector.register(self.connection, selectors.EVENT_READ, "client")
        open_streams = 2

        stdout_buffer = bytearray()
        stderr_buffer = bytearray()
        last_sequence = -1
        saw_terminal = False
        terminal_event: dict[str, Any] | None = None
        trace_bytes = 0
        trace_events = 0
        deadline = time.monotonic() + CHILD_TIMEOUT_SECONDS
        failure: str | None = None

        try:
            while open_streams:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    failure = "trace process exceeded the 15-second limit"
                    break
                for key, _ in selector.select(min(remaining, 0.1)):
                    if key.data == "client":
                        try:
                            pending = self.connection.recv(1, socket.MSG_PEEK)
                        except BlockingIOError:
                            continue
                        if not pending:
                            raise ConnectionResetError("client disconnected")
                        # HTTP/1.0 closes this response, so pipelined bytes are
                        # not meaningful to the current request.
                        selector.unregister(self.connection)
                        continue
                    try:
                        chunk = os.read(key.fileobj.fileno(), 8192)
                    except BlockingIOError:
                        continue
                    if not chunk:
                        selector.unregister(key.fileobj)
                        open_streams -= 1
                        continue
                    if key.data == "stderr":
                        available = MAX_STDERR_BYTES - len(stderr_buffer)
                        stderr_buffer.extend(chunk[: max(available, 0)])
                        continue
                    stdout_buffer.extend(chunk)
                    while b"\n" in stdout_buffer:
                        line, _, remainder = stdout_buffer.partition(b"\n")
                        stdout_buffer = bytearray(remainder)
                        if saw_terminal:
                            failure = "trace process emitted data after its terminal event"
                            break
                        trace_bytes += len(line) + 1
                        trace_events += 1
                        if trace_bytes > MAX_TRACE_BYTES:
                            failure = "trace process exceeded the aggregate output limit"
                            break
                        if trace_events > MAX_TRACE_EVENTS:
                            failure = "trace process exceeded the event count limit"
                            break
                        try:
                            last_sequence, terminal, event = self._validate_trace_line(
                                bytes(line), last_sequence
                            )
                        except RequestProblem as problem:
                            failure = problem.message
                            break
                        if terminal:
                            saw_terminal = True
                            terminal_event = event
                        else:
                            self._write_event(event)
                    if failure is not None:
                        break
                    if len(stdout_buffer) > MAX_TRACE_LINE_BYTES:
                        failure = "trace process emitted an oversized line"
                        break
                if failure is not None:
                    break

            if failure is None and stdout_buffer:
                if saw_terminal:
                    failure = "trace process emitted data after its terminal event"
                else:
                    trace_bytes += len(stdout_buffer)
                    trace_events += 1
                    if trace_bytes > MAX_TRACE_BYTES:
                        failure = "trace process exceeded the aggregate output limit"
                    elif trace_events > MAX_TRACE_EVENTS:
                        failure = "trace process exceeded the event count limit"
                    else:
                        last_sequence, terminal, event = self._validate_trace_line(
                            bytes(stdout_buffer), last_sequence
                        )
                        if terminal:
                            saw_terminal = True
                            terminal_event = event
                        else:
                            self._write_event(event)

            if failure is not None:
                _terminate_and_reap(child)
                self._write_transport_failure(last_sequence + 1, failure)
                return

            try:
                exit_code = child.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                _terminate_and_reap(child)
                self._write_transport_failure(
                    last_sequence + 1, "trace process did not exit after closing output"
                )
                return
            if not saw_terminal:
                diagnostic = stderr_buffer.decode("utf-8", errors="replace").strip()
                detail = f"trace process exited with status {exit_code}"
                if diagnostic:
                    detail += f": {diagnostic}"
                self._write_transport_failure(last_sequence + 1, detail)
                return

            assert terminal_event is not None
            terminal_name = terminal_event["event"]
            terminal_outcome = terminal_event.get("outcome")
            if terminal_name == "run_completed" and terminal_outcome not in {
                "success",
                "script_error",
            }:
                self._write_transport_failure(
                    last_sequence + 1, "terminal event has an invalid completed outcome"
                )
                return
            if terminal_name == "run_failed" and terminal_outcome != "host_error":
                self._write_transport_failure(
                    last_sequence + 1, "terminal event has an invalid failed outcome"
                )
                return

            expected_exit = 4
            if terminal_name == "run_completed":
                expected_exit = 0 if terminal_outcome == "success" else 3
            if exit_code != expected_exit:
                self._write_transport_failure(
                    last_sequence + 1,
                    f"terminal event conflicts with trace process status {exit_code}",
                )
                return
            self._write_event(terminal_event)
        finally:
            selector.close()

    def _validate_trace_line(
        self, line: bytes, last_sequence: int
    ) -> tuple[int, bool, dict[str, Any]]:
        if not line:
            raise RequestProblem(502, "trace process emitted an empty line")
        if len(line) > MAX_TRACE_LINE_BYTES:
            raise RequestProblem(502, "trace process emitted an oversized line")
        try:
            event = _decode_json(line)
        except RequestProblem as problem:
            raise RequestProblem(
                502, f"trace process emitted invalid JSON: {problem.message}"
            ) from problem
        if not isinstance(event, dict):
            raise RequestProblem(502, "trace event must be a JSON object")
        if event.get("schema") != TRACE_SCHEMA:
            raise RequestProblem(502, "trace event has an unsupported schema")
        sequence = event.get("seq")
        if (
            type(sequence) is not int
            or sequence < 0
            or (last_sequence < 0 and sequence != 0)
            or sequence <= last_sequence
        ):
            raise RequestProblem(502, "trace sequence must be strictly increasing")
        name = event.get("event")
        if not isinstance(name, str) or not name:
            raise RequestProblem(502, "trace event name must be a non-empty string")
        return sequence, name in TERMINAL_EVENTS, event


def _terminate_and_reap(child: subprocess.Popen[bytes]) -> None:
    if child.poll() is not None:
        try:
            child.wait(timeout=0)
        except subprocess.TimeoutExpired:
            pass
        return
    try:
        os.killpg(child.pid, signal.SIGTERM)
    except (ProcessLookupError, PermissionError):
        child.terminate()
    try:
        child.wait(timeout=0.5)
        return
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(child.pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        child.kill()
    child.wait(timeout=1.0)


def make_server(
    trace_executable: str | os.PathLike[str],
    host: str = "127.0.0.1",
    port: int = 8765,
    static_directory: str | os.PathLike[str] | None = None,
) -> SolidScopeServer:
    if host not in {"127.0.0.1", "::1"}:
        raise ValueError("Solid Scope may bind only to a loopback address")
    executable = Path(trace_executable).expanduser().resolve(strict=True)
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise ValueError("trace executable must be an executable file")
    assets = (
        Path(static_directory).resolve(strict=True)
        if static_directory is not None
        else Path(__file__).resolve().parent
    )
    if not assets.is_dir():
        raise ValueError("static directory must be a directory")
    return SolidScopeServer((host, port), executable, assets)


def parse_arguments(arguments: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Serve the Solid Scope visualizer")
    parser.add_argument("--trace-executable", required=True)
    parser.add_argument("--host", default="127.0.0.1", choices=("127.0.0.1", "::1"))
    parser.add_argument("--port", default=8765, type=int)
    result = parser.parse_args(arguments)
    if not 0 <= result.port <= 65_535:
        parser.error("--port must be between 0 and 65535")
    return result


def main(arguments: list[str] | None = None) -> int:
    options = parse_arguments(arguments)
    server = make_server(
        options.trace_executable, host=options.host, port=options.port
    )
    address = server.server_address
    display_host = f"[{address[0]}]" if ":" in address[0] else address[0]
    print(f"Solid Scope: http://{display_host}:{address[1]}/", flush=True)
    try:
        server.serve_forever(poll_interval=0.1)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
