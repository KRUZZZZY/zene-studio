#!/usr/bin/env python3
"""A real MCP stdio client, and the interpreter pick that goes with it.

WHY THIS IS NOT THE MCP SDK. The MCP stdio transport is newline-delimited
JSON-RPC over a child process's stdin/stdout, so the CLIENT half needs nothing but
the standard library. That matters here: the checks that drive the bridge run
under the interpreter CMake found (`PYTHON3_EXECUTABLE`), which is not
necessarily one with the `mcp` distribution installed, while the SERVER half —
the bridge's own `tools/mcp-zene-control/server.py` — does need it. Splitting it
this way keeps the client portable and leaves `server_python` as the single place
that decides what the server runs under, so "no interpreter with `mcp`" is a
documented SKIP (exit 77, ctest "Skipped") instead of an import error.

`tests/control-mcp-group-coverage.py` is the check that uses this: one command
per command group, driven through a real session against a real instance. The
class is deliberately transport-only — it knows nothing about Zene Studio — so a
future check can drive any prompt over the same session.
"""

from __future__ import annotations

import json
import os
import select
import subprocess
import sys
import threading
import time
from collections import deque
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BRIDGE_DIR = REPO / "tools" / "mcp-zene-control"

#: ctest's "did not run" convention; kept in step with SKIP_RETURN_CODE in
#: tests/CMakeLists.txt by this constant being the only source.
SKIP_CODE = 77


def server_python(explicit: str | None = None) -> str | None:
    """The first interpreter that can import `mcp`, or None (which is a SKIP).

    Order: the caller's choice, `ZENE_CONTROL_PYTHON`, the interpreter running the
    checking script, then this machine's Hermes venv (where the bridge is usually
    installed). None means no candidate had the distribution, so the server under
    test cannot be started at all.
    """
    candidates = [explicit, os.environ.get("ZENE_CONTROL_PYTHON"), sys.executable,
                  "/home/kruzzzzy/.hermes/hermes-agent/venv/bin/python3"]
    tried: list[str] = []
    for candidate in candidates:
        if not candidate or candidate in tried or not os.path.exists(candidate):
            continue
        tried.append(candidate)
        if subprocess.run([candidate, "-c", "import mcp"], capture_output=True).returncode == 0:
            return candidate
    print("SKIP: no interpreter with the `mcp` distribution (%s)" % ", ".join(tried or ["none"]))
    print("      the bridge's stdio server is the thing under test, so a check that cannot "
          "start it reports Skipped (exit %d) rather than Passed" % SKIP_CODE)
    return None


def protocol_version(python: str) -> str:
    """The MCP protocol the SERVER half speaks, asked of its own `mcp.types`.

    Asking is the point: a hardcoded revision on the client side turns a protocol
    bump on either side into a handshake failure, which is a false alarm about the
    thing under test rather than a fact about it.
    """
    out = subprocess.run([python, "-c", "import mcp.types as t;print(t.LATEST_PROTOCOL_VERSION)"],
                         capture_output=True, text=True).stdout.strip()
    return out or "2025-06-18"


class BridgeSession:
    """One MCP stdio session to `tools/mcp-zene-control/server.py`."""

    def __init__(self, python: str, socket_path: str, state_dir: str, protocol: str):
        self.protocol = protocol
        self.exchanges: list[dict] = []
        self.stderr: deque[str] = deque(maxlen=200)
        self._id = 0
        self._buffer = b""
        self.proc = subprocess.Popen(
            [python, str(BRIDGE_DIR / "server.py"), "--socket", socket_path,
             "--state-dir", state_dir],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            cwd=str(BRIDGE_DIR), env={**os.environ, "ZENE_CONTROL_STATE": state_dir})
        threading.Thread(target=self._drain_stderr, daemon=True).start()

    def _drain_stderr(self) -> None:
        """Keep the server's diagnostics for a failure message, without blocking it."""
        assert self.proc.stderr is not None
        for line in self.proc.stderr:
            self.stderr.append(line.decode("utf-8", "replace").rstrip())

    def _read_line(self, timeout: float) -> bytes:
        """One protocol line from stdout, or a bounded failure (never a wait)."""
        deadline = time.time() + timeout
        while b"\n" not in self._buffer:
            remaining = deadline - time.time()
            if remaining <= 0:
                raise TimeoutError("no reply from the bridge server within %.1fs" % timeout)
            assert self.proc.stdout is not None
            ready, _, _ = select.select([self.proc.stdout], [], [], remaining)
            if not ready:
                continue
            chunk = os.read(self.proc.stdout.fileno(), 1 << 16)
            if not chunk:
                raise RuntimeError("the bridge server exited (%s):\n%s"
                                   % (self.proc.poll(), "\n".join(self.stderr)))
            self._buffer += chunk
        line, self._buffer = self._buffer.split(b"\n", 1)
        return line

    def request(self, method: str, params: dict | None = None, timeout: float = 120.0) -> dict:
        """One JSON-RPC request; returns its result, raises on a protocol error."""
        self._id += 1
        request = {"jsonrpc": "2.0", "id": self._id, "method": method, "params": params or {}}
        assert self.proc.stdin is not None
        self.proc.stdin.write((json.dumps(request) + "\n").encode("utf-8"))
        self.proc.stdin.flush()
        while True:
            message = json.loads(self._read_line(timeout).decode("utf-8", "replace"))
            if message.get("id") != self._id:
                continue  # a notification or a server-initiated message
            self.exchanges.append({"method": method, "request": request, "reply": message})
            if "error" in message:
                raise RuntimeError("MCP %s failed: %s" % (method, json.dumps(message["error"])))
            return message.get("result") or {}

    def initialize(self) -> dict:
        """The MCP handshake, then the `initialized` notification the spec requires."""
        result = self.request("initialize", {
            "protocolVersion": self.protocol, "capabilities": {},
            "clientInfo": {"name": "zene-control-check", "version": "1.0"}})
        assert self.proc.stdin is not None
        self.proc.stdin.write((json.dumps({"jsonrpc": "2.0",
                                           "method": "notifications/initialized"}) + "\n")
                              .encode("utf-8"))
        self.proc.stdin.flush()
        return result

    def tools_list(self) -> list[dict]:
        return self.request("tools/list", {})["tools"]

    def call_tool(self, tool: str, arguments: dict | None = None, timeout: float = 180.0) -> dict:
        """tools/call, flattened for a caller that wants the payload and the summary."""
        started = time.monotonic()
        result = self.request("tools/call", {"name": tool, "arguments": arguments or {}}, timeout)
        elapsed = time.monotonic() - started
        first = next((block.get("text") or "" for block in (result.get("content") or [])
                      if block.get("type") == "text"), "")
        return {"tool": tool, "arguments": arguments or {}, "is_error": bool(result.get("isError")),
                "summary": first, "payload": result.get("structuredContent") or {},
                "elapsed_s": round(elapsed, 3)}

    def close(self) -> None:
        """Close stdin so the server exits, then reap it (kill only if it will not)."""
        try:
            if self.proc.stdin is not None:
                self.proc.stdin.close()
        except OSError:
            pass
        try:
            self.proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.proc.kill()
