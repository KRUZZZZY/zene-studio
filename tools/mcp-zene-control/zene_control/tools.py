"""Bridge behaviour: the generated-tool call path and the bridge-owned tools.

Three bridge-owned tools exist no matter what the DAW registers:

* `zene_commands` — the DAW's command list, live when an instance is up and
  from the cache/snapshot when it is not, so an agent can see what exists
  *before* launching anything.
* `zene_status`  — where the socket is, whether an instance answers, whether the
  engine is ready, and the verbatim typed error when it is not.

Everything else is generated (see :mod:`zene_control.registry`).
"""
from __future__ import annotations

import os
import time
from typing import Any

from .config import (ENGINE_FREE_GROUPS, LONG_COMMANDS, SERVER_NAME, SERVER_VERSION,
                     Config, ErrorKind, ZeneControlError, engine_free)
from .protocol import ControlClient
from . import registry as R

#: Readiness may consume at most this fraction-free bound: the readiness poll
#: uses min(ready_timeout, timeout_s) and the command then gets its own
#: timeout_s, so one call is bounded by ready_timeout + timeout_s.
COMMANDS_LIST_TIMEOUT = 10.0


class Bridge:
    """Stateless-ish façade over the socket: one instance per configuration."""

    def __init__(self, config: Config):
        self.config = config

    # -- command-list resolution -----------------------------------------

    def cache_path(self) -> str:
        return os.path.join(self.config.state_dir, "commands.json")

    def snapshot_path(self) -> str:
        return self.config.snapshot_path or os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "commands_snapshot.json")

    def fetch_live(self, timeout: float | None = None) -> R.CommandSource:
        """`control.commands_list` from the running instance (engine-free)."""
        budget = timeout or min(self.config.call_timeout, COMMANDS_LIST_TIMEOUT)
        with ControlClient(self.config.socket_path, budget, self.config.proto) as client:
            ping = client.handshake(wait_ready=False)
            result = client.request("control.commands_list", timeout=budget)
        provenance = {
            "version": ping.get("version"),
            "proto": ping.get("proto"),
            "engine_ready": ping.get("engine_ready"),
            "socket_path": self.config.socket_path,
        }
        bundle = R.bundle_from_result(result, provenance)
        R.save_bundle(self.cache_path(), bundle)
        return R.CommandSource(specs=R.specs_from_commands(bundle["commands"]),
                               bundle=bundle, source="live")

    def resolve(self, prefer_live: bool = True,
                timeout: float | None = None) -> R.CommandSource:
        """The command list to serve. Never raises; says where it came from."""
        error: ZeneControlError | None = None
        offline = self.config.offline
        if prefer_live and not offline:
            try:
                return self.fetch_live(timeout=timeout)
            except ZeneControlError as exc:
                error = exc
            except Exception as exc:  # defensive: a listing must never kill tools/list
                error = ZeneControlError(ErrorKind.BRIDGE_ERROR,
                                         f"could not read the live command list: {exc}")
        for path, source in ((self.cache_path(), "cache"), (self.snapshot_path(), "snapshot")):
            bundle = R.load_bundle(path)
            if bundle is not None:
                try:
                    specs = R.specs_from_commands(bundle["commands"])
                except ZeneControlError as exc:
                    error = error or exc
                    continue
                return R.CommandSource(specs=specs, bundle=bundle, source=source,
                                       path=path, error=error)
        return R.CommandSource(specs=[], bundle={}, source="none", error=error)

    def find_spec(self, command_id: str, source: R.CommandSource | None = None) -> R.CommandSpec:
        resolved = source or self.resolve()
        for spec in resolved.specs:
            if spec.id == command_id:
                return spec
        raise ZeneControlError(
            ErrorKind.NOT_FOUND,
            f"the DAW's command list ({resolved.source}, {resolved.count} commands) has no {command_id!r}",
            hint="call zene_commands to see what this instance registers",
        )

    def find_spec_by_tool(self, name: str, source: R.CommandSource | None = None) -> R.CommandSpec:
        resolved = source or self.resolve()
        for spec in resolved.specs:
            if spec.tool_name == name:
                return spec
        known = ", ".join(sorted(s.tool_name for s in resolved.specs))
        raise ZeneControlError(
            ErrorKind.NOT_FOUND,
            f"unknown tool {name!r}; the DAW registers: {known}",
            hint="tools are generated from control.commands_list — call zene_commands for the live list",
        )

    # -- the generated-tool call path -------------------------------------

    def call_command(self, spec: R.CommandSpec, arguments: dict | None = None) -> dict:
        args = dict(arguments or {})
        override = args.pop("timeout_s", None)
        timeout = self.config.timeout_for(spec.id, override)
        engine_free_command = not spec.requires_engine
        started = time.monotonic()
        error: ZeneControlError | None = None
        try:
            with ControlClient(self.config.socket_path, timeout, self.config.proto) as client:
                # READINESS RULE: ping first, wait for engine_ready when the
                # command needs the engine, and only then send the command.
                ping = client.handshake(
                    wait_ready=not engine_free_command,
                    ready_timeout=min(self.config.ready_timeout, timeout),
                )
                result = client.request(spec.id, args, timeout=timeout)
        except ZeneControlError as exc:
            error = exc
            ping = None
            result = {}
        elapsed = time.monotonic() - started
        if error is not None:
            payload = error.to_payload(command=spec.id, elapsed_s=elapsed)
            payload["tool"] = spec.tool_name
            payload["timeout_s"] = timeout
            payload["engine_free"] = engine_free_command
            return payload
        payload = {
            "ok": True,
            "tool": spec.tool_name,
            "command": spec.id,
            "mutating": spec.mutating,
            "engine_free": engine_free_command,
            "timeout_s": timeout,
            "elapsed_s": round(elapsed, 3),
            "result": result,
            "instance": {
                "version": (ping or {}).get("version"),
                "proto": (ping or {}).get("proto"),
                "engine_ready": (ping or {}).get("engine_ready"),
                "socket_path": self.config.socket_path,
            },
            "summary": self._summarise(spec, result, elapsed),
        }
        if spec.id in LONG_COMMANDS:
            payload["long_operation"] = LONG_COMMANDS[spec.id]
        return payload

    @staticmethod
    def _summarise(spec: R.CommandSpec, result: dict, elapsed: float) -> str:
        interesting = ", ".join(f"{k}={v}" for k, v in list(result.items())[:4])
        detail = f" ({interesting})" if interesting else ""
        return f"{spec.id}: ok in {elapsed:.2f}s{detail}"

    # -- bridge-owned tools ----------------------------------------------

    def commands_payload(self, arguments: dict | None = None) -> dict:
        args = dict(arguments or {})
        override = args.pop("timeout_s", None)
        source_pref = str(args.pop("source", "auto") or "auto")
        include_schemas = bool(args.pop("include_schemas", True))
        if source_pref not in ("auto", "live", "cache", "snapshot"):
            return ZeneControlError(
                ErrorKind.INVALID_ARGS,
                f"source must be one of auto|live|cache|snapshot, got {source_pref!r}",
            ).to_payload(command="zene_commands")
        if source_pref == "cache":
            resolved = self.resolve(prefer_live=False, timeout=override)
            # resolve() falls through cache -> snapshot; keep the honest label.
        elif source_pref == "snapshot":
            bundle = R.load_bundle(self.snapshot_path())
            if bundle is None:
                return ZeneControlError(
                    ErrorKind.NOT_FOUND,
                    f"no command snapshot at {self.snapshot_path()}",
                ).to_payload(command="zene_commands")
            resolved = R.CommandSource(specs=R.specs_from_commands(bundle["commands"]),
                                       bundle=bundle, source="snapshot",
                                       path=self.snapshot_path())
        elif source_pref == "live":
            budget = self.config.timeout_for("control.commands_list", override)
            resolved = self.fetch_live(timeout=budget)
        else:
            resolved = self.resolve(timeout=override)

        payload = resolved.payload(
            bridge={"name": SERVER_NAME, "version": SERVER_VERSION},
            socket_path=self.config.socket_path,
            live=resolved.source == "live",
            naming={
                "prefix": R.TOOL_PREFIX,
                "rule": "tool name = prefix + command id with '.' replaced by '_' "
                        "(and any other unsafe character replaced by '_')",
                "example": "mixer.set_volume -> zene_mixer_set_volume",
                "collision_policy": "refuse (never drop a command silently)",
            },
            bridge_tools=list(R.BRIDGE_TOOL_NAMES),
            engine_free_groups=sorted(ENGINE_FREE_GROUPS),
        )
        if not include_schemas:
            payload["commands"] = [
                {k: c.get(k) for k in ("id", "group", "description", "requires", "mutating")}
                | {"tool_name": R.tool_name(str(c.get("id")))}
                for c in payload["commands"]
            ]
        if resolved.error is not None:
            payload["summary"] = resolved.summary()
        return payload

    def status_payload(self, arguments: dict | None = None) -> dict:
        args = dict(arguments or {})
        override = args.pop("timeout_s", None)
        budget = self.config.timeout_for("zene_status", override)
        socket_exists = os.path.exists(self.config.socket_path)
        payload: dict[str, Any] = {
            "ok": True,
            "bridge": {"name": SERVER_NAME, "version": SERVER_VERSION},
            "socket_path": self.config.socket_path,
            "socket_exists": socket_exists,
            "workdir": self.config.workdir,
            "state_dir": self.config.state_dir,
            "expected_proto": self.config.proto,
            "offline": self.config.offline,
            "budgets": {
                "ready_timeout_s": self.config.ready_timeout,
                "call_timeout_s": self.config.call_timeout,
                "long_call_timeout_s": self.config.long_call_timeout,
            },
            "reachable": False,
            "engine_ready": None,
            "instance": None,
            "error": None,
        }
        try:
            with ControlClient(self.config.socket_path, budget, self.config.proto) as client:
                ping = client.handshake(wait_ready=False)
            payload.update({
                "reachable": True,
                "engine_ready": ping.get("engine_ready"),
                "instance": {"version": ping.get("version"), "proto": ping.get("proto")},
            })
        except ZeneControlError as exc:
            payload["error"] = exc.to_payload(command="zene_status")["error"]

        resolved = self.resolve(prefer_live=False)
        payload["command_list"] = {
            "source": resolved.source,
            "count": resolved.count,
            "captured_at": resolved.bundle.get("captured_at"),
            "path": resolved.path,
            "proto": resolved.bundle.get("proto"),
        }
        snapshot = R.load_bundle(self.snapshot_path())
        if snapshot is not None:
            payload["snapshot"] = {
                "path": self.snapshot_path(),
                "captured_at": snapshot.get("captured_at"),
                "count": snapshot.get("count"),
                "instance": snapshot.get("instance"),
            }
        if not socket_exists and payload["error"] is None:
            payload["error"] = ZeneControlError(
                ErrorKind.NO_INSTANCE, f"no control socket at {self.config.socket_path}"
            ).to_payload(command="zene_status")["error"]

        if payload["reachable"] and payload["engine_ready"]:
            payload["verdict"] = "instance reachable and engine ready: engine commands are safe to call"
        elif payload["reachable"]:
            payload["verdict"] = ("instance reachable but engine_ready is false: the bridge will poll "
                                  "control.ping before sending engine commands")
        else:
            payload["verdict"] = ("no usable instance; the bridge still serves the cached/snapshot "
                                  "command list (zene_commands)")
        payload["summary"] = (f"zene-control: reachable={payload['reachable']} "
                              f"engine_ready={payload['engine_ready']} "
                              f"commands={payload['command_list']['count']} "
                              f"({payload['command_list']['source']}) socket={self.config.socket_path}")
        return payload

    def project_state(self) -> dict:
        """`project.get_state`, the state the product serialises (SPEC A14)."""
        spec = self.find_spec("project.get_state")
        return self.call_command(spec, {})


# -- bridge-owned tool schemas --------------------------------------------

_TIME = {
    "type": "number", "minimum": 0.1, "maximum": 3600,
    "description": "Bridge-side budget in seconds for this call. Bounded; never a hang.",
}

BRIDGE_TOOL_SPECS: list[tuple[str, str, dict]] = [
    (
        "zene_commands",
        "List the DAW's command registry — the same list the bridge turns into tools. "
        "Uses the live instance when one answers, and falls back to the last-known copy "
        "(cache, then the committed snapshot) when none does, so this works BEFORE an "
        "instance exists. Reports honestly where the list came from.",
        {
            "type": "object",
            "properties": {
                "source": {"type": "string", "enum": ["auto", "live", "cache", "snapshot"],
                           "default": "auto",
                           "description": "auto = live, else cache, else snapshot."},
                "include_schemas": {"type": "boolean", "default": True,
                                    "description": "Include each command's args/result JSON schemas."},
                "timeout_s": _TIME,
            },
            "required": [],
            "additionalProperties": False,
        },
    ),
    (
        "zene_status",
        "Where the control socket is, whether an instance answers, whether its engine is "
        "ready, the bridge's budgets and the typed error when there is no instance. Always "
        "answers (a diagnostic), never raises for a missing instance.",
        {
            "type": "object",
            "properties": {"timeout_s": _TIME},
            "required": [],
            "additionalProperties": False,
        },
    ),
]

RESOURCE_PROJECT_STATE = "zene://project/state"
RESOURCE_INSTANCE_STATUS = "zene://instance/status"


def engine_free_command_id(command_id: str) -> bool:
    """True for the engine-free `control.*` group (used by the docs/tests)."""
    return engine_free(command_id.split(".", 1)[0])


def long_commands() -> dict[str, str]:
    return dict(LONG_COMMANDS)
