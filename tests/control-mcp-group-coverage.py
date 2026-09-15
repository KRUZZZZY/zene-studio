#!/usr/bin/env python3
"""One command per command group, driven through a REAL MCP stdio session.

THE CLAIM UNDER TEST: every command group the running instance registers is
reachable as an MCP tool and can be DRIVEN through it — not merely listed. The
gap this was written for (feature-list row 49): ten groups (`browser`, `comp`,
`export`, `link`, `modulator`, `rack`, `session`, `telemetry`, `warp`, `wasm`)
had no MCP tool at all and 74 ids were invisible, because the registered bridge
served a stale 70-id 0.1.0-alpha list while the tree's snapshot carried 144.

WHY THE MCP LAYER AND NOT THE BINARY. The bridge generates its tool list from a
live instance's `control.commands_list` (`zene_control/registry.py`), so the
mechanism needs no per-feature bridge work — but a mechanism nobody drives is a
claim. This starts the real binary (the shared `control_socket_harness`, the
headless recipe every control transcript here uses), opens a real MCP stdio
session to `tools/mcp-zene-control/server.py` over that instance's socket, and:

  1. `tools/list` — every id the instance registers has a generated tool, and no
     generated tool names an id the instance does NOT register (both directions,
     so a stale tool list fails here instead of passing quietly);
  2. `zene_commands` twice, `source=snapshot` then `source=live` — the BEFORE and
     AFTER id/group counts an agent sees with and without an instance, each one
     produced by a recorded MCP call rather than by this script reading files;
  3. `zene_status` — the stale-offline-list flag must equal the truth this script
     computes for itself (fired exactly when the live and offline id sets
     differ). That is the self-checking half: a staleness report that can be
     wrong in either direction is one that can fail, which is what the offline
     copy never had;
  4. one `tools/call` per target group, with the arguments the command's own
     schema requires, filled from the instance's own state through further MCP
     calls (`track.list`/`track.add`, `mixer.get_state`/`mixer.add_channel`,
     `arrangement.get_state`/`clip.add`), so a group whose read verb needs a real
     track, channel or clip id is driven against real state instead of skipped.
     Each reply's `command` must be the command that was intended: the proof the
     call was forwarded under the right id, not answered from a bridge-side table.

A GROUP THIS BUILD DOES NOT COMPILE IN. There is one: `wasm.*` needs
`WANT_WASM=ON` plus the wasmtime C API, and every build on this machine degrades
it to OFF (`Wasmtime_LIBRARY-NOTFOUND`), so its ids exist in the registry sources
and in no binary. `--compiled-out <prefix>` declares a group this configuration
lacks, checked in BOTH directions: a prefix declared compiled out that the
instance DOES register is a failure (the flag would hide real coverage), and a
target group that is neither registered nor declared is a failure too.
tests/CMakeLists.txt passes `--compiled-out wasm.` exactly when the sandbox is
off; a wasm-enabled build gets no flag and must drive the group for real. The
bridge's own half of that case — a declared group becomes tools and forwards
verbatim — is tools/mcp-zene-control/tests/test_declared_surface.py.

SKIP, NOT PASS. Exit 77 (ctest "Skipped") when no interpreter with the `mcp`
distribution can be found: the bridge's stdio server is the thing under test, so
without it this check cannot look at anything and must not report Passed.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-mcp-group-coverage.py <zene-binary> \\
        [--group PREFIX]... [--compiled-out PREFIX]... [--python INTERPRETER] \\
        [--json TRANSCRIPT.json]

Exit codes: 0 every target group covered and driven; 1 a gap; 2 cannot run (no
binary); 77 skipped (no python with the mcp distribution).
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent

sys.path.insert(0, str(HERE))

import control_socket_harness as H  # noqa: E402
from mcp_stdio_session import (BRIDGE_DIR, SKIP_CODE, BridgeSession,  # noqa: E402
                               protocol_version, server_python)

#: The bridge's own naming rule is the single source of truth for tool names, so
#: this check does not re-implement it; a bridge package that cannot be imported
#: is a SKIP in main(), as in tests/control-commands-snapshot.py.
sys.path.insert(0, str(BRIDGE_DIR))
try:
    from zene_control import registry as R  # noqa: E402
except ImportError:  # pragma: no cover - reached only when the SKIP in main() fires
    R = None  # type: ignore[assignment]


def tool_of(command_id: str) -> str:
    return R.tool_name(command_id)


#: The groups row 49 measured as invisible. A group is a prefix of a command id.
DEFAULT_GROUPS = ("browser", "comp", "export", "link", "modulator", "rack", "session",
                  "telemetry", "warp", "wasm")


def parse_args(argv):
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("binary", help="the built zene binary ($<TARGET_FILE:zene>)")
    parser.add_argument("--group", action="append", default=[], metavar="PREFIX",
                        help="a command group this run must cover (repeatable; default: the "
                             "ten row 49 named)")
    parser.add_argument("--compiled-out", action="append", default=[], metavar="PREFIX",
                        help="a command group this configuration does not compile in "
                             "(repeatable; wasm. for a build without the wasmtime C API); "
                             "checked in both directions")
    parser.add_argument("--python", default=None,
                        help="interpreter that can import the `mcp` distribution (the bridge's "
                             "stdio server): default ZENE_CONTROL_PYTHON, then this "
                             "interpreter, then this machine's Hermes venv")
    parser.add_argument("--json", default=None, dest="json_path",
                        help="write the full request/reply transcript to this path")
    return parser.parse_args(argv[1:])


def command_ids(commands: list[dict]) -> list[str]:
    """Every non-empty id in a command list, sorted and deduplicated."""
    return sorted({str(entry.get("id") or "") for entry in commands} - {""})


def group_of(command_id: str) -> str:
    """The group a command id belongs to: `mixer.set_volume` -> `mixer`."""
    return command_id.split(".", 1)[0]


def required_args(command: dict) -> list[str]:
    """The names the command's own args schema marks required."""
    return list((command.get("args_schema") or {}).get("required") or [])


def result_of(reply: dict) -> dict:
    """The DAW's own result: the bridge wraps it under `result` in its envelope."""
    return (reply["payload"] or {}).get("result") or {}


def ids_of(node) -> list[str]:
    """The ids in one state node, whichever shape the DAW used for it.

    The state commands address their objects three ways: a map keyed by id, a list
    of objects each carrying its id, or a plain list of ids. A caller that wants
    one addressable id does not care which.
    """
    if isinstance(node, dict):
        return [str(key) for key in node]
    ids: list[str] = []
    for item in (node or []):
        if isinstance(item, dict):
            for key in ("id", "clip", "track", "channel"):
                if isinstance(item.get(key), str):
                    ids.append(item[key])
                    break
        elif isinstance(item, (str, int)):
            ids.append(str(item))
    return ids


def candidates(commands: list[dict], group: str) -> list[dict]:
    """The group's commands, most drivable first: read-only, then fewest args."""
    rows = [entry for entry in commands if group_of(str(entry.get("id"))) == group]
    return sorted(rows, key=lambda entry: (bool(entry.get("mutating")),
                                           len(required_args(entry)), str(entry.get("id"))))


class Proof:
    """The state a run builds, and the arguments each group's commands need."""

    def __init__(self, session: BridgeSession):
        self.session = session
        self.setup: list[dict] = []

    def call(self, tool: str, arguments: dict | None = None, phase: str = "drive") -> dict:
        reply = self.session.call_tool(tool, arguments)
        reply["phase"] = phase
        if phase == "setup":
            self.setup.append(reply)
        return reply

    def _ids(self, tool: str, key: str) -> list[str]:
        return ids_of(result_of(self.call(tool, {}, phase="setup")).get(key))

    def ensure_track(self) -> str:
        known = self._ids("zene_track_list", "tracks")
        if known:
            return known[0]
        return str(result_of(self.call("zene_track_add", {}, phase="setup")).get("track"))

    def ensure_channel(self) -> str:
        known = [item for item in self._ids("zene_mixer_get_state", "channels") if item != "ch-0"]
        if known:
            return known[0]
        return str(result_of(self.call("zene_mixer_add_channel", {},
                                       phase="setup")).get("channel"))

    def ensure_clip(self) -> str:
        """A clip on a SAMPLE track: warp.* pins audio positions, and the DAW refuses
        an instrument clip with `refused` rather than answering."""
        track = self._sample_track()
        known = self._clip_on(track)
        if known:
            return known
        return str(result_of(self.call("zene_clip_add", {"track": track, "position": 0},
                                       phase="setup")).get("clip"))

    def _sample_track(self) -> str:
        """A sample track's id, added to the project when it has none."""
        tracks = result_of(self.call("zene_track_list", {}, phase="setup")).get("tracks") or []
        found = [str(item["id"]) for item in tracks
                 if isinstance(item, dict) and item.get("type") == "sample"]
        if found:
            return found[0]
        return str(result_of(self.call("zene_track_add", {"type": "sample"},
                                       phase="setup")).get("track"))

    def _clip_on(self, track: str) -> str | None:
        """One clip id on that track, or None when it carries none."""
        state = result_of(self.call("zene_arrangement_get_state", {}, phase="setup"))
        for entry in (state.get("tracks") or []):
            if isinstance(entry, dict) and str(entry.get("id")) == track:
                return next(iter(ids_of(entry.get("clips"))), None)
        return None

    def arguments_for(self, command: dict) -> dict:
        """The schema's required arguments, from the instance's own state."""
        providers = {"track": self.ensure_track, "channel": self.ensure_channel,
                     "clip": self.ensure_clip, "key": lambda: 60, "velocity": lambda: 100,
                     "position": lambda: 0, "offset_ticks": lambda: 0, "source_frame": lambda: 0}
        args: dict = {}
        for name in required_args(command):
            if name not in providers:
                raise KeyError("no value for required argument %r" % name)
            args[name] = providers[name]()
        return args


def drive_group(proof: Proof, commands: list[dict], group: str, attempts: int = 3) -> dict:
    """Drive one command of `group` through the MCP layer; report what happened."""
    made = []
    for command in candidates(commands, group)[:attempts]:
        command_id = str(command["id"])
        try:
            args = proof.arguments_for(command)
        except KeyError as exc:
            made.append({"command": command_id, "not_attempted": str(exc)})
            continue
        reply = proof.call(tool_of(command_id), args)
        made.append({"tool": reply["tool"], "arguments": args, "is_error": reply["is_error"],
                     "forwarded_command": reply["payload"].get("command"),
                     "summary": reply["summary"], "elapsed_s": reply["elapsed_s"]})
        if not reply["is_error"] and reply["payload"].get("command") == command_id:
            return {"group": group, "command": command_id, "ok": True, "attempts": made,
                    "reply": reply}
    return {"group": group, "command": None, "ok": False, "attempts": made}


def tool_gap_problems(live_ids: list[str], tool_names: set[str]) -> list[str]:
    """Both directions: no live id without a tool, no tool without a live id."""
    expected = {tool_of(command_id): command_id for command_id in live_ids}
    problems = ["the bridge exposes no tool for %r (%s)" % (command_id, tool)
                for tool, command_id in sorted(expected.items()) if tool not in tool_names]
    problems += ["the bridge exposes %r, which this instance does not register" % name
                 for name in sorted(tool_names - set(expected) - {"zene_commands", "zene_status"})]
    return problems


def flag_problems(live_ids: list[str], declared: set[str]) -> list[str]:
    """Each `--compiled-out` flag must describe a group this build really lacks."""
    problems = []
    for prefix in sorted(declared):
        present = [item for item in live_ids if group_of(item) == prefix]
        if present:
            problems.append("--compiled-out %s. was declared, but the instance registers %d of "
                            "its ids (e.g. %s): the flag is wrong for this build and would hide "
                            "real coverage" % (prefix, len(present), ", ".join(present[:3])))
    return problems


def target_group_problems(live_ids: list[str], declared: set[str],
                          targets: list[str]) -> list[str]:
    """Every target group must be registered here, or declared compiled out."""
    return ["group %r registers no id in this configuration and no --compiled-out flag "
            "declares it" % group for group in targets
            if group not in declared and not [item for item in live_ids if group_of(item) == group]]


def coverage_problems(live_ids: list[str], tool_names: set[str], declared: set[str],
                      targets: list[str]) -> list[str]:
    """Everything the tool list must satisfy, gathered from the three checks above."""
    return (tool_gap_problems(live_ids, tool_names) + flag_problems(live_ids, declared)
            + target_group_problems(live_ids, declared, targets))


def print_counts(label: str, ids: list[str]) -> None:
    """One line for the BEFORE/AFTER surfaces: ids, groups, and the group names."""
    print("%-17s %3d id(s) across %2d group(s): %s"
          % (label, len(ids), len({group_of(item) for item in ids}),
             ", ".join(sorted({group_of(item) for item in ids}))))


def environment_report(options, python: str, protocol: str, targets: list[str]) -> None:
    """The configuration this run used, before anything is measured."""
    print("binary            %s" % os.path.abspath(options.binary))
    print("bridge            %s" % (BRIDGE_DIR / "server.py"))
    print("server python     %s (MCP protocol %s)" % (python, protocol))
    print("target groups     %s%s" % (", ".join(targets),
                                      "" if not options.compiled_out else
                                      "  (compiled out: %s)" % ", ".join(options.compiled_out)))


def session_report(info: dict, instance, live: list[str], tools: list[dict],
                   before: dict) -> None:
    """The mcp session, and the two surfaces an agent sees with/without an instance."""
    print("\nmcp session       %s %s over %s"
          % (info.get("serverInfo", {}).get("name"), info.get("serverInfo", {}).get("version"),
             instance.socket_path))
    print_counts("BEFORE (offline)", command_ids(before["payload"].get("commands") or []))
    print_counts("AFTER (live)", live)
    print("%-17s %3d tool(s) = %d generated + 2 bridge-owned"
          % ("tools/list", len(tools), len(tools) - 2))


def staleness_problems(status: dict, before: dict, live: list[str]) -> list[str]:
    """The flag must equal the truth this script computes for itself."""
    drift = status["payload"].get("offline_drift") or {}
    snapshot = next((copy for copy in drift.get("copies") or []
                     if copy.get("source") == "snapshot"), None)
    if snapshot is None:
        return ["zene_status reported no offline copy to compare against, so the staleness flag "
                "cannot be checked"]
    truth = set(live) != set(command_ids(before["payload"].get("commands") or []))
    print("stale flag        snapshot says stale=%s, truth is %s (%d offline vs %d live)"
          % (snapshot["stale"], truth, snapshot["offline_count"], snapshot["live_count"]))
    if bool(snapshot["stale"]) == truth:
        return []
    return ["the staleness flag said %s while the surfaces %s: it cannot be trusted in either "
            "direction" % (snapshot["stale"], "differ" if truth else "match")]


def drive_or_excuse(proof: Proof, commands: list[dict], live: list[str], group: str,
                    declared: set[str]) -> dict:
    """One target group: driven through MCP, or excused by a flag this run declared."""
    if not [item for item in live if group_of(item) == group]:
        excused = group in declared
        print("%-12s %s" % (group, "EXCUSED: this build compiles no %s.* id (declared with "
                                   "--compiled-out; a wasm-enabled build must drive it)" % group
                            if excused else
                            "MISSING: no %s.* id and no --compiled-out flag" % group))
        return {"group": group, "command": None, "ok": excused, "excused": excused}
    result = drive_group(proof, commands, group)
    if result["ok"]:
        print("%-12s %-26s ok in %.2fs  %s"
              % (group, result["command"], result["reply"]["elapsed_s"],
                 result["reply"]["summary"][:80]))
    else:
        print("%-12s FAIL: no candidate could be driven; %s"
              % (group, json.dumps(result["attempts"])[:280]))
    return result


def drive_targets(proof: Proof, commands: list[dict], live: list[str], targets: list[str],
                  declared: set[str]) -> tuple[list[dict], list[str]]:
    """Every target group through MCP, plus a problem per group that did not answer."""
    print("\none command per target group, through MCP:")
    results = [drive_or_excuse(proof, commands, live, group, declared) for group in targets]
    driven = [result for result in results if result.get("ok") and result.get("command")]
    registered = [result for result in results if not result.get("excused")]
    if len(driven) == len(registered):
        return results, []
    return results, ["only %d of %d target group(s) that this configuration registers could be "
                     "driven" % (len(driven), len(registered))]


def measure(session, instance, commands: list[dict], live: list[str], targets: list[str],
            declared: set[str], options) -> tuple[list[dict], list[str], dict]:
    """Every MCP exchange this run makes, and every finding it produced."""
    info = session.initialize()
    tools = session.tools_list()
    proof = Proof(session)
    before = session.call_tool("zene_commands", {"source": "snapshot", "include_schemas": False})
    after = session.call_tool("zene_commands", {"source": "live", "include_schemas": True})
    status = session.call_tool("zene_status", {})
    session_report(info, instance, live, tools, before)
    problems = coverage_problems(live, {entry["name"] for entry in tools}, declared, targets)
    problems += staleness_problems(status, before, live)
    results, drive_problems = drive_targets(proof, commands, live, targets, declared)
    record = {"binary": os.path.abspath(options.binary), "targets": targets,
              "compiled_out": options.compiled_out, "live_ids": live, "info": info,
              "tools_list_count": len(tools), "before": before, "after": after, "status": status,
              "groups": results, "setup_calls": proof.setup, "exchanges": session.exchanges}
    return results, problems + drive_problems, record


def write_transcript(path: str, record: dict) -> None:
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(record, handle, indent=2, default=str)
    print("\ntranscript        %s" % path)


def target_groups(options) -> list[str]:
    """The groups this run requires: the `--group` flags, else row 49's ten."""
    if options.group:
        return list(options.group)
    return list(DEFAULT_GROUPS)


def declared_prefixes(options) -> set[str]:
    """The `--compiled-out` flags, without their trailing dot."""
    return {item.rstrip(".") for item in options.compiled_out}


def live_commands(client) -> tuple[list[dict], list[str]]:
    """The instance's own command list, read without the bridge (a second source)."""
    entries = H.ok_result(client.call(1, "control.commands_list"), 1).get("commands")
    if not isinstance(entries, list):
        return [], []
    return entries, command_ids(entries)


def run_and_write(session, instance, commands: list[dict], live: list[str], targets: list[str],
                  options) -> tuple[list[dict], list[str]]:
    """Measure the bridge, then write the transcript when the caller asked for one."""
    results, problems, record = measure(session, instance, commands, live, targets,
                                        declared_prefixes(options), options)
    if options.json_path:
        write_transcript(options.json_path, record)
    return results, problems


def pass_line(live: list[str], results: list[dict]) -> str:
    """The one-line success summary: both surfaces, and the groups driven."""
    driven = [str(result["command"]) for result in results
              if result.get("ok") and result.get("command")]
    excused = [result["group"] for result in results if result.get("excused")]
    return ("PASS: %d id(s) across %d group(s) have an MCP tool; %d of %d registered target "
            "group(s) driven end to end through MCP (%s); excused by configuration: %s"
            % (len(live), len({group_of(item) for item in live}), len(driven),
               len(results) - len(excused), ", ".join(driven), ", ".join(excused) or "none"))


def verdict(problems: list[str], live: list[str], results: list[dict]) -> int:
    """Print the findings, then PASS or FAIL — which is this process's exit code."""
    print("")
    for problem in problems:
        print("  - %s" % problem)
    if problems:
        print("FAIL: %d finding(s); the bridge does not cover this instance's surface"
              % len(problems))
        return 1
    print(pass_line(live, results))
    return 0


def main(argv) -> int:
    options = parse_args(argv)
    if not os.path.exists(options.binary):
        print("cannot run: no binary at %s" % options.binary)
        return 2
    if R is None:
        print("SKIP: the bridge package at %s is not importable" % BRIDGE_DIR)
        return SKIP_CODE
    python = server_python(options.python)
    if python is None:
        return SKIP_CODE
    targets = target_groups(options)
    protocol = protocol_version(python)
    environment_report(options, python, protocol, targets)
    state_dir = tempfile.mkdtemp(prefix="zene-mcp-coverage-", dir="/tmp")

    with H.start_instance(options.binary) as instance:
        client = H.connect(instance)
        H.wait_ready(instance, client, None)
        commands, live = live_commands(client)
        session = BridgeSession(python, instance.socket_path, state_dir, protocol)
        try:
            results, problems = run_and_write(session, instance, commands, live, targets, options)
        except (H.Timeout, TimeoutError, RuntimeError) as error:
            print("FAIL: %s" % error)
            print("\n".join(session.stderr))
            return 1
        finally:
            if session.stderr:
                H.dump("bridge server stderr", "\n".join(session.stderr))
            session.close()
            client.close()
    return verdict(problems, live, results)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
