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

from .config import (COMMANDS_LIST_TIMEOUT, ENGINE_FREE_GROUPS, LONG_COMMANDS, SERVER_NAME,
                     SERVER_VERSION, SNAPSHOT_FIX, Config, ErrorKind, ZeneControlError,
                     engine_free)
from .protocol import ControlClient
from .staleness import StalenessMixin
from . import registry as R

#: The command-list probe's bound moved to :mod:`zene_control.config` (the
#: staleness mixin uses the same value); it stays importable from here because
#: this module documented it, and `__all__` says so.
__all__ = ["Bridge", "COMMANDS_LIST_TIMEOUT"]


class Bridge(StalenessMixin):
    """Stateless-ish façade over the socket: one instance per configuration.

    Serving and dispatching commands live here; the live-vs-offline surface
    comparison (`probe_live`, `offline_drift`) is in :class:`staleness.StalenessMixin`,
    which needs nothing from this file but the two paths it defines.
    """

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
        """The command list to serve. Never raises; says where it came from.

        Live first when asked for. With no instance, the offline copies are
        ranked by freshness (`registry.rank_offline_bundles`) instead of being
        taken in a fixed order: the cache is just "the list the last live fetch
        saw", so serving it unconditionally let a stale cache shadow a newer
        committed snapshot. The copy that is served is named, and the ones that
        were ranked staler are reported in `skipped`.
        """
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
        ranked = R.rank_offline_bundles([(self.cache_path(), "cache"),
                                         (self.snapshot_path(), "snapshot")])
        for path, source, bundle in ranked:
            try:
                specs = R.specs_from_commands(bundle["commands"])
            except ZeneControlError as exc:
                # A copy the bridge cannot turn into tools is skipped, not fatal:
                # the next-freshest readable copy is still a real answer.
                error = error or exc
                continue
            return R.CommandSource(specs=specs, bundle=bundle, source=source,
                                   path=path, error=error,
                                   skipped=self._staler_copies(ranked, path))
        return R.CommandSource(specs=[], bundle={}, source="none", error=error)

    @staticmethod
    def _staler_copies(ranked: list[tuple[str, str, dict]],
                       served_path: str) -> tuple[dict, ...]:
        """The readable offline copies the served one outranks, for the record."""
        served = next((entry for entry in ranked if entry[0] == served_path), None)
        if served is None or len(ranked) < 2:
            return ()
        return tuple({
            "path": path, "source": source,
            "captured_at": bundle.get("captured_at"),
            "count": len(bundle.get("commands") or []),
            "why": "ranked staler than the copy served",
        } for path, source, bundle in ranked if path != served_path)

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

    @staticmethod
    def _commands_arguments(arguments: dict | None) -> tuple[Any, str, bool]:
        """(timeout override, source preference, include schemas) from the tool arguments."""
        args = dict(arguments or {})
        override = args.pop("timeout_s", None)
        source_pref = str(args.pop("source", "auto") or "auto")
        include_schemas = bool(args.pop("include_schemas", True))
        return override, source_pref, include_schemas

    def _commands_source_or_error(self, source_pref: str, override: float | None):
        """The CommandSource for one preference, or the error payload to return.

        Exactly one member of the returned pair is None: either the resolved
        source, or the payload describing why that preference cannot be served.
        """
        if source_pref == "cache":
            # resolve() falls through cache -> snapshot; keep the honest label.
            return self.resolve(prefer_live=False, timeout=override), None
        if source_pref == "snapshot":
            bundle = R.load_bundle(self.snapshot_path())
            if bundle is None:
                return None, ZeneControlError(
                    ErrorKind.NOT_FOUND,
                    f"no command snapshot at {self.snapshot_path()}",
                ).to_payload(command="zene_commands")
            return R.CommandSource(specs=R.specs_from_commands(bundle["commands"]),
                                   bundle=bundle, source="snapshot",
                                   path=self.snapshot_path()), None
        if source_pref == "live":
            budget = self.config.timeout_for("control.commands_list", override)
            return self.fetch_live(timeout=budget), None
        return self.resolve(timeout=override), None

    @staticmethod
    def _compact_commands(payload: dict) -> None:
        """Replace each command with the fields an agent scans, no schemas."""
        payload["commands"] = [
            {k: c.get(k) for k in ("id", "group", "description", "requires", "mutating")}
            | {"tool_name": R.tool_name(str(c.get("id")))}
            for c in payload["commands"]
        ]

    def commands_payload(self, arguments: dict | None = None) -> dict:
        """`zene_commands`: the DAW's command list, from wherever it can be got."""
        override, source_pref, include_schemas = self._commands_arguments(arguments)
        if source_pref not in ("auto", "live", "cache", "snapshot"):
            return ZeneControlError(
                ErrorKind.INVALID_ARGS,
                f"source must be one of auto|live|cache|snapshot, got {source_pref!r}",
            ).to_payload(command="zene_commands")
        resolved, error = self._commands_source_or_error(source_pref, override)
        if error is not None:
            return error
        # _commands_source_or_error returns exactly one of the two, so a source
        # is in hand here; the assert states that invariant for reader and type
        # checker alike.
        assert resolved is not None

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
        if resolved.source == "live":
            # A live list is authoritative for TOOLS, but the offline copies are
            # what answers when no instance is up: measure them while there IS
            # one, so staleness is reported here instead of discovered later.
            payload["offline_drift"] = self.offline_drift(resolved.bundle)
        if not include_schemas:
            self._compact_commands(payload)
        if resolved.error is not None:
            payload["summary"] = resolved.summary()
        drift = payload.get("offline_drift")
        if drift and drift["stale"]:
            payload["summary"] = (f"{payload['summary']}; the offline copy(ies) "
                                  f"{', '.join(drift['stale_copies'])} are STALE against this "
                                  f"instance (see offline_drift) — regenerate with: "
                                  f"{SNAPSHOT_FIX.split('#')[0].strip()}")
        return payload

    def _status_base(self) -> dict[str, Any]:
        """The diagnostic's configuration half: everything that needs no socket."""
        return {
            "ok": True,
            "bridge": {"name": SERVER_NAME, "version": SERVER_VERSION},
            "socket_path": self.config.socket_path,
            "socket_exists": os.path.exists(self.config.socket_path),
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

    def _status_probe(self, payload: dict, budget: float) -> None:
        """Fill in reachability: one bounded ping, and the typed error when it fails."""
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
            if not payload["socket_exists"]:
                payload["error"] = ZeneControlError(
                    ErrorKind.NO_INSTANCE, f"no control socket at {self.config.socket_path}"
                ).to_payload(command="zene_status")["error"]

    def _status_surfaces(self, payload: dict, budget: float) -> None:
        """What an offline call would serve, and how it compares with a live instance.

        The instance is answering, so the question "is the offline list stale?" HAS
        an answer. Ask it: without this, the only way to learn that the snapshot was
        older than the running DAW was to compare figures by hand, which is what
        left a 70-id copy in place for days.
        """
        resolved = self.resolve(prefer_live=False)
        payload["command_list"] = {
            "source": resolved.source,
            "count": resolved.count,
            "captured_at": resolved.bundle.get("captured_at"),
            "surface": R.bundle_surface(resolved.bundle),
            "path": resolved.path,
            "proto": resolved.bundle.get("proto"),
        }
        if payload["reachable"]:
            live_bundle, live_error = self.probe_live(budget)
            payload["live_surface"] = self._live_surface(live_bundle, live_error)
            if live_bundle is not None:
                payload["offline_drift"] = self.offline_drift(live_bundle)
                payload["command_list"]["live_count"] = live_bundle["count"]
        snapshot = R.load_bundle(self.snapshot_path())
        if snapshot is not None:
            payload["snapshot"] = {
                "path": self.snapshot_path(),
                "captured_at": snapshot.get("captured_at"),
                "count": snapshot.get("count"),
                "surface": R.bundle_surface(snapshot),
                "instance": snapshot.get("instance"),
            }

    @staticmethod
    def _live_surface(live_bundle: dict | None, live_error: ZeneControlError | None) -> dict:
        """The live surface's fingerprint, or why it could not be read."""
        if live_bundle is not None:
            return live_bundle["surface"]
        error = live_error or ZeneControlError(ErrorKind.BRIDGE_ERROR,
                                               "live command list unreadable")
        return {"read": False, "error": error.to_payload(command="control.commands_list")["error"]}

    def _status_summary(self, payload: dict) -> str:
        """One line: reachability, the served list, the live surface, and staleness."""
        drift = payload.get("offline_drift") or {}
        live = payload.get("live_surface") or {}
        parts = [f"zene-control: reachable={payload['reachable']}",
                 f"engine_ready={payload['engine_ready']}",
                 f"commands={payload['command_list']['count']} "
                 f"({payload['command_list']['source']})"]
        if "id_count" in live:
            parts.append(f"live_surface={live['id_count']} ids/{live['group_count']} groups")
        if drift.get("stale"):
            behind = drift["copies"][0]["missing_count"] if drift.get("copies") else 0
            parts.append(f"OFFLINE STALE: {drift['stale_copies']} behind by {behind} id(s)")
        parts.append(f"socket={self.config.socket_path}")
        return " ".join(parts)

    def status_payload(self, arguments: dict | None = None) -> dict:
        """`zene_status`: the diagnostic, assembled in four separable steps."""
        args = dict(arguments or {})
        budget = self.config.timeout_for("zene_status", args.pop("timeout_s", None))
        payload = self._status_base()
        self._status_probe(payload, budget)
        self._status_surfaces(payload, budget)
        payload["verdict"] = self._status_verdict(payload)
        payload["summary"] = self._status_summary(payload)
        return payload

    def _status_verdict(self, payload: dict) -> str:
        """What the reachability and staleness facts add up to, in one sentence."""
        if payload["reachable"] and payload["engine_ready"]:
            verdict = "instance reachable and engine ready: engine commands are safe to call"
        elif payload["reachable"]:
            verdict = ("instance reachable but engine_ready is false: the bridge will poll "
                       "control.ping before sending engine commands")
        else:
            verdict = ("no usable instance; the bridge still serves the cached/snapshot "
                       "command list (zene_commands)")
        drift = payload.get("offline_drift") or {}
        if drift.get("stale"):
            verdict += ("; the offline command list is STALE against this instance's surface "
                        "(see offline_drift) — regenerate it with: "
                        + SNAPSHOT_FIX.split("#")[0].strip())
        return verdict

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
        "instance exists. Reports honestly where the list came from, the SURFACE the copy "
        "describe (id count, group count, id hash) and, whenever an instance is answering, "
        "an `offline_drift` block measuring every offline copy against it — a stale copy is "
        "named, with the ids it is missing.",
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
        "ready, the bridge's budgets, the live surface beside the offline copies, and the "
        "typed error when there is no instance. Always answers (a diagnostic), never raises "
        "for a missing instance. When an instance is up it also reports `offline_drift`: "
        "whether the cached/snapshot list an agent would be served with nothing running is "
        "STALE against that instance, and which ids it is missing.",
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
