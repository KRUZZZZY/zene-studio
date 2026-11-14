#!/usr/bin/env python3
"""Human-style GUI session test, run after each promoted train beside the button sweep (owner 2026-10-30: "it should
actually click things, run a session and click things, and see what happens, as close to a human experience as
possible, and if something doesnt work it should raise it").

  session.py run --train NAME [--binary PATH] [--effort medium]

GPT 6.1 Terra works through expected-uses.json in ONE continuous session (state carries over, like a person working)
on the real window (gui.py: Xvnc + xcb, temp HOME, Dummy audio), screenshots after every action. Its verdict per use
is written as results.jsonl / verdicts.jsonl in the run folder in the button sweep's shape, so the FAILs go through
the sweep's own Luna triage (sweep.stage_triage): research/reviews/terra-gui/queue/*.jsonl, and URGENT-BUGS.txt for a
failed core use or a crash. Linux only here; a Windows pass is for big pushes and needs a Windows machine.
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(Path.home() / ".local/share/zene-bunny-fix/qwen"))
import prompt_context
PILOT = HERE.parent
sys.path.insert(0, str(PILOT / "buttons"))
import sweep  # noqa: E402  (shares the queue writer, logging, the OpenAI wait and the URL stubs' idea)

USES = HERE / "expected-uses.json"
FIXTURES = PILOT / "fixtures"
G = f"{PILOT / 'venv/bin/python'} {PILOT / 'gui.py'}"
TIMEOUT = 3600


def prompt_for(uses):
    steps = "\n".join(f"  {k + 1}. [{u['id']}] {u['title']}\n     DO: {u['do'].replace('{FIXTURES}', str(FIXTURES))}"
                      f"\n     EXPECT: {u['expect']}" for k, u in enumerate(uses))
    return f"""You are a QA tester using Zene Studio (a DAW) through its real GUI on a private virtual screen, exactly as
a person would: mouse and keyboard only, looking at the screen. The app is already running; do not start or stop it.
Your only hands and eyes are this command:

  G="{G}"
  $G shot shot-N.png         then LOOK at the image with your image viewing tool (number them 1, 2, 3, ...)
  $G click X Y [right]  |  $G dclick X Y  |  $G drag X1 Y1 X2 Y2  |  $G move X Y  |  $G scroll X Y up|down N
  $G type TEXT  |  $G key Escape | Enter | Tab | space | Ctrl+S | ...   |  $G log   (the app's own log, for errors)

The screen is 1600x1000; coordinates are pixels from the top-left. Take a screenshot after every action and check it
before the next. If a command prints an error or the app stops answering, say so and note it - never assume an action
worked. If a use cannot be done the obvious way, try what a reasonable user would try (another menu, a right-click, a
shortcut) before calling it a FAIL, and say what you tried. Do not directly edit source, configuration or project
files using shell commands. You MAY save and reopen test projects through Zene's GUI inside this run folder or the
app's temporary HOME: testing Save necessarily creates a project file. Screenshots may also be created here.
Use fresh screenshot coordinates after every resize, panel switch or pop-out; never reuse an earlier position.
Do not click through another window: close or move overlapping test dialogs/panels before continuing. This virtual
screen has no window manager: a pop-out can be a separate undecorated window at the same position. Do not infer
failure merely from unchanged placement. Tiny tool/LED states need a full-resolution screenshot or a cropped view.
Work through the uses IN ORDER in one session (later uses build on earlier ones). Budget: about 160 actions in all;
use them - do not stop early. Give UNSURE only after at least three different reasonable attempts, and never mark a
use PASS that you did not see work with your own eyes on a screenshot.
Use NOT-TESTED (attempted=false) if the harness, missing prerequisites, instructions, or time prevented you from
performing the action. State the blocker; it remains incomplete coverage and is not evidence of a product defect.
An attempted product action that refuses, crashes or fails is still FAIL, not NOT-TESTED.

USES:
{steps}

Finish with a short human summary, then one JSON object per use on its own line, nothing else after them:
{{"use": "<id>", "verdict": "PASS|FAIL|UNSURE|NOT-TESTED", "attempted": true, "did": "<what you did>", "saw": "<what the screen showed>", "shots": [<numbers>], "log_errors": "<relevant lines from $G log, or empty>"}}
"""


def profile_uses(path, task_id=None):
    """Explicit Rune profiles supplement, and never replace, promoted-build sweeps."""
    data = json.loads(Path(path).read_text())
    profile = data.get('profile', '')
    if not re.fullmatch(r'[a-z0-9-]+', profile):
        raise ValueError('Invalid review profile identity')
    uses = data.get('uses', [])
    if task_id:
        uses = [u for u in uses if task_id in u.get('task_ids', [])]
    required = ('id', 'title', 'do', 'expect', 'core')
    if not uses or any(any(k not in u for k in required) for u in uses):
        raise ValueError('Review profile is empty or lacks observable acceptance criteria')
    if len({u['id'] for u in uses}) != len(uses):
        raise ValueError('Review scenario identities must be unique')
    return profile, uses


def run(train, binary, effort, uses_file=None, task_id=None):
    rundir = HERE / "runs" / train.split("/")[-1]
    if uses_file:
        profile, uses = profile_uses(uses_file, task_id)
        rundir = rundir / f"profile-{profile}-{time.time_ns()}"
    else:
        uses = json.loads(USES.read_text())["uses"]
    rundir.mkdir(parents=True, exist_ok=True)
    if uses_file:
        (rundir / 'review-profile.json').write_text(json.dumps(
            {'profile': profile, 'task_id': task_id, 'uses': uses, 'binary': binary,
             'train': train, 'scope': 'task-review; does not sign off the promoted-build sweeps'}, indent=2) + '\n')
    prompt = prompt_context.stamp(prompt_for(uses))
    (rundir / "PROMPT.md").write_text(prompt)
    sweep.log(rundir, f"session of {train} with {binary}: {len(uses)} uses")
    if not sweep.openai_wait(rundir):
        sweep.log(rundir, "OpenAI plan still paused; session skipped")
        return 1
    env = dict(os.environ, TERRA_GUI_STATE=str(rundir / "gui-state.json"))
    env.pop("LMMS_PLUGIN_DIR", None)
    stubs = rundir / "stubs"                    # no click may open the owner's browser
    stubs.mkdir(exist_ok=True)
    for name in ("xdg-open", "kde-open", "kde-open5", "gio", "firefox", "zen-browser", "sensible-browser"):
        p = stubs / name
        p.write_text(f'#!/bin/sh\necho "$(date +%T) {name} $*" >> "{rundir}/opened-urls.log"\nexit 0\n')
        p.chmod(0o755)
    env.update(PATH=f"{stubs}:{env.get('PATH', '')}", BROWSER=str(stubs / "xdg-open"))
    gui = [str(PILOT / "venv/bin/python"), str(PILOT / "gui.py")]
    r = subprocess.run(gui + ["start", binary, "--size", "1600x1000"], env=env, capture_output=True, text=True,
                       timeout=120)
    if r.returncode != 0:
        sweep.log(rundir, f"zene did not start: {(r.stderr or r.stdout)[-300:]}")
        sweep.notify(f"GUI session of {train}: Zene did not start")
        return 1
    started = time.time()
    (rundir / 'final.md').unlink(missing_ok=True)
    sweep.write_jsonl(rundir / 'needs-retest.jsonl', [])
    try:
        with open(rundir / "PROMPT.md") as stdin, open(rundir / "events.jsonl", "w") as ev, \
                open(rundir / "stderr.log", "w") as er:
            rc = subprocess.run(
                ["codex", "exec", "-m", "gpt-6.1-terra", "-c", f'model_reasoning_effort="{effort}"',
                 "-c", 'approval_policy="never"', "-s", "workspace-write",
                 "-c", "sandbox_workspace_write.network_access=true", "-C", str(rundir), "--skip-git-repo-check",
                 "--json", "-o", str(rundir / "final.md"), "-"],
                env=env, stdin=stdin, stdout=ev, stderr=er, timeout=TIMEOUT).returncode
    except subprocess.TimeoutExpired:
        rc = "timeout"
    finally:
        state = json.loads((rundir / "gui-state.json").read_text()) if (rundir / "gui-state.json").exists() else {}
        alive = False
        try:
            os.kill(state["pid"], 0)
            alive = True
        except (KeyError, ProcessLookupError):
            pass
        log_tail = Path(state["log"]).read_text(errors="replace")[-3000:] if state.get("log") else ""
        (rundir / "zene-log-tail.txt").write_text(log_tail)
        subprocess.run(gui + ["stop"], env=env, capture_output=True, timeout=60)
    sweep.log(rundir, f"terra rc={rc} in {int(time.time() - started)} s; zene alive at the end: {alive}")

    by_id = {u["id"]: u for u in uses}
    got = {}
    for line in (rundir / "final.md").read_text().splitlines() if (rundir / "final.md").exists() else []:
        line = line.strip().strip("`")
        if line.startswith('{"use"'):
            try:
                v = json.loads(line)
            except ValueError:
                continue
            if v.get("use") in by_id:
                got[v["use"]] = v
    results, verdicts = [], []
    for u in uses:
        v = got.get(u["id"], {"verdict": "NOT-TESTED", "attempted": False,
                              "saw": "Terra gave no verdict (ran out of budget or time)"})
        shots = [rundir / f"shot-{n}.png" for n in v.get("shots", []) if isinstance(n, int)]
        shot = next((str(s) for s in reversed(shots) if s.exists()), None)
        not_tested = (v.get('verdict') == 'NOT-TESTED' or v.get('attempted') is False
                      or not v.get('did') or not shot)
        cid = f"session|{u['id']}"
        results.append({"id": cid, "view": "session", "role": "use", "name": u["title"], "tooltip": "",
                        "expected": f"{u['do']} -> {u['expect']}",
                        "outcome": "not-exercised" if not_tested else "clicked",
                        "errors": [v["log_errors"]] if v.get("log_errors") else [], "pair": shot,
                        "log_tail": log_tail if (not alive or v.get("verdict") == "FAIL") else "", "core": u["core"]})
        verdicts.append({"id": cid, "verdict": 'UNSURE' if not_tested else v.get("verdict", "UNSURE"),
                         "saw": f"did: {v.get('did', '')}; saw: {v.get('saw', '')}"})
    if not alive:                              # the app died during the session: always a failure, always urgent
        results.append({"id": "session|crash", "view": "session", "role": "use", "name": "Zene kept running",
                        "tooltip": "", "expected": "Zene is still running at the end of the session",
                        "outcome": "crash-or-hang", "errors": [], "pair": None, "log_tail": log_tail, "core": True})
        verdicts.append({"id": "session|crash", "verdict": "FAIL", "saw": "Zene was not running at the end"})
    sweep.write_jsonl(rundir / "results.jsonl", results)
    sweep.write_jsonl(rundir / "verdicts.jsonl", verdicts)
    counts = {k: sum(v["verdict"] == k for v in verdicts) for k in ("PASS", "FAIL", "UNSURE")}
    sweep.log(rundir, f"session verdicts: {counts}")

    # the owner wrote these expectations: a mismatch is a bug by definition, and a core use the tester could not get
    # done (UNSURE) counts as not working - Luna only finds the cause
    untested = {r['id'] for r in results if r['outcome'] == 'not-exercised'}
    for v in verdicts:
        if (v["verdict"] == "UNSURE" and v['id'] not in untested
                and v["id"] in {f"session|{u['id']}" for u in uses if u["core"]}):
            v["verdict"] = "FAIL"
            v["saw"] = "a tester could not get this done: " + v["saw"]
    sweep.write_jsonl(rundir / "verdicts.jsonl", verdicts)
    counts = {k: sum(v['verdict'] == k for v in verdicts) for k in ('PASS', 'FAIL', 'UNSURE')}
    added, urgent = sweep.stage_triage(
        rundir, train, how="a tester working through a normal session by hand (the step and what they saw are in each item; screenshots in the run folder) - the expected outcome is the OWNER'S SPECIFICATION, so report what in the code makes Zene differ from it rather than whether the current behaviour was intended -", force_real=True)
    # a failed core use is urgent even if Luna did not mark it so (owner: "if something doesnt work it should raise it")
    core_fail = {f"session|{u['id']}" for u in uses if u["core"]} | {"session|crash"}
    for row in added:
        if row["control_id"] in core_fail and not row["urgent"]:
            row["urgent"] = True
            with open(sweep.URGENT, "a") as f:
                f.write(f"{row['cluster']}\t{row['title'][:120]}\n")
            urgent.append(row)
    failed = [v["id"].split("|")[1] for v in verdicts if v["verdict"] == "FAIL"]
    (rundir / "SUMMARY.md").write_text(
        f"# GUI session {train} ({datetime.date.today()})\n\n- {counts}\n- failed: {', '.join(failed) or 'none'}\n"
        f"- {len(added)} bugs queued ({len(urgent)} urgent): " + ", ".join(r["cluster"] for r in added) + "\n")
    sweep.notify(f"GUI session {train.split('/')[-1]}: {counts['PASS']}/{len(uses)} uses work"
                 + (f"; failed: {', '.join(failed)}" if failed else "") + f" -> {len(added)} bugs queued"
                 + (f", {len(urgent)} URGENT" if urgent else "") + ".")
    incomplete = sweep.incomplete_checks(results, {v['id']: v for v in verdicts})
    with open(rundir / 'SUMMARY.md', 'a') as summary:
        summary.write(f"- Incomplete checks (not product passes): {len(incomplete)}: {', '.join(incomplete)}\n")
    return 2 if incomplete or sweep.jsonl(rundir / 'needs-retest.jsonl') or rc != 0 else 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stage", choices=["run"])
    ap.add_argument("--train", default=(sweep.ROOT / "FIX-BASE").read_text().strip())
    ap.add_argument("--binary", default=str(sweep.BINARY))
    ap.add_argument("--effort", default="high")
    ap.add_argument('--uses-file', help='Explicit task review profile; isolated from promoted-build sweep sign-off')
    ap.add_argument('--task-id', help='Run only scenarios associated with this task in --uses-file')
    a = ap.parse_args()
    sys.path.insert(0, str(Path.home() / '.local/share/zene-bunny-fix/qwen'))
    import terra_priority
    if a.task_id and not a.uses_file:
        ap.error('--task-id requires --uses-file')
    if a.uses_file:
        sys.exit(run(a.train, a.binary, a.effort, a.uses_file, a.task_id))
    sys.exit(terra_priority.sweep_run('session', a.train, run, a.binary, a.effort))
