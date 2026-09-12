"""Test harness: a real headless instance, a real socket, a stub DAW.

The instance recipe is the one AGENT-TOOLING.md §4 prescribes (and the one the
in-app lane's own integration test uses) — copy it, don't re-derive it:

  * ``QT_QPA_PLATFORM=offscreen``;
  * a minimal config passed with ``--config`` whose ``audiodev`` is exactly
    ``Dummy (no sound output)`` (``AudioDummy::name()``); with no sound card the
    app otherwise puts up a modal "Audio device setup failed" dialog before the
    event loop starts and the engine never becomes ready;
  * ``HOME``/``XDG_CONFIG_HOME``/``XDG_DATA_HOME`` pointed at a temp dir so a run
    cannot disturb the user's real settings;
  * all temp files under /tmp;
  * then poll ``control.ping`` until ``engine_ready`` is true.

``StubDaw`` is a deliberately broken instance used to prove the error taxonomy
without racing a real process: it can accept and never reply (timeout), accept
and close mid-request (disconnect), or answer ping with a foreign protocol
version or ``engine_ready: false`` forever.
"""
from __future__ import annotations

import json
import os
import shutil
import signal
import socket
import subprocess
import tempfile
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
BRIDGE_DIR = HERE.parent
REPO_DIR = BRIDGE_DIR.parent

PYTHON = os.environ.get("ZENE_CONTROL_PYTHON", "/home/kruzzzzy/.hermes/hermes-agent/venv/bin/python3")
#: The built binary of the lane that implements the control surface.
DEFAULT_BINARY = REPO_DIR / "zene-pa-agentctl" / "build" / "lmms"
FIXTURE = HERE / "data" / "agent-control-fixture.mmp"

CONNECT_TIMEOUT = 30.0
ENGINE_TIMEOUT = 120.0
RESPONSE_TIMEOUT = 60.0


def binary_path() -> str:
    raw = os.environ.get("ZENE_CONTROL_BINARY")
    path = Path(raw) if raw else DEFAULT_BINARY
    if not path.exists():
        raise RuntimeError(
            f"no Zene Studio binary at {path}; set ZENE_CONTROL_BINARY to a build that "
            f"accepts --control-socket (the zene-pa-agentctl lane's build/lmms)"
        )
    return str(path)


def bridge_argv(socket_path: str, **extra: object) -> list[str]:
    """`python3 server.py --socket ...` plus optional CLI overrides."""
    argv = [str(BRIDGE_DIR / "server.py"), "--socket", socket_path]
    for key, value in extra.items():
        if value is True:
            argv.append(f"--{key.replace('_', '-')}")
        elif value is not None and value is not False:
            argv += [f"--{key.replace('_', '-')}", str(value)]
    return argv


# ---------------------------------------------------------------------------
# A raw client for the DAW: used to read control.commands_list independently of
# the bridge, so the set-equality assertion compares two real sources.
# ---------------------------------------------------------------------------

class RawDawClient:
    def __init__(self, path: str, timeout: float = RESPONSE_TIMEOUT):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect(path)
        self.buffer = b""
        self._id = 0

    def call(self, cmd: str, args: dict | None = None, proto: int = 1) -> dict:
        self._id += 1
        request_id = self._id
        raw = json.dumps({"id": request_id, "cmd": cmd, "args": args or {}, "proto": proto})
        self.sock.sendall(raw.encode("utf-8") + b"\n")
        while b"\n" not in self.buffer:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("the instance closed the connection")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return json.loads(line)

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------------------
# A real headless instance
# ---------------------------------------------------------------------------

class Instance:
    """One `lmms --control-socket` process under the headless recipe."""

    def __init__(self, binary: str | None = None, tmp_root: str | None = None):
        self.binary = binary or binary_path()
        self.tmp = tempfile.mkdtemp(prefix="zene-control-test-",
                                    dir=tmp_root or "/tmp")
        self.socket_path = os.path.join(self.tmp, "zene.sock")
        self.workspace = os.path.join(self.tmp, "workspace")
        os.makedirs(self.workspace)
        self.config_path = os.path.join(self.tmp, "lmmsrc.xml")
        with open(self.config_path, "w", encoding="utf-8") as handle:
            handle.write(
                '<?xml version="1.0"?>\n'
                '<!DOCTYPE lmms-config-file>\n'
                '<lmmsconfig version="0.2.0-alpha" configversion="3">\n'
                '  <app configured="1"/>\n'
                '  <audioengine audiodev="Dummy (no sound output)"/>\n'
                '  <paths workingdir="%s"/>\n'
                '</lmmsconfig>\n' % self.workspace
            )
        self.env = dict(os.environ)
        self.env["QT_QPA_PLATFORM"] = "offscreen"
        self.env["HOME"] = self.tmp
        self.env["XDG_CONFIG_HOME"] = os.path.join(self.tmp, "config")
        self.env["XDG_DATA_HOME"] = os.path.join(self.tmp, "data")
        os.makedirs(self.env["XDG_CONFIG_HOME"], exist_ok=True)
        os.makedirs(self.env["XDG_DATA_HOME"], exist_ok=True)
        self.log_path = os.path.join(self.tmp, "app.log")
        self._log = open(self.log_path, "wb")
        self.process = subprocess.Popen(
            [self.binary, "--config", self.config_path, "--control-socket", self.socket_path],
            stdout=self._log, stderr=subprocess.STDOUT, env=self.env, cwd=self.tmp,
        )
        self.started_at = time.monotonic()

    # -- waiting ---------------------------------------------------------

    def wait_socket(self, timeout: float = CONNECT_TIMEOUT) -> float:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"the instance exited ({self.process.returncode}) before the "
                                   f"socket was usable:\n{self.log_tail()}")
            if os.path.exists(self.socket_path):
                try:
                    probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    probe.settimeout(1.0)
                    probe.connect(self.socket_path)
                    probe.close()
                    return time.monotonic() - self.started_at
                except OSError:
                    pass
            time.sleep(0.1)
        raise RuntimeError(f"the control socket {self.socket_path} never became connectable:\n"
                           f"{self.log_tail()}")

    def wait_engine(self, timeout: float = ENGINE_TIMEOUT) -> float:
        """Poll control.ping until engine_ready, the way the readiness rule says."""
        self.wait_socket(timeout=min(timeout, CONNECT_TIMEOUT))
        client = RawDawClient(self.socket_path, timeout=RESPONSE_TIMEOUT)
        deadline = time.time() + timeout
        last: dict = {}
        try:
            while time.time() < deadline:
                last = client.call("control.ping")
                if last.get("ok") and (last.get("result") or {}).get("engine_ready"):
                    return time.monotonic() - self.started_at
                if self.process.poll() is not None:
                    raise RuntimeError(f"the instance exited while waiting for the engine:\n"
                                       f"{self.log_tail()}")
                time.sleep(0.2)
        finally:
            client.close()
        raise RuntimeError(f"the engine never became ready (last ping: {last})\n{self.log_tail()}")

    def commands(self) -> list[dict]:
        """The DAW's own control.commands_list, read without the bridge."""
        client = RawDawClient(self.socket_path, timeout=RESPONSE_TIMEOUT)
        try:
            reply = client.call("control.commands_list")
        finally:
            client.close()
        if not reply.get("ok"):
            raise RuntimeError(f"control.commands_list failed: {reply}")
        return reply["result"]["commands"]

    def ping(self, proto: int = 1) -> dict:
        client = RawDawClient(self.socket_path, timeout=RESPONSE_TIMEOUT)
        try:
            return client.call("control.ping", proto=proto)
        finally:
            client.close()

    # -- process control -------------------------------------------------

    def freeze(self) -> None:
        """SIGSTOP: the instance still holds the socket but never replies."""
        os.kill(self.process.pid, signal.SIGSTOP)

    def resume(self) -> None:
        os.kill(self.process.pid, signal.SIGCONT)

    def kill(self) -> None:
        """SIGKILL: no cleanup, so the socket file is left behind (stale)."""
        if self.process.poll() is None:
            os.kill(self.process.pid, signal.SIGKILL)
            self.process.wait(timeout=30)

    def quit(self, timeout: float = 30.0) -> int:
        """Graceful shutdown through control.quit (unlinks the socket)."""
        try:
            client = RawDawClient(self.socket_path, timeout=RESPONSE_TIMEOUT)
            try:
                client.call("control.quit")
            finally:
                client.close()
        except OSError:
            pass
        deadline = time.time() + timeout
        while time.time() < deadline and self.process.poll() is None:
            time.sleep(0.1)
        if self.process.poll() is None:
            self.kill()
            return -1
        return self.process.returncode

    def log_tail(self, limit: int = 4000) -> str:
        try:
            with open(self.log_path, "r", errors="replace") as handle:
                return handle.read()[-limit:]
        except OSError:
            return "(no log)"

    def cleanup(self) -> None:
        try:
            if self.process.poll() is None:
                self.kill()
        finally:
            try:
                self._log.close()
            except OSError:
                pass
            shutil.rmtree(self.tmp, ignore_errors=True)

    def __enter__(self) -> "Instance":
        return self

    def __exit__(self, *_exc) -> None:
        self.cleanup()


# ---------------------------------------------------------------------------
# A deliberately broken DAW: proves the error taxonomy deterministically
# ---------------------------------------------------------------------------

class StubDaw:
    """An AF_UNIX listener with a policy, so error paths are not races.

    Policies: ``silent`` (accept, read, never reply), ``close`` (accept, read one
    line, close mid-request), ``ping`` (answer control.ping, per `proto` and
    `engine_ready`), ``ok`` (answer everything with an empty result).
    """

    def __init__(self, policy: str = "ok", proto: int = 1, engine_ready: bool = False,
                 tmp_root: str | None = None):
        self.policy = policy
        self.proto = proto
        self.engine_ready = engine_ready
        self.tmp = tempfile.mkdtemp(prefix="zene-stub-", dir=tmp_root or "/tmp")
        self.path = os.path.join(self.tmp, "stub.sock")
        self.seen: list[dict] = []
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._threads: list[threading.Thread] = []
        self._listener: socket.socket | None = None

    def start(self) -> "StubDaw":
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(self.path)
        listener.listen(8)
        listener.settimeout(0.2)
        self._listener = listener
        thread = threading.Thread(target=self._accept_loop, daemon=True)
        thread.start()
        self._threads.append(thread)
        return self

    def _accept_loop(self) -> None:
        while not self._stop.is_set():
            listener = self._listener
            if listener is None:
                return
            try:
                conn, _ = listener.accept()
            except (socket.timeout, TimeoutError):
                continue
            except OSError:
                return
            handler = threading.Thread(target=self._serve, args=(conn,), daemon=True)
            handler.start()
            self._threads.append(handler)

    def _serve(self, conn: socket.socket) -> None:
        buffer = b""
        try:
            while not self._stop.is_set():
                try:
                    chunk = conn.recv(65536)
                except OSError:
                    return
                if not chunk:
                    return
                buffer += chunk
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    try:
                        request = json.loads(line.decode("utf-8", "replace"))
                    except ValueError:
                        continue
                    with self._lock:
                        self.seen.append(request)
                    if self.policy == "silent":
                        continue  # never reply
                    if self.policy == "close":
                        return  # drop the connection mid-request
                    reply = self._reply(request)
                    conn.sendall(json.dumps(reply).encode("utf-8") + b"\n")
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def _reply(self, request: dict) -> dict:
        request_id = request.get("id")
        cmd = request.get("cmd")
        if self.policy == "weird":
            # an error kind the bridge must not trust as its own taxonomy
            return {"id": request_id, "ok": False,
                    "error": {"kind": "made_up_kind", "message": "stub: unknown kind"}}
        if self.policy == "daw_like":
            # mirrors ControlServer::protoMatches: a request without `proto` is
            # accepted from any version, a foreign proto is refused.
            if "proto" in request and request.get("proto") != self.proto:
                return {"id": request_id, "ok": False,
                        "error": {"kind": "refused",
                                  "message": f"unsupported protocol version; this instance "
                                             f"speaks proto {self.proto}"}}
            if cmd == "control.ping":
                return {"id": request_id, "ok": True,
                        "result": {"pong": True, "engine_ready": self.engine_ready,
                                   "version": "stub-0", "proto": self.proto}}
            return {"id": request_id, "ok": True, "result": {}}
        if self.policy == "ping":
            if cmd == "control.ping":
                return {"id": request_id, "ok": True,
                        "result": {"pong": True, "engine_ready": self.engine_ready,
                                   "version": "stub-0", "proto": self.proto}}
            return {"id": request_id, "ok": False,
                    "error": {"kind": "busy", "message": "stub: engine not ready"}}
        return {"id": request_id, "ok": True, "result": {}}

    def commands_seen(self) -> list[str]:
        with self._lock:
            return [str(r.get("cmd")) for r in self.seen]

    def stop(self) -> None:
        self._stop.set()
        if self._listener is not None:
            try:
                self._listener.close()
            except OSError:
                pass
        shutil.rmtree(self.tmp, ignore_errors=True)

    def __enter__(self) -> "StubDaw":
        return self.start()

    def __exit__(self, *_exc) -> None:
        self.stop()
