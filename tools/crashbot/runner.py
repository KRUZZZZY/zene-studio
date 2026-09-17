#!/usr/bin/env python3
"""Crashbot scenario runner v0 — drive N instances through scenario files, typed outcomes.

L2 of verification/AGENT-CRASH-TESTING-PLAN.md. `pool.py` owns the processes; this module owns
boot -> drive -> collect -> reap for N instances x a scenario set and never launches a process
itself. The schema, its references and its budget rule live in `scenario.py`.

Three rules are the whole point of the module, each enforced in code rather than hoped for:
ONE CASE PER INSTANCE because `control.undo` reverses the LAST recorded transaction (IR-21);
BOUNDS ARE PER-COMMAND DECLARED BUDGETS with the render class at 180 s and every other command
on the tight socket bound, never a health probe inside a render's dead window (IR-15,
docs/RENDER-CHILD-WAIT.md); and HANG IS NEVER A CRASH - a hang is a budget fact about a LIVE
process, a crash is a dead one (IR-17). `README.md` states the reasoning at length.

Usage:
  python3 tools/crashbot/runner.py --scenarios tools/crashbot/scenarios --instances 2 \\
      --run-dir /tmp/crashbot-runs/smoke-001 --binary <tree>/build/zene

Exit codes: 0 every case reached its end; 1 a case recorded a crash/hang/mismatch/not_run;
2 usage or schema error; 3 the run aborted (injected fault or an unhandled worker error).
"""

import argparse
import json
import os
import sys
import threading
import time

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import pool as P          # noqa: E402  (the pool owns the processes)
import scenario as S      # noqa: E402  (the schema, the references, the bounds)

FIXTURE_DIR = os.path.join(P.TREE, "tools", "mcp-zene-control", "tests", "data")
DEFAULT_CASE_CAP_S = 120.0
DEFAULT_RUN_CAP_S = 900.0
TERMINAL_BAD = ("crash", "hang", "mismatch", "cap_exceeded", "schema_refused", "not_run")


# ---------------------------------------------------------------------------
# one call, and its typed outcome
# ---------------------------------------------------------------------------


def call_command(session, cmd, args, budget):
    call = {"cmd": cmd, "args": args, "budget_s": budget, "reply": None, "error": None,
            "timeout": False, "seconds": None}
    started = time.perf_counter()
    try:
        call["reply"] = session["client"].call(next(session["ids"]), cmd, args or {},
                                               timeout=budget, transcript=session["transcript"])
    except (P.H.Timeout, P.H.Blocked, OSError, ValueError) as error:
        call["timeout"] = True
        call["error"] = "%s: %s" % (type(error).__name__, str(error).splitlines()[0][:200])
    call["seconds"] = round(time.perf_counter() - started, 3)
    return call


def classify(call, session):
    """crash if the process is gone, hang if it is alive - never the same outcome (IR-17)."""
    if call["timeout"]:
        if not session["instance"].alive():
            return "crash", None, "the process died while the call was outstanding (%s)" % call["error"]
        return "hang", None, call["error"]
    reply = call["reply"] or {}
    if not session["instance"].alive():
        return "crash", None, "the process died after answering %s" % call["cmd"]
    if reply.get("ok") is True:
        return "ok", None, None
    error = reply.get("error") or {}
    return "refusal", error.get("kind"), error.get("message")


def read_back(session, step, env):
    """Run the step's read-back command, bounded by ITS OWN class bound (never a probe)."""
    if not step.get("read_back"):
        return None, None
    cmd = step["read_back"]
    if cmd not in session["registry"]:
        return {"reply": None, "seconds": None, "timeout": False,
                "error": "%s is not in this build's live registry" % cmd}, None
    call = call_command(session, cmd, S.resolve_value(step.get("read_back_args") or {}, env),
                        S.effective_budget({"cmd": cmd}))
    if call["timeout"] or not (call["reply"] or {}).get("ok"):
        return call, None
    return call, (call["reply"].get("result") or {})


def new_outcome(step):
    return {"index": step["index"], "cmd": step["cmd"], "budget_s": S.effective_budget(step),
            "outcome": None, "error_kind": None, "detail": step.get("note"), "seconds": None,
            "read_back": None, "mismatches": []}


def guard_step(session, step, caps, outcome):
    """The pre-call refusals. True when the step is already decided (a typed outcome is set)."""
    if session["stopped"]:
        outcome["outcome"] = "not_run"
        outcome["detail"] = session["stopped"]
        return True
    if time.time() > min(caps["case_deadline"], caps["run_deadline"]):
        session["stopped"] = ("the runner's own wall-clock cap expired (case %.0fs / run %.0fs)"
                              % (caps["case_cap_s"], caps["run_cap_s"]))
        outcome["outcome"] = "cap_exceeded"
        outcome["detail"] = session["stopped"]
        return True
    if step["cmd"] not in session["registry"]:
        outcome["outcome"] = "schema_refused"
        outcome["detail"] = "%s is not in this build's live registry (%d ids)" \
                            % (step["cmd"], len(session["registry"]))
        return True
    return False


def resolve_args(step, env, outcome):
    """Resolve the step's args (references included). False when the schema refused them."""
    try:
        return S.resolve_value(step.get("args") or {}, env)
    except S.SchemaError as error:
        outcome["outcome"] = "schema_refused"
        outcome["detail"] = str(error)
        return None


def finish_expectations(step, call, outcome, env):
    """Compare the declared expectations, then bind what the step named."""
    own = (call["reply"] or {}).get("result") or {}
    target = own
    if step.get("read_back"):
        target = ((outcome["read_back"] or {}).get("reply") or {}).get("result") or {}
    outcome["mismatches"] = (S.expectation_problems(step, target, env, "expect")
                             + S.expectation_problems(step, own, env, "expect_call"))
    outcome["mismatches"].extend(S.bindings_for(step, own, env))
    if outcome["mismatches"]:
        outcome["outcome"] = "mismatch"


def do_read_back(session, step, env, outcome):
    """Run and compare the step's read-back. True when the step is decided by its outcome."""
    call_back, result = read_back(session, step, env)
    outcome["read_back"] = {"cmd": step["read_back"], "reply": call_back["reply"],
                            "seconds": call_back["seconds"]}
    if result is not None:
        return False
    if not session["instance"].alive():
        outcome["outcome"] = "crash"
        outcome["detail"] = "the process died during read_back %s" % step["read_back"]
    elif call_back["timeout"]:
        outcome["outcome"] = "hang"
        outcome["detail"] = call_back["error"]
    else:
        outcome["outcome"] = "mismatch"
        outcome["mismatches"] = ["read_back %s refused: %r"
                                 % (step["read_back"], (call_back["reply"] or {}).get("error"))]
    return True


def run_step(session, step, env, caps):
    """One step -> one typed outcome. Never raises: a fault here is a recorded outcome."""
    outcome = new_outcome(step)
    if guard_step(session, step, caps, outcome):
        return outcome
    args = resolve_args(step, env, outcome)
    if args is None:
        return outcome
    call = call_command(session, step["cmd"], args, S.effective_budget(step))
    outcome["seconds"] = call["seconds"]
    outcome["reply"] = call["reply"]
    outcome["outcome"], outcome["error_kind"], outcome["detail"] = classify(call, session)
    if outcome["outcome"] in ("crash", "hang"):
        return outcome
    if step.get("expect_error") and outcome["error_kind"] != step["expect_error"]:
        outcome["outcome"] = "mismatch"
        outcome["mismatches"] = ["expect_error: got %r, want %r"
                                 % (outcome["error_kind"], step["expect_error"])]
        return outcome
    if step.get("read_back") and do_read_back(session, step, env, outcome):
        return outcome
    if outcome["outcome"] == "ok":
        finish_expectations(step, call, outcome, env)
    return outcome


# ---------------------------------------------------------------------------
# one case on one ready instance
# ---------------------------------------------------------------------------


def fetch_registry(rec, transcript, case_id):
    """The live registry, once per case: a step whose id is absent is a schema refusal."""
    try:
        listed = rec.client.call(1, "control.commands_list", timeout=S.SOCKET_BOUND_S,
                                 transcript=transcript)
        return {entry["id"] for entry in ((listed.get("result") or {}).get("commands") or [])}
    except (P.H.Timeout, OSError) as error:
        transcript.add("<-", "control.commands_list for %s failed: %s" % (case_id, error))
        return set()


def terminal_outcome(steps):
    for step in steps:
        if step["outcome"] in TERMINAL_BAD:
            return step["outcome"]
    return "ok"


def run_case(pool, name, scenario, run_dir, caps, step_pause_s=0.0, kill_plan=None, fault=None):
    """Drive ONE already-ready instance through ONE scenario. No boot, no reap, no pkill."""
    rec = pool.instances[name]
    directory = os.path.join(run_dir, "cases", scenario["id"].replace("/", "__"), name)
    os.makedirs(directory, exist_ok=True)
    env = {"bindings": {}, "work_dir": os.path.join(directory, "work"),
           "fixture_dir": FIXTURE_DIR, "fixtures_used": {}}
    os.makedirs(env["work_dir"], exist_ok=True)
    transcript = P.H.Transcript()
    session = {"client": rec.client, "instance": rec.instance, "transcript": transcript,
               "registry": fetch_registry(rec, transcript, scenario["id"]),
               "ids": iter(range(2, 100000)), "stopped": None}
    caps = dict(caps)
    caps["case_deadline"] = time.time() + caps["case_cap_s"]
    case = {"case": scenario["id"], "scope": scenario["scope"], "seed": scenario["seed"],
            "instance": name, "pid": rec.pid, "surface_ids": len(session["registry"]),
            "groups": scenario.get("groups"), "notes": scenario.get("notes"), "steps": [],
            "counts": {}, "fixtures": {}, "deliberate_kill": None}
    for number, step in enumerate(scenario["steps"], start=1):
        step = dict(step, index=number)
        if fault and fault["case"] == scenario["id"] and fault["step"] == step["index"]:
            raise RuntimeError("fault injected at %s step %d (the reaper's negative control)"
                               % (scenario["id"], step["index"]))
        if kill_plan and kill_plan["step"] == step["index"]:
            case["deliberate_kill"] = pool.sigkill(
                name, reason="deliberate SIGKILL, acceptance proof (b): %s step %d"
                             % (scenario["id"], step["index"]))
        outcome = run_step(session, step, env, caps)
        case["steps"].append(outcome)
        case["counts"][outcome["outcome"]] = case["counts"].get(outcome["outcome"], 0) + 1
        if outcome["outcome"] in ("crash", "hang", "cap_exceeded"):
            session["stopped"] = ("stopped after a %s on step %d"
                                  % (outcome["outcome"], step["index"]))
        if step_pause_s:
            time.sleep(step_pause_s)
    case["fixtures"] = env["fixtures_used"]
    case["terminal"] = terminal_outcome(case["steps"])
    with open(os.path.join(directory, "outcome.json"), "w") as handle:
        json.dump(case, handle, indent=2, default=str)
    with open(os.path.join(directory, "transcript.txt"), "w") as handle:
        handle.write("\n".join(transcript.lines))
    case["dir"] = directory
    return case


# ---------------------------------------------------------------------------
# a wave: one instance per scenario, then reap
# ---------------------------------------------------------------------------


def worker(pool, name, scenario, run_dir, caps, results, lock, kill_plan, fault):
    """One instance's thread. A death ends THIS instance's case, never the wave."""
    try:
        case = run_case(pool, name, scenario, run_dir, caps, caps.get("step_pause_s", 0.0),
                        kill_plan, fault)
        deaths = pool.collect_deaths(detail="while driving case %s" % scenario["id"])
        if deaths:
            case["terminal"] = "crash"
            case["death"] = deaths
        with lock:
            results[name] = {"case": case, "fault": None}
    except Exception as error:                    # a worker fault must reach the main thread
        with lock:
            results[name] = {"case": None, "fault": "%s: %s" % (type(error).__name__, error)}


def record_wave(pool, cases, ledger):
    """The wave's evidence into the run ledger: pids, terminals, artifacts, reaping."""
    ledger["waves"][-1].update(
        {"run_json": pool.ledger_path, "pids": pool.pids(),
         "cases": [case["case"] for case in cases],
         "terminals": {case["case"]: case["terminal"] for case in cases},
         "artifacts": pool.artifacts, "deliberate_kills": pool.deliberate,
         "reaping": pool.reaping})


def run_wave(pool, wave, run_dir, caps, kill_plan, fault_plan, ledger):
    """Boot, drive one case per instance in threads, reap. Returns the wave's cases."""
    with pool:
        pool.boot()
        results, lock = {}, threading.Lock()
        threads = [threading.Thread(target=worker,
                                    args=(pool, name, scenario, run_dir, caps, results, lock,
                                          kill_plan.get(name, {}).get(scenario["id"]), fault_plan),
                                    name="w-%s" % name)
                   for name, scenario in zip(pool.names, wave)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        cases = [results[name]["case"] for name in pool.names
                 if (results.get(name) or {}).get("case")]
        faults = [value["fault"] for value in results.values() if value.get("fault")]
        record_wave(pool, cases, ledger)
        if faults:
            # Raised INSIDE the with-block on purpose: the abort path then runs through the
            # pool's own context exit, so a mid-run raise reaps by construction and the reaping
            # record names the exception that caused it.
            raise RuntimeError("worker fault: %s" % "; ".join(faults))
    return cases


def coverage_of(cases):
    commands, kinds, scenarios = set(), {}, set()
    for case in cases:
        scenarios.add(case["case"])
        for step in case["steps"]:
            if step["outcome"] != "not_run":
                commands.add(step["cmd"])
            if step["error_kind"]:
                kinds[step["error_kind"]] = kinds.get(step["error_kind"], 0) + 1
    return {"distinct_scenario_ids": len(scenarios), "scenario_ids": sorted(scenarios),
            "distinct_command_ids": len(commands), "commands": sorted(commands),
            "error_kinds": kinds}


def tally(cases):
    counts = {outcome: 0 for outcome in S.OUTCOMES}
    for case in cases:
        for step in case["steps"]:
            counts[step["outcome"]] = counts.get(step["outcome"], 0) + 1
    counts["cases"] = len(cases)
    counts["cases_reached_end"] = sum(1 for case in cases if case["terminal"] == "ok")
    return counts


def write_ledger(path, ledger, cases, pool):
    keys = ("case", "instance", "pid", "terminal", "dir", "seed", "scope", "surface_ids",
            "deliberate_kill")
    ledger["cases"] = [dict({key: case[key] for key in keys}, counts=case["counts"])
                       for case in cases]
    ledger["outcomes"] = tally(cases)
    ledger["coverage"] = coverage_of(cases)
    ledger["finished_at"] = P.utc_now()
    if pool is not None:
        ledger.update({"pids": pool.pids(), "reaping": pool.reaping, "artifacts": pool.artifacts,
                       "orphan_check_after": P.orphan_check([], label="after the wave")})
    with open(path + ".tmp", "w") as handle:
        json.dump(ledger, handle, indent=2, default=str)
    os.replace(path + ".tmp", path)


def report(ledger):
    outcomes = ledger["outcomes"]
    print("\n=== crashbot run %s ===" % ledger["run_id"])
    print("  cases: %d (%d reached their end)"
          % (outcomes["cases"], outcomes["cases_reached_end"]))
    print("  steps: %s" % {key: value for key, value in outcomes.items()
                           if key not in ("cases", "cases_reached_end") and value})
    print("  coverage: %d distinct scenario id(s), %d distinct command id(s), refusal kinds %s"
          % (ledger["coverage"]["distinct_scenario_ids"],
             ledger["coverage"]["distinct_command_ids"],
             json.dumps(ledger["coverage"]["error_kinds"])))
    for wave in ledger["waves"]:
        if wave.get("deliberate_kills"):
            print("  deliberate kills: %s" % json.dumps(wave["deliberate_kills"]))
    for case in ledger["cases"]:
        print("  %-34s %-4s %-8s %s" % (case["case"], case["instance"], case["terminal"],
                                        json.dumps(case["counts"])))
    print("  ledger -> %s" % os.path.join(ledger["run_dir"], "ledger.json"))
    return 0 if all(case["terminal"] == "ok" for case in ledger["cases"]) else 1


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def parse_kill(spec, wave, names):
    """`--sigkill INSTANCE:CASE:STEP` — the acceptance (b) hook, by exact PID only."""
    plan = {"kills": [], "per": {}}
    if not spec:
        return plan
    parts = spec.split(":")
    ids = [scenario["id"] for scenario in wave]
    if len(parts) != 3:
        raise S.SchemaError("--sigkill wants INSTANCE:CASE:STEP, got %r" % spec)
    name, case_id, step = parts[0], parts[1], int(parts[2])
    if name not in names or case_id not in ids:
        raise S.SchemaError("--sigkill %r: this wave has instances %s and cases %s"
                            % (spec, names, ids))
    # The instance must be the one that runs that case: a plan pointing at the wrong pair would
    # silently kill nothing, and a proof that kills nothing proves nothing.
    runner_of_case = names[ids.index(case_id)]
    if runner_of_case != name:
        raise S.SchemaError("--sigkill: %s runs %s in this wave, not %s"
                            % (runner_of_case, case_id, name))
    plan["per"][name] = {case_id: {"step": step, "instance": name}}
    plan["kills"].append({"instance": name, "case": case_id, "step": step,
                          "mode": "deliberate SIGKILL by exact PID"})
    return plan


def parse_fault(spec, wave):
    if not spec:
        return None
    case_id, step = spec.split(":")
    if case_id not in [scenario["id"] for scenario in wave]:
        raise S.SchemaError("--inject-fault-at names case %s but this wave runs %s"
                            % (case_id, [scenario["id"] for scenario in wave]))
    return {"case": case_id, "step": int(step)}


def build_parser():
    parser = argparse.ArgumentParser(description="crashbot scenario runner v0")
    parser.add_argument("--scenarios", nargs="+", required=True,
                        help="scenario files and/or directories of them")
    parser.add_argument("--instances", type=int, default=1)
    parser.add_argument("--binary", default=P.DEFAULT_BINARY)
    parser.add_argument("--run-dir", default=None)
    parser.add_argument("--case-cap-s", type=float, default=DEFAULT_CASE_CAP_S)
    parser.add_argument("--run-cap-s", type=float, default=DEFAULT_RUN_CAP_S)
    parser.add_argument("--step-pause-s", type=float, default=0.0)
    parser.add_argument("--sigkill", default=None,
                        help="INSTANCE:CASE:STEP - deliberate SIGKILL (acceptance proof b)")
    parser.add_argument("--inject-fault-at", default=None,
                        help="CASE:STEP - fault injection for the reaper's negative control")
    return parser


def new_ledger(run_dir, args):
    return {"kind": "crashbot-scenario-run", "schema_version": 0,
            "run_id": os.path.basename(run_dir), "started_at": P.utc_now(), "run_dir": run_dir,
            "budgets": {"render_commands": list(S.RENDER_COMMANDS),
                        "render_budget_s": S.RENDER_BUDGET_S, "socket_bound_s": S.SOCKET_BOUND_S,
                        "case_cap_s": args.case_cap_s, "run_cap_s": args.run_cap_s},
            "waves": [], "faults": [], "cases": [], "coverage": {}, "outcomes": {}}


def main(argv=None):
    args = build_parser().parse_args(argv)
    run_dir = os.path.abspath(args.run_dir or os.path.join(
        P.DEFAULT_SCRATCH, "run-%s" % time.strftime("%Y%m%d-%H%M%S")))
    os.makedirs(run_dir, exist_ok=True)
    ledger_path = os.path.join(run_dir, "ledger.json")
    ledger = new_ledger(run_dir, args)
    pool, cases = None, []
    try:
        scenarios = S.collect_scenarios(args.scenarios, FIXTURE_DIR)
        instances = max(1, min(args.instances, P.MAX_INSTANCES))
        ledger["scenarios"] = [{"id": item["id"], "scope": item["scope"], "seed": item["seed"],
                                "steps": len(item["steps"]), "groups": item.get("groups")}
                               for item in scenarios]
        ledger["binary"] = P.binary_identity(args.binary)
        if ledger["binary"].get("version_exit") != 0:
            raise S.SchemaError("the binary does not run: %s" % ledger["binary"]["version"])
        run_deadline = time.time() + args.run_cap_s
        caps = {"case_cap_s": args.case_cap_s, "run_cap_s": args.run_cap_s,
                "run_deadline": run_deadline, "step_pause_s": args.step_pause_s}
        for index in range(0, len(scenarios), instances):
            wave = scenarios[index:index + instances]
            names = ["i%d" % slot for slot in range(len(wave))]
            kill_plan = parse_kill(args.sigkill, wave, names)
            pool = P.InstancePool(names=names, binary=args.binary,
                                  run_dir=os.path.join(run_dir, "waves", "w%d" % (1 + index // instances)),
                                  run_id="%s-w%d" % (ledger["run_id"], 1 + index // instances))
            P.register(pool)
            ledger["waves"].append({"wave": pool.run_id, "run_json": pool.ledger_path,
                                    "declared_kills": kill_plan["kills"]})
            cases.extend(run_wave(pool, wave, run_dir, caps, kill_plan["per"],
                                  parse_fault(args.inject_fault_at, wave), ledger))
            write_ledger(ledger_path, ledger, cases, pool)
    except (RuntimeError, S.SchemaError) as error:
        kind = "schema" if isinstance(error, S.SchemaError) else "abort"
        ledger["faults"].append({"at": P.utc_now(), "kind": kind,
                                 "error": "%s: %s" % (type(error).__name__, error)})
        if pool is not None:
            pool.file_artifact("run_aborted", detail=ledger["faults"][-1]["error"],
                               outcome={"faults": ledger["faults"]})
            pool.reap("run aborted: %s" % kind)
        write_ledger(ledger_path, ledger, cases, pool)
        print("%s: %s" % (kind.upper(), error))
        print("ledger -> %s" % ledger_path)
        return 2 if kind == "schema" else 3
    write_ledger(ledger_path, ledger, cases, pool)
    return report(ledger)


if __name__ == "__main__":
    sys.exit(main())
