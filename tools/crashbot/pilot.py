#!/usr/bin/env python3
"""Crashbot pilot driver (T3) - N passes over the pack set, one declared shape, one ledger.

The pilot is the T3 deliverable's engine: it runs `runner.py` once per pass over the whole
scenario directory (each pass its own run directory and its own ledger), stops on a declared
bound, and aggregates what the passes produced - case outcomes, distinct-command coverage,
captures, and the hygiene proof per pass. It launches nothing itself: `runner.py` owns the
instances and reaps them; this file only starts runner processes and reads what they filed.

Bounds, all mandatory and all recorded in the pilot ledger:
  * instances per pass <= 4 (the pool's own measured ceiling);
  * cases: stop at `--target-cases` (default 300);
  * wall clock: stop at `--wall-budget-s` (default 4500 s = 75 min);
  * disk: stop if free space on the run root drops under `--min-free-gb` (default 20);
  * every pass carries `--case-cap-s` and `--run-cap-s` so a stuck case cannot wander.

Hygiene per pass is a VERIFICATION, not an assertion: every pid in every wave ledger is
looked up in /proc, every harness world is checked for existence, and `our_pids_alive_after`
from each wave's own reaping record is collected. A pass with a live pid or a world left
behind stops the pilot - a crash hunt that leaks instances is not evidence.

Usage:
  python3 tools/crashbot/pilot.py --scenarios tools/crashbot/scenarios \\
      --binary /tmp/zene-pilot/zene --run-root /tmp/zene-pilot/pilot \\
      --instances 4 --target-cases 300 --wall-budget-s 4500
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import pool as P          # noqa: E402  (utc_now, sha256_of, orphan_check: the same facts)

RUNNER = os.path.join(HERE, "runner.py")
MAX_INSTANCES = 4


def read_json(path):
    try:
        with open(path) as handle:
            data = json.load(handle)
    except (OSError, ValueError) as error:
        return {"_read_error": str(error)}
    return data if isinstance(data, dict) else {"_value": data}


def cases_summary(ledger):
    """The pass ledger's cases, reduced to what the pilot aggregates."""
    rows = []
    for case in ledger.get("cases") or []:
        rows.append({"case": case.get("case"), "instance": case.get("instance"),
                     "terminal": case.get("terminal"), "counts": case.get("counts"),
                     "seed": case.get("seed"), "pid": case.get("pid"),
                     "surface_ids": case.get("surface_ids")})
    return rows


def _instance_evidence(wave, instance, reaping):
    """One instance row plus what the LIVE box says about its pid and its world."""
    pid, world = instance.get("pid"), instance.get("world")
    alive = bool(pid) and os.path.exists("/proc/%d" % pid)
    present = bool(world) and os.path.exists(world)
    reap_row = ((reaping or {}).get("instances") or {}).get(instance.get("name")) or {}
    row = {"name": instance.get("name"), "pid": pid, "alive": alive, "world_present": present,
           "quit_ok": reap_row.get("quit_ok"), "exit_code": reap_row.get("exit_code"),
           "hard_kill": reap_row.get("hard_kill")}
    findings = {"pids_alive": {"wave": wave, "pid": pid} if alive else None,
                "worlds_present": {"wave": wave, "world": world} if present else None,
                "hard_kills": {"wave": wave, "pid": pid} if reap_row.get("hard_kill") else None}
    return row, findings


def _wave_evidence(name, ledger):
    """One wave ledger reduced to its rows and the findings its own reaping record holds."""
    reaping = ledger.get("reaping") or {}
    rows, findings = [], {}
    for instance in ledger.get("instances") or []:
        row, per_instance = _instance_evidence(name, instance, reaping)
        rows.append(row)
        for key, item in per_instance.items():
            if item:
                findings.setdefault(key, []).append(item)
    return {"wave": ledger.get("run_id"), "instances": rows,
            "our_pids_alive_after": reaping.get("our_pids_alive_after")}, findings


def hygiene_of(pass_dir):
    """Every pid and world of every wave of this pass, checked against the live box."""
    evidence = {"waves": [], "pids_checked": 0, "pids_alive": [], "worlds_present": [],
                "our_pids_alive_after": [], "hard_kills": [], "artifacts": []}
    waves = os.path.join(pass_dir, "waves")
    for name in sorted(os.listdir(waves)) if os.path.isdir(waves) else []:
        ledger = read_json(os.path.join(waves, name, "run.json"))
        row, findings = _wave_evidence(name, ledger)
        evidence["waves"].append(row)
        evidence["pids_checked"] += len(row["instances"])
        for key, items in findings.items():
            evidence[key].extend(items)
        evidence["artifacts"].extend(dict(item, wave=name) for item in ledger.get("artifacts") or []
                                     if isinstance(item, dict))
    return evidence


def stop_reason(state, args):
    """The declared bounds, checked in one place: None means 'run another pass'."""
    if state["cases"] >= args.target_cases:
        return "target cases reached (%d)" % state["cases"]
    if state["wall_s"] >= args.wall_budget_s:
        return "wall budget reached (%.0fs)" % state["wall_s"]
    free_gb = shutil.disk_usage(args.run_root).free / float(1 << 30)
    if free_gb < args.min_free_gb:
        return "free disk %.1f GB below the declared floor of %.1f GB" % (free_gb, args.min_free_gb)
    if state["live_pids"]:
        return "a pass leaked %d instance pid(s)" % len(state["live_pids"])
    return None


def run_one_pass(index, args, state, journal):
    pass_dir = os.path.join(args.run_root, "pass-%03d" % index)
    argv = [sys.executable, RUNNER, "--scenarios"] + args.scenarios + [
        "--instances", str(args.instances), "--binary", args.binary,
        "--run-dir", pass_dir, "--case-cap-s", str(args.case_cap_s),
        "--run-cap-s", str(args.run_cap_s)]
    started = time.time()
    print("--- pass %d: %s" % (index, " ".join(argv)), flush=True)
    log_path = os.path.join(pass_dir, "runner.log")
    os.makedirs(pass_dir, exist_ok=True)
    with open(log_path, "w") as log:
        done = subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT, text=True)
    state["wall_s"] += time.time() - started
    ledger = read_json(os.path.join(pass_dir, "ledger.json"))
    cases = cases_summary(ledger)
    hygiene = hygiene_of(pass_dir)
    entry = {"pass": index, "dir": pass_dir, "argv": argv, "exit": done.returncode,
             "seconds": round(time.time() - started, 2), "binary": ledger.get("binary"),
             "cases": cases, "coverage": ledger.get("coverage") or {},
             "captures": ledger.get("captures") or [], "artifacts": ledger.get("artifacts") or [],
             "hygiene": hygiene, "faults": ledger.get("faults") or [],
             "reaping": ledger.get("reaping")}
    state["cases"] += len(cases)
    state["commands"] |= set((ledger.get("coverage") or {}).get("commands") or [])
    state["scenarios"] |= set((ledger.get("coverage") or {}).get("scenario_ids") or [])
    state["shas"] |= {ledger.get("binary", {}).get("sha256")}
    state["live_pids"] = hygiene["pids_alive"] + [{"wave": "reaping", "pid": pid}
                                                  for pid in hygiene["our_pids_alive_after"]]
    state["worlds_present"] = hygiene["worlds_present"]
    state["passes"].append(entry)
    journal(entry)
    print("    exit=%d cases=%d (%d total) live_pids=%d worlds_present=%d captures=%d"
          % (done.returncode, len(cases), state["cases"], len(state["live_pids"]),
             len(state["worlds_present"]), len(entry["captures"])), flush=True)
    return entry


def build_parser():
    parser = argparse.ArgumentParser(description="crashbot pilot driver (T3)")
    parser.add_argument("--scenarios", nargs="+", required=True)
    parser.add_argument("--binary", default=P.DEFAULT_BINARY)
    parser.add_argument("--run-root", default="/tmp/zene-pilot/pilot")
    parser.add_argument("--instances", type=int, default=4)
    parser.add_argument("--target-cases", type=int, default=300)
    parser.add_argument("--wall-budget-s", type=float, default=4500.0)
    parser.add_argument("--min-free-gb", type=float, default=20.0)
    parser.add_argument("--case-cap-s", type=float, default=210.0)
    parser.add_argument("--run-cap-s", type=float, default=2400.0)
    return parser


def run_passes(args, state, journal):
    """Passes until a declared bound stops the pilot; returns the reason it stopped."""
    index, reason = 0, None
    while reason is None:
        index += 1
        run_one_pass(index, args, state, journal)
        reason = stop_reason(state, args)
    return reason


def coverage_summary(state):
    surface_ids = [case["surface_ids"] or 0 for entry in state["passes"] for case in entry["cases"]]
    return {"distinct_command_ids": len(state["commands"]), "commands": sorted(state["commands"]),
            "distinct_scenario_ids": len(state["scenarios"]), "scenarios": sorted(state["scenarios"]),
            "registry_size": max(surface_ids or [0])}


def hygiene_summary(state):
    return {"pids_alive_at_end": state["live_pids"],
            "worlds_present_at_end": state["worlds_present"],
            "pids_checked": sum(entry["hygiene"]["pids_checked"] for entry in state["passes"]),
            "our_pids_alive_after_union": sorted({pid for entry in state["passes"]
                                                  for pid in entry["hygiene"]["our_pids_alive_after"]}),
            "orphan_check_at_end": P.orphan_check([], label="pilot end")}


def main(argv=None):
    args = build_parser().parse_args(argv)
    if args.instances > MAX_INSTANCES:
        print("the pool ceiling is %d instances: %d refused (plan L1)" % (MAX_INSTANCES,
                                                                         args.instances))
        return 2
    args.run_root = os.path.abspath(args.run_root)
    os.makedirs(args.run_root, exist_ok=True)
    ledger_path = os.path.join(args.run_root, "pilot-ledger.json")
    state = {"cases": 0, "wall_s": 0.0, "commands": set(), "scenarios": set(), "shas": set(),
             "passes": [], "live_pids": [], "worlds_present": []}
    identity = P.binary_identity(args.binary)
    pilot = {"kind": "crashbot-pilot", "started_at": P.utc_now(), "run_root": args.run_root,
             "binary_at_start": identity, "box_at_start": P.box_state(),
             "bounds": {"instances": args.instances, "target_cases": args.target_cases,
                        "wall_budget_s": args.wall_budget_s, "min_free_gb": args.min_free_gb,
                        "case_cap_s": args.case_cap_s, "run_cap_s": args.run_cap_s},
             "orphan_check_at_start": P.orphan_check([], label="pilot start"),
             "passes": [], "stop_reason": None}
    if identity.get("version_exit") != 0:
        print("the binary does not run: %s" % identity.get("version"))
        return 2

    def journal(_entry=None):
        pilot["passes"] = state["passes"]
        pilot["totals"] = {"cases": state["cases"], "wall_s": round(state["wall_s"], 2),
                           "distinct_commands": len(state["commands"]),
                           "distinct_scenarios": len(state["scenarios"]),
                           "binary_shas": sorted(name for name in state["shas"] if name)}
        with open(ledger_path + ".tmp", "w") as handle:
            json.dump(pilot, handle, indent=2, default=str)
        os.replace(ledger_path + ".tmp", ledger_path)

    reason = run_passes(args, state, journal)
    pilot["stop_reason"] = reason
    pilot["finished_at"] = P.utc_now()
    pilot["coverage"] = coverage_summary(state)
    pilot["hygiene"] = hygiene_summary(state)
    pilot["binary_at_end"] = P.binary_identity(args.binary)
    journal()
    print("\n=== pilot %s ===" % args.run_root)
    print("  passes: %d  cases: %d  wall: %.0fs  stop: %s"
          % (len(state["passes"]), state["cases"], state["wall_s"], reason))
    print("  coverage: %d distinct command id(s) of %s live, %d distinct scenario id(s)"
          % (pilot["coverage"]["distinct_command_ids"], pilot["coverage"]["registry_size"],
             pilot["coverage"]["distinct_scenario_ids"]))
    print("  hygiene: %d pid(s) checked, alive at end: %s, worlds present: %s"
          % (pilot["hygiene"]["pids_checked"], state["live_pids"], state["worlds_present"]))
    print("  ledger -> %s" % ledger_path)
    return 0 if not state["live_pids"] and not state["worlds_present"] else 1


if __name__ == "__main__":
    sys.exit(main())
