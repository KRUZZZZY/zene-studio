#!/usr/bin/env python3
"""Crashbot capture (T3) - the replayable directory a non-ok case leaves behind.

Plan L5/T3 (`verification/AGENT-CRASH-TESTING-PLAN.md` §2 L5, §7 T3): every case that does
not reach its end files ONE directory holding what a triager needs to reproduce it without
the run: the raw socket transcript, the case's own outcomes, the project/scratch state the
case built (copied before the pool's reap removes the world), the exact argv and environment
the instance ran with, its settings file, the instance's own logs, exit/signal, the box
state, and the product's crash reports when arming was in place. Clean runs file NOTHING -
the writer is not a catch-all, and a run of 300 clean cases leaves 0 byte of capture.

The product's reporter is armed per instance with `crash.enable` (directory = a path inside
the case's own scratch), read back with `crash.list_reports` BEFORE the instance is closed,
and `crash.upload_report` is never called: bots never phone home (`irc` plan §4 rule 1).

Nothing here launches a process; nothing here writes outside the run directory under /tmp or
verification/. A dead instance's in-memory state cannot be copied - what a capture holds for
a crash is the transcript that led to it plus whatever the case had written to disk, and the
file says so in `capture.json.env_note` rather than implying more.
"""

import json
import os
import shutil
import stat
import sys
import time

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import pool as P          # noqa: E402  (module-level facts: harness path, box state, orphan check)

FILE_CAP_BYTES = 32 * 1024 * 1024          # one file: copied or named as skipped
TOTAL_CAP_BYTES = 256 * 1024 * 1024        # one capture directory
ARM_TIMEOUT_S = 10.0
REPORT_TIMEOUT_S = 10.0
REPORT_NAME_HINTS = ("crash-report", "zene-crash", "-report.txt", "report.txt", ".report",
                     "-report.json", ".marker")


def utc_now():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def sha256_of(path):
    import hashlib
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _safe_call(client, request_id, cmd, args, timeout, transcript=None):
    """One bounded call that never raises: a capture must survive a dying instance."""
    started = time.perf_counter()
    record = {"cmd": cmd, "args": args, "reply": None, "error": None, "seconds": None}
    try:
        record["reply"] = client.call(request_id, cmd, args or {}, timeout=timeout,
                                      transcript=transcript)
    except Exception as error:                                    # bounded: never fatal here
        record["error"] = "%s: %s" % (type(error).__name__, str(error).splitlines()[0][:300])
    record["seconds"] = round(time.perf_counter() - started, 3)
    return record


# ---------------------------------------------------------------------------
# arming the product's own reporter (per instance, one directory per case)
# ---------------------------------------------------------------------------


def arm_directory(case_dir):
    directory = os.path.join(case_dir, "work", "crash-reports")
    os.makedirs(directory, exist_ok=True)
    return directory


def arm(client, case_dir, next_id=1):
    """Arm the product's reporter through `crash.list_reports` + `crash.enable`.

    Measured on this build (0.2.1-alpha.612+a039d26): a test instance is armed AT STARTUP
    and writes ONE bounded report file (`<workspace>/crash-reports/zene-crash-report.txt`),
    so `crash.enable` is REFUSED with 'the crash reporter is ALREADY armed in this instance'
    when it has nothing to change. The honest sequence is therefore: read the state first,
    enable only when the product says it is not armed, and record WHICH path was taken -
    including the reporter's own report/session-marker paths, taken from the product's reply
    rather than assumed. `crash.upload_report` is never called (plan §4 rule 1).
    """
    directory = arm_directory(case_dir)
    state = read_reports(client, next_id)
    record = {"directory": directory, "state_before": state,
              "enable": None, "armed": None, "already_armed": None}
    armed = ((state.get("reply") or {}).get("result") or {}).get("armed")
    if armed is True:
        record.update({"armed": True, "already_armed": True})
        return record
    enable = _safe_call(client, next_id + 1, "crash.enable", {"directory": directory},
                        ARM_TIMEOUT_S)
    record.update({"enable": enable, "armed": bool((enable["reply"] or {}).get("ok")),
                   "already_armed": False})
    return record


def read_reports(client, next_id=2):
    """`crash.list_reports` before close: the product's own count, whatever it is."""
    return _safe_call(client, next_id, "crash.list_reports", {}, REPORT_TIMEOUT_S)


# ---------------------------------------------------------------------------
# bounded copies
# ---------------------------------------------------------------------------


def _copy_one(path, source, target, cap, budget):
    """One file: a copy row, or a skip record. Never raises for a file it cannot copy."""
    if os.path.islink(path) or not os.path.isfile(path):
        return None, {"path": path, "why": "not a regular file"}, 0
    size = os.path.getsize(path)
    if size > cap or size > budget["left"]:
        why = "above the per-file cap" if size > cap else "above the capture total cap"
        return None, {"path": path, "bytes": size, "why": why}, 0
    destination = os.path.join(target, os.path.relpath(path, source))
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    shutil.copy2(path, destination)
    budget["left"] -= size
    row = {"path": path, "relative": os.path.relpath(path, source), "bytes": size}
    if size <= 8 * 1024 * 1024:
        row["sha256"] = sha256_of(path)
    return row, None, size


def copy_tree(source, target, skip=(), cap=FILE_CAP_BYTES, budget=None):
    """Copy a tree with per-file and total caps; returns (manifest, skipped, copied_bytes)."""
    budget = {"left": TOTAL_CAP_BYTES} if budget is None else budget
    manifest, skipped, copied = [], [], 0
    if not os.path.isdir(source):
        return manifest, skipped, copied
    for root, _dirs, names in os.walk(source):
        for name in sorted(names):
            path = os.path.join(root, name)
            if path in skip or name in skip:
                continue
            row, missed, size = _copy_one(path, source, target, cap, budget)
            copied += size
            if row:
                manifest.append(row)
            if missed:
                skipped.append(missed)
    return manifest, skipped, copied


def copy_file(source, target):
    try:
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copy2(source, target)
        return {"path": source, "bytes": os.path.getsize(source)}
    except OSError as error:
        return {"path": source, "error": str(error)}


# ---------------------------------------------------------------------------
# the capture directory itself
# ---------------------------------------------------------------------------


def _case_summary(case):
    steps = [{"index": step["index"], "cmd": step["cmd"], "outcome": step["outcome"],
              "error_kind": step["error_kind"], "detail": step["detail"],
              "seconds": step["seconds"], "mismatches": step.get("mismatches")}
             for step in case["steps"]]
    return {"case": case["case"], "scope": case["scope"], "seed": case["seed"],
            "terminal": case["terminal"], "counts": case["counts"],
            "first_bad_step": next((step for step in steps
                                    if step["outcome"] not in ("ok", "refusal")), None),
            "steps": steps}


def _instance_facts(pool, case):
    rec = pool.instances.get(case["instance"])
    if rec is None:
        return {}, None
    alive = rec.alive()
    return {"name": rec.name, "pid": rec.pid, "socket": rec.socket, "world": rec.world,
            "state": rec.state, "spawned_at": rec.spawned_at, "alive_at_capture": alive,
            "exit_code": None if alive else rec.process.returncode,
            "signal": None if alive else (abs(rec.process.returncode)
                                          if (rec.process.returncode or 0) < 0 else None)}, rec


def _argv_env_settings(rec):
    instance = rec.instance
    argv = [instance.binary, "--config", instance.config_path,
            "--control-socket", instance.socket_path]
    env = {key: value for key, value in instance.env().items()
           if key in ("LD_LIBRARY_PATH", "QT_QPA_PLATFORM", "HOME", "XDG_CONFIG_HOME",
                      "XDG_DATA_HOME", "SDL_AUDIODRIVER", "PWD", "PATH", "LANG",
                      "LMMS_DATA_DIR", "LMMS_PLUGIN_DIR")}
    settings = None
    try:
        with open(instance.config_path) as handle:
            settings = handle.read()
    except OSError:
        pass
    return argv, env, settings


def _report_files(arm_info, world):
    """Every file the product's reporter may have written: the armed dir + the whole world.

    The reporter's own paths (measured): `<workspace>/crash-reports/zene-crash-report.txt`
    and `<workspace>/zene-session-open.marker`. The walk is name-hint driven and bounded by
    the capture's own file caps, so a big world cannot flood a capture.
    """
    found = []
    roots = [path for path in ((arm_info or {}).get("directory"), world) if path]
    for root in roots:
        for base, _dirs, names in os.walk(root):
            for name in names:
                lowered = name.lower()
                if any(hint in lowered for hint in REPORT_NAME_HINTS):
                    found.append(os.path.join(base, name))
    return sorted(set(found))


def _capture_index(run_dir):
    captures = os.path.join(run_dir, "captures")
    return len(os.listdir(captures)) + 1 if os.path.isdir(captures) else 1


def _report_layers(directory, rec, arm_info, next_id):
    """The product's own reporting evidence: its state read live, and the files it wrote."""
    state = None
    if rec is not None and rec.alive() and rec.client is not None:
        state = read_reports(rec.client, next_id)
    files = [copy_file(path, os.path.join(directory, "crash-report", os.path.basename(path)))
             for path in _report_files(arm_info, rec.world if rec else None)]
    return {"list_reports": state, "files": files,
            "armed_directory": (arm_info or {}).get("directory")}


def _world_layers(directory, rec):
    """The instance's whole world tree (its HOME/XDG dirs), minus the socket."""
    if rec is None or not os.path.isdir(rec.world):
        return [], [], 0
    return copy_tree(rec.world, os.path.join(directory, "world"), skip=(rec.socket,))


def _collect_layers(directory, case, rec, arm_info, transcript_path, next_id):
    """Copy every evidence layer into `directory`; returns the manifest `file_capture` needs."""
    if transcript_path and os.path.exists(transcript_path):
        copy_file(transcript_path, os.path.join(directory, "transcript.txt"))
    copy_file(os.path.join(case["dir"], "outcome.json"), os.path.join(directory, "outcome.json"))
    return {"reports": _report_layers(directory, rec, arm_info, next_id),
            "work": copy_tree(os.path.join(case["dir"], "work"), os.path.join(directory, "work")),
            "world": _world_layers(directory, rec)}


def _capture_payload(pool, case, facts, argv, env, settings, layers):
    """The capture.json document: what was run, on what, and what is on disk beside it."""
    work_manifest, work_skipped, work_bytes = layers["work"]
    world_manifest, world_skipped, world_bytes = layers["world"]
    reports = layers["reports"]
    return {
        "kind": "crashbot-capture", "at": utc_now(), "case": _case_summary(case),
        "instance": facts, "binary": pool.identity,
        "harness": {"path": P.HARNESS_PATH, "sha256": P.sha256_of(P.HARNESS_PATH)},
        "argv": argv, "env": env, "settings_xml": settings,
        "crash_reporting": {"arm": {key: value for key, value in (case["crash"]["arm"]).items()
                                    if key != "state_before"},
                            "arm_ok": (case["crash"]["arm"] or {}).get("armed"),
                            "state_before": reports["list_reports"],
                            "files": reports["files"]},
        "evidence": {"transcript": os.path.join(case["dir"], "transcript.txt"),
                     "outcome_json": os.path.join(case["dir"], "outcome.json"),
                     "work_copied": work_manifest, "work_skipped": work_skipped,
                     "work_bytes": work_bytes, "world_copied": world_manifest,
                     "world_skipped": world_skipped, "world_bytes": world_bytes},
        "env_note": ("a dead instance's in-memory state cannot be copied: for a crash the "
                     "transcript plus the case's on-disk work is the reproduction; for a hang "
                     "the instance was still alive at capture time (instance.alive_at_capture)"),
        "box": P.box_state(),
    }


def file_capture(pool, case, scenario, run_dir, arm_info, transcript_path, next_id=5000):
    """File ONE replayable directory for a case that did not reach its end. Returns its row."""
    rec = pool.instances.get(case["instance"])
    label = "%03d-%s-%s" % (_capture_index(run_dir), case["case"].replace("/", "-"),
                            case["instance"])
    directory = os.path.join(run_dir, "captures", label)
    os.makedirs(directory, exist_ok=True)
    facts, rec = _instance_facts(pool, case)
    argv, env, settings = _argv_env_settings(rec) if rec else ([], {}, None)
    layers = _collect_layers(directory, case, rec, arm_info, transcript_path, next_id)
    payload = _capture_payload(pool, case, facts, argv, env, settings, layers)
    payload["capture"] = label
    payload["directory"] = directory
    payload["orphan_check"] = P.orphan_check([row.pid for row in pool.instances.values()],
                                             label="at capture %s" % label)
    with open(os.path.join(directory, "capture.json"), "w") as handle:
        json.dump(payload, handle, indent=2, default=str)
    with open(os.path.join(directory, "transcript.txt"), "a") as handle:
        handle.write("\n\n---- capture filed %s ----\n" % payload["at"])
    _write_replay(directory, scenario, pool.identity, case)
    work_bytes = layers["work"][2]
    world_bytes = layers["world"][2]
    return {"capture": label, "case": case["case"], "instance": case["instance"],
            "pid": facts.get("pid"), "terminal": case["terminal"], "path": directory,
            "at": payload["at"], "bytes": work_bytes + world_bytes}


def _write_replay(directory, scenario, identity, case):
    """One-command reproduction (IR-22): the exact pack, seed, binary and a fresh run dir."""
    source = scenario.get("_path") or "tools/crashbot/scenarios/%s.json" % \
        scenario["id"].replace("/", "-")
    python = os.environ.get("PYTHON3", "python3")
    worktree = os.environ.get("CRASHBOT_WORKTREE", "/home/kruzzzzy/Documents/AI_KOS_PROJECT/"
                              "projects/lmms-fl-research/zene-030/wcrash")
    # `--version` prints the build-options block too, so only its FIRST line is a comment here
    version = str(identity.get("version") or "?").splitlines()[0]
    lines = [
        "#!/bin/sh",
        "# replay %s (seed %s) - filed by tools/crashbot/capture.py" % (case["case"], case["seed"]),
        "# build under test: %s sha256=%s" % (version, identity.get("sha256")),
        "set -e",
        "cd %s" % worktree,
        "exec %s tools/crashbot/runner.py --scenarios %s --instances 1 \\" % (python, source),
        "    --binary %s --run-dir /tmp/zene-pilot/replay-%s-$(date +%%s)" % (
            identity.get("path"), case["case"].replace("/", "-")),
        "",
    ]
    path = os.path.join(directory, "replay.sh")
    with open(path, "w") as handle:
        handle.write("\n".join(lines))
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def finalize(pool, cases):
    """After the reap: bind each filed capture to its instance's reaping row (exit, world_gone).

    Takes the run's CASES, not the capture rows: the filtering belongs here so the runner's
    per-wave call stays branch-free (its own complexity budget is measured by the gates).
    """
    captures = [case["capture"] for case in cases if case.get("capture")]
    if not captures:
        return captures
    reaping = pool.reaping or {}
    rows = reaping.get("instances") or {}
    for row in captures:
        path = os.path.join(row["path"], "capture.json")
        try:
            with open(path) as handle:
                payload = json.load(handle)
        except (OSError, ValueError):
            row["finalize_error"] = "capture.json unreadable at finalize"
            continue
        payload["reaping"] = rows.get(row["instance"])
        payload["our_pids_alive_after"] = reaping.get("our_pids_alive_after")
        payload["finalized_at"] = utc_now()
        with open(path, "w") as handle:
            json.dump(payload, handle, indent=2, default=str)
        row["reap"] = (rows.get(row["instance"]) or {}).get("exit_code")
        row["hard_kill"] = (rows.get(row["instance"]) or {}).get("hard_kill")
    return captures


def _selftest(argv):
    """`capture.py --selftest`: the writer's own negative control, no instance involved."""
    import tempfile
    root = tempfile.mkdtemp(prefix="crashbot-capture-selftest-", dir="/tmp")
    source = os.path.join(root, "source")
    os.makedirs(os.path.join(source, "sub"))
    with open(os.path.join(source, "small.txt"), "w") as handle:
        handle.write("evidence\n")
    manifest, skipped, copied = copy_tree(source, os.path.join(root, "copy"))
    problems = []
    if len(manifest) != 1 or copied != 9:
        problems.append("copy_tree missed the small file: %r %r" % (manifest, copied))
    if not os.path.exists(os.path.join(root, "copy", "small.txt")):
        problems.append("copy_tree did not write the copy")
    print("copy_tree manifest: %s" % json.dumps(manifest))
    print("skipped: %r bytes:%d" % (skipped, copied))
    shutil.rmtree(root, ignore_errors=True)
    print("verdict: %s" % ("PASS" if not problems else "FAIL %s" % problems))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(_selftest(sys.argv[1:]))
