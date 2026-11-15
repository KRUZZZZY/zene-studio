#!/usr/bin/env python3
"""Sol train unblocker (owner 2026-11-03): "use like a gpt 6.1 sol queue to unblock the train, and have it monitored and
pinged every time the train is down, probably use medium" + "make sure it uses the same gpt model over and over again,
and have compaction at like 254k tokens on for that session".

merge_train.fail() calls note_failure() on EVERY unpromoted train (merge conflicts and postponements excepted). The
failure is queued (merge/SOL-QUEUE.jsonl) and one worker (transient unit zene-train-sol) works the queue:

  - always GPT 6.1 Sol, medium effort, in ONE persistent Codex session (merge/SOL-SESSION.json holds its thread id;
    every failure resumes it, so Sol keeps what it learned), with auto-compaction at 254k tokens;
  - a newer failure supersedes queued older ones (each train is rebuilt from scratch, only the latest matters);
  - Sol edits and builds in a fresh preserved worktree for each failure. Its runner commits the edits, reruns
    targeted checks in the jail, and queues ONLY the repair delta for the next train; failed checks go back to the
    same persistent session for correction. The full merged test bar and gates remain required for promotion.
  - `sol-test` can also diagnose the failed train's binaries (8 GB cap, holds the train lock);
  - its verdict can take merged branches off the train (each gets a port job, exactly like the train doctor) and mark
    queued bug clusters urgent (URGENT-BUGS.txt, which the bug fixers take first). With an exclusion the train is
    restarted at once. Promotion still needs the full bar and gates: nothing here relaxes them.
  - every step pings the owner (notify-me): Sol started, its verdict, or why it could not run.

While this queue is on, the train doctor's model ladder is not started (one diagnosis per failure). Off switch:
qwen/SOL-QUEUE-OFF (the doctor ladder takes over again).
  train_sol.py status | run | kick
"""
import fcntl
import json
import reset_reserve
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
import merge_train as mt          # noqa: E402
import prompt_context
import train_doctor as td         # noqa: E402  (merges, excerpt, named_files, git, luna_ok)

OFF = ROOT / "SOL-QUEUE-OFF"
QUEUE = mt.M / "SOL-QUEUE.jsonl"
STATE = mt.M / "SOL.json"
SESSION = mt.M / "SOL-SESSION.json"
LOCK = mt.M / "sol.lock"
RUNS = mt.M / "sol"
WT = mt.PROG / "zene-train-sol"
TRAIN_WT = mt.PROG / "zene-cachyos"
SCRATCH = Path.home() / ".cache/zene-tmp/sol"
JAIL = mt.PROG / "tools/haiku/jail.py"
MODEL = "gpt-6.1-sol"
EFFORT = "medium"
COMPACT_AT = 254000
TOOL_OUTPUT_TOKENS = 2500
TIMEOUT_S = 90 * 60
MAX_EXCLUDE = 3
UNIT = "zene-train-sol"
REPAIRS = mt.M / "SOL-REPAIRS.json"
REPAIR_LOCK = mt.M / "sol-repairs.lock"
REPAIR_TREES = mt.PROG / "zene-sol-repairs"
GATE_CHECKS = {"tests/no-upstream-regression-gate.sh", "tests/evidence-gate.sh",
               "tests/fork-sources-gate.sh", "tests/no-tautology-gate.sh", "tests/unregistered-tests-gate.sh"}
METADATA = {"tests/upstream-modifications.txt", "tests/fork-sources.txt",
            "tests/evidence-gate-exempt.txt", "tests/all-sources.txt"}
NO_PUSH = {"GIT_CONFIG_COUNT": "2",
           "GIT_CONFIG_KEY_0": "url.no-push-from-sol-jail:///.pushInsteadOf", "GIT_CONFIG_VALUE_0": "https://github.com/",
           "GIT_CONFIG_KEY_1": "url.no-push-from-sol-jail:///.pushInsteadOf", "GIT_CONFIG_VALUE_1": "git@github.com:"}


def enabled():
    return not OFF.exists()


def load(path, default):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return default


def save(path, data):
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(data, indent=1) + "\n")
    tmp.replace(path)


def log(msg):
    mt.log(f"sol: {msg}")


def worker_active():
    return subprocess.run(["systemctl", "--user", "is-active", "--quiet", UNIT]).returncode == 0


def kick():
    """Start the worker unless it runs already or nothing is queued."""
    if not QUEUE.exists() or not QUEUE.read_text().strip() or worker_active():
        return False
    subprocess.run(["systemctl", "--user", "reset-failed", UNIT], capture_output=True)
    r = subprocess.run(["systemd-run", "--user", f"--unit={UNIT}", "--collect",
                        "--description=Zene train unblocker (GPT 6.1 Sol, one persistent session)",
                        "--property=MemoryMax=10G", f"--setenv=PATH={Path.home()}/.local/bin:{Path.home()}/.hermes/node/bin:/usr/local/bin:/usr/bin:/bin",
                        sys.executable, str(Path(__file__).resolve()), "run"], capture_output=True, text=True)
    return r.returncode == 0


def note_failure(new, base, why):
    """Called by merge_train.fail() for every unpromoted train. Never raises."""
    try:
        if "merge conflict" in why or "postponed" in why:
            return
        with QUEUE.open("a") as f:
            f.write(json.dumps({"train": new, "base": base, "why": why, "at": time.strftime("%Y-%m-%dT%H:%M:%S")}) + "\n")
        started = kick()
        log(f"{new} queued for Sol" + ("; worker started" if started else "; the running worker takes it"))
    except Exception as e:          # the train's own failure handling must never break on this
        mt.log(f"sol: could not queue ({e!r})")


def note_postponement(base, why, hours):
    """Owner 2026-11-13: the watcher hands a train postponed for many hours to Sol (a postponement is not a failure,
    so note_failure never sees it; Terra's gate kept train 28 from running for a day). Never raises."""
    try:
        number = int(re.search(r"train-(\d+)$", base).group(1)) + 1
        job = {"train": f"050/m0-train-{number}", "base": base, "kind": "postponed",
               "why": f"the train has been postponed for {hours:.0f} h: {why}", "at": time.strftime("%Y-%m-%dT%H:%M:%S")}
        with QUEUE.open("a") as f:
            f.write(json.dumps(job) + "\n")
        started = kick()
        log(f"{job['train']} postponed {hours:.0f} h; queued for Sol" + ("; worker started" if started else ""))
    except Exception as e:
        mt.log(f"sol: could not queue a postponement ({e!r})")


def postponed_brief(job, n, history):
    import terra_priority
    state = terra_priority.snapshot(job["base"])
    tail = "\n".join(mt.LOG.read_text(errors="replace").splitlines()[-25:]) if mt.LOG.exists() else ""
    prev = "\n".join(f"- {h['at']} {h['train']}: {h['result']}" for h in history[-8:]) or "- (none)"
    terra = {k: state.get(k) for k in ("open", "waiting", "swept_failed", "exclusive")}
    terra["required"] = [r.get("cluster") for r in state.get("required") or []]
    return f"""Train case #{n} for you in this session - a different kind: NOT a failed train but one that never starts.
The automatic merge train keeps postponing the next train after {job['base']}:

  {job['why']}

Find out why it cannot start and get it moving within the rules. Diagnose from the pipeline's own state; do not build.
- Merge-train orchestration (last lines of {mt.LOG}):
```
{tail}
```
- Terra priority state (qwen/terra_priority.py snapshot): {json.dumps(terra)}
  Sweep markers: {ROOT / 'terra-sweeps'}; sweep summaries: {terra_priority.PILOT}/<buttons|session>/runs/<train>/SUMMARY.md.
- Focused-integration cohort: qwen/integration_jobs.py (unsettled integrations hold the train up to a few hours each).
- Merge train code: {ROOT / 'merge_train.py'} (run(): the postponement branches), {ROOT / 'terra_priority.py'} (gate()).
- LUNA WORKERS (owner 2026-11-13: you lead, GPT 6 Luna workers do the legwork): `{ROOT}/luna-task --cwd <dir>
  [--write] --label <name> "<task>"` starts a fresh Luna session for ONE self-contained task and prints its short
  report. Hand it the mechanical work - reading and summarising long logs, bisecting, running sol-test or a targeted
  build, searching the tree, drafting a repair in your checkout (--write; it never commits) - and keep your own
  context for judgement and the verdict. Each worker has NO memory: put every path, command and fact it needs in the
  task. Two run at once (run them in the background with & and `wait` to parallelise); exit 75 means Luna is paused
  or busy - then do it yourself. Check a worker's claims before you rely on them.
You may mark bug clusters urgent so the fixers take them first. You may NOT change gates, tests, pipeline code or
limits, push, or edit any worktree: if the way out needs one of those, say exactly what the owner must decide.

## Your earlier verdicts in this session
{prev}

## Answer
End your reply with exactly one fenced json block and nothing after it:
```json
{{"urgent": [{{"cluster": "...", "why": "one sentence"}}],
  "owner_decision": "what the owner must decide or change, or empty if nothing",
  "summary": "two sentences for the owner: why the train cannot start and what happens next"}}
```"""


def handle_postponed(job, history):
    n = len(history) + 1
    if reset_reserve.paused("codex") or not td.luna_ok():
        return "waiting", "the OpenAI (Codex) allowance is paused; queued until it is back"
    RUNS.mkdir(exist_ok=True)
    out = RUNS / f"postponed-{job['train'].split('/')[-1]}-{int(time.time())}.md"
    log(f"Sol (medium) looking at why {job['train']} keeps being postponed")
    text, _thread = session(postponed_brief(job, n, history), out)
    if not text:
        return "failed", f"the Sol session gave no answer (see {out.with_suffix('.log')})"
    v = verdict(text)
    if v is None:
        return "no verdict", f"Sol answered without a usable verdict ({out})"
    urgent = mark_urgent(v.get("urgent") or [])
    decision = str(v.get("owner_decision") or "").strip()[:400]
    summary = str(v.get("summary") or "")[:500]
    mt.notify(f"Sol on the postponed train {job['train']}: {summary}"
              + (f" Marked urgent: {', '.join(urgent)}." if urgent else "")
              + (f" Needs you: {decision}" if decision else ""))
    return "done", (f"marked urgent {', '.join(urgent)}; " if urgent else "") + summary + (f" | owner: {decision}" if decision else "")


# ------------------------------------------------------------------ the brief
def killed_tests(text):
    """Tests that had started but not finished when the bar stopped (a resource kill names no test)."""
    started = re.findall(r"Start\s+\d+: (\S+)", text)
    done = set(re.findall(r"Test\s+#\d+: (\S+)", text))
    return [t for t in started if t not in done]


def brief(job, n, history):
    base, new = job["base"], job["train"]
    m = re.search(r"see (\S+?\.log)", job["why"])
    log_path = Path(m.group(1)) if m else None
    text = log_path.read_text(errors="replace") if log_path and log_path.exists() else ""
    rows = td.merges(base, new)
    files = td.named_files(text, new)
    tail = "\n".join(text.splitlines()[-40:])
    running = killed_tests(text)
    pending_repairs = json.dumps([{"id": r["id"], "summary": r["summary"]} for r in load(REPAIRS, [])
                                  if r.get("status") == "pending"], indent=1)
    options = [line.strip() for line in (ROOT / "FIX-CMAKE-ARGS").read_text().splitlines()
               if line.strip().startswith("-D")]
    wasm = "ON" if "WANT_WASM:BOOL=ON" in (TRAIN_WT / "build/CMakeCache.txt").read_text() else "OFF"
    configure = shlex.join(["cmake", "-S", ".", "-B", "build", "-G", "Ninja", *options,
                            "-DWANT_WASM=" + wasm])
    prev = "\n".join(f"- {h['at']} {h['train']}: {h['result']}" for h in history[-8:]) or "- (none)"
    return f"""Train failure #{n} for you in this session. You are the merge-train unblocker for Zene Studio (an LMMS-derived
C++/Qt6 DAW). This is ONE long-running session: every time the automatic merge train fails, the next failure comes
to you here, so use what you learned from earlier failures (listed below) and do not repeat a verdict that did not help.

## What failed
The train merged accepted branches onto the fix base {base} as {new}, built it and ran the test bar:

  {job['why']}

Tests that were still running when the bar stopped: {', '.join(running) or '(none / not a resource kill)'}.
"TRAIN RESOURCE FAILURE ... rss=N" means one process of the bar went past the train's per-process RSS limit (6 GB),
so the bar was killed: find which test does that and which merged branch made it do so.

## Where things are
- {WT}: your private repair checkout of the failed train. Edit files and build/test HERE. The runner commits your
  edits, verifies the checks you name, and applies only your repair delta to the next selected train. This checkout
  is preserved on failure; never edit any other source worktree.
- Configure your ONE private build with `{configure}`. Build only affected targets using
  `cmake --build build --target zene <AffectedTestTargets> -j12`; stop below 12 GB free. The full train builds all
  targets later. Run ctest FROM build/tests and treat zero matched tests as failure.
- Register changed/new files with reasons; follow AGENTS.md. Add a failing-then-passing regression for behavioral
  changes. Record exact commands/results in a short Markdown report. For metadata-only fixes, run the actual failed
  gates; use their documented declaration/exemption mechanisms and retain all enforcement rules.
- {TRAIN_WT}/build: the FAILED TRAIN's build ({new}'s binaries). Its source checkout was switched back to {base}, so a
  test that reads source files (docs, snapshots) compares train binaries with BASE files - a mismatch of that kind
  is a stale artifact, not the bug. Never edit {TRAIN_WT} sources and never run git there.
- `{ROOT}/sol-test '<ctest -R regex>' [timeout_s]` runs those test binaries one at a time under an 8 GB cap and
  prints the output and the peak RSS per process. Use it to confirm which test blows up (it refuses while a train runs).
- Read-only git in {mt.PROG}/zene-studio: `git log --first-parent {base}..{new}`, `git show <merge>`, `git diff`.
- LUNA WORKERS (owner 2026-11-13: you lead, GPT 6 Luna workers do the legwork): `{ROOT}/luna-task --cwd <dir>
  [--write] --label <name> "<task>"` starts a fresh Luna session for ONE self-contained task and prints its short
  report. Hand it the mechanical work - reading and summarising long logs, bisecting, running sol-test or a targeted
  build, searching the tree, drafting a repair in your checkout (--write; it never commits) - and keep your own
  context for judgement and the verdict. Each worker has NO memory: put every path, command and fact it needs in the
  task. Two run at once (run them in the background with & and `wait` to parallelise); exit 75 means Luna is paused
  or busy - then do it yourself. Check a worker's claims before you rely on them.
- Bug queue clusters live in {mt.PROG}/research/reviews/luna-easy-fix-2026-10-06/queue/easy.jsonl; a failing test
  often already has a cluster named test-<TestName> with a rework in {ROOT}/rework/.

## Rules
OWNER UPDATE 2026-11-05: your job is to APPLY AND VERIFY repairs, not stop at diagnosis. This supersedes earlier
verdict-only instructions in this persistent session. Fix the cause in your private checkout whenever practical;
use branch exclusions when a feature genuinely needs separate integration. Do not keep returning the same diagnosis.
Never push, rewrite history, weaken a test/gate, or change train resource limits. Leave commits and queue writes to
this runner: edit files, build and test, then submit the repair/checks object below. The runner commits explicit
changed paths, reruns checks, queues the repair delta after candidate merges, and restarts the train. Source repairs
require targeted ctest on rebuilt binaries; provenance/exemption repairs require the relevant gate checks. A failed
verification returns to you in the SAME session so you can correct it.

## Error excerpt (from {log_path})
```
{td.excerpt(text) if td.ERROR_RE.search(text) else tail}
```

## Files the error names
{chr(10).join('- ' + f for f in files) or '- (none)'}

## Every merge on {new} (oldest first)
{chr(10).join('- ' + r['branch'] for r in reversed(rows))}

## Pending verified repairs
{pending_repairs}
If a pending repair itself conflicts with the current selection or needs replacing, apply its intended fix in your
checkout and name its ID in repair.supersedes. The runner preserves the old commit and uses your replacement instead.

## Your earlier verdicts in this session
{prev}

## Answer
End your reply with exactly one fenced json block and nothing after it:
```json
{{"repair": {{"summary": "what you fixed (omit repair only if you made no edits)", "supersedes": [],
             "checks": [{{"kind": "gate", "script": "tests/no-upstream-regression-gate.sh"}},
                        {{"kind": "gate", "script": "tests/evidence-gate.sh"}},
                        {{"kind": "ctest", "regex": "^AffectedRegressionTest$", "targets": ["zene", "AffectedRegressionTest"]}}]}},
  "exclude": [{{"branch": "bugs/qwen-...", "why": "one sentence with file:line or sol-test evidence"}}],
  "urgent": [{{"cluster": "test-SomeTest", "why": "one sentence"}}],
  "summary": "two sentences for the owner: what broke the train and what this verdict does"}}
```
"exclude": merged branches (at most {MAX_EXCLUDE}) to take off this train - each gets a port job so its fix is redone on
the newest line, nothing is lost. "urgent": queued bug clusters to fix first. Either list may be empty; if both are,
the summary must identify the exact external blocker if there is also no repair. Remove the ctest check only for
metadata/documentation-only repairs. All gate checks use the repository's unchanged gate executables. The model,
effort and persistent session remain unchanged.
"""


# ------------------------------------------------------------------ the session
def repair_git(*args, cwd=None):
    r = subprocess.run(["git", "-C", str(cwd or WT), *args], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(f"git {' '.join(args)}: {r.stderr[-2000:]}")
    return r.stdout.strip()


def seed_dependencies(target, donor, commit="HEAD"):
    """Populate private source dependencies from local Git objects, without fetching or sharing mutable builds."""
    rows = repair_git("ls-tree", "-r", commit, cwd=target).splitlines()
    for row in rows:
        header, rel = row.split("\t", 1)
        mode, _, revision = header.split()
        if mode != "160000":
            continue
        source = donor / rel
        if not (source / ".git").exists():
            source = td.CLONE / rel
        if not (source / ".git").exists():
            raise RuntimeError(f"no local source checkout for dependency {rel}; repair worktree preserved at {target}")
        destination = target / rel
        subprocess.run(["git", "clone", "--quiet", "--no-checkout", "--shared", str(source), str(destination)],
                       capture_output=True, check=True)
        repair_git("checkout", "--quiet", "--detach", revision, cwd=destination)
        seed_dependencies(destination, source, revision)
    if target == WT:
        for rel in ("build/clap", "build/vst3sdk", "third_party/wasmtime"):
            source, destination = donor / rel, target / rel
            if source.is_dir() and not destination.exists():
                shutil.copytree(source, destination, symlinks=True)


def checkout(new):
    """Each repair owns a new checkout/build; failed and uncommitted work is retained."""
    global WT
    REPAIR_TREES.mkdir(parents=True, exist_ok=True)
    WT = REPAIR_TREES / f"{new.split('/')[-1]}-{time.time_ns()}"
    repair_git("worktree", "add", "--detach", str(WT), new, cwd=td.CLONE)
    seed_dependencies(WT, TRAIN_WT)
    return WT


def jail_prefix():
    admin = repair_git("rev-parse", "--path-format=absolute", "--git-dir")
    rw = [Path.home() / ".codex", RUNS, SCRATCH, Path("/tmp"), WT, TRAIN_WT / "build", Path(admin)]
    argv = [sys.executable, "-W", "ignore", str(JAIL)]
    for path in rw:
        argv += ["--rw", str(path)]
    return argv + ["--"]


def repair_env():
    env = dict(os.environ, **NO_PUSH)
    env.pop("LMMS_PLUGIN_DIR", None)
    deps = Path.home() / ".local/zene-deps"
    env.update(TMPDIR=str(SCRATCH), CCACHE_DIR=str(SCRATCH / "ccache"),
               CCACHE_TEMPDIR=str(SCRATCH / "ccache-tmp"), QT_QPA_PLATFORM="offscreen",
               PKG_CONFIG_PATH=f"{deps}/root/usr/lib/pkgconfig",
               LD_LIBRARY_PATH=f"{deps}/root/usr/lib:{deps}/onnxruntime-linux-x64-1.28.0/lib:"
                               f"{WT}/third_party/wasmtime/lib:" + env.get("LD_LIBRARY_PATH", ""))
    return env


def check_command(check):
    if not isinstance(check, dict):
        raise ValueError("repair checks must be objects")
    if check.get("kind") == "gate" and check.get("script") in GATE_CHECKS:
        return ["bash", check["script"]], WT
    if check.get("kind") == "ctest" and isinstance(check.get("regex"), str) and check["regex"].strip():
        return ["ctest", "-R", check["regex"], "-j1", "--timeout", "900", "--no-tests=error",
                "--output-on-failure"], WT / "build/tests"
    raise ValueError("expected a registered gate check or a nonempty targeted ctest regex")


def verify_repair(repair, paths):
    checks = repair.get("checks") or []
    if not isinstance(checks, list):
        raise ValueError("repair.checks must be a list")
    behavior = [p for p in paths if p not in METADATA and not p.startswith("docs/")]
    if behavior and not any(isinstance(c, dict) and c.get("kind") == "ctest" for c in checks):
        raise ValueError("source repairs require a rebuilt targeted ctest, including a regression for changed behavior")
    checks = [{"kind": "gate", "script": script} for script in
              ("tests/no-upstream-regression-gate.sh", "tests/evidence-gate.sh")] + checks
    proof, seen = [], set()
    def run_check(argv, cwd):
        log_path = RUNS / f"repair-check-{time.time_ns()}.log"
        with log_path.open("w") as stream:
            r = subprocess.run(jail_prefix() + argv, cwd=cwd, env=repair_env(), stdout=stream,
                               stderr=subprocess.STDOUT, timeout=1800)
        proof.append({"argv": argv, "cwd": str(cwd), "exit": r.returncode, "log": str(log_path)})
        if r.returncode:
            raise RuntimeError(f"repair verification failed: {argv}, exit {r.returncode}; "
                               f"{log_path}\n{log_path.read_text(errors='replace')[-6000:]}")

    for check in checks:
        argv, cwd = check_command(check)
        key = (tuple(argv), str(cwd))
        if key in seen:
            continue
        seen.add(key)
        if check.get("kind") == "ctest":
            targets = check.get("targets")
            if not isinstance(targets, list) or not targets or any(
                    not isinstance(t, str) or not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.+-]*", t) for t in targets):
                raise ValueError("ctest repair checks must name the affected build targets, including the test binary")
            run_check(["cmake", "--build", str(WT / "build"), "--target", *targets, "-j12"], WT)
        run_check(argv, cwd)
    return proof


def queue_repair(job, repair, source_head):
    if not isinstance(repair, dict) or not str(repair.get("summary") or "").strip():
        raise ValueError("repair needs a summary and verification checks")
    repair_git("diff", "--check")
    paths = set(repair_git("diff", "--name-only", source_head).splitlines())
    paths.update(repair_git("ls-files", "--others", "--exclude-standard").splitlines())
    if not paths:
        raise ValueError("repair requested but no changed files were produced")
    # Gate declarations/exemptions may be repaired through their documented inputs;
    # gate executables and ratchets retain their existing enforcement.
    forbidden = [p for p in paths if p.startswith("tests/") and
                 ("gate" in Path(p).name or "ratchet" in Path(p).name) and p not in METADATA]
    if forbidden:
        raise ValueError(f"repair changes gate enforcement rather than its declared inputs: {forbidden}")
    current = repair_git("status", "--porcelain")
    if current:
        repair_git("add", "--", *sorted(paths))
        repair_git("diff", "--check", source_head)
        repair_git("-c", "user.name=Zene train Sol", "-c", "user.email=noreply@zene.invalid",
                   "commit", "-m", f"fix(train-sol): {str(repair['summary'])[:180]}")
    proof = verify_repair(repair, sorted(paths))
    identity = f"sol-repair-{time.time_ns()}"
    tree = repair_git("rev-parse", "HEAD^{tree}")
    # One commit containing ONLY the repair delta. The failed train's feature
    # merges are context, never imported into the next train by this queue.
    commit = repair_git("-c", "user.name=Zene train Sol", "-c", "user.email=noreply@zene.invalid",
                        "commit-tree", tree, "-p", source_head, "-m",
                        f"fix(train-sol): {str(repair['summary'])[:180]}\n\nSol-Repair-Id: {identity}")
    branch = f"fix/{identity}"
    repair_git("branch", branch, commit)
    base_commit = repair_git("rev-parse", job["base"] + "^{commit}")
    supersedes = repair.get("supersedes") or []
    if not isinstance(supersedes, list) or any(not isinstance(item, str) for item in supersedes):
        raise ValueError("repair.supersedes must list pending repair IDs replaced by this correction")
    row = {"id": identity, "train": job["train"], "base": base_commit, "source_head": source_head,
           "commit": commit, "branch": branch, "worktree": str(WT), "summary": repair["summary"],
           "paths": sorted(paths), "checks": proof, "status": "pending", "at": time.strftime("%Y-%m-%dT%H:%M:%S")}
    with REPAIR_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        rows = load(REPAIRS, [])
        eligible = {r["id"] for r in rows if r.get("status") == "pending" and r.get("base") == base_commit}
        if not set(supersedes).issubset(eligible):
            raise ValueError("supersedes names a repair that is not pending on this base")
        for old in rows:
            if old["id"] in supersedes:
                old.update(status="superseded", superseded_by=identity)
        rows.append(row)
        save(REPAIRS, rows)
    return row


def pending_repairs(base):
    """Verified deltas pinned to this base, shared by scheduling and application."""
    base_commit = mt.git("rev-parse", base + "^{commit}")
    return [row for row in load(REPAIRS, [])
            if row.get("status") == "pending" and row.get("base") == base_commit]


def apply_repairs(base, train):
    """Apply verified deltas after candidate merges; promotion still runs the full bar/gates."""
    for row in pending_repairs(base):
        r = subprocess.run(["git", "-C", str(TRAIN_WT), "cherry-pick", "--keep-redundant-commits", "-x", row["commit"]],
                           capture_output=True, text=True)
        if r.returncode:
            subprocess.run(["git", "-C", str(TRAIN_WT), "cherry-pick", "--abort"], capture_output=True)
            raise RuntimeError(f"Sol repair {row['id']} could not apply to {train}: {(r.stderr or r.stdout)[-1500:]}; "
                               "repair checkout and commit preserved")
        log(f"applied verified repair {row['id']} to {train}")


def promoted_repairs(base, train):
    base_commit = mt.git("rev-parse", base + "^{commit}")
    with REPAIR_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        rows = load(REPAIRS, [])
        for row in rows:
            if row.get("status") == "pending" and row.get("base") == base_commit:
                row.update(status="promoted", promoted_train=train)
        if rows:
            save(REPAIRS, rows)


def delegation_policy():
    return f"""CURRENT SOL/LUNA WORKING POLICY (owner 2026-11-15; supersedes earlier workflow suggestions)
You lead the shared CI/train session: plan, assess evidence and submit the verdict. Use `{ROOT}/luna-task` for
bulk log reading, repository searches, bisects, implementation, builds/tests/gates and mechanical checks.
Give each worker one bounded task with exact paths, relevant evidence, expected result and log location; it has
no prior memory. Keep the existing two-worker limit. Parallelise only independent tasks, use one writer per
checkout, and review an implementation after its diff settles. After a reported fix, inspect its diff and critical
evidence; do not repeat a full audit of unchanged files. Delegate a scoped independent review of the changed
surface. If it finds a defect, request the correction and then check that defect; retain earlier valid evidence.
Do not replace independent review with the implementer's own assertion, and do not omit required tests or gates.

Context discipline applies to YOUR commands as well as workers: run builds, tests, gates and verbose diagnostics
through `{Path.home()}/.local/share/zene-bunny-fix/duet/quiet-run LABEL -- COMMAND [ARGS...]`.
Do not cat complete logs, registry files, patches or worker transcripts into context. Read named line ranges,
error excerpts or short diffs; keep complete evidence on disk and pass paths to workers. Tool output is limited
to {TOOL_OUTPUT_TOKENS} tokens per command, so truncation is not complete evidence: inspect a targeted excerpt.
Keep the working summary short: pinned SHA, failure, current hypothesis, files changed, verified checks, remaining
blocker and evidence paths. Reuse it after compaction; preserve this persistent thread and its {COMPACT_AT}-token
compaction setting. No new broad survey after each handoff. A Luna exit 75 means paused/busy: wait if possible;
if direct work is necessary, use the same bounded scope and quiet runner. A nonzero worker exit is not success,
even when a partial report exists. Existing account/usage guards, issue budgets and release gates still apply.
"""


def codex_cmd(out, thread):
    """Always the same model, effort and compaction; resume the one session when it exists."""
    head = ["codex", "exec", "-c", f'model_reasoning_effort="{EFFORT}"', "-c", f"model_auto_compact_token_limit={COMPACT_AT}",
            "-c", f"tool_output_token_limit={TOOL_OUTPUT_TOKENS}",
            "-c", 'approval_policy="never"', "-s", "danger-full-access", "-C", str(WT)]
    tail = ["-m", MODEL, "--skip-git-repo-check", "--json", "-o", str(out), "-"]
    return head + (["resume", thread] if thread else []) + tail


def session(prompt, out):
    """One turn in the persistent session. Returns (final text, thread id)."""
    # Apply at the final delivery boundary, including corrections and postponed-train turns.
    prompt = prompt_context.stamp(prompt + "\n\n" + delegation_policy())
    SCRATCH.mkdir(parents=True, exist_ok=True)
    sess = load(SESSION, {})
    jail = jail_prefix()[:-1]
    env = repair_env()
    for attempt in ("resume", "fresh") if sess.get("thread") else ("fresh",):
        if reset_reserve.paused("codex"):
            return "", sess.get("thread")
        thread = sess.get("thread") if attempt == "resume" else None
        events = out.with_suffix(".jsonl")
        with events.open("w") as ev, out.with_suffix(".log").open("w") as err:
            try:
                r = subprocess.run(jail + ["--"] + codex_cmd(out, thread), input=prompt, text=True, stdout=ev,
                                   stderr=err, env=env, cwd=str(WT), timeout=TIMEOUT_S)
                rc = r.returncode
            except subprocess.TimeoutExpired:
                rc = 124
        new_thread = thread
        for line in events.read_text(errors="replace").splitlines():
            try:
                e = json.loads(line)
            except ValueError:
                continue
            if e.get("type") == "thread.started" and e.get("thread_id"):
                new_thread = e["thread_id"]
        if reset_reserve.paused("codex"):
            # Keep the existing cached thread; a reserve pause must never create
            # a fresh fallback session or discard the repair checkout.
            if new_thread and not sess.get("thread"):
                save(SESSION, {"thread":new_thread, "model":MODEL, "effort":EFFORT, "turns":0})
            return "", sess.get("thread") or new_thread
        if rc == 0 and out.exists():
            if new_thread != sess.get("thread"):
                sess = {"thread": new_thread, "model": MODEL, "effort": EFFORT, "compact_at": COMPACT_AT,
                        "started": time.strftime("%Y-%m-%dT%H:%M:%S"), "turns": 0}
                log(f"new persistent session {new_thread}")
            sess["turns"] = sess.get("turns", 0) + 1
            sess["last"] = time.strftime("%Y-%m-%dT%H:%M:%S")
            save(SESSION, sess)
            return out.read_text(errors="replace"), new_thread
        if attempt == "resume":
            log(f"session {thread} could not be resumed (exit {rc}); starting a fresh one")
        else:
            log(f"session failed (exit {rc}); see {out.with_suffix('.log')}")
    return "", None


def verdict(text):
    blocks = re.findall(r"```json\s*(\{.*?\})\s*```", text, re.S)
    if not blocks:
        return None
    try:
        v = json.loads(blocks[-1])
    except ValueError:
        return None
    return v if isinstance(v, dict) else None


def mark_urgent(entries):
    import qwenfix
    queued = {str(i["cluster"]) for i in qwenfix.load_queue()}
    have = set(qwenfix.urgent_bugs())
    added = []
    for e in entries:
        c = str((e or {}).get("cluster") or "")
        if c and c in queued and c not in have:
            with qwenfix.URGENT_BUGS.open("a") as f:
                f.write(f"{c}\tSol train unblocker {time.strftime('%Y-%m-%d')}: {str(e.get('why', ''))[:200]}\n")
            added.append(c)
            have.add(c)
    return added


def handle(job, history):
    n = len(history) + 1
    train = job["train"]
    if reset_reserve.paused("codex") or not td.luna_ok():
        return "waiting", "the OpenAI (Codex) allowance is paused; queued until it is back (banked resets redeem automatically)"
    checkout(train)
    source_head = repair_git("rev-parse", "HEAD")
    RUNS.mkdir(exist_ok=True)
    out = RUNS / f"{train.split('/')[-1]}-{int(time.time())}.md"
    log(f"Sol (medium) diagnosing {train} in the persistent session")
    mt.notify(f"Train {train} is down; GPT 6.1 Sol is on it (failure #{n} in its session).")
    text, thread = session(brief(job, n, history), out)
    if not text and reset_reserve.paused("codex"):
        return "waiting", "96% banked-reset reserve: persistent session and repair checkout preserved"
    if not text:
        return "failed", f"the Sol session gave no answer (see {out.with_suffix('.log')})"
    v = verdict(text)
    if v is None:
        return "no verdict", f"Sol answered without a usable verdict ({out})"
    repair_row = None
    if v.get("repair"):
        for attempt in range(3):
            try:
                repair_row = queue_repair(job, v["repair"], source_head)
                break
            except (ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
                if attempt == 2:
                    return "repair failed", f"{error}; work preserved in {WT}"
                log(f"repair verification failed; Sol corrects it in the same session: {str(error)[:500]}")
                text, thread = session(
                    f"Your repair did not pass verification: {error}\nContinue fixing the SAME worktree {WT}. "
                    "The runner may have committed your previous files; keep those and correct the failure. "
                    "Return the same fenced JSON repair/checks format. Do not weaken tests or gates.", out)
                v = verdict(text) or {}
                if not v.get("repair"):
                    return "repair failed", f"no corrected repair submitted; work preserved in {WT}"
    merged = {r["branch"]: r for r in td.merges(job["base"], train)}
    excl = [e for e in v.get("exclude") or [] if isinstance(e, dict) and e.get("branch") in merged][:MAX_EXCLUDE]
    for e in excl:
        b = e["branch"]
        accepted = next((r for r in mt.accepted_rows() if r["branch"] == b), None)
        cluster = accepted["cluster"] if accepted else re.sub(r"^(bugs/qwen-|feat/050-)", "", b).removesuffix("-oc")
        mt.port_job({"cluster": cluster, "branch": b, "commit": td.git("rev-parse", merged[b]["merge"] + "^2")},
                    f"Sol train unblocker: {str(e.get('why', ''))[:300]}", exclude=True)
    urgent = mark_urgent(v.get("urgent") or [])
    summary = str(v.get("summary") or "")[:500]
    parts = []
    if repair_row:
        parts.append(f"verified and queued {repair_row['branch']} ({repair_row['commit'][:12]})")
    if excl:
        parts.append("took " + ", ".join(e["branch"] for e in excl) + " off the train (port job each)")
    if urgent:
        parts.append("marked urgent: " + ", ".join(urgent))
    result = "; ".join(parts) or "no action"
    if excl or repair_row:
        restarted = subprocess.run(["systemctl", "--user", "start", "--no-block", "zene-merge-train.service"], capture_output=True)
        result += "; train restart requested" if restarted.returncode == 0 else "; restart failed; actions remain queued"
    mt.notify(f"Sol on {train}: {result}. {summary}" + ("" if parts else " It needs you."))
    return "done", f"{result} - {summary}"


def run():
    RUNS.mkdir(parents=True, exist_ok=True)
    with LOCK.open("w") as lk:
        try:
            fcntl.flock(lk, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            print("a Sol worker is already running")
            return 0
        st = load(STATE, {"history": []})
        while enabled():
            lines = [l for l in (QUEUE.read_text().splitlines() if QUEUE.exists() else []) if l.strip()]
            if not lines:
                break
            job = json.loads(lines[-1])                 # the newest failure supersedes the older ones
            QUEUE.write_text("")
            if len(lines) > 1:
                log(f"{len(lines) - 1} older queued failure(s) superseded by {job['train']} at {job['at']}")
            st["current"] = job
            save(STATE, st)
            try:
                status, detail = (handle_postponed if job.get("kind") == "postponed" else handle)(job, st["history"])
            except Exception as e:
                status, detail = "error", repr(e)[:300]
            log(f"{job['train']}: {status} - {detail}")
            if status == "waiting":
                with QUEUE.open("a") as f:              # back in the queue; the watcher kicks the worker again
                    f.write(json.dumps(job) + "\n")
                if st.get("waiting_told") != job["at"]:
                    mt.notify(f"Train {job['train']} is down and Sol is waiting: {detail}.")
                    st["waiting_told"] = job["at"]
                st.pop("current", None)
                save(STATE, st)
                break
            if status in ("failed", "no verdict", "error", "repair failed"):
                mt.notify(f"Sol could not unblock {job['train']}: {detail}. It needs you.")
            st["history"].append({"train": job["train"], "at": time.strftime("%Y-%m-%dT%H:%M"),
                                  "status": status, "result": detail})
            st["history"] = st["history"][-50:]
            st.pop("current", None)
            save(STATE, st)
    return 0


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    if cmd == "run":
        sys.exit(run())
    if cmd == "kick":
        print("started" if kick() else "not started (nothing queued or already running)")
        sys.exit(0)
    print(json.dumps({"enabled": enabled(), "worker_active": worker_active(), "session": load(SESSION, {}),
                      "queued": [json.loads(l) for l in (QUEUE.read_text().splitlines() if QUEUE.exists() else []) if l.strip()],
                      "state": load(STATE, {})}, indent=1))
