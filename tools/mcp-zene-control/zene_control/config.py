"""Path resolution, defaults and the typed error taxonomy of the bridge.

The bridge holds no DAW knowledge of its own: it discovers every command from
``control.commands_list`` on a live instance (see :mod:`zene_control.registry`).
What lives here is only *bridge* policy — where the socket is, how long a call
may take, and the closed set of error kinds a caller can branch on.
"""
from __future__ import annotations

import argparse
import os
from dataclasses import dataclass, field
from pathlib import Path

#: MCP server identity.
SERVER_NAME = "zene-control"
SERVER_VERSION = "1.0.0"

#: Name the bridge looks for in the working directory when nothing else says
#: where the DAW's control socket is (see `Config.socket_path`).
DEFAULT_SOCKET_NAME = "zene-control.sock"

#: The control-protocol version the DAW spoke when this bridge was verified.
#: `control.ping` reports the version an instance speaks; a difference is a
#: typed `proto_mismatch` error rather than a guess.
DEFAULT_PROTO = 1

#: Bounded defaults. Every socket operation is bounded by one of these.
DEFAULT_READY_TIMEOUT = 60.0      # poll control.ping this long for engine_ready
DEFAULT_CALL_TIMEOUT = 30.0       # per-command reply budget
DEFAULT_LONG_CALL_TIMEOUT = 900.0 # per-command budget for LONG_COMMANDS
MAX_CALL_TIMEOUT = 3600.0
MIN_CALL_TIMEOUT = 0.1
MIN_READY_TIMEOUT = 0.5

#: Commands whose group is engine-free in the DAW. Verified against
#: `src/core/ControlCommandsControl.cpp`: every `control.*` command sets
#: `requiresEngine = false`; every other registered command keeps the default
#: `true`. The registry does NOT serialise `requiresEngine` in
#: `control.commands_list`, so the bridge derives it from the group and says so
#: in the documentation rather than pretending it read it off the wire.
ENGINE_FREE_GROUPS = frozenset({"control"})

#: Bridge-side policy, not registry data: the registry declares no duration
#: hint, so a long command is named here with the reason it is long. Everything
#: else uses `DEFAULT_CALL_TIMEOUT`. Callers can always pass `timeout_s`.
LONG_COMMANDS: dict[str, str] = {
    "render.render": "renders the whole song by shelling out to the CLI; minutes on a long project",
}


class ErrorKind:
    """The closed set of error kinds the bridge reports.

    The last five are the DAW's own kinds (AGENT-TOOLING.md §4) and are passed
    through verbatim; the rest are raised by the bridge and mean something went
    wrong between an MCP client and the DAW.
    """

    # --- bridge-side -----------------------------------------------------
    NO_INSTANCE = "no_instance"        # nothing exists at the socket path
    STALE_SOCKET = "stale_socket"      # the file is there, nobody is listening
    DISCONNECTED = "disconnected"      # the instance went away mid-request
    TIMEOUT = "timeout"                # bounded budget for a call ran out
    PROTO_MISMATCH = "proto_mismatch"  # instance speaks a different proto
    NOT_READY = "not_ready"            # engine_ready never became true in time
    BRIDGE_ERROR = "bridge_error"      # malformed reply / internal failure

    # --- passthrough from the DAW ---------------------------------------
    NOT_FOUND = "not_found"
    REQUIRES = "requires"
    INVALID_ARGS = "invalid_args"
    BUSY = "busy"
    REFUSED = "refused"


#: Kinds the DAW itself can put on the wire.
DAW_ERROR_KINDS = frozenset(
    {ErrorKind.NOT_FOUND, ErrorKind.REQUIRES, ErrorKind.INVALID_ARGS,
     ErrorKind.BUSY, ErrorKind.REFUSED}
)

#: Kinds worth retrying unchanged (transient or startup-related).
RETRYABLE_KINDS = frozenset(
    {ErrorKind.NOT_READY, ErrorKind.BUSY, ErrorKind.TIMEOUT, ErrorKind.DISCONNECTED,
     ErrorKind.STALE_SOCKET, ErrorKind.NO_INSTANCE}
)

HINTS = {
    ErrorKind.NO_INSTANCE: "start an instance with `--control-socket <path>`, "
                           "or point the bridge at the running one with --socket / ZENE_CONTROL_SOCKET",
    ErrorKind.STALE_SOCKET: "the socket file was left behind by an instance that is gone; "
                            "either remove the file or start the instance that owns it",
    ErrorKind.DISCONNECTED: "the instance exited or crashed mid-request; check it is still running "
                            "and re-issue the call",
    ErrorKind.TIMEOUT: "raise the per-call budget with the tool's `timeout_s` argument or the "
                       "bridge's --call-timeout, and check the instance is not wedged",
    ErrorKind.PROTO_MISMATCH: "restart the bridge with `--proto <the instance's version>`",
    ErrorKind.NOT_READY: "the engine never became ready: a headless instance needs the Dummy-audio "
                         "config recipe, and `control.ping` reports readiness per call",
    ErrorKind.BUSY: "the instance is still starting up; retry after control.ping reports engine_ready",
    ErrorKind.NOT_FOUND: "the instance does not register that command or id; call zene_commands "
                        "for the live list",
    ErrorKind.INVALID_ARGS: "the arguments did not satisfy the command's args_schema; the tool's "
                           "inputSchema is the DAW's own, so a missing or mistyped field is the cause",
    ErrorKind.REQUIRES: "the command declares a precondition the instance does not meet",
    ErrorKind.REFUSED: "the instance refused the change (the command exists but the state does "
                       "not allow it); the message says why",
    ErrorKind.BRIDGE_ERROR: "the reply could not be used; see detail",
}


class ZeneControlError(Exception):
    """A typed, actionable failure. Never a bare hang, never a silent no-op."""

    def __init__(self, kind: str, message: str, hint: str | None = None,
                 detail: dict | None = None):
        super().__init__(message)
        self.kind = kind
        self.message = message
        self.hint = hint if hint is not None else HINTS.get(kind)
        self.detail = detail or {}

    def to_payload(self, command: str | None = None, elapsed_s: float | None = None) -> dict:
        error: dict = {
            "kind": self.kind,
            "message": self.message,
            "retryable": self.kind in RETRYABLE_KINDS,
            "origin": "daw" if self.kind in DAW_ERROR_KINDS else "bridge",
        }
        if self.hint:
            error["hint"] = self.hint
        if command:
            error["command"] = command
        if elapsed_s is not None:
            error["elapsed_s"] = round(elapsed_s, 3)
        if self.detail:
            error["detail"] = self.detail
        return {"ok": False, "error": error, "summary": f"ERROR [{self.kind}]: {self.message}"}


@dataclass(frozen=True)
class Config:
    """Everything the bridge needs to find and talk to one instance."""

    socket_path: str
    workdir: str
    state_dir: str
    proto: int = DEFAULT_PROTO
    ready_timeout: float = DEFAULT_READY_TIMEOUT
    call_timeout: float = DEFAULT_CALL_TIMEOUT
    long_call_timeout: float = DEFAULT_LONG_CALL_TIMEOUT
    snapshot_path: str = ""
    offline: bool = False

    def timeout_for(self, command_id: str, override: float | None = None) -> float:
        """Per-call budget: explicit override, else the long-command policy."""
        if override is not None:
            return clamp_timeout(float(override))
        if command_id in LONG_COMMANDS:
            return self.long_call_timeout
        return self.call_timeout


def clamp_timeout(value: float) -> float:
    if not value == value:  # NaN
        raise ZeneControlError(ErrorKind.BRIDGE_ERROR, "timeout_s must be a number")
    return max(MIN_CALL_TIMEOUT, min(MAX_CALL_TIMEOUT, float(value)))


def _env_float(name: str, default: float) -> float:
    raw = os.environ.get(name)
    if raw is None or raw.strip() == "":
        return default
    try:
        return float(raw)
    except ValueError as exc:
        raise ZeneControlError(ErrorKind.BRIDGE_ERROR,
                               f"{name}={raw!r} is not a number") from exc


def _env_int(name: str, default: int) -> int:
    raw = os.environ.get(name)
    if raw is None or raw.strip() == "":
        return default
    try:
        return int(raw)
    except ValueError as exc:
        raise ZeneControlError(ErrorKind.BRIDGE_ERROR,
                               f"{name}={raw!r} is not an integer") from exc


def parse_args(argv: list[str] | None = None) -> Config:
    """Resolve the configuration from CLI arguments and the environment.

    Precedence for every field: CLI argument > ``ZENE_CONTROL_*`` environment
    variable > documented default. The socket default is
    ``<workdir>/zene-control.sock`` and the workdir defaults to the current
    working directory, so an instance launched from this directory is found
    without any configuration at all.
    """
    parser = argparse.ArgumentParser(
        prog="zene-control",
        description="MCP bridge to a running Zene Studio control socket. "
                    "Tools are generated from the instance's control.commands_list.",
    )
    parser.add_argument("--socket", dest="socket_path", default=None,
                        help=f"control socket path (default: <workdir>/{DEFAULT_SOCKET_NAME})")
    parser.add_argument("--workdir", default=None,
                        help="working directory used to resolve the default socket path (default: cwd)")
    parser.add_argument("--state-dir", default=None,
                        help="where the last-known command list is cached (default: <server>/.state)")
    parser.add_argument("--proto", type=int, default=None,
                        help=f"control protocol version to require (default: {DEFAULT_PROTO})")
    parser.add_argument("--ready-timeout", type=float, default=None,
                        help=f"seconds to poll control.ping for engine_ready (default: {DEFAULT_READY_TIMEOUT})")
    parser.add_argument("--call-timeout", type=float, default=None,
                        help=f"default per-call budget in seconds (default: {DEFAULT_CALL_TIMEOUT})")
    parser.add_argument("--long-call-timeout", type=float, default=None,
                        help=f"per-call budget for long commands (default: {DEFAULT_LONG_CALL_TIMEOUT})")
    parser.add_argument("--offline", action="store_true",
                        help="never open the socket; serve the cached/snapshot command list only")
    args = parser.parse_args(argv)

    workdir = os.path.abspath(
        args.workdir or os.environ.get("ZENE_CONTROL_WORKDIR") or os.getcwd()
    )
    socket_path = (
        args.socket_path
        or os.environ.get("ZENE_CONTROL_SOCKET")
        or os.path.join(workdir, DEFAULT_SOCKET_NAME)
    )
    # The DAW refuses a relative socket path, so fail here with a clear message
    # rather than on the far side of a connect().
    socket_path = os.path.abspath(socket_path)

    server_dir = str(Path(__file__).resolve().parent)
    state_dir = os.path.abspath(
        args.state_dir or os.environ.get("ZENE_CONTROL_STATE")
        or os.path.join(os.path.dirname(server_dir), ".state")
    )

    return Config(
        socket_path=socket_path,
        workdir=workdir,
        state_dir=state_dir,
        proto=args.proto if args.proto is not None else _env_int("ZENE_CONTROL_PROTO", DEFAULT_PROTO),
        ready_timeout=max(MIN_READY_TIMEOUT, args.ready_timeout if args.ready_timeout is not None
                          else _env_float("ZENE_CONTROL_READY_TIMEOUT", DEFAULT_READY_TIMEOUT)),
        call_timeout=clamp_timeout(args.call_timeout if args.call_timeout is not None
                                   else _env_float("ZENE_CONTROL_CALL_TIMEOUT", DEFAULT_CALL_TIMEOUT)),
        long_call_timeout=clamp_timeout(args.long_call_timeout if args.long_call_timeout is not None
                                        else _env_float("ZENE_CONTROL_LONG_CALL_TIMEOUT", DEFAULT_LONG_CALL_TIMEOUT)),
        snapshot_path=os.environ.get("ZENE_CONTROL_SNAPSHOT", ""),
        offline=bool(args.offline) or os.environ.get("ZENE_CONTROL_OFFLINE", "").strip() not in ("", "0"),
    )


def engine_free(group: str) -> bool:
    """True when a command's group answers before the engine is initialised."""
    return group in ENGINE_FREE_GROUPS
