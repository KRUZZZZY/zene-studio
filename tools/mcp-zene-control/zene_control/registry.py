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

PROVENANCE IS NOT ENOUGH ON ITS OWN, so every bundle also records the *surface*
it describes — the id count, the group count and a hash of the sorted id list
(:func:`surface_fingerprint`) — and :func:`surface_drift` compares two surfaces
and names the ids that differ. An offline copy whose recorded surface disagrees
with a live instance is stale *in a way that can be pointed at*: the bridge
reports it (`zene_status`, `zene_commands`) instead of quietly serving a shorter
list. The recurring defect this guards against is a derived artefact that
cannot fail: a stamp alone cannot be checked, a fingerprint can.
"""
from __future__ import annotations

import copy
import hashlib
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

#: How many ids a drift report names before it stops listing and starts
#: counting. A 100-id gap is reported in full up to this many entries plus a
#: count, so the reply stays readable and nothing is silently dropped: the
#: counts are always exact.
DRIFT_SAMPLE = 20

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

def command_ids(commands: list[dict] | None) -> list[str]:
    """The non-empty ids of a command list, sorted and deduplicated."""
    return sorted({str(entry.get("id") or "") for entry in (commands or [])} - {""})


def group_of(command_id: str) -> str:
    """The group a command id belongs to (`mixer.set_volume` -> `mixer`)."""
    return command_id.split(".", 1)[0]


def surface_fingerprint(commands: list[dict] | None) -> dict:
    """What surface a command list describes, in three checkable numbers.

    `id_count` and `group_count` are the figures a reader scans; `ids_sha256` is
    the one a *check* can use, because it moves when any id is added, removed or
    renamed and cannot be produced from a stale list. `\n` is the separator so
    the digest is over a canonical form: sorted ids, one per line, no trailing
    newline.
    """
    ids = command_ids(commands)
    return {
        "id_count": len(ids),
        "group_count": len({group_of(item) for item in ids}),
        "ids_sha256": hashlib.sha256("\n".join(ids).encode("utf-8")).hexdigest(),
    }


def surface_drift(live_commands: list[dict] | None,
                  offline_commands: list[dict] | None,
                  limit: int = DRIFT_SAMPLE) -> dict:
    """How an offline copy's surface differs from a live instance's.

    `stale` is the flag this exists for: True when the two lists do not register
    the same ids, false when they do. `missing_ids` are ids the live instance
    registers and the copy does not offer (the coverage gap an agent cannot see
    through the copy); `extra_ids` are ids the copy offers that no live instance
    registers (a removed or renamed feature the copy still advertises). Both are
    capped at `limit` entries with the exact remainder in `*_truncated`, so a
    100-id gap is readable and still counted to the id.
    """
    live = set(command_ids(live_commands))
    offline = set(command_ids(offline_commands))
    missing = sorted(live - offline)
    extra = sorted(offline - live)
    drift = {
        "stale": bool(missing or extra),
        "live_count": len(live),
        "offline_count": len(offline),
        "missing_count": len(missing),
        "extra_count": len(extra),
        "missing_ids": missing[:limit],
        "extra_ids": extra[:limit],
        "missing_truncated": max(0, len(missing) - limit),
        "extra_truncated": max(0, len(extra) - limit),
    }
    if drift["stale"]:
        drift["verdict"] = (
            f"the offline copy describes a different surface: {len(missing)} id(s) the live "
            f"instance registers are not in it, {len(extra)} id(s) it carries are not registered"
        )
    else:
        drift["verdict"] = (f"the offline copy and the live instance register the same "
                            f"{len(live)} id(s)")
    return drift


def bundle_surface(bundle: dict) -> dict:
    """The surface a bundle describes: what it recorded, else one derived from its ids.

    Bundles captured before 2026-09-15 carry no `surface` key. The fingerprint is a
    function of the ids, so deriving it from the copy's own command list is the same
    value a fresh capture would record — and it means an older copy is still
    comparable instead of being reported as surface-less.
    """
    recorded = bundle.get("surface")
    if isinstance(recorded, dict) and recorded.get("ids_sha256"):
        return recorded
    return surface_fingerprint(bundle.get("commands"))


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
        "surface": surface_fingerprint(commands),
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


# -- choosing between the offline copies -----------------------------------

def _ranking_key(bundle: dict) -> tuple:
    """How fresh one offline bundle is: (has a stamp, the stamp, its size).

    `captured_at` is written by :func:`bundle_from_result` with ``time.gmtime``
    as a fixed-width ``%Y-%m-%dT%H:%M:%SZ`` string, so two stamps compare
    chronologically as strings. A bundle with no usable stamp ranks below every
    stamped one rather than above it: an undated copy cannot be shown to be
    current, and the committed snapshot always carries one.
    """
    stamp = bundle.get("captured_at")
    dated = isinstance(stamp, str) and bool(stamp)
    commands = bundle.get("commands")
    return (dated, stamp if dated else "", len(commands) if isinstance(commands, list) else 0)


def rank_offline_bundles(candidates: list[tuple[str, str]]) -> list[tuple[str, str, dict]]:
    """The readable offline bundles, freshest first.

    ``candidates`` is an ordered list of ``(path, source_label)``. The order the
    caller gives is the tie-break, so equal stamps keep the caller's preference.

    WHY THIS EXISTS (and why the order the caller gives is not the answer on its
    own): the bridge used to serve whichever offline copy happened to exist, in
    the caller's order - cache first. A cache is written by the last *live* fetch,
    so a cache captured from an older instance shadowed the committed snapshot
    forever, with no comparison between them. Measured on this tree: a 70-id cache
    from 0.1.0-alpha made 74 command ids the current snapshot carries unreachable
    (tools/mcp-zene-control/README.md, COVERAGE-MATRIX-2026-09-13.md §4). Freshest
    wins makes regenerating the snapshot - the documented fix - actually take
    effect, and leaves a *newer* cache (a newer instance's list) in charge, which
    is correct.

    Returns [] when nothing is readable, so a caller can tell "no offline copy"
    from "an offline copy of unknown freshness".
    """
    readable: list[tuple[str, str, dict]] = []
    for path, source in candidates:
        bundle = load_bundle(path)
        if bundle is not None:
            readable.append((path, source, bundle))
    # `sorted` is stable, so equal ranking keys keep the caller's order.
    return sorted(readable, key=lambda entry: _ranking_key(entry[2]), reverse=True)


@dataclass
class CommandSource:
    """The command list the bridge is currently serving, and where it came from."""

    specs: list[CommandSpec]
    bundle: dict
    source: str                # "live" | "cache" | "snapshot" | "none"
    path: str | None = None
    error: ZeneControlError | None = None
    skipped: tuple[dict, ...] = ()

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
            passed_over = ""
            if self.skipped:
                passed_over = (f"; {len(self.skipped)} staler offline copy(ies) passed over "
                               f"({', '.join(str(item.get('source')) for item in self.skipped)})")
            return (f"{self.count} commands from the {self.source} "
                    f"(captured {self.bundle.get('captured_at')}){passed_over}{stale}")
        return "no command list available: no live instance and no cached or snapshot copy"

    def payload(self, **extra: Any) -> dict:
        out: dict = {
            "ok": True,
            "source": self.source,
            "count": self.count,
            "captured_at": self.bundle.get("captured_at"),
            "surface": bundle_surface(self.bundle),
            "instance": self.bundle.get("instance") or {},
            "proto": self.bundle.get("proto"),
            "commands": self.bundle.get("commands") or [],
            "summary": self.summary(),
        }
        if self.path:
            out["path"] = self.path
        if self.skipped:
            out["skipped_offline"] = list(self.skipped)
        if self.error is not None:
            out["instance_error"] = self.error.to_payload()["error"]
        out.update(extra)
        return out
