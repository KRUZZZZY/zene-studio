#!/usr/bin/env python3
"""Reaper selftest — the negative controls that make `pool.reap()` a tested feature.

T1's acceptance, as four runnable proofs, each with its own command and its own evidence:

  (a)  raise-mid-run   — a wave aborted by an injected fault must still reap everything and
                         file ONE artifact. Proves the finally/abort path and the artifact
                         filing (a clean run files none: asserted by the same run's sibling).
  (a2) SIGTERM mid-run — the same, but the signal arrives at the runner and the pool's SIGTERM
                         handler reaps; this is the path a killed CI lane or a closing terminal
                         exercises, and it is the one that cannot be tested by a return value.
  (b)  SIGKILL         — one instance is SIGKILLed BY ITS EXACT PID mid-case: EXACTLY ONE
                         artifact is filed, the other instance's case still reaches its end,
                         and the wave still shuts down cleanly.
  (d)  orphan check    — `pgrep -a zene` on a clean box (exit 1), then the same probe while a
                         pool is up (our PIDs are listed and attributed), then after the reap
                         (our PIDs gone). This is the wave-start helper, demonstrated.

Every claim is PID-scoped: the PIDs come from the pool's own run.json. Nothing here uses a
pattern kill - `pkill -f` has already matched a lane's own build on this box twice, so the only
signals sent are `os.kill(<exact pid>, ...)` on processes this script started.

Scratch only under /tmp. Exit 0 only when every proof held.

Usage:
  python3 tools/crashbot/selftest_reaper.py            # all four proofs
  python3 tools/crashbot/selftest_reaper.py --proof b  # one of a a2 b d
"""

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import time

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import pool as P          # noqa: E402

RUNNER = os.path.join(HERE, "runner.py")
SCENARIOS = os.path.join(HERE, "scenarios")
RUN_ROOT = "/tmp/crashbot-selftest"
PYTHON = sys.executable
BINARY = os.environ.get("ZENE_BINARY", P.DEFAULT_BINARY)


def fresh(proof):
    directory = os.path.join(RUN_ROOT, proof)
    shutil.rmtree(directory, ignore_errors=True)
    os.makedirs(directory, exist_ok=True)
    return directory


def run_runner(run_dir, extra, scenarios=None, timeout=300):
    """Run the runner as a CHILD process, unpiped exit code, full output kept."""
    argv = [PYTHON, RUNNER, "--scenarios"] + list(scenarios or [SCENARIOS]) + \
           ["--run-dir", run_dir, "--binary", BINARY] + extra
    started = time.time()
    done = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
    return {"argv": argv, "exit": done.returncode, "seconds": round(time.time() - started, 2),
            "stdout": done.stdout, "stderr": done.stderr,
            "tail": (done.stdout + done.stderr).strip().splitlines()[-6:]}


def ledger_of(run_dir):
    with open(os.path.join(run_dir, "ledger.json")) as handle:
        return json.load(handle)


def wave_ledgers(run_dir):
    waves = os.path.join(run_dir, "waves")
    out = {}
    for name in sorted(os.listdir(waves)):
        path = os.path.join(waves, name, "run.json")
        if os.path.exists(path):
            with open(path) as handle:
                out[name] = json.load(handle)
    return out


def pids_gone(pids):
    return [pid for pid in pids if os.path.exists("/proc/%d" % pid)]


def worlds_gone(worlds):
    return [world for world in worlds if os.path.exists(world)]


def case_files(run_dir):
    found = []
    for root, _dirs, files in os.walk(run_dir):
        found.extend(os.path.join(root, name) for name in files if name == "outcome.json")
    return found


def pids_and_worlds(waves):
    pids, worlds, artifacts = [], [], []
    for ledger in waves.values():
        for row in ledger["instances"]:
            pids.append(row["pid"])
            worlds.append(row["world"])
        artifacts.extend(ledger["artifacts"])
    return pids, worlds, artifacts


# ---------------------------------------------------------------------------
# the proofs
# ---------------------------------------------------------------------------


def proof_raise(run_dir):
    """(a) A scenario run that raises mid-way: reaped anyway, ONE artifact filed.

    Two packs are named EXPLICITLY, not taken from the scenarios directory: the wave the fault
    and its sibling land in is part of the proof, and a directory whose pack set grows (T3
    widened it from 3 packs to 44) would silently re-partition the wave and make the sibling
    assertion measure a different case than the one it names.
    """
    arrange = os.path.join(SCENARIOS, "arrange-undo-structural.json")
    mixer = os.path.join(SCENARIOS, "mixer-routing-churn.json")
    result = run_runner(run_dir, ["--instances", "2", "--inject-fault-at",
                                  "arrange/undo-structural-0001:12"], scenarios=[arrange, mixer])
    waves = wave_ledgers(run_dir)
    pids, worlds, artifacts = pids_and_worlds(waves)
    ledger = ledger_of(run_dir)
    checks = {
        "exit_code_3": result["exit"] == 3,
        "one_artifact": len(artifacts) == 1,
        "artifact_is_run_aborted": bool(artifacts) and artifacts[0]["kind"] == "run_aborted",
        "artifact_on_disk_with_its_evidence": bool(artifacts)
                                              and os.path.exists(os.path.join(artifacts[0]["path"],
                                                                              "artifact.json")),
        "fault_recorded": any("fault injected" in item.get("error", "")
                              for item in ledger.get("faults") or []),
        "no_pid_alive": pids_gone(pids) == [],
        "no_world_left": worlds_gone(worlds) == [],
        "reaping_reason_names_the_abort": all("context exit (exception" in (ledger_["reaping"] or {}).get("reason", "")
                                              for ledger_ in waves.values()),
        "faulted_case_has_no_outcome_file": not any(
            "arrange__undo-structural-0001" in path for path in case_files(run_dir)),
        "sibling_case_did_finish": any(
            "mixer__routing-churn-0001" in path for path in case_files(run_dir)),
    }
    return {"proof": "a: raise mid-run", "run_dir": run_dir, "pids": pids, "worlds": worlds,
            "artifacts": artifacts, "result": result, "checks": checks,
            "passed": all(checks.values())}


def proof_sigterm(run_dir):
    """(a2) SIGTERM at the runner mid-case: the pool's handler reaps, by exact PID only.

    The two packs are named explicitly for the same reason proof (a) names its pair: with the
    scenarios DIRECTORY as the input, a widened pack set re-partitions every wave under the
    proof (T3 took it from 3 packs to 44) and the "a case was still in flight" claim would
    then rest on whatever happened to be in wave 1.
    """
    arrange = os.path.join(SCENARIOS, "arrange-undo-structural.json")
    mixer = os.path.join(SCENARIOS, "mixer-routing-churn.json")
    argv = [PYTHON, RUNNER, "--scenarios", arrange, mixer, "--run-dir", run_dir,
            "--binary", BINARY, "--instances", "2", "--step-pause-s", "0.5"]
    child = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    wave_json = os.path.join(run_dir, "waves", "w1", "run.json")
    deadline = time.time() + 90
    pids = []
    while time.time() < deadline:
        if os.path.exists(wave_json):
            try:
                with open(wave_json) as handle:
                    ledger = json.load(handle)
                pids = [row["pid"] for row in ledger["instances"] if row["state"] != "reaped"]
            except (json.JSONDecodeError, KeyError):
                pids = []
            if pids:
                break
        time.sleep(0.1)
    time.sleep(2.0)                              # mid-case by construction (step pauses)
    os.kill(child.pid, signal.SIGTERM)           # exact PID: OUR child
    child.wait(timeout=60)
    with open(wave_json) as handle:
        ledger = json.load(handle)
    reaping = ledger["reaping"] or {}
    worlds = [row["world"] for row in ledger["instances"]]
    checks = {
        "died_of_sigterm": child.returncode == -signal.SIGTERM,
        "pids_found_before_the_signal": bool(pids),
        "reaping_runs": bool(reaping),
        "reaping_reason_is_signal_15": str(reaping.get("reason")) == "signal 15",
        "no_pid_alive": pids_gone(pids) == [],
        "no_world_left": worlds_gone(worlds) == [],
        "no_hard_kill_needed": all(not row.get("hard_kill") for row in reaping.get("instances", {}).values()),
        "case_never_finished": case_files(run_dir) == [],
        "our_pids_alive_after_is_empty": reaping.get("our_pids_alive_after") == [],
    }
    return {"proof": "a2: SIGTERM mid-run", "run_dir": run_dir, "runner_pid": child.pid,
            "pids": pids, "worlds": worlds, "returncode": child.returncode,
            "checks": checks, "passed": all(checks.values())}


def sigkill_checks(result, artifacts, killed, reaping, terminals, pids, worlds, wave):
    """Every claim acceptance (b) makes, as one dict, so the proof reads as its own rubric."""
    artifact = artifacts[0] if artifacts else {}
    killed_pid = killed[0]["pid"] if killed else None
    deliberate = wave["deliberate"][0] if wave["deliberate"] else {}
    sibling = reaping.get("i0") or {}
    killed_reap = reaping.get("i1") or {}
    return {
        "exit_code_1": result["exit"] == 1,
        "one_artifact": len(artifacts) == 1,
        "artifact_is_instance_died": artifact.get("kind") == "instance_died",
        "artifact_pid_is_the_killed_one": killed_pid is not None
                                          and artifact.get("pid") == killed_pid,
        "artifact_signal_is_sigkill": artifact.get("signal") == signal.SIGKILL,
        "killed_instance_has_no_hard_kill": not killed_reap.get("hard_kill"),
        "sibling_quit_cleanly": sibling.get("quit_ok") is True and sibling.get("exit_code") == 0,
        "sibling_case_reached_its_end": terminals.get("arrange/undo-structural-0001") == "ok",
        "killed_case_terminal_is_crash": terminals.get("mixer/routing-churn-0001") == "crash",
        "no_pid_alive": pids_gone(pids) == [],
        "no_world_left": worlds_gone(worlds) == [],
        "deliberate_kill_recorded_with_pid": bool(deliberate) and deliberate.get("pid") == killed_pid,
    }


def capture_checks(captures, killed_pid):
    """T3's own negative control: the crash case filed ONE replayable directory, complete."""
    capture = captures[0] if captures else {}
    directory = capture.get("path") or ""
    try:
        with open(os.path.join(directory, "capture.json")) as handle:
            payload = json.load(handle)
    except (OSError, ValueError):
        payload = {}
    instance = payload.get("instance") or {}
    return {
        "crash_case_files_one_capture": len(captures) == 1
                                        and capture.get("case") == "mixer/routing-churn-0001",
        "capture_names_the_killed_pid": capture.get("pid") == killed_pid,
        "capture_has_transcript_and_replay": bool(directory) and all(
            os.path.exists(os.path.join(directory, name))
            for name in ("transcript.txt", "replay.sh", "outcome.json", "capture.json")),
        "capture_carries_exit_and_signal": instance.get("exit_code") == -9
                                           and instance.get("signal") == 9,
        "capture_bound_to_the_reap": bool(payload.get("reaping")),
        "capture_records_the_binary_sha": bool((payload.get("binary") or {}).get("sha256")),
    }


def proof_sigkill(run_dir):
    """(b) One instance SIGKILLed mid-case: EXACTLY one artifact, the sibling still finishes.

    T3 extends this proof: the killed case is a CRASH outcome, and a crash outcome must file
    one replayable capture directory (transcript, replay.sh, argv/env, exit/signal, the reap).
    """
    arrange = os.path.join(SCENARIOS, "arrange-undo-structural.json")
    mixer = os.path.join(SCENARIOS, "mixer-routing-churn.json")
    killed_case = "mixer/routing-churn-0001"
    result = run_runner(run_dir, ["--instances", "2", "--sigkill", "i1:%s:12" % killed_case],
                        scenarios=[arrange, mixer])
    waves = wave_ledgers(run_dir)
    pids, worlds, artifacts = pids_and_worlds(waves)
    ledger = ledger_of(run_dir)
    wave = list(waves.values())[0] if waves else {"instances": [], "deliberate": [],
                                                  "reaping": {"instances": {}}}
    killed = [row for row in wave["instances"] if row["name"] == "i1"]
    killed_pid = killed[0]["pid"] if killed else None
    terminals = {case["case"]: case["terminal"] for case in ledger["cases"]}
    checks = sigkill_checks(result, artifacts, killed, wave["reaping"]["instances"] or {},
                            terminals, pids, worlds, wave)
    checks.update(capture_checks(ledger.get("captures") or [], killed_pid))
    return {"proof": "b: deliberate SIGKILL", "run_dir": run_dir, "pids": pids, "worlds": worlds,
            "artifacts": artifacts, "terminals": terminals, "killed": killed, "checks": checks,
            "passed": all(checks.values()), "result": result}


def proof_orphan_check(_run_dir):
    """(d) The wave-start check: probe consistency, attribution of our PIDs, then gone.

    The plan's literal pass is `pgrep -a zene` exit 1 - "no process named zene anywhere". That
    is a statement about the WHOLE BOX, and a foreign lane driving the same release binary makes
    it false while this run is perfectly clean (measured: two foreign PIDs were live during this
    proof). So the proof asserts the PID-scoped facts that hold under either regime, and records
    the name-scoped probe as evidence rather than as a criterion.
    """
    before = P.orphan_check([], label="before anything is booted")
    pool = P.InstancePool(names=["i0"], binary=BINARY, run_dir=os.path.join(RUN_ROOT, "d-orphan"),
                          run_id="selftest-orphan")
    P.register(pool)
    with pool:
        pool.boot()
        during = P.orphan_check([pool.instances["i0"].pid], label="while the pool holds one")
        alive_during = during["our_pids_alive"]
        log = pool.instances["i0"].instance.stderr_path
    after = P.orphan_check([], label="after the reap")
    checks = {
        "probe_is_consistent": (before["probe"]["exit"] == 1) == (before["probe"]["matches"] == []),
        "our_pid_listed_and_attributed_while_up": alive_during == [pool.instances["i0"].pid],
        "foreign_procs_are_never_claimed_as_ours": all(
            str(pool.instances["i0"].pid) not in line for line in during["foreign_alive"]),
        "our_pids_gone_after": after["our_pids_alive"] == []
                               and not any(str(pool.instances["i0"].pid) in line
                                           for line in after["probe"]["matches"]),
        "world_gone_after": not os.path.exists(pool.instances["i0"].world),
        "app_log_removed_with_the_world": not os.path.exists(log),
    }
    return {"proof": "d: orphan check", "before": before, "during": during, "after": after,
            "pid": pool.instances["i0"].pid, "checks": checks, "passed": all(checks.values()),
            "name_scoped_clean_box_observed": before["probe"]["exit"] == 1,
            "foreign_pids_present_during_the_proof": len(during["foreign_alive"])}


def proof_hang_control(run_dir):
    """(h) IR-17's negative control, live: a FROZEN instance is a hang, a KILLED one is a crash.

    The two outcomes are the runner's own classifier at work (`runner.run_step`) against a real
    process: SIGSTOP by exact PID freezes it, a step with a 2 s declared budget expires with the
    process still alive -> `hang`; SIGCONT proves the freeze was a budget fact and not damage
    (the instance answers again); after a SIGKILL by exact PID the same step over the same dead
    socket is a `crash`. The hang is never counted as a crash, and nothing is killed for it.
    """
    import runner as R
    pool = P.InstancePool(names=["i0"], binary=BINARY, run_dir=run_dir, run_id="selftest-hang")
    P.register(pool)
    env = {"bindings": {}, "work_dir": run_dir, "fixture_dir": R.FIXTURE_DIR, "fixtures_used": {}}
    step = {"index": 1, "cmd": "transport.get_state", "args": {}, "budget_s": 2.0}
    with pool:
        pool.boot()
        rec = pool.instances["i0"]
        transcript = P.H.Transcript()
        session = {"client": rec.client, "instance": rec.instance, "transcript": transcript,
                   "registry": R.fetch_registry(rec, transcript, "selftest/hang-classifier"),
                   "ids": iter(range(2, 100000)), "stopped": None}
        caps = {"case_cap_s": 60, "run_cap_s": 120, "case_deadline": time.time() + 60,
                "run_deadline": time.time() + 120}
        os.kill(rec.pid, signal.SIGSTOP)                     # exact PID: freeze, do not kill
        hang = R.run_step(session, step, env, caps)
        alive_during = rec.alive()
        os.kill(rec.pid, signal.SIGCONT)
        time.sleep(0.3)
        recovered = rec.client.call(700001, "control.ping", timeout=10,
                                    transcript=transcript).get("ok")
        os.kill(rec.pid, signal.SIGKILL)                     # exact PID: now the crash side
        rec.process.wait(timeout=30)
        crash = R.run_step(session, step, env, caps)
        pid = rec.pid
    checks = {
        "frozen_instance_times_out": hang["outcome"] == "hang",
        "hang_carries_the_budget_as_its_detail": "no response line" in str(hang["detail"])
                                                 or "timed out" in str(hang["detail"]),
        "process_was_alive_during_the_hang": alive_during,
        "hang_is_not_a_crash": hang["outcome"] != "crash",
        "instance_recovered_after_SIGCONT": recovered is True,
        "same_step_is_a_crash_once_killed": crash["outcome"] == "crash",
        "pid_gone_after_the_kill": not os.path.exists("/proc/%d" % pid),
    }
    return {"proof": "h: hang vs crash", "run_dir": run_dir, "pid": pid, "hang": hang,
            "crash": crash, "checks": checks, "passed": all(checks.values())}


PROOFS = {"a": proof_raise, "a2": proof_sigterm, "b": proof_sigkill, "d": proof_orphan_check,
          "h": proof_hang_control}


def main(argv=None):
    parser = argparse.ArgumentParser(description="crashbot reaper selftest")
    parser.add_argument("--proof", default="all", choices=["all"] + sorted(PROOFS))
    args = parser.parse_args(argv)
    os.makedirs(RUN_ROOT, exist_ok=True)
    wanted = sorted(PROOFS) if args.proof == "all" else [args.proof]
    results = []
    for key in wanted:
        definition = PROOFS[key]
        run_dir = fresh({"a": "a-raise", "a2": "a2-sigterm", "b": "b-sigkill",
                         "d": "d-orphan", "h": "h-hang"}[key])
        started = time.time()
        outcome = definition(run_dir)
        outcome["seconds"] = round(time.time() - started, 2)
        results.append(outcome)
        print("\n=== proof %s: %s (%.1fs) ===" % (key, outcome["proof"], outcome["seconds"]))
        for name, held in outcome["checks"].items():
            print("   %-40s %s" % (name, "ok" if held else "FAILED"))
        print("   verdict: %s" % ("PASS" if outcome["passed"] else "FAIL"))
    summary = {"at": P.utc_now(), "binary": P.binary_identity(BINARY),
               "results": results, "passed": all(item["passed"] for item in results)}
    with open(os.path.join(RUN_ROOT, "summary.json"), "w") as handle:
        json.dump(summary, handle, indent=2, default=str)
    print("\n%s (%d proof(s)) -> %s/summary.json"
          % ("PASS" if summary["passed"] else "FAIL", len(results), RUN_ROOT))
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
