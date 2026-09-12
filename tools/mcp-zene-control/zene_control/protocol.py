"""The wire client: one AF_UNIX connection, line-delimited JSON-RPC.

Wire contract (verified against `src/core/ControlServer.cpp`, 2026-09-11 and
re-measured while building this bridge):

    request   {"id":<int>,"cmd":"<group.verb>","args":{...},"proto":1}
    response  {"id":..,"ok":true,"result":{..}}
              {"id":..,"ok":false,"error":{"kind":"..","message":".."}}

Every read is bounded by a timeout. Every failure is one of the kinds in
:class:`zene_control.config.ErrorKind` — the four the task calls out (no
instance, stale socket, mid-flight disconnect, protocol mismatch) each get
their own kind and their own actionable message.
"""
from __future__ import annotations

import errno
import json
import os
import socket
import time

from .config import DAW_ERROR_KINDS, ErrorKind, ZeneControlError


def _describe_oserror(path: str, exc: OSError) -> ZeneControlError:
    """Map a connect() errno onto an actionable typed error."""
    if exc.errno == errno.ENOENT:
        return ZeneControlError(
            ErrorKind.NO_INSTANCE,
            f"no control socket at {path}",
        )
    if exc.errno == errno.ECONNREFUSED:
        return ZeneControlError(
            ErrorKind.STALE_SOCKET,
            f"the socket file {path} exists but nothing is listening on it",
            detail={"errno": "ECONNREFUSED", "socket_path": path},
        )
    if exc.errno == errno.ENOTSOCK:
        return ZeneControlError(
            ErrorKind.STALE_SOCKET,
            f"{path} exists but is not a socket",
            detail={"errno": "ENOTSOCK", "socket_path": path},
        )
    if exc.errno in (errno.EACCES, errno.EPERM):
        return ZeneControlError(
            ErrorKind.BRIDGE_ERROR,
            f"permission denied connecting to {path}",
            hint="the instance pins its socket to mode 0600; run the bridge as the user that owns the instance",
            detail={"errno": "EACCES", "socket_path": path},
        )
    return ZeneControlError(
        ErrorKind.BRIDGE_ERROR,
        f"could not connect to {path}: {exc.strerror or exc}",
        detail={"errno": exc.errno, "socket_path": path},
    )


class ControlClient:
    """A single connection to the DAW's control socket.

    One connection per MCP tool call: nothing is shared between calls, so a
    long `render.render` cannot head-of-line-block another call and a
    mid-flight disconnect can only affect the call that hit it.
    """

    def __init__(self, socket_path: str, timeout: float, proto: int = 1):
        self.socket_path = socket_path
        self.timeout = float(timeout)
        self.proto = int(proto)
        self._sock: socket.socket | None = None
        self._buf = b""
        self._next_id = 0

    # -- lifecycle --------------------------------------------------------

    def __enter__(self) -> "ControlClient":
        self.connect()
        return self

    def __exit__(self, *_exc) -> None:
        self.close()

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

    def connect(self) -> None:
        if not os.path.isabs(self.socket_path):
            raise ZeneControlError(
                ErrorKind.BRIDGE_ERROR,
                f"the control socket path must be absolute, got {self.socket_path!r}",
            )
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(self.timeout)
        started = time.monotonic()
        try:
            sock.connect(self.socket_path)
        except OSError as exc:
            sock.close()
            if isinstance(exc, socket.timeout):
                raise ZeneControlError(
                    ErrorKind.TIMEOUT,
                    f"connecting to {self.socket_path} did not complete within {self.timeout:g}s",
                    detail={"elapsed_s": round(time.monotonic() - started, 3)},
                ) from exc
            raise _describe_oserror(self.socket_path, exc) from exc
        self._sock = sock

    # -- framing ----------------------------------------------------------

    def _read_line(self, timeout: float) -> bytes:
        if self._sock is None:
            raise ZeneControlError(ErrorKind.BRIDGE_ERROR, "read before connect")
        deadline = time.monotonic() + timeout
        while b"\n" not in self._buf:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise self._timeout_error(timeout)
            self._sock.settimeout(remaining)
            try:
                chunk = self._sock.recv(65536)
            except socket.timeout as exc:
                raise self._timeout_error(timeout) from exc
            except (ConnectionResetError, BrokenPipeError) as exc:
                raise ZeneControlError(
                    ErrorKind.DISCONNECTED,
                    "the instance closed the connection before replying "
                    "(it exited or crashed mid-request)",
                ) from exc
            except OSError as exc:
                raise ZeneControlError(
                    ErrorKind.DISCONNECTED,
                    f"the control connection failed while reading: {exc.strerror or exc}",
                ) from exc
            if not chunk:
                raise ZeneControlError(
                    ErrorKind.DISCONNECTED,
                    "the instance closed the connection before replying "
                    "(it exited or crashed mid-request)",
                )
            self._buf += chunk
        line, self._buf = self._buf.split(b"\n", 1)
        return line

    def _timeout_error(self, timeout: float) -> ZeneControlError:
        return ZeneControlError(
            ErrorKind.TIMEOUT,
            f"no reply within {timeout:g}s",
            detail={"socket_path": self.socket_path, "budget_s": timeout},
        )

    # -- request ----------------------------------------------------------

    def _send(self, cmd: str, args: dict | None, budget: float, request_id: int,
              include_proto: bool) -> None:
        """Frame one request and send it. Every send failure is typed here."""
        sock = self._sock
        if sock is None:  # request() checks first; this keeps the send path typed on its own
            raise ZeneControlError(ErrorKind.BRIDGE_ERROR, "request before connect")
        envelope: dict = {"id": request_id, "cmd": cmd, "args": args or {}}
        if include_proto:
            envelope["proto"] = self.proto
        payload = json.dumps(envelope, separators=(",", ":"))
        try:
            sock.settimeout(budget)
            sock.sendall(payload.encode("utf-8") + b"\n")
        except socket.timeout as exc:
            raise self._timeout_error(budget) from exc
        except (BrokenPipeError, ConnectionResetError) as exc:
            raise ZeneControlError(
                ErrorKind.DISCONNECTED,
                "the instance closed the connection while the request was being sent",
            ) from exc
        except OSError as exc:
            raise ZeneControlError(
                ErrorKind.DISCONNECTED,
                f"the control connection failed while sending: {exc.strerror or exc}",
            ) from exc

    def _read_reply(self, cmd: str, request_id: int, budget: float) -> dict:
        """The reply addressed to `request_id`, parsed; anything else is typed."""
        line = self._read_line(budget)
        try:
            reply = json.loads(line.decode("utf-8", "replace"))
        except ValueError as exc:
            raise ZeneControlError(
                ErrorKind.BRIDGE_ERROR,
                "the instance sent a reply that is not JSON",
                detail={"line": line[:400].decode("utf-8", "replace")},
            ) from exc
        if not isinstance(reply, dict):
            raise ZeneControlError(ErrorKind.BRIDGE_ERROR, "the instance's reply was not an object")
        if reply.get("id") != request_id:
            raise ZeneControlError(
                ErrorKind.BRIDGE_ERROR,
                f"reply id {reply.get('id')!r} does not match request id {request_id}",
                detail={"cmd": cmd, "reply": str(reply)[:400]},
            )
        return reply

    @staticmethod
    def _reply_result(cmd: str, reply: dict) -> dict:
        """The `result` of an ok reply, or the typed error the reply carries.

        An error kind the bridge does not recognise is downgraded to
        `bridge_error` rather than trusted; the DAW's own kinds pass through
        verbatim.
        """
        if reply.get("ok") is True:
            result = reply.get("result")
            return result if isinstance(result, dict) else {}
        error = reply.get("error") if isinstance(reply.get("error"), dict) else {}
        kind = str(error.get("kind") or ErrorKind.BRIDGE_ERROR)
        if kind not in DAW_ERROR_KINDS:
            kind = ErrorKind.BRIDGE_ERROR
        raise ZeneControlError(
            kind,
            str(error.get("message") or f"{cmd} failed"),
            detail={"command": cmd, "daw_error": str(reply.get("error"))[:400]},
        )

    def request(self, cmd: str, args: dict | None = None, timeout: float | None = None,
                include_proto: bool = True) -> dict:
        """Send one command and return its `result` object, or raise typed.

        `include_proto=False` omits the `proto` member. The DAW's ControlServer
        accepts a request without it (``protoMatches`` returns true when the
        member is absent) and still reports the version it speaks in the reply,
        which is the only way to learn the version of an instance that would
        otherwise refuse a request declaring a foreign one.

        The three steps — frame/send, read/verify, interpret — are separate
        methods on purpose: each carries its own typed failures, and the caller
        here is a straight line so the order is visible (send errors are raised
        before anything is read, as they always were).
        """
        if self._sock is None:
            raise ZeneControlError(ErrorKind.BRIDGE_ERROR, "request before connect")
        budget = self.timeout if timeout is None else float(timeout)
        self._next_id += 1
        request_id = self._next_id
        self._send(cmd, args, budget, request_id, include_proto)
        reply = self._read_reply(cmd, request_id, budget)
        return self._reply_result(cmd, reply)

    # -- readiness --------------------------------------------------------

    def handshake(self, wait_ready: bool, ready_timeout: float | None = None,
                  expected_proto: int | None = None) -> dict:
        """Verify the protocol version and, when asked, wait for readiness.

        `control.ping` is the readiness probe (AGENT-TOOLING.md §4) and the only
        command this loop sends. When `wait_ready` is true it polls until
        `engine_ready` is true or `ready_timeout` (default: this client's own
        budget) runs out — the caller passes the bridge's readiness budget, so
        the loop can never overrun it. Engine commands are never sent before
        this returns.

        The probe is sent WITHOUT the `proto` member on purpose: the DAW accepts
        a proto-less request from any version and reports its own version back,
        so a version mismatch is reported as a typed `proto_mismatch` instead of
        arriving as the DAW's generic `refused`.
        """
        expected = self.proto if expected_proto is None else int(expected_proto)
        budget = self.timeout if ready_timeout is None else float(ready_timeout)
        deadline = time.monotonic() + max(budget, 0.1)
        ping: dict = {}
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            reply = self.request("control.ping", timeout=min(remaining, 5.0),
                                 include_proto=False)
            ping = reply
            reported = ping.get("proto")
            if reported is None:
                raise ZeneControlError(
                    ErrorKind.PROTO_MISMATCH,
                    f"the instance did not report a control-protocol version; "
                    f"the bridge speaks proto {expected}",
                    detail={"ping": ping},
                )
            if int(reported) != expected:
                raise ZeneControlError(
                    ErrorKind.PROTO_MISMATCH,
                    f"the instance speaks control proto {reported}, the bridge requires proto {expected}",
                    detail={"instance_proto": int(reported), "bridge_proto": expected,
                            "instance_version": ping.get("version")},
                )
            if not wait_ready or ping.get("engine_ready") is True:
                return ping
            time.sleep(0.15)
        raise ZeneControlError(
            ErrorKind.NOT_READY,
            f"the engine was not ready after {budget:g}s of polling control.ping "
            f"(last ping: {ping})",
            detail={"last_ping": ping, "ready_budget_s": budget},
        )
