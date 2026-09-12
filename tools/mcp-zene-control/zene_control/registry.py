"""The command registry: tools are GENERATED from `control.commands_list`.

Nothing about the DAW's command surface is written down in this file. At call
time the bridge asks a live instance for `control.commands_list`, turns each
command's `id` + `args_schema` + `result_schema` into an MCP tool, and that is
what `tools/list` returns. A command added to the DAW appears here without an
edit; a command removed disappears; a schema change is reflected on the next
`tools/list`. The only bridge-owned additions are:

* the tool **name**, derived deterministically from the command id (below);
* one extra optional argument, `timeout_s`, added to every generated tool and
  stripped before the command is forwarded (documented in MCP-ZENE-CONTROL.md);
* prose in the description (id, group, mutating flag, long-operation marker).

When no instance is reachable the tool list is served from the last-known copy
(``<state-dir>/commands.json``, written on every successful live fetch) and,
if that is missing too, from the snapshot committed next to this module
(``commands_snapshot.json``, produced by ``snapshot_commands.py`` from a real
instance). Both carry their provenance so a caller can tell live from stale.
"""
from __future__ import annotations

import copy
import json
import os
import re
import time
from dataclasses import dataclass, field
from typing import Any

from .config import LONG_COMMANDS, ZeneControlError, ErrorKind, engine_free

#: Every generated tool is named ``zene_`` + the command id with ``.`` replaced
#: by ``_`` (so `mixer.set_volume` -> `zene_mixer_set_volume`). Any character
#: that is not safe in an MCP tool name becomes ``_`` and runs collapse.
TOOL_PREFIX = "zene_"

#: Bridge-owned tools that exist even with no instance connected. Kept out of
#: the generated namespace's way by an explicit uniqueness check.
BRIDGE_TOOL_NAMES = ("zene_commands", "zene_status")

BUNDLE_KIND = "zene-control-command-list"
BUNDLE_SCHEMA = 1

_UNSAFE = re.compile(r"[^0-9A-Za-z_]+")
_RUNS = re.compile(r"_{2,}")


def tool_name(command_id: str, prefix: str = TOOL_PREFIX) -> str:
    """Stable MCP tool name for a DAW command id.

    ``mixer.set_volume`` -> ``zene_mixer_set_volume``. Deterministic, so the
    same command id always maps to the same tool name, and injective for the
    ids the registry uses (verified by a unit test over the live list).
    """
    return prefix + _RUNS.sub("_", _UNSAFE.sub("_", command_id.replace(".", "_")))


@dataclass(frozen=True)
class CommandSpec:
    """One DAW command, exactly as the instance described it."""

    id: str
    group: str
    verb: str
    description: str
    requires: tuple[str, ...] = ()
    mutating: bool = False
    args_schema: dict = field(default_factory=dict)
    result_schema: dict = field(default_factory=dict)
    tool_name: str = ""

    @property
    def requires_engine(self) -> bool:
        """Derived from the group (the registry does not serialise the flag)."""
        return not engine_free(self.group)

    def to_dict(self) -> dict:
        return {
            "id": self.id, "group": self.group, "verb": self.verb,
            "description": self.description, "requires": list(self.requires),
            "mutating": self.mutating, "args_schema": self.args_schema,
            "result_schema": self.result_schema, "tool_name": self.tool_name,
        }


def spec_from_wire(entry: dict) -> CommandSpec:
    command_id = str(entry.get("id") or "")
    if not command_id:
        raise ZeneControlError(ErrorKind.BRIDGE_ERROR,
                               f"command entry without an id: {entry!r}")
    group, _, verb = command_id.partition(".")
    raw_args = entry.get("args_schema")
    raw_result = entry.get("result_schema")
    return CommandSpec(
        id=command_id,
        group=str(entry.get("group") or group),
        verb=str(entry.get("verb") or verb),
        description=str(entry.get("description") or ""),
        requires=tuple(entry.get("requires") or ()),
        mutating=bool(entry.get("mutating")),
        args_schema=raw_args if isinstance(raw_args, dict) else {},
        result_schema=raw_result if isinstance(raw_result, dict) else {},
        tool_name=tool_name(command_id),
    )


def specs_from_commands(commands: list[dict]) -> list[CommandSpec]:
    specs = [spec_from_wire(entry) for entry in commands]
    names: dict[str, str] = {name: "" for name in BRIDGE_TOOL_NAMES}
    for spec in specs:
        other = names.get(spec.tool_name)
        if other is not None:
            raise ZeneControlError(
                ErrorKind.BRIDGE_ERROR,
                f"command ids {other!r} and {spec.id!r} both map to MCP tool "
                f"name {spec.tool_name!r}; the bridge would have to drop one, so it refuses",
            )
        names[spec.tool_name] = spec.id
    return specs


# -- MCP tool construction -------------------------------------------------

def mcp_tool(spec: CommandSpec, timeout_s: float) -> dict:
    """The `tools/list` entry for one DAW command, as plain JSON-schema data.

    Deliberately declares no `outputSchema`: the bridge wraps the DAW's result
    in an envelope (`ok`, `summary`, `result`, `instance`, `elapsed_s`), so the
    DAW's *result* schema is carried in `meta` for information instead of being
    asserted where it would not validate.
    """
    schema = copy.deepcopy(spec.args_schema) if spec.args_schema else {}
    schema.setdefault("type", "object")
    properties = schema.get("properties")
    if not isinstance(properties, dict):
        properties = {}
        schema["properties"] = properties
    # Bridge-owned knob, stripped before forwarding. Never required.
    properties["timeout_s"] = {
        "type": "number",
        "minimum": 0.1,
        "maximum": 3600,
        "description": "Bridge-side budget in seconds for this call (default "
                       f"{timeout_s:g}s for this command). Bounded; never a hang.",
    }
    schema.setdefault("additionalProperties", False)

    notes = [f"Zene Studio command `{spec.id}` (group `{spec.group}`), generated from the "
             "control.commands_list of a running instance — this tool disappears if the DAW "
             "stops registering it."]
    if spec.requires_engine:
        notes.append("Requires the engine: the bridge polls control.ping until engine_ready is true "
                     "before sending anything (a headless instance needs the Dummy-audio recipe).")
    else:
        notes.append("Engine-free: answers before the engine is ready (control group).")
    if spec.mutating:
        notes.append("Mutating: the instance records a transaction that control.undo can reverse.")
    if spec.id in LONG_COMMANDS:
        notes.append(f"LONG OPERATION — {LONG_COMMANDS[spec.id]}; it does not block other calls "
                     "(one connection per call) and is bounded by timeout_s.")
    if spec.requires:
        notes.append("Declared requires: " + ", ".join(spec.requires) + ".")
    description = (spec.description or "(no description)") + "\n\n" + " ".join(notes)
    return {
        "name": spec.tool_name,
        "description": description,
        "inputSchema": schema,
        "annotations": {
            "read_only_hint": not spec.mutating,
            "destructive_hint": bool(spec.mutating),
            "idempotent_hint": not spec.mutating,
            "open_world_hint": False,
            "title": spec.id,
        },
        "meta": {
            "zene/command": spec.id,
            "zene/group": spec.group,
            "zene/mutating": spec.mutating,
            "zene/requires_engine": spec.requires_engine,
            "zene/result_schema": spec.result_schema,
            "zene/timeout_default_s": timeout_s,
        },
    }


# -- command-list bundles (live / cache / snapshot) ------------------------

def bundle_from_result(result: dict, provenance: dict) -> dict:
    commands = result.get("commands")
    if not isinstance(commands, list):
        raise ZeneControlError(ErrorKind.BRIDGE_ERROR,
                               "control.commands_list returned no 'commands' array")
    return {
        "kind": BUNDLE_KIND,
        "schema": BUNDLE_SCHEMA,
        "captured_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "proto": result.get("proto"),
        "count": len(commands),
        "instance": provenance,
        "commands": commands,
    }


def load_bundle(path: str) -> dict | None:
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return None
    if not isinstance(data, dict) or data.get("kind") != BUNDLE_KIND:
        return None
    if not isinstance(data.get("commands"), list):
        return None
    return data


def save_bundle(path: str, bundle: dict) -> str | None:
    """Best-effort cache write; returns the path or None if it could not be written."""
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        tmp = f"{path}.tmp{os.getpid()}"
        with open(tmp, "w", encoding="utf-8") as handle:
            json.dump(bundle, handle, indent=2, sort_keys=True)
        os.replace(tmp, path)
        return path
    except OSError:
        return None


@dataclass
class CommandSource:
    """The command list the bridge is currently serving, and where it came from."""

    specs: list[CommandSpec]
    bundle: dict
    source: str                # "live" | "cache" | "snapshot" | "none"
    path: str | None = None
    error: ZeneControlError | None = None

    @property
    def count(self) -> int:
        return len(self.specs)

    def summary(self) -> str:
        if self.source == "live":
            instance = self.bundle.get("instance") or {}
            return (f"{self.count} commands from the live instance "
                    f"(version {instance.get('version')}, proto {self.bundle.get('proto')})")
        if self.source in ("cache", "snapshot"):
            stale = ""
            if self.error is not None:
                stale = f" — no live instance ({self.error.kind}: {self.error.message})"
            return (f"{self.count} commands from the {self.source} "
                    f"(captured {self.bundle.get('captured_at')}){stale}")
        return "no command list available: no live instance and no cached or snapshot copy"

    def payload(self, **extra: Any) -> dict:
        out: dict = {
            "ok": True,
            "source": self.source,
            "count": self.count,
            "captured_at": self.bundle.get("captured_at"),
            "instance": self.bundle.get("instance") or {},
            "proto": self.bundle.get("proto"),
            "commands": self.bundle.get("commands") or [],
            "summary": self.summary(),
        }
        if self.path:
            out["path"] = self.path
        if self.error is not None:
            out["instance_error"] = self.error.to_payload()["error"]
        out.update(extra)
        return out
